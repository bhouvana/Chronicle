#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "chronicle/event.hpp"
#include "chronicle/ring_buffer.hpp"
#include "chronicle/session.hpp"
#include "chronicle/snapshot.hpp"
#include "chronicle/timeline.hpp"

// docs/06-recording-model.md's operation-log-per-stream model, now backed
// by the lock-free per-thread ring buffer (docs/05-architecture.md's target
// design), closing the gap ADR 0004 deliberately left open until
// bench/RESULTS.md gave a real baseline to build against.
//
// Fast path (ring not full) is lock-free for every OverflowPolicy. The slow
// path (ring full) differs by policy:
//   - DropNewest: discard, still lock-free.
//   - Block: spin-retry against the same ring, still lock-free (a real
//     blocking policy now that a consumer-drained ring exists -- see
//     docs/rfc/0001's "Block should regain real meaning" note).
//   - DropOldest: a true lock-free single-producer overwrite-when-full ring
//     needs a seqlock-style per-slot generation scheme to stay race-free,
//     which this pass doesn't attempt (see docs/adr/0009). Instead,
//     DropOldest's overflow case takes a small per-stream mutex to safely
//     evict-then-push, and drain() takes that same mutex only when the
//     stream's policy is DropOldest -- everywhere else, drain() stays
//     lock-free too. The common case (ring has room) never touches this
//     mutex regardless of policy.
//
// For scalar T, every event's payload is already the full new value, so
// there is no separate snapshot list (see the prior version of this
// comment, unchanged from v0.1 -- ADR 0002's snapshot+log split becomes
// load-bearing once delta-encoded container payloads need it).

namespace chronicle {

template <typename T>
class Stream : public StreamBase {
public:
    Stream(Session& owner, std::string name)
        : session_(owner), name_(std::move(name)), ring_capacity_(compute_ring_capacity(owner)),
          id_(next_stream_id()) {}

    Stream(Stream const&) = delete;
    Stream& operator=(Stream const&) = delete;

    // Optional observer invoked synchronously from record(), once per event,
    // with the just-recorded value and its call site -- the hook this
    // stream's chronicle::tracy_bridge::PlotHandle (include/chronicle/
    // tracy_bridge.hpp) attaches itself to for live plotting, without
    // stream.hpp itself knowing anything about Tracy. A raw function
    // pointer + context, not std::function: the common case (no hook
    // attached, checked every record() call) must cost exactly one branch,
    // not touch a possibly-heap-allocated callable. Only one hook at a time
    // is supported deliberately -- this is a narrow, single-purpose
    // extension point, not a general pub/sub system chronicle-core doesn't
    // otherwise have a use for. Not synchronized against concurrent
    // record() calls on other threads: attach (set_record_hook with a
    // non-null hook) before, or detach (set_record_hook(nullptr, nullptr))
    // strictly after, any concurrent producer activity on this stream --
    // same lifetime discipline as constructing/destroying the Stream
    // itself, not a new hazard this introduces.
    using RecordHook = void (*)(void* context, T const& value, std::source_location const& call_site);

    void set_record_hook(RecordHook hook, void* context) noexcept {
        record_hook_ = hook;
        record_hook_context_ = context;
    }

