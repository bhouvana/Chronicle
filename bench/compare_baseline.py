#!/usr/bin/env python3
"""chronicle-bench CI regression gate (docs/10-roadmap.md's v1.0 item,
docs/adr/0017-ci-performance-gate.md).

Compares a fresh `chronicle-bench --json` run against bench/baseline.json,
failing (non-zero exit) if any benchmark is more than `--tolerance` slower.

Tolerance defaults to 100% (2x) deliberately, not a smaller number: this
project's own measurements (bench/RESULTS.md's Tracy-bridge A/B test) found
run-to-run swings of ~30-40% from ambient system noise alone, on a single
dev workstation. bench/baseline.json was captured on that dev workstation,
not the CI runner this script actually runs on -- a shared/virtualized
GitHub Actions runner is typically noisier still, not quieter. A tight
tolerance here would fail most runs on noise, not signal, which defeats the
gate's purpose. This is a first, deliberately loose gate meant to catch
gross regressions (an accidental O(n^2), a reintroduced lock on the hot
path) -- not a substitute for bench/RESULTS.md's own honest, human-reviewed
comparisons for anything subtler. Revisit once a CI-native baseline
(captured on the runner itself, across several runs) exists to compare
against instead of a dev-machine one.

`--min-ns` (default 5.0) is a second, necessary guard found by actually
running this gate on GitHub's own runners (not anticipated in advance):
`untracked_assignment`'s baseline is 0.25 ns/op -- so close to zero that it
mostly measures steady_clock's resolution, not real work. The first real CI
run swung to 0.70 ns/op there, a "+178%" delta that FAILED the gate despite
being a sub-nanosecond difference no one could act on. Percentage tolerance
alone breaks down at magnitudes this close to the measurement floor: any
benchmark whose baseline is below `--min-ns` is reported but never flagged
as a regression, regardless of its percentage delta.
"""

import argparse
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fresh", help="path to a chronicle-bench --json output file")
    parser.add_argument("baseline", help="path to bench/baseline.json")
    parser.add_argument(
        "--tolerance",
        type=float,
        default=1.0,
        help="fraction slower than baseline allowed before failing (default 1.0 = 100%% = 2x)",
    )
    parser.add_argument(
        "--min-ns",
        type=float,
        default=5.0,
        help="baselines below this (ns/op) are reported but never flagged as a regression -- "
        "too close to timer resolution for a percentage delta to mean anything (default 5.0)",
    )
    args = parser.parse_args()

    with open(args.fresh, encoding="utf-8") as f:
        fresh = json.load(f)["results_ns_per_op"]
    with open(args.baseline, encoding="utf-8") as f:
        baseline = json.load(f)["results_ns_per_op"]

    regressions = []
    print(f"{'benchmark':<55} {'baseline':>12} {'fresh':>12} {'delta':>8}")
    print("-" * 90)
    for key, baseline_ns in baseline.items():
        if key not in fresh:
            print(f"{key:<55} {'(missing from fresh run)':>34}")
            continue
        fresh_ns = fresh[key]
        delta = (fresh_ns - baseline_ns) / baseline_ns if baseline_ns > 0 else 0.0
        below_floor = baseline_ns < args.min_ns
        flag = ""
        if delta > args.tolerance and not below_floor:
            regressions.append((key, baseline_ns, fresh_ns, delta))
            flag = "  <-- REGRESSION"
        elif delta > args.tolerance and below_floor:
            flag = "  (below --min-ns floor, not flagged)"
        print(f"{key:<55} {baseline_ns:>12.2f} {fresh_ns:>12.2f} {delta:>+7.1%}{flag}")

    print()
    if regressions:
        print(f"FAIL: {len(regressions)} benchmark(s) exceeded +{args.tolerance:.0%} tolerance:")
        for key, baseline_ns, fresh_ns, delta in regressions:
            print(f"  {key}: {baseline_ns:.2f} -> {fresh_ns:.2f} ns/op ({delta:+.1%})")
        return 1

    print(f"OK: all benchmarks within +{args.tolerance:.0%} of baseline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
