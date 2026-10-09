# RenderFlow RF6-RF9 structural Resolve redesign

Status: **DEFINITION CLOSED. IMPLEMENTATION NOT STARTED.**

Date: 2026-10-05

Continuation of: [`render-flow-rf-descriptor-ownership-plan.md`](render-flow-rf-descriptor-ownership-plan.md)

## Program goal

RF0-RF5 removed steady-state whole-resource descriptor publication from warm RenderFlow Resolve. The next program attacks the remaining Resolve cost structurally rather than by locally micro-optimizing individual loops.

The current 320-resource warm Profile ceiling is approximately **853.268 us p50**. The remaining cost is concentrated in three architectural families rather than randomly scattered overhead:

- `PhysicalAssignment`: approximately **316 us p50**;
- generic operation compilation: approximately **284 us p50**; and
- repeated state reconstruction/finalization: approximately **158-163 us p50**.

The design objective is to make whole categories of that work disappear on stable warm frames while preserving the exact ownership, queue, lifetime, import/export, aliasing, rollback and device-loss contracts.

## Permanent correctness invariants

- Every frame receives a fresh `ExecutionGenerationId` and fresh `ExecutionGenerationRef::Impl`.
- Packet runtime state, packet liveness, decisions, current imports, import fences, terminal receipts and selected native backings are always current-frame data.
- Persistent structural identity belongs to a resource-plan lane, never to one permanently pinned native resource.
- GPU submitted-fence safety remains authoritative. No CPU wait is introduced to force lane reuse.
- A stable lane may need multiple fence-safe backing instances because CPU frames can overlap prior GPU use.
- Pressure, trim, export transfer, cache clear and device loss may invalidate backing instances without invalidating the abstract lane identity when the structural plan itself is still valid.
- Imported native resources and incoming readiness fences are never persisted in the plan.
- Terminal export ownership transfer remains authoritative. An exported dedicated backing leaves its lane instance.
- Exact texture subresource tracking, buffer state normalization, fork/join queue semantics, retained imports, terminal exports and placed aliasing remain correctness requirements.
- Graph-cache identity alone is not sufficient to prove a resource-plan hit.
- A plan hit requires exact normalized structural equivalence after a fast candidate lookup.
- Placed persistent backing optimization is not part of the first production cutover because the preserved 320-resource profile has placed resources disabled.

## Ownership model

Persistent plan metadata belongs to `RenderFlowResourceAllocator::Impl`.

`RenderGraphCache` may eventually hold only a weak generation-stamped plan identifier. It must not own native backing references or authoritative plan lifetime.

The plan owns structural assignment only:

```text
CompiledPhysicalPlan
  resources / semantic contract
  stable PlanLane[]
    compatibility class
    required capacity
    descriptor demand
    lifetime / queue class
    assigned logical allocations
    lease-instance identities
```

Native ownership remains inside the dedicated/placed pools.

A persistent lane must not retain an extra `TextureRef` or `BufferRef` merely to keep a fast-path association alive. Extra persistent RHI references would violate the pools' exact ownership/refcount proofs.

For dedicated resources, exact-current `DedicatedResourcePool::Impl::Entry` already has a monotonically changing `storeSerial`. A safe opaque lease identity can therefore be backed by a generation-stamped pool handle conceptually equivalent to:

```text
{ entryIndex, storeSerial }
```

Raw pool indices alone are invalid because entry slots are recycled.

The public plan should use an opaque pool reservation/lease handle rather than depending directly on entry layout.

Lease lifecycle:

```text
Idle
  -> CheckedOut
  -> InFlight(submitted fences)
  -> Idle
```

If no lease instance is fence-safe when a new CPU frame arrives, Resolve must not wait. It falls back to the normal pool acquire path, provisions another backing if needed, and may adopt that backing as another lane instance.

## Plan identity

The exact plan signature is generation-free and must include all structural semantics that can change the correctness of assignment or lowering, including at least:

- logical resource declarations and resource keys;
- exact descriptors and buffer capacity requirements;
- compatibility classes;
- mapping and swap topology;
- semantic use begin/end positions;
- access, content intent and required states;
- exact texture subresource ranges;
- true named/explicit resource scopes;
- command-scope and queue topology;
- fork/join dependencies;
- descriptor demand;
- terminal export structural contracts;
- placed alias/range provenance where applicable; and
- relevant `FrameResourcePolicy` / placed-capability state.

Current-frame data explicitly excluded from persistent identity:

- `ExecutionGenerationId` and session/generation counters;
- imported `TextureRef` / `BufferRef` values;
- import/readiness fences;
- swapchain acquisition serials;
- current `Decision` values;
- selected native backing handles;
- current submitted fences; and
- terminal receipts.

