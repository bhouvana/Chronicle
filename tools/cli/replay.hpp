#pragma once

#include <chronicle/io/loaded_session.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// Generic replay for IndexedOp/KeyedOp shapes, folding LoadedEvents up to a
// target version into materialized state -- possible only because the wire
// vocabulary is small and closed (wire.hpp); this cannot know what the
// *original* element type was, only that (say) it was an Int64. Shared
// between main.cpp's diff subcommand and diff_runs.cpp (docs/12 topic 5),
// which both need "replay this stream's ops up to some point" and shouldn't
// duplicate the fold logic to get it.

namespace chronicle_cli {

[[nodiscard]] inline std::vector<chronicle::io::WireValue> replay_indexed(
    chronicle::io::LoadedStream const& stream, std::uint64_t up_to_version) {
    using chronicle::ContainerOpKind;
    std::vector<chronicle::io::WireValue> result;
    for (auto const& event : stream.events) {
        if (event.version > up_to_version) {
            break;
        }
        auto const index = static_cast<std::size_t>(event.key_or_index.u);
        switch (event.op_kind) {
            case ContainerOpKind::Insert:
                if (index >= result.size()) {
                    result.push_back(event.value);
                } else {
                    result.insert(result.begin() + static_cast<std::ptrdiff_t>(index), event.value);
                }
                break;
            case ContainerOpKind::Update:
                if (index < result.size()) {
                    result[index] = event.value;
                }
                break;
            case ContainerOpKind::Erase:
                if (index < result.size()) {
                    result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
                }
                break;
            case ContainerOpKind::Clear:
                result.clear();
                break;
        }
    }
    return result;
}

[[nodiscard]] inline std::vector<std::pair<chronicle::io::WireValue, chronicle::io::WireValue>> replay_keyed(
    chronicle::io::LoadedStream const& stream, std::uint64_t up_to_version) {
    using chronicle::ContainerOpKind;
    std::vector<std::pair<chronicle::io::WireValue, chronicle::io::WireValue>> result;
    auto find_key = [&](chronicle::io::WireValue const& key) {
        return std::find_if(result.begin(), result.end(),
                             [&](auto const& kv) { return kv.first == key; });
    };
    for (auto const& event : stream.events) {
        if (event.version > up_to_version) {
            break;
        }
        switch (event.op_kind) {
            case ContainerOpKind::Insert:
            case ContainerOpKind::Update: {
                auto it = find_key(event.key_or_index);
                if (it != result.end()) {
                    it->second = event.value;
                } else {
                    result.emplace_back(event.key_or_index, event.value);
                }
                break;
            }
            case ContainerOpKind::Erase: {
                auto it = find_key(event.key_or_index);
                if (it != result.end()) {
                    result.erase(it);
                }
                break;
            }
            case ContainerOpKind::Clear:
                result.clear();
                break;
        }
    }
    return result;
}

} // namespace chronicle_cli
