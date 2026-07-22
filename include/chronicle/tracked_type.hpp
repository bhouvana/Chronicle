#pragma once

#include <array>
#include <cstddef>
#include <source_location>
#include <string>
#include <tuple>
#include <utility>

#include "chronicle/session.hpp"
#include "chronicle/stream.hpp"

// docs/07-api-design.md's "Registration without wrapper types" path: for
// ABI-sensitive/POD structs that must stay byte-identical, `tracked<T>`
// isn't viable (it changes layout). CHRONICLE_TRACK_TYPE(Type, field1,
// field2, ...) is the alternative this file implements.
//
// This DEVIATES from docs/07's original sketch -- see
// docs/adr/0011-tracked-type-explicit-handle.md for the full account.
// Summary: the sketch implied `chronicle::set(p.seq, 42)` working from a
// bare field reference, which requires a global address-keyed registry
// (`&p.seq` -> its Stream) to recover "which stream does this address
// belong to." That is exactly the class of bug ADR 0009 already found and
// fixed for the ring buffer (stack addresses get reused across objects,
// so an address-keyed lookup can silently resolve to a stale entry). This
// implementation uses an explicit handle instead:
//
//   CHRONICLE_TRACK_TYPE(NetworkPacket, seq, latency);
//   NetworkPacket p{};
//   auto tracked = chronicle::track_type(p, session, "packet");
//   tracked.set<0>(42);      // writes p.seq = 42 AND records it
//   tracked.get<0>();        // reads p.seq
//
// Index-based, not name-based (`tracked.set<0>(...)`, not `.set_seq(...)`):
// generating per-field named methods would need the macro to emit a
// variable number of *method definitions*, not just a comma-separated
// argument list -- meaningfully more macro complexity for ergonomics only,
// not safety. Left as a possible follow-up if index-based access proves
// too unergonomic in practice; not attempted speculatively.

namespace chronicle {

// Specialized by CHRONICLE_TRACK_TYPE(Type, ...) below -- deliberately no
// primary definition, so using track_type<T>() without having invoked the
// macro for T is a compile error pointing at this line, not a silent
// no-op.
template <typename T>
struct TrackedFieldsOf;

template <typename T, typename... FieldTypes>
class TrackedType {
public:
    TrackedType(T& instance, std::tuple<FieldTypes T::*...> members,
                std::tuple<Stream<FieldTypes>*...> streams)
        : instance_(&instance), members_(members), streams_(streams) {}

    template <std::size_t I>
    void set(std::tuple_element_t<I, std::tuple<FieldTypes...>> value,
             std::source_location call_site = std::source_location::current()) {
        instance_->*std::get<I>(members_) = value;
        std::get<I>(streams_)->record(value, call_site);
    }

    template <std::size_t I>
    [[nodiscard]] std::tuple_element_t<I, std::tuple<FieldTypes...>> const& get() const {
        return instance_->*std::get<I>(members_);
    }

    // Escape hatch to the underlying Stream, e.g. for history()/last_writer()
    // -- TrackedType intentionally doesn't wrap every Stream<T> query itself.
    template <std::size_t I>
    [[nodiscard]] Stream<std::tuple_element_t<I, std::tuple<FieldTypes...>>>& stream() const {
        return *std::get<I>(streams_);
    }

private:
    T* instance_;
    std::tuple<FieldTypes T::*...> members_;
    std::tuple<Stream<FieldTypes>*...> streams_;
};

namespace detail {

template <typename T, typename... FieldTypes, std::size_t... I>
[[nodiscard]] TrackedType<T, FieldTypes...> make_tracked_type(
    T& instance, Session& session, std::string const& name,
    std::tuple<FieldTypes T::*...> const& members,
    std::array<char const*, sizeof...(FieldTypes)> const& names,
    std::source_location const& call_site, std::index_sequence<I...>) {
    std::tuple<Stream<FieldTypes>*...> streams{&session.create_stream<FieldTypes>(
        name + "." + names[I])...};
    // Record each field's current value as version 0, same rationale as
    // tracked<T>'s track(): history() should never start empty.
    (std::get<I>(streams)->record(instance.*std::get<I>(members), call_site), ...);
    return TrackedType<T, FieldTypes...>(instance, members, streams);
}

} // namespace detail

// docs/07's `chronicle::track(player, session, "player_1")` counterpart for
// the CHRONICLE_TRACK_TYPE path. Named track_type(), not track(), to avoid
// overload ambiguity with tracked<T>/tracked_vector<T>/tracked_map<K,V>'s
// track() overloads -- T here is the raw struct type, not a tracked<T>
// wrapper, and nothing about the call site disambiguates which is meant.
template <typename T>
[[nodiscard]] auto track_type(T& instance, Session& session, std::string name,
                               std::source_location call_site = std::source_location::current()) {
    constexpr auto members = TrackedFieldsOf<T>::members();
    constexpr auto names = TrackedFieldsOf<T>::names();
    return detail::make_tracked_type(instance, session, name, members, names, call_site,
                                      std::make_index_sequence<std::tuple_size_v<decltype(members)>>{});
}

} // namespace chronicle

// -- CHRONICLE_TRACK_TYPE(Type, field1, ..., fieldN) --  N in [1, 8].
// The classic preprocessor "count the arguments, dispatch to a numbered
// overload" pattern -- not clever, but easy to verify correct by reading
// it, which matters more here than brevity. 8 fields is a deliberate,
// documented v0.5 limit, not a fundamental one: extending it means adding
// more CHRONICLE_PP_MEMBER_N/CHRONICLE_PP_NAME_N rows, mechanically.

