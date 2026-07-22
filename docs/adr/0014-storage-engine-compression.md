# ADR 0014: At-Rest Zstd Compression via a Codec Extension Point, Not a Forced Dependency

## Status
Accepted

## Context
[06-recording-model.md](../06-recording-model.md)'s "Encoding & compression"
section calls for "LZ4 for live/low-latency streams, Zstd for at-rest
session files ... applied at the snapshot/chunk level, not per-event."
[10-roadmap.md](../10-roadmap.md)'s v1.0 scope narrows this to "Compression
(Zstd at rest, LZ4 live-stream) in Storage Engine."

Two things became clear before writing any code:

- **No live-stream transport exists yet.** docs/06 describes streaming to
  "an external viewer process over a local socket" as a real path, but
  nothing in this codebase implements it — the interactive browser viewer
  (`chronicle-cli serve`, also v1.0, not yet built) is what would eventually
  own that socket. Building LZ4 compression for a transport that doesn't
  exist would be speculative, the same mistake this project has
  deliberately avoided elsewhere (e.g. [ADR 0011](0011-tracked-type-explicit-handle.md)'s
  index-based-not-named-methods deferral, [ADR 0013](0013-tracy-bridge.md)'s
  zone-message deferral). **Scoped out of this pass entirely** — a real
  follow-up once `chronicle-cli serve` exists to attach it to, not attempted
  alongside this.
- **The current wire format is row-major, not columnar.** docs/06 also
  describes a columnar, field-grouped encoding as the target layout
  specifically because it compresses better than interleaved row-major
  data. What v0.1/v0.2 actually shipped
  (`include/chronicle/io/session_writer.hpp`) is row-major (event-by-event,
  as recorded) — a real, pre-existing gap between the original sketch and
  what got built, not something this pass introduces or fixes. Compressing
  the row-major format as it exists today is still genuinely useful
  (Zstd finds real redundancy in it regardless — see measurements below),
  just not as effective as a columnar rewrite would be. That rewrite is a
  separate, larger Storage Engine change, out of scope here.

## Decision

### Format
`kFormatVersion` bumps 2 → 3 (`include/chronicle/io/format.hpp`). The header
gains a one-byte `CompressionKind` tag (`None = 0`, `Zstd = 1`), written and
read **in the clear**, always, before anything compressed is touched — a
reader that doesn't understand a given kind fails with a clear
"unsupported codec" error, not a silent misparse of compressed bytes as if
they were the plain format. When the tag isn't `None`, everything after it
is exactly one blob — `[compressed_size: u64][compressed bytes]` — covering
the *entire* rest of the file (every stream, every event), not per-stream
or per-event chunks. Coarser than docs/06's "snapshot/chunk level," and a
deliberate simplification: `SessionWriter` already materializes each
stream's full `history()` into memory before writing it
(`stream->history()` returns a `std::vector`), so a whole-session buffer
doesn't introduce a new memory-scaling behavior this writer didn't already
have — implementing genuine incremental/chunked compression on top of an
already-fully-buffering writer would add real complexity for a memory
characteristic that wouldn't actually change. Revisit if/when the writer
itself becomes genuinely streaming.

### No forced dependency
`format.hpp`, `session_writer.hpp`, and `loaded_session.hpp` never include
`<zstd.h>` or link a compression library — same principle as
[ADR 0008](0008-cli-avoids-streambase-virtual-dispatch.md),
[tools/codegen](0012-chronicle-codegen-libtooling.md), and
[the Tracy bridge](0013-tracy-bridge.md): most Chronicle consumers won't
want Zstd forced on them, and chronicle-core's zero-required-dependency
promise doesn't get to have an exception just because this particular
feature is framed as "core Storage Engine" in the roadmap prose. A
`CompressionCodec` (`format.hpp`) is a plain pair of function pointers —
`{kind, compress, decompress}` — the exact same "runtime extension point,
not a compile-time dependency" shape as `Stream<T>::RecordHook`
([ADR 0013](0013-tracy-bridge.md)). `SessionWriter`'s constructor takes an
optional `CompressionCodec const*` (default `nullptr` → byte-for-byte the
same uncompressed write path as before this feature, modulo the one new
header byte); `load_session()` takes an optional `std::span<CompressionCodec
const>` of codecs the caller supports (default empty → only `None` files
readable, with a clear error otherwise).

