# RenderFlow Warm Resolve Symbolic Execution Program

Status: **implementation authorized; first-delivery architecture frozen; source implementation owned by the prime agent. The first delivery uses allocator-owned cached snapshots plus per-generation copies. A zero-copy/refcounted packet-program representation is explicitly deferred until profiling proves it is needed.**

Date: 2026-10-11

This document is the durable design record for removing the remaining redundant per-frame compilation from warm `RenderFlowResourceAllocator::Resolve`. It is intentionally self-contained so implementation can continue after chat compaction without reconstructing the reasoning from conversation history.

## 1. Problem and measured baseline

The current warm resource-contract path already avoids replaying declarations and rebuilding logical lifetime topology. A bound `RenderFlowResourceContract` runs `PrepareBoundStructuralPatches`, then `InstantiateStructuralPlan`, and skips the cold `CompileOperations`/lifetime-discovery path. RF13 also retains a certified dedicated physical lane plan for cache-entry variants.

That still leaves substantial work proportional to the complete static graph on every frame. A fresh `ExecutionGenerationRef::Impl` is allocated, packet arrays are instantiated again, generation-bound step/action identities are patched, physical resources are checked out, physical state is simulated again, packet-local native resources are rediscovered and sorted again, and RF17 recompiles the exact barrier stream again.

The same-binary burn-free 320-resource measurement that triggered this study is the baseline for this program:

- whole-frame p50: approximately **2.842 ms**;
- mean `RenderFlow/Resolve`: approximately **780 us**;
- workload: **320 resources**, with the artificial CPU-burning loop removed so the measurement represents resource-use and graph-node scale rather than fake compute work.

Do not compare the implementation result against captures that used a different binary, the old fake-burning workload, or older allocator stages and call the delta causal. Historical RF18 evidence is still useful for locating cost: the exact-current RF18 320-resource stress capture recorded approximately 834 us mean Resolve, 464 us `FinalizeStates`, and 132 us nested `CompilePacketBarriers`, but that is context rather than the acceptance baseline for this change.

The goal is architectural: on an unchanged certified graph/contract/physical topology, Resolve should instantiate current owners and dynamic values instead of recompiling the resource-state program.

## 2. Current exact code seams

Line numbers below are exact for the shared checkout at study time and will move during implementation. Function names are the durable reference.

### RenderFlow Resolve

`source/rendering/src/render_flow_resource_resolve.cpp`:

- `PrepareBoundStructuralPatches` around line 842 validates current dynamic import/export/decision patches against the cached semantic contract.
- `InstantiateStructuralPlan` around line 1114 recreates generation-local allocations, packets, steps, action records, use ordinal maps, command scopes, queue dependencies, decisions and liveness from `PersistentStructuralPlan`.
- `BuildPersistentDedicatedCandidate` around line 1277 builds the dedicated allocation-to-lane topology retained across warm frames.
- `LowerPhysicalStates` around line 1484 walks physical history and packet steps to lower exact state transitions/actions.
- `BuildPacketResourceBindings` around line 2140 rediscovers packet physical resources, sorts them by current native `ResourceRef.value`, builds the packet-local binding table and rewrites step/action binding slots.
- `CompilePacketBarriers` around line 2299 builds packet-local state cells, seeds exact entry states, emits/coalesces transition/UAV barriers, and assigns RF17 native batch ids.
- the bound-contract warm branch is around lines 2950-2959;
- persistent dedicated-plan admission is around lines 4392-4449;
- current final native-state path calls `LowerPhysicalStates`, `BuildPacketResourceBindings`, and `CompilePacketBarriers` around lines 5257-5310;
- structural and physical candidates are published near the end of Resolve around lines 5436 onward.

`source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp`:

- `CompiledPacket` around line 927 currently mixes immutable semantic program data, current-generation native bindings, exact compiled barriers, and mutable execution runtime in one object.
- `PersistentDedicatedPhysicalProgram` around line 1286 is the existing bounded allocator-local physical topology cache. It is keyed by resource-contract identity/publication, RHI device incarnation, and descriptor-domain identity and retains strong reusable dedicated-pool leases.
- `PersistentStructuralPlan` is the resource-contract semantic topology image used by `InstantiateStructuralPlan`.
- `ResourceContractStorage` around line 1517 owns the persistent structural plan and normalized node templates.
- `ExecutionGenerationRef::Impl` owns one frame's current physical bindings, packets, imports/exports and execution metadata.

### Execution lifetime

`source/rendering/src/render_flow_resource_execution.cpp`:

- `ReleaseGenerationRuntimeOwnersInternal` around line 19 clears generation-local liveness owners, retained imports, presentation acquisition, pending exports, placed batch and physical bindings.
- `CompiledExecutionPacketView::OpenCursorInternal` around line 1021 admits `packet.resourceBindings`, then seeds `packet.entryStates` before resource recording.
- `RenderFlowResourceAllocator::Finish` around line 1820 validates terminal evidence, retires physical owners, releases retained descriptor borrows, and then calls `ReleaseGenerationRuntimeOwners` before dropping the allocator's generation reference.

`source/rendering/src/render_flow_resource_allocator.cpp`:

- `RenderFlowResourceContract::Reset` around line 389 immediately destroys/frees `ResourceContractStorage`.

This matters because an `ExecutionGenerationRef` held by a packet view can survive allocator `Finish`. The first implementation avoids a new cross-owner lifetime entirely by copying cached lowering into generation-owned arrays before publication. If a later zero-copy representation lets generations reference shared immutable program storage, that storage must then have a lifetime independent of raw `ResourceContractStorage` and physical-cache slots.

### RHI binding contract

`source/rhi/nvrhi/src/common_backend.cpp`:

- `CommonBackend::AdmitCommandListResources` around line 5003 requires admitted texture/buffer `ResourceRef`s to be strictly sorted and unique by `ResourceRef.value`.
- `CommonBackend::SeedCommandListStates` around line 5112 requires entry-state resources to be valid, unique, sorted, and complete for texture mip/slice state cells.

RF17 compiled barriers address packet resources by the index in this admitted table. Current native handle values can change every frame, so the final numeric barrier binding slot is not a cache-stable value.

## 3. Architecture decision

Introduce one allocator-owned immutable **`PersistentExecutionProgram` snapshot** alongside each `PersistentDedicatedPhysicalProgram`. It represents the already-proven execution/state/barrier lowering in generation-independent symbolic physical coordinates. Build it from a successful full Resolve after physical assignment, state lowering and RF17 barrier compilation have all succeeded. Reuse it only when both the semantic resource contract and the dedicated physical topology are certified unchanged.

On a certified warm hit, Resolve still performs the work that is genuinely frame-specific: current import/export patches, dedicated lease checkout or fallback acquisition, current physical binding creation, descriptor borrowing/publication, current incoming fence waits, current decision values, current execution generation/liveness, and native binding-slot remapping required by the RHI sorted-resource contract.

It does **not** re-run the general physical-state simulator or RF17 barrier compiler. For the first delivery it copies the cached lowered step/action/barrier data into the fresh generation, patches current generation identities and dynamic values, and remaps symbolic bindings to the current RHI sorted binding slots. This deliberately preserves the current generation-owned packet lifetime model.

The first implementation is deliberately restricted to the contract-backed dedicated-resource path where `PersistentDedicatedPhysicalProgram` already certifies an allocation-to-lane topology. Placed-resource mode continues through the existing full lowering path. Placed alias provenance changes the symbolic physical graph and must receive its own persistent certification before it can share this optimization.

## 4. Program ownership, copies and future refcount option

The first delivery does **not** add a new shared packet-program lifetime across cache entries and live execution generations. `PersistentDedicatedPhysicalProgram` owns its `PersistentExecutionProgram` snapshot by value (or by another allocator-owned nonescaping storage object with the exact same lifetime). A warm Resolve copies the lowered immutable packet data it needs into the fresh `ExecutionGenerationRef::Impl` packet arrays before publication.