    // call_site defaults to the *direct* caller's location, which is only
    // meaningful when this caller is a named function the user actually
    // wrote at their own call site (tracked_vector::push_back(), chronicle::
    // set(), a direct stream.record() call, ...). A wrapper that doesn't
    // forward its own captured location (tracked<T>::operator=, which
    // structurally can't capture one at all -- see docs/adr/0010) must pass
    // an explicit std::source_location{} here, not rely on this default,
    // or every plain assignment would misleadingly report this line inside
    // stream.hpp instead of "unknown."
    void record(T value, std::source_location call_site = std::source_location::current()) {
        // docs/adr/0019-hybrid-logical-clock.md: one branch when disabled
        // (the default) -- same cost model already measured negligible for
        // RecordHook's null check above. The atomic CAS loop only runs for
        // sessions that opted in.
        HlcTimestamp const hlc = session_.config().causal_clock ? session_.next_hlc_tick() : HlcTimestamp{};
        Event<T> event{
            std::move(value),
            next_version_.fetch_add(1, std::memory_order_relaxed),
            std::chrono::steady_clock::now(),
            std::this_thread::get_id(),
            call_site,
            hlc,
        };

        if (record_hook_ != nullptr) {
            record_hook_(record_hook_context_, event.value, event.call_site);
        }

        auto& ring = ring_for_current_thread();
        if (ring.try_push(std::move(event))) {
            return; // fast, lock-free path -- the overwhelmingly common case
        }

        switch (session_.config().overflow) {
            case OverflowPolicy::DropNewest:
                return; // event untouched by the failed try_push; just drop it
            case OverflowPolicy::Block:
                while (!ring.try_push(std::move(event))) {
                    std::this_thread::yield();
                }
                return;
            case OverflowPolicy::DropOldest: {
                std::lock_guard<std::mutex> lock(overflow_mutex_);
                if (ring.try_push(std::move(event))) {
                    return; // drain() made room between our failed try and the lock
                }
                Event<T> discard;
                ring.try_pop(discard);
                ring.try_push(std::move(event)); // guaranteed room: we're the sole producer
                return;
            }
        }
    }

    void drain() override { drain_impl(); }

    [[nodiscard]] Timeline<T> history() const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::vector<HistoryRecord<T>> records;
        records.reserve(log_.size());
        for (auto const& event : log_) {
            records.push_back(to_history_record(event));
        }
        return Timeline<T>(std::move(records));
    }

    // Best-effort: two events can carry the same std::chrono::steady_clock
    // reading (clock resolution vs. how fast back-to-back recorded
    // operations execute, especially under optimization -- confirmed in
    // practice, see docs/adr/0007-timestamp-ties-under-optimization.md).
    // Ties are resolved by including both, i.e. by version order, which is
    // the one ordering this stream can actually guarantee (ADR 0003).
    // Prefer snapshot_at_version() whenever the caller controls versions
    // (e.g. via current_version() immediately after a mutation) instead of
    // capturing a time_point across operations.
    [[nodiscard]] std::optional<Snapshot<T>> snapshot_at(
        std::chrono::steady_clock::time_point at) const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::optional<Snapshot<T>> result;
        for (auto const& event : log_) {
            if (event.timestamp > at) break;
            result = Snapshot<T>{event.value, event.version, event.timestamp};
        }
        return result;
    }

    // Exact: per-stream version order is the one ordering ADR 0003 actually
    // guarantees (unlike wall-clock time, which can tie -- see snapshot_at()
    // above). This is the mechanism to reach for whenever precision matters,
    // e.g. in tests or in any query that isn't inherently about wall-clock
    // time.
    [[nodiscard]] std::optional<Snapshot<T>> snapshot_at_version(std::uint64_t version) const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::optional<Snapshot<T>> result;
        for (auto const& event : log_) {
            if (event.version > version) break;
            result = Snapshot<T>{event.value, event.version, event.timestamp};
        }
        return result;
    }

    // docs/adr/0019-hybrid-logical-clock.md: unlike snapshot_at_version(),
    // meaningfully comparable *across different Stream<T>/Stream<U>
    // instances* sharing the same Session -- the actual cross-stream
    // capability version numbers cannot provide. Requires
    // Session::Config::causal_clock; events recorded before it was enabled
    // (or on a stream from a session that never enabled it) have hlc ==
    // HlcTimestamp{}, which never satisfies is_known() and is skipped.
    //
    // Deliberately a full scan, not an early-break loop like the other
    // snapshot_at*() methods above: computing this event's hlc and this
    // event's version are two separate operations (an atomic CAS on the
    // session's shared clock, then a fetch_add on this stream's own
    // version counter), not one atomic step. Under concurrent producers,
    // a thread can be preempted between the two, so a later-versioned
    // event can carry an *earlier* hlc than one that got its version
    // first -- log_ staying version-sorted (ADR 0009) does not guarantee
    // it stays hlc-sorted too. A full scan tracking the best-qualifying
    // event so far is correct regardless of that ordering; an early-break
    // loop here would not be. This is a cold query path, not record()'s
    // hot one, so the O(n) cost is the right trade for correctness.
    [[nodiscard]] std::optional<Snapshot<T>> snapshot_at_hlc(HlcTimestamp target) const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::optional<Snapshot<T>> result;
        HlcTimestamp best{};
        for (auto const& event : log_) {
            if (!is_known(event.hlc) || target < event.hlc) {
                continue;
            }
            if (!result.has_value() || best < event.hlc) {
                best = event.hlc;
                result = Snapshot<T>{event.value, event.version, event.timestamp};
            }
        }
        return result;
    }

    // The version of the most recently recorded (and drained) event, or 0 if
    // nothing has been recorded yet. Pair with snapshot_at_version() to
    // capture "state as of right now" without any clock involved at all.
    [[nodiscard]] std::uint64_t current_version() const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        return log_.empty() ? 0 : log_.back().version;
    }

    // docs/10-roadmap.md's v0.5 "causal-chain queries ('what last wrote this
    // value')" item: the full record of the most recent mutation, including
    // where it happened (see event.hpp's call_site). Cheaper than
    // history().back() since it never materializes the whole Timeline.
    [[nodiscard]] std::optional<HistoryRecord<T>> last_writer() const {
        drain_impl();
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_.empty()) {
            return std::nullopt;
        }
        return to_history_record(log_.back());
    }

    [[nodiscard]] std::string const& name() const noexcept override { return name_; }

