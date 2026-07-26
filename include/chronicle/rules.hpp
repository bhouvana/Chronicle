#pragma once

#include <functional>
#include <memory>
#include <source_location>
#include <vector>

#include "chronicle/tracked.hpp"

// docs/adr/0041-doctor-and-rules.md: "Did the program violate any
// expectations?" -- runtime verification, not just observability. One
// concept, two entry points sharing the same predicate:
//
//   chronicle::rules::check_rule(history, predicate)  -- offline, cold path,
//     scans an already-recorded Timeline<T> and returns every violation.
//   chronicle::rules::watch(field, predicate, callback) -- live, built on
//     the same Stream<T>::RecordHook mechanism chronicle::derive() and the
//     Tracy bridge use (composable since docs/adr/0040-composable-record-hooks.md
//     -- a watch can coexist with a derive() binding or a Tracy plot on
//     the same field, not compete with them for one slot).
//
// SCOPE, STATED UP FRONT:
// - Scalar tracked<T> fields only, same "scalar first" precedent as
//   ADR 0024's cost-model tool and ADR 0026's anomaly detection --
//   container rules ("inventory.size() <= 128") are real, scoped future
//   work, not attempted here.
// - Point predicates only ("is this value currently valid"), not temporal
//   ones ("queue.size() never decreases for 30s") -- a temporal rule needs
//   a real windowed evaluator, genuinely more scope, left as a named
//   future increment, not silently unsupported.
// - No CHRONICLE_RULE(expr) macro sugar: turning an arbitrary boolean
//   expression into a reactively-re-evaluated predicate is a real,
//   separate undertaking (closer to Catch2's expression-decomposition
//   macros) -- this ships the underlying mechanism a macro could be built
//   on top of later, not the macro itself.

namespace chronicle::rules {

template <typename T>
struct RuleViolation {
    std::uint64_t version = 0;
    T value{};
};

// Offline: scans an already-recorded history, returns every version whose
// value failed `predicate`. Cold path -- no cost to anything not calling
// this.
template <typename T, typename Predicate>
[[nodiscard]] std::vector<RuleViolation<T>> check_rule(Timeline<T> const& history, Predicate&& predicate) {
    std::vector<RuleViolation<T>> violations;
    for (auto const& record : history) {
        if (!predicate(record.value)) {
            violations.push_back(RuleViolation<T>{record.version, record.value});
        }
    }
    return violations;
}

// Live: owns a RecordHook attachment for as long as the returned handle
// lives (same ownership discipline as chronicle::derived::Derivation and
// chronicle::tracy_bridge::PlotHandle) -- fires `callback` synchronously
// from record(), once per violation, with the offending value and its
// call site.
template <typename T>
class Watch {
public:
    using Predicate = std::function<bool(T const&)>;
    using Callback = std::function<void(T const&, std::source_location const&)>;

    Watch(tracked<T>& field, Predicate predicate, Callback callback)
        : predicate_(std::move(predicate)), callback_(std::move(callback)) {
        auto* stream = field.stream();
        if (stream != nullptr) {
            stream_ = stream;
            hook_handle_ = stream_->add_record_hook(&Watch::on_record, this);
        }
    }

    Watch(Watch const&) = delete;
    Watch& operator=(Watch const&) = delete;

    ~Watch() {
        if (stream_ != nullptr) {
            stream_->remove_record_hook(hook_handle_);
        }
    }

private:
    static void on_record(void* context, T const& value, std::source_location const& call_site) {
        auto* self = static_cast<Watch*>(context);
        if (!self->predicate_(value)) {
            self->callback_(value, call_site);
        }
    }

    Stream<T>* stream_ = nullptr;
    std::size_t hook_handle_ = 0;
    Predicate predicate_;
    Callback callback_;
};

} // namespace chronicle::rules

namespace chronicle {

template <typename T>
[[nodiscard]] std::unique_ptr<rules::Watch<T>> watch(
    tracked<T>& field, std::function<bool(T const&)> predicate,
    std::function<void(T const&, std::source_location const&)> callback) {
    return std::make_unique<rules::Watch<T>>(field, std::move(predicate), std::move(callback));
}

} // namespace chronicle
