#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "chronicle/session.hpp"

// docs/adr/0031-object-graph.md ("Layer 2" of the user's 10-layer vision
// for Chronicle, docs/13-vision.md). Makes "object" a first-class,
// queryable concept -- but derived honestly from the existing
// dot-separated stream-naming convention (`track(hp, session,
// "player.health")`), the exact grouping rule
// docs/adr/0016-interactive-browser-viewer.md's `renderObjectGraph()`
// already uses in the browser viewer's JS. This is NOT a new relationship
// model Chronicle explicitly tracks -- there is no API here for
// "Weapon references Player" or "this object was destroyed at v90"; both
// would need an explicit relationship-registration mechanism this project
// doesn't have, real separate future scope, not attempted here.

namespace chronicle {

// "player.health" -> "player"; "player.stats.mana" -> "player.stats"
// (splits on the LAST '.', same as ADR 0016's JS -- a name with more than
// one '.' groups by its immediate parent, not its root); "score" (no '.')
// -> "score" (its own single-field object). Kept as one free function so
// both the live-Session path below and tools/cli/object_graph.hpp's
// LoadedSession-based path can each call it and stay consistent with each
// other and with the browser viewer, without the two being able to share
// an actual implementation (one operates on C++ types, the other on
// already-serialized JS running in a browser).
[[nodiscard]] inline std::string object_name_of(std::string const& stream_name) {
    auto const dot = stream_name.find_last_of('.');
    return dot == std::string::npos ? stream_name : stream_name.substr(0, dot);
}

// Distinct object names derived from every stream currently created on
// `session`, in first-seen order (not sorted -- callers who want sorted
// output can sort themselves; this preserves the order streams were
// created in, which is often the more useful default for "what exists").
[[nodiscard]] inline std::vector<std::string> object_names(Session const& session) {
    std::vector<std::string> names;
    for (auto const& stream_name : session.stream_names()) {
        auto const object = object_name_of(stream_name);
        if (std::find(names.begin(), names.end(), object) == names.end()) {
            names.push_back(object);
        }
    }
    return names;
}

// Every stream name under `object` -- i.e. every stream whose
// object_name_of() equals `object` exactly. An object with no fields
// (a name nothing currently maps to) yields an empty vector, not an error:
// "objects" are derived from whatever streams happen to exist, not
// declared upfront, so there's nothing to validate `object` against.
[[nodiscard]] inline std::vector<std::string> field_names_of(Session const& session,
                                                              std::string const& object) {
    std::vector<std::string> fields;
    for (auto const& stream_name : session.stream_names()) {
        if (object_name_of(stream_name) == object) {
            fields.push_back(stream_name);
        }
    }
    return fields;
}

} // namespace chronicle
