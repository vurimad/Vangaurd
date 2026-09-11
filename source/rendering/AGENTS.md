# Shader atomic operations

These rules apply to rendering shaders and C++ changes that introduce or change
their dispatch, counter, or buffer contracts.

- Do not introduce one shared-destination atomic per instance, candidate,
  primitive, pixel, or other potentially large element stream. Aggregate equal
  destination keys within a wave, reduce within a workgroup, or use disjoint
  count/prefix/scatter ranges. Apply the same rule to diagnostic/error counters.
- Skip zero-increment atomics unless the returned atomic read is required.
  Skip wholly unwritable reservations only when requested/overflow accounting
  is preserved independently. Do not read an atomic cursor non-atomically to
  decide whether it is full.
- For every atomic site, document its destination scope, maximum update rate,
  and ownership of initialization and consumption. Review the all-same-key,
  all-distinct-key, partial-wave, all-invalid, zero-work, and overflow cases.
  Include the view/partition identity in aggregation keys. Never assume a fixed
  wave width or broadcast from a lane that has exited.
- Aggregation reduces contention; it does not prove that an address shared by
  many waves is inexpensive. Keep cross-wave hotspots visible in the design.
  Add workgroup hash tables, counter shards, sorting, or extra passes only with
  a concrete workload justification; fewer atomics alone is not proof of a
  faster shader. Account for barriers, scratch memory, occupancy and traffic.
- A per-element atomic exception requires an adjacent explanation of the
  enforced bound on updates/contenders and why it cannot become an unbounded
  hotspot. If the justification depends on timing, require target-GPU evidence
  over the supported worst-case workload and an explicit budget. "Rare", "only
  diagnostics", or "the demo is small" is not sufficient. With exclusive write
  ownership, prefer an ordinary write unless atomic semantics are required.
- Keep concurrently updated fields out of ordinary struct loads. Preserve
  overflow-safe reservation arithmetic, unique scatter destinations and graph
  barriers between initialization, counting, prefixing, scatter and consumers.
  Atomics and wave operations do not replace those resource dependencies.

Validation must follow the user's current build/test schedule. Record deferred
checks honestly; do not claim that a source audit establishes GPU performance.
