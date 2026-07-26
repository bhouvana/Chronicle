// docs/12-future-research-topics.md topic 3, docs/adr/0029-memcpy-interposition.md.
//
// HONESTLY LABELED: this file is real, standard LD_PRELOAD-interposition
// C++ (the well-established `dlsym(RTLD_NEXT, ...)` pattern every
// LD_PRELOAD shim uses), but it has NOT been built or run anywhere --
// this tool-execution environment is Windows-only with no Linux available
// to compile or exercise it against. Unlike shim.cpp (Windows/Detours),
// which was actually built and run (docs/adr/0029's demo.cpp output is
// real), this file is offered as a real, reviewable implementation
// sketch, not a verified one. Same honesty standard this project applies
// to the VS Code extension's Electron-GUI gap (docs/adr/0022): a
// documented environmental wall, not a silently-downgraded claim. Anyone
// building this on real Linux should treat it as an untested starting
// point and verify it the same way shim.cpp/demo.cpp were verified here
// (a real hook, a real watched vs. unwatched destination, a real
// compile-time-constant-size case confirming the same structural blind
// spot) before relying on it.
//
// Expected build: `g++ -shared -fPIC -O2 memcpy_preload.cpp -o libchronicle_memcpy_shim.so -ldl`
// Expected use:   `LD_PRELOAD=./libchronicle_memcpy_shim.so ./your_program`

#include <chronicle/interposition_registry.hpp>

#include <cstddef>
#include <cstring>
#include <dlfcn.h>

extern "C" {

using MemcpyFn = void* (*)(void*, void const*, std::size_t);

// Resolved lazily (not at static-init time): dlsym itself can call into
// libc functions that may re-enter this same interposed symbol before
// process startup has finished setting up TLS/locale state -- a real,
// commonly-hit LD_PRELOAD footgun, guarded against here defensively even
// though it hasn't been exercised on real Linux in this environment.
MemcpyFn real_memcpy() {
    static MemcpyFn fn = reinterpret_cast<MemcpyFn>(dlsym(RTLD_NEXT, "memcpy"));
    return fn;
}

void* memcpy(void* dst, void const* src, std::size_t size) {
    MemcpyFn const real = real_memcpy();
    if (chronicle::interposition::Registry::instance().overlaps(dst, size)) {
        // A real deployment would report this hit somewhere observable
        // (a log line, a counter queryable the way shim.cpp's
        // watched_hit_count()/total_call_count() are) -- left as the same
        // shape as the Windows side rather than inventing a different
        // reporting mechanism per platform, but not implemented in this
        // untested file beyond the registry check itself.
    }
    return real(dst, src, size);
}

} // extern "C"
