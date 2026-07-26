#include "query.hpp"

#include <algorithm>

using namespace chronicle::io;

namespace chronicle_cli {

std::vector<StreamActivity> most_changed_streams(LoadedSession const& session) {
    std::vector<StreamActivity> activity;
    activity.reserve(session.streams.size());
    for (auto const& stream : session.streams) {
        activity.push_back(StreamActivity{stream.name, stream.events.size()});
    }
    std::stable_sort(activity.begin(), activity.end(),
                      [](StreamActivity const& a, StreamActivity const& b) { return a.event_count > b.event_count; });
    return activity;
}

std::vector<std::uint64_t> thread_index(LoadedSession const& session) {
    std::vector<std::uint64_t> threads;
    for (auto const& stream : session.streams) {
        for (auto const& event : stream.events) {
            if (std::find(threads.begin(), threads.end(), event.thread_hash) == threads.end()) {
                threads.push_back(event.thread_hash);
            }
        }
    }
    return threads;
}

MergedObjectHistory events_from_thread(LoadedSession const& session, std::uint64_t thread_hash) {
    // Reuses merge_entire_session() (object_graph.cpp, ADR 0036) rather
    // than a second merge/ordering implementation.
    auto merged = merge_entire_session(session);

    merged.entries.erase(std::remove_if(merged.entries.begin(), merged.entries.end(),
                                         [&](MergedEntry const& entry) {
                                             return entry.event->thread_hash != thread_hash;
                                         }),
                          merged.entries.end());
    return merged;
}

} // namespace chronicle_cli
