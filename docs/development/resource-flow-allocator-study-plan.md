# Vanguard Resource Flow Allocator Study Plan

Date: 2026-08-31

Status: all four RED study sets, both Unreal sets, and the authoritative V1
design synthesis are complete and preserved in
`resource-flow-allocator-design.md`. The separate file-level execution plan is
next. No production implementation begins until that plan is reviewed and
explicitly approved.

## 1. Objective And Boundary

This study will define Vanguard's frame-local render-resource allocator:

```text
render work declares logical texture/buffer requests
  -> PreConsume records a deterministic typed plan once
  -> culling removes dead work before lifetime finalization
  -> Resolve compiles per-use execution steps and physical assignments
  -> compatible physical resources are reused or placed over shared memory
  -> Consume executes the compiled steps without replaying declarations
  -> alias activation, barriers, and retirement happen at scheduled points
  -> Cleanup validates the frame and retains only safe cache state
```

The allocator owns logical frame-resource declarations, temporal identity,
lifetime analysis, physical assignment, alias activation, and the compiled
resource-action schedule. It does not own render-graph topology, node
scheduling, pass compilation, pipeline selection, persistent asset streaming,
or general GPU-memory residency.

The render graph and node context are inspected only where they call into the
allocator or establish a phase/fence/queue boundary. This study must not turn
into another render-graph-core study.

## 2. Starting Direction Already Chosen

The studies may refine details, but they begin with these decisions:

- Vanguard will use a RED-shaped `PreConsume -> Resolve -> Consume -> Cleanup`
  lifecycle unless source evidence makes it technically unsuitable.
- A normal render-node implementation should not rely on authors remembering
  scattered `if (IsConsumePhase())` guards. The eventual node-facing API should
  separate declaration/planning from real execution while preserving the fixed
  virtual node execution wrapper. The exact API is outside this allocator study;
  this plan records only the allocator contract it must support.
- A logical frame resource is not shader virtual texturing and does not imply a
  shader-side page-table lookup. In Vanguard documentation, an RHI object with
  deferred memory binding should be called a **placed resource** or
  **deferred-binding resource**, not merely a "virtual resource."
- Vanguard should support two physical strategies:
  - whole-resource reuse when compatible cached resource lifetimes do not
    overlap;
  - true placed-resource aliasing, where distinct resource objects occupy the
    same heap memory and require explicit alias activation/barriers.
- Texture and buffer support are both part of the allocator contract.
- Current Vanguard RHI behavior must be checked before proposing new RHI APIs.
  Existing NVRHI and Vanguard D3D12 extensions are evidence, not assumptions.

If a study set contradicts one of these directions, it must record the exact
source evidence and stop for an architecture decision.

## 3. Permanent Study Outputs

This file is the phase checklist. Findings will be preserved in:

- `docs/development/resource-flow-allocator-design.md`
  - cited RED and Unreal findings;
  - `Copy`, `Adapt`, `Reject`, and `Vanguard-specific` decisions;
  - request, timeline, allocation, synchronization, and ownership invariants;
  - terminology and an open-question log.
- `docs/development/resource-flow-allocator-execution-plan.md`
  - the current Vanguard surface that can be reused;
  - required migrations and file-level implementation order;
  - validation and rollout gates.

The design document begins with RED Study Set 1. The execution plan is created
only after the RED study has established the complete semantic contract. Neither
document authorizes implementation until the final synthesis is approved.

## 4. Evidence And Scope Rules

Every study set must follow these rules:

1. Cite the local file, symbol, and useful line range for architectural claims.
2. Mark every conclusion as `Copy`, `Adapt`, `Reject`, or
   `Vanguard-specific`.
3. Distinguish observed source behavior from a proposed Vanguard design.
4. Inspect only the files assigned to the current set. A directly referenced
   helper may be added only when its necessity is recorded.
