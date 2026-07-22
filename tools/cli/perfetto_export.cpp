#include "perfetto_export.hpp"

#include <cstdio>
#include <sstream>
#include <string>

namespace chronicle_cli {

namespace {

// Small, deliberate duplication of html_export.cpp's json_escape() --
// keeping this exporter self-contained rather than coupling two otherwise-
// unrelated export paths to a shared internal helper, same reasoning
// html_export.hpp already gives for its own JS/C++ duplication.
std::string json_escape(std::string const& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Chrome/Perfetto counter args are numeric name/value pairs -- WireValue's
// String kind has no numeric representation, so scalar string streams fall
// back to instant events (see write_perfetto_export below) rather than
// forcing a fabricated number here.
double wire_value_as_number(chronicle::io::WireValue const& v) {
    using chronicle::io::WireKind;
    switch (v.kind) {
        case WireKind::Int64: return static_cast<double>(v.i);
        case WireKind::UInt64: return static_cast<double>(v.u);
        case WireKind::Double: return v.d;
        case WireKind::Bool: return v.b ? 1.0 : 0.0;
        case WireKind::String: return 0.0; // unreachable -- callers check is_numeric first
    }
    return 0.0;
}

bool is_numeric(chronicle::io::WireValue const& v) {
    return v.kind != chronicle::io::WireKind::String;
}

std::string json_wire_value_display(chronicle::io::WireValue const& v) {
    using chronicle::io::WireKind;
    switch (v.kind) {
        case WireKind::Int64: return std::to_string(v.i);
        case WireKind::UInt64: return std::to_string(v.u);
        case WireKind::Double: return std::to_string(v.d);
        case WireKind::Bool: return v.b ? "true" : "false";
        case WireKind::String: return "\"" + json_escape(v.s) + "\"";
    }
    return "null";
}

std::string op_kind_name(chronicle::ContainerOpKind k) {
    switch (k) {
        case chronicle::ContainerOpKind::Insert: return "insert";
        case chronicle::ContainerOpKind::Erase: return "erase";
        case chronicle::ContainerOpKind::Update: return "update";
        case chronicle::ContainerOpKind::Clear: return "clear";
    }
    return "?";
}

// ts is microseconds (verified against the Chrome/Catapult Trace Event
// Format spec, not assumed) -- Chronicle's own elapsed_ns is nanosecond-
// precision, so this is a real, one-way precision loss on export, not a
// round-trippable conversion. Fine for a visualization export: nobody
// re-imports a Perfetto trace back into Chronicle.
std::int64_t to_perfetto_ts(std::int64_t elapsed_ns) { return elapsed_ns / 1000; }

// A single synthetic pid for the whole exported session: Chronicle has no
// concept of an OS process id anywhere in its data model (a session is
// produced by one process, but its identity is never captured), so
// inventing anything more specific here would be fabricated, not derived
// data. `tid` is the real thing worth preserving -- every event already
// carries thread_hash (chronicle/io/format.hpp's thread_hash()), which
// becomes each stream's grouping in Perfetto's per-thread track view.
constexpr int kSyntheticPid = 1;

} // namespace

void write_perfetto_export(chronicle::io::LoadedSession const& session, std::ostream& out) {
    using chronicle::io::StreamShape;

    out << "[\n";
    bool first_event = true;
    auto emit_comma = [&] {
        if (!first_event) {
            out << ",\n";
        }
        first_event = false;
    };

    for (auto const& stream : session.streams) {
        std::string const name = json_escape(stream.name);

        if (stream.shape == StreamShape::Scalar) {
            for (auto const& event : stream.events) {
                emit_comma();
                auto const ts = to_perfetto_ts(event.elapsed_ns);
                if (is_numeric(event.value)) {
                    out << "  {\"name\":\"" << name << "\",\"cat\":\"chronicle\",\"ph\":\"C\",\"ts\":"
                        << ts << ",\"pid\":" << kSyntheticPid << ",\"args\":{\"" << name
                        << "\":" << wire_value_as_number(event.value) << "}}";
                } else {
                    // Scalar strings can't be counters -- an instant event
                    // with the value in args is the honest fallback, not a
                    // fabricated numeric encoding of a string.
                    out << "  {\"name\":\"" << name << "\",\"cat\":\"chronicle\",\"ph\":\"I\",\"ts\":"
                        << ts << ",\"pid\":" << kSyntheticPid << ",\"tid\":" << event.thread_hash
                        << ",\"s\":\"t\",\"args\":{\"value\":" << json_wire_value_display(event.value)
                        << "}}";
                }
            }
            continue;
        }

        // IndexedOp/KeyedOp: a structural change (insert/update/erase/clear)
        // has no natural numeric-counter representation, so every one of
        // these becomes an instant event carrying the op/key/value as args
        // -- an honest annotation, not a plotted line implying continuity
        // the data doesn't have.
        for (auto const& event : stream.events) {
            emit_comma();
            auto const ts = to_perfetto_ts(event.elapsed_ns);
            out << "  {\"name\":\"" << name << "\",\"cat\":\"chronicle\",\"ph\":\"I\",\"ts\":" << ts
                << ",\"pid\":" << kSyntheticPid << ",\"tid\":" << event.thread_hash
                << ",\"s\":\"t\",\"args\":{\"op\":\"" << op_kind_name(event.op_kind) << "\"";
            if (event.op_kind != chronicle::ContainerOpKind::Clear) {
                out << ",\"key\":" << json_wire_value_display(event.key_or_index);
            }
            if (event.op_kind == chronicle::ContainerOpKind::Insert ||
                event.op_kind == chronicle::ContainerOpKind::Update) {
                out << ",\"value\":" << json_wire_value_display(event.value);
            }
            out << "}}";
        }
    }

    out << "\n]\n";
}

} // namespace chronicle_cli