Hashing/counts may locate candidate plans. Exact normalized comparison must prove compatibility before use.

## Exact-current shutdown prerequisite

The earlier RF3 shutdown blocker/high reported during architecture study has already been closed in the current shared checkout and must not be carried forward as an open prerequisite.

Current healthy shutdown now:

- performs non-destructive descriptor-retirement preparation;
- does not run the destructive placed poll during preflight;
- flushes shared retirements;
- validates both dedicated and placed pools after the flush; and
- enters void/non-recoverable terminal commit only after both validations pass.

RF6 implementation must preserve this two-phase ownership boundary.

## RF6 - Persistent physical lanes and dedicated lease fast path

### Target

Primary target is current `PhysicalAssignment`, especially:

- per-allocation dedicated pool reacquire;
- compatibility-tree lookup/probing;
- same-frame lane coloring/reuse bookkeeping;
- routine reusable-index removal/insertion; and
- trim/descriptor-cold index churn on normal warm turnover.

The preserved 320-resource probe is dedicated-only and all 320 writes occur before all 320 reads. Therefore all lifetimes overlap and same-frame lane reuse cannot succeed in that workload. Its same-frame reuse machinery is pure representation overhead there.

### Migration rule

RF6 does **not** redesign semantic compilation or state lowering yet.

The existing compiler remains authoritative. From a proven successful current Resolve, RF6 builds an immutable candidate physical plan transactionally.

First stage should support shadow/exact validation of plan identity and lane assignment before allowing the fast path to bypass current assignment work.

### Warm hit behavior

For each plan lane:

1. find an `Idle` generation-stamped lease instance whose retirement fences are complete;
2. check it out without performing the ordinary general compatibility search/index churn;
3. if no instance is safe, use ordinary `Acquire` once and optionally adopt the new backing as another lease instance;
4. never CPU-wait for a lane backing;
5. preserve current canonical descriptor borrowing; and
6. publish only after all lanes and current-frame bindings are valid.

### Invalidation and fallback

Any stale lease token, pressure/trim eviction, export transfer, incompatible policy, structural mismatch or failed candidate validation falls back to the existing path.

The previous cached plan must not be partially mutated by a failed current-frame candidate.

Device loss invalidates all backing associations and should invalidate the structural plan cache in the first production implementation.

### RF6 closure gates

- Plan-ready lanes perform zero ordinary compatibility-tree search.
- Plan-ready lane turnover performs zero routine general reusable-index remove/reinsert churn.
- No CPU wait is introduced for GPU-lagged resources.
- Stale generation-stamped lease handles are rejected after slot recycling.
- Export, rollback, pressure, trim, cache clear, device loss and shutdown ownership tests pass.
- Normal quad, graph-only, 64-resource and 320-resource Profile workloads are re-run.
- Initial performance bar: approximately **<=150 us p50** `PhysicalAssignment` on the 320-resource workload before proceeding aggressively.

If the structural work disappears but measured assignment time does not materially collapse, reprofile before RF7 rather than guessing.

## RF7 - Dense semantic-use IR and removal of the common generic use tape

### Target

Current 320-resource generic operation population is approximately:

- 320 declarations;
- 640 `UseBegin`;
- 640 `UseEnd`;
- 1280 automatic named-scope open/close operations;
- approximately 2880 `CandidateOperation`s total.

`ResourceUseId.ordinal` currently behaves like a generic operation address, which forces an operation-sized sparse use-record mapping and broad interpreter work.

### New identity model

`ResourceUseId.ordinal` becomes a dense use index.

Each `CandidateUse` stores its semantic payload once, including:

- logical resource/allocation identity;
- access;
- content intent;
- required state;
- texture subresource range;
- clear payload;
- begin semantic sequence position;
- end semantic sequence position;
- packet/scope identity;
- runtime use slot; and
- descriptor demand where required.

`BeginUse` creates the dense record. `EndUse` fills the endpoint on that record instead of appending `CandidateOperationKind::UseEnd`.

### Preserve total semantic ordering

Dense array storage index is not global ordering.

The writer must keep a separate monotonically increasing semantic sequence ordinal. Ordered declarations, uses, mappings, swaps and rare mutation events retain exact relative sequence through this ordinal.

### Split common semantic data from rare ordered mutations

Desired producer representation:

```text
declarations / mappings
dense CandidateUse[]
views
decisions / current-frame values

small ordered mutation stream
  swaps
  non-trivial mapping changes
  explicit/cross-node scopes
  imports/exports where order matters
  queue/synchronization topology
```

### Scope correctness

Do not initially delete named/explicit scope semantics.

Current node-local automatic scope events and use endpoints are not proven equivalent: scopes can widen planner lifetime independently of executable use endpoints.