That copy is intentional. It makes cache eviction and contract reset lifetime-safe with the existing public API:

- an active or retained execution generation points only at its own packet/step/action/barrier arrays;
- `RenderFlowResourceContract::Reset` may destroy `ResourceContractStorage` without affecting a published generation;
- evicting/replacing `PersistentDedicatedPhysicalProgram` may destroy its cached snapshot immediately because no published generation points into it;
- a held `CompiledExecutionPacketView` after `Finish` remains backed by its `ExecutionGenerationRef::Impl`, exactly as today.

Therefore **no new `PersistentExecutionProgram` refcount is required for the first implementation**. Existing generation refcounting remains the only lifetime mechanism visible to packet views.

The cached snapshot must own **zero** of the following:

- `rhi::Texture`, `rhi::Buffer`, `rhi::ResourceRef` that retain current native resources;
- `rhi::DescriptorHandle` values from the current descriptor domain;
- current `PhysicalResourceId` values carrying an execution generation;
- current import/export objects, presentation acquisitions or fence values;
- `PacketUseLiveness` or mutable packet execution cursors/state.

`Finish` must remain the ownership boundary for native resources and descriptors. `ReleaseGenerationRuntimeOwnersInternal` must still be able to clear physical bindings/imports/presentation/descriptor borrows while packet views retain their own copied compiled metadata plus generation-local stale-check state.

A later optimization may split `CompiledPacket` into a separately refcounted immutable `CompiledPacketProgram` plus a small per-generation runtime/binding overlay. That is **not** part of the first delivery. It should be attempted only if post-fast-path profiling shows packet copying is a material part of the remaining Resolve cost. If that later phase is implemented, the shared immutable program must then receive independent refcounting because contract/physical-cache storage can die while generation views remain alive.

## 5. Certification key and invalidation

A program is usable only when all facts that can change state/barrier topology are proven identical to those under which it was built.

The minimum certificate is:

1. exact `ResourceContractStorage::contractIdentity`;
2. exact resource-contract `publicationSerial`;
3. exact RHI device incarnation;
4. exact descriptor-domain identity;
5. dedicated-resource policy compatible with the compiled program, with placed resources disabled for the first implementation;
6. exact persistent dedicated physical topology: allocation-to-lane relation plus lane compatibility/capacity state represented by the active `PersistentDedicatedPhysicalProgram`;
7. exact packet/command-scope/queue topology implied by the bound contract;
8. exact symbolic physical state-descriptor signature for every program binding;
9. exact captured decision vector for the first implementation.

The first delivery does not require a second persistent identity mechanism if the execution snapshot is stored directly inside the `PersistentDedicatedPhysicalProgram` that owns the exact lane mapping. Replacement/eviction of that physical program destroys its snapshot at the same time. The warm path still validates the current structural allocation -> physical relation before restoring cached lowering. An explicit nonzero topology publication serial may be added if it materially simplifies diagnostics or tests, but co-ownership plus exact relation validation is sufficient for correctness.

This first version should prefer a conservative miss over comparing whole physical programs or trying to prove equivalence after rebuild. A later optimization may reuse a specialization across physically equivalent rebuilds using a stable topology fingerprint, but it is not needed to eliminate the recurring warm cost.

Existing contract patch validation remains authoritative. The execution-program cache must not broaden what a resource contract accepts. A dynamic import replacement, export-slot generation change, fence-value change or presentation acquisition is eligible only if the existing `PrepareBoundStructuralPatches`/binding path already accepts it under the semantic contract. For the first delivery, a changed captured decision value is a conservative execution-program miss even when the semantic contract itself remains valid; the full lowering path handles that frame and may publish a replacement snapshot. This restriction can be relaxed later only after proving the cached lowering is decision-independent for the relevant contract.

Events that force a program miss/invalidation include:

- contract republish or revision change;
- renderer/graph/queue topology change that selects a new contract publication;
- persistent dedicated physical-program eviction or rebuild;
- enabling placed resources;
- RHI device loss/recreation/incarnation change;
- descriptor-domain replacement;
- any dynamic patch rejected by the current resource-contract validator;
- any captured decision value differing from the cached first-delivery snapshot;
- any current physical binding whose state-descriptor shape disagrees with the symbolic program certificate.

Dynamic native handles by themselves do **not** invalidate the program. They are exactly what the mutable overlay is designed to patch.

## 6. Symbolic physical model

The symbolic program must not use logical allocation identity directly as the packet resource binding identity, because several non-overlapping logical allocations may reuse one dedicated physical lane. Current `BuildPacketResourceBindings` deduplicates by `PhysicalResourceId`, not by logical allocation.

Use a generation-independent **symbolic physical id** instead.

For the first dedicated-only specialization:

- allocator-owned transient/dedicated logical allocations map to the certified persistent dedicated **lane index**;
- retained imports and exported resources that cannot use a persistent lane receive stable symbolic physical entries derived from the structural allocation/patch slot in the contract;
- all steps/actions that resolve to the same certified physical lane reference the same symbolic physical id.

Each symbolic physical record stores only topology/state facts needed to validate and patch the current resource:

- resource kind (texture/buffer);
- expected creation/state descriptor class needed by barrier lowering;
- texture mip count and array size when applicable;
- buffer memory class where native barrier eligibility depends on it;
- initial-state/explicit-state contract;
- canonical-descriptor demand bits;
- source classification (dedicated lane, retained import patch, terminal/export-owned allocation if required).

The cold builder derives these records from the exact `PhysicalBindingRecord`s and persistent lane mapping that were used by the successful state compiler. Publication occurs only if every packet step/action can be translated from the generation physical id to exactly one symbolic physical id.

## 7. Cached snapshot contents

The first-delivery program stores the lowering output that is expensive to recompute and safe to copy into a fresh generation. A practical representation is a top-level certificate plus one cached packet-lowering record per packet. It does not need to replace the public/current `CompiledPacket` representation yet.

Top-level immutable data:

- certification key described above;
- command-scope/packet topology witnesses needed to prove the snapshot still matches the instantiated structural plan;
- symbolic physical records;
- exact structural-allocation -> symbolic-physical mapping used by the successful cold lowering;
- exact captured decision vector for the first implementation;
- counts/capacity witnesses used to reject corrupted or incompatible program data before publication.

Per-packet immutable data:

- node, flow group, command scope and queue witnesses;
- the post-`LowerPhysicalStates` step records normalized to symbolic physical ids/current-generation-free use identity;
- post-lowering exceptional action records and action-tape/barrier-span fields required to restore the packet exactly;
- begin/end maps and state-predecessor topology when these fields are touched by lowering or are convenient for exact equivalence validation;
- the packet's stable set of symbolic physical ids;
- cached entry-state cells expressed as `{symbolicPhysical, state, mip, slice}` or an equivalent compact grouped form;
- cached RF17 barrier templates expressed as `{symbolicPhysical, kind, before, after, subresource}`;
- cached RF17 barrier batch ids;
- the already-finalized barrier spans for each step/action and packet exit action;
- alias fields must be zero/empty in the first dedicated-only specialization; any nonzero placed alias metadata makes the candidate ineligible.

Any cached public identity stores only stable flow/ordinal topology. When copied into the new `CompiledPacket`, the patcher writes the current `sessionGeneration` so stale identities continue to fail exactly as today.

## 8. Per-generation copied instance and mutable overlay

The first warm path keeps `ExecutionGenerationRef::Impl` and `CompiledPacket` as the generation-owned execution representation. Every frame still instantiates the structural plan and owns fresh packet arrays/runtime. After current physical assignment is known and the snapshot certificate passes, cached lowered fields are copied into those packet arrays instead of being recomputed.

The generation owns:

