#pragma once

#include <cstdint>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>

#include "chronicle/stream.hpp"

#include <tracy/Tracy.hpp>

// docs/08-visualization.md's "Tracy bridge" interop item (v0.5): emit
// Chronicle mutation events as Tracy plot data points, live, when a Tracy
// profiler is connected -- so a team already running Tracy sees Chronicle-
// tracked field history inline in a tool they already trust, instead of
// Chronicle building a competing timeline UI (explicit non-goal, Phase 3).
//
// This is a genuinely separate, optional header: chronicle-core (stream.hpp
// et al.) has zero knowledge of Tracy and stays that way for every consumer
// who doesn't include this file. The core-library change this feature
// needed was Stream<T>::add_record_hook()/remove_record_hook() -- a small,
// fixed-size (kMaxRecordHooks) function-pointer extension point, real cost
// measured (not assumed) at every record() call regardless of use --
// see bench/RESULTS.md and docs/adr/0040-composable-record-hooks.md, which
// generalized this from set_record_hook()'s original single-slot design
// (ADR 0013's own choice) specifically so a Tracy-plotted field can also
// simultaneously feed a chronicle::derived::Derivation or any other
// consumer, instead of the two competing for one slot.
//
// Scope: plot data points for arithmetic T only, matching Tracy's actual
// PlotData() overload set (int64_t, float, double -- see
// tracy/client/TracyProfiler.hpp). docs/08 also mentions "zone messages" as
// an alternative for non-numeric mutations (structural tracked_vector<T>/
// tracked_map<K,V> changes, or non-arithmetic tracked<T> fields); not
// implemented in this pass -- a real follow-up, not attempted speculatively
// alongside the plot path this ADR (0013) verified end-to-end.

namespace chronicle::tracy_bridge {

// RAII handle: owns the plot's name string (Tracy retains only the
// `char const*` it's given, so that string must outlive every future
// record() call on the attached stream, not just the attach() call itself)
// and detaches the hook on destruction. Mirrors ADR 0011's
// TrackedType<T,...> handle -- an explicit, caller-owned object, not a
// global address-keyed registry, for the same reason: this codebase has
// already found and fixed the bug class that kind of registry invites (see
// ADR 0009).
template <typename T>
class PlotHandle {
public:
    static_assert(std::is_arithmetic_v<T>,
                   "chronicle::tracy_bridge::plot() requires an arithmetic field type -- "
                   "Tracy's PlotData() only accepts int64_t/float/double, see docs/adr/0013");

    // docs/adr/0040-composable-record-hooks.md: add_record_hook()/
    // remove_record_hook() instead of set_record_hook() -- lets a
    // Tracy-plotted field simultaneously feed a chronicle::derived::Derivation
    // (or anything else), which the old single-slot set_record_hook()
    // could not.
    PlotHandle(Stream<T>& stream, std::string name) : stream_(&stream), name_(std::move(name)) {
        TracyPlotConfig(name_.c_str(), tracy::PlotFormatType::Number, /*step=*/false, /*fill=*/true,
                         /*color=*/0);
        hook_handle_ = stream_->add_record_hook(&PlotHandle::on_record, this);
    }

    ~PlotHandle() {
        if (stream_ != nullptr) {
            stream_->remove_record_hook(hook_handle_);
        }
    }

    PlotHandle(PlotHandle const&) = delete;
    PlotHandle& operator=(PlotHandle const&) = delete;

    // Movable: the moved-from handle's destructor must not then detach the
    // hook the moved-to handle now owns -- removes `other`'s registration
    // (which still points at `other`'s soon-to-be-stale `this`) and
    // installs a fresh one under the moved-to object's real address.
    PlotHandle(PlotHandle&& other) noexcept
        : stream_(other.stream_), name_(std::move(other.name_)) {
        if (stream_ != nullptr) {
            stream_->remove_record_hook(other.hook_handle_);
            hook_handle_ = stream_->add_record_hook(&PlotHandle::on_record, this);
        }
        other.stream_ = nullptr;
    }
    PlotHandle& operator=(PlotHandle&&) = delete;

private:
    // TracyPlot(name, value) resolves to tracy::Profiler::PlotData(), whose
    // overload set is exactly {int64_t, float, double} -- passing a `T`
    // that isn't one of those exactly (e.g. plain `int`) is ambiguous, not
    // implicitly convertible, since int->int64_t and int->float are
    // equally-ranked standard conversions. Every integral T is explicitly
    // routed to the int64_t overload; every floating-point T keeps its own
    // width (float stays float, double stays double) rather than
    // collapsing both to double, so a float field's plot doesn't silently
    // pay a width it didn't ask for.
    static void on_record(void* context, T const& value, std::source_location const&) {
        auto* self = static_cast<PlotHandle*>(context);
        if constexpr (std::is_integral_v<T>) {
            TracyPlot(self->name_.c_str(), static_cast<std::int64_t>(value));
        } else {
            TracyPlot(self->name_.c_str(), value);
        }
    }

    Stream<T>* stream_;
    std::string name_;
    std::size_t hook_handle_ = 0;
};

// chronicle::tracy_bridge::plot(stream, "player_1.hp") -- attaches live
// Tracy plotting to `stream` for as long as the returned handle lives.
// [[nodiscard]]: a PlotHandle destroyed immediately (the return value
// discarded) detaches on the spot and plots nothing, which is never what a
// caller means by writing this call.
template <typename T>
[[nodiscard]] PlotHandle<T> plot(Stream<T>& stream, std::string name) {
    return PlotHandle<T>(stream, std::move(name));
}

} // namespace chronicle::tracy_bridge
