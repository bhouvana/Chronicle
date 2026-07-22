# Recording & Storage Model

## Strategy survey, evaluated against our constraints

| Strategy | Memory scaling | Replay cost | Determinism | Verdict |
|---|---|---|---|---|
| Full snapshot per mutation | O(mutations × object size) | O(1) — direct read | trivial | Rejected as default: unbounded growth (Phase 3 risk #4); fine as an *opt-in* mode for small hot objects |
| Operation log only (event = the mutation itself, e.g. "field X set to Y") | O(mutations × event size), event size ≪ object size | O(mutations since last snapshot) to reconstruct | trivial (it's already the causal record) | **Primary strategy** — matches the event-sourcing model chosen in Phase 3 |
| Field-level deltas | O(mutations × Δsize) | O(deltas) | trivial | Same family as operation log; used for containers/blobs where "old vs new value" isn't naturally a discrete event |
| Periodic full snapshot + operation log since | O(snapshot_interval × avg mutation rate) bounded per window | O(distance to nearest snapshot) — bounded | trivial | **Chosen hybrid** (see below) |
| Memory-page snapshotting (COW via `mprotect`/`fork`) | coarse, page-granularity | fast for whole-process, useless for single-field | trivial but heavyweight | Rejected for v1: OS-specific, page-granularity is the wrong grain for "one field changed" |
| Persistent/immutable trees (structural sharing) | O(log n) per version for tree-shaped containers | O(log n) random access to any version | trivial | Adopted specifically for `chronicle`-wrapped containers (Phase 1's Immer reference) — not for scalar object fields, where it's overkill |
| Binary diff / hash-consing dedup | good for large, sparsely-changing blobs | requires diff decode | trivial | Reserved for large blob/buffer tracking (e.g. tracked byte arrays), not the common scalar-field case |

## Chosen model: **operation log + periodic snapshots, per StateStream**

Each tracked entity (object, field, container, arena) is one `StateStream`. A stream
is:

```
Snapshot(V0) → Event(V0→V1) → Event(V1→V2) → ... → Snapshot(Vn) → Event(Vn→Vn+1) → ...
```

- **Events** are small, structured records: `{version, timestamp, thread_id,
  call_site_id, causal_predecessor, payload}`. Payload is either the new value
  (scalar fields — cheap, and makes "current value" always O(1)) or an encoded delta
  (containers/blobs).
- **Snapshots** are inserted periodically (configurable: every N events, every N
  bytes of accumulated delta, or on an explicit `chronicle::checkpoint()` call) so
  that reconstructing any version never requires walking more than one snapshot
  interval's worth of events — this bounds Replay cost (Phase 5 requirement) without
  paying full-snapshot memory on every mutation.
- **Retention policy** is explicit, per-stream, and required at registration time —
  not a global afterthought:
  - `Unbounded` (session recording to disk, e.g. offline QA replay capture)
  - `RingWindow(N events | duration)` (e.g. "last 5 seconds" — the default for
    real-time game debugging, directly solving Phase 3 risk #4)
  - `SnapshotOnly` (no event log at all — just periodic snapshots; cheapest, coarsest
    grain, for very high-frequency low-value fields)

## Causal ordering, not global ordering

Each event carries a `causal_predecessor` (the version of the *same stream* it
follows) and a `thread_id` + monotonic per-thread sequence number, **not** a global
wall-clock total order. This is a direct, deliberate response to the multithreading
risk flagged in Phase 3/4: we guarantee "this stream's history is a correct, total
order of its own mutations" (trivially true — mutations to one stream are already
serialized by whatever synchronization the user's code uses to make the mutation
safe at all) without overclaiming a synchronized global timeline across streams,
which would require solving distributed/whole-program determinism (Phase 12).
Cross-stream queries ("what was Y when X changed") use timestamps as a best-effort
approximation, clearly documented as such, upgradable later via a hybrid logical
clock without changing the on-disk format (the field is already reserved).

## Encoding & compression

- Event payloads are encoded with a **columnar, type-driven binary format**: values
  of the same field across many events compress far better grouped by field
  (columnar) than interleaved (row-major event order), because consecutive values of
  one field are typically similar (small deltas, repeated values, slowly varying
  numbers) — same insight as columnar databases and as Perfetto's trace format.
  Row-major *event* order is kept as the logical/query model; columnar layout is a
  storage-engine encoding detail, transparent above the Storage Engine boundary.
- Delta encoding for containers uses a simple structural diff (index-based
  insert/remove/update triples) rather than a generic byte-diff — cheaper to compute
  and directly maps to `push_back`/`erase`/assignment operations already known to the
  Instrumentation layer, so no re-derivation of "what changed" from raw bytes is
  needed.
- Compression (LZ4 for live/low-latency streams, Zstd for at-rest session files) is
  applied at the snapshot/chunk level, not per-event — matches Tracy/Perfetto
  practice and avoids per-event compression overhead on the hot path (compression
  only ever happens in Storage's cold-path drain, never in Recording).

## In-memory vs on-disk

Both are the *same* format, differing only in backing store: a session can live
entirely in a ring buffer in the target process's memory (default; zero I/O
dependency, matches Tracy's default in-process buffer model), stream live to an
external viewer process over a local socket (Tracy/TTD precedent), or flush to a
session file on disk (`.chronicle` — chunked, seekable, so tools can load a subrange
without reading the whole file, similar to Perfetto's trace format goals). This
symmetry is required so Replay/Query (Phase 5) can treat "live" and "loaded from
disk" identically.

## What determines which model a given `StateStream` uses

Chosen automatically by adapter (scalar object field → operation log; `vector`-like
container → structural delta; large binary blob → chunked binary diff; ECS
component → operation log keyed by entity), but always overridable per-stream via
the registration API (Phase 7) — the model is a per-stream policy decision, not a
global one, because a finance order book and a single `int` health field have
legitimately different cost/fidelity tradeoffs.