`include/chronicle/io/zstd_codec.hpp` — genuinely separate and optional,
like `tracy_bridge.hpp` — implements the actual Zstd calls
(`ZSTD_compress`/`ZSTD_decompress`, using `ZSTD_getFrameContentSize()` to
size the decompression buffer without a separately-stored uncompressed-size
field) and exposes `chronicle::io::zstd_codec()` returning a ready-made
`CompressionCodec`. Only a translation unit that includes this header and
calls into it needs libzstd linked; `chronicle-core`'s umbrella header
(`chronicle.hpp`) never includes anything under `io/` at all, so ordinary
embedded-recording consumers are entirely unaffected regardless.

`chronicle-cli` (`tools/cli/`) is the one place this pass wires compression
in by default when available, since it's the read-side consumer everyone
shares: its `CMakeLists.txt` does `find_package(zstd CONFIG QUIET)` and, if
found, defines `CHRONICLE_CLI_HAVE_ZSTD` and links `zstd::libzstd`;
`main.cpp` guards its one `#include <chronicle/io/zstd_codec.hpp>` and its
one extra `codecs` argument to `load_session()` behind that macro. No
runtime flag: a build with Zstd available transparently reads both
compressed and uncompressed files; a build without it still reads
uncompressed files exactly as before and gives a clear error on a
compressed one, rather than failing to build the default `CHRONICLE_BUILD_TOOLS`
target for everyone without vcpkg configured.

### Verification performed
`tests/unit/compression_test.cpp` — six tests covering round-trip
correctness for scalar/vector/map streams, empty input, an uncompressed
file still working when a codec list is supplied but unneeded, and (the
failure mode the format tag exists to prevent) a compressed file read
*without* a matching codec producing a clear thrown error rather than
garbage. Only added to `chronicle-core-tests` when `find_package(zstd
CONFIG QUIET)` succeeds (`tests/unit/CMakeLists.txt`) — the default,
no-vcpkg build stays exactly as dependency-free as it was before this
feature; **208/208 checks (38 tests) still pass unchanged** in that
configuration. With Zstd available, the full suite is **225/225 checks
across 44 tests**.

Real, not just unit-level: `tools/cli/chronicle-cli.exe`, rebuilt with Zstd
available, was run against both a compressed and an uncompressed
`.chronicle` file produced by a throwaway 25,000-mutation, 2-stream
session (`player.health`, `network.latency`) — `chronicle-cli list` and
`chronicle-cli history` produced byte-identical results from both files
(same event count — 1,024, `Session`'s default `RingWindow(1024)` retention
correctly capping it — same values, same versions), confirming the
compressed path isn't just internally round-trip-consistent but actually
readable by the real tool with zero extra flags. Measured compression
ratio on that same session: **133,195 bytes → 15,637 bytes (11.7% of
original, ~8.5x)** — a real number from realistic (slowly-varying
int/double, call-site info per event) data, not a synthetic best case.

## Consequences
- Positive: chronicle-core (the embedded, always-linked recording library)
  gains zero new dependencies or footprint from this feature — verified by
  the default build's unchanged 208/208 result.
- Positive: a real, measured compression win (~8.5x on realistic data) for
  the tools/consumers that opt in, verified against the actual
  `chronicle-cli` binary, not just a unit test's in-memory round trip.
- Positive: the codec mechanism (`CompressionCodec`, function pointers, not
  a hardcoded `if (kind == Zstd)`) is additive — a future LZ4-for-live-
  streams codec, when the transport it needs exists, slots in the same way
  without touching `format.hpp`/`session_writer.hpp`/`loaded_session.hpp`
  again.
- Negative: whole-session-as-one-blob compression means both writing and
  reading a compressed file need the entire (post-header) payload in memory
  at once — an honest, documented ceiling this pass doesn't try to hide,
  not a regression from the writer's pre-existing behavior (see "Format"
  above).
- Negative: LZ4 live-stream compression and the columnar wire-format
  rewrite docs/06 originally envisioned are both explicitly out of scope —
  the former has no transport to attach to yet, the latter is a separate,
  larger change. Neither silently dropped; both recorded here as real
  follow-ups.
- Negative: format v3 is a breaking, non-backward-compatible bump from v2 —
  consistent with every prior bump in this project (v1→v2,
  [ADR 0010](0010-call-site-capture.md)), since no compatibility commitment
  exists yet for this format.
