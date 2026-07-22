#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "chronicle/container_op.hpp"
#include "chronicle/map_op.hpp"
#include "chronicle/session.hpp"
#include "chronicle/stream.hpp"
#include "chronicle/tracked_type.hpp" // TrackedFieldsOf<T> -- reused, not reinvented

#include <entt/entt.hpp>

// chronicle-adapter-entt (docs/10-roadmap.md's v1.0 item): bridges an
// entt::registry's component lifecycle (construct/update/destroy) into
// Chronicle history, so an ECS-based program gets tracked fields without
// hand-writing a track() call at every mutation site the way non-ECS code
// does.
//
// Genuinely separate from chronicle-core, per docs/11-repository-structure-
// and-standards.md's "adapters/ -- reserved for adapters with a real
// external dependency" (unlike tracked_vector<T>/tracked_map<K,V>, which
// have no such dependency and live in include/chronicle/ per
// docs/adr/0006-container-tracking-lives-in-core-not-adapters.md). Nothing
// in chronicle-core includes this file or knows EnTT exists.
//
// Design: a component's fields, not its whole (usually non-serializable
// aggregate) value, are what gets tracked -- reusing
// TrackedFieldsOf<Component> (include/chronicle/tracked_type.hpp,
// docs/adr/0011-tracked-type-explicit-handle.md), the same reflection
// metadata CHRONICLE_TRACK_TYPE already provides, rather than inventing a
// second mechanism for "what are this struct's trackable fields." A
// component with no CHRONICLE_TRACK_TYPE registration is a compile error at
// track_component<Component>(), the same deliberate signal
// TrackedFieldsOf's undefined primary template already gives elsewhere.
//
// Wire shape: one Stream<MapOp<std::uint32_t, FieldType>> per field, keyed
// by entt::to_integral(entity) -- reusing the exact KeyedOp shape
// tracked_map<K,V> already produces (map_op.hpp), so chronicle-cli and the
// HTML viewer need zero changes to display entt-tracked components; they
// already know how to render a MapOp stream. Insert on construction (and
// for any entity that already has the component when tracking starts, same
// "history should never start empty" convention track_type()/track() use
// elsewhere), Update on entt's on_update signal, Erase on destruction --
// entt's own construct/update/destroy vocabulary maps directly onto
// ContainerOpKind with no translation needed. See
// docs/adr/0015-entt-adapter.md for the full account, including why the
// whole-component-value approach was rejected first.

namespace chronicle::adapters::entt {

using EntityKey = std::uint32_t;

template <typename Component, typename... FieldTypes>
class ComponentTracker {
public:
    ComponentTracker(::entt::registry& registry, std::tuple<FieldTypes Component::*...> members,
                      std::tuple<Stream<MapOp<EntityKey, FieldTypes>>*...> streams)
        : registry_(&registry), members_(members), streams_(streams) {
        for (auto entity : registry_->template view<Component>()) {
            record_all(registry_->template get<Component>(entity), ::entt::to_integral(entity),
                       ContainerOpKind::Insert, /*is_erase=*/false);
        }
        registry_->template on_construct<Component>().template connect<&ComponentTracker::on_insert>(*this);
        registry_->template on_update<Component>().template connect<&ComponentTracker::on_update>(*this);
        registry_->template on_destroy<Component>().template connect<&ComponentTracker::on_destroy>(*this);
    }

    // Signal connections capture `this` (see EnTT's sink::connect<Candidate>
    // (instance) -- the payload is a raw reference, not a value the sink
    // owns), so a relocated tracker would leave the registry pointing at a
    // stale address. Non-copyable, non-movable: construct one per component
    // type and hold onto it, the same lifetime discipline
    // chronicle::tracy_bridge::PlotHandle already established
    // (docs/adr/0013-tracy-bridge.md).
    ComponentTracker(ComponentTracker const&) = delete;
    ComponentTracker& operator=(ComponentTracker const&) = delete;
    ComponentTracker(ComponentTracker&&) = delete;
    ComponentTracker& operator=(ComponentTracker&&) = delete;