- its own copied steps/actions/action tape/barrier spans and exact compiled barrier arrays restored from the snapshot;
- current `PhysicalBindingRecord`s and their native owners;
- retained imports, presentation acquisition, pending exports and current export slots;
- current packet resource bindings, physical ids and canonical descriptor handles;
- materialized `CommandListEntryState` entries containing current `ResourceRef`s;
- materialized `rhi::CompiledBarrier` entries containing current RHI numeric binding slots;
- current decision bool values;
- current incoming queue wait fence values;
- packet runtime state and generation-local `PacketUseLiveness`;
- current generation/session ids used by public stale checks.

The first implementation therefore has no pointer from a generation into the cache snapshot. If measured copy cost remains important after state/barrier recompilation is removed, the later end-state may split today's `CompiledPacket` conceptually into:

```text
CompiledPacketProgram (immutable, separately refcounted, cache-owned)
    +
PacketInstance / PacketExecutionRuntime (fresh per generation)
```

That later representation would let execution read immutable steps/actions/maps/tape from shared program storage and mutable bindings/runtime from the generation instance. It is an optimization follow-up, not a prerequisite for eliminating `LowerPhysicalStates` and `CompilePacketBarriers` from stable warm frames.

## 9. RHI sorted binding remap

This is the critical correctness constraint for the warm path.

`CommonBackend::AdmitCommandListResources` requires the current packet's `ResourceRef`s to be strictly increasing by `ResourceRef.value`. Resource handles can be replaced or recycled across frames, so the sorted order can change without any semantic topology change. RF17 barriers index the admitted table. Therefore a cached barrier cannot store today's final numeric `resourceBinding` and replay it blindly tomorrow.

The warm patcher performs this exact packet-local algorithm:

1. Start from the cached packet's stable symbolic physical set.
2. Resolve every symbolic physical to the current generation `PhysicalResourceId`, `ResourceRef`, and current canonical descriptor handles.
3. Validate kind/state-descriptor shape against the program certificate.
4. Build a small `ResolvedProgramBinding { symbolicIndex, physical, resource, srv, uav }` array.
5. Sort this array by current `resource.value`.
6. Reject the same impossible condition rejected today: two distinct symbolic physical identities resolving to one native resource identity. A repeated reference to one symbolic physical is deduplicated before this step.
7. Emit the packet's sorted `resourceBindings`, parallel current-generation `resourceBindingPhysicals`, SRV array and UAV array.
8. Build a dense `symbolicBinding -> nativePacketSlot` remap.
9. Materialize `entryStates` in sorted-resource order by copying the cached state cells for each symbolic binding and inserting the current `ResourceRef`.
10. Materialize RF17 `CompiledBarrier`s by copying cached kind/state/subresource fields and replacing symbolic binding with the remapped native packet slot. Cached barrier batch ids remain unchanged.

This retains the RHI's current validation and native execution model. Changing `AdmitCommandListResources` to accept arbitrary order would only move sorting/indexing work into command recording and widen the correctness surface. It is not part of this program.

The per-frame sort is intentionally retained as residual cost. It is bounded by the number of resources actually used by each packet, and it replaces much larger rediscovery/state-simulation/barrier-compilation work.

## 10. Cached state/barrier correctness

The exact RF17 barrier stream is reusable only because the semantic and symbolic physical topology are both certified.

The cold build is authoritative. `LowerPhysicalStates` and `CompilePacketBarriers` run normally and produce the exact successful program. The candidate builder then converts their current physical ids/native slots to symbolic physical ids and stores the resulting entry-state/barrier templates. It does not independently reimplement the state compiler.

On a warm hit, the patcher validates both the structural allocation-to-symbolic-physical relation and the current native state signature before restoring cached lowering. In particular:

- texture/buffer kind matches;
- texture mip/array state-cell cardinality matches;
- explicit vs automatic state semantics match;
- initial state contract matches;
- memory/usage facts that affect native barrier eligibility match;
- the active persistent lane mapping matches the snapshot's allocation-to-symbolic-physical relation;
- each structural allocation resolves to the same symbolic physical relation used by the cold program, after replacing only the current execution generation component of `PhysicalResourceId`;
- all explicit entry-state cells required by the program can be produced from the accepted current patch.

Because RF17 batch ids encode dependency separation by symbolic resource use and semantic fence boundaries, binding-slot permutation alone does not change them. The patcher may preserve cached batch ids after remapping symbolic ids to current native slots.

UAV RAW/WAW ordering, texture subresource exactness, automatic close-time restoration, explicit-state predecessors, Present semantics and clear ordering must remain byte-for-byte/field-for-field equivalent to the cold compiler result for the same certified topology.

## 11. Dynamic patch handling

The warm execution program is a compiled static skeleton with a deliberately small dynamic patch surface.

The following remain current-frame values:

- import native resource identity and retained owner;
- import readiness and incoming fence values accepted by the semantic contract;
- export slot generation/current slot mapping;
- current presentation acquisition and backbuffer resource;
- current canonical descriptor handles;
- current physical ids/generation;
- current decision bool values, although the first-delivery fast path requires them to equal the cached decision vector;
- current execution generation/session generation;
- current packet liveness/runtime state.

`PrepareBoundStructuralPatches` should continue to produce/validate these current values before the execution-program hit is admitted. The execution program consumes the already-validated patch image; it must not create a second looser contract validator. The extra first-delivery decision equality check is only a conservative specialization guard; a mismatch goes through the existing full state/barrier lowering rather than failing the semantic contract.

## 12. Publication and transactional rules

The cache must never publish a partially built execution program.

Cold/full Resolve sequence:

1. validate/instantiate semantic contract as today;
2. acquire/assign physical resources as today;
3. complete `LowerPhysicalStates`;
4. complete `BuildPacketResourceBindings`;
5. complete `CompilePacketBarriers`;
6. complete ordinary Resolve validation/finalization;
7. build a **candidate** symbolic execution snapshot from the proven result and the candidate/existing persistent dedicated topology;
8. validate that every generation-local reference can be translated to the symbolic form without native ownership escaping;
9. only after the frame is otherwise publishable, install/move the completed snapshot into the matching `PersistentDedicatedPhysicalProgram` together with its contract/state/decision certificate.

If candidate construction fails because the case is unsupported, Resolve may still succeed through the existing path; it simply does not publish the optimization. Candidate failure must not poison a valid semantic contract or a valid persistent dedicated physical plan unless the failure proves those objects themselves inconsistent.

Warm fast-path sequence:

1. validate contract patches;
2. locate matching persistent dedicated physical program;
3. locate its execution-program snapshot and validate the contract/topology/decision certificate;
4. instantiate/check out current physical owners using the existing proven dedicated-plan path;
5. validate the resulting structural allocation -> current physical relation and current native state signatures against the snapshot;
6. copy the cached lowered step/action/barrier metadata into the generation-owned packets while patching current generation identities;
7. build the current sorted RHI binding remap, entry-state table and remapped compiled barriers transactionally;
8. publish the fresh execution generation only after every packet is complete.

No partially patched generation becomes visible through `publishedGeneration`.

## 13. Failure and fallback rules

Correctness always wins over a warm hit. The program is an optimization artifact, never the source of truth for accepting new semantics.

Use these rules:

- **Certification miss before native acquisition:** take the existing full Resolve path. This is normal, not an error.
- **Semantic/dynamic patch rejection:** preserve the exact existing failure or cold-recompile behavior chosen by the renderer. The execution program does not override it.
- **Physical topology/snapshot mismatch:** full lowering path; successful completion may publish a replacement execution snapshot.
- **Placed-resource policy active:** full lowering path.
- **Current allocation->physical relation, decision vector, or native state signature disagrees with the snapshot:** treat it as a warm-specialization miss. Use the existing full lowering path for that frame after any required local patch scratch is discarded. Only classify it as `BackendContractViolation` if the mismatch violates an invariant already guaranteed by the active persistent physical program rather than representing an allowed dynamic difference.
- **Native allocation/descriptor publication/backend failure:** propagate the same failure semantics as today. Do not silently retry a second full Resolve after partially acquiring resources unless the existing transaction has been completely rolled back.
- **Program internal corruption/impossible symbolic mapping:** `BackendContractViolation`, invalidate the specialization, and do not publish the generation.
- **Out of memory while materializing the mutable overlay:** `CapacityExceeded`/existing allocation failure path; no partial publication.

