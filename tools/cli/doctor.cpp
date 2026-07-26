#include "doctor.hpp"

#include "query.hpp"

#include <algorithm>

using chronicle::ContainerOpKind;
using namespace chronicle::io;

namespace chronicle_cli {

std::vector<RaceFinding> detect_races(MergedObjectHistory const& merged, std::uint64_t window_us,
                                       std::size_t max_lookahead) {
    std::vector<RaceFinding> findings;
    if (!merged.ordered_by_hlc) {
        return findings; // no real HLC to compare -- nothing to honestly report
    }
    auto const& entries = merged.entries;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::size_t const end = std::min(entries.size(), i + 1 + max_lookahead);
        for (std::size_t j = i + 1; j < end; ++j) {
            if (entries[i].field_name == entries[j].field_name) {
                continue;
            }
            auto const& a = *entries[i].event;
            auto const& b = *entries[j].event;
            if (!is_known(a.hlc) || !is_known(b.hlc) || a.thread_hash == b.thread_hash) {
                continue;
            }
            std::uint64_t const diff =
                a.hlc.physical_us > b.hlc.physical_us ? a.hlc.physical_us - b.hlc.physical_us
                                                       : b.hlc.physical_us - a.hlc.physical_us;
            if (diff <= window_us) {
                findings.push_back(RaceFinding{entries[i].field_name, i, entries[j].field_name, j});
            }
        }
    }
    return findings;
}

std::vector<GrowthFinding> detect_growth_anomalies(std::vector<LoadedStream const*> const& fields,
                                                    std::size_t min_size_to_flag) {
    std::vector<GrowthFinding> findings;
    for (auto const* field : fields) {
        if (field->shape == StreamShape::Scalar) {
            continue;
        }
        bool ever_shrunk = false;
        std::size_t size = 0;
        for (auto const& event : field->events) {
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
        if (!ever_shrunk && size >= min_size_to_flag) {
            findings.push_back(GrowthFinding{field->name, size});
        }
    }
    return findings;
}

bool write_doctor_report(LoadedSession const& session, std::ostream& out) {
    auto const groups = group_by_object(session);
    out << "chronicle doctor\n";
    out << "  streams: " << session.streams.size() << "   objects: " << groups.size() << "\n\n";

    out << "hot fields (most events):\n";
    auto const activity = most_changed_streams(session);
    for (std::size_t i = 0; i < std::min<std::size_t>(5, activity.size()); ++i) {
        out << "  " << activity[i].name << "  " << activity[i].event_count << " event(s)\n";
    }
    out << "\n";

    auto const threads = thread_index(session);
    out << "threads: " << threads.size() << " distinct\n\n";

    out << "persisted call chains: " << session.provenance.size()
        << "   persisted derivation explanations: " << session.derivations.size() << "\n\n";

    bool any_empty = false;
    for (auto const& stream : session.streams) {
        if (stream.events.empty()) {
            if (!any_empty) {
                out << "streams with no recorded events:\n";
                any_empty = true;
            }
            out << "  " << stream.name << "\n";
        }
    }
    if (any_empty) {
        out << "\n";
    }

    auto const merged = merge_entire_session(session);
    auto const races = detect_races(merged);
    out << "possible races: " << races.size() << "\n";
    for (auto const& race : races) {
        out << "  " << race.field_a << " (position " << race.position_a << ") vs. " << race.field_b
            << " (position " << race.position_b << ")\n";
    }
    out << "\n";

    std::vector<LoadedStream const*> all_fields;
    all_fields.reserve(session.streams.size());
    for (auto const& stream : session.streams) {
        all_fields.push_back(&stream);
    }
    auto const growth = detect_growth_anomalies(all_fields);
    out << "possible leaks: " << growth.size() << "\n";
    for (auto const& finding : growth) {
        out << "  " << finding.field_name << ": " << finding.final_size << " item(s), never shrunk\n";
    }
    out << "\n";

    bool const healthy = races.empty() && growth.empty() && !any_empty;
    out << (healthy ? "OK: no issues found.\n" : "ISSUES FOUND -- see above.\n");
    return !healthy;
}

} // namespace chronicle_cli
