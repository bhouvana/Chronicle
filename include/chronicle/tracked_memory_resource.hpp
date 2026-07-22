#pragma once

#include <cstdint>
#include <memory_resource>
#include <source_location>
#include <string>

#include "chronicle/container_op.hpp"
#include "chronicle/map_op.hpp"
#include "chronicle/session.hpp"
#include "chronicle/stream.hpp"

// docs/10-roadmap.md's v2.0 "PMR allocator/arena adapter" item.
// docs/04-technical-limitations.md already answered *whether* this is
// possible: "Can every allocation be discovered? Yes, if the allocator is
// ours or hooked (operator new/delete, PMR, custom)" -- std::pmr's
// memory_resource is exactly that hook, and it's standard-library, not a
// third-party dependency.
//
// docs/11-repository-structure-and-standards.md's original layout sketch
// (and docs/adr/0006, written when tracked_vector<T> was being placed)
// both listed an allocator adapter under `adapters/allocator/`, reserved
// for "adapters with a real external dependency." Checked directly against
// that stated rule, not assumed to still apply: <memory_resource> is
// standard library (C++17), with no external dependency at all -- the same
// situation ADR 0006 itself found for tracked_vector<T> ("has no such
// dependency... put it in include/chronicle/ instead"). This header
// follows that same precedent and corrects the same category of mistake
// docs/11's original sketch made once already, rather than repeating it: it
// lives in include/chronicle/, not adapters/allocator/, and ships as part
// of chronicle-core.
//
// Design: an allocation event is naturally {address, size} -- Insert on
// allocate, Erase on deallocate -- exactly the shape map_op.hpp's
// MapOp<K,V>/ContainerOpKind already models for tracked_map<K,V>. Reusing
// it here (Stream<MapOp<std::uint64_t, std::uint64_t>>, keyed by the
// allocated address) means chronicle-cli and the HTML/live viewers need
// **zero changes** to display allocation history -- the same payoff
// ADR 0015 already got by reusing this exact shape for the EnTT adapter.

namespace chronicle {

using AllocationEvents = Stream<MapOp<std::uint64_t, std::uint64_t>>;

// Wraps an upstream std::pmr::memory_resource, recording every
// allocate/deallocate as a MapOp event keyed by the returned/freed address,
// valued by size in bytes. Attach to a std::pmr::polymorphic_allocator (or
// any PMR container) exactly like any other memory_resource:
//
//   chronicle::Session session;
//   chronicle::TrackedMemoryResource arena(std::pmr::get_default_resource(), session, "arena");
//   std::pmr::vector<int> v(&arena);
//
// Non-copyable, non-movable: memory_resource's own virtual-dispatch
// identity (do_is_equal) is tied to `this`, and relocating a resource
// containers are already using would be unsound regardless of anything
// Chronicle adds -- the same "construct once, hold onto it" discipline
// already established for chronicle::tracy_bridge::PlotHandle
// (docs/adr/0013-tracy-bridge.md) and
// chronicle::adapters::entt::ComponentTracker (docs/adr/0015-entt-adapter.md).
class TrackedMemoryResource : public std::pmr::memory_resource {
public:
    TrackedMemoryResource(std::pmr::memory_resource* upstream, Session& session, std::string name)
        : upstream_(upstream), stream_(&session.create_stream<MapOp<std::uint64_t, std::uint64_t>>(
                                      std::move(name))) {}

    TrackedMemoryResource(TrackedMemoryResource const&) = delete;
    TrackedMemoryResource& operator=(TrackedMemoryResource const&) = delete;
    TrackedMemoryResource(TrackedMemoryResource&&) = delete;
    TrackedMemoryResource& operator=(TrackedMemoryResource&&) = delete;

    [[nodiscard]] AllocationEvents& stream() const noexcept { return *stream_; }

private:
    // Allocate from upstream *first* -- the address is the MapOp key, so
    // there is nothing to record until upstream has actually handed one
    // back. A failed allocate() throws before this line is reached, same
    // as any other memory_resource -- nothing recorded for an allocation
    // that never happened.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* ptr = upstream_->allocate(bytes, alignment);
        stream_->record(MapOp<std::uint64_t, std::uint64_t>{
            ContainerOpKind::Insert, reinterpret_cast<std::uint64_t>(ptr), static_cast<std::uint64_t>(bytes)});
        return ptr;
    }

    // Record the Erase *before* physically freeing -- symmetric with
    // chronicle::adapters::entt::ComponentTracker's on_destroy ordering
    // (docs/adr/0015): once upstream_->deallocate() returns, `p` may
    // already be handed back out by a subsequent allocate(), so the log
    // entry for "this address was freed" must be attributable to this
    // deallocation, not a possible future one reusing the same address.
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        stream_->record(MapOp<std::uint64_t, std::uint64_t>{
            ContainerOpKind::Erase, reinterpret_cast<std::uint64_t>(p), static_cast<std::uint64_t>(bytes)});
        upstream_->deallocate(p, bytes, alignment);
    }

    // Standard practice for a resource with observable side effects
    // (cppreference's own memory_resource examples do the same): compares
    // equal only to itself, not to other wrappers around the same
    // upstream, since each instance tracks its own independent address set
    // -- deallocating through a *different* TrackedMemoryResource would
    // record an Erase this instance's stream never saw the matching Insert
    // for.
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    AllocationEvents* stream_;
};

} // namespace chronicle
