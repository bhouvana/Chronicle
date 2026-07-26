#include "diff_runs.hpp"
#include "replay.hpp"

#include <chronicle/io/wire.hpp>

#include <algorithm>
#include <vector>

using namespace chronicle::io;

namespace chronicle_cli {

namespace {

std::string shape_name(StreamShape s) {
    switch (s) {
        case StreamShape::Scalar: return "scalar";
        case StreamShape::IndexedOp: return "indexed (vector)";
        case StreamShape::KeyedOp: return "keyed (map)";
    }
    return "?";
}

// Scalar streams: aligned by ordinal position within each run's own
// history, not by version (a per-run-instance counter with no cross-run
// meaning -- the same reason ADR 0019's HLC is explicitly scoped to one
// Session, not usable here either). Ordinal position is the one thing two
// independent runs of "the same scenario" can actually share: "the Nth
// recorded value of this field," which is exactly the semantic docs/12
// topic 5 asks for when it says object identity/addresses don't carry
// across runs.
bool diff_scalar_stream(LoadedStream const& a, LoadedStream const& b, std::ostream& out) {
    std::size_t const common = std::min(a.events.size(), b.events.size());
    std::size_t first_divergence = common;
    std::size_t divergence_count = 0;
    for (std::size_t i = 0; i < common; ++i) {
        if (a.events[i].value != b.events[i].value) {
            if (first_divergence == common) {
                first_divergence = i;
            }
            ++divergence_count;
        }
    }

    bool differs = divergence_count > 0 || a.events.size() != b.events.size();
    if (!differs) {
        return false;
    }

    out << "  " << a.name << " [scalar]: ";
    if (divergence_count > 0) {
        out << divergence_count << " of " << common << " aligned event(s) differ, first at position "
            << first_divergence << ": " << to_display_string(a.events[first_divergence].value) << " (run A) vs "
            << to_display_string(b.events[first_divergence].value) << " (run B)\n";
    } else {
        out << "identical for the first " << common << " event(s)\n";
    }
    if (a.events.size() != b.events.size()) {
        out << "    run A recorded " << a.events.size() << " event(s), run B recorded " << b.events.size()
            << " event(s)\n";
    }
    return true;
}

// IndexedOp/KeyedOp: op-by-op ordinal alignment would be noisy (the same
// final state is reachable via different op sequences), so this compares
// *final replayed state* only -- an honest, scoped choice, not an attempt
// at full structural cross-run diffing.
bool diff_indexed_stream(LoadedStream const& a, LoadedStream const& b, std::ostream& out) {
    auto const va = a.events.empty() ? std::vector<WireValue>{}
                                      : replay_indexed(a, a.events.back().version);
    auto const vb = b.events.empty() ? std::vector<WireValue>{}
                                      : replay_indexed(b, b.events.back().version);
    bool differs = false;
    std::size_t const common = std::min(va.size(), vb.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (va[i] != vb[i]) {
            if (!differs) out << "  " << a.name << " [indexed, final state]:\n";
            differs = true;
            out << "    [" << i << "]: " << to_display_string(va[i]) << " (run A) vs "
                << to_display_string(vb[i]) << " (run B)\n";
        }
    }
    if (va.size() != vb.size()) {
        if (!differs) out << "  " << a.name << " [indexed, final state]:\n";
        differs = true;
        out << "    run A final length " << va.size() << ", run B final length " << vb.size() << "\n";
    }
    return differs;
}

bool diff_keyed_stream(LoadedStream const& a, LoadedStream const& b, std::ostream& out) {
    auto const ma = a.events.empty() ? std::vector<std::pair<WireValue, WireValue>>{}
                                      : replay_keyed(a, a.events.back().version);
    auto const mb = b.events.empty() ? std::vector<std::pair<WireValue, WireValue>>{}
                                      : replay_keyed(b, b.events.back().version);
    bool differs = false;
    auto find_key = [](std::vector<std::pair<WireValue, WireValue>> const& m, WireValue const& key) {
        return std::find_if(m.begin(), m.end(), [&](auto const& kv) { return kv.first == key; });
    };
    for (auto const& [key, value] : ma) {
        auto it = find_key(mb, key);
        if (it == mb.end()) {
            if (!differs) out << "  " << a.name << " [keyed, final state]:\n";
            differs = true;
            out << "    " << to_display_string(key) << ": " << to_display_string(value) << " (run A) vs (absent, run B)\n";
        } else if (it->second != value) {
            if (!differs) out << "  " << a.name << " [keyed, final state]:\n";
            differs = true;
            out << "    " << to_display_string(key) << ": " << to_display_string(value) << " (run A) vs "
                << to_display_string(it->second) << " (run B)\n";
        }
    }
    for (auto const& [key, value] : mb) {
        if (find_key(ma, key) == ma.end()) {
            if (!differs) out << "  " << a.name << " [keyed, final state]:\n";
            differs = true;
            out << "    " << to_display_string(key) << ": (absent, run A) vs " << to_display_string(value)
                << " (run B)\n";
        }
    }
    return differs;
}

} // namespace

bool write_run_diff(LoadedSession const& run_a, LoadedSession const& run_b, std::ostream& out) {
    bool any_diff = false;

    for (auto const& stream : run_a.streams) {
        if (run_b.find(stream.name) == nullptr) {
            out << "  " << stream.name << ": only in run A\n";
            any_diff = true;
        }
    }
    for (auto const& stream : run_b.streams) {
        if (run_a.find(stream.name) == nullptr) {
            out << "  " << stream.name << ": only in run B\n";
            any_diff = true;
        }
    }

    for (auto const& a : run_a.streams) {
        auto const* b = run_b.find(a.name);
        if (b == nullptr) {
            continue;
        }
        if (a.shape != b->shape) {
            out << "  " << a.name << ": shape differs (run A: " << shape_name(a.shape) << ", run B: "
                << shape_name(b->shape) << ") -- cannot compare\n";
            any_diff = true;
            continue;
        }
        switch (a.shape) {
            case StreamShape::Scalar: any_diff |= diff_scalar_stream(a, *b, out); break;
            case StreamShape::IndexedOp: any_diff |= diff_indexed_stream(a, *b, out); break;
            case StreamShape::KeyedOp: any_diff |= diff_keyed_stream(a, *b, out); break;
        }
    }

    if (!any_diff) {
        out << "  (no differences found across " << run_a.streams.size() << " common stream(s))\n";
    }
    return any_diff;
}

} // namespace chronicle_cli
