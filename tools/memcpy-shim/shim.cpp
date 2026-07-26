#include "shim.hpp"

#include <chronicle/interposition_registry.hpp>

#include <atomic>
#include <cstring>

// detours.h needs _AMD64_/_X86_/etc. (the Windows SDK's own architecture
// macros, defined by <windows.h> based on the compiler's _M_X64/_M_IX86),
// not just the compiler-level _M_* macros -- found by actually building
// this (a real "Unknown architecture" #error otherwise), not anticipated
// from reading Detours' docs. <windows.h> must come first.
#include <windows.h>
#include <detours/detours.h>

// Standard Detours attach/detach transaction pattern -- see Microsoft's own
// Detours samples; nothing novel in the hooking mechanics themselves, the
// real content here is DetourMemcpy checking the address-range registry
// before counting a "watched" hit.

namespace chronicle::interposition {

namespace {
using MemcpyFn = void* (*)(void*, void const*, std::size_t);
MemcpyFn TrueMemcpy = std::memcpy;

std::atomic<std::size_t> g_total_calls{0};
std::atomic<std::size_t> g_watched_hits{0};

void* DetourMemcpy(void* dst, void const* src, std::size_t size) {
    g_total_calls.fetch_add(1, std::memory_order_relaxed);
    if (Registry::instance().overlaps(dst, size)) {
        g_watched_hits.fetch_add(1, std::memory_order_relaxed);
    }
    return TrueMemcpy(dst, src, size);
}
} // namespace

bool install_memcpy_hook() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(TrueMemcpy), reinterpret_cast<PVOID>(DetourMemcpy));
    return DetourTransactionCommit() == NO_ERROR;
}

bool uninstall_memcpy_hook() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(TrueMemcpy), reinterpret_cast<PVOID>(DetourMemcpy));
    return DetourTransactionCommit() == NO_ERROR;
}

std::size_t total_call_count() { return g_total_calls.load(std::memory_order_relaxed); }
std::size_t watched_hit_count() { return g_watched_hits.load(std::memory_order_relaxed); }

} // namespace chronicle::interposition