An external native allocation failure must not invalidate an otherwise valid cached execution snapshot. A later frame can still use it if certification remains valid.

## 14. Statistics and profiling evidence

Add explicit counters so the optimization can be proved rather than inferred:

- execution-program builds;
- execution-program hits;
- execution-program misses by broad reason (no program, physical topology mismatch, policy/placed, state-shape mismatch); avoid an unbounded diagnostic taxonomy;
- optional total symbolic bindings/barriers patched on hits.

Add focused Profile zones around the new work, for example:

- `RenderFlow/Resolve/WarmProgramPatch`;
- `RenderFlow/Resolve/WarmProgramBindings`.

The strongest hit witness is negative evidence: on a certified warm program hit there must be **zero** calls/occurrences of:

- `RenderFlow/Resolve/LowerPhysicalStates`;
- `RenderFlow/Resolve/CompilePacketBarriers`.

If `BuildPacketResourceBindings` is replaced by the warm remapper, give the remapper its own zone rather than reusing the old compiler zone.

## 15. Correctness verification matrix

Existing focused tests already contain the right difficult cases and should be extended rather than replaced with shallow cache-hit tests.

### Contract and dynamic patch proofs

Extend `RunResourceContractDynamicPatchProof` in `source/rendering/tests/geometry_allocator_tests.cpp` to prove a cold program build followed by certified hits while exercising:

- import A -> B replacement under the same semantic descriptor/state contract;
- recycled native resource index with an advanced generation (ABA rejection of the stale ref);
- increasing incoming fence values/current waits;
- export-slot generation/order changes accepted by the contract;
- submitted success and aborted frame behavior;
- unchanged decision vector producing an execution-program hit;
- changed decision value producing a conservative execution-program miss/full lowering without invalidating the semantic contract, followed by replacement snapshot publication if the full Resolve succeeds;
- injected native failure followed by a later valid warm hit without corrupting the program.

### Presentation

Extend `RunPresentationImportExecutionTest` to prove two different real swapchain acquisitions can use one execution program while the current backbuffer resource is patched and Present semantics remain correct.

### State and barrier parity

Run the same cold-vs-warm specialization through:

- `RunAutomaticExplicitStateParityTest`;
- `RunExplicitTextureSubresourceStateTest`;
- `RunPartialTextureUavBarrierRejectionTest`;
- `RunExplicitTextureCoalescedTransitionStateTest`;
- existing cross-queue fork/join and import/export state coverage.

The warm result must preserve exact required states, UAV ordering, subresource runs, barrier counts/batches where topology is identical, clear ordering, and terminal state behavior.

### Native binding permutation

Add a focused proof where current resources behind two symbolic bindings change native handle order across frames. The warm program must still:

- admit a strictly sorted unique `resourceBindings` table;
- remap entry states to the new sorted slots;
- remap every cached barrier to the correct resource;
- execute successfully on real D3D12.

This is the regression test for the central RHI constraint.

### Lifetime and invalidation

Add tests for:

- holding a `CompiledExecutionPacketView`/generation reference past `Finish`, then resetting/evicting the resource contract/physical program; the generation's copied packet/barrier arrays must remain memory-safe while native owners/descriptors are already released and the cache snapshot is already destroyed;
- stale prior-generation use/decision/range/packet ids remaining rejected;
- filling/evicting the four-entry persistent dedicated physical-program cache: semantic contract may remain warm, but an evicted physical specialization must miss and rebuild without stale program reuse;
- renderer/graph/queue revision selecting a new contract;
- descriptor-domain replacement;
- RHI device loss/reinitialization;
- placed policy forcing the existing full path.

## 16. Performance gates

Performance claims must use the burn-free workload and a controlled Profile comparison. Record executable hash, host/window conditions, resource count, warmup/capture window, and the exact counters proving program hits.

Primary 320-resource gate:

- baseline: approximately **780 us mean Resolve** on the same-binary burn-free study workload;
- first implementation target: **<= 300 us mean Resolve** on stable certified warm hits;
- expected engineering residual/floor for this design before further measurement: approximately **180-250 us**, representing current physical owner checkout, import/export/descriptor patching, required per-packet native binding sort/remap, generation/runtime setup, and publication/cleanup;
- do not promise `<150 us` Resolve until post-change profiling identifies and removes additional residual work.

The 180-250 us figure is a planning estimate, not a measured guaranteed floor. It should be replaced by measured residual zones after the first fast path exists.

Secondary gates:

- zero warm `LowerPhysicalStates` and `CompilePacketBarriers` occurrences on certified hits;
- one program build followed by monotonically increasing hit count for an unchanged variant;
- no increase in GPU work/barrier semantics for the same topology;
- no correctness-suite regression in Debug or Development;
- no meaningful regression at 64 resources/normal graph where the absolute savings are smaller;
- report whole-frame p50 against the approximately 2.842 ms burn-free baseline, but do not convert the ~480 us Resolve opportunity into a guaranteed wall-frame saving because scheduling/overlap/noise can change the realized frame delta.

If the first implementation lands above 350 us mean Resolve while proving true program hits, profile the residual before adding another cache layer. Likely residual candidates are physical checkout/descriptor work, immutable packet copying left in the transitional representation, or packet binding sort/materialization.

## 17. Practical implementation sequence

The prime agent owns source implementation. The safest sequence is:

### Phase A - allocator-owned snapshot model, no fast path

1. Add `PersistentExecutionProgram` cached snapshot structures containing symbolic physical/state/barrier lowering data but no current native owners.
2. Store the snapshot by value with `PersistentDedicatedPhysicalProgram` and clear it whenever that physical cache entry is cleared/replaced. Add a topology serial only if it materially helps diagnostics; correctness relies on co-ownership plus exact mapping/signature validation.
3. Add stats/counters and invalidation paths. Do **not** add a generation pointer/refcount to the snapshot in this phase.
4. Add/extend a lifetime test proving a published generation remains valid through held packet views after the owning persistent physical cache entry is evicted, because the generation owns copied compiled arrays.

### Phase B - cold candidate builder and equivalence oracle

5. After successful existing RF17 compilation, build a symbolic candidate by translating current packet physical ids/native binding slots back to certified symbolic physical ids and recording the allocation->symbolic-physical relation, native state signatures and decision vector.
6. Reject unsupported placed/alias cases from candidate publication.
7. In tests, reconstruct/materialize a packet from the candidate and compare its restored lowering, entry states, barrier fields/batches, spans, use maps and action topology with the authoritative cold packet. Do not execute through the candidate yet.

### Phase C - dedicated warm restore and RHI remap

8. Keep `PrepareBoundStructuralPatches` and `InstantiateStructuralPlan` as the current semantic/generation setup for the first delivery.
9. On bound-contract + persistent-dedicated-plan hit, validate the snapshot certificate and exact decision vector.
10. Reuse existing physical lane checkout/import/export/descriptor ownership logic.
11. After physical assignment, validate the structural allocation->current physical relation and current native state signatures against the snapshot.
12. Copy cached lowered step/action/tape/barrier-span fields into the generation-owned packets while rewriting current execution/session generations and current physical ids.
13. Implement packet-local symbolic binding resolution, native `ResourceRef` sort, duplicate rejection, symbolic-to-native slot remap, entry-state materialization, and compiled-barrier materialization.
14. Route certified hits around `LowerPhysicalStates` and `CompilePacketBarriers`; any specialization miss takes the existing full lowering path.
15. Add the native binding permutation test and the cold/warm real-D3D12 state/barrier parity tests.

