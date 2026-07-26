// chronicle-bench: the microbenchmark suite docs/09-performance.md and
// ADR 0004 both require before any hot-path cost claim is trusted. This is
// the "measure, don't assert" step -- numbers printed here are real
// measurements against the lock-free per-thread ring buffer implementation
// (docs/adr/0009-lock-free-ring-buffer.md), which replaced ADR 0004's
// mutex-staging-deque. See bench/RESULTS.md for the honest comparison: the
// single-threaded numbers below are NOT simply "faster than before" -- the
// per-call thread-local lookup the new design needs has real cost too, and
// the actual win only shows up under real concurrent contention (the
// bench_contended_multithreaded* benchmarks), which the mutex-based design
// could never have measured well in the first place.
//
// `--json`: machine-readable output for bench/compare_baseline.py
// (docs/10-roadmap.md's v1.0 CI performance-regression gate). Keys match
// bench/baseline.json's `results_ns_per_op` exactly -- both sides of that
// comparison were written to agree on names, not reconciled after the
// fact.

#include <algorithm>
#include <atomic>
#include <chronicle/chronicle.hpp>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {

// Optimization barrier: without an observable side effect, a loop whose
// only work is `value = counter++` on a local nobody reads afterward is
// legally dead code, and an optimizing compiler (correctly) removes it --
// which is exactly what happened to the first draft of this suite's
// untracked-assignment benchmark (it reported a fabricated 0.00 ns/op).
// Forcing a volatile write per iteration makes each iteration an observable
// side effect the compiler cannot elide, without adding a real memory
// fence/atomic cost that would itself distort the measurement.
volatile int g_sink = 0;

template <typename F>
double time_ns_per_op(std::size_t iterations, F&& f) {
    for (std::size_t i = 0; i < iterations / 10; ++i) {
        f();
    }
    auto const start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        f();
    }
    auto const end = std::chrono::steady_clock::now();
    double const total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    return total_ns / static_cast<double>(iterations);
}

struct BenchResult {
    std::string key;   // bench/baseline.json's results_ns_per_op key
    std::string label; // human-readable table row label
    double ns_per_op;
};

BenchResult bench_untracked_assignment() {
    chronicle::tracked<int> value{0};
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    return {"untracked_assignment", "untracked tracked<int>::operator=", ns};
}

BenchResult bench_tracked_assignment_ring_window() {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::ring_window(1024), chronicle::OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_ring_window");
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    return {"tracked_assignment_ring_window_1024_single_threaded",
            "tracked<int>::operator= (RingWindow 1024)", ns};
}

BenchResult bench_tracked_assignment_unbounded() {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::unbounded(), chronicle::OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_unbounded");
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    return {"tracked_assignment_unbounded_single_threaded",
            "tracked<int>::operator= (Unbounded, undrained)", ns};
}

// docs/adr/0019-hybrid-logical-clock.md: causal_clock adds an atomic CAS
// loop to record() when enabled (one branch, same cost model as
// Stream<T>::RecordHook, when disabled -- see the other tracked_assignment
// benchmarks above for that number). Measured here rather than assumed,
// same discipline as every other hot-path-adjacent addition in this
// codebase.
BenchResult bench_tracked_assignment_causal_clock() {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::ring_window(1024), chronicle::OverflowPolicy::DropOldest, 64, true});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_causal_clock");
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    return {"tracked_assignment_causal_clock_single_threaded",
            "tracked<int>::operator= (causal_clock enabled)", ns};
}

// docs/adr/0032-provenance-stacktrace.md: chronicle::set_with_stacktrace()
// captures a full std::stacktrace on every call -- a real, measured order
// of magnitude past causal_clock's cost above, which is exactly why it's a
// separate, differently-named, deliberate opt-in rather than a
// Session::Config flag. Fewer iterations than the other benchmarks
// (10,000 vs 1,000,000): this path is ~150-200x slower, so matching
// iteration counts would make this single benchmark dominate the whole
// suite's runtime for no extra precision this rare-use path needs.
BenchResult bench_set_with_stacktrace() {
    chronicle::Session session;
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_stacktrace");
    int counter = 0;
    double const ns = time_ns_per_op(10'000, [&] {
        chronicle::set_with_stacktrace(value, counter++);
        g_sink = value.get();
    });
    return {"set_with_stacktrace", "chronicle::set_with_stacktrace<int>()", ns};
}

