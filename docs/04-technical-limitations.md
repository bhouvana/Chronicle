# Technical Limitations Imposed by C++

This document answers, without softening: **can every assignment actually be
observed?** No. This is not a temporary engineering gap to be closed later — it is a
permanent consequence of what C++ is. Every design decision downstream must be made
with this fact visible, not hidden behind a marketing claim of completeness.

## The central fact

C++ gives you two ways to mutate memory: through a **typed lvalue expression**
(`obj.field = x`, `*ptr = x`, `container.push_back(x)`) or through **untyped/raw
memory operations** (`memcpy`, `memmove`, `memset`, placement `new` overwriting
bytes, DMA, hardware/mapped-memory writes, inline assembly, SIMD stores). Chronicle's
instrumentation model (compiler-inserted hooks or wrapper types) can only ever
observe the first category, because interception requires the write to pass through
code we control or the compiler inserts around. The second category is, by
construction, invisible to any purely source/compile-time approach. This is not a
Chronicle-specific weakness: it's the same reason ASan needs shadow-memory checked at
*every* access (not just typed ones) to catch raw-memory bugs, at real cost (2x+).

## Per-mechanism analysis

- **Operator overloading (`chronicle::tracked<T>`)**: works cleanly for scalar and
  simple aggregate fields the developer explicitly wraps. Breaks down for types that
  need to *be* their underlying type at the ABI boundary (passed to C APIs, laid out
  in a `struct` that must match an external format) — a wrapper changes size/layout.
  Mitigation: `tracked<T>` must be `[[no_unique_address]]`-friendly and provide
  implicit conversion, not aim for byte-identical layout compatibility.

- **Object lifetime / placement new / `std::launder`**: reusing storage via placement
  new starts a new object's lifetime; any history keyed by *address* silently
  conflates two unrelated objects unless Chronicle also intercepts construction
  (constructor/destructor hooks) and treats address+generation, not address alone, as
  identity. `std::launder` exists specifically because the compiler is permitted to
  assume placement-new'd objects at the same address are unrelated — our storage
  model must adopt the same discipline (generation-counted handles, not raw pointers,
  as history keys — see Phase 6).

- **Move semantics / copy elision (NRVO/RVO/guaranteed elision in C++17)**: an
  object's *identity* is not stable across a move — the moved-from object is a
  distinct (if unspecified-state) object from the moved-to one. Guaranteed copy
  elision in C++17 means a returned object may never actually be "constructed
  and then copied" — there may be no intermediate object to observe at all. History
  must model moves as an explicit edge (`A moved-into B`) rather than assume identity
  continuity, and must accept that guaranteed-elision return values simply have no
  pre-move history to show, by language design, not implementation gap.

- **Multiple/virtual inheritance**: a `Derived*` and a `Base*` subobject can have
  different addresses (pointer adjustment via `this`-offset). Address-keyed identity
  breaks unless normalized to a stable object handle established at
  construction/registration time rather than derived from a pointer value at query
  time.

- **Aliasing / raw pointers / unions / bitfields**: two pointers of different
  (unrelated) types are assumed by the strict-aliasing rule not to overlap — but
  `reinterpret_cast`, unions, and (with careful sizing) `memcpy`-based type punning
  can still alias real memory. A tracked field can be silently mutated through an
  aliased raw pointer that never goes through our wrapper. Bitfields compound this:
  they don't have addresses at all (`&s.bitfield` is ill-formed), so field-level
  tracking of a bitfield member is only possible via whole-struct-level tracking
  (observe the containing storage unit changing, not the individual bit range)
  or via a generated accessor pair, never via a `tracked<T>` member.

- **Thread-local storage, atomics, and data races**: `thread_local` objects have
  per-thread identity under one name — history must be keyed per-(object,thread), not
  merged. Atomics are the *one* place C++ gives strong, standardized guarantees about
  concurrent memory access ordering, and hooking an atomic's store means the hook
  itself must be at least as cheap and correctly ordered as the atomic op it wraps
  (implies hooks compile to a relaxed-load into a lock-free ring buffer, never a
  mutex). Genuine data races (unsynchronized concurrent access to non-atomic memory)
  are **undefined behavior in the language already** — Chronicle cannot be asked to
  give meaningful history for a race any more than the compiler is required to; at
  best we can *detect and flag* apparent races (à la TSan) as a v2+ feature, never
  silently paper over them.