### Phase D - profile the copied implementation before changing lifetime architecture

16. Profile stable warm hits and measure the residual contribution from `InstantiateStructuralPlan` plus snapshot-to-generation packet copies separately from physical checkout and binding remap.
17. If the <=300 us gate is reached and copying is not material, stop. The copied representation is the preferred simpler lifetime model.
18. Only if copying remains material, design the separately refcounted immutable `CompiledPacketProgram` + per-generation runtime/binding overlay, then re-run stale-id, cache-eviction and post-Finish held-view proofs before enabling it.

### Phase E - profile and close

19. Profile the exact burn-free 64/320 workloads with program hit counters and new zones.
20. Confirm the primary <=300 us mean 320-resource gate or profile the remaining residual if missed.
21. Preserve captures/results and update this document or a closure record with exact hashes and measured numbers.

Only after this is closed should placed-resource symbolic topology or an RHI admission redesign be considered. They are separate correctness programs and are not prerequisites for the dedicated warm-path win.

## 18. Non-goals and invariants that must not be weakened

This change does not remove resource-state tracking, GPU barriers, UAV ordering, alias safety, imported/exported state contracts, queue synchronization, descriptor lifetime tracking, or execution-generation stale checks. It removes repeated **compilation of an unchanged proven program**.

The following remain non-negotiable:

- barriers still execute every frame when required by the cached program;
- a changed semantic contract or physical topology cannot reuse the old program;
- every native barrier addresses the current packet's sorted admitted resource table;
- texture mip/slice state remains exact;
- explicit-state predecessor/terminal proof remains exact;
- imports, exports and presentation acquisitions remain current-frame owners;
- current descriptors remain generation/domain correct;
- terminal receipt validation and pool retirement remain generation-local;
- stale public ids remain rejected after terminal completion;
- device loss and descriptor-domain recreation cannot revive old native chains;
- a cache miss always has the existing full compiler as the correctness fallback.

## 19. Exit definition

This architecture is complete when the dedicated contract-backed warm path behaves as an instantiation/patch operation rather than a compiler pass:

```text
validated semantic patches
    + certified dedicated physical topology
    + current physical/native owner checkout
    + packet-local sorted binding remap
    + current generation/runtime publication
```

The warm path should no longer scale with the total number of unchanged state transitions by re-running the state machine. Its remaining scale should come from resources that must actually be bound/owned this frame, dynamic patches, packet binding remap, and execution runtime objects that genuinely belong to the new generation.

The first closure report must include exact program hit/build evidence, correctness suites, same-method burn-free profile captures, residual-zone measurements, and any fallback cases still using the full compiler.

## 20. Implementation checkpoint (2026-10-11; source work underway)

This section is the compaction resume point. It supersedes the earlier
read-only-study status but **does not** claim performance closure.

Changed implementation files:

- `source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp`:
  added `PersistentStateProgram`, `PersistentStatePacket`, native-state
  descriptor witnesses, generation-independent entry cells, per-frame slot
  remap scratch; cache is owned by `PersistentDedicatedPhysicalPlan`.
- `source/rendering/src/render_flow_resource_resolve.cpp`: added
  `CapturePersistentStateProgram`, `ValidatePersistentStateProgram`,
  `RestorePersistentStateProgram`; certified dedicated warm hits bypass
  `LowerPhysicalStates` and `CompilePacketBarriers`, then reuse
  `BuildPacketResourceBindings` and patch cached barrier slots to the current
  native admission order. Capture occurs only after cold full compilation
  and completed action-tape finalization. Placed policy and all misses retain
  the previous compiler. Cache snapshots own no native resources/handles.
- `source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp`:
  added program hit/build/miss counters.
- `source/rendering/tests/geometry_allocator_tests.cpp`:
  dedicated real-RHI cold/warm smoke now requires one build and one hit.

Validation completed at this checkpoint:

- x64 Development `rendering` and `geometryAllocatorTests` compile/link PASS;
  the first full `geometryAllocatorTests.exe` run passed all cases (including
  dynamic import/fence/export, subresource/UAV, explicit/automatic state,
  placed alias, presentation, device-incarnation tests).
- After counters/assertions were added, targeted `rendering.vcxproj` and
  `geometryAllocatorTests.vcxproj` /m:1 with
  `/p:BuildProjectReferences=false` PASS and full
  `geometryAllocatorTests.exe` PASS again, including new cache-hit assertions.
- The intermediate broad Development rebuild hit an unrelated locked
  `nvToolsExt64_1.dll` RED compatibility copy, so the follow-up used
  existing built dependencies; this is not a RenderFlow compilation failure.
- `git diff --check` for source changes passed (only expected LF/CRLF notices).

Outstanding gates before claiming closure:

1. Profile x64 build of the same new executable and remote burn-free
   normal/64/320 resources; require verified program hits and zero warm
   `LowerPhysicalStates`/`CompilePacketBarriers` zones.
2. Strengthen tests for a changed decision causing a compiled-program miss,
   native resource-binding permutation, and retained packet view after
   persistent cache removal.
3. Investigate any missed <=300us mean Resolve target as measured residual
   rather than layering speculative micro-optimizations.
4. Run Debug and Development focused suites after all corrections, and
   preserve final result and trace paths here.

Keep existing broad dirty checkout unchanged outside the above owned paths.

## 21. First measured implementation result (2026-10-11, 09:12)

This is the **current resume checkpoint**. The work is functioning, but the
primary performance target remains open.

### Final source shape at this checkpoint

The first warm restore materialization copied a compiled program and then
called `BuildPacketResourceBindings`, resulting in two native sort/remap
passes. Its 320-resource warm Resolve averaged ~601 us, and
`BindCachedStateProgram` alone averaged ~258 us.

The current improved implementation **eliminates that redundant binding
pass**. One current-native-ResourceRef sort per packet directly produces the
admitted table, entry-state grouping, descriptor witnesses, and a cold symbolic
binding -> new native slot permutation. Every step/action/compiled-barrier
binding is patched from that one permutation. On certified warm hits,
`BuildPacketResourceBindings`, `LowerPhysicalStates`, and
`CompilePacketBarriers` are all skipped. The existing full compiler remains
the fallback when certification fails and for placed resources.

### Captures (all 1280x720 remote i7-4790K/GTX 1060; 18 s capture, final 12 s)

Final implementation executable SHA-256:
`C2A9D2017D7CD240E264A53701A1622A036E74FFF3234DF1E83C51260680BDDE`.

| Workload | Burn-free pre-change p50 | Current p50 | Improvement |
| --- | ---: | ---: | ---: |
| Normal renderer | 1.174676 ms | **1.111282 ms** | 0.063394 ms |
| 64 resources / 90 graph jobs / 15 lists | 1.657696 ms | **1.579294 ms** | 0.078402 ms |
| 320 resources / 90 graph jobs / 15 lists | 2.841659 ms | **2.583703 ms** | 0.257956 ms |

Measured 320-resource Resolve mean: **~780 us -> 474.700 us**.
Latest warm 320 trace:

- Resolve: 6,470 calls mean 474.700 us;
- cached binding: **6,461 hits**, mean 130.934 us;
- `LowerPhysicalStates`: **9 cold calls only**, zero warm calls;
- `CompilePacketBarriers`: **9 cold calls only**, zero warm calls;
- Setup: 133.237 us mean;
- PhysicalAssignment: 195.523 us mean;
- FinalizeStates: 141.500 us mean (includes cached binding).

Files under `build/profiling/desktop_runs/`:

- `rf_warm_program_resources320_20261011_090232` (earlier copying implementation);
- `rf_warm_program_direct_bind_resources320_20261011_090657` (**current**);
- `rf_warm_program_direct_bind_resources64_20261011_091046`;
- `rf_warm_program_direct_bind_normal_20261011_091209`.

