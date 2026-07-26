#include "narrate.hpp"

#include "json_util.hpp"
#include "object_graph.hpp"

#include <algorithm>
#include <map>

using chronicle::ContainerOpKind;
using namespace chronicle::io;

namespace chronicle_cli {

namespace {

// Mirrors chronicle::possible_race()'s exact logic (include/chronicle/race.hpp,
// ADR 0023) -- same two conditions, same default window -- applied to
// LoadedEvent's hlc/thread_hash fields directly, since this runs over
// already-loaded file data (LoadedEvent), not live HistoryRecord<T>.
// Deliberately not claiming a *new* race-detection mechanism: this is the
// same primitive, at the type-erasure boundary ADR 0008 already
// established chronicle-cli operates behind.
bool possibly_racing(LoadedEvent const& a, LoadedEvent const& b, std::uint64_t window_us) {
    if (!is_known(a.hlc) || !is_known(b.hlc)) {
        return false;
    }
    if (a.thread_hash == b.thread_hash) {
        return false;
    }
    std::uint64_t const diff = a.hlc.physical_us > b.hlc.physical_us ? a.hlc.physical_us - b.hlc.physical_us
                                                                      : b.hlc.physical_us - a.hlc.physical_us;
    return diff <= window_us;
}

std::string call_site_description(CallSiteInfo const& call_site) {
    if (!call_site.known()) {
        return "unknown call site";
    }
    auto const slash = call_site.file.find_last_of("/\\");
    std::string const filename = slash == std::string::npos ? call_site.file : call_site.file.substr(slash + 1);
    return filename + ":" + std::to_string(call_site.line);
}

} // namespace

void write_narration(LoadedSession const& session, std::string const& object_name, std::size_t position,
                      std::ostream& out) {
    auto const groups = group_by_object(session);
    auto const it = std::find_if(groups.begin(), groups.end(),
                                  [&](ObjectGroup const& g) { return g.name == object_name; });
    if (it == groups.end() || it->fields.empty()) {
        out << "no such object (or it has no recorded fields): " << object_name << "\n";
        return;
    }

    auto const merged = merge_object_history(*it);
    if (merged.entries.empty()) {
        out << "object has no recorded events: " << object_name << "\n";
        return;
    }
    std::size_t const cutoff = std::min(position, merged.entries.size() - 1);

    out << "narrative for " << object_name << " at position " << cutoff << " of "
        << (merged.entries.size() - 1) << ":\n\n";

    // -- current state + provenance (real, persisted data only) --
    // docs/adr/0039-persist-provenance-and-derivation.md: full call chains
    // and derivation explanations are now available offline when the
    // producing process opted a field into set_with_stacktrace()/
    // derive() -- this is the fuller Layer 10 narrative ADR 0038 said
    // would "fall out for free once persistence exists." Falls back to
    // the single persisted call site for every field that didn't opt in
    // (still the overwhelming common case).
    auto const snapshots = snapshot_fields(it->fields, merged, cutoff);
    for (auto const& snap : snapshots) {
        out << "  " << snap.field_name << ": " << (snap.recorded ? snap.rendered : "(not yet recorded)") << "\n";
        // Last event at/before cutoff for this specific field.
        LoadedEvent const* last_event = nullptr;
        for (std::size_t i = 0; i <= cutoff; ++i) {
            if (merged.entries[i].field_name == snap.field_name) {
                last_event = merged.entries[i].event;
            }
        }
        if (last_event == nullptr) {
            continue;
        }

        if (auto const* changes = session.derivation_for(snap.field_name, last_event->version)) {
            out << "    derived because:\n";
            for (auto const& change : *changes) {
                out << "      " << change.name << ": " << change.old_value << " -> " << change.new_value
                    << (change.changed ? " (changed)" : " (unchanged)") << "\n";
            }
        } else if (auto const* frames = session.provenance_for(snap.field_name, last_event->version)) {
            out << "    call chain (most recent frame first):\n";
            for (auto const& frame : *frames) {
                out << "      " << frame.description;
                if (!frame.source_file.empty()) {
                    auto const slash = frame.source_file.find_last_of("/\\");
                    std::string const filename =
                        slash == std::string::npos ? frame.source_file : frame.source_file.substr(slash + 1);
                    out << "  (" << filename << ":" << frame.source_line << ")";
                }
                out << "\n";
            }
        } else {
            out << "    last write: " << call_site_description(last_event->call_site) << "\n";
        }
    }

    // -- structural anomaly pass (docs/13-vision.md Layer 8) --
    std::map<std::string, std::uint64_t> max_version_at_cutoff;
    for (std::size_t i = 0; i <= cutoff; ++i) {
        auto& mv = max_version_at_cutoff[merged.entries[i].field_name];
        mv = std::max(mv, merged.entries[i].event->version);
    }
    bool printed_anomaly_header = false;
    for (auto const* field : it->fields) {
        if (field->shape == StreamShape::Scalar) {
            continue;
        }
        auto found = max_version_at_cutoff.find(field->name);
        if (found == max_version_at_cutoff.end()) {
            continue;
        }
        bool ever_shrunk = false;
        std::size_t size = 0;
        std::size_t max_size = 0;
        for (auto const& event : field->events) {
            if (event.version > found->second) {
                break;
            }
            switch (event.op_kind) {
                case ContainerOpKind::Insert: ++size; break;
                case ContainerOpKind::Erase:
                    if (size > 0) --size;
                    ever_shrunk = true;
                    break;
                case ContainerOpKind::Update: break;
                case ContainerOpKind::Clear:
                    if (size > 0) ever_shrunk = true;
                    size = 0;
                    break;
            }
            max_size = std::max(max_size, size);
        }
        if (!ever_shrunk && size >= 5) {
            if (!printed_anomaly_header) {
                out << "\n  structural notes:\n";
                printed_anomaly_header = true;
            }
            out << "    " << field->name << ": grew to " << size
                << " item(s), never shrunk as of this position -- possible leak (docs/13-vision.md Layer 8)\n";
        }
    }

    // -- cross-thread proximity pass (docs/13-vision.md Layer 3/6-lite) --
    constexpr std::uint64_t kRaceWindowUs = 1000;
    std::size_t const window_start = cutoff >= 5 ? cutoff - 5 : 0;
    bool printed_race_header = false;
    for (std::size_t i = window_start; i <= cutoff; ++i) {
        for (std::size_t j = i + 1; j <= cutoff; ++j) {
            if (merged.entries[i].field_name == merged.entries[j].field_name) {
                continue; // same-stream events are never a cross-thread race candidate
            }
            if (possibly_racing(*merged.entries[i].event, *merged.entries[j].event, kRaceWindowUs)) {
                if (!printed_race_header) {
                    out << "\n  possible races nearby (different threads, HLCs within " << kRaceWindowUs
                        << "us -- may have raced, not confirmed):\n";
                    printed_race_header = true;
                }
                out << "    " << merged.entries[i].field_name << " (position " << i << ") vs. "
                    << merged.entries[j].field_name << " (position " << j << ")\n";
            }
        }
    }
}

void write_narration_json(LoadedSession const& session, std::string const& object_name, std::size_t position,
                           std::ostream& out) {
    auto const groups = group_by_object(session);
    auto const it = std::find_if(groups.begin(), groups.end(),
                                  [&](ObjectGroup const& g) { return g.name == object_name; });
    if (it == groups.end() || it->fields.empty()) {
        out << "{\"error\":\"no such object (or it has no recorded fields): " << json_escape(object_name)
            << "\"}\n";
        return;
    }
    auto const merged = merge_object_history(*it);
    if (merged.entries.empty()) {
        out << "{\"error\":\"object has no recorded events: " << json_escape(object_name) << "\"}\n";
        return;
    }
    std::size_t const cutoff = std::min(position, merged.entries.size() - 1);

    out << "{\"object\":\"" << json_escape(object_name) << "\",\"position\":" << cutoff << ",\"of\":"
        << (merged.entries.size() - 1) << ",\"fields\":[";

    auto const snapshots = snapshot_fields(it->fields, merged, cutoff);
    for (std::size_t idx = 0; idx < snapshots.size(); ++idx) {
        auto const& snap = snapshots[idx];
        if (idx != 0) out << ",";
        out << "{\"name\":\"" << json_escape(snap.field_name) << "\",\"value\":"
            << (snap.recorded ? "\"" + json_escape(snap.rendered) + "\"" : "null") << ",\"recorded\":"
            << (snap.recorded ? "true" : "false");

        LoadedEvent const* last_event = nullptr;
        for (std::size_t i = 0; i <= cutoff; ++i) {
            if (merged.entries[i].field_name == snap.field_name) {
                last_event = merged.entries[i].event;
            }
        }
        if (last_event == nullptr) {
            out << "}";
            continue;
        }

        if (auto const* changes = session.derivation_for(snap.field_name, last_event->version)) {
            out << ",\"derived_because\":[";
            for (std::size_t i = 0; i < changes->size(); ++i) {
                if (i != 0) out << ",";
                auto const& change = (*changes)[i];
                out << "{\"name\":\"" << json_escape(change.name) << "\",\"old_value\":\""
                    << json_escape(change.old_value) << "\",\"new_value\":\"" << json_escape(change.new_value)
                    << "\",\"changed\":" << (change.changed ? "true" : "false") << "}";
            }
            out << "]";
        } else if (auto const* frames = session.provenance_for(snap.field_name, last_event->version)) {
            out << ",\"call_chain\":[";
            for (std::size_t i = 0; i < frames->size(); ++i) {
                if (i != 0) out << ",";
                auto const& frame = (*frames)[i];
                out << "{\"description\":\"" << json_escape(frame.description) << "\",\"source_file\":\""
                    << json_escape(frame.source_file) << "\",\"source_line\":" << frame.source_line << "}";
            }
            out << "]";
        } else {
            bool const known = last_event->call_site.known();
            out << ",\"last_write\":{\"known\":" << (known ? "true" : "false");
            if (known) {
                out << ",\"file\":\"" << json_escape(last_event->call_site.file)
                    << "\",\"line\":" << last_event->call_site.line;
            }
            out << "}";
        }
        out << "}";
    }
    out << "],";

    // Structural anomaly pass -- same fold as write_narration()'s human-readable version.
    std::map<std::string, std::uint64_t> max_version_at_cutoff;
    for (std::size_t i = 0; i <= cutoff; ++i) {
        auto& mv = max_version_at_cutoff[merged.entries[i].field_name];
        mv = std::max(mv, merged.entries[i].event->version);
    }
    out << "\"structural_notes\":[";
    bool first_note = true;
    for (auto const* field : it->fields) {
        if (field->shape == StreamShape::Scalar) {
            continue;
        }
        auto found = max_version_at_cutoff.find(field->name);
        if (found == max_version_at_cutoff.end()) {
            continue;
        }
        bool ever_shrunk = false;
        std::size_t size = 0;
        for (auto const& event : field->events) {
            if (event.version > found->second) {
                break;
            }
            switch (event.op_kind) {
                case ContainerOpKind::Insert: ++size; break;
                case ContainerOpKind::Erase:
                    if (size > 0) --size;
                    ever_shrunk = true;
                    break;
                case ContainerOpKind::Update: break;
                case ContainerOpKind::Clear:
                    if (size > 0) ever_shrunk = true;
                    size = 0;
                    break;
            }
        }
        if (!ever_shrunk && size >= 5) {
            if (!first_note) out << ",";
            first_note = false;
            out << "{\"field\":\"" << json_escape(field->name) << "\",\"size\":" << size << "}";
        }
    }
    out << "],";

    constexpr std::uint64_t kRaceWindowUs = 1000;
    std::size_t const window_start = cutoff >= 5 ? cutoff - 5 : 0;
    out << "\"possible_races\":[";
    bool first_race = true;
    for (std::size_t i = window_start; i <= cutoff; ++i) {
        for (std::size_t j = i + 1; j <= cutoff; ++j) {
            if (merged.entries[i].field_name == merged.entries[j].field_name) {
                continue;
            }
            if (possibly_racing(*merged.entries[i].event, *merged.entries[j].event, kRaceWindowUs)) {
                if (!first_race) out << ",";
                first_race = false;
                out << "{\"field_a\":\"" << json_escape(merged.entries[i].field_name) << "\",\"position_a\":"
                    << i << ",\"field_b\":\"" << json_escape(merged.entries[j].field_name)
                    << "\",\"position_b\":" << j << "}";
            }
        }
    }
    out << "]}\n";
}

} // namespace chronicle_cli
