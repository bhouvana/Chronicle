#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "chronicle/tracked.hpp"

// docs/13-vision.md's Layer 4 ("derived state"), docs/adr/0033-derived-state.md.
//
// Layer 4 named a real fork: an explicit reactive/signals API the caller
// opts fields into, or inferring dependencies from source via static
// analysis. This is the former -- the latter is a project on the scale of
// (or larger than) tools/codegen's Clang-LibTooling tool, a genuinely
// different undertaking, not attempted here.
//
// Built directly on Stream<T>::RecordHook (stream.hpp) -- "an optional
// observer invoked synchronously from record(), once per event" -- the
// same extension point the Tracy bridge (ADR 0013) already uses for a
// different consumer. No new recording mechanism, no change to record()'s
// hot path.
//
// SCOPE, STATED UP FRONT:
// - docs/adr/0040-composable-record-hooks.md: RecordHook is no longer
//   single-slot -- a dependency field already carrying a Tracy-bridge
//   hook (or any other hook) can now also feed a Derivation, via
//   add_record_hook()/remove_record_hook() instead of set_record_hook().
// - No derived-of-derived: a Derivation's dependencies must be plain
//   tracked<T> fields, not another Derivation's target. Avoids real,
//   harder problems (cycle detection, recomputation ordering across a
//   dependency graph) genuinely out of scope here.
// - In-process only, same as ADR 0032's provenance -- explain()'s output
//   is not persisted to the .chronicle wire format.

namespace chronicle::derived {

struct DependencyChange {
    std::string name;      // the dependency's stream name (dep.stream()->name())
    std::string old_value; // rendered before this recomputation
    std::string new_value; // rendered after this recomputation
    bool changed = false;
};

namespace detail {

template <typename T>
[[nodiscard]] std::string render(T const& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    } else {
        return "<?>"; // no generic renderer for this type -- honest placeholder, not a crash
    }
}

} // namespace detail

// Keyed by (target stream id, version) -- the same (stream_id, version)
// registry shape as chronicle::provenance::Registry (ADR 0032) and
// chronicle::interposition::Registry (ADR 0029): a third reuse of one
// established pattern, not a new one. StreamBase::id() (ADR 0032) is what
// makes this address-reuse-safe.
class Registry {
public:
    static Registry& instance() {
        static Registry registry;
        return registry;
    }

    void record(std::uint64_t stream_id, std::uint64_t version, std::vector<DependencyChange> changes) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[{stream_id, version}] = std::move(changes);
    }

    [[nodiscard]] std::optional<std::vector<DependencyChange>> find(std::uint64_t stream_id,
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
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<DependencyChange>> entries_;
};

// Owns the RecordHook attachments on every dependency's stream for as
// long as this object lives. Not copyable/movable (the hooks capture a
// raw `this`, same lifetime discipline stream.hpp's RecordHook doc
// comment already requires); construct via chronicle::derive() below and
// hold the returned handle for as long as auto-recomputation should keep
// happening -- same ownership discipline chronicle::track()'s returned
// Stream<T>& already requires of its Session.
template <typename Result, typename... Deps>
class Derivation {
public:
    using ComputeFn = std::function<Result(Deps const&...)>;

    Derivation(tracked<Result>& target, ComputeFn compute, tracked<Deps>&... deps)
        : target_(&target), compute_(std::move(compute)), deps_(&deps...),
          last_values_(deps.get()...) {
        attach_hooks(std::index_sequence_for<Deps...>{});
    }

    Derivation(Derivation const&) = delete;
    Derivation& operator=(Derivation const&) = delete;

    ~Derivation() { detach_hooks(std::index_sequence_for<Deps...>{}); }

private:
    template <std::size_t... I>
    void attach_hooks(std::index_sequence<I...>) {
        (attach_one<I>(), ...);
    }
    template <std::size_t... I>
    void detach_hooks(std::index_sequence<I...>) {
        (detach_one<I>(), ...);
    }

    static constexpr std::size_t kNotAttached = static_cast<std::size_t>(-1);