### Correctness and remaining scope

- Focused x64 Development `geometryAllocatorTests.exe`: PASS, including
  the new cold-build/warm-hit assertions and changed-decision
  invalidation-miss assertion; all pre-existing dynamic import/export,
  state/UAV/subresource, placed alias, device/RHI and presentation cases PASS.
- Focused x64 Development `renderingTests.exe`: PASS (Render Graph,
  Resource Flow Allocator, viewport and texture upload candidate cases).
- x64 Profile rendering/runtime builds: PASS.
- No full Debug-suite verification yet, and no specialized two-binding
  order-permutation or retained-view-after-eviction regression yet.
- The measured ~475 us Resolve **does not satisfy** the <=300 us primary
  target. The remaining costs (~133 us execution-generation construction,
  ~196 us physical assignment, ~131 us program binding) are now measured.

Do **not** claim the initial <=300us goal is achieved or call the project
closed. Any next architectural work should target repeated generation object
materialization and physical-owner checkout only after a fresh read-only
design proving significant removable work. Do not reopen the already-solved
state transition compiler. Preserve the existing dirty checkout and this
document across compaction.

## 22. Resumed audit, regressions and isolated warm-path costs (2026-10-11, 09:37)

**Still OPEN against <=300 us.** The original 474.700 us and 2.583703 ms
measurements in §21 remain the last minimally instrumented accepted comparison.
This checkpoint records correctness work and a *diagnostic* new 320-resource
capture; a material architectural performance cut has **not** been established.

### Independent exact-current audit and fixed medium findings

Worker-40 independently reviewed `CapturePersistentStateProgram`,
`ValidatePersistentStateProgram`, `RestorePersistentStateProgram`, import and
export state/queue/readiness witnesses, dynamic incoming-fence *value*, native
ResourceRef sorting and admission, generation IDs, RHI barriers, physical-plan
eviction and packet-view ownership. Initial finding: **0 BLOCKER, 0 HIGH,
3 MEDIUM**; all three were repaired in this continuation:

1. A cached restore internal failure now supplies caller `failure` to
   `FailResolve` instead of `nullptr`, so the backend contract violation is
   observable even when no earlier callee set the failure record.
2. The RF12 dynamic import/resource/fence/export patch proof now asserts
   **cold state-program build=1, warm state-program hit=1, miss=0** in addition
   to its RHI execution, fence-value, stale native-generation and structural
   reuse checks.
3. The four-entry persistent dedicated cache test retains a
   `CompiledExecutionPacketView` from the oldest program across physical-plan
   eviction and explicit `ClearPersistentCaches`, observes intact metadata,
   and requires stale terminal use to fail with `InvalidPhase`. Packet arrays
   are generation-owned, not borrowed from the evicted cache entry.

An additional independently found warm policy flaw was fixed: after a changed
decision causes a correct full-compiler miss on a matching *physical* plan,
the successfully recompiled `PersistentStateProgram` now replaces only that
plan's optional symbolic snapshot. Failure leaves the old valid snapshot
untouched. The regression checks changed-decision miss/build, then a further
stable frame's hit, avoiding a repeated full compiler on every new-decision
frame. This is optional optimization publication, not resource ownership or
semantic contract republishing.

New real RHI fixture forces sorted resource binding permutation by allocating
native import A, stationary anchor, native import B, proving native handle
value(A) < value(anchor) < value(B), or its complete reverse. The cold frame
binds A; warm frame binds B, crossing the stationary handle order while
retaining correct import binding, RF17 barriers and descriptor proof. Four
separate warm rejection tests mutate imported initial state, terminal state,
initial queue and terminal queue; they must reject the previously published
structural contract with `DescriptorConflict` before cached program admission.

VS2022 x64 Development `rendering` and `geometryAllocatorTests` builds and
full `geometryAllocatorTests.exe`: **PASS** after all above changes. Debug
`rendering`, `geometryAllocatorTests` and the full executable: **PASS** after
all above changes. Development `renderingTests` had passed in §21 and its
production APIs were untouched here; it has not yet been rerun.

### Fine-grained diagnostic Profile

Controlled same-host Profile executable SHA-256:
`8613BC09047F561EF8F481264FDA4CF066EDF3BD5CCA302208EBC2AD08A6CB4D`.
Remote run `rf_warm_secondstudy_zones_current_320_20261011_093444` used
1280x720, the unchanged burn-free 90-job/15-list/320-resource workload, and
an 18-second Tracy capture. It completed capture with 6819 frames, 19.06 s
span and 11,563,228 zones. Trace recovered locally to
`build/profiling/desktop_runs/rf_warm_secondstudy_zones_current_320_20261011_093444/capture.tracy`;
the 391 MB per-zone CSV caused the scheduled runner to miss the final JSON
deadline, although the trace, frame CSV and normal runtime logs existed.
The local final-12-second frame CSV yielded 4459 frames, p50 **2.590106 ms**
and p95 3.510606 ms. Treat this run as *diagnostic only*; the huge per-lease
Tracy instrumentation alters the hot-path measurement.

| Instrumented zone | Count | Mean/call | Per-Resolve equivalent |
| --- | ---: | ---: | ---: |
| `RenderFlow/Resolve` | 6,590 | 495.746 us | 495.746 us |
| `Setup` | 6,591 | 133.140 us | ~133.140 us |
| `InstantiateStructuralPlan` | 6,582 | 119.768 us | ~119.6 us |
| `PhysicalAssignment` | 6,590 | 214.998 us | 214.998 us |
| `WarmLeaseCheckout` | 2,756,456 | 205 ns | ~85.829 us |
| `FinalizeStates` | 6,590 | 143.360 us | 143.360 us |
| `ValidateCachedStateProgram` | 6,581 | 9.089 us | ~9.1 us |
| `BindCachedStateProgram` | 6,581 | 133.160 us | ~133 us |
| `WarmPacketNativeBindings` | 98,715 | 3.050 us | ~45.7 us |
| `WarmPacketProgramPatch` | 98,715 | 5.779 us | ~86.6 us |
| `LowerPhysicalStates` | 9 | 167.639 us cold only | no warm invocation |
| `CompilePacketBarriers` | 9 | 106.663 us cold only | no warm invocation |

The nested zone rows **cannot** be added to frame wall time; packet and
lease counts normalize their total measured durations over 6590 Resolves.
The 2.75-million lease subzones are especially intrusive: ~85.8 us is the
*inclusive* instrumented sum, **not** proof that one pool spin-lock costs that
much in a production build. After recovering this trace the high-cardinality
`WarmLeaseCheckout`, `WarmPacketNativeBindings` and
`WarmPacketProgramPatch` zone wrappers were removed from source, leaving the
per-frame `InstantiateStructuralPlan`, `ValidateCachedStateProgram` and
existing outer stage zones for affordable future captures.

### Coherent second architectural direction (study, not performance claim)

Worker-37 reviewed exact-current cost and recommended a **prebound dedicated
compiled physical variant** layered on the immutable symbolic state program:
cache static packet/action/barrier/lane semantics, then reuse one fully sorted
native binding permutation and prepared per-packet metadata while its exact
dedicated lease set remains valid. Per-frame overlays must still carry current
execution/session generations, native owner proof, descriptor handles,
fences/waits, liveness and terminal status. Native RHI resource admission and
`SeedCommandListStates` must continue every command list. Dedicated internal
resources first; fail closed to legacy for placed aliases, unknown exports,
imports or presentation until separate witnesses are proved. Bounded variants
are keyed by immutable contract identity/publication, device/domain scope,
decision values, exact lease serial+store serial, state description, current
native refs/descriptor identities and queue topology; a stale/unavailable
lease or policy mismatch cannot publish a cached concrete plan.

One separate implementation workstream is developing transactional
`TryAcquireLeaseSet` support in `render_flow_resource_pool.cpp`/its internal
declaration, with exact preflight under one lock and all-or-nothing acquisition;
another is studying precise prebound packet variant seams. Neither should
be described as validated until it is integrated, tested, and profiled.

