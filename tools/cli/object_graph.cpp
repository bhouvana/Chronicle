#include "object_graph.hpp"

#include <chronicle/object_graph.hpp> // chronicle::object_name_of() -- the one shared splitting rule

#include <algorithm>

using namespace chronicle::io;

namespace chronicle_cli {

std::vector<ObjectGroup> group_by_object(LoadedSession const& session) {
    std::vector<ObjectGroup> groups;
    for (auto const& stream : session.streams) {
        auto const object = chronicle::object_name_of(stream.name);
        auto it = std::find_if(groups.begin(), groups.end(),
                                [&](ObjectGroup const& g) { return g.name == object; });
        if (it == groups.end()) {
            groups.push_back(ObjectGroup{object, {&stream}});
        } else {
            it->fields.push_back(&stream);
        }
    }
    return groups;
}

} // namespace chronicle_cli