BenchResult bench_history_query(std::size_t log_size) {
    // Unbounded retention + periodic drains so `log_size` events genuinely
    // land in the durable log (RingWindow's default 1024 cap would silently
    // truncate this and defeat the point of measuring at each size).
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::unbounded(), chronicle::OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.history_query");
    for (std::size_t i = 0; i < log_size; ++i) {
        value = static_cast<int>(i);
        if (i % 1000 == 999) {
            session.drain_all();
        }
    }
    session.drain_all();

    double const ns = time_ns_per_op(100, [&] {
        auto const hx = chronicle::history(value);
        if (hx.empty()) {
            std::abort(); // should never happen; guards against the call being optimized away
        }
    });
    char label[64];
    std::snprintf(label, sizeof(label), "history() over %zu events", log_size);
    char key[64];
    std::snprintf(key, sizeof(key), "history_query_%zu_events", log_size);
    return {key, label, ns};
}

// Aggregate throughput under real concurrent producers -- the scenario the
// lock-free ring buffer (docs/adr/0009) actually targets, unlike the
// single-threaded benchmarks above. Reports ns/op as (wall-clock time for
// all threads to finish) / (total operations across all threads), which is
// directly comparable to the single-threaded ns/op numbers: lower than
// those means real parallel speedup; higher means contention overhead is
// winning. A concurrent drainer thread runs throughout -- without one,
// DropNewest/DropOldest would spend the whole benchmark on their (rare, by
// design) overflow path instead of the fast one, which wouldn't reflect
// realistic sustained-throughput usage.
BenchResult bench_contended_multithreaded_assignment(int num_threads) {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::unbounded(), chronicle::OverflowPolicy::DropNewest, 64});
    auto& stream = session.create_stream<int>("bench.contended");
    constexpr std::size_t iterations_per_thread = 200'000;

    std::atomic<bool> stop{false};
    std::thread drainer([&] {
        while (!stop.load(std::memory_order_acquire)) {
            stream.drain();
        }
    });

    auto const start = std::chrono::steady_clock::now();
    std::vector<std::thread> producers;
    for (int t = 0; t < num_threads; ++t) {
        producers.emplace_back([&stream, t] {
            for (std::size_t i = 0; i < iterations_per_thread; ++i) {
                stream.record(static_cast<int>(static_cast<std::size_t>(t) * iterations_per_thread + i));
            }
        });
    }
    for (auto& th : producers) {
        th.join();
    }
    auto const end = std::chrono::steady_clock::now();
    stop.store(true, std::memory_order_release);
    drainer.join();

    double const total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double const total_ops = static_cast<double>(num_threads) * static_cast<double>(iterations_per_thread);
    char label[64];
    std::snprintf(label, sizeof(label), "contended record() across %d threads", num_threads);
    char key[64];
    std::snprintf(key, sizeof(key), "contended_record_%d_thread%s_aggregate", num_threads,
                  num_threads == 1 ? "" : "s");
    return {key, label, total_ns / total_ops};
}

std::string json_escape(std::string const& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

void print_human(std::vector<BenchResult> const& results) {
    std::printf("chronicle-bench -- lock-free ring buffer (docs/adr/0009), see bench/RESULTS.md\n");
    std::printf("%-52s %10s\n", "benchmark", "result");
    std::printf("--------------------------------------------------------------------------\n");
    for (auto const& r : results) {
        std::printf("%-52s %10.2f ns/op\n", r.label.c_str(), r.ns_per_op);
    }
}

void print_json(std::vector<BenchResult> const& results) {
    std::printf("{\"results_ns_per_op\":{");
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) {
            std::printf(",");
        }
        std::printf("\"%s\":%.4f", json_escape(results[i].key).c_str(), results[i].ns_per_op);
    }
    std::printf("}}\n");
}

} // namespace

int main(int argc, char** argv) {
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--json") {
            json = true;
        }
    }

    std::vector<BenchResult> results;
    results.push_back(bench_untracked_assignment());
    results.push_back(bench_tracked_assignment_ring_window());
    results.push_back(bench_tracked_assignment_unbounded());
    results.push_back(bench_tracked_assignment_causal_clock());
    results.push_back(bench_set_with_stacktrace());
    results.push_back(bench_history_query(10));
    results.push_back(bench_history_query(1'000));
    results.push_back(bench_history_query(100'000));
    results.push_back(bench_contended_multithreaded_assignment(1));
    results.push_back(bench_contended_multithreaded_assignment(2));
    results.push_back(bench_contended_multithreaded_assignment(4));
    results.push_back(bench_contended_multithreaded_assignment(8));

    if (json) {
        print_json(results);
    } else {
        print_human(results);
    }
    return 0;
}