**Next gates:** integrate only a demonstrably correct second cut; rerun VS2022
Development/Debug focused suites, direct 320 burn-free baseline-style Profile
without fine-grained per-lease zones, plus normal and 64 resources; preserve
SHA, frame p50 and Resolve mean, counts and cost deltas. If <=300 us is not
met, report the residual honestly and keep this task open.

## 23. Deferred structural execution and measured no-batch checkpoint (2026-10-11, 10:45)

**Status:** Implementation and same-host controlled comparison exist; the
<=300 us warm 320-resource Resolve objective is **not yet met**. All GPU
barriers remain issued, and every frame still performs current native binding
admission, per-lease/fence checks and generation-owned publication. Do not
confuse overlapping Tracy subzones with additive frame-wall durations.

### What was changed

After the first cached symbolic state-program implementation, hot
`InstantiateStructuralPlan` was copying a structural `CompiledStep` array and
rewriting session generations every warm frame, even though the subsequent
`RestorePersistentStateProgram` immediately discarded and replaced that array.
`StructuralPacketTemplate` now retains frozen `beginStepByUseOrdinal`,
`endStepByUseOrdinal` and `ImportEvent{logicalAllocation,beginsUse,terminalPresent}`
streams. On a candidate state-program warm hit, `InstantiateStructuralPlan(...,
deferSteps=true)` publishes the dense use maps, decisions, current import
waits/presentation acquisition and per-generation liveness but does not
construct the redundant step array. `CompiledPacket::structuralStepCount`
supports validation of that deferred representation.

The cached state program then reconstructs generation-owned steps and native
binding slots from its compiled symbolic program; a cache mismatch calls
`MaterializeDeferredStructuralSteps` before the old full state/barrier
compiler. Dynamic decision changes trigger one full recompilation and
transactionally refresh the cached symbolic state-program snapshot. Device,
descriptor-domain, contract-publication, native sorted-resource-table and
physical descriptor-state witnesses remain authoritative.

### Why the experimental lease set was rejected

A separate `DedicatedResourcePool::TryAcquireLeaseSet` preflight/checkout
transaction was integrated and tested but then **completely removed** from
the active path. It added 39.960 us per warm 320-resource frame before
structural deferral and 32.494 us after deferral, without eliminating the
main resource-assignment walk. The batch-only run regressed Resolve to
502.498 us mean. The existing independent `TryAcquireLease` path remains
the accepted ownership/fence mechanism. Batch API, counters, scratch arrays,
integration and isolated regression fixture have been removed. Do not cite
the batch path as an active optimization.

### Comparable controlled Profile evidence (i7-4790K / GTX 1060)

All below are 1280x720 foreground-verified 18-second captures, reporting
the final 12-second full-frame window; same host, real command lists,
320 resources (240 textures/80 buffers), 640 use events and 90 graph jobs
with no artificial CPU-burning loop.

| Version | Resolve mean | Frame p50 | Notes |
| --- | ---: | ---: | --- |
| Original burn-free | ~780 us | 2.841659 ms | Correct baseline, no fake burn |
| First cached symbolic program | 474.700 us | 2.583703 ms | 6461 warm hits of 6470 |
| Deferred structural steps + experimental lease batch | 409.274 us | 2.537831 ms | Batch removed afterward |
| **Deferred structural steps, no batch** | **423.030 us** | **2.471566 ms** | **7352 warm hits of 7361** |

No-batch run:
`build/profiling/desktop_runs/rf_lazy_structural_nobatch_320_20261011_100823`.
Result JSON `ok=true`, `validRendererThroughEnd=true`,
`foregroundVerified=true`, 1280x720, 4666 final-window frames, p95
3.460278 ms. Profile executable SHA-256:
`8B8D51FBCF9FE9DD7D4161419964A825BCB68B5E7905188FC58CF5048971FF9E`.
No `BatchLeaseSet` zone appears. Mean warm-zone measurements:
`InstantiateStructuralPlan` 47.671 us (7352 calls);
`Setup` 59.218 us (7361);
`PhysicalAssignment` 198.725 us (7361);
`BindCachedStateProgram` 150.734 us (7352);
`FinalizeStates` 160.671 us (7361);
`ValidateCachedStateProgram` 8.910 us (7352).
`LowerPhysicalStates`, `CompilePacketBarriers`,
`CompileOperations` and `ValidateLifetimes` each ran only nine times,
not once per certified warm frame.

The removal improved frame p50 compared to the experiment while Resolve
mean increased 13.756 us. Different captures have host jitter; the evidence
does not support claiming that a single subzone delta accounts for the
whole wall-frame improvement.

### Tests and next architecture seam

Post-batch-removal Development `geometryAllocatorTests.exe` and
`renderingTests.exe` both passed. Important covered properties include real
D3D12 native-binding sort inversions, dynamic imports/fences, import
initial/terminal state and queue contract mutation, changed-decision
recompile-then-hit, retained packet view after cache eviction, placed fallback,
device-loss and presentation. Debug revalidation after source stabilizes.

The next candidate cut is to precompile the dedicated pool's immutable
per-allocation request with the exact persistent physical plan; a certified
warm frame can then reuse the request but must still acquire a live physical
owner, enforce every lease/fence epoch and publish current canonical
descriptors. Code is under test/profile comparison; **no microsecond
benefit is claimed until a controlled run**. More ambitious sharing of
immutable packet arrays would require generation-local overlays, terminal
packet-view lifetime proof across eviction, and exact sorted RHI native slot
remapping; it has not been implemented or validated here.

### Measured precompiled dedicated request improvement

The next exact-contract change was built under Development and Profile and
the full Development `geometryAllocatorTests.exe` suite passed. Add
`PersistentDedicatedPlanAllocation::dedicatedRequest`, populated once in
`BuildPersistentDedicatedCandidate` from the same canonical
`BuildDedicatedResourceRequest` previously recomputed per logical allocation
every warm Resolve. After exact contract or standalone descriptor/queue
matching, `ResolveFrame` uses this immutable physical request for non-export
dedicated allocations. Every actual lane still performs current-frame
`TryAcquireLease`/fallback `Acquire`, native owner publication, capacity,
descriptor and fence checks. Dynamic imports, exports and placed resources
remain on their earlier paths.

The controlled run
`rf_warm_precompiled_dedicated_request_320_20261011_104151` used Profile
executable SHA-256
`76F683E0D168B66F461F1B0C130B0A0340EBA295E9D22A444FB197E586937AE4`
and passed renderer, foreground and dimensions checks; 4740 frame-window
samples. Resolve mean **387.752 us** over 7116 calls, warm bind 7107
calls; frame p50 **2.438703 ms** (p95 3.248878 ms). PhysicalAssignment
mean **165.727 us**, cached bind 149.054 us, Setup 58.849 us and
InstantiateStructuralPlan 48.461 us. Versus no-batch immediately before
this change, Resolve mean improved **35.278 us**, physical assignment
**32.998 us** and frame p50 **0.032863 ms**. These are separate captures,
not a same-frame A/B, and should not be overinterpreted beyond the
consistent reduction.

Debug `rendering.vcxproj`, `geometryAllocatorTests.vcxproj` and the full
Debug `geometryAllocatorTests.exe` also passed against the precompiled
request implementation. Subsequent work is evaluating native-order proof
and bulk immutable-payload cloning on the packet bind path; neither
may be called a measured improvement until separate Profile results exist.

## 24. Warm packet/structural bulk-cloning experiments and remaining 300 us gap

All experiments below preserve exact current RHI native-binding admission,
generation-local packet publication, pool lease/fence checkout, descriptor
ownership and RF17 compiled-barrier execution. The original no-batch 320
run in §23 remains the control. Separate 18-second same-host/1280x720
captures are not paired per-frame A/B tests: only differences larger than
their apparent noise warrant attribution.