private:
    static std::size_t compute_ring_capacity(Session const& owner) {
        auto const& retention = owner.config().retention;
        // Unbounded retention still needs a *finite* per-thread staging
        // ring; "unbounded" describes the drained log, not how much an
        // undrained producer may buffer before a drain has to happen. Same
        // 4096 default v0.1 used for its equivalent cap.
        //
        // +1: RingBuffer<T> keeps one slot permanently empty to distinguish
        // full from empty (see ring_buffer.hpp), so requesting exactly N
        // only guarantees N-1 usable slots unless we ask for one more.
        return (retention.is_unbounded() ? 4096 : retention.max_events()) + 1;
    }

    static std::uint64_t next_stream_id() {
        static std::atomic<std::uint64_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    // Each Stream<T> instance owns one ring buffer per thread that has ever
    // called record() on it. The cache below stores a non-owning pointer
    // per thread; Stream<T> (via ring_registry_) keeps the actual
    // RingBuffer<Event<T>> objects alive for as long as the Stream itself
    // lives, so the pointer stays valid regardless of when/whether the
    // producing thread exits.
    //
    // The cache is keyed by id_ (a globally unique, monotonically
    // increasing counter set once at construction), deliberately NOT by
    // `this`. An earlier version of this code cached by raw Stream<T>
    // address and was wrong: Session/Stream objects are very often
    // stack-local (every unit test in this project creates one per test
    // function), so sequential calls on the same thread routinely reuse the
    // same stack address for a *different* Stream<T> object. A pointer-keyed
    // cache would then resolve a brand-new stream to a previous, already-
    // destroyed one's dangling RingBuffer pointer -- a real, easily-triggered
    // use-after-free, not a rare edge case (this is exactly what caused the
    // heap corruption AddressSanitizer caught during this feature's own
    // testing; see docs/adr/0009-lock-free-ring-buffer.md). A monotonic ID
    // is never reused, so a new Stream<T> at a reused address always misses
    // the cache and gets a fresh RingBuffer, never a stale one.
    //
    // Residual, deliberately-accepted cost: a thread's cache entry for a
    // destroyed stream is never removed, so long-running threads that touch
    // many short-lived streams accumulate unused map entries over the
    // program's lifetime. A safety bug, fixed; this is "only" an unbounded
    // memory growth concern for a usage pattern (many short-lived Sessions
    // on one long-lived thread) this project doesn't yet have a real example
    // of -- worth a future ADR if it becomes one, not solved speculatively.
    RingBuffer<Event<T>>& ring_for_current_thread() const {
        thread_local std::unordered_map<std::uint64_t, RingBuffer<Event<T>>*> cache;
        auto it = cache.find(id_);
        if (it != cache.end()) {
            return *it->second;
        }

        auto owned = std::make_unique<RingBuffer<Event<T>>>(ring_capacity_);
        RingBuffer<Event<T>>* ptr = owned.get();
        {
            std::lock_guard<std::mutex> lock(ring_registry_mutex_);
            ring_registry_.push_back(std::move(owned));
        }
        cache.emplace(id_, ptr);
        return *ptr;
    }

    void drain_impl() const {
        std::vector<RingBuffer<Event<T>>*> rings;
        {
            std::lock_guard<std::mutex> lock(ring_registry_mutex_);
            rings.reserve(ring_registry_.size());
            for (auto const& ring : ring_registry_) {
                rings.push_back(ring.get());
            }
        }
        if (rings.empty()) {
            return;
        }

        // Only DropOldest's record()-side slow path ever pops from a ring
        // outside of drain() itself; holding overflow_mutex_ here is what
        // keeps that rare case from racing this (normal, far more frequent)
        // consumer. Every other policy never touches this mutex at all.
        bool const needs_overflow_lock = session_.config().overflow == OverflowPolicy::DropOldest;
        std::unique_lock<std::mutex> overflow_lock(overflow_mutex_, std::defer_lock);
        if (needs_overflow_lock) {
            overflow_lock.lock();
        }

        std::vector<Event<T>> staged;
        for (auto* ring : rings) {
            Event<T> event;
            while (ring->try_pop(event)) {
                staged.push_back(std::move(event));
            }
        }
        if (overflow_lock.owns_lock()) {
            overflow_lock.unlock();
        }
        if (staged.empty()) {
            return;
        }

        // Draining rings one at a time (all of thread A's events, then all
        // of thread B's) does not itself preserve global version order
        // across threads, even though each event's version was assigned
        // from a single atomic counter shared by the whole stream: the
        // counter only guarantees each fetch_add() returns a distinct,
        // unique value, not that a thread which *received* a smaller
        // version necessarily *pushes* it to its own ring before a thread
        // holding a larger version pushes to its ring -- OS scheduling
        // between "get a version number" and "make it visible" can still
        // reorder arrival. Concretely: this was caught by
        // tests/unit/concurrency_test.cpp failing its `sorted` check
        // roughly 1 run in 3 -- sorting only *this* drain's batch and
        // appending it to the end of log_ is not enough, because a later
        // drain can still collect a smaller version number that simply
        // hadn't been pushed yet during an earlier drain. The fix is to
        // merge each new (already internally sorted) batch into the
        // existing sorted log_, not append it -- std::inplace_merge keeps
        // log_ correctly ordered regardless of arrival order. See
        // docs/adr/0009-lock-free-ring-buffer.md.
        std::sort(staged.begin(), staged.end(),
                  [](Event<T> const& a, Event<T> const& b) { return a.version < b.version; });

        std::lock_guard<std::mutex> lock(log_mutex_);
        auto const merge_point = log_.insert(log_.end(), std::make_move_iterator(staged.begin()),
                                              std::make_move_iterator(staged.end()));
        std::inplace_merge(log_.begin(), merge_point, log_.end(),
                            [](Event<T> const& a, Event<T> const& b) { return a.version < b.version; });
        apply_retention();
    }

    void apply_retention() const {
        auto const& retention = session_.config().retention;
        if (retention.is_unbounded()) {
            return;
        }
        std::size_t const max_events = retention.max_events();
        if (log_.size() > max_events) {
            log_.erase(log_.begin(), log_.begin() + static_cast<std::ptrdiff_t>(log_.size() - max_events));
        }
    }

    Session& session_;
    std::string name_;
    std::size_t ring_capacity_;
    std::uint64_t id_;
    std::atomic<std::uint64_t> next_version_{0};

    RecordHook record_hook_ = nullptr;
    void* record_hook_context_ = nullptr;

    mutable std::mutex ring_registry_mutex_;
    mutable std::vector<std::unique_ptr<RingBuffer<Event<T>>>> ring_registry_;

    // See the switch in record() and drain_impl() above: only ever taken on
    // DropOldest's overflow slow path and by drain() for DropOldest streams.
    mutable std::mutex overflow_mutex_;

    mutable std::mutex log_mutex_;
    mutable std::vector<Event<T>> log_;
};

template <typename T>
Stream<T>& Session::create_stream(std::string name) {
    auto stream = std::make_unique<Stream<T>>(*this, std::move(name));
    Stream<T>& ref = *stream;
    streams_.push_back(std::move(stream));
    return ref;
}

} // namespace chronicle