#define CHRONICLE_PP_EXPAND(x) x
#define CHRONICLE_PP_CONCAT_(a, b) a##b
#define CHRONICLE_PP_CONCAT(a, b) CHRONICLE_PP_CONCAT_(a, b)
#define CHRONICLE_PP_GET_9TH(a1, a2, a3, a4, a5, a6, a7, a8, N, ...) N
#define CHRONICLE_PP_COUNT(...) \
    CHRONICLE_PP_EXPAND(CHRONICLE_PP_GET_9TH(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1))

#define CHRONICLE_PP_MEMBER_1(Type, a1) &Type::a1
#define CHRONICLE_PP_MEMBER_2(Type, a1, a2) &Type::a1, &Type::a2
#define CHRONICLE_PP_MEMBER_3(Type, a1, a2, a3) &Type::a1, &Type::a2, &Type::a3
#define CHRONICLE_PP_MEMBER_4(Type, a1, a2, a3, a4) &Type::a1, &Type::a2, &Type::a3, &Type::a4
#define CHRONICLE_PP_MEMBER_5(Type, a1, a2, a3, a4, a5) \
    &Type::a1, &Type::a2, &Type::a3, &Type::a4, &Type::a5
#define CHRONICLE_PP_MEMBER_6(Type, a1, a2, a3, a4, a5, a6) \
    &Type::a1, &Type::a2, &Type::a3, &Type::a4, &Type::a5, &Type::a6
#define CHRONICLE_PP_MEMBER_7(Type, a1, a2, a3, a4, a5, a6, a7) \
    &Type::a1, &Type::a2, &Type::a3, &Type::a4, &Type::a5, &Type::a6, &Type::a7
#define CHRONICLE_PP_MEMBER_8(Type, a1, a2, a3, a4, a5, a6, a7, a8) \
    &Type::a1, &Type::a2, &Type::a3, &Type::a4, &Type::a5, &Type::a6, &Type::a7, &Type::a8

#define CHRONICLE_PP_NAME_1(a1) #a1
#define CHRONICLE_PP_NAME_2(a1, a2) #a1, #a2
#define CHRONICLE_PP_NAME_3(a1, a2, a3) #a1, #a2, #a3
#define CHRONICLE_PP_NAME_4(a1, a2, a3, a4) #a1, #a2, #a3, #a4
#define CHRONICLE_PP_NAME_5(a1, a2, a3, a4, a5) #a1, #a2, #a3, #a4, #a5
#define CHRONICLE_PP_NAME_6(a1, a2, a3, a4, a5, a6) #a1, #a2, #a3, #a4, #a5, #a6
#define CHRONICLE_PP_NAME_7(a1, a2, a3, a4, a5, a6, a7) #a1, #a2, #a3, #a4, #a5, #a6, #a7
#define CHRONICLE_PP_NAME_8(a1, a2, a3, a4, a5, a6, a7, a8) \
    #a1, #a2, #a3, #a4, #a5, #a6, #a7, #a8

#define CHRONICLE_PP_MEMBERS(Type, ...) \
    CHRONICLE_PP_EXPAND(CHRONICLE_PP_CONCAT(CHRONICLE_PP_MEMBER_, CHRONICLE_PP_COUNT(__VA_ARGS__))(Type, __VA_ARGS__))
#define CHRONICLE_PP_NAMES(...) \
    CHRONICLE_PP_EXPAND(CHRONICLE_PP_CONCAT(CHRONICLE_PP_NAME_, CHRONICLE_PP_COUNT(__VA_ARGS__))(__VA_ARGS__))

#define CHRONICLE_TRACK_TYPE(Type, ...)                                                          \
    template <>                                                                                  \
    struct chronicle::TrackedFieldsOf<Type> {                                                     \
        static constexpr auto members() { return std::make_tuple(CHRONICLE_PP_MEMBERS(Type, __VA_ARGS__)); } \
        static constexpr std::array<char const*, CHRONICLE_PP_COUNT(__VA_ARGS__)> names() {       \
            return {CHRONICLE_PP_NAMES(__VA_ARGS__)};                                              \
        }                                                                                          \
    }

// Marks a struct for `tools/codegen/chronicle-codegen` (docs/10-roadmap.md's
// v0.5 codegen tool) to generate a CHRONICLE_TRACK_TYPE(...) call for,
// listing every direct data member automatically:
//
//   struct CHRONICLE_TRACKABLE NetworkPacket { int seq; float latency; };
//
// Expands to `[[clang::annotate("chronicle::track")]]`, NOT the plain
// `[[chronicle::track]]` docs/10's roadmap text originally sketched --
// verified directly (an AST dump, not assumed) that Clang silently drops
// unrecognized attributes like `[[chronicle::track]]` without leaving any
// trace in the AST at all, which would make them invisible to any
// LibTooling-based scanner. `clang::annotate` is Clang's actual supported
// mechanism for exactly this "let external tools find marked declarations"
// use case, and does survive into the AST as an inspectable AnnotateAttr.
// A no-op under any compiler other than Clang (the attribute is simply
// unrecognized and ignored, same as any other vendor-scoped attribute) --
// the codegen tool itself requires Clang to run, but code carrying this
// marker compiles fine everywhere.
#define CHRONICLE_TRACKABLE [[clang::annotate("chronicle::track")]]