1. **Native sorted-binding proof:** `RestorePersistentStateProgram` builds
   the current native `ResourceRef` table and checks strictly increasing
   identity in the cold program's binding order. It only invokes
   `std::sort` when the current ref table is not strictly ordered. It
   always validates and remaps the current slots, including the inverse
   permutation tested by the D3D12 retained-import regression. Run
   `rf_precompiled_request_sorted_fast_320_20261011_104719`, SHA
   `022225C542650FB532244D5489E7CCFA6E27847D1FAF8BCD9587168093B2AEC4`:
   Resolve **385.789 us** (6724 calls; 6715 cached), binding 147.235 us,
   PhysicalAssignment 166.533 us; frame p50 **2.467804 ms**.
2. **Bulk immutable packet-image copy + generation-local patch:** RED
   `DynArray` typed copying already dispatches trivial element arrays to
   bulk memcpy. Replaced `CopyStateArray`'s per-element `PushBack` loops
   with `target = source`; `RestorePersistentStateProgram` clones
   compiled steps/actions/exit actions/barriers and subsequently patches
   current session/execution-generation, physical slots and native sorted
   binding ordinals **in the generation-owned copies**. Entry states and
   canonical descriptor bindings still derive from current physical
   owners. Run `rf_bulk_packet_clone_320_20261011_104941`, SHA
   `5A2ABA15430E60417DD8C33B863BDADD4A16DE71F101657E1EAF9B9BAB877CBD`:
   Resolve **385.052 us** (7013 calls; 7004 cached),
   BindCachedStateProgram **143.416 us**; frame p50 **2.454640 ms**.
   Both Debug and Development allocator suites passed this modification.
3. **Avoid re-copying canonical dedicated requests:** On certified
   dedicated program hits, preserve the exact saved
   `DedicatedResourceRequest` by const reference. Set its
   `requiresCanonicalDescriptors` from the cold physical-plan allocation
   when building the plan. Cold/export paths still build and validate a
   current request. Run
   `rf_precompiled_request_ref_320_20261011_105340`, SHA
   `78DE84A629435610181F95C2DEF090E3EF13CD508E2D0FAA3C0ABD1077E7A3C8`:
   Resolve **390.997 us**, bind 141.224 us, PhysicalAssignment
   176.252 us, frame p50 **2.447472 ms**. No reliable evidence
   the by-reference change helps the whole frame; retain for clear
   immutable ownership semantics, not a claimed speedup.
4. **Bulk certified structural template publication:** After a structural
   contract hit, `InstantiateStructuralPlan` copies the dense logical
   allocation table, per-packet dense use step maps, compiled command
   scopes, queue dependencies and cross-queue producer scope watermarks
   with RED typed array assignment. It still patches fresh per-frame
   physical generation, import-ordinal translation, dynamic decisions,
   incoming fence waits/presentation acquisition, and liveness; a
   cached symbolic program miss still materializes full compiled steps
   before legacy compilation. Run
   `rf_bulk_structural_template_320_20261011_105819`, SHA
   `69423F5AA27D6C6E89F020A8171FCABE34C3C888FBF9562B5BDF0689E412F2A7`:
   `ok=true`, `foregroundVerified=true`,
   `validRendererThroughEnd=true`, 4698 frame-window samples;
   Resolve **391.282 us** (7073 calls; 7064 cached),
   instantiation **46.171 us**, bind 141.093 us,
   PhysicalAssignment 181.935 us, Setup 54.215 us;
   frame p50 **2.467281 ms**, p95 3.349172 ms. The slight
   instantiation reduction is real but too small to establish
   frame-level superiority under host variability.

**Measured conclusion:** The substantial proven second-stage improvement
is the canonical physical-request compilation: no-batch 423.030 us to
the 387.752 us direct experiment while full-frame median fell from
2.471566 to 2.438703 ms. Subsequent clone/order changes roughly
hold 385–391 us Resolve and 2.45–2.47 ms frame p50 rather than
materially shifting the frontier. Do **not** present 385.052 us
from the best Resolve capture and 2.438703 ms from another
capture as a single-build pair.

The remaining about **85–91 us Resolve gap to <=300 us** is not
uncompiled GPU state calculation: in all runs the heavyweight full
compiler calls occur only on nine cold frames. Most cost comes from
generation-owned packet publication/binding (~140–150 us), physical
ownership acquisition and descriptor proof (~165–182 us), plus
structural metadata and setup (~50 us each). Fine-grained lease
instrumentation is intentionally removed because it perturbed
production timings. Further major reduction needs an exact certified
native physical-program variant or separately owned immutable packet
program with per-generation overlays. It must preserve current sorted
binding identity, lease/fence epoch and backings, descriptor/generation
lifetimes, exact queue/import/export decisions, packet views surviving
cache eviction and all RHI barriers; a pointer-sharing shortcut or
unsafe assumption about stable graph identity is not acceptable.

### Final same-binary 3-workload validation (SHA 69423F5A...)

All three captures used the same exact Profile binary
`69423F5AA27D6C6E89F020A8171FCABE34C3C888FBF9562B5BDF0689E412F2A7`,
same interactive benchmark host/driver, 1280x720, 18-second capture
and final 12-second frame window. All returned `ok=true`,
`validRendererThroughEnd=true`, `foregroundVerified=true`.
The workload changes only in graph resource probe count; the fake
CPU-burning loop stays removed in all workloads.

| Workload | Burn-free original p50 | Final current p50 | Final Resolve mean | Cached / all Resolve | Run ID |
| --- | ---: | ---: | ---: | ---: | --- |
| Normal | 1.174676 ms | **1.147983 ms** | 77.230 us | 15196 / 15205 | `rf_final_bulk_structural_normal_20261011_110411` |
| 64 resources | 1.657696 ms | **1.572185 ms** | 155.038 us | 12093 / 12101 | `rf_final_bulk_structural_64_20261011_110151` |
| 320 resources | 2.841659 ms | **2.467281 ms** | 391.282 us | 7064 / 7073 | `rf_bulk_structural_template_320_20261011_105819` |

Normal final 12-second window n=9929, p95 1.649465 ms;
64-resource n=7374, p95 2.178495 ms; 320-resource n=4698,
p95 3.349172 ms. Average Resolve zones have no per-lease Tracy
high-cardinality probes. The clean 320 full-frame median reduction
is **0.374378 ms (~13.2%)** relative to the original burn-free
baseline. Mean Resolve reduction from the approximate 780 us
original baseline is about **388.7 us (~49.8%)**; do not imply
an exact original Resolve fraction from the approximate input.

### Source/build/test closure at this checkpoint

Post-bulk-structural source successfully built Debug
`rendering.vcxproj`, `geometryAllocatorTests.vcxproj` and executed
the full `geometryAllocatorTests.exe` with exit zero. Latest
Development `rendering.vcxproj`, `geometryAllocatorTests.vcxproj`
and full `geometryAllocatorTests.exe` also passed; subsequent
Development `renderingTests.vcxproj` plus executable passed (Render
Graph, Resource Flow allocator, texture candidate, viewport).
Profile `rendering.vcxproj` and `runtime.vcxproj` passed and yielded
the exact SHA above. Narrow preexisting LNK4006 duplicate symbol
linker warnings are unrelated and nonfatal.

No commit, reset, clean or unrelated file mutation performed. The
shared checkout remains dirty. The <=300 us aspirational target
is **still open**, despite the correctness-validated architecture
and substantial ~50% Resolve reduction. A future cut must act on
the remaining *per-generation executable packet image* and/or
*per-frame physical pool admission*, not disable or defer native
barriers. Require focused proof of packet-view lifetimes across
eviction, exact physical owner/lease serial identity, current
native sorted table, RHI device/domain, current import/presentation
fences and failure rollback before broadening the cached variant.