5. Do not read entire feature-rendering directories to collect more examples.
   Representative call sites are selected only to answer a named question.
6. Do not redesign render-graph topology, jobs, command-list groups, or render
   passes during allocator study.
7. Do not modify production code during the study.
8. End each set by updating the design findings, listing unresolved questions,
   checking its exit gate, and stopping for review.

## 5. RED Source Root

The RED snapshot is available at:

```text
D:\root\R6.Root\Mainline\dev\src\common
```

It is a non-Git local snapshot. Set 1 will record a source-corpus hash manifest
for the files actually studied so later conclusions remain reproducible.

## 6. RED Study Sets

The RED study is deliberately limited to four sets. They follow data flow from
the public API down to physical memory and then back out to frame integration.

### RED Set 1 — Public Contract And Logical Resource Identity

Primary files:

```text
renderer/src/renderFlowResourceAllocator.h
renderer/src/renderFlowResourceAllocator.cpp
renderer/src/renderFlowNameTag.h
renderer/src/renderNodeImplContext.h
renderer/src/renderNodeImplContext.cpp
```

The context files are restricted to allocator-facing methods: name-tag creation,
allocation/declaration, temporary resources, imports, use begin/end, swaps,
decisions, descriptor helpers, and resolved-resource access. Camera, binding,
PSO, and general render-node behavior are out of scope.

Questions this set must answer:

- What are the allocator phases and which calls are legal in each phase?
- What is the complete request vocabulary for textures and buffers?
- How are name, hash, flow space, flow group, and temporary identity separated?
- What do allocation, allocation-like, import, use, swap, decision, queue, sync,
  and retrieval operations mean at the public boundary?
- Which descriptors and compatibility properties are public contracts versus
  RED implementation details?
- Which errors are asserted, returned, or silently assumed by RED?
- What must Vanguard expose to a future typed node resource-declaration layer?

Preserved output:

- public semantic vocabulary and phase legality table;
- logical tag/identity model;
- texture and buffer descriptor requirements;
- first `Copy / Adapt / Reject / Vanguard-specific` ledger;
- exact questions passed to Set 2.

Exit gate:

```text
We can describe every public allocator operation without referring to RED's
internal storage or physical allocation algorithm.
```

### RED Set 2 — Request Tape, Replay, And Temporal Lifetimes

Primary files:

```text
renderer/src/renderFlowInternalData.h
renderer/src/renderFlowInternalData.cpp
```

Restricted symbols/areas:

```text
Limits
SRequest and request payloads
SParallelGroup
STagLifetime and logical event/timeline records
HandlePreConsumeRequest
HandleConsumeRequest
request ordering and validation
decision replay
swap/import processing
GetResourceAtGivenTime and request-time lookup
```

Physical pool assignment and GPU object creation are intentionally deferred to
Set 3 even though they live in the same files.

Questions this set must answer:

- How can nodes record requests in parallel while producing deterministic
  logical order?
- What is the exact key for request time?
- How does Consume prove that it replayed PreConsume correctly?
- How do decisions keep request-affecting branches stable?
- How do use ranges, temporary identities, imports, and logical swaps change a
  tag's timeline?
- When does one logical name resolve to different physical objects at different
  request times?
- How are malformed streams, missing ends, duplicate declarations, and invalid
  swaps detected?
- Which fixed capacities, packed representations, and hash assumptions should
  Vanguard reject?

Preserved output:

- deterministic request-tape algorithm;
- temporal logical-resource state machine;
- replay validation rules and diagnostic requirements;
- pseudocode for timeline construction independent of RED containers;
- tests required before physical allocation is introduced.

Exit gate:

```text
Given a synthetic request tape, we can compute and explain each logical
resource's value and lifetime at every request time without creating a GPU
resource.
```

### RED Set 3 — Resolve, Physical Pools, And Aliasing

Allocator files and restricted areas:

```text
renderer/src/renderFlowInternalData.h
  SAllocation
  SAllocationPool
  SAllocationRequest
  physical/cached allocation records

renderer/src/renderFlowInternalData.cpp
  Resolve
  AssignAllocationsFromPool
  physical resource construction
  cache release/aging
  alias activation and safe retirement calls
```

GPU API contract and D3D12 implementation:

```text
gpuApi/include/gpuApiInterface.h
  TextureAliasingDesc
  GetAliasingDesc
  CanTexturesAlias
  CreateAliasingTexture / CreateAliasingBuffer
  BarrierTextureAliasing / BarrierBufferAliasing
  MakeStateSafeToRetire

gpuApi/src/gpuApiCommonBarrier.cpp
gpuApi/src/dx12/gpuApiDX12Alloc.h
gpuApi/src/dx12/gpuApiDX12Alloc.cpp
gpuApi/src/dx12/gpuApiDX12Barrier.cpp
gpuApi/src/dx12/gpuApiDX12Texture.cpp
gpuApi/src/dx12/gpuApiDX12Buffer.cpp
```

Questions this set must answer:

- How are logical lifetimes converted into physical allocation requests?
- What determines size, alignment, heap class, and compatibility?
- Does RED allocate one shared block and multiple resource objects, reuse an
  existing object, or use both strategies?
- How are intervals packed, and what causes an allocation to spill into another
  pool/block?
- When is an alias barrier emitted, what is its before/after resource, and when
  may the new contents be discarded?
- Why must the previous resource be made safe to retire?
- How do buffers differ from textures?
- Which objects survive across frames, how are they matched, and how are stale
  cache entries aged out?
- What synchronization assumptions make reuse and aliasing safe?

Preserved output:

- logical-to-physical resolve algorithm;
- explicit distinction between whole-resource reuse and placed aliasing;
- compatibility and barrier contract;
- pool/cache ownership and retirement model;
- RED-to-current-Vanguard RHI capability/gap table.

Exit gate:

```text
For every physical assignment, we can state which memory and resource object it
uses, why its lifetime may overlap that memory, and which barrier/retirement
operation makes the transition legal.
```

### RED Set 4 — Frame Ownership And Integration Boundary

Primary files, restricted to allocator integration:

```text
renderer/src/renderRenderFrame.cpp
  allocator construction/ownership
  PreConsume, Resolve, Consume, and Cleanup transitions
  fences/joins surrounding those transitions

renderer/src/renderNodeGraph.cpp
  sequential and parallel allocator request walks only

renderer/src/renderNodeGraphFactory.cpp
  allocator queue-scope and synchronization markers only

renderer/src/renderGraphNodes.cpp
  CRenderNodeBase::Process boundary
  synchronization nodes
  allocator queue/resource lifecycle requests

renderer/src/renderInterface.h
renderer/src/renderInterface.cpp
  only if needed to establish allocator ownership or teardown
```

At most five representative feature call sites may be inspected. Each one must
answer a specific unresolved question about declaration, use-range extension,
temporary allocation, swap, import, or queue synchronization. They are examples,
not an invitation to study the feature renderer.

Questions this set must answer:

- Who owns the allocator and how long does its cross-frame cache live?
- Which job fence makes parallel PreConsume complete before Resolve?
- Which execution path performs Consume replay?
- How do render/GPU flow groups select request time without making CPU job order
  allocator order?
- How are queue begin/end and fork/join/sync events represented to lifetime
  analysis?
- When are resolved resources first legal to retrieve?
- When is cleanup performed, and what proves it happens once after terminal GPU
  work has been recorded safely?
- What minimal contract must the future render graph and node execution context
  satisfy without making the allocator own either system?

Preserved output:

- frame ownership and phase sequence;
- job/queue synchronization boundary;
- minimal node-context integration contract;
- lifecycle failure and teardown behavior;
- complete RED findings synthesis and unresolved questions for Unreal.

Exit gate:

