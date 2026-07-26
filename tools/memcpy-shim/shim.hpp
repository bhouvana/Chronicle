#pragma once

#include <cstddef>

// docs/12-future-research-topics.md topic 3, docs/adr/0029-memcpy-interposition.md.
// Windows-only, Detours-based (Microsoft::Detours via vcpkg, no CMake
// package config -- see CMakeLists.txt's find_path/find_library, per
// detours' own vcpkg usage note). Opt-in, heavyweight, diagnostic-only:
// chronicle-core never depends on this; it's a separate executable/module
// a caller builds only when CHRONICLE_BUILD_MEMCPY_SHIM is turned on.

namespace chronicle::interposition {

// Installs a process-wide hook on the CRT's memcpy. Returns false if
// Detours failed to attach (e.g. already attached, or an incompatible
// binary). Not thread-safe to call concurrently with itself or
// uninstall_memcpy_hook() -- install once, near the start of main(),
// same discipline Stream<T>::set_record_hook() already documents for its
// own single-hook extension point.
[[nodiscard]] bool install_memcpy_hook();
[[nodiscard]] bool uninstall_memcpy_hook();

// Every memcpy call observed while the hook is installed, regardless of
// whether its destination overlapped a watched range -- this is what the
// original spike found fires on unrelated internal CRT calls too, the
// "firehose" problem chronicle::interposition::Registry exists to filter.
[[nodiscard]] std::size_t total_call_count();

// Only calls whose destination overlapped a chronicle::interposition::watch()-ed
// range -- the actual, filtered signal this module exists to produce.
[[nodiscard]] std::size_t watched_hit_count();

} // namespace chronicle::interposition
