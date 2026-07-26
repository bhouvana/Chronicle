#!/usr/bin/env python3
"""Chronicle cost-model estimator (docs/12-future-research-topics.md topic 4,
docs/adr/0024-cost-model-tool.md).

Turns docs/09-performance.md's "measure after the fact" philosophy into a
"predict before you commit to tracking this field" estimate: given a
tracked<T> scalar field's expected write frequency, retention policy, and
whether Session::Config::causal_clock is enabled, reports the projected
hot-path cost and on-disk storage footprint -- built entirely from real,
already-measured constants, not new guesses.

Every number this script uses is either read from bench/baseline.json
(the project's own real chronicle-bench output, per docs/adr/0017's CI
gate) or a small set of on-disk-format constants measured directly against
a real written .chronicle file (see the "MEASURED CONSTANTS" section below
for exact methodology and how to reproduce them). Nothing here is a
theoretical/asymptotic estimate.

Scope, stated honestly (same bar as every other feature in this project):
only tracked<T> scalar fields are modeled. tracked_vector<T>/tracked_map<K,V>
have per-operation payloads of genuinely variable size (an inserted string,
a container element) that this tool cannot honestly reduce to one constant
without measuring the caller's actual payload sizes -- extending this to
containers is real, scoped future work, not attempted here.
"""

import argparse
import json
import sys

# -- MEASURED CONSTANTS --
#
# BASE_BYTES_PER_EVENT: measured directly, not assumed. Built a real Session
# with one tracked<int> field, recorded 1000 plain `field = value` mutations
# (no chronicle::set(), so call_site is unknown; causal_clock left at its
# default false, so hlc is unknown -- i.e. the common case bench/baseline.json's
# own tracked_assignment_* benchmarks also measure), wrote it through the
# real chronicle::io::SessionWriter to an actual file, and divided the
# resulting file size by 1000:
#
#   chronicle::Session session;
#   chronicle::tracked<int> health{100};
#   chronicle::track(health, session, "player.health");
#   for (int i = 0; i < 999; ++i) health = i;
#   // write via chronicle::io::SessionWriter, then stat the file
#
# Result: 81082 bytes / 1000 events = 81.082 bytes/event.
BASE_BYTES_PER_EVENT = 81.082

# HLC_EXTRA_BYTES_PER_EVENT: not measured empirically -- unlike the base
# constant above, this doesn't need to be, because it's a fixed, unconditional
# write with no variable-length component: chronicle/io/format.hpp's
# write_hlc() is exactly `write_u64(physical_us) + write_u64(logical)`,
# 8 + 8 = 16 bytes, every time, verified by reading that function directly
# (include/chronicle/io/format.hpp), not by running anything.
HLC_EXTRA_BYTES_PER_EVENT = 16.0

# Which bench/baseline.json key to read for a given (causal_clock, threads)
# combination. Single-threaded keys are named *_single_threaded; contended
# keys only exist in an aggregate form (docs/09-performance.md's storage
# drain philosophy -- see bench/RESULTS.md for why per-thread contended
# numbers aren't broken out further).
def _resolve_baseline_key(threads: int, causal_clock: bool) -> str:
    if threads <= 1:
        return "tracked_assignment_causal_clock_single_threaded" if causal_clock \
            else "tracked_assignment_ring_window_1024_single_threaded"
    if causal_clock:
        raise SystemExit(
            "error: bench/baseline.json has no contended causal_clock benchmark yet -- "
            "only tracked_assignment_causal_clock_single_threaded exists. Re-run with "
            "--threads 1, or add the contended measurement to chronicle-bench first "
            "rather than guessing at its cost."
        )
    nearest = min((1, 2, 4, 8), key=lambda n: abs(n - threads))
    return f"contended_record_{nearest}_threads_aggregate"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", default="bench/baseline.json", help="path to bench/baseline.json")
    parser.add_argument("--writes-per-sec", type=float, required=True,
                         help="expected write frequency for the field being evaluated")
    parser.add_argument("--duration-s", type=float, default=60.0,
                         help="projection horizon in seconds (default 60)")
    parser.add_argument("--threads", type=int, default=1,
                         help="concurrent producer threads writing this field (default 1)")
    parser.add_argument("--causal-clock", action="store_true",
                         help="model Session::Config::causal_clock enabled (ADR 0019)")
    retention_group = parser.add_mutually_exclusive_group(required=True)
    retention_group.add_argument("--ring-window", type=int, metavar="N",
                                  help="RetentionPolicy::ring_window(N) -- storage capped at N events")
    retention_group.add_argument("--unbounded", action="store_true",
                                  help="RetentionPolicy::unbounded() -- storage grows without bound")
    args = parser.parse_args()

    with open(args.baseline, encoding="utf-8") as f:
        baseline = json.load(f)["results_ns_per_op"]

    key = _resolve_baseline_key(args.threads, args.causal_clock)
    if key not in baseline:
        print(f"error: '{key}' not found in {args.baseline} -- baseline.json may be stale "
              f"relative to this tool. Re-run chronicle-bench and update it first.", file=sys.stderr)
        return 1
    ns_per_op = baseline[key]

    total_events = args.writes_per_sec * args.duration_s
    hot_path_total_ns = total_events * ns_per_op
    hot_path_total_s = hot_path_total_ns / 1e9

    bytes_per_event = BASE_BYTES_PER_EVENT + (HLC_EXTRA_BYTES_PER_EVENT if args.causal_clock else 0.0)

    print(f"Cost model for a tracked<T> scalar field")
    print(f"  write rate:        {args.writes_per_sec:g} writes/sec over {args.duration_s:g}s "
          f"= {total_events:,.0f} events")
    print(f"  hot-path source:   bench/baseline.json['{key}'] = {ns_per_op:.2f} ns/op "
          f"(threads={args.threads}, causal_clock={args.causal_clock})")
    print()
    print(f"Hot-path cost projection:")
    print(f"  total recording time added to producer thread(s): {hot_path_total_ns:,.0f} ns "
          f"(~{hot_path_total_s * 1000:.3f} ms over the {args.duration_s:g}s window)")
    print()
    print(f"Storage footprint projection:")
    print(f"  bytes/event: {bytes_per_event:.2f} "
          f"(measured base {BASE_BYTES_PER_EVENT:.2f}"
          f"{' + HLC ' + str(HLC_EXTRA_BYTES_PER_EVENT) if args.causal_clock else ''})")
    if args.ring_window is not None:
        retained_events = min(total_events, args.ring_window)
        capped_bytes = retained_events * bytes_per_event
        print(f"  retention: ring_window({args.ring_window}) -- storage is CAPPED, not unbounded")
        print(f"  steady-state on-disk/in-memory footprint: ~{capped_bytes:,.0f} bytes "
              f"({capped_bytes / 1024:.1f} KiB)")
        if total_events > args.ring_window:
            print(f"  note: {total_events:,.0f} events over {args.duration_s:g}s exceeds the "
                  f"{args.ring_window}-event window -- only the most recent {args.ring_window} survive.")
    else:
        total_bytes = total_events * bytes_per_event
        print(f"  retention: unbounded -- storage is NOT capped")
        print(f"  projected footprint over {args.duration_s:g}s: ~{total_bytes:,.0f} bytes "
              f"({total_bytes / (1024 * 1024):.2f} MiB)")
        print(f"  warning: unbounded retention means this grows without limit for as long as "
              f"the field is tracked -- re-run with a longer --duration-s to see the trend, "
              f"or reconsider ring_window() for this field.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