```text
The allocator can be placed in Vanguard's frame lifecycle with explicit owner,
phase transitions, joins, queue semantics, and cleanup—without redesigning the
render graph.
```

## 7. Later Unreal Comparison

Unreal is studied only after all four RED sets. Its purpose is to challenge and
modernize specific RED decisions, not to replace Vanguard's RED-shaped execution
architecture with RDG wholesale.

### Unreal Set U1 — RDG Logical Lifetimes And Pooled Fallback

```text
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Public\RenderGraphResources.h
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Public\RenderGraphResources.inl
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Public\RenderGraphBuilder.h
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Public\RenderGraphBuilder.inl
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Private\RenderGraphBuilder.cpp
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Private\RenderGraphResources.cpp
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Private\RenderGraphResourcePool.h
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Private\RenderGraphResourcePool.cpp
```

This set compares declared access, first/last use, external/extracted resources,
resource culling effects, validation, and non-aliased pooled reuse. General RDG
pass compilation, shader parameters, and renderer features remain out of scope.

Completed on 2026-08-30. Direct supporting files were added only where the
primary corpus delegated the exact U1 behavior:

```text
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Public\RenderTargetPool.h
D:\UnrealEngine\Engine\Source\Runtime\RenderCore\Private\RenderTargetPool.cpp
D:\UnrealEngine\Engine\Source\Runtime\RHI\Public\RHITransientResourceAllocator.h
  only the lifetime-fence contract at lines 14-67
```

Preserved results:

- Unreal declares resource access once, compiles/culls it, and executes each
  surviving pass once;
- culling precedes final lifetime and physical-allocation derivation;
- imports and exports have distinct ownership and publication contracts;
- pooled fallback reuses the same complete RHI object and is not heap aliasing;
- Vanguard keeps RED's lifecycle and virtual wrapper but replaces full Consume
  replay with ordered, per-resource-use compiled execution packets;
- pool compatibility, budget, validation, and failure-policy corrections are
  recorded in Sections 56 through 67 of the design document.

Exit gate: **passed**. U1 did not inspect transient heap placement or backend
alias barriers; those remain exclusively U2.

### Unreal Set U2 — Transient Heap Allocation And Backend Aliasing

```text
D:\UnrealEngine\Engine\Source\Runtime\RHI\Public\RHITransientResourceAllocator.h
D:\UnrealEngine\Engine\Source\Runtime\RHI\Private\RHITransientResourceAllocator.cpp
D:\UnrealEngine\Engine\Source\Runtime\RHI\Public\RHIValidationTransientResourceAllocator.h
D:\UnrealEngine\Engine\Source\Runtime\RHI\Public\RHITransition.h
D:\UnrealEngine\Engine\Source\Runtime\RHI\Private\RHITransition.cpp
D:\UnrealEngine\Engine\Source\Runtime\RHICore\Public\RHICoreTransientResourceAllocator.h
D:\UnrealEngine\Engine\Source\Runtime\RHICore\Private\RHICoreTransientResourceAllocator.cpp
D:\UnrealEngine\Engine\Source\Runtime\D3D12RHI\Private\D3D12TransientResourceAllocator.h
D:\UnrealEngine\Engine\Source\Runtime\D3D12RHI\Private\D3D12TransientResourceAllocator.cpp
```

This set compares heap-range allocation, alias-overlap tracking, acquire/discard
operations, queue-aware fences, validation, backend capability fallback, and
cache garbage collection. Vulkan may be spot-checked only if a concrete
cross-backend question remains after the D3D12 study.

Completed on 2026-08-30. The D3D12 evidence answered the backend questions, so
no Vulkan expansion was needed. Direct support was restricted to:

