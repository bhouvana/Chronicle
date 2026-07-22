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

#include <algorithm>
#include <atomic>
#include <chronicle/chronicle.hpp>
#include <chrono>
#include <cstdio>
#include <cstddef>
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

void bench_untracked_assignment() {
    chronicle::tracked<int> value{0};
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    std::printf("%-52s %10.2f ns/op\n", "untracked tracked<int>::operator=", ns);
}

void bench_tracked_assignment_ring_window() {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::ring_window(1024), chronicle::OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_ring_window");
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    std::printf("%-52s %10.2f ns/op\n", "tracked<int>::operator= (RingWindow 1024)", ns);
}

void bench_tracked_assignment_unbounded() {
    chronicle::Session session(chronicle::Session::Config{
        chronicle::RetentionPolicy::unbounded(), chronicle::OverflowPolicy::DropOldest, 64});
    chronicle::tracked<int> value{0};
    chronicle::track(value, session, "bench.tracked_unbounded");
    int counter = 0;
    double const ns = time_ns_per_op(1'000'000, [&] {
        value = counter++;
        g_sink = value.get();
    });
    std::printf("%-52s %10.2f ns/op\n", "tracked<int>::operator= (Unbounded, undrained)", ns);
}

void bench_history_query(std::size_t log_size) {
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
    std::printf("%-52s %10.2f ns/op (%.3f ns/event)\n", label, ns,
                ns / static_cast<double>(std::max<std::size_t>(log_size, 1)));
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
void bench_contended_multithreaded_assignment(int num_threads) {
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
    std::printf("%-52s %10.2f ns/op (aggregate)\n", label, total_ns / total_ops);
}

} // namespace

int main() {
    std::printf("chronicle-bench -- lock-free ring buffer (docs/adr/0009), see bench/RESULTS.md\n");
    std::printf("%-52s %10s\n", "benchmark", "result");
    std::printf("--------------------------------------------------------------------------\n");

    bench_untracked_assignment();
    bench_tracked_assignment_ring_window();
    bench_tracked_assignment_unbounded();
    bench_history_query(10);
    bench_history_query(1'000);
    bench_history_query(100'000);
    bench_contended_multithreaded_assignment(1);
    bench_contended_multithreaded_assignment(2);
    bench_contended_multithreaded_assignment(4);
    bench_contended_multithreaded_assignment(8);

    return 0;
}