- **Custom allocators / intrusive containers / arenas**: history at the "field of an
  object" grain says nothing about an allocator handing out and reusing raw storage
  underneath. This needs its own adapter layer (arena/allocator hooks recording
  alloc/free/reuse events) that is conceptually a different `StateStream` producer
  from object-field tracking, not a special case of it (reinforces the layered
  adapter architecture from Phase 5).

- **SIMD stores / vectorized/auto-vectorized loops**: the compiler may batch four or
  eight scalar writes into one vector store the source never spelled out
  individually. If those scalars are behind `tracked<T>` wrappers, the wrapper's
  overloaded `operator=` prevents the compiler from vectorizing across them in the
  first place (a real, load-bearing performance cost — see Phase 9) — so this is a
  *cost*, not a gap: you get correct tracking by giving up some auto-vectorization
  opportunities on tracked hot loops. Untracked SIMD code remains invisible to us.

- **Compiler optimizations / UB more broadly**: any hook we insert must not itself
  introduce UB or change program behavior (no relying on unspecified evaluation
  order, no assuming otherwise-observable side effects survive dead-store
  elimination on a variable the compiler can prove is otherwise unused). Where the
  compiler is legally permitted to elide a "useless" write to an object *only*
  because it doesn't know we're watching, our own hook call is itself what keeps the
  write observable, at the cost of also inhibiting exactly the DCE/CSE opportunities
  that write would otherwise have offered.

- **ODR / modules / templates**: header-only instrumentation macros/templates must be
  written to guarantee identical definitions across translation units (no
  `#ifdef`-gated behavior differences) or they violate ODR silently. C++20 modules
  change how macros propagate (macros are not exported across module boundaries),
  meaning a modules-based codebase needs the codegen/attribute-based instrumentation
  path (Phase 4/7), not the macro path, from day one.

## Direct answers to the brief's checklist

| Question | Answer |
|---|---|
| Can every assignment be observed? | No — only assignments through instrumented lvalues/setters. |
| Can every object be tracked? | Only registered ones; automatic discovery needs reflection C++23 lacks. |
| Can every mutation be intercepted? | No — raw memory ops bypass source-level hooks by construction. |
| Can every allocation be discovered? | Yes, if the allocator is ours or hooked (operator new/delete, PMR, custom); no for allocators we never see. |
| Can `memcpy` bypass tracking? | Yes, always, unless libc interposition is explicitly enabled (opt-in, platform-specific). |
| Can pointer aliasing break history? | Yes — an aliased raw write is invisible. |
| Can DMA / hardware writes? | Always invisible; out of scope entirely (no software hook exists). |
| Can placement new? | Handled only if construction is also hooked; otherwise breaks address-based identity. |
| Can `std::launder`? | Confirms our own obligation to use generation-counted handles, not raw addresses. |
| Can unions? | Breaks field-level tracking; only whole-storage tracking is sound. |
| Can bitfields? | No address to hook; requires accessor-based tracking, not `tracked<T>`. |
| Can SIMD? | Untracked SIMD is invisible; tracked fields forfeit auto-vectorization. |
| Can custom allocators? | Only via a separate allocator-level adapter, not object tracking. |
| Can intrusive containers? | Same as above — needs its own adapter, not automatic. |

## The honest product claim this leads to

Chronicle observes **every mutation that flows through code it was told to watch**,
with clearly documented, enumerable blind spots (raw memory ops, unhooked allocators,
untracked aliasing, DMA/hardware). This is the same honesty bar ASan/UBSan hold
themselves to ("we catch what we instrument for"), and it is a defensible, valuable
claim — not the same as, and must never be marketed as, "omniscient" tracking.