    template <std::size_t I>
    void attach_one() {
        using Dep = std::tuple_element_t<I, std::tuple<Deps...>>;
        auto* stream = std::get<I>(deps_)->stream();
        if (stream != nullptr) {
            hook_handles_[I] = stream->add_record_hook(&Derivation::hook_trampoline<I, Dep>, this);
        }
    }
    template <std::size_t I>
    void detach_one() {
        if (hook_handles_[I] == kNotAttached) {
            return;
        }
        auto* stream = std::get<I>(deps_)->stream();
        if (stream != nullptr) {
            stream->remove_record_hook(hook_handles_[I]);
        }
    }

    template <std::size_t I, typename Dep>
    static void hook_trampoline(void* context, Dep const&, std::source_location const&) {
        static_cast<Derivation*>(context)->recompute();
    }

    void recompute() {
        Result const new_value =
            std::apply([this](auto*... dep_ptrs) { return compute_(dep_ptrs->get()...); }, deps_);

        std::vector<DependencyChange> changes = build_changes(std::index_sequence_for<Deps...>{});
        last_values_ = std::apply(
            [](auto*... dep_ptrs) { return std::make_tuple(dep_ptrs->get()...); }, deps_);

        *target_ = new_value; // ordinary tracked<Result> assignment -- history()/snapshot() just work

        auto* target_stream = target_->stream();
        if (target_stream != nullptr) {
            Registry::instance().record(target_stream->id(), current_version(*target_), std::move(changes));
        }
    }

    template <std::size_t... I>
    std::vector<DependencyChange> build_changes(std::index_sequence<I...>) {
        std::vector<DependencyChange> changes;
        changes.reserve(sizeof...(Deps));
        (changes.push_back(one_change<I>()), ...);
        return changes;
    }

    template <std::size_t I>
    DependencyChange one_change() {
        auto* dep = std::get<I>(deps_);
        auto const& old_value = std::get<I>(last_values_);
        auto const& new_value = dep->get();
        std::string const dep_name = dep->stream() != nullptr ? dep->stream()->name() : std::string{"?"};
        return DependencyChange{dep_name, detail::render(old_value), detail::render(new_value),
                                 !(old_value == new_value)};
    }

    tracked<Result>* target_;
    ComputeFn compute_;
    std::tuple<tracked<Deps>*...> deps_;
    std::tuple<Deps...> last_values_;
    // One add_record_hook() handle per dependency, so detach_one() removes
    // exactly this Derivation's own slot -- not whatever else happens to
    // be attached to that stream (docs/adr/0040-composable-record-hooks.md).
    // kNotAttached for a dependency whose stream() was null at construction.
    std::array<std::size_t, sizeof...(Deps)> hook_handles_{
        [] {
            std::array<std::size_t, sizeof...(Deps)> handles;
            handles.fill(kNotAttached);
            return handles;
        }()};
};

} // namespace chronicle::derived

namespace chronicle {

// `compute` is deduced as its own type Fn -- NOT written as
// std::function<Result(Deps const&...)> directly -- because Deps is a
// pack shared with the trailing `tracked<Deps>&... deps` parameter.
// GCC and Clang apply the class-template-deduction rule (temp.deduct.call)
// to a std::function-typed parameter: the argument must literally be (or
// derive from) a std::function specialization, not merely convertible to
// one, so a plain lambda is rejected even with Result/Deps fully spelled
// out via explicit template arguments -- confirmed as a real, reproducible
// portability bug (MSVC alone accepts it; a standalone repro against
// local Clang 21.1.6 reproduced the exact failure and confirmed this
// fix). Converting Fn to ComputeFn ourselves, as an ordinary (non-deduced)
// constructor call below, sidesteps that rule entirely.
template <typename Result, typename... Deps, typename Fn>
[[nodiscard]] std::unique_ptr<derived::Derivation<Result, Deps...>> derive(
    tracked<Result>& target, Fn compute, tracked<Deps>&... deps) {
    return std::make_unique<derived::Derivation<Result, Deps...>>(
        target, std::function<Result(Deps const&...)>(std::move(compute)), deps...);
}

template <typename Result>
[[nodiscard]] std::optional<std::vector<derived::DependencyChange>> explain(tracked<Result> const& target,
                                                                             std::uint64_t version) {
    auto* stream = target.stream();
    if (stream == nullptr) {
        return std::nullopt;
    }
    return derived::Registry::instance().find(stream->id(), version);
}

template <typename Result>
[[nodiscard]] std::optional<std::vector<derived::DependencyChange>> explain(tracked<Result> const& target) {
    return explain(target, current_version(target));
}

} // namespace chronicle