RF7 first removes generic `UseEnd` storage and sparse use identity while preserving scope semantics. Automatic scope folding may happen only after a focused equivalence proof.

### Critical semantic traps

- The terminal executable use is selected by the **latest EndUse**, not by the use with the latest BeginUse.
- `RequestExport` binds the slot's **final mapping after all swaps**, not the mapping visible at the point where the request was recorded.
- Named-scope close resolves the current slot mapping; an explicit `ResourceScopeId` closes the allocation captured by that scope.
- Scope lifetime may be wider than first/last executable use.
- Writes define content at `EndUse` under the current content model.

### RF7 closure gates

- No operation-ordinal `ResourceUseId` semantics.
- No generic `CandidateOperationKind::UseEnd` in the normal producer path.
- No operation-sized sparse use-record map.
- Mapping/swap/scope/import/export ordering remains exact.
- Existing non-LIFO retained-import, swap, overlap/content, queue and export tests remain valid.
- Initial performance ambition: semantic compilation approximately **<170 us p50** on 320, with structural deletion taking priority over the exact number.

## RF8 - One post-binding physical-state lowerer

### Target

Delete the current repeated state reconstruction model:

```text
semantic compilation
  -> provisional logical steps/actions
physical assignment
  -> native physical identity
CompileScopeEntryStates x many
  -> Gather
  -> BuildEntries
  -> Replay
  -> Commit
```

The current repeated gather/replay/commit representation is not mandatory. Final native-state lowering is mandatory.

### New model

Before physical assignment, semantic uses carry intent only:

- required state;
- access/content intent;
- exact subresource range;
- clear value;
- UAV-ordering requirements;
- begin/end sequence position;
- packet/scope; and
- queue.

After physical bindings are selected, one deterministic lowerer walks the physical timelines and directly emits:

- `CompiledPacket::entryStates`;
- texture/buffer transitions;
- UAV barriers;
- clear actions;
- queue handoffs;
- state-predecessor edges;
- placed alias activation/finalization;
- terminal export transitions; and
- packet exit actions.

Logical-only validation uses equivalent virtual state owners through the same semantic rules.

### Structures/passes intended for deletion from the production route

Once equivalence is proven:

- repeated `CompileScopeEntryStates`;
- packet resource rediscovery for state reconstruction;
- duplicate packet-local mutable `scopeStates`;
- `scopeActions` gather/replay representation;
- provisional transition/UAV/clear emission that is later rewritten;
- final local-state-to-physical-history commit copies; and
- redundant logical-to-physical patch structures that are no longer needed after direct physical emission.

### Mandatory work that remains

- native resource entry-state sorting required by the RHI ABI;
- exact subresource-state tracking;
- queue handoff/fork/join logic;
- alias activation/finalization;
- import/export terminal-state rules;
- buffer `Common` normalization and explicit-state continuity;
- validation of overlapping use legality and contents-defined rules.

### Migration proof

Initially run the new lowerer as a test/Profile shadow compiler against the old path.

Canonicalize and compare:

- entry states;
- transitions/UAV/clear actions;
- predecessor relationships;
- queue dependencies;
- alias actions; and
- terminal state/export behavior.

Execute only one path. Cut over only after equivalence is proven.

### RF8 closure gates

- `CompileScopeEntryStates` disappears from the new production route rather than merely becoming faster.
- Exact texture subresource, `keepInitialState=false`, fork/join, retained import, terminal export, placed alias, logical-only and device-loss/abort coverage passes.
- Initial state-lowering performance ambition: approximately **<70 us p50** on 320.

## RF9 - Warm structural resource-plan fast path and legacy cutover

RF9 composes RF6-RF8 only after each stage independently proves correctness and a real structural win.

### Warm fast path

Desired flow:

```text
parallel producer semantic output
  -> fast structural signature
  -> exact normalized plan compare
  -> plan hit
  -> bind current imports/fences/decisions
  -> checkout fence-safe lane lease instances
  -> instantiate fresh execution generation
  -> patch current PhysicalResourceIds/native refs
  -> run the single mandatory physical-state lowerer
  -> final native entry-state sort
  -> publish generation
```

On a true warm hit, Resolve should not repeat:

- interpretation of approximately 2880 generic operations;
- reconstruction of common Begin/End identity;
- full lane coloring;
- ordinary global pool search for plan-ready dedicated lanes;
- routine reusable-index churn for plan-ready lanes;
- repeated packet resource/state gather and replay; or
- `CompileScopeEntryStates` commit reconstruction.

RF5's warm zero canonical descriptor Allocate/Write/Retire property remains intact.

### Plan miss

On miss:

