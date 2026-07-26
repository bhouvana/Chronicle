#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <string>
#include <vector>

// docs/adr/0031-object-graph.md ("Layer 2"). File-based counterpart to
// chronicle::object_names()/field_names_of() (include/chronicle/object_graph.hpp),
// which only work on a live in-process Session. Reuses
// chronicle::object_name_of() directly (a plain std::string function with
// no Session dependency) rather than re-deriving the splitting rule a
// second time -- the browser viewer's JS (ADR 0016) is the one place this
// rule is unavoidably duplicated, since it runs in a browser, not C++.

namespace chronicle_cli {

struct ObjectGroup {
    std::string name;
    std::vector<chronicle::io::LoadedStream const*> fields;
};

// Groups every stream in `session` by chronicle::object_name_of(), in
// first-seen order (matching object_names()'s live-Session behavior).
[[nodiscard]] std::vector<ObjectGroup> group_by_object(chronicle::io::LoadedSession const& session);

} // namespace chronicle_cli
