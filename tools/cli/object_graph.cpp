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

MergedObjectHistory merge_object_history(ObjectGroup const& group) {
    MergedObjectHistory result;
    result.ordered_by_hlc = true;
    for (auto const* field : group.fields) {
        for (auto const& event : field->events) {
            result.entries.push_back(MergedEntry{field->name, &event, field->shape});
            if (!is_known(event.hlc)) {
                result.ordered_by_hlc = false;
            }
        }
    }

    if (result.ordered_by_hlc) {
        std::sort(result.entries.begin(), result.entries.end(),
                  [](MergedEntry const& a, MergedEntry const& b) { return a.event->hlc < b.event->hlc; });
    } else {
        std::sort(result.entries.begin(), result.entries.end(), [](MergedEntry const& a, MergedEntry const& b) {
            return a.event->elapsed_ns < b.event->elapsed_ns;
        });
    }
    return result;
}

MergedObjectHistory merge_entire_session(LoadedSession const& session) {
    ObjectGroup everything{"(entire program)", {}};
    everything.fields.reserve(session.streams.size());
    for (auto const& stream : session.streams) {
        everything.fields.push_back(&stream);
    }
    return merge_object_history(everything);
}

} // namespace chronicle_cli