    // Escape hatch to a specific field's Stream, e.g. for history()/
    // last_writer()/session_writer.hpp -- same reasoning as
    // TrackedType<T,FieldTypes...>::stream<I>() in tracked_type.hpp: this
    // class intentionally doesn't wrap every Stream<T> query itself.
    template <std::size_t I>
    [[nodiscard]] Stream<MapOp<EntityKey, std::tuple_element_t<I, std::tuple<FieldTypes...>>>>& stream() const {
        return *std::get<I>(streams_);
    }

    ~ComponentTracker() {
        registry_->template on_construct<Component>().template disconnect<&ComponentTracker::on_insert>(*this);
        registry_->template on_update<Component>().template disconnect<&ComponentTracker::on_update>(*this);
        registry_->template on_destroy<Component>().template disconnect<&ComponentTracker::on_destroy>(*this);
    }

private:
    static void on_insert(ComponentTracker& self, ::entt::registry& registry, ::entt::entity entity) {
        self.record_all(registry.get<Component>(entity), ::entt::to_integral(entity), ContainerOpKind::Insert,
                         /*is_erase=*/false);
    }

    static void on_update(ComponentTracker& self, ::entt::registry& registry, ::entt::entity entity) {
        self.record_all(registry.get<Component>(entity), ::entt::to_integral(entity), ContainerOpKind::Update,
                         /*is_erase=*/false);
    }

    // Listeners fire *before* removal (entt::basic_registry::on_destroy's
    // documented guarantee), so registry.get<Component>(entity) below is
    // still valid -- but the recorded op itself carries no value
    // (is_erase=true), matching tracked_map<K,V>::erase()'s own
    // Erase-carries-no-value convention (map_op.hpp) exactly.
    static void on_destroy(ComponentTracker& self, ::entt::registry& registry, ::entt::entity entity) {
        self.record_all(registry.get<Component>(entity), ::entt::to_integral(entity), ContainerOpKind::Erase,
                         /*is_erase=*/true);
    }

    template <std::size_t I>
    void record_field(Component const& component, EntityKey key, ContainerOpKind kind, bool is_erase) {
        using FieldType = std::tuple_element_t<I, std::tuple<FieldTypes...>>;
        FieldType const value = is_erase ? FieldType{} : component.*std::get<I>(members_);
        std::get<I>(streams_)->record(MapOp<EntityKey, FieldType>{kind, key, value});
    }

    template <std::size_t... I>
    void record_all_impl(Component const& component, EntityKey key, ContainerOpKind kind, bool is_erase,
                          std::index_sequence<I...>) {
        (record_field<I>(component, key, kind, is_erase), ...);
    }

    void record_all(Component const& component, EntityKey key, ContainerOpKind kind, bool is_erase) {
        record_all_impl(component, key, kind, is_erase, std::index_sequence_for<FieldTypes...>{});
    }

    ::entt::registry* registry_;
    std::tuple<FieldTypes Component::*...> members_;
    std::tuple<Stream<MapOp<EntityKey, FieldTypes>>*...> streams_;
};

namespace detail {

template <typename Component, typename... FieldTypes, std::size_t... I>
[[nodiscard]] ComponentTracker<Component, FieldTypes...> make_component_tracker(
    ::entt::registry& registry, Session& session, std::string const& component_name,
    std::tuple<FieldTypes Component::*...> const& members,
    std::array<char const*, sizeof...(FieldTypes)> const& names, std::index_sequence<I...>) {
    std::tuple<Stream<MapOp<EntityKey, FieldTypes>>*...> streams{
        &session.create_stream<MapOp<EntityKey, FieldTypes>>(component_name + "." + names[I])...};
    return ComponentTracker<Component, FieldTypes...>(registry, members, streams);
}

} // namespace detail

// chronicle::adapters::entt::track_component<Position>(registry, session,
// "position") -- Component must already have a CHRONICLE_TRACK_TYPE(Component,
// field1, ...) registration (include/chronicle/tracked_type.hpp); this
// function is the entt-specific counterpart to chronicle::track_type(),
// driven by entt's signals instead of explicit set<I>() calls.
template <typename Component>
[[nodiscard]] auto track_component(::entt::registry& registry, Session& session, std::string component_name) {
    constexpr auto members = TrackedFieldsOf<Component>::members();
    constexpr auto names = TrackedFieldsOf<Component>::names();
    return detail::make_component_tracker(registry, session, component_name, members, names,
                                           std::make_index_sequence<std::tuple_size_v<decltype(members)>>{});
}

} // namespace chronicle::adapters::entt