1. run the full new semantic compilation/lowering path;
2. build a candidate structural plan transactionally;
3. execute the current frame normally;
4. publish the candidate plan only after it is internally proven valid.

Never use a "previous plan is probably compatible" heuristic.

### Invalidation

Hard invalidation includes at least:

- allocator reinitialization/shutdown;
- device loss/reset or descriptor-domain/device epoch replacement;
- allocator persistent-cache clear;
- incompatible structural resource-schema change;
- queue topology or export-contract change;
- policy or placed-capability change; and
- explicit abandoned published execution where the existing ownership contract requires full invalidation.

Graph-cache eviction drops only a weak association. Allocator plan metadata may outlive that weak association only if its own bounded cache policy and exact signature remain valid.

Idle lease instances may be trimmed/evicted under normal pressure while plan metadata survives.

### RF9 closure target

Warm 320 dedicated case should prove:

- zero broad generic CompileOperations interpretation for common uses;
- zero per-frame lane-color rebuild on an exact hit;
- zero normal pool compatibility search for ready plan lanes;
- zero routine reusable-index churn for ready lane instances;
- zero `CompileScopeEntryStates` production calls; and
- zero warm canonical descriptor Allocate/Write/Retire.

Ambitious program bar: **Resolve <= approximately 300 us p50** on the 320-resource stress workload.

This is a target, not a promised result. If the intended work is structurally deleted and Resolve remains materially above the target, profile the new remainder before defining another RF.

## Required workload matrix

Every RF performance gate uses matched warm windows from:

- normal quad: current baseline approximately **153.598 us p50 Resolve**;
- graph-only scheduler probe: approximately **140.333 us**;
- 64-resource RED-like mix: approximately **246.963 us**; and
- 320-resource stress ceiling: approximately **853.268 us**.

Do not infer broad frame wins from a dirty shared checkout. Profile causal phase/path deletion directly and distinguish critical-path wall-clock latency from aggregate worker CPU.

The preserved 64/320 probes do not enable placed resources. They cannot be used to claim placed fast-path wins.

## RF6 minimum correctness test matrix

- lane hit after prior submitted fences complete;
- one lane backing still in flight chooses another ready instance;
- all lane backings in flight falls back to ordinary acquire without CPU wait;
- stale generation-stamped lease identity rejected after pool slot reuse;
- trimming/pressure eviction safely removes an idle lane instance;
- terminal export consumes/detaches the dedicated backing;
- rollback before publication leaves previous cached plan untouched;
- cache clear and graph-plan association invalidation;
- device loss;
- allocator healthy shutdown;
- exact pool ownership/refcount proofs;
- pending GPU fences still block native reuse; and
- pressure retains authority over idle plan-associated resources.

## Program order

The implementation order is strict:

1. RF6 persistent dedicated lanes / lease fast path;
2. matched correctness + Profile closure;
3. RF7 dense semantic-use IR;
4. matched correctness + Profile closure;
5. RF8 shadow then production physical-state lowerer;
6. matched correctness + Profile closure;
7. RF9 bounded warm plan cache / cutover;
8. matched whole-program closure.

Do not implement RF6-RF9 simultaneously.

## Explicit non-goals / prohibitions

- Do not cache or reuse an old `ExecutionGenerationRef::Impl`.
- Do not permanently bind one lane to one native resource across CPU frames.
- Do not retain extra persistent RHI refs to preserve lane identity.
- Do not reuse native backings before submitted fences prove safety.
- Do not add CPU waits for persistent lane reuse.
- Do not use raw recycled pool indices as persistent identities.
- Do not treat `RenderGraphKey` as sufficient resource-plan identity.
- Do not persist imported native resources, readiness fences or swapchain serials.
- Do not weaken terminal export ownership transfer.
- Do not weaken device-loss, rollback or shutdown semantics.
- Do not delete named/explicit scope semantics before an equivalence proof.
- Do not select terminal state by latest BeginUse; use latest EndUse semantics.
- Do not bind exports to the mapping at RequestExport position; use final mapping semantics.
- Do not delete the final RHI native-resource entry-state sort.
- Do not optimize persistent placed backing behavior from the current 320 trace.
- Do not claim RF6-RF9 gains before matched profiling.

## Definition closure

The RF6-RF9 architecture definition is closed enough to begin RF6 independently.

RF6 entry condition is now:

- exact-current source reread before edits;
- preserve the current healthy shutdown two-phase contract;
- keep the current semantic compiler and state compiler authoritative;
- introduce only allocator/pool-owned persistent lane/lease metadata and exact-plan proof;
- preserve ordinary dedicated acquire as fallback; and
- preserve current `Finish` submitted-fence semantics exactly.

No RF6-RF9 implementation, build, test, native launch or new Profile capture was performed as part of this definition pass.
