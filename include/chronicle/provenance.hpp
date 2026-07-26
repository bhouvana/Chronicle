#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <source_location>
#include <stacktrace>
#include <string>
#include <utility>
#include <vector>

#include "chronicle/tracked.hpp"

// docs/13-vision.md's Layer 3 ("provenance"), docs/adr/0032-provenance-stacktrace.md.
//
// last_writer()/call_site (docs/adr/0010) already answer "this value, from
// this line" -- one stack frame. This answers "this value, from this
// entire call chain," using C++23's std::stacktrace, verified directly
// (not assumed) to work with real symbol/file/line resolution on both
// compilers this project's CI matrix actually has (MSVC 19.44, Clang
// 21.1.6), given debug info (/Zi, -g).
//
// REAL, MEASURED COST: ~11,000 ns per capture (a 4-frame trace,
// 100,000-iteration microbenchmark on this project's own dev machine) --
// roughly 150-200x a plain tracked write (55-95 ns/op,
// bench/baseline.json). This is an order of magnitude past ADR 0019's HLC
// cost and rules out ever wiring this into record()'s hot path, even as an
// opt-in Session::Config flag the way causal_clock is. set_with_stacktrace()
// below is a deliberately separate, deliberately differently-named entry
// point -- never an overload of chronicle::set() -- so its cost is never
// paid by accident.
//
// REAL, DOCUMENTED LIMITATION: optimizers can inline whole frames away.
// A probe built for this feature showed two trivial one-line wrapper
// functions vanish entirely from the captured trace under /O2. A captured
// trace reflects the compiled binary's actual call graph, not a
// guaranteed 1:1 map to every function the source defines -- the same
// "never claim more precision than the mechanism actually has" bar every
// other blind spot in this project is held to (docs/04-technical-limitations.md).
//
// IN-PROCESS ONLY for this increment: not persisted to the .chronicle wire
// format. A real, separate future increment -- it would need its own
// format bump and a real decision about storing variable-length per-event
// data far larger than ADR 0024's ~81 bytes/event scalar baseline.

namespace chronicle::provenance {

// An immediately-converted, storable snapshot of one std::stacktrace_entry
// -- captured and copied out at the moment of the call, never holding
// onto the entry/platform handle itself.
struct StackFrame {
    std::string description;
    std::string source_file;
    std::uint32_t source_line = 0;
};

[[nodiscard]] inline std::vector<StackFrame> capture_current_stacktrace() {
    std::vector<StackFrame> frames;
    for (auto const& entry : std::stacktrace::current()) {
        frames.push_back(StackFrame{entry.description(), entry.source_file(),
                                     static_cast<std::uint32_t>(entry.source_line())});
    }
    return frames;
}

// Keyed by (stream id, version) -- StreamBase::id() (docs/adr/0032), not a
// raw Stream<T>* -- for the same address-reuse-safety reason ADR 0009's
// ring-buffer cache already established. Same mutex-guarded-map shape as
// chronicle::interposition::Registry (docs/adr/0029): a deliberately
// reused pattern, not a new one.
class Registry {
public:
    static Registry& instance() {
        static Registry registry;
        return registry;
    }

    void record(std::uint64_t stream_id, std::uint64_t version, std::vector<StackFrame> frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[{stream_id, version}] = std::move(frames);
    }

    [[nodiscard]] std::optional<std::vector<StackFrame>> find(std::uint64_t stream_id,
                                                                std::uint64_t version) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find({stream_id, version});
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<StackFrame>> entries_;
};

} // namespace chronicle::provenance

namespace chronicle {

// The deliberate, expensive, explicitly-named opt-in: captures a full
// call-chain snapshot alongside the existing call-site (docs/adr/0010),
// via the same field.assign() chronicle::set() already uses. Never an
// overload of set() -- see this header's cost note above for why.
template <typename T>
void set_with_stacktrace(tracked<T>& field, T value,
                          std::source_location call_site = std::source_location::current()) {
    auto frames = provenance::capture_current_stacktrace();
    field.assign(value, call_site);
    auto* stream = field.stream();
    if (stream != nullptr) {
        provenance::Registry::instance().record(stream->id(), current_version(field), std::move(frames));
    }
}

template <typename T>
[[nodiscard]] std::optional<std::vector<provenance::StackFrame>> provenance_of(tracked<T> const& field,
                                                                                std::uint64_t version) {
    auto* stream = field.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return provenance::Registry::instance().find(stream->id(), version);
}

// Convenience overload: "why is this value what it is right now."
template <typename T>
[[nodiscard]] std::optional<std::vector<provenance::StackFrame>> provenance_of(tracked<T> const& field) {
    return provenance_of(field, current_version(field));
}

} // namespace chronicle
