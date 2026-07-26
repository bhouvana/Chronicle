#include "object_graph.hpp"

#include <chronicle/object_graph.hpp> // chronicle::object_name_of() -- the one shared splitting rule

#include "replay.hpp"

#include <algorithm>
#include <map>

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

std::vector<FieldSnapshot> snapshot_fields(std::vector<LoadedStream const*> const& fields,
                                            MergedObjectHistory const& merged, std::size_t cutoff) {
    std::map<std::string, LoadedEvent const*> last_scalar;
    std::map<std::string, std::uint64_t> max_version;
    for (std::size_t i = 0; i <= cutoff && i < merged.entries.size(); ++i) {
        auto const& entry = merged.entries[i];
        if (entry.shape == StreamShape::Scalar) {
            last_scalar[entry.field_name] = entry.event;
        } else {
            auto& mv = max_version[entry.field_name];
            mv = std::max(mv, entry.event->version);
        }
    }

    std::vector<FieldSnapshot> result;
    result.reserve(fields.size());
    for (auto const* field : fields) {
        FieldSnapshot snap{field->name, field->shape, false, {}};
        if (field->shape == StreamShape::Scalar) {
            auto found = last_scalar.find(field->name);
            if (found != last_scalar.end()) {
                snap.recorded = true;
                snap.rendered = to_display_string(found->second->value);
            }
        } else {
            auto found = max_version.find(field->name);
            if (found != max_version.end()) {
                snap.recorded = true;
                if (field->shape == StreamShape::IndexedOp) {
                    auto const values = replay_indexed(*field, found->second);
                    std::string rendered = "[";
                    for (std::size_t i = 0; i < values.size(); ++i) {
                        if (i != 0) rendered += ", ";
                        rendered += to_display_string(values[i]);
                    }
                    rendered += "]";
                    snap.rendered = std::move(rendered);
                } else {
                    auto const kvs = replay_keyed(*field, found->second);
                    std::string rendered = "{";
                    for (std::size_t i = 0; i < kvs.size(); ++i) {
                        if (i != 0) rendered += ", ";
                        rendered += to_display_string(kvs[i].first) + ": " + to_display_string(kvs[i].second);
                    }
                    rendered += "}";
                    snap.rendered = std::move(rendered);
                }
            }
        }
        result.push_back(std::move(snap));
    }
    return result;
}

} // namespace chronicle_cli