```text
D:\UnrealEngine\Engine\Source\Runtime\RHI\Private\RHIValidation.cpp
  only transient-allocation validation at lines 3578-3702
D:\UnrealEngine\Engine\Source\Runtime\D3D12RHI\Private\D3D12LegacyBarriers.cpp
  only alias/discard transition lowering
D:\UnrealEngine\Engine\Source\Runtime\D3D12RHI\Private\D3D12EnhancedBarriers.cpp
  only alias/discard VA-overlap ordering
external/nvrhi/upstream/src/validation/validation-device.cpp
external/nvrhi/upstream/src/d3d12/d3d12-device.cpp
external/nvrhi/upstream/src/d3d12/d3d12-texture.cpp
external/nvrhi/upstream/src/d3d12/d3d12-buffer.cpp
  only the existing placement contract cross-check
```

Preserved results:

- Unreal separates logical schedule fences from hardware completion fences;
- D3D12 uses true placed resources over contiguous native heap ranges, with no
  shader lookup;
- exact prior-owner provenance is compiled before execution and backend alias
  activation is an RHI lowering concern;
- reserved-page remapping is a different optional provider and is deferred;
- Vanguard already has heaps, deferred binding, memory requirements, alias and
  discard operations, resource retention, and fence-safe destruction;
- the missing work is allocator policy plus truthful compatibility, placement
  metadata, release validation, budgets, failure classification, and atomic
  plan publication;
- whole-resource reuse remains the required capability fallback;
- Unreal/NVRHI defects and Vanguard hardening requirements are recorded in
  Sections 68 through 80 of the design document.

Exit gate: **passed**. Every adopted Unreal idea is tied to a concrete Vanguard
requirement; production code remains untouched.

Unreal comparison exit gate:

```text
Every Unreal idea is tied to a concrete RED weakness or Vanguard requirement;
none is adopted merely because Unreal is newer.
```

## 8. Vanguard Cross-Check Corpus

Each RED set may consult the smallest relevant part of Vanguard's current code
to avoid proposing duplicate machinery:

```text
source/rhi/include/vanguard/rhi/rhi_types.hpp
source/rhi/include/vanguard/rhi/rhi.hpp
source/rhi/nvrhi/src/common_backend.cpp
source/rhi/nvrhi/src/d3d12_backend.cpp
source/rendering/src/frame_renderer.cpp
```

This is a compatibility cross-check, not a license to modify these files or
expand the study into all of RHI/rendering.

## 9. Explicit Exclusions

The following are outside this study unless a cited allocator dependency makes
one unavoidable:

- render-graph topology, graph merge, node grouping, and graph cache design;
- renderer feature passes and effect implementation;
- persistent mesh/texture streaming and asset residency;
- bindless descriptor architecture;
- sparse/virtual texturing and shader page tables;
- general D3D12 memory-budget residency (`MakeResident`/`Evict`);
- multi-GPU policy;
- ESRAM/platform-specific RED behavior;
- copying RED fixed capacities, 8-bit flow ids, packed unions, or hash-only
  identity assumptions;
- implementing production code before the file-level execution plan is reviewed
  and explicitly approved.

## 10. Study Order And Current State

```text
RED Set 1  Public contract and logical identity       COMPLETE
RED Set 2  Request tape and temporal lifetimes        COMPLETE
RED Set 3  Resolve, physical pools, and aliasing      COMPLETE
RED Set 4  Frame ownership and integration boundary  COMPLETE
Unreal U1  Logical lifetime and pooled comparison     COMPLETE
Unreal U2  Transient heaps and backend aliasing       COMPLETE
Final      Authoritative V1 design synthesis           COMPLETE
Plan       File-level execution plan                   COMPLETE, READY FOR REVIEW
Next       Stage 1 implementation                      REQUIRES EXPLICIT APPROVAL
Implement Explicit user approval required             BLOCKED
```

The study, final design synthesis, and file-level execution plan are complete.
Production implementation remains blocked until the execution plan is reviewed
and explicitly approved.

The execution plan is preserved in
`docs/development/resource-flow-allocator-execution-plan.md`.
