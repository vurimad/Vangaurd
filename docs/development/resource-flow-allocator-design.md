# Vanguard Resource Flow Allocator Design

Date: 2026-08-31

Status: RED Study Sets 1 through 4 and Unreal Sets U1 and U2 are complete. The
authoritative Vanguard V1 synthesis is complete in Sections 81 through 90.
Unreal U1 replaces RED's full Consume request replay with a compile-once,
per-use execution-packet model while preserving the same lifecycle, temporal
semantics, process wrapper, and publication gates. U2 adds optional placed-heap
aliasing above Vanguard's existing RHI primitives, with whole-resource reuse as
the mandatory fallback.
Production implementation remains blocked until this synthesis and the separate
execution plan are reviewed.

## 1. Scope Of This Document

The Resource Flow Allocator is the frame-local system that turns logical texture
and buffer declarations into resources usable during render execution.

The intended outer lifecycle is:

```text
PreConsume -> Resolve -> Consume -> Cleanup
```

Study Set 1 established:

- what a logical frame resource is;
- which public operations exist;
- how names, flow spaces, flow groups, and temporary identities differ;
- which descriptor concepts must cross the public boundary;
- how node-facing resource calls reach the allocator;
- which existing Vanguard RHI types should be reused.

Study Set 2 adds:

- deterministic parallel request recording and total logical order;
- RED's exact Consume replay and recorded decisions, preserved as source
  evidence but superseded by U1's compiled execution-packet contract;
- generated temporary identity;
- logical allocation versions and time-varying tag mappings;
- use-interval, swap, import, and resolved-lookup invariants;
- structured validation requirements that RED currently lacks.

Study Set 3 adds:

- exact whole-resource reuse versus true placed-resource aliasing semantics;
- active-shape resource objects and maximum-shape backing requirements;
- deterministic interval packing and explicit physical action scheduling;
- placed texture/buffer activation and safe-retirement requirements;
- cross-frame pool, external ownership, and fence-retired cache rules;
- a current Vanguard RHI reuse/gap audit.

Study Set 4 adds:

- renderer/device-lifetime allocator ownership;
- the exact PreConsume join, Resolve join, Consume gate, and terminal cleanup;
- GPU-flow-group request time independent of CPU worker order;
- request-position alias activation and safe retirement;
- command-list queue scopes and fork/join submission ordering;
- the minimal boundary between the allocator, render graph, jobs, and RHI.

Unreal Set U1 adds:

- one-time resource declaration separated from one-time virtual execution;
- culling before first/last-use and physical-allocation compilation;
- per-use compiled execution packets for temporal mappings and boundary actions;
- explicit import, extraction, and final-access ownership contracts;
- whole-resource pooled fallback using the same RHI object, not placed aliasing;
- cache-policy and validation corrections that Vanguard should apply.

Unreal Set U2 adds:

- logical queue-lifetime fences separated from hardware completion fences;
- byte-range allocation over native heaps and precise prior-owner provenance;
- D3D12 placed-resource creation and backend alias/discard lowering;
- persistent heap and placed-object cache policy;
- an audit proving that Vanguard already owns the required low-level RHI
  primitives and needs policy/validation hardening rather than another API;
- a conservative capability fallback and transactional publication boundary.

Reader authority rule:

```text
Sections 2 through 80  source-study evidence and historical checkpoints
Sections 81 through 90 authoritative Vanguard V1 design
```

If an earlier Copy/Adapt decision, replay API, phase name, or physical-plan
sketch conflicts with Sections 81 through 90, the later synthesis wins. The
remaining design work is the separate file-level execution plan, not another
source-discovery pass.

## 2. RED Set 1 Source Snapshot

RED source root:

```text
D:\root\R6.Root\Mainline\dev\src\common
```

The snapshot has no Git metadata. The exact Set 1 corpus is pinned here by file
length and SHA-256:

```text
renderer/src/renderFlowResourceAllocator.h
  lines: 362
  sha256: DA68B9BD5667450B0F3952D8BC2CCC978A6C82DA1F1A905FBE385C324B61AEE6

renderer/src/renderFlowResourceAllocator.cpp
  lines: 258
  sha256: C62F930848F30B047C21BBC4AE4B767C4225834D4FEE06F843DD03CC0EFF782B

renderer/src/renderFlowNameTag.h
  lines: 126
  sha256: 73DA9B87F9BD3EE9814FCFB5C6E72B8AF9054AAA72EC53B483ACFEB30514B6B3

renderer/src/renderNodeImplContext.h
  lines: 1135
  sha256: 40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A

renderer/src/renderNodeImplContext.cpp
  lines: 413
  sha256: B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7

renderer/src/renderCommon.h
  lines: 29
  sha256: 1B572A3534CABC57424A40867ADEB4149AC3CA02B8673D20391054440D75F1BF
```

`renderCommon.h` was added as one direct dependency because it defines the queue,
flow-space, and flow-group types used by the public allocator API. Only lines
13-23 were inspected from it.

The context files were restricted to resource methods, descriptor helpers, and
the node setup needed to establish flow identity. Binding, PSO, command-list,
camera-rendering, and graph behavior were not studied.

## 3. RED Public Phase Contract

Evidence:

- `renderFlowResourceAllocator.h:20-26`
- `renderFlowResourceAllocator.h:291-308`
- `renderFlowResourceAllocator.h:338-341`
- `renderFlowResourceAllocator.cpp:42-55`

RED exposes four phases:

| Phase | Public meaning |
|---|---|
| `Startup` | Begin a frame and verify that the previous frame reached cleanup. |
| `PreConsume` | Record requests. Resolved resources are unavailable. |
| `Consume` | Replay the same requests. Resolved resources are available. |
| `Cleanup` | Finish frame-local validation/cleanup and retain allowed cache state. |

There is no public `Resolve` phase value. Resolution is internal to the
transition into Consume. The exact transition algorithm belongs to Set 2 and
Set 3.

`SetPhase` is explicitly not thread-safe and must be called at synchronization
points. The public header also establishes the defining replay invariant:

```text
PreConsume requests are issued in GPU execution order.
Consume must issue exactly the same request stream.
```

The comments use some older phase names, but the current enum and API express
the PreConsume/Consume pair.

### Phase legality established by Set 1

| Operation | Startup | PreConsume | Consume | Cleanup |
|---|---:|---:|---:|---:|
| change phase | synchronization boundary | synchronization boundary | synchronization boundary | synchronization boundary |
| declaration/use/swap/decision requests | no public purpose | record | exact replay | no public purpose |
| retrieve resolved texture/buffer | invalid | invalid | valid | invalid |
| clear persistent caches | unspecified | unspecified | unspecified | unspecified |

The façade delegates requests without enforcing this table itself. Exact
transition and replay validation are intentionally deferred to Set 2.

### Vanguard decision

**Copy:** preserve the four-phase lifecycle and exact replay invariant.

**Adapt:** expose loud typed failures for illegal phases and mismatched replay;
do not rely on comments and assertions.

**Adapt:** node authors should eventually receive a declaration-facing context
and a resolved execution-facing context instead of manually branching on
`IsConsumePhase()`. That node API will be designed with the render graph later.
The allocator must support it but does not own it.

## 4. RED Resource And Use Vocabulary

### Resource kinds

RED supports textures and buffers through `FlowResourceType`.

Evidence: `renderFlowResourceAllocator.h:28-32`.

### Use intent

RED exposes:

```text
Read
Write
Read | Write
NoDiscard
```

Evidence: `renderFlowResourceAllocator.h:11-18`.

`NoDiscard` is a first-use hint. Despite its name, RED's comment says it marks a
discard as unnecessary, for example when the resource is about to be cleared.
Its exact interaction with alias activation belongs to Set 3.

This vocabulary expresses lifetime and coarse content intent. It is not a
complete backend state, barrier, stage, or subresource-access model.

### Vanguard decision

**Copy:** texture and buffer resources are both V1 requirements.

**Adapt:** keep backend-independent logical access intent separate from
`rhi::ResourceState`. A render-target write, sampled read, copy destination, and
storage read/write may all affect lifetime while requiring different backend
states.

**Defer:** exact access enum, stage visibility, and subresource granularity remain
open until the request/lifetime and Unreal comparisons.

## 5. RED Logical Identity

Evidence:

- `renderFlowNameTag.h:7-54`
- `renderFlowNameTag.h:56-121`
- `renderFlowResourceAllocator.cpp:25-26,57-60`
- `renderCommon.h:13-23`
- `renderNodeImplContext.h:513-521`
- `renderNodeImplContext.cpp:313-333`

### Name

`RenderFlowNameAndHash` computes a 32-bit hash from a string. The original text
is retained only in debug-name builds; otherwise `GetName()` returns null.
Static names can be hashed at compile time and dynamic names at runtime.

### Flow space

Flow space is the logical namespace for a name:

```text
normal camera/view node -> current camera flow space
unique/global node      -> flow space 0
explicit shared tag     -> flow space 0
```

Therefore unique-node resources and explicitly shared resources intentionally
share one global namespace.

### Flow group

Flow group is not part of the logical name construction. Every request and
resolved lookup carries the current **GPU** render-flow group separately:

```cpp
m_renderFlowGroup = nodeContext.m_renderFlowGroups[RNDT_Gpu];
```

Flow space answers "which logical namespace?" Flow group answers "at which GPU
request position?"

### Temporary identity

`RTTempAlloc` passes the reserved `NAMELESS_ALLOC_REQUEST`, not a tag derived
from its display name. `RequestAlloc` recognizes that sentinel and asks internal
state to generate the actual tag, which it returns to the caller.

The supplied temporary name is therefore a diagnostic label, not identity.
How the generated id stays deterministic across parallel PreConsume and Consume
is a Set 2 question.

### RED encoding

RED intends eight bits of flow space and 24 bits of autogenerated id, then
produces a single 32-bit value:

```cpp
nameHash ^ ((space << 24) | autoGenId)
```

Equality and ordering use only that combined hash. Debug mode retains the
components and asserts that equality did not hide a collision. The bounds
assertions for flow space and autogenerated id are commented out.

Reset/non-camera contexts use `0xFF` as a sentinel flow space, but the tag
constructor does not reject it.

### Vanguard decision

**Copy:** preserve the semantic separation between name, flow space, generated
temporary identity, and request-time flow group.

**Reject:** do not use RED's XOR-combined 32-bit hash as authoritative identity.
Do not make correctness depend on collision checks compiled only in debug.

**Reject:** do not use unvalidated integer sentinels such as `0xFF` for context
setup state.

**Vanguard-specific:** use typed structured logical ids. Exact widths and the
temporary-id algorithm remain deferred to Set 2, but equality must compare the
real identity components. A debug name/hash may accelerate lookup and improve
diagnostics without becoming the sole identity proof.

**Vanguard-specific:** an `rhi::ResourceRef` is physical GPU-object identity,
not logical frame-resource identity.

## 6. RED Public Request Vocabulary

Evidence:

- declarations: `renderFlowResourceAllocator.h:310-322`
- lifetime/mapping: `renderFlowResourceAllocator.h:324-329`
- queue/replay: `renderFlowResourceAllocator.h:331-335`
- retrieval/cache: `renderFlowResourceAllocator.h:338-350`
- request construction: `renderFlowResourceAllocator.cpp:57-254`

### Declaration operations

| RED operation | Set 1 semantic meaning |
|---|---|
| `RequestAlloc(tag, descriptor)` | Declare a logical resource with an explicit descriptor. Duplicate declarations do not intend additional physical memory when no intervening free exists. |
| `RequestAlloc(tag, sourceTag)` | Declare a logical resource using the descriptor of a still-live source resource. |
| nameless `RequestAlloc` | Declare a temporary resource and return its generated logical tag. |
| `RequestInjection(texture)` | Introduce an external texture under a logical tag. |
| `RequestIsAlloc` | Replay-stabilized node-to-node query; RED explicitly says it is not allocation failure detection. |

Allocation-by-tag requires the source allocation to be alive and available. The
public façade asserts only that the source is not the nameless sentinel; deeper
validation is internal.

### Lifetime and mapping operations

| RED operation | Set 1 semantic meaning |
|---|---|
| `RequestUseBegin` | Begin one logical use range with coarse usage flags. |
| `RequestUseEnd` | End that logical use range. |
| `RequestFree` | Explicitly end the logical allocation's availability. Detailed semantics remain for Set 2. |
| `RequestSwap` | Exchange the temporal mappings of two logical tags. |
| `RequestSwapRef` | Exchange a logical tag's current physical resource with an external resource reference. |

`RequestSwapRef(TextureRef&)` is a true exchange during Consume: RED retrieves
the resource currently mapped to the tag, replays the swap request, then writes
the previous logical resource back into the caller's external reference.

### Queue and replay operations

| RED operation | Set 1 semantic meaning |
|---|---|
| `RequestBeginQueue` | Begin a graphics or compute queue scope. |
| `RequestEndQueue` | End the current queue scope. |
| `RequestQueueSync` | Record a command-list synchronization marker. |
| `RequestDecision` | Record a branch result in PreConsume and reproduce it in Consume. |

RED queue kinds are only `Graphic` and `Compute`; `None` and `Max` are sentinels
(`renderCommon.h:13-20`). Queue semantics belong to Set 2 and Set 4.

### Retrieval and policy operations

`GetTexture` and `GetBuffer` retrieve the physical resource mapped to a logical
tag at the current flow group's consumed request position. RED explicitly warns
that returned references must not be cached because mapping can change after
swap opcodes.

`SetProcessEviction` and `ClearAllCaches` are policy/maintenance operations, not
logical requests.

### RED defects found at the public boundary

1. `RequestSwapRef(BufferRef&)` is declared at
   `renderFlowResourceAllocator.h:329`, but a repository-wide search found no
   implementation. Calling it would fail to link.
2. External injection is texture-only despite buffers being a public resource
   kind.
3. `RequestQueueSync` and `RequestDecision` pass the flow group to
   `HandleRequest` but do not initialize the request object's `m_flowGroup`
   field (`renderFlowResourceAllocator.cpp:220-233`). Set 2 must determine
   whether this is harmless redundancy or a replay/debug defect.
4. Request wrappers perform little validation and return no structured errors.
5. `RequestIsAlloc` is admitted by RED's own context comment to conflict with
   multithreaded command-list construction
   (`renderNodeImplContext.h:248-250`).

### Vanguard decision

**Copy:** explicit declaration, declare-like, temporary declaration, use
begin/end, temporal swap, recorded decisions, queue markers, and request-time
retrieval are all required semantic operations.

**Adapt:** imports must support both textures and buffers and must state
ownership and descriptor authority explicitly.

**Adapt:** replace ambiguous external exchange with named import/export/exchange
operations after Set 2 establishes the temporal behavior actually required.

**Provisional reject:** do not use `IsAllocated` as hidden communication between
nodes. Prefer graph-build decisions or explicit recorded decisions. Set 2 will
verify whether any allocator-specific need remains.

**Adapt:** public operations must return structured errors or propagate a frame
failure. Invalid phases, tags, descriptors, use balancing, and replay mismatch
must not be assertion-only behavior.

## 7. RED Descriptor Contract

Evidence:

- defaults and copy: `renderFlowResourceAllocator.h:37-69`
- swap compatibility: `renderFlowResourceAllocator.h:79-124`
- equality ignoring maximum size: `renderFlowResourceAllocator.h:126-163`
- equality: `renderFlowResourceAllocator.h:165-206`
- storage: `renderFlowResourceAllocator.h:237-280`
- common descriptor helpers: `renderNodeImplContext.cpp:28-173`

`SRenderFlowTargetDesc` is one packed texture-or-buffer union.

### Texture concepts

- format and texture kind;
- current width, height, depth, slices, and mip count;
- maximum width, height, and mip count for dynamic resolution/capacity reuse;
- sampling, unordered-access, and render-target capabilities;
- extra backend usage flags;
- optimized clear value and clear-to-zero policy.

### Buffer concepts

- current and maximum element count;
- element stride;
- typed-buffer format;
- category and usage.

### Current versus maximum shape

RED descriptor helpers preserve current render size independently from maximum
camera render size. Final-output and screenshot helpers deliberately choose
different maximum-size policies. Composition descriptors also derive mip count
from their current surface dimensions.

This proves that active logical shape and reusable physical capacity are
different concepts at the public boundary. Set 3 will determine exactly how
maximum capacity participates in physical matching.

### RED comparison problems

RED defines three comparison modes:

1. swap compatibility;
2. equality ignoring maximum size;
3. nominal exact equality.

They are not complete field-wise comparisons:

- `clearToZero` is absent from all three;
- buffer `format` is absent from all three;
- swap comparison deliberately ignores optimized clear values and selected
  extended usage flags;
- exact texture comparison uses bitwise float comparison for clear values.

The packed representation also imposes legacy constraints:

- texture dimensions are 16-bit;
- mip counts are four-bit, limiting them to 15;
- copy construction uses `memcpy`;
- debug names are borrowed raw pointers;
- after selecting buffer kind, callers must initialize the buffer union fields
  because construction initialized the texture interpretation.

### Vanguard decision

**Reuse:** use Vanguard's existing `rhi::Format`, `rhi::TextureDimension`,
`rhi::Extent3D`, `rhi::TextureUsage`, and `rhi::BufferUsage`. Do not create
allocator-specific duplicates of those enums.

**Adapt:** logical descriptors should be a typed texture-or-buffer variant, not
a packed union. They must preserve active shape separately from maximum reusable
capacity where needed.

**Adapt:** optimized clear/content-initialization policy must be explicit and
typed if later physical study proves it affects resource construction.

**Reject:** do not copy RED's field widths, bitfields, raw `memcpy`, borrowed
debug pointer, or implicit union initialization.

**Reject:** do not overload one descriptor `operator==` for multiple policies.
Use named validation policies:

```text
declaration equality
logical swap compatibility
physical reuse compatibility
placed-memory compatibility
```

Only declaration equality belongs fully to Set 1. Physical comparisons belong
to Set 3.

## 8. Node-Facing Context Findings

Evidence:

- resource surface: `renderNodeImplContext.h:204-275`
- scoped use wrapper: `renderNodeImplContext.h:114-185,523-574`
- resolved getters: `renderNodeImplContext.h:587-645`
- request forwarding: `renderNodeImplContext.cpp:223-294`
- per-node flow identity: `renderNodeImplContext.cpp:313-344`

Every node-facing request routes through the current GPU flow group. Normal
name-taking operations first construct a tag in the node's current flow space;
shared operations force flow space zero.

RED's `ScopeNameTag` combines three roles:

```text
logical tag
RAII use-begin/use-end guard
consume-time typed physical getter
```

Creation begins use, destruction ends use, the object is non-copyable/movable,
and a null form records nothing. Templated allocation helpers allocate and begin
use immediately.

One sharp edge is `ScopeNameTag::Clear()`: it merely removes the context pointer,
which suppresses the destructor's `UseEnd` without balancing the open range.

### Vanguard decision

**Copy:** keep one normal node-facing front door so node code does not bypass
flow identity or request position.

**Adapt:** do not combine logical identity, declaration, active use, and resolved
physical access into one opaque value. Those are distinct states and should be
represented distinctly in the eventual typed context API.

**Adapt:** if an RAII convenience guard is provided, it must not expose a silent
escape that leaves an unbalanced use range.

**Defer:** the exact virtual `Execute` integration and typed resource-schema API
remain render-graph work. This allocator study only requires that planning and
resolved access can be separated without changing logical identity.

## 9. Vanguard Existing Surface Cross-Check

The following current Vanguard facilities already exist and must be reused.

### Typed physical identity and retention

- typed `TextureRef`, `BufferRef`, and `HeapRef`:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:41-71`;
- generation-safe type-erased `ResourceRef`:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:87-175`;
- owning intrusive `Texture`, `Buffer`, and `Heap` wrappers with fence-safe
  backend retirement:
  `source/rhi/include/vanguard/rhi/rhi.hpp:241-368`.

`ResourceRef` is useful for storing a resolved physical resource generically,
but it must not become a logical allocator tag.

### Resource descriptors and validation

- use capabilities and backend state:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:477-532`;
- extent and subresource vocabulary:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:551-563`;
- `TextureDesc` and `BufferDesc`:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:676-698`;
- stable failure vocabulary:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:1609-1635`;
- descriptor validation:
  `source/rhi/src/rhi.cpp:443-526`.

The existing descriptors already cover:

```text
texture extent/dimension/format/mips/array/samples/usage/initial state
buffer byte size/stride/format/usage/initial state/memory type
deferred native memory binding through virtualResource
```

The allocator must not invent parallel format, dimension, usage, or memory-type
enums. It will need logical metadata above these physical descriptor fields:

```text
logical identity and flow space
resource kind
active versus maximum reusable capacity
declaration/import ownership
logical access intent
request/debug origin
temporal mapping
```

`virtualResource` means deferred native memory binding. It is a physical RHI
creation detail and must be called placed/deferred-binding behavior in allocator
documentation. It is not virtual texturing and should not be selected ad hoc by
ordinary node declarations.

### Import metadata gap

The public RHI resource API at
`source/rhi/include/vanguard/rhi/rhi.hpp:23-64` can create resources, heaps, bind
memory, and query memory requirements. It does not expose immutable
`GetTextureDesc` or `GetBufferDesc` functions.

The NVRHI backend retains original descriptors privately
(`source/rhi/nvrhi/src/common_backend.cpp:1311-1341,1432-1457`) but exposes only
memory requirements (`common_backend.cpp:2428-2442`). A raw external reference
therefore cannot prove its descriptor to the allocator.

The current presentation acquisition also carries a texture reference without
its complete descriptor (`source/rendering/include/vanguard/rendering/viewport.hpp:305-322`);
format is retained separately in presentation state
(`source/rendering/include/vanguard/rendering/presentation_service.hpp:88-99`).

Open design question:

```text
Should an import require an authoritative descriptor token from its owner, or
should RHI expose immutable descriptor queries?
```

No RHI change is justified until later studies determine which ownership model
is cleaner. Either route must retain an imported resource safely; a non-owning
raw `TextureRef` or `BufferRef` alone is insufficient.

### No competing allocator exists

`FrameRenderer::RenderFrame` currently stops at the future render-graph executor
boundary (`source/rendering/src/frame_renderer.cpp:26-49`). No current frame
resource declaration or allocator API needs to be migrated.

## 10. Set 1 Decision Ledger

### Copy from RED

- `PreConsume -> Resolve -> Consume -> Cleanup` semantics;
- exact request replay as a correctness invariant;
- logical names separated by flow space;
- explicit shared/global namespace;
- generated identity for temporary declarations;
- flow group as request-time context, separate from identity;
- texture and buffer logical resources;
- declare, declare-like, use begin/end, swap, decision, and queue markers;
- request-time-aware physical retrieval;
- active shape versus maximum reusable capacity.

### Adapt from RED

- structured typed ids instead of hash-only identity;
- dynamic validated ranges instead of eight-bit sentinels;
- typed texture/buffer variants instead of packed unions;
- existing Vanguard RHI format/shape/usage vocabulary;
- explicit import ownership and descriptor authority;
- named descriptor comparison policies;
- structured failures with tag, phase, request origin, and nested RHI error;
- separate planning and resolved execution context surfaces;
- safe balanced ergonomic guards only.

### Reject from RED

- 32-bit XOR identity as the authoritative key;
- debug-only collision correctness;
- packed descriptor union and `memcpy` copy;
- 16-bit dimensions and four-bit mip counts;
- raw borrowed debug-name pointers;
- assertion-only public validation;
- texture-only import surface;
- declared-but-unimplemented buffer exchange;
- implicit node communication through `IsAllocated` unless Set 2 proves a real
  allocator requirement;
- ambiguous descriptor equality.

### Vanguard-specific

- generation-safe RHI resource retention;
- `rhi::ResourceRef` only for resolved physical identity;
- reuse of existing RHI descriptors and failure vocabulary;
- placed/deferred-binding terminology;
- no duplicate frame allocator currently exists, so this can begin with a clean
  public contract.

## 11. Questions Passed To RED Set 2

1. How is a generated temporary id derived so parallel recording and Consume
   replay produce the identical tag?
2. What exact tuple defines request time and deterministic merge order?
3. Which fields are compared during replay, including flow group for queue sync
   and decisions?
4. How are duplicate declarations, descriptor-source declarations, free, and
   redeclaration represented over time?
5. How are nested or overlapping use ranges validated?
6. How do logical swaps change tag-to-resource mappings at later request times?
7. Does `RequestIsAlloc` have any defensible temporal meaning, or can Vanguard
   remove it completely?
8. How are recorded decisions indexed and replayed when branches nest?
9. How are imported/external references retained and compared during replay?
10. Is `ScopeNameTag::Clear()` used to transfer/end responsibility, or is it an
    unbalanced-range escape that Vanguard should reject outright?
11. Is explicit `Free` a supported semantic operation in nontrivial timelines?
12. Which malformed streams are caught, and which currently rely only on
    assertions or fixed-capacity assumptions?

## 12. RED Set 1 Exit Gate

Passed.

Every public allocator operation can now be described without relying on RED's
internal request storage or physical allocation algorithm. The remaining
questions are explicitly routed to Set 2 or later sets.

At the Set 1 checkpoint, no production code had changed and Set 2 remained
gated on review.

## 13. RED Set 2 Source Snapshot

Set 2 was restricted to the logical request-tape portions of RED's internal
allocator. Physical assignment and alias placement in the same implementation
file were not studied as Set 2 design input.

```text
renderer/src/renderFlowInternalData.h
  lines: 393
  sha256: 1426C35474FA7F5EE1725881B39B9646C4F35A44EEE440153E202246DB0ED333

renderer/src/renderFlowInternalData.cpp
  lines: 2027
  sha256: CDF7E3C296EF9D6FD465504E8C509EC25D6BCD9F4204F29FF39AE9B2E4A77AD4
```

Two representative call sites were inspected only to resolve Set 1's
`ScopeNameTag::Clear()` question:

```text
renderer/src/renderNode_RenderTargets.cpp
  lines: 710
  sha256: 38F0873E2ACDDB528C2693C1A0B0DF193544D23BF5523A06AB57CA8B609F7A30

renderer/src/renderNode_SubsurfaceScattering.cpp
  lines: 378
  sha256: 749A3B8E7BE10739156CCA9FB7C563AFAE6F4698A3CD6D3364A51091AAEABFE5
```

## 14. RED Request Tape And Deterministic Time

RED's internal request vocabulary is one packed tagged record containing:

```text
AllocByDesc      AllocByTag       IsAlloc
UseBegin         UseEnd           Free
Swap             SwapWithRef      InjectResource
BeginQueue       EndQueue         QueueSync
Decision
```

Evidence: `renderFlowInternalData.h:12-83`. The record is deliberately compact;
RED asserts a 56-byte `SRequest` at `renderFlowInternalData.cpp:6-9`. This is a
storage optimization, not a suitable Vanguard public type: the unions allow
invalid payload combinations and cause queue and decision data to be read as a
name-tag hash by the common indexing path.

### Parallel recording without scheduling-defined order

RED owns one sequential request array and one replay cursor per GPU flow group
(`renderFlowInternalData.h:86-109`). PreConsume assumes one sequential writer
for a given group, while distinct groups may be recorded concurrently. Atomic
appends collect sortable keys, but atomic arrival order is not semantic order
(`renderFlowInternalData.cpp:943-1012`).

RED defines request time as:

```text
(gpu_flow_group, request_ordinal_within_group)
```

and packs it as:

```text
(group << 16) | ordinal
```

Per-tag sorting prepends the 32-bit name-tag hash. Sorting therefore produces
tag order followed by deterministic GPU-flow and local-request order, regardless
of CPU worker completion order (`renderFlowInternalData.cpp:112-152,1430-1433`).

**Copy:** per-group sequential tapes, parallel recording across groups, and the
lexicographic `(GPU flow group, local ordinal)` logical order.

This is a deterministic total order, not proof of a general multi-queue
happens-before relation. Queue correctness remains assigned to Set 4.

**Adapt:** represent the coordinate directly:

```cpp
struct RequestPosition
{
    GpuFlowGroupId group;
    uint32_t ordinal;
};
```

Do not pack field widths into correctness-sensitive integers. Flow groups own
ordering; atomics may reserve memory but must never decide logical time.

**Reject:** RED's fixed capacities of 160 groups, 300/320 requests per group,
4096 total requests, 320 allocation requests, 16 fork/join requests, and 64
nontrivial tag occurrences (`renderFlowInternalData.h:31-45`). Several atomic
writers reserve array entries without checking capacity before writing
(`renderFlowInternalData.cpp:958-992`). Vanguard should use growable pre-sized
storage with explicit configurable budgets and checked `CapacityExceeded`
failures.

**Reject:** grouping correctness by only a 32-bit hash. Full structured
`LogicalResourceId` values must key timelines; hashes may accelerate lookup but
collisions must still compare the complete id.

### Deterministic temporary identity

RED derives a temporary tag from the location of its upcoming declaration:

```text
temporary id = (flow_group, next request ordinal)
```

PreConsume uses the current group-tape length; Consume uses the current replay
cursor. RED packs 12 bits of each coordinate into a 24-bit autogenerated id,
reserves zero for named resources, and uses the diagnostic name
`"TempNameTag"` (`renderFlowInternalData.cpp:756-788`).

**Copy:** temporary identity comes from deterministic declaration position, not
from a global atomic counter or debug label.

**Adapt:** make it a structured frame-local id:

```text
LogicalResourceId::Temporary {
    declaration: RequestPosition,
}
```

Temporary creation and declaration must be one operation so no unused generated
id can desynchronize the coordinate. The optional label is diagnostic only.

## 15. Exact Replay And Recorded Decisions

Consume selects the group tape, reads the next expected record, and compares it
with the operation being replayed (`renderFlowInternalData.cpp:881-939`). At
Cleanup, RED checks that every touched group consumed every recorded request
(`renderFlowInternalData.cpp:1861-1874`).

RED uses request-specific equality rather than raw-byte equality
(`renderFlowInternalData.cpp:17-91`):

| Request | RED replay comparison |
|---|---|
| declare by descriptor | tag, group, descriptor |
| declare like | destination tag, source tag, group |
| use begin | tag, group, usage flags |
| use end / free | tag, group |
| swap | both tags and group |
| swap with external | logical tag and group; external ref ignored |
| import/inject | tag and group; external ref ignored |
| queue begin/end | queue/group fields according to opcode |
| queue sync | sync type only |
| decision | opcode/position only; proposed value ignored |

For a decision, PreConsume records the proposed value. Consume may propose a
different value, but returns the value stored at the matching tape position
(`renderFlowInternalData.cpp:917-931,996-1004`). This guarantees that a branch
which changes the request stream follows the same path in both phases.

**Copy:** strict positional replay, opcode-specific structural comparison,
recorded decision results, and a final no-leftover-requests check.

**Adapt:** every decision also carries a stable `DecisionId` or operation label.
Position alone cannot diagnose two adjacent decisions being exchanged.

**Adapt:** external operations compare a stable external-resource token,
resource kind, authoritative descriptor, and ownership mode. A raw backend
handle need not be byte-identical, but external identity must not be silently
ignored as it is in RED.

**Adapt:** validate group, bounds, and request equality before advancing the
replay cursor. RED increments first and relies mostly on assertions. Vanguard
must return a structured mismatch without corrupting replay state:

```text
TapeMismatch
  phase
  group and ordinal
  expected typed request and origin
  actual typed request and origin
  complete logical ids and debug labels
  nearby expected tape records
```

One group has exactly one recording owner and one positional replay owner.
Command-list subwork sharing that group remains sequential inside the owner.
Different groups may replay concurrently.

## 16. What RED Actually Computes For Lifetimes

`SAllocationRequest` represents one logical allocation version and stores its
descriptor, start/end timestamps, and eventual physical indirection.
`STagLifetime` either has a trivial direct version or a sparse timeline of
events that change which version a tag resolves to
(`renderFlowInternalData.h:229-295`).

This is the crucial Set 2 correction:

```text
RED does not validate balanced resource-use scopes.
```

For a trivial tag, every request after its declaration merely expands one
contiguous first-to-last-touch interval. A declaration that is never touched
does not produce an allocation request (`renderFlowInternalData.cpp:1479-1530`).

For a nontrivial tag, `UseBegin` and `UseEnd` execute the same lifetime update:
set the first-use timestamp if needed, extend the end timestamp, and append an
event. Missing optional mappings are silently skipped
(`renderFlowInternalData.cpp:1715-1748`). RED has no begin/end stack, use id,
underflow check, leftover-open-use check, or crossing-scope diagnostic.

**Copy:** unused declarations may be eliminated, and a logical allocation's
default retention span is conservatively first use through last use. Gaps are
not automatically aliasable because contents may need to survive until a later
use.

**Adapt:** use requests must be balanced and identify the same logical use:

```cpp
UseBegin { ResourceUseId use, LogicalResourceId resource, ResourceAccess access }
UseEnd   { ResourceUseId use, LogicalResourceId resource }
```

`UseBegin` captures the allocation version mapped at that position. Its matching
`UseEnd` closes that captured version even if the logical tag is swapped in the
middle. This avoids RED's behavior where an end-by-tag may extend whichever
version happens to be mapped after a swap.

Short lexical guards may own a generated `ResourceUseId`. Cross-node scopes
must use an explicit stable scope id and explicit end request. Optionality must
also be explicit; a missing declaration must not silently turn an ordinary use
into a no-op.

### `ScopeNameTag::Clear()` resolved

`ScopeNameTag::Clear()` only nulls the guard's context pointer and suppresses
its destructor's `UseEnd` (`renderNodeImplContext.h:114-183`). RED uses it to
transfer responsibility across nodes:

- G-buffer setup opens uses, clears the local guards, and a later end node calls
  explicit `RTUseEnd` (`renderNode_RenderTargets.cpp:71-77,109-115,159-167`);
- SSS emissive setup does the same for `color`
  (`renderNode_SubsurfaceScattering.cpp:73-94`).

**Reject:** a Vanguard guard must not expose a detach-without-end escape. Use an
explicit cross-node scope id when a lifetime intentionally outlives a local
guard.

## 17. Temporal Mappings, Swaps, And Imports

RED marks tags involved in declare-like or swap operations as nontrivial and
processes their requests in total logical order
(`renderFlowInternalData.cpp:1563-1755`).

Observed behavior:

- `AllocByTag(destination, source)` copies the source descriptor into a distinct
  logical allocation version. It does not share the source physical identity;
  a missing source only asserts (`1593-1626`).
- `Swap(A, B)` exchanges the current allocation-version mappings and appends a
  mapping event to both tag timelines (`1630-1655`).
- `SwapWithRef(tag, external)` ends the outgoing mapping and installs a new,
  non-aliasable external version under the tag (`1658-1713`).
- a normal injection is handled only in RED's trivial path (`1531-1549`); there
  is no nontrivial injection handler.

The Vanguard logical model should separate three identities:

```text
LogicalResourceId       stable name in a flow space, or temporary declaration
LogicalAllocationId     one declared/imported logical allocation version
ExternalResourceToken   stable identity and ownership proof for an import
```

A tag timeline records only mapping changes:

```cpp
struct MappingChange
{
    RequestPosition effective_after;
    std::optional<LogicalAllocationId> value;
};

struct LogicalAllocationPlan
{
    LogicalAllocationId id;
    FrameResourceDesc descriptor;
    AllocationSource source;
    std::vector<UseInterval> uses;
    std::optional<RetentionInterval> retention;
};
```

Every request has explicit boundary semantics:

```text
value_before(position) -> operands observed by this request
apply request
value_after(position)  -> mapping observed by following requests
```

Example:

```text
(7,0) Declare A -> X        after: A=X
(7,1) Declare B -> Y        after: B=Y
(7,2) UseBegin A            captures X
(7,3) Swap A,B              before A=X,B=Y; after A=Y,B=X
(7,4) UseBegin A            captures Y
```

This replaces RED's implicit predecessor-event search. RED returns the previous
event at an exact later event timestamp and can return null after the last
timeline event; its typed getter dereferences that result without checking
(`renderFlowInternalData.cpp:1916-1950`). Explicit before/after lookup has no
off-by-one or uncovered-tail state.

### Declaration and swap validation

Vanguard resolves requests in total order while maintaining:

```text
current_mapping[LogicalResourceId]
open_uses[ResourceUseId]
timelines[LogicalResourceId]
logical_allocations[LogicalAllocationId]
```

Rules:

- declaration requires an unmapped destination, except for an explicitly
  idempotent same-version redeclaration before any mapping-changing operation;
- an idempotent redeclaration requires exact declaration equality; conflicting
  descriptors fail;
- declare-like requires a currently mapped, type-correct source and creates a
  new logical allocation version;
- swap requires both mappings and named `logical swap compatibility` from Set 1;
- imports support textures and buffers and work in both trivial and nontrivial
  timelines;
- begin requires a current mapping and unique use id;
- end requires the matching open use and closes its captured version;
- resolved access is Consume-only, type-correct, and tied to an active use at an
  explicit current position;
- a returned physical handle is a snapshot and must not be cached across a
  mapping change.

The exact descriptor-to-physical-allocation rules remain Set 3.

## 18. `Free`, `IsAlloc`, And Other RED Gaps

### `Free` is not a complete RED operation

RED can record and replay `Free`, but the logical resolver has no nontrivial
`Free` case. In the trivial path it is only another timestamp, does not remove
the tag from the lifetime map, and does not make `IsAlloc` false
(`renderFlowInternalData.cpp:52-54,922-926,1496-1513,1581-1753`). A repository
search found no allocator `Free` call site beyond its wrappers.

**Reject for Vanguard V1:** do not expose `Free` while its use case and contract
are absent. If later needed, add an explicit `ReleaseLogicalMapping` operation:
it must require no incompatible open uses, close the current mapping at
`value_after(position)`, and define whether redeclaration creates a new logical
allocation version. It must never be only a lifetime hint.

### `IsAlloc` has no useful temporal contract

RED's Consume result only tests whether the tag has any lifetime-map entry, not
whether it is mapped at the current request time. RED itself comments that this
may be wrong (`renderFlowInternalData.cpp:922-926`). A repository-wide search
found no renderer call sites beyond the wrapper and implementation.

**Reject:** omit `IsAllocated` / `IsDeclared` from the Vanguard V1 node API.
Request-affecting conditions use recorded decisions. Optional resource access
must be explicit and time-aware, not hidden communication through allocator
map membership.

### Other defects not to copy

- duplicate declarations are not strictly validated; the trivial scan can let
  a later descriptor replace an earlier one;
- import/exchange replay ignores external reference identity;
- use requests for missing nontrivial tags may silently disappear;
- `STagLifetime::SEvent::m_queue` exists but is not initialized by
  `FillInLifetimeEvent` (`renderFlowInternalData.h:286-292`,
  `renderFlowInternalData.cpp:164-171`);
- invalid groups, malformed descriptors, replay mismatch, and capacity errors
  rely mainly on assertions;
- allocation descriptor validation runs after request recording/replay mutation
  (`renderFlowInternalData.cpp:790-850`).

Vanguard validates before mutating tape or cursor state and reports errors with
the complete request origin.

## 19. Vanguard Set 2 Logical Tape Model

The minimum model carried into synthesis is:

```cpp
using FlowRequest = Variant<
    Declare,
    DeclareLike,
    Import,
    UseBegin,
    UseEnd,
    Swap,
    ExchangeExternal,
    QueueBegin,
    QueueEnd,
    QueueSync,
    Decision>;

struct RequestRecord
{
    FlowRequest request;
    RequestOrigin origin;
};

struct GroupTape
{
    std::vector<RequestRecord> requests;
    uint32_t replay_cursor;
    RecordingOwner owner;
};

struct RequestTape
{
    TapeState state;
    DynamicGroupStorage groups;
    TapeBudget budget;
};
```

`RequestOrigin` includes at least the render node id/name, flow space or camera,
operation label, and useful authoring location. Flow group is owned by the group
tape and `RequestPosition`; it is not redundantly trusted inside every payload.

Tape state is explicit:

```text
Recording
  -> Sealed          all PreConsume writers joined; tape immutable
  -> LogicalResolved
  -> Replaying       Consume
  -> Complete        every group cursor reached its end
```

Set 3 will insert physical resolution into the Resolve boundary without changing
the sealed logical tape.

### Logical resolve pseudocode

```text
seal tapes after the PreConsume join

for position in lexicographic(group, ordinal) order:
    request = tape[position]
    validate request against value_before(position)

    declare/import:
        create a distinct LogicalAllocationId
        append mapping effective after position

    use_begin:
        capture current LogicalAllocationId under ResourceUseId

    use_end:
        close the captured LogicalAllocationId interval

    swap/exchange:
        append mapping changes effective after position

    decision/queue marker:
        update its own validated logical state

reject unmatched ends, leftover opens, malformed queue scopes,
missing sources, incompatible swaps, and conflicting declarations

for each logical allocation version:
    retention = first use through last use
    omit a physical request if it has no use
```

Queue begin/end and fork/join markers must already be structurally balanced in
the logical tape. RED pairs sorted fork/join timestamps and extends allocations
which start inside the interval through its join
(`renderFlowInternalData.cpp:1759-1788`). Whether that is sufficient for
Vanguard's real queues remains a Set 4 synchronization question; Set 2 records
the regions but does not invent physical alias safety from them.

## 20. Set 2 Validation And Test Contract

The logical layer can be tested without an RHI device. Required cases:

1. shuffled PreConsume worker completion produces the same positions and plan;
2. two distinct logical ids with the same hash remain separate;
3. tape underflow, mismatch, and leftover requests fail at the exact position;
4. replay failure does not advance the cursor;
5. a changed Consume decision proposal returns the recorded PreConsume value;
6. inserting a request changes later temporary positions but yields a precise
   origin diagnostic rather than a silent identity collision;
7. an unused declaration produces no physical-allocation candidate;
8. conflicting duplicate declarations fail;
9. missing/wrong-kind declare-like sources fail;
10. unbalanced, duplicated, and crossing use ids fail;
11. a use captures the same allocation version across a logical swap;
12. the swap example in Section 17 resolves `A` to `X` before and `Y` after;
13. missing or incompatible swap operands fail;
14. imports and external exchanges validate stable token, descriptor, kind, and
    ownership while preserving texture/buffer distinction;
15. lookup before first mapping, outside active use, or with wrong type fails;
16. malformed queue scopes and unmatched fork/join markers fail;
17. every configured tape budget fails before an out-of-bounds write.

No GPU texture, buffer, heap, alias barrier, or pool is needed for these tests.

## 21. Set 2 Decision Ledger

### Copy from RED

- one deterministic sequential tape per GPU flow group;
- parallel recording across independently owned groups;
- `(GPU flow group, local ordinal)` as logical request time;
- exact positional Consume replay and final completion validation;
- decisions returning their recorded PreConsume result;
- temporary identity derived from declaration position;
- separate logical allocation versions and time-varying tag mappings;
- declare-like creates a distinct version from a source descriptor;
- swap changes mappings rather than copying resource contents;
- unused declarations may be elided;
- conservative first-use-to-last-use retention by default.

### Adapt from RED

- typed request variants and full logical ids;
- structured request positions instead of packed bit fields;
- dynamic storage with explicit checked budgets;
- stable decision, use, origin, and external-resource identities;
- explicit before/after mapping boundaries;
- balanced uses which capture an allocation version;
- texture-and-buffer imports in nontrivial timelines;
- strict optional-resource semantics;
- structured validation before state mutation;
- active-use, type, and request-position-aware resolved access.

### Reject from RED

- packed unions as the semantic request model;
- hash-only timeline grouping;
- atomic arrival order as anything more than storage reservation;
- unchecked fixed arrays and magic group/request widths;
- assertion-only replay and descriptor validation;
- unbalanced first/last-touch pretending to be begin/end validation;
- silent uses of undeclared optional resources;
- `ScopeNameTag::Clear()` guard detachment;
- V1 `Free` and `IsAlloc`;
- external-resource replay that ignores external identity;
- predecessor lookup with an uncovered tail;
- dead or uninitialized queue fields.

### Vanguard-specific

- `LogicalResourceId`, `LogicalAllocationId`, `ResourceUseId`, and
  `ExternalResourceToken` are distinct types;
- request origins are first-class diagnostic data;
- the sealed logical tape and logical plan are independently testable without
  an RHI device;
- existing generation-safe RHI references remain physical results, never
  logical timeline identity.

## 22. Questions Passed To RED Set 3

1. How does each `LogicalAllocationPlan` obtain size, alignment, heap class, and
   a physical resource object?
2. Which descriptor fields define whole-resource reuse, and which define placed
   memory compatibility?
3. Does RED use conservative retention spans directly for interval packing, and
   how are simultaneous endpoints treated?
4. When does RED reuse one resource object versus create distinct placed objects
   over the same memory?
5. How are imported/exchanged versions excluded from unsafe aliasing and cache
   ownership?
6. Which alias activation and retirement operation occurs at each allocation
   version boundary?
7. How do texture and buffer paths differ?
8. Which logical validation currently happens too late inside physical resolve?
9. What current Vanguard RHI capability can implement each operation, and what
   concrete gaps remain?

## 23. RED Set 2 Exit Gate

Passed.

Given a synthetic request tape, the model above can deterministically compute:

- the request at every `(group, ordinal)` position;
- every decision returned during replay;
- every logical resource's mapping before and after each request;
- which allocation version each use captures;
- each conservative logical retention interval;
- every malformed stream error;

without constructing a GPU resource.

No production code was changed during Set 2. Its physical-assignment questions
were carried into RED Set 3.

## 24. RED Set 3 Source Snapshot

RED Set 3 was restricted to physical resolve, pool/cache ownership, placed
resource construction, alias activation, and state-safe retirement. The exact
RED corpus is:

```text
renderer/src/renderFlowInternalData.h
  lines: 393
  sha256: 1426C35474FA7F5EE1725881B39B9646C4F35A44EEE440153E202246DB0ED333

renderer/src/renderFlowInternalData.cpp
  lines: 2027
  sha256: CDF7E3C296EF9D6FD465504E8C509EC25D6BCD9F4204F29FF39AE9B2E4A77AD4

gpuApi/include/gpuApiInterface.h
  lines: 2500
  sha256: 5805B58F7CC06FC30FE30EE31F0974D3FCC926717F009D6155D875569FAECA74

gpuApi/src/gpuApiCommonBarrier.cpp
  lines: 710
  sha256: 8160F7097263D4DD15C08DF8679134E649D7947E93D92F79CA10F0F20F8FD8AE

gpuApi/src/dx12/gpuApiDX12Alloc.h
  lines: 533
  sha256: D33741F930B9591DD7A16780F68F5B4712EC506313D4ABA6ACB9255D8A74825B

gpuApi/src/dx12/gpuApiDX12Alloc.cpp
  lines: 286
  sha256: 40DCBA3A348A1392703BF1D8554584DB8197655946BBAE8BC56DC4B7DB33CE72

gpuApi/src/dx12/gpuApiDX12Barrier.cpp
  lines: 277
  sha256: D95C8B7E92DAAC7E6BDFDC0C1CF51AE10860A10195DDA04F8C3CB6CDF4481D12

gpuApi/src/dx12/gpuApiDX12Texture.cpp
  lines: 3450
  sha256: 9781350A6C414FF778921E7CD319FC37CFBC9D566B8B7C10334CAAA320C524EF

gpuApi/src/dx12/gpuApiDX12Buffer.cpp
  lines: 2234
  sha256: 9880666225AAD6FDCD5F91F8BACA1E6B29724921C20CE353098C683BD386CE11
```

Two directly referenced support files were added only for shared alias-memory
ownership and `HashMap::Insert` behavior:

```text
gpuApi/src/dx12/gpuApiDX12.h
  lines: 1651
  sha256: 4BCEA0A5B9F3A7C75C9AAE7110BEF4B47CEE75BB8396E60C528991B97A30A00A

redContainers/include/hashMap.h
  lines: 325
  sha256: 511B0D97EDFD00D5619A4C3C6546A6C90EB56767BC46E092D9AA5751EA2AEC42
```

The current Vanguard RHI and vendored NVRHI D3D12 implementation were inspected
only to answer the Set 3 capability question. They were already modified in the
shared working tree before this study, so the claims below cite exact paths and
line ranges rather than treating Git `HEAD` as their source snapshot.

## 25. RED Physical Storage Model

Evidence:

- `renderFlowInternalData.h:111-260`
- `renderFlowInternalData.cpp:176-310`

RED separates frame-local logical allocation requests from persistent physical
pool objects.

`SAllocationRequest` is a frame-local candidate. It carries:

- the logical allocation/version indirection;
- active and maximum descriptors;
- first use, actual last use, and conservative packing end;
- worst-case byte size or texture aliasing requirements;
- alias and cache eligibility.

`SAllocation` is a persistent physical texture or buffer object. It carries the
exact descriptor of that object, its current-frame use interval, cacheability,
age, and resource reference.

`SAliasedBlock` is one complete backing-memory region. It owns:

```text
base resource
  -> a worst-case/dummy resource that anchors the memory allocation

placed child resources
  -> distinct active-shape resource objects over that same memory range

current-frame usages
  -> intervals which occupy the entire range
```

There is no byte-offset packing inside an `SAliasedBlock`. Every child aliases
the same complete range. A block is one temporal coloring slot, not a general
heap suballocator.

`SAllocationPool` persists across frames. It owns alias blocks and standalone
objects; the logical request/timeline data does not own the GPU memory.

### Vanguard adaptation

Vanguard already exposes an explicit `rhi::Heap`. It should represent an alias
block directly:

```text
AliasHeapBlock
  heap
  size/alignment/compatibility class
  placed texture or buffer objects
  current-frame occupancy intervals
  cache/age metadata
```

No dummy maximum-size texture or buffer is needed merely to own the heap. This
is an adaptation to Vanguard's existing RHI, not a second allocation API.

Vanguard V1 should copy RED's bounded whole-range model: one heap block, offset
zero, temporal sharing only. General offset suballocation can be studied later
without changing the logical request contract.

## 26. RED Resolve And Assignment Algorithm

Evidence:

- requirement initialization: `renderFlowInternalData.cpp:404-448`
- object construction batching: `1084-1144`
- physical assignment: `1147-1399`
- async lifetime extension: `1766-1787`
- request sorting: `1792-1818`

RED performs these steps:

1. Convert each used logical allocation version into an
   `SAllocationRequest`. Unused declarations are discarded.
2. Preserve the actual last-use time, then extend a separate packing end through
   async fork/join regions where necessary.
3. Compute worst-case requirements from maximum shape.
4. Sort requests by descending size, then ascending start time.
5. Sort existing alias blocks by descending size and then block id.
6. First-fit each request into a compatible block whose current-frame intervals
   do not overlap it.
7. Reuse an eligible exact resource object or create a new resource object.
8. If no block fits, create a new backing region and active-shape object.
9. Batch expensive placed-object creation on jobs and explicitly fence those
   jobs before Resolve completes.

This is a deterministic greedy heuristic only when every tie is deterministic.
It is not optimal packing, compaction, or budget-aware allocation. RED does not
recover cleanly from physical allocation failure.

### Endpoint convention

RED uses closed intervals:

```cpp
request.end >= existing.start && existing.end >= request.start
```

(`renderFlowInternalData.cpp:1014-1017`). Therefore `[2, 7]` and `[7, 10]`
overlap. A later allocation can reuse the memory only when its start is strictly
after the earlier end.

**Adapt:** Vanguard should keep the conservative behavior but encode it with
explicit request boundaries:

```text
occupancy = [Before(first use), After(conservative last use)]
```

Two occupancies sharing a request position therefore overlap. This avoids an
implicit integer-endpoint rule and composes with the before/after mapping
semantics established in Set 2.

### Stable order

RED stops its request ordering key at `(size, start)` and uses a non-stable sort
(`renderFlowInternalData.cpp:1800-1812`). Vanguard's final tie-breaker must be a
stable logical allocation/version id. Thread completion and container iteration
must not change the physical plan.

## 27. Two Different Reuse Strategies

RED uses both strategies discussed before this study. They must remain separate
in Vanguard terminology and diagnostics.

### Whole-resource reuse

The same resource object and its already owned memory are assigned again.

For standalone objects, RED permits same-frame reuse when:

- descriptors compare equal;
- the object remains cacheable;
- the new lifetime starts strictly after the previous lifetime ends.

Evidence: `renderFlowInternalData.cpp:1320-1347`.

No alias barrier is required because the GPU resource object does not change.
Ordinary declared state transitions and first-use initialization still apply.

Vanguard V1 should use an exact physical-object key:

```text
resource kind
active dimensions/count/mips/layers/samples
format
all native creation/usage flags
memory type
initial/clear metadata where it affects native creation
```

Planning-only maximum capacity and debug names are not part of this exact
active-object key. Usage-superset reuse may be studied later; V1 uses exact
native creation compatibility.

### True placed-resource aliasing

Distinct resource objects occupy one backing-memory region:

```text
heap range H
  texture or buffer object A at H+0
  texture or buffer object B at H+0
  texture or buffer object C at H+0
```

Only non-overlapping logical occupancies may use the same block. Switching from
one object to another requires an alias activation barrier.

Within an alias block, RED may reuse an exact placed child object across frames,
but deliberately never assigns that same child twice in one frame even when the
logical spans are disjoint (`renderFlowInternalData.cpp:1032-1050`). A different
object may still use the same memory.

**Copy for Vanguard V1:** a placed object may be assigned at most once per frame.
This keeps resource state, views, activation history, and diagnostics simple.
The heap memory still receives the intended same-frame reuse.

## 28. Active Shape, Maximum Shape, And Compatibility

Evidence:

- descriptor mapping: `renderFlowInternalData.cpp:176-310`
- request initialization: `404-448`
- RED GPU API requirement contract: `gpuApiInterface.h:681-692,1735-1744,1842-1855`
- D3D12 texture requirements: `gpuApiDX12Texture.cpp:1587-1647`
- D3D12 buffer requirements: `gpuApiDX12Buffer.cpp:912-959`

RED uses two descriptor shapes:

```text
maximum shape -> capacity required from the backing memory
active shape  -> resource object created for this frame's actual dimensions
```

For textures, maximum width, height, and mip count determine the alias-block
requirement. The child object uses current width, height, and mip count. For
buffers, maximum element count determines capacity while current element count
determines the child object.

The backend compatibility rule is conceptually:

```text
required range fits inside the block
heap base alignment satisfies the resource requirement
chosen offset satisfies the resource offset alignment
heap class == resource compatibility class
memory type matches
resource kind/policy is supported by that class
```

Alignment compatibility is not a generic numeric `required <= block` test. It
is a divisibility/placement condition defined by the backend. V1 uses offset
zero, but heap creation must still honor the returned base/alignment contract.

RED texture heap class distinguishes RT/DS and non-RT/DS resources on D3D12
resource-heap tier 1; tier 2 can use an all-resource heap class
(`gpuApiDX12Texture.cpp:1587-1604`). RED buffers use buffer-only heap flags,
64-KiB placement alignment, and additional accounting-group compatibility
(`gpuApiDX12Buffer.cpp:227-239,930-959`).

Even where tier 2 technically permits mixing, RED has no texture-buffer
cross-alias route. Vanguard V1 should likewise use separate texture and buffer
alias pools. Both kinds are supported, but they do not share one alias block.

### Requirement queries in current Vanguard

The existing RHI can query requirements only from a deferred-binding resource
object, not directly from a descriptor. For maximum shape, Resolve can create a
memory-unbound maximum-shape probe, query it, and cache the result. The cache key
must contain the complete descriptor plus backend/device epoch; a hash may
accelerate lookup but cannot be equality.

An eventual descriptor overload would be an ergonomic optimization, not a new
physical-allocation architecture.

## 29. D3D12 Placed Object And Memory Ownership

RED creates real D3D12 placed resources.

Texture evidence:

- placed creation: `gpuApiDX12Texture.cpp:391-549`
- alias factory and restrictions: `1654-1715`
- shared owner fields: `gpuApiDX12.h:333-367,421-460`

Buffer evidence:

- placed creation: `gpuApiDX12Buffer.cpp:206-306`
- alias eligibility: `912-959`
- shared owner fields: `gpuApiDX12.h:522-526`

A texture alias region is deliberately dedicated in RED. A buffer alias region
may itself be a D3D12MA suballocation, but every alias child still uses the same
allocation and offset. Each child has its own `ID3D12Resource`, views, and state
tracking while strongly retaining the shared allocation.

Mapped, readback, transient, and unsupported buffer modes cannot enter the RED
alias path. Aliased resources are created without uploaded initial data.

**Copy:** backing-memory ownership and placed resource-object ownership are
separate, and every placed object must strongly retain its heap/range.

**Adapt:** Vanguard's existing resource payload already retains its bound
`rhi::Heap`. The flow allocator should own normal `rhi::Texture`, `rhi::Buffer`,
and `rhi::Heap` wrappers and must not implement another reference-count or
fence-retirement system.

## 30. Alias Activation And Safe Retirement

Evidence:

- RED event creation: `renderFlowInternalData.cpp:1302-1313`
- RED Consume handling: `854-915`
- D3D12 alias barriers: `gpuApiDX12Barrier.cpp:113-210`
- state normalization: `gpuApiCommonBarrier.cpp:363-438`
- state import: `gpuApiCommonBarrier.cpp:507-557`

For each aliasable allocation, RED schedules:

```text
START at first use
  -> BarrierTextureAliasing or BarrierBufferAliasing

END at actual, unextended last use
  -> MakeStateSafeToRetire
```

The conservative async-extended end controls when the memory may be assigned to
another allocation. The actual end controls when the old resource object can be
normalized. These are intentionally different times.

The complete correctness sequence is:

```text
last access to old object
  -> make old state safe to retire
  -> establish command-list/queue happens-before
  -> alias barrier(old, new)
  -> optional backend discard for new
  -> first transition/access to new object
```

An alias barrier does not wait for another queue. `MakeStateSafeToRetire` is not
destruction and does not prove GPU completion. Set 4 must establish the queue
ordering around these operations.

RED normally passes a null `before` operand from the allocator. Vanguard should
pass the exact previous placed object when the block plan knows it. Null is
reserved for the first activation or a deliberately unknown prior occupant.
Before and after must be validated as the same heap range.

### Discard is not preservation

RED derives the texture discard Boolean from `RFUF_NoDiscard`. The original flag
comment says that discard may be unnecessary when the first operation clears
the resource. It does not promise preservation of contents after another object
occupied the memory.

Vanguard should represent this as an explicit activation policy, for example:

```text
RequestBackendDiscard
SkipBackendDiscardBecauseFirstUseInitializes
```

The resource-use contract separately determines whether the first access fully
initializes required contents.

### Physical action schedule

Vanguard must not copy RED's one-event-per-timestamp map. Each request position
needs two ordered lists:

```text
before_request[position]
  -> zero or more alias activations

after_request[position]
  -> zero or more safe-retire actions
```

Multiple resources may start or end at the same position. Actions use a stable
tie-breaker such as `(block id, allocation version id)`.

## 31. External Resources And Pool Ownership

Evidence:

- RED plain injection: `renderFlowInternalData.cpp:1531-1549,1880-1883`
- RED external exchange: `1658-1707`

Plain imported resources bypass RED's persistent physical pool. Their frame
wrappers are cleared at cleanup and they never enter placed aliasing.

`SwapWithRef` converts both sides to standalone objects. RED removes the outgoing
wrapper without releasing the transferred object and lets the incoming object
enter the standalone cache. It trusts the external object's descriptor.

Vanguard needs explicit ownership categories:

```text
ImportedRetained
  -> strongly retained for the frame without ownership transfer, never cached
     or aliased

TransferredIn
  -> may enter the standalone pool only after explicit ownership transfer and
     authoritative descriptor validation

TransferredOut
  -> allocator-created object is forced standalone and non-cacheable until the
     exchange, then its owning reference is handed out and removed from
     allocator ownership
```

The stable `ExternalResourceToken` from Set 2 proves replay identity; it does not
by itself prove descriptor compatibility or ownership transfer.

## 32. Cross-Frame Cache And Retirement

Evidence:

- pool records: `renderFlowInternalData.h:152-206,389-391`
- normal cache release: `renderFlowInternalData.cpp:624-750`
- forced clear: `2003-2025`

RED retains alias blocks, placed child objects, and standalone objects across
frames. Used entries reset age; unused entries age; ordinary cleanup uses an
eight-frame threshold. Underfilled alias blocks may be discarded as a whole,
while old children can be removed individually.

Vanguard should copy persistent ownership but adapt policy:

- record `last_used_frame`, owned bytes, object count, and block utilization;
- use explicit byte/object budgets and a configurable age threshold;
- reset only frame occupancy data during Cleanup;
- evict placed children before an otherwise unused heap block;
- release a heap when the pool owns no useful children or reservation for it;
- surface fallback, allocation, eviction, and retained-byte counters.

Releasing an RHI wrapper is sufficient. Vanguard's existing lifetime manager
retires native objects by recorded graphics/compute/copy fences. The flow
allocator must not duplicate generation tracking, fence epochs, or deferred
destruction.

## 33. RED Defects Vanguard Must Not Copy

The following are source-observed defects or unsafe assumptions, not stylistic
preferences.

1. `SAliasedBlock` leaves size/age fields uninitialized, and ordinary new-block
   creation does not seed every field (`renderFlowInternalData.h:171-196`;
   `.cpp:383-392,1281-1289`).
2. Alias actions use `HashMap<timestamp, one event>`. Insert failure is ignored,
   so two ends at one swap timestamp can lose a required retirement
   (`renderFlowInternalData.h:384`; `.cpp:1302-1313,1630-1654`;
   `redContainers/include/hashMap.h:186-193`).
3. Debug builds include debug-name hash in placed-child reuse and can produce a
   different object count than release builds (`renderFlowInternalData.cpp:1039-1047`).
4. RED buffer descriptor comparisons omit typed format even though format affects
   size/native creation (`renderFlowResourceAllocator.h:149-155,191-198`).
5. Texture-requirement cache equality is only a 32-bit hash
   (`renderFlowInternalData.h:209-227`).
6. Request sorting lacks a complete stable tie-breaker.
7. External exchange trusts the supplied descriptor rather than the actual
   object (`renderFlowInternalData.cpp:1670-1704`).
8. Allocation failure, unsupported combinations, and late compatibility checks
   are primarily assertion/fatal paths (`455-503,558-618`).
9. Eviction contains unused policy input and uninitialized control state
   (`renderFlowInternalData.h:357`; `.cpp:318-397,658-750`).
10. The comment that standalone resources exist only for exchange is stale;
    non-aliasable buffers use the same path.

RED GPU API issues add further constraints:

- texture alias size is exposed as 32-bit and omits alignment from the public
  compatibility descriptor;
- `CanTexturesAlias(nullptr, ...)` overpromises eligibility;
- alias barriers do not verify shared memory ownership;
- texture shared-memory ownership uses fragile manual reference counting;
- some failed allocations log and continue toward a null dereference;
- essential restrictions are assertion-only;
- activation assumes the after object is not already tracked on that command
  list.

Evidence: `gpuApiInterface.h:681-692`;
`gpuApiDX12Texture.cpp:1607-1715,3412-3449`;
`gpuApiDX12Buffer.cpp:266-280`;
`gpuApiDX12Barrier.cpp:128-205`;
`gpuApiCommonBarrier.cpp:399-437,507-555`.

## 34. Existing Vanguard RHI Capability Audit

The essential mechanisms already exist. The flow allocator belongs above them.

| Required operation | Existing Vanguard mechanism | Set 3 decision |
|---|---|---|
| Generation-safe owned object | typed refs and owning `rhi::Texture`/`Buffer`/`Heap` wrappers | Reuse; do not add allocator-specific ref counting. |
| Deferred-binding texture/buffer | `TextureDesc::virtualResource`, `BufferDesc::virtualResource` | Reuse; documentation should call these placed/deferred-binding resources. |
| Query size/alignment/class | `GetMemoryRequirements` | Reuse the type, but make all fields authoritative. |
| Create backing memory | `CreateHeap` | Reuse after alignment/class lowering is corrected. |
| Place object in heap | `BindMemory(texture/buffer, heap, offset)` | Reuse after structured range/class validation is corrected. |
| Alias activation | `BarrierTextureAliasing`, `BarrierBufferAliasing` | Reuse; executor supplies ordering and exact operands. |
| Safe retirement state | `MakeStateSafeToRetire` | Reuse; it is a state transition, not a fence. |
| Deferred native destruction | existing RHI resource lifetime manager | Reuse; no second retirement queue. |
| Heap residency | existing D3D12 residency maps and command-resource retention | Reuse; frame allocator does not own general residency policy. |

Key evidence:

- public creation/query/bind API:
  `source/rhi/include/vanguard/rhi/rhi.hpp:23-25,60-63`
- deferred-binding flags and requirement/heap records:
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:676-698,742-754`
- state, alias, and retirement API:
  `rhi.hpp:207-217`
- virtual-object lowering:
  `source/rhi/nvrhi/src/common_backend.cpp:1311-1341,1432-1457`
- heap binding and retained heap ownership:
  `common_backend.cpp:2394-2426`
- alias barrier lowering:
  `common_backend.cpp:3629-3663` and
  `source/rhi/nvrhi/src/d3d12_backend.cpp:1164-1181`
- state-safe retirement:
  `common_backend.cpp:3673-3679`
- generation/fence retirement:
  `source/rhi/nvrhi/src/resource_lifetime.cpp:148-253,846-1009`

Current Vanguard command lists disable NVRHI automatic barriers
(`common_backend.cpp:2483-2490`). The graph/executor must therefore record the
safe-retire transition, alias barrier, and first-use transition deliberately;
the RHI will not reconstruct missing flow-allocator ordering.

### Concrete RHI gaps

The APIs exist, but general true aliasing is not yet truthfully described by the
current backend:

1. `GetMemoryRequirements` hard-codes `compatibilityClass = 0` for textures and
   buffers (`common_backend.cpp:2428-2442`).
2. `CreateHeap` drops public alignment and compatibility class
   (`common_backend.cpp:1509-1516`).
3. Vendored NVRHI D3D12 hard-codes heap alignment and heap flags. Tier 1 always
   selects RT/DS texture heaps; tier 2 selects all-resource heaps
   (`external/nvrhi/upstream/src/d3d12/d3d12-device.cpp:1204-1218`).
4. Vanguard advertises aliasing whenever NVRHI reports virtual resources
   (`common_backend.cpp:1247-1261`), which does not express tier-1 resource
   classes honestly.
5. `BindMemory` lacks explicit virtual-resource, offset-alignment, overflow,
   range-fit, memory-type, and compatibility-class diagnostics
   (`common_backend.cpp:2394-2426`).
6. Resource payloads retain the heap but not bound offset/range, preventing the
   backend from validating alias barrier overlap.
7. Real D3D12 tests cover one two-buffer same-offset path, not texture aliasing,
   invalid placement, discard, or cross-queue activation
   (`source/rhi/nvrhi/tests/d3d12_backend_tests.cpp:511-523,895-901`).

These are corrections to the existing RHI contract, not justification for a
new RHI implementation.

### Conservative enablement rule

Whole-resource reuse requires none of the incomplete placed-memory machinery and
is safe whenever normal resource creation is available.

True placed aliasing is enabled only when the active backend profile proves:

```text
resourceAliasing capability is truthful
requirements size/alignment/class are authoritative
heap creation honors those requirements
BindMemory validates the placement
typed alias barriers and safe retirement are available
```

Until those conditions hold, Resolve selects standalone whole-resource reuse.
It must not interpret `compatibilityClass == 0` as proof that every resource can
share a heap. A runtime placement failure may fall back to standalone allocation
only through an explicit policy, diagnostic, and counter; it must not be silent.

## 35. Vanguard Set 3 Physical Plan

The logical model from Set 2 remains unchanged. Physical Resolve consumes each
`LogicalAllocationPlan` and produces one of:

```text
Imported-retained assignment
  -> strong frame retention without ownership transfer; never pooled or aliased

Transferred-in standalone assignment
  -> existing external object whose ownership is explicitly transferred to the
     allocator; never placed, cacheable only after descriptor/ownership proof

Whole-object assignment
  -> exact cached or newly created ordinary texture/buffer; a transfer-out plan
     is forced here, remains non-cacheable, and hands the object away at exchange

Placed assignment
  -> active-shape deferred object + AliasHeapBlock + offset zero
```

### Deterministic resolve outline

```text
validate and seal all logical plans

for each used plan:
    if imported-retained:
        validate token, descriptor authority, kind, and frame retention
        assign retained external object
        continue

    if transferred-in:
        validate token, descriptor authority, kind, and ownership transfer
        assign incoming object as standalone
        mark cacheable only when the transfer contract permits it
        continue

    # Remaining candidates are allocator-created owned objects. A plan whose
    # object will be transferred out is marked force-standalone/non-cacheable.
    obtain active-object key
    obtain maximum-shape memory requirements
    preserve actual_end and conservative_occupancy_end

sort remaining allocator-created candidates by:
    descending required size
    descending required alignment
    compatibility class
    first-use position
    stable logical allocation id

for each remaining allocator-created candidate:
    if true aliasing is disabled or plan is not eligible:
        assign exact whole object with non-overlapping occupancy
    else:
        find first deterministic compatible AliasHeapBlock with no overlap
        create block if none fits
        reuse an exact placed child unused this frame, or create/bind one
        record occupancy

derive each block's chronological occupant sequence
emit before-request activation lists
emit after-request retirement lists
validate the complete physical plan
fence all resource-construction jobs
publish resolved lookup tables atomically
```

Resolve must not expose a partially assigned plan. Any fatal failure leaves
Consume unavailable for that frame.

### Required physical records

Names may change during execution planning, but the concepts are locked:

- `PhysicalResourceObjectId`: allocator-local stable id for diagnostics;
- exact active descriptor and physical-object key;
- owning typed RHI resource;
- whole-object or placed strategy;
- heap/block id and bound range for placed objects;
- returned `MemoryRequirements`;
- frame occupancy and last-used frame;
- explicit cacheability/ownership state;
- activation and retirement action references.

Generation-safe `rhi::ResourceRef` remains backend object identity. It does not
replace allocator-local assignment, block, or timeline identity.

## 36. Set 3 Validation And Test Contract

Logical-tape tests from Set 2 remain device-independent. Physical tests add a
fake requirement/heap backend first, then real D3D12 coverage.

Required deterministic planner cases:

1. exact standalone objects reuse only for non-overlapping lifetimes;
2. descriptors differing in format, samples, usage, memory type, or another
   native creation field never whole-reuse;
3. active shape creates the object while maximum shape sizes the block;
4. same-end/start positions overlap conservatively;
5. async extension delays block reuse but retirement stays at actual last use;
6. equal-size/start candidates use stable allocation-id tie-breaking;
7. different kind, memory type, compatibility class, or mutually incompatible
   alignment requirements spill to a different block;
8. overlapping intervals never share one alias block;
9. disjoint intervals use distinct placed objects over the same block;
10. a placed child is not assigned twice in one frame;
11. multiple starts/ends at one position retain every ordered action;
12. block occupant sequence produces exact `before` operands;
13. first activation permits null `before`; later known occupants do not;
14. imported-retained objects never enter a pool, transferred-out objects are
    forced standalone/non-cacheable until handoff, and transferred-in objects
    enter only the standalone pool after explicit ownership and descriptor
    validation;
15. requirement-cache hash collision still compares full descriptors;
16. allocation/binding failure is structured and never publishes a partial plan;
17. cache eviction releases owned refs while the RHI fence manager delays native
    destruction safely;
18. disabling true aliasing preserves resolved semantics through whole objects.

Required RHI/backend cases before general aliasing is enabled:

1. placed textures and buffers return nonzero, accurate size/alignment/class;
2. heap alignment and class are honored;
3. misaligned, overflowing, out-of-range, wrong-memory, wrong-class, and double
   bindings fail with precise codes;
4. two objects at the same validated range can activate in sequence;
5. barriers reject unrelated heap ranges;
6. texture discard and no-discard activation paths execute on real D3D12;
7. resource and heap references survive command submission and retire only after
   all queue fences complete;
8. tier-1 capability/class behavior is truthful or true aliasing is disabled;
9. debug-layer tests cover transition-old, alias-barrier, transition-new order;
10. cross-queue activation tests are added only after Set 4 locks queue rules.

## 37. Set 3 Decision Ledger

### Copy from RED

- finalize logical lifetimes before physical assignment;
- preserve actual end separately from conservative async-safe occupancy end;
- separate whole-object reuse from true placed aliasing;
- maximum shape for backing capacity and active shape for resource objects;
- deterministic size-first interval coloring;
- distinct placed objects over a shared memory range;
- explicit alias activation and state-safe retirement;
- external resources excluded from unsafe aliasing;
- persistent cross-frame pools and aged cache eviction;
- fence parallel object creation before Consume.

### Adapt from RED

- explicit `rhi::Heap` ownership instead of a dummy base resource;
- 64-bit size/alignment and authoritative compatibility class;
- separate texture and buffer alias pools in V1;
- explicit before/after request action lists;
- exact previous occupant in alias barriers when known;
- complete stable sort keys;
- full-descriptor requirement-cache equality;
- configurable cache budgets and counters;
- structured physical failures and explicit fallback policy;
- explicit import/transfer ownership categories;
- existing Vanguard fence-retired resource wrappers.

### Reject from RED

- uninitialized pool/cache state;
- one lifetime event per timestamp;
- hash-only correctness;
- debug-name-dependent allocation policy;
- descriptor comparisons missing format or native creation fields;
- 32-bit physical sizes and omitted alignment checks;
- assertion-only allocation/placement correctness;
- barriers accepting unrelated alias memory;
- implicit external ownership transfer and descriptor trust;
- silent alias fallback;
- copying the brute-force first-fit policy as an immutable long-term architecture.

### Vanguard-specific

- no new RHI abstraction: reuse current placed resource, heap, barrier, and
  retirement APIs;
- no duplicate generation/fence lifetime manager;
- whole-resource reuse is the universal safe path;
- true aliasing is capability/profile gated until current RHI requirement,
  alignment, class, and binding-validation gaps are corrected;
- `compatibilityClass == 0` is not wildcard compatibility;
- the complete physical plan is validated and published atomically.

## 38. Questions Passed To RED Set 4

1. Which object owns the persistent physical pool and exactly when is it reset or
   destroyed?
2. Which explicit job fence proves all PreConsume groups and Resolve construction
   jobs are complete before Consume?
3. Where are before-request activation and after-request retirement actions
   executed relative to request replay?
4. Which CPU and GPU dependency establishes old-occupant completion before an
   alias barrier on the same queue?
5. How do fork/join queue scopes extend occupancy, and which queue records safe
   retirement versus activation?
6. Can RED activate an alias on a different queue from its last use, and what
   exact submit/signal/wait chain makes that legal?
7. When does command-list closure flush a pending safe-retire transition?
8. When is Cleanup called, and what lets the cache release refs without waiting
   idle?
9. What minimal node-context split can execute the same typed request contract
   without scattered phase branches?
10. Which RED integration assumptions should Vanguard reject because its RHI has
    explicit per-queue fence retirement and disabled automatic barriers?

## 39. RED Set 3 Exit Gate

Passed.

For every Vanguard physical assignment, the model can now state:

- whether it is external, a reused whole object, or a placed object;
- which exact resource object it uses;
- which heap range it uses when placed;
- which size, alignment, class, kind, and memory-type checks make it compatible;
- why its occupancy cannot overlap another occupant of that range;
- which before-request alias activation and after-request safe-retirement actions
  apply;
- which owner retains the resource and heap across the frame/cache;
- why eventual release uses the existing RHI fence-retirement machinery.

No production code was changed in Set 3. The reviewed Set 4 findings follow.

## 40. RED Set 4 Source Snapshot

RED Set 4 was restricted to allocator ownership and the frame, job, graph, and
queue boundaries that make request replay safe.

```text
renderer/src/renderRenderFrame.cpp
  lines: 4980
  sha256: F2D36A9CF070D5CC0AFDBC828728781BC3BBE6FD7BC069FD5C8A487B9139E6D8

renderer/src/renderNodeGraph.cpp
  lines: 938
  sha256: 98BEBCF439C51698B6194EF1450966E32F5ED6A20559DED2EC3C2FC2D2049962

renderer/src/renderNodeGraphFactory.cpp
  lines: 548
  sha256: FF47C138F3D48EF83E1A071C3C8179FC481A4347A31C02941C1C2BE439B58495

renderer/src/renderGraphNodes.cpp
  lines: 2444
  sha256: 88CDF545C5C438A2807D85ED73310F1F608AB6D916DABF659E4123ABE64F7169

renderer/src/renderInterface.h
  lines: 1319
  sha256: 3858B296F3429F1A769726D8FAA9851C95812093EF9DCDAA527ECA1DF4BDBDCC

renderer/src/renderInterface.cpp
  lines: 3585
  sha256: 5E98A04FF8E0DCD281A532F51BA6F63226326E5CDF2366A93B219F2FDBDAC9DD
```

One directly referenced support file was inspected only to prove serialization
between rendered frames:

```text
renderer/src/renderCommandHandler.cpp
  lines: 2290
  sha256: 99499F347FF53D8E028384088DBA1C38F2D83B2F3AE17779540C8E00030AC552
```

The allocator internals and GPU API contracts cited below are the same pinned
files from Sets 2 and 3. No feature-rendering call site was needed.

## 41. Allocator Ownership And Lifetime

Evidence:

- owner field: `renderInterface.h:464-474`
- creation with renderer resources: `renderInterface.cpp:1408-1412`
- recreation after render-command synchronization: `924-946`
- shutdown after render-command synchronization: `1497-1500,1538-1549`
- frame and worker contexts borrow it:
  `renderRenderFrame.cpp:4260-4278,4846-4853`

RED's renderer owns one `CRenderFlowResourceAllocator`. A graph does not own it,
a node does not own it, and a frame does not construct it. Its physical pool and
cached resource objects survive frame cleanup. They are destroyed or recreated
only with renderer/device resources, or explicitly cleared by a cache-eviction
request (`renderInterface.cpp:699-710`).

The ownership shape is:

```text
renderer/device lifetime
  -> one Resource Flow Allocator
       -> persistent physical pools and cached resource objects
       -> one replaceable frame session

graph/frame/node contexts
  -> borrowed allocator/session access only
```

### Vanguard decision

**Copy:** one renderer/device-lifetime allocator owner and a frame-local session.
Do not put a physical pool in a graph cache or node context.

**Vanguard-specific:** `FrameRenderer` is the closest existing owner. It is a
renderer-side, device-lifetime object owned by `RenderingServiceImpl`, and its
`RenderFrame` method is already the exact location reserved for the missing
Render Graph executor. The intended containment is:

```text
RenderingServiceImpl
  -> FrameRenderer
       -> RenderFlowResourceAllocator
       -> future Render Graph executor/cache
```

The allocator is initialized only after RHI initialization and shut down after
the CPU rendering chain is quiescent but before RHI shutdown. Node contexts and
the graph executor receive references; they never receive ownership.

## 42. Exact RED Frame And Job Sequence

Primary evidence:

- early consume-job construction and kickoff gate:
  `renderRenderFrame.cpp:4811-4836`
- phase calls and resolve gate: `4929-4961`
- final consume wait and graph unlock: `4969-4977`
- CPU dependency-counter construction: `4254-4475`
- phase implementation: `renderFlowInternalData.cpp:1825-1910`
- parallel physical creation fence: `1084-1143,1381`

RED overlaps consume-job construction with allocator planning without allowing a
consume node to execute early:

```text
previous frame CPU tail complete
  -> built graph is held immutable
  -> construct consume jobs behind nodesKickoffCounter
       draw-buffer dependency
       + open allocator CompletionDeferral

  -> Startup
  -> PreConsume
  -> parallel request walk using Fence::None
  -> explicit PreConsume fence

  -> Resolve job
       logical resolve
       physical assignment / construction jobs
       explicit construction fence
       phase becomes Consume only after Resolve returns

  -> finish allocator kickoff deferral
  -> dependency-counter consume jobs may run
  -> unique terminal consume completion
  -> cleanup
  -> release graph immutable/exclusive execution guard
```

There are two distinct joins:

1. `renderNodeGraph.cpp:852-876` joins all parallel PreConsume buckets before the
   later Resolve job can run.
2. `FlowAllocBatcher::Finish()` joins physical resource-construction jobs before
   Resolve publishes the plan and `SetPhase(RFP_Consume)` returns.

Only after both joins does `FlowAllocator_Resolve_Finish` release the consume
kickoff deferral. Resolved getters therefore cannot race planning or partially
created resources.

RED also serializes a new rendered frame against the previous renderer CPU tail
through `m_flushCounter` (`renderCommandHandler.cpp:912-964`). This is CPU
serialization, not a per-frame GPU-idle wait.

### Vanguard decision

**Copy:** preserve the explicit PreConsume join, Resolve/construction join, and
consume kickoff gate.

**Adapt:** expose `Resolve` as a named operation returning a success/failure and
completion boundary. RED hides it inside `SetPhase(RFP_Consume)`.

**Vanguard-specific:** the existing `jobs::Builder`, `Counter`,
`CompletionDeferral`, `Fence::None`, and `DispatchFence()` already provide the
needed mechanics. `RenderCommandSystem` already serializes retained frames on
one CPU rendering tail and supplies its continuation builder through
`RenderFrameContext`. The allocator must reuse that chain, not create a private
scheduler.

## 43. GPU Flow Time Is Not CPU Worker Order

Evidence: `renderNodeGraph.cpp:731-760,784-876`.

Every built graph node has a unique dense GPU flow group. Sequential graph
execution flattens by that group. Parallel PreConsume deliberately does not: it
uses a deterministic quasi-random permutation and buckets of eight to balance
nodes with different request counts.

Each worker receives its own context copy. `SetupNodeData` installs the node's
GPU flow group, and that group selects one allocator tape. A bucket processes a
given node once and sequentially, so one group has one writer even though many
groups record concurrently.

```text
CPU schedule:
  group 9, group 2, group 14, group 1 ...

allocator order:
  (group 1, request 0..n)
  (group 2, request 0..n)
  ...
  (group 14, request 0..n)
```

Worker start time, completion time, bucket index, CPU dependency order, and
thread id are never allocator timestamps.

### Vanguard decision

**Copy:** per-GPU-flow-group sequential tapes, independent worker contexts, and
an explicit join before tapes become immutable.

**Reject:** RED's prime `1021` permutation and fixed node-count assumption. It
performs modulo by zero for an empty graph and ceases to be a permutation when
the graph reaches the prime in non-assert builds. Vanguard should use stable
chunk assignment without a magic maximum.

## 44. Consume Execution And The Process Wrapper

Evidence:

- common wrapper: `renderGraphNodes.cpp:114-235`
- consume worker context: `renderRenderFrame.cpp:4254-4279`
- CPU dependency wiring: `4298-4335`
- job dispatch and terminal continuation: `4337-4475`

RED consume does not use the parallel PreConsume walk. It creates one runtime
job per graph node and wires only CPU dependencies into readiness counters.
Exactly one graph sink is required so the frame has one completion continuation.

Every executable node goes through `CRenderNodeBase::Process`:

```text
setup node identity and worker-local context
  -> create/bind required command list in Consume
  -> reset per-node binding state
  -> call virtual Execute
  -> run or dispatch command-list epilogue after child jobs
```

`Execute` runs in both phases in RED. Command-list creation, binding, and GPU
epilogue happen only in Consume. Deferred epilogues capture the node context and
implementation pointer, so graph topology, node implementations, frame state,
and allocator session must all outlive the terminal job counter.

### Vanguard decision

**Copy:** retain a nonvirtual `process_node` wrapper around virtual node work,
CPU dependency-counter consume jobs, worker-local mutable contexts, and an
immutable/exclusive graph execution lifetime.

**Adapt:** the allocator exposes separate planning-writer and execution-replay
interfaces. The future graph/node API decides how a node describes one typed
resource contract, but planning code must not have resolved getters and GPU
execution code must not receive an unrestricted record-or-replay phase switch.
This removes RED's error-prone scattered `IsConsumePhase()` guards without
changing the process wrapper's role.

**Reject:** RED's global `CRenderNodeJob` current-frame pointer. Vanguard already
passes retained frame state through job payloads and `RenderFrameContext`.

## 45. Exact Replay Point For Physical Actions

Evidence:

- request dispatch: `renderFlowInternalData.cpp:789-806`
- consume cursor, comparison, and actions: `881-938`
- resolved lookup at current cursor: `1916-2002`

At Consume, RED selects the next request in the current flow-group tape and
compares it with the replayed request. Before returning from that request call,
it checks whether the exact `(group, ordinal)` has an alias lifetime event:

```text
first-use request boundary
  -> alias activation barrier
  -> node retrieves/uses the new resource

last-use request boundary
  -> all node commands using the old resource were already recorded
  -> MakeStateSafeToRetire(old resource)
  -> request call returns
```

The action is recorded on the command list active at that exact point. It cannot
be hoisted to generic node entry or delayed to generic node exit: one node may
open and close multiple resources, swap mappings, or contain ordered subnodes.

Resolved lookup uses the current group's already replayed ordinal. Thus a
time-varying logical tag resolves to the object valid at that request position,
not to one frame-global object.

### Vanguard decision

**Copy:** exact request-position activation and retirement.

**Adapt:** validate the replay request first, execute all ordered actions for the
boundary, then commit cursor advancement. RED increments its cursor before its
assertions and stores at most one action per request id. Vanguard uses the
ordered before/after action lists established in Set 3.

**Vanguard-specific:** physical actions require an execution context with an
active command recorder of the planned queue. The current RHI alias and
safe-retire functions operate on the thread-bound command list, and automatic
barriers are disabled. Calling replay without the correct command context is a
structured execution failure.

## 46. Queue Scopes And Submission Ranges

Evidence:

- command-list group: `renderNodeGraphFactory.cpp:18-181`
- sync node: `renderGraphNodes.cpp:279-298`
- frame command-list registry and submission:
  `renderInterface.h:196-208`; `renderInterface.cpp:137-187`
- request forwarding: `renderFlowResourceAllocator.cpp:202-230`

A graph-visible command-list group owns one graphics or compute command list and
an ordered list of subnodes. It records:

```text
BeginQueue(queue kind, parent GPU flow group)
  subnode request stream
EndQueue(parent GPU flow group)
```

All subnodes share the parent graph node's tape and remain sequential. If a
subnode dispatches child jobs, `EndQueue` and the outer process epilogue are
scheduled behind those jobs.

A separate synchronization node records `QueueSync` in both phases. In Consume
it also asks `RenderFrameCommandLists` to submit every unsubmitted command-list
slot through the sync node's GPU flow-group index. Submission then explicitly
fences its close/preparation work before clearing those slots.

RED's lifetime resolver uses `BeginQueue` and `EndQueue` only for exact replay;
their queue kind does not participate in physical compatibility or interval
packing. Only fork/join `QueueSync` markers affect lifetime widening.

### Vanguard decision

**Copy:** ordered queue scopes around command-list subnodes and a distinct sync
marker that is planned in PreConsume but performs real submission only during
Consume.

**Adapt:** queue scope is authoritative planning data in Vanguard. Every resource
request position is annotated with its graphics or compute queue, and replay
validates that the active command list has that queue. `EndQueue` must be
explicitly dependent on all child request work; do not rely on undocumented
builder ordering.

**Reject:** treating begin/end markers as replay-only decoration.

## 47. Fork/Join And Cross-Queue Alias Safety

RED's GPU API defines a bounded async-compute model
(`gpuApiInterface.h:1246-1262`):

```text
ForkAsyncCompute
  -> future compute work waits for prior graphics work

JoinAsyncCompute
  -> future graphics work waits for prior compute work
```

The renderer emits the same fork/join marker to the allocator and to command-list
submission. RED sorts those markers and extends the occupancy end of an
allocation that starts strictly inside the region through the join
(`renderFlowInternalData.cpp:1759-1788`). Its actual end remains unchanged so
safe retirement is still recorded at the real last use.

The necessary happens-before cases are:

```text
old graphics use before fork
  -> Fork wait
  -> compute alias activation is legal

old compute use inside region
  -> Join wait
  -> later graphics alias activation is legal

graphics use and compute use both inside the open region
  -> no cross-queue happens-before
  -> the memory must not alias
```

### Vanguard correction

RED's widening condition is insufficient as a general rule. An allocation may
start before the fork and still be used by one queue inside the async region.
Scalar GPU flow order cannot prove it completes before work on the other queue.

For V1, Vanguard uses a conservative region rule:

```text
if an allocation's actual use interval intersects an async fork/join region:
    occupancy_start = min(actual_start, fork_boundary)
    occupancy_end   = max(actual_end, join_boundary)

actual_start / actual_end remain unchanged for activation and retirement
```

This may forgo same-queue alias opportunities inside async regions, but it is
safe and simple. A later queue-DAG allocator may recover those opportunities by
proving that every old access happens-before the new activation.

Fork/join regions must be balanced, non-nested for the current V1 sync model,
and consistent between allocator tape and actual RHI submission. The allocator
does not create GPU synchronization; it only refuses unsafe memory overlap based
on synchronization the executor will really submit.

### Current Vanguard support

Vanguard already exposes `None`, `ForkAsyncCompute`, and `JoinAsyncCompute` and
serializes submission internally. Its NVRHI backend inserts graphics-to-compute
and compute-to-graphics queue waits and returns the completion fence of the
post-sync queue (`source/rhi/nvrhi/src/common_backend.cpp:2540-2680`). Reuse this
contract; do not add an allocator-specific fence API.

## 48. Cleanup, Cache Safety, And Failure Recovery

Evidence:

- RED terminal node: `renderGraphNodes.cpp:1601-1628`
- cleanup implementation: `renderFlowInternalData.cpp:1861-1905`
- unique-sink check: `renderRenderFrame.cpp:4337-4351`
- final consume wait: `4969-4977`

RED's ordinary `EndFrame` node changes `Consume -> Cleanup`. Cleanup checks, in
assert builds, that every touched group consumed its complete tape. It then
clears frame-local requests, lifetimes, imports, cursors, and alias actions while
retaining persistent pool objects. Eligible unused cache entries age and may be
released after eight frames.

Cleanup waits for CPU node processing and command recording, not GPU idle.
Released owning references remain safe because the GPU API/RHI defers native
destruction until recorded queue fences complete. Cache aging is disabled for
some blank/offscreen frames so they do not evict normal-scene working sets.

### Vanguard decision

**Adapt:** cleanup belongs to the frame executor epilogue after the verified
terminal consume counter, not in an ordinary graph-authored node. It runs exactly
once and releases the graph/session lifetime guard afterward.

Normal completed cleanup:

```text
all consume and child jobs joined
  -> exact replay completion validation
  -> close/discard all command recorders as required
  -> clear frame-local allocator state
  -> apply explicit cache-aging policy
  -> Idle
```

Failure cleanup is also a required state transition. A failure before plan
publication never releases the consume gate. A failure during Consume still
joins dispatched work, discards unsubmitted command lists, records the status of
submitted work, invalidates unsafe touched cache entries, clears the session
without normal cache aging, and returns the allocator to `Idle`. It must not
leave the next frame observing `Resolving` or `Replaying` state.

Releasing a cached RHI reference is enough for native fence-safe destruction;
the allocator must not add another deferred-destruction manager.

## 49. RED Integration Defects Vanguard Must Not Copy

1. `ExecuteParallel` has no phase assertion; calling it during Consume would run
   GPU node processing concurrently (`renderNodeGraph.cpp:813-878`).
2. The prime-based permutation has an empty-graph modulo hazard and a fixed
   `< 1021` correctness assumption (`833-845`).
3. The executor checks for exactly one sink but does not prove that sink is the
   allocator-cleaning `EndFrame` node (`renderRenderFrame.cpp:4337-4351`).
4. Cleanup replay-count validation exists only in assertion builds
   (`renderFlowInternalData.cpp:1863-1874`).
5. A failure before `EndFrame` can leave allocator phase, global job-frame state,
   and graph execution guards installed.
6. The external consume kickoff dependency is attached to every node and again
   to roots (`renderRenderFrame.cpp:4298-4335,4452-4463`). This is redundant.
7. Deferred process/group jobs capture raw implementation pointers. Correctness
   depends on external graph/node-store lifetime rather than an explicit token.
8. Command-list group child ordering and `EndQueue` depend on builder behavior
   that is not expressed in the allocator contract.
9. `BeginQueue`/`EndQueue` queue kinds are ignored by RED lifetime planning.
10. Async widening protects only allocations whose start is inside a fork/join
    interval, not every lifetime that overlaps concurrent queues.
11. `m_processEviction` lacks constructor initialization; an early cache clear
    can preserve/restore indeterminate policy state
    (`renderFlowInternalData.h:357`).
12. Essential phase, request-count, and queue-shape failures are largely
    assertion-only.

## 50. Existing Vanguard Integration Boundary

The current engine already has the outer machinery needed by the allocator:

| Required boundary | Existing Vanguard surface | Decision |
|---|---|---|
| Device-lifetime renderer owner | `RenderingServiceImpl` owns `FrameRenderer` and the RHI lifetime | Put the allocator inside `FrameRenderer`; do not expose it as an engine service. |
| Serialized CPU frame chain | `RenderCommandSystem::cpuTail` and retained `RenderFrameContext` | Reuse. |
| Render-path job continuation | `RenderFrameContext::GetJobs()` | Reuse; no private allocator scheduler. |
| Parallel work and explicit join | `jobs::Builder::DispatchParallel`, `Fence::None`, `DispatchFence` | Reuse. |
| Consume gate | `jobs::Counter` and `CompletionDeferral` | Reuse. |
| Exact insertion point | `FrameRenderer::RenderFrame` currently reports that no Render Graph executor is installed | Install graph/allocator orchestration here later. |
| Graphics/compute queue identity | RHI command-list type and `QueueType` | Reuse. |
| Fork/join submission waits | `rhi::CommandListSyncType` and serialized backend submission | Reuse. |
| Placed activation/retirement | current RHI alias barrier and safe-retire calls | Reuse after Set 3 validation gaps are fixed. |
| Fence-safe object release | current RHI lifetime manager | Reuse; do not duplicate. |

The missing production systems are the Resource Flow Allocator and Render Graph
executor themselves. The existing jobs, frame chain, queue submission, placed
resources, barriers, and fence retirement are not placeholders to reimplement.

`RenderingServiceImpl` already waits for CPU command shutdown and GPU idle before
RHI teardown. Future allocator shutdown must occur before the RHI is destroyed.
Device recreation likewise requires a quiescent frame chain and allocator pool
reinitialization.

## 51. Minimal Allocator/Graph Integration Contract

> **Historical checkpoint — do not implement this API:** Unreal U1 replaces the
> request-tape cursors, `begin_replay`, `replay`, and temporal tag getter below
> with the compile-once execution-packet contract in Section 63. Sections 81
> through 90 now provide the final semantic API. Only the lifecycle joins,
> publication gate, GPU-flow ordering, queue rules, and process wrapper findings
> remain authoritative from this section.

The allocator does not own graph topology, node implementations, jobs, command
lists, or submission. The graph/executor must provide:

- a stable `GpuFlowGroupId` for each graph-visible execution unit;
- exactly one sequential request writer and replay cursor per group;
- queue scope and sync markers matching actual command-list behavior;
- a joined PreConsume completion boundary;
- an immutable graph/node lifetime through terminal consume completion;
- the active command recorder and queue at each replayed physical action;
- one executor-owned normal-or-failure cleanup epilogue.

The allocator provides:

```text
begin_frame(frame serial, explicit cache policy)
record(group, typed request)
seal_after(preconsume join)
resolve(builder) -> validated unpublished/published result
begin_replay()
replay(group, typed request, command context)
get_resolved(tag, group, current position)
finish_frame(Completed | Aborted)
clear_persistent_cache()
```

These are semantic operations, not final method names.

The future node-facing split remains:

```text
PlanningContext
  -> typed declaration/use/swap/import/decision/queue requests
  -> no resolved resources, command list, or GPU calls

ExecutionContext
  -> exact replay of the planned typed requests
  -> resolved getters and command recording
  -> no ability to mutate the sealed plan
```

`process_node` remains the common wrapper around virtual node work. The exact
way typed per-node resource schemas are stored and passed is intentionally left
for the Render Graph design and Unreal U1 comparison; it is not allocator
ownership.

## 52. Set 4 Validation Contract

Required integration tests, initially with a fake graph and fake command
recorder:

1. shuffled PreConsume worker completion never changes logical or physical
   plans;
2. Resolve cannot begin until every recording worker joins;
3. Consume cannot begin until Resolve and all physical creation jobs join;
4. a failed Resolve never publishes a partial plan or releases the consume gate;
5. each consume job observes an immutable graph and live node/session owner;
6. resolved getters fail before replay and identify the object at the current
   `(group, ordinal)`;
7. activation occurs before first access and retirement after the last recorded
   access at the exact request boundary;
8. several actions at one boundary execute in deterministic order;
9. queue scopes are balanced and replay queue matches the active command list;
10. a graphics-to-compute alias requires a real fork wait;
11. a compute-to-graphics alias requires a real join wait;
12. two lifetimes overlapping one open async region never alias in conservative
    V1 planning;
13. cleanup runs once only after all consume/child jobs join;
14. incomplete replay is a release-build frame failure;
15. injected failure at every phase returns the allocator to `Idle` and leaves
    no open/unowned command list;
16. normal cleanup does not wait GPU idle, while released objects remain alive
    until their queue fences complete;
17. device teardown first quiesces frames, then releases allocator pools, then
    shuts down RHI.

## 53. RED Set 4 Decision Ledger

### Copy from RED

- renderer/device-lifetime ownership and cross-frame physical pools;
- per-frame request sessions borrowed by graph/node contexts;
- GPU-flow-group request time independent of CPU scheduling;
- worker-local PreConsume contexts and an explicit join;
- explicit Resolve/construction join before Consume;
- dependency-counter consume jobs behind a kickoff gate;
- a nonvirtual process wrapper and immutable execution lifetime;
- exact request-position activation, retirement, and resolved lookup;
- ordered queue scopes and paired allocator/runtime sync markers;
- cleanup after terminal CPU recording without a GPU-idle wait.

### Adapt from RED

- named, fallible phase operations rather than a permissive `SetPhase`;
- explicit planning and execution context capabilities;
- queue kind as physical-planning and replay-validation data;
- conservative whole-region async occupancy widening;
- executor-owned cleanup and abort recovery;
- explicit owner tokens for graph, node, and session job lifetimes;
- growable deterministic PreConsume scheduling;
- release-build validation and structured diagnostics.

### Reject from RED

- phase branching as the node safety boundary;
- global current-frame state;
- cleanup in an ordinary graph node;
- prime `1021` scheduling;
- assertion-only replay and lifecycle correctness;
- replay-only queue begin/end markers;
- implicit child-job command-list ordering;
- assuming total GPU flow order proves order between concurrent queues;
- widening only resources that start inside an async region;
- a failure path that can strand allocator state.

### Vanguard-specific

- `FrameRenderer` owns the allocator and future graph executor inside the
  existing `RenderingServiceImpl` device lifetime;
- `RenderCommandSystem` remains the one CPU frame chain;
- Vanguard Jobs provides all allocator/graph CPU synchronization;
- current RHI submission supplies real fork/join queue waits;
- current RHI lifetime management supplies fence-safe release;
- alias actions require the planned active command list because automatic
  barriers are disabled;
- no new RHI, job system, graph-owned pool, or allocator retirement queue.

## 54. Questions Passed To The Unreal Comparison

RED study is complete. Unreal U1/U2 should challenge only these remaining design
choices:

1. Can RDG-style compile-once resource declarations remove Consume request
   replay while preserving Vanguard's cached virtual-node process wrapper?
2. How does Unreal validate external versus extracted ownership and descriptor
   authority?
3. Which parts of pooled whole-object reuse belong above the transient heap
   allocator?
4. How are culled passes reflected in first/last use without executing planning
   code twice?
5. How are multiple acquire/discard actions at one pass boundary ordered?
6. What queue/fence model protects transient ranges across async compute, and is
   it less conservative than widening an entire fork/join region?
7. How does Unreal recover from graph compilation or transient allocation
   failure without publishing partial state?
8. Which cache budgets, garbage collection, and diagnostics are worth adapting
   over RED's fixed age threshold?
9. Can Unreal's validation layer inform Vanguard's release-build tape, queue,
   placement, and barrier diagnostics without importing RDG wholesale?

## 55. RED Set 4 Exit Gate

Passed.

The allocator now has an explicit Vanguard placement and complete RED-derived
integration contract:

- `FrameRenderer` owns it for the renderer/device lifetime;
- PreConsume and Resolve each have an explicit jobs join;
- Consume is released only after successful plan publication;
- GPU flow groups, not CPU workers, define request time;
- queue scopes annotate requests and sync markers correspond to real submission;
- cross-queue aliasing requires actual fork/join happens-before;
- physical actions execute at exact replay positions on the planned command
  recorder;
- cleanup runs once in the executor epilogue and has a defined abort path;
- persistent cache release delegates native safety to existing RHI fences.

No production code was changed at this RED-only checkpoint. Unreal Set U1 is
preserved in Sections 56 through 67, and U2 is now also complete in Sections 68
through 80.

## 56. Unreal U1 Source Snapshot

Unreal U1 was restricted to RDG logical resources, declared access, culling,
first/last use, import/extraction ownership, and the whole-resource pooled
fallback. Transient heap allocation, placed-resource aliasing, and backend alias
barriers remain U2.

Unreal source root:

```text
D:\UnrealEngine
```

This export has no Git metadata. `Engine/Build/Build.version` identifies it as
UE 5.8.1, branch `UE5`, compatible changelist `55116800`, with changelist `0`.
The version file has SHA-256
`FA0355D0DAFD1D4BD34983841799CD9E1093A273EC8EE93E71B73984E40055D9`.

The exact planned U1 corpus is pinned here:

```text
Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h
  lines: 1515
  sha256: 367CA11F59C00DB846121871CF1C4B52DBE7E424D4047D09C89B8505BA5ECB29

Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.inl
  lines: 266
  sha256: 51B22E6ABE69CA3BD13D16DE45A8C71990AE27F4C744C9C1EA4CCE0A5AA2E083

Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h
  lines: 1200
  sha256: 177C74FFC847EB6899127263B0E3B79298CF2D57C87877877EF9AB9A6A67B580

Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.inl
  lines: 757
  sha256: 3C3FC4FB833123A35975A34109077AD9A29A9A2506237933A14AF1967B81FD1B

Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp
  lines: 5259
  sha256: 49FE0A44086B74DFDFED4E9C1462B8B0FE7B5787232CDB4835497B1A1B2D5246

Engine/Source/Runtime/RenderCore/Private/RenderGraphResources.cpp
  lines: 223
  sha256: 908F18B1889D5697F44B41FDE43FE1AADFA246D0F72EBAC441C41FB94FB764AB

Engine/Source/Runtime/RenderCore/Private/RenderGraphResourcePool.h
  lines: 142
  sha256: 9E5740B734BC69BB8166FC07F6F19BDC4F5D3A363FE2A9A7BD8D2C5E61D3AEC5

Engine/Source/Runtime/RenderCore/Private/RenderGraphResourcePool.cpp
  lines: 415
  sha256: A55F52C346AF955F0DC44A0E8370DCBD73B45BCAA7F096FA3BE27F989B90A75D
```

Two directly referenced texture-pool files were added because
`FRDGBuilder::AllocatePooledTextures` delegates the complete texture fallback to
`GRenderTargetPool`; the planned corpus otherwise exposed only the call site:

```text
Engine/Source/Runtime/RenderCore/Public/RenderTargetPool.h
  lines: 213
  sha256: 1F54F3D974D79FC71514ACFADAA6484A2E553DA8059820568525DEA7849A793B

Engine/Source/Runtime/RenderCore/Private/RenderTargetPool.cpp
  lines: 761
  sha256: F379F2259B4219F29ED3FB35853C1B866C9D794F4D6FDA52A10687B5014FB3D8
```

One U2 file was inspected only at lines 14-67 to interpret the lifetime-fence
predicate used by both whole-resource pools:

```text
Engine/Source/Runtime/RHI/Public/RHITransientResourceAllocator.h
  lines: 559
  sha256: C1CB029794883DCBABDCDBD5E876F2E2FD37EA8C359CFDC7E2B8403F0A8B6E70
```

No transient allocator implementation, heap-range allocator, D3D12 placed
resource path, renderer feature, or general shader-parameter implementation was
studied in U1.

## 57. Unreal Compiles Declarations Once And Executes Work Once

Evidence:

- graph contract: `RenderGraphBuilder.h:45-48`
- immutable pass parameters and deferred work: `RenderGraphBuilder.h:203-221`
- pass creation and setup: `RenderGraphBuilder.inl:248-270`
- setup-queue drain: `RenderGraphBuilder.cpp:1692-1730,1806-1815`
- access enumeration: `2384-2516`
- graph compilation and culling: `1327-1423`
- pass execution: `3511-3525`

Unreal does not call the same pass body in two allocator phases. Authoring code
creates logical resources, fills an immutable pass-parameter object, and gives
RDG a deferred execution lambda. Setup enumerates the declared texture and
buffer accesses without invoking that lambda. Compilation builds dependencies,
culls dead passes, derives resource lifetimes and barriers, and assigns physical
resources. Only then does `Pass->Execute` run, once, between its compiled
prologue and epilogue.

```text
declare immutable pass resources
  -> enumerate declared access
  -> build producer graph
  -> cull
  -> compile surviving lifetimes and barriers
  -> assign physical resources
  -> execute surviving pass body once
```

This directly solves the error-prone RED pattern that motivated the Unreal
comparison. An author does not write:

```cpp
if (context.IsConsumePhase())
{
    // real commands
}
```

inside a function that must also reproduce the exact declaration tape.

Unreal's builder is a per-graph, normally per-frame object. U1 therefore does
not recommend importing RDG's lambda API or rebuilding Vanguard's RED-shaped
graph architecture wholesale. It recommends copying the separation:

```text
PreConsume / Plan
  call node resource planning once

Resolve
  compile and publish immutable resource execution packets

Consume / Execute
  call virtual Execute once using the compiled packet
```

**Vanguard decision:** keep the existing lifecycle names and synchronization
boundaries, but remove full Consume request replay. PreConsume becomes
record-once planning. Consume has a much narrower compiled-step cursor that runs
already planned boundary actions; it does not re-declare resources, re-evaluate
decisions, or rebuild temporal mappings.

## 58. Logical Resources, Views, And State

Evidence:

- generic resource and checked RHI access: `RenderGraphResources.h:133-193`
- allocation-lifetime resource: `292-359`
- lifetime fields: `417-457`
- texture identity and subresources: `595-664`
- buffer identity and state: `1339-1406`
- subresource first/last pairing: `RenderGraphResources.inl:7-46`
- state merge rules: `RenderGraphResources.cpp:69-151`

`FRDGViewableResource` is Unreal's logical allocation and culling unit. Texture
and buffer objects have typed registered handles, one eventual RHI object, graph
first/last-use metadata, reference count, terminal access, and optional transient
acquire/discard positions. Views are separate graph objects but point to a
parent texture or buffer.

Texture state is tracked per mip/array/plane subresource. Each subresource keeps
its current, first, merged, and last-producer state. Buffers use one state and
producer chain in the inspected U1 model. State segments carry access, pipeline,
first/last pass, UAV-hazard identity, transition flags, and barrier location.

This is ordinary CPU-side logical indirection:

```text
LogicalTextureId
  -> compiled physical texture handle
  -> texture views

LogicalBufferId
  -> compiled physical buffer handle
  -> buffer views
```

It is not shader virtual memory, sparse paging, or a shader lookup table. Once a
pass executes, it receives the real RHI resource/view selected by compilation.

**Copy:** logical resource, child-view, and physical-object separation;
compile-time producer/state chains; per-subresource texture state; whole-buffer
state for V1; explicit terminal access; execution-only resolved access.

**Adapt:** use Vanguard typed, generation-checked logical and physical handles.
Do not expose Unreal-style raw pointers or a normal-node equivalent of
`GetRHIUnchecked()`. Freeze every effective descriptor before lifetime and
physical compilation.

**Reject:** `~0u` as a hidden deallocated reference-count sentinel and the
immediate-mode trick that initializes reference count to one. Allocation and
culling state should be explicit enums/records.

## 59. Culling Happens Before Lifetime Finalization

Evidence:

- producer/dependency construction: `RenderGraphBuilder.cpp:1199-1275`
- cull-root traversal: `1278-1323`
- reference counts and culling: `1327-1423`
- per-pass resource setup: `2329-2531`
- surviving-pass collection: `1928-1981`
- texture first/last use: `3648-3712`
- buffer first/last use: `3715-3778`

Resource access declarations first build producer edges. A writable use becomes
the new producer, later uses depend on the relevant producer, and cross-pipeline
hazards add the required graph relationship. Passes begin marked as culled.
External/extracted outputs, explicit external outputs, and `NeverCull` passes
seed a backwards walk through producer edges.

Compilation then subtracts every culled pass's resource references. Physical
collection visits only surviving passes. The first surviving use assigns
`FirstPass`; pass-end reference decrements update `LastPasses`; reaching zero
emits the deallocation operation at that surviving pass.

```text
candidate pass P writes temporary T
candidate pass Q reads T but has no surviving output

P and Q culled
  -> their references are removed
  -> T gets no physical lifetime
  -> no allocation is performed
```

Extraction contributes one additional reference which is released at the graph
epilogue, extending that resource through publication. Unused external resources
are released at the prologue instead of being treated as live for the whole
graph (`RenderGraphBuilder.cpp:1940-1960`).

At one pass boundary Unreal processes:

```text
pass prologue: collect every allocation / begin
pass epilogue: collect every deallocation / end
```

The call order is explicit at `RenderGraphBuilder.cpp:1962-1970`. A resource
that ends in pass `P` therefore cannot supply storage to another resource that
begins in the same pass. Render-pass merging moves begin/end sets to the merged
outer boundaries and rejects allocation inside a merged region
(`1439-1489,3652-3659,3719-3726`).

**Vanguard decision:** planning may record candidate uses in parallel, but graph
culling must finish before the allocator derives final first/last positions or
performs physical assignment. The stable time key remains
`(GpuFlowGroupId, local step ordinal)`, not Unreal insertion order and not CPU
worker completion order. At equal positions, all before-actions run before
commands and all after-actions run only after the relevant command recording.

## 60. Imports, Extractions, And External Access Are Different Contracts

Evidence:

- external texture registration: `RenderGraphBuilder.cpp:1081-1119`
- external buffer registration: `1122-1155`
- conversion to external: `723-752`
- extraction requests: `RenderGraphBuilder.inl:441-499`
- extraction lifetime reference: `RenderGraphBuilder.cpp:1373-1381`
- output publication: `2194-2212`
- external-access bypass: `898-1076,2408-2414,2468-2474`
- resource flags and cull-root rule: `RenderGraphResources.h:310-359`

### Import

Registering an external resource starts with an existing pooled physical object
owned outside the graph. Unreal deduplicates by underlying RHI identity, derives
the RDG descriptor from that physical object, retains it, and marks the logical
resource external.

The physical object is descriptor authority. A caller-provided expectation may
be validated, but it must not overwrite the actual format, size, usage, sample
count, or capacity. Repeated registration returns the first logical object;
Vanguard must additionally reject a repeated registration with conflicting
descriptor expectations, ownership mode, or final-access contract.

### Extraction

Extraction starts from a graph logical resource. The request clears the caller's
output, marks the resource extracted, records an output destination, and makes
the final producer a culling root. Compilation retains the resource through the
epilogue. Unreal publishes the pooled reference only after all pass execution
and parallel execution work have completed.

```text
Import:
  retained physical object exists before graph execution

Extraction / export:
  graph-owned logical object becomes a retained external handle only after
  successful terminal command recording and final-state compilation
```

Publication does not imply GPU idle. The returned RHI handle and existing RHI
retirement machinery keep the object alive; a CPU readback still needs its own
completion synchronization.

### External access mode

Unreal's external-access mode is separate from external ownership. It temporarily
stops ordinary graph culling and transition tracking for a read-only resource
whose state is managed through an interop path. `ConvertToExternal*` is another
escape hatch: it eagerly allocates a pooled object and turns an internal logical
resource into an external culling root.

**Vanguard decision:** expose typed semantic records, not raw output pointers or
overloaded booleans:

```text
ImportResource
  retained physical handle
  physical-authoritative descriptor
  initial access / queue ownership
  required final access

ExportResource
  logical allocation version
  typed destination slot
  required final access
  publish-after-success policy
```

Keep manual/untracked external access out of allocator V1. If later required for
interop, it must be an engine-internal scoped operation with explicit transitions
and validation. Do not use eager `ConvertToExternal` as the normal ownership
path.

## 61. Whole-Resource Pooling Is The Safe Physical Fallback

Evidence:

- ordered pooled texture operations: `RenderGraphBuilder.cpp:3128-3177`
- ordered pooled buffer operations: `3179-3215`
- texture previous-owner chain: `4723-4745`
- buffer previous-owner chain: `4771-4788`
- previous-state continuity: `4053-4071,4203-4207`
- texture pool lookup: `RenderTargetPool.cpp:185-270`
- buffer pool lookup: `RenderGraphResourcePool.cpp:32-166`
- lifetime-fence meaning: `RHITransientResourceAllocator.h:14-67`

Unreal's pooled fallback assigns the same complete RHI resource object to
sequential logical resources:

```text
Logical A ----\
               same FRHITexture / FRHIBuffer object
Logical B ----/
```

This is not true heap aliasing. It does not create two placed objects over one
memory range, does not change resource identity, and does not require an alias
barrier. RDG links logical owners and carries the previous owner's last resource
state into the next owner's first transition.

Pool availability has two conditions:

1. the pool is the only remaining CPU owner;
2. the compiled old deallocation and new allocation fence records do not describe
   simultaneous graphics/async-compute use.

The exact transient-heap fence and alias-barrier machinery is U2, but the U1
whole-object pool already proves that refcount-only availability is insufficient
for reuse within one compiled graph.

### Texture compatibility

The texture pool canonicalizes the request by removing obsolete FastVRAM flags
and forcing shader-resource capability. It hashes that canonical descriptor and
then performs unconditional full descriptor equality. Extent, format, usage,
mips, layers, samples, and clear behavior therefore remain exact. A larger
texture is not silently treated as a smaller attachment.

### Buffer compatibility

Buffers may be capacity-bucketed. Page mode rounds byte capacity to 64 KiB;
power-of-two mode rounds to a power of two and then applies page alignment. The
physical `NumAllocatedElements` remains the capacity while the logical request
may expose a smaller element count.

This is useful, but Unreal stores the current logical count by `const_cast` into
a `const` pooled descriptor (`RenderGraphResourcePool.cpp:130-133,162-164`;
`RenderGraphResources.h:1214`). Vanguard must instead store:

```text
immutable physical capacity / canonical creation descriptor
current logical active descriptor
```

as separate fields.

### Vanguard fallback rule

Before true placed aliasing is enabled on a backend, Vanguard can still obtain
safe frame-local reuse:

```text
exact canonical texture match
or compatible buffer capacity class
  + pool-only ownership
  + non-overlapping compiled lifetime/queue fences
    -> reuse the same whole RHI object
```

Every lookup uses a hash bucket only as an accelerator. Canonical descriptor
equality remains authoritative in release builds. Allocation, prior-owner state,
queue-fence compatibility, and the final strong owner are all stored in the
compiled physical plan. Reused contents are undefined for a new logical owner.
Load/preserve is valid only when the current logical allocation already has
defined contents through an import or prior write. Sharing the same RHI object
never transfers content provenance from the previous logical owner.
Clear/discard/load policy and provenance remain part of the declared use.

## 62. Pool Cache Policy, Budgets, And Diagnostics

Evidence:

- buffer statistics and dump: `RenderGraphResourcePool.cpp:11-84`
- buffer aging and release: `212-250`
- texture allocation counters: `RenderTargetPool.cpp:118-133`
- texture trim policy: `360-475`
- texture diagnostics: `575-648`
- texture use-age updates: `689-708`
- texture RHI teardown: `650-655`

Unreal's two whole-resource pools have different retention policies. Buffers are
released when the pool is their only owner and they have not been requested for
more than 30 frames. There is no byte target or allocation-admission limit.
Textures track consecutive unused frames and use `r.RenderTargetPoolMin` as a
post-frame soft trimming threshold. Eligible old entries are evicted in oldest-
first order until retained memory approaches that threshold, but a new request
may exceed it and trimming may be unable to recover memory while resources are
live.

These are cache policies, not correctness rules and not hard budgets. They must
remain separate from lifetime compatibility:

```text
May this object be reused now?
  -> ownership + compiled queue/lifetime fences + descriptor compatibility

Should this free object remain cached?
  -> age + pressure + soft target + class policy

May a new physical allocation be admitted?
  -> checked size + hard limit + eviction attempt + structured failure
```

Vanguard should use one allocator-owned whole-resource pool framework with
texture and buffer policy objects. Every entry stores an immutable canonical
creation descriptor, physical capacity, byte cost, cached views, last-use age,
retirement state, and keyed free-list membership. Texture policy requires exact
canonical shape equality. Buffer policy may accept an equal-or-larger capacity
inside an explicit size class while preserving the active logical descriptor as
separate data.

The cache policy needs:

- a configurable soft retained-byte target;
- an optional hard admitted-byte limit per memory class;
- total and per-class current, free, assigned, and fence-retired bytes;
- current and historical high-water marks;
- create, reuse, miss, release, eviction, and admission-failure counts;
- pressure-driven eviction of free, retirement-safe entries only;
- age as a ranking/tie-breaking policy rather than the sole safety rule;
- structured dumps containing canonical descriptor, capacity, logical owner,
  age, queue/lifetime fences, and why an entry is not reusable.

RHI destruction remains deferred through the existing backend/device lifetime
machinery. Removing an entry from this cache does not mean that native memory is
immediately safe to destroy.

Do not copy these Unreal defects:

- buffer compatibility is hash-authoritative in non-check builds because full
  equality is only a `check` (`RenderGraphResourcePool.cpp:86-108`);
- buffer hashing includes metadata that equality ignores
  (`RenderGraphResources.h:1085-1100`);
- active logical size is written through `const_cast` into a const descriptor;
- byte multiplication and alignment are not consistently checked;
- both pools can grow until the RHI allocation itself fails;
- lookup is a linear first-match scan;
- `OnFrameStart()`'s ten-frame result is ignored while trimming uses a separate
  two-frame threshold (`RenderTargetPool.cpp:388-392,689-708`);
- texture `ReleaseRHI()` clears native references without clearing parallel
  hashes/accounting, relying on an implicit terminal-teardown contract.

**Vanguard decision:** hash is only a lookup accelerator. Release builds always
perform canonical descriptor and capacity validation. All byte arithmetic is
checked at 64 bits. Allocation either publishes a complete physical assignment
or returns a typed capacity/RHI failure; no partially compiled plan becomes
visible to Consume.

## 63. Authoritative Vanguard U1 Execution Contract

This section supersedes the full request-tape replay portion of Section 51. It
does not discard RED's useful lifecycle, temporal identity, flow-group ordering,
queue scopes, or process wrapper. It changes how node-authored declarations are
carried from planning into execution.

```text
PreConsume
  each node declares/plans resource operations once
  each worker owns its local candidate-plan storage
  workers join

Graph compile
  build producer dependencies
  choose culling roots and remove dead work
  establish a frame-local surviving-node/dependency overlay and deterministic
  order without mutating cached graph topology

Resolve
  compile logical allocation versions and temporal tag mappings
  derive final first/last-use intervals from surviving work
  assign/import whole physical objects (and later placed ranges)
  compile ordered per-use bindings and boundary actions
  validate the complete scratch plan
  join resolver work and atomically publish one execution generation

Consume
  process_node installs the node's compiled execution packet
  the virtual Execute body runs once
  a narrow step cursor exposes declared resolved bindings and performs the
  compiled before/after actions at their required command-recording points

Cleanup
  prove every required step/action completed exactly once
  publish exports only after successful terminal execution
  return or retire physical resources and advance cache policy
```

The allocator-facing shape is approximately:

```text
CompiledExecutionPacket
  execution_generation
  node_id
  gpu_flow_group
  ordered_steps[]
  captured_decisions[]
  validated_import_reference_indices[]

CompiledExecutionStep
  step_id
  queue and command-scope identity
  kind:
    ResourceUse(resource_use_id, logical_allocation_version,
                resolved texture/buffer/view handle)
    QueueBegin / QueueEnd
    QueueSync / Submit
    OwnershipOrExternalAction
    ExportFinalStateAction
  before_actions[]
  after_actions[]
```

The execution generation, not each node packet, owns the strong imported-resource
references. Packets hold validated indices/references into that immutable session
table so imports are retained once and cannot acquire inconsistent node-local
ownership.

### Resolved bindings are per use, not merely per node

A node-level bag of `tag -> physical handle` is incorrect because a logical tag
can change value during one node or command-list group:

```text
step 0: read A       -> physical X
step 1: swap A, B
step 2: write A      -> physical Y
```

The compiled bindings for step 0 and step 2 must therefore be different even
though both were authored with logical tag `A`. A `ResourceUseId` identifies a
specific declared use at a stable local ordinal. Resolve binds each use to the
logical allocation version active at that point and then to its physical object
or view.

The execution cursor is deliberately narrow. It advances through every compiled
step in total order. A resource-use step returns that use's typed resolved handle;
executor-owned control steps perform queue begin/end, sync/submit, ownership, or
export-final-state behavior at the compiled location. Actual export ownership is
still published only by the successful terminal graph epilogue. Captured
decisions need no runtime step. The cursor may not declare resources, alter
decisions, perform arbitrary
`get_resolved(tag, current_time)`, or reconstruct the planning stream. It rejects
the wrong node, generation, queue, command scope, step order, resource kind, or
double completion.

This small cursor is not RED's Consume replay. It is comparable to executing a
compiled command/resource script: declarations and temporal mapping are already
finished, while action placement still has to interleave with the real commands
recorded by the node.

### Virtual execution remains straightforward

`RenderNodeImpl::Execute` can remain virtual with its existing runtime-oriented
signature. `process_node` remains the nonvirtual common wrapper and installs the
packet/cursor into `RenderNodeExecutionContext` before calling `Execute` once.
The exact ergonomic API belongs to the Render Graph design; valid options
include typed use-slot access through the context or an implementation-owned
static resource schema. No templated virtual function is required.

A node or command-list subnode that launches child jobs must retain the immutable
packet generation until all child recording is complete. Its after-actions run
only after the commands that use the resource have been recorded, never merely
because the parent `Execute` returned.

### Static graph cache versus per-frame compilation

The graph cache may retain topology, node implementation ownership, static
resource-schema/use-slot identifiers, and graph-shape variants. Each frame still
instantiates a fresh resource plan containing current descriptors, decisions,
imports, physical handles, logical allocation versions, and action schedules.
Planning hooks are pure/reentrant with respect to cached node and graph objects;
parallel workers write only frame-local candidate-plan storage. Culling produces
a frame-local surviving overlay and never deletes or rewires cached topology.

If a decision changes topology or the static resource schema, it selects a
different graph-cache key. A frame-dependent resource decision is evaluated once
during planning, captured in the packet, and read by `Execute`; Consume must not
reevaluate the source condition.

Candidate planning may run in parallel, but merge order is always:

```text
(GpuFlowGroupId, node-local operation / compiled-step ordinal)
```

CPU worker completion order is never allocator time. Culling precedes lifetime
finalization. Cross-node begin/end scopes require a stable compiled scope id;
culling either preserves both required endpoints through dependencies or rejects
the resulting broken scope.

### RED terms replaced by the U1 contract

```text
RED-derived checkpoint              Authoritative U1 contract
----------------------              -------------------------
per-group request tape              immutable candidate plan / use steps
full request replay                 narrow compiled execution-step cursor
replayed decision request           once-captured decision value
get(tag, request time)              typed resolved ResourceUseId
matching request counts             step/action completeness validation
```

## 64. Validation And Transactional Failure Contract

Unreal validates pass declaration, extraction, external registration, merged
access, external-access mode, graph begin/end, pass execution, and barriers. It
also prevents normal checked RHI access outside execution
(`RenderGraphResources.h:133-193`). Those boundaries are valuable.

The inspected public APIs `FRDGBuilder::Compile` and `Execute` are `void`
(`RenderGraphBuilder.h:411-412,516`) and primarily use checks/fatal validation.
There is no recoverable transactional compilation contract. Vanguard should not
copy that failure posture for allocator construction, where capacity,
unsupported descriptors, and native allocation failures are plausible runtime
events.

Resolve builds into scratch ownership. Publication is one atomic generation
change only after every logical timeline, import, physical assignment, action,
and retained reference validates. Failure destroys the scratch plan, releases
temporary strong references through normal RHI ownership, leaves export slots
empty, and leaves the previous/invalid execution generation inaccessible. A
partially resolved packet is never executable.

Release-build validation must reject at least:

- descriptor conflicts for one logical declaration or duplicate import;
- forged, stale, wrong-kind, or wrong-generation handles;
- use-before-declaration, unbalanced use/scope endpoints, and illegal swaps;
- culling that leaves a required cross-node scope endpoint missing;
- overflow or alignment failure during byte/capacity computation;
- hash collisions whose canonical descriptors differ;
- reuse while another strong owner exists or queue/lifetime fences overlap;
- use of an execution packet by the wrong node, frame, queue, or command scope;
- out-of-order, skipped, duplicated, or wrong-kind execution steps;
- export publication before terminal execution succeeds;
- cleanup while child jobs or packet generations remain live.

Required focused tests before physical allocator rollout include:

1. the planning hook runs once while virtual execution runs once;
2. culled work creates no lifetime or physical allocation;
3. one logical tag resolves to different physical handles across a swap;
4. decisions are evaluated once and observed identically by execution;
5. imported physical descriptors are authoritative and conflicting reimports
   fail;
6. exports stay empty on resolve or execution failure;
7. all begins precede all ends at one execution boundary;
8. hash collisions cannot bypass full compatibility checks;
9. soft trimming never evicts assigned/retiring entries and hard admission
   returns a structured error;
10. stale generation, queue mismatch, skipped step, and duplicate completion are
    rejected;
11. failed Resolve publishes neither packets nor partial pool ownership;
12. packet lifetime covers every child recording job and its after-actions.

## 65. Unreal U1 Decision Ledger

### Copy

- Separate resource declaration/planning from real command execution.
- Cull dead work before final first/last-use and physical allocation.
- Treat logical resources as lifetime/culling units and views as typed children.
- Track texture state per subresource and buffers as a whole for V1.
- Use imports and exports as explicit ownership contracts and culling roots.
- Publish extracted resources only after successful terminal execution.
- Reuse the same complete RHI object when canonical compatibility, ownership,
  and queue-aware lifetimes permit it.
- Carry the previous logical owner's final state into the next owner of a reused
  whole object.
- Begin/acquire all uses before command work and end/retire them afterwards;
  conservatively forbid same-boundary end-to-begin reuse.
- Keep descriptor hashes as accelerators and full equality as authority.

### Adapt

- Keep Vanguard's `PreConsume -> Resolve -> Consume -> Cleanup` lifecycle and
  virtual node/process-wrapper architecture, but compile declarations into
  per-use packets instead of replaying the declaration body.
- Use stable GPU flow-group/use ordinals instead of Unreal pass insertion order.
- Use typed generation-checked ids instead of raw pointers and unchecked access.
- Make external physical descriptors authoritative while reporting conflicting
  caller expectations as structured errors.
- Use a generic allocator-owned whole-resource pool with texture/buffer policies,
  immutable physical capacity, explicit logical active shape, budgets, pressure
  handling, and diagnostics.
- Keep static use-slot schemas cacheable but compile current decisions, imports,
  temporal mappings, physical handles, and actions per frame.
- Return structured Resolve/admission failures and publish plans atomically.

### Reject

- Running the same implementation body in both PreConsume and Consume.
- A node-level unordered binding bag that loses temporal use position.
- Arbitrary resolved lookup by logical tag during execution.
- Immediate `ConvertToExternal` and broad untracked external-access mode as
  normal V1 paths.
- Hash-only compatibility, `const_cast` descriptor mutation, unchecked size
  arithmetic, inconsistent magic ages, and linear first-match lookup as the
  final pool design.
- Global singleton resource pools as the allocator ownership model.
- Fatal-only compilation and partial/nontransactional plan publication.
- Presenting whole-object reuse as placed-resource heap aliasing.

### Vanguard-specific

- `ResourceUseId` and execution generation are explicit validation identities.
- Per-use compiled packets preserve RED temporal swaps without Consume replay.
- Graph CPU dependencies control job readiness; finalized GPU flow order and
  queue scopes control allocator time and physical-action placement.
- The existing Vanguard RHI deferred-destruction path remains distinct from
  logical lifetime end, pool eviction, and future alias-range retirement.
- Hard budget/admission behavior is explicit rather than inferred from cache
  trimming.

## 66. Questions Passed To Unreal U2

U2 is deliberately restricted to the questions U1 cannot answer:

1. How do Unreal's transient allocators turn logical intervals into heap ranges
   and placed resources?
2. What exact overlap record produces acquire/discard and alias barriers?
3. How are graphics and async-compute fences encoded for a shared heap range?
4. Which texture/buffer descriptor and heap properties define alias
   compatibility on D3D12?
5. What backend capability test selects transient placement versus the U1
   whole-object fallback?
6. How are transient heap pages cached, trimmed, and retired across frames?
7. What validation proves that no live placed object still owns an overlapping
   region?
8. How are native allocation failures handled, and what must Vanguard add to
   keep Resolve transactional?
9. Does current Vanguard/NVRHI already expose each required placed-resource,
   alias-barrier, queue-fence, and retirement primitive?

U2 must not reopen U1's declaration/execution split or expand into general RDG,
renderer passes, virtual texturing, streaming residency, or GPU memory residency.

## 67. Unreal U1 Exit Gate

U1 is complete. The study can now explain:

- how Unreal derives logical dependencies, culls work, and computes surviving
  lifetimes before allocating resources;
- how imports, exports, views, subresources, and whole-resource reuse work;
- why pooled fallback is not placed aliasing and needs no shader lookup;
- which Unreal cache and validation defects Vanguard must not copy;
- how Vanguard keeps RED's lifecycle and virtual process wrapper while executing
  every node once through ordered per-use compiled packets.

No production code was changed at this U1 checkpoint. U2 is now also complete
in Sections 68 through 80.

## 68. Unreal U2 Source Snapshot

U2 was inspected against Unreal Engine 5.8.1, compatible changelist 55116800,
branch `UE5`, on 2026-08-30. The primary corpus was pinned by line count and
SHA-256 so later engine updates cannot silently change the evidence:

| File under `Engine/Source/Runtime` | Lines | SHA-256 |
| --- | ---: | --- |
| `RHI/Public/RHITransientResourceAllocator.h` | 559 | `C1CB029794883DCBABDCDBD5E876F2E2FD37EA8C359CFDC7E2B8403F0A8B6E70` |
| `RHI/Private/RHITransientResourceAllocator.cpp` | 59 | `E7141E8F29535B114297308741866ADB16540B5EEA0A527F1ECD78F8060B911E` |
| `RHI/Public/RHIValidationTransientResourceAllocator.h` | 49 | `A73F6F1EF534E7370EE149E40329756D1C4A9CB38891C04D366C8B98A5CAB948` |
| `RHI/Public/RHITransition.h` | 546 | `39432C101ECB694C33EB79CC9641EE050DE12E5BE92EBA0F8630B87B614FA16F` |
| `RHI/Private/RHITransition.cpp` | 51 | `0E8457729E763961A045A21CFF281473EDA61005A917AB2CEC537B5AC795FB10` |
| `RHICore/Public/RHICoreTransientResourceAllocator.h` | 1167 | `A075983B409247B2394E772090DE61DC7459DDCB0EC352F428F371A47278A6EE` |
| `RHICore/Private/RHICoreTransientResourceAllocator.cpp` | 1626 | `C825998C157E382315A2167731713E6BE76C570F1C303F2A06E70FFFB00988B8` |
| `D3D12RHI/Private/D3D12TransientResourceAllocator.h` | 91 | `2433B522DDAF0DA79CAC297DF005293CC7AB7F8D05282DB4579A141F15E294ED` |
| `D3D12RHI/Private/D3D12TransientResourceAllocator.cpp` | 242 | `6D711458C61D4384DDFAE68A39F65117B48666EB15AAEDE724D0E0AB533BDF78` |

Three narrow support ranges were inspected because the primary corpus delegates
the exact behavior to them:

- `RHI/Private/RHIValidation.cpp:3578-3702` for the validation wrapper;
- `D3D12RHI/Private/D3D12LegacyBarriers.cpp:421-443,887-910,1283-1400`
  for alias/discard lowering;
- `D3D12RHI/Private/D3D12EnhancedBarriers.cpp:2606-2665,2970-3280`
  for enhanced-barrier alias initialization ordering.

The Vanguard cross-check remained bounded to the existing RHI surface,
`CommonBackend`, D3D12 callbacks/tests, resource lifetime tracking, and the
corresponding pinned NVRHI placement implementation and validation wrapper. No
renderer feature or production code was inspected or changed.

## 69. Three Physical Strategies, With Precise Names

U2 confirms that three distinct strategies must not be called by the same
ambiguous word:

| Strategy | Physical behavior | Shader lookup | Vanguard status |
| --- | --- | --- | --- |
| Whole-resource reuse | A complete compatible RHI object is reused after its logical lifetime ends. | None | Required U1 fallback |
| Placed-heap aliasing | Distinct RHI objects occupy overlapping byte ranges of one native heap at non-overlapping times. | None | Optional U2 provider |
| Reserved-page remapping | A stable GPU virtual address is mapped to changing physical page spans by the backend. | None | Deferred |

Neither Unreal strategy is virtual texturing. There is no shader page-table
lookup. Unreal's page allocator is GPU virtual-memory management performed by
the RHI and driver.

The initial Vanguard implementation is therefore locked to:

```text
WholeResourceReuse       always available
PlacedHeapAliasing       capability-selected
ReservedPageRemapping    deferred until a measured backend/workload need exists
```

## 70. Logical Allocation Fences Are Not Hardware Fences

`FRHITransientAllocationFences` records graph timeline positions, not submitted
GPU fence values (`RHITransientResourceAllocator.h:14-118`). An acquire belongs
to one pipeline. A discard may cover graphics, async compute, or both. Async
work also records the graphics fork and join positions that contain it.

Unreal permits a freed physical range to be reused only when the previous
discard region and next acquire region cannot execute simultaneously. It then
computes the earliest legal acquire point for the queue direction:

```text
graphics -> graphics  previous graphics discard
graphics/all -> async graphics fork point
async -> async        previous async discard
async/all -> graphics graphics join point
```

The freed range retains its previous logical owner and these logical fences
until alias actions have been compiled. The later resource's acquire bound and
the previous resource's discard bound are adjusted to surround the handoff.

Vanguard must keep two different clocks:

```text
CompiledSchedulePoint
  proves that logical lifetimes may share a range and places queue actions

ResidencyFenceSet / submission completion
  proves that cached native objects and heaps may actually be destroyed
```

CPU worker completion, graph schedule position, and hardware completion are not
interchangeable.

V1 implementation rollout is conservative:

- same-queue alias handoffs are enabled first;
- graphics/compute handoffs are enabled only when the compiled schedule carries
  the explicit fork/join dependency and conformance tests prove the lowering;
- copy-queue alias reuse is prohibited until Vanguard has an explicit copy
  dependency/wait contract;
- touching logical boundaries are treated conservatively as overlapping unless
  an ordered end action is guaranteed before the acquire action.

## 71. Contiguous Heap Allocation And Placed Objects

Unreal's D3D12 path uses the generic `FRHITransientHeapAllocator`, a first-fit
allocator over a sorted list of byte ranges
(`RHICoreTransientResourceAllocator.cpp:132-295`). Each free range stores:

```text
offset + size
previous resource
previous discard fences
```

Allocation aligns against the heap GPU address, walks physically consecutive
ranges, rejects any prior lifetime that overlaps the new acquire interval, and
can consume adjacent ranges left by several prior owners. Every crossed prior
owner becomes alias provenance. Deallocation restores the original allocation,
including alignment padding, and records the retiring owner/fences.

The backend supplies native allocation size and alignment. D3D12 then creates a
distinct placed texture or buffer at the chosen 64-bit heap offset and starts it
in `ERHIAccess::Discard`
(`D3D12TransientResourceAllocator.cpp:137-240`). This is true memory aliasing:

```text
ID3D12Heap range [N, N + S)
  -> ID3D12Resource A during A's compiled lifetime
  -> ID3D12Resource B during B's later compiled lifetime
```

Heap compatibility and resource-object compatibility are different:

- heap compatibility decides whether a resource kind may occupy the native
  heap category and whether size/alignment fit;
- placed-object reuse requires exact descriptor equality plus the same heap and
  offset, because the native placed object is permanently associated with that
  placement.

D3D12 resource heap tier 2 can mix buffers and texture categories. Tier 1 needs
separate buffer, non-RT/DS texture, and RT/DS texture heap classes. Vanguard
must expose this truth through capabilities/compatibility data instead of
assuming one universal device-local heap.

The Vanguard range allocator must use checked `u64` arithmetic for every align,
addition, end calculation, capacity comparison, and heap-growth calculation.
Range ids must be typed/generational rather than Unreal's unchecked `uint16`.

## 72. Why Reserved-Page Remapping Is Deferred

Unreal's alternative allocator keeps a reserved RHI resource at a stable GPU
virtual address and assigns discontiguous physical page spans from one or more
page pools (`RHICoreTransientResourceAllocator.h:761-1157` and
`.cpp:929-1626`). Page-map requests are accumulated and submitted at `Flush`.
If a cached reserved object receives the same mapping, Unreal skips the remap.

This is useful when a backend has efficient reserved-resource mapping, when
large resources benefit from page-level physical commitment, or when fast and
ordinary memory pools must be mixed. It also adds:

- page-span packing and mapping batches;
- a second physical pool/cache hierarchy;
- backend-specific map synchronization;
- compact page-index limits and more failure/rollback cases.

Current Vanguard/NVRHI exposes deferred binding of a whole placed object to one
heap offset. It does not expose the reserved-resource page-remap provider needed
to copy this strategy honestly. U2 therefore rejects page remapping for V1. It
may be introduced later as a separate provider without changing logical resource
identity or the per-use packet contract.

## 73. Semantic Alias Actions And D3D12 Lowering

The allocator must compile a backend-neutral handoff, not emit D3D12 calls
itself:

```text
PlacedAliasActivation {
    after,
    previous_owners,
    acquire_step,
    discard_steps,
    source_queue,
    destination_queue,
    target_state,
}
```

Unreal's legacy D3D12 path lowers acquire to one conservative alias barrier with
`pResourceBefore = nullptr` and the new resource as `pResourceAfter`
(`D3D12LegacyBarriers.cpp:887-896,1346-1388`). Exact predecessor lists are
retained only in RHI validation. Discard is handled separately, including a
graphics-queue workaround for resource types that cannot be discarded safely on
async compute.

The enhanced path represents first activation as an undefined/no-access to
target-state transition. It detects overlapping GPU virtual-address ranges and
separates the old discard from the new initialization because enhanced barrier
rules forbid putting both operations into the same barrier group
(`D3D12EnhancedBarriers.cpp:2639-2664,2972-3045,3261-3277`).

Vanguard's current D3D12 callback already emits an explicit D3D12 alias barrier
and can discard the new owner (`source/rhi/nvrhi/src/d3d12_backend.cpp:1164-1181`).
The compiled allocator action should call the existing RHI abstraction. A
future enhanced-barrier backend may lower the same semantic action differently.

Exact predecessor identity remains in Vanguard release plans even when one
backend uses null-before. It is required for validation, diagnostics, non-D3D12
backends, and proving that every overlapping old owner has ended.

Current Vanguard alias APIs are same-kind (`texture -> texture` and
`buffer -> buffer`). V1 therefore allows precise alias chains only within the
same resource kind. Cross-kind reuse requires a deliberately supported broad
backend barrier and a truthful heap-class capability; it is not inferred.

## 74. Object Caches, Heap Caches, Budgets, And Retirement

Unreal has two independent cache levels:

1. each heap caches discarded placed texture/buffer objects by descriptor hash
   seeded with heap offset;
2. a persistent cache retains empty native heaps for later allocator cycles.

This is worth preserving, but Unreal's policy is not sufficient. Its object
cache is hash-only, its capacity is soft and can remain exceeded while entries
are young, and heap GC is controlled by CPU allocation cycles. There is no hard
admission budget or recoverable allocation failure.

Vanguard's authoritative policy is:

- use hashes only to find candidates; exact canonical descriptor, heap id,
  offset, native requirements, and compatibility class decide equality;
- distinguish requested bytes, aliased peak, heap capacity, resident/committed
  bytes, cached bytes, and pending-retirement bytes;
- check soft pressure before growing and a hard budget before native creation;
- trim reusable object entries, then empty heaps, then use U1 whole-resource
  fallback or return a structured error according to policy;
- use CPU age/LRU only to select cache victims;
- use the existing queue-completion lifetime system to prove native object/heap
  destruction safety;
- never treat logical lifetime end or allocator-cycle age as GPU completion.

Heap size is policy, not a copied Unreal constant. Vanguard should start from a
configurable per-class minimum, round growth with checked arithmetic, and admit
the heap against budget. Unreal's fixed 128 MiB minimum is evidence, not a
locked Vanguard value.

## 75. Current Vanguard/NVRHI Capability Audit

Vanguard is not missing the low-level placed-resource architecture. The current
path already supports:

```text
CreateTexture/CreateBuffer(virtualResource = true)
  -> GetMemoryRequirements
  -> CreateHeap
  -> BindMemory(resource, heap, offset)
  -> BarrierTextureAliasing / BarrierBufferAliasing
  -> DiscardTexture / DiscardBuffer
  -> MakeStateSafeToRetire
```

It also already has graphics/compute/copy submission fences, command-list
resource retention, heap retention by placed objects, and fence-safe deferred
destruction. The D3D12 conformance test demonstrates two deferred-binding
buffers placed at offset zero in one heap and activated in sequence
(`d3d12_backend_tests.cpp:511-524,900-901`).

The missing or incomplete parts are narrower:

| Existing area | What U2 found |
| --- | --- |
| Capabilities | `transientHeaps` and `resourceAliasing` exist, but both mirror one coarse NVRHI virtual-resource feature. |
| Heap description | Vanguard exposes alignment and compatibility class, but `CreateHeap` forwards neither. |
| Memory requirements | Size/alignment are native; compatibility class is always zero. |
| Binding | One-time binding and retain rollback exist, but release validation lacks virtual-kind, checked bounds, alignment, memory type/class, and a stored placement record. |
| Alias barrier | The native primitive exists, but the façade cannot prove the resources are bound, share a heap, or overlap because payloads retain only the heap. |
| Queue handoff | Graphics/compute fork/join exists; copy has no equivalent alias dependency path. |
| Failure | Native failure is reported, but placement failures are not classified precisely enough for allocator policy. |

Pinned NVRHI's optional validation wrapper checks virtual status, capacity, and
offset alignment (`validation-device.cpp:334-387,510-563`). Vanguard constructs
the direct NVRHI device rather than that wrapper, so those checks are not active
on Vanguard's public RHI path. Even upstream uses overflow-prone
`offset + size > capacity` and does not solve compatibility classes.

Pinned NVRHI also exposes a Tier-1 hazard: its D3D12 heap path creates Tier-1
heaps as RT/DS-only while Vanguard reports the same general alias capability.
Truthful heap classes/capabilities must be fixed before placed aliasing is
enabled on such devices.

U2 therefore does **not** add another RHI API family or another resource
lifetime manager. It hardens the existing surface and builds allocator policy
above it.

## 76. Authoritative Vanguard U2 Physical Plan

After U1 culling and lifetime finalization, Resolve performs placed allocation
entirely in scratch state:

```text
1. Select WholeResourceReuse or PlacedHeapAliasing from truthful capabilities.
2. Canonicalize each surviving physical requirement and heap class.
3. Walk resources in deterministic first-acquire order with a stable tie-break.
4. Query checked native size/alignment once per canonical resource requirement.
5. Find a compatible free range whose prior logical fences do not overlap.
6. Otherwise try another admitted heap; grow only after pressure and budget checks.
7. Record range, prior owners, discard points, acquire point, and queue handoff.
8. Reuse an exact placed object or create a deferred-binding RHI object.
9. Bind once to the selected heap/offset; retain placement metadata in the plan.
10. Join every asynchronous creation operation.
11. Compile semantic alias/state/discard actions into per-use packets.
12. Validate the complete plan, queue dependencies, budgets, and ownership ledger.
13. Atomically publish the plan; only then may Consume execute once.
```

One-time binding needs no `UnbindMemory`. If a scratch build fails, all
unpublished placed-resource and heap handles are released through the existing
fence-aware lifetime system. A retry creates fresh deferred-binding objects.
No logical mapping, per-use packet, pool ownership, or cache insertion is
published partially.

The minimum records are:

```cpp
struct PlacedHeapClass {
    MemoryType memory_type;
    ResourceKindMask allowed_kinds;
    uint64_t native_class;
};

struct PlacedAllocation {
    HeapRef heap;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
    PlacedHeapClass heap_class;
    PlacementGeneration generation;
};

struct AliasHandoff {
    PlacedAllocationId allocation;
    SmallVector<PhysicalResourceId> previous_owners;
    CompiledSchedulePoint acquire;
    SmallVector<CompiledSchedulePoint> discards;
    QueueType destination_queue;
};
```

The exact C++ names remain a synthesis/implementation-plan decision. The
semantics above are locked.

## 77. Validation And Transactional Failure Contract

Release validation must prove:

1. every placed resource is deferred-binding, unbound before placement, and
   supported by the active capability;
2. `size > 0`, alignment is valid, offset is aligned, and
   `size <= heap_capacity - offset` uses checked arithmetic;
3. resource requirements and heap memory/class/category are compatible;
4. a placed object is bound exactly once and its immutable placement record
   matches the allocator ledger;
5. every simultaneous logical lifetime has disjoint physical ranges;
6. every overlapping range handoff has ended every prior owner before acquire;
7. the destination queue is dependency-ordered after all source queues;
8. copy-queue or unsupported cross-kind handoffs are rejected;
9. cache hits pass exact descriptor/heap/offset equality after hash lookup;
10. all creation jobs finish before publication;
11. failed Resolve leaves no published plan, cache ownership, or logical mapping;
12. heap destruction waits for all recorded graphics/compute/copy submissions.

Required focused tests include:

- exact-fit, aligned split, adjacent-range combination, fragmentation, and
  deterministic first-fit cases;
- same-queue reuse and forbidden overlapping lifetimes;
- graphics/compute fork-join cases in both directions;
- copy-queue rejection until supported;
- stale ids/generations, overflow, misalignment, out-of-range binding, wrong
  heap class, double bind, and non-deferred resource binding;
- exact cache equality despite deliberate hash collision;
- alias barrier for an unbound, different-heap, non-overlapping, or wrong-kind
  pair;
- native heap/resource failure at every transaction step and complete rollback;
- budget pressure, fallback to whole-resource reuse, cache trim, and fence-safe
  heap retirement;
- one real D3D12 validation-layer test that writes owner A, activates owner B at
  the same range, and verifies B after queue completion.

## 78. Unreal And NVRHI Defects Vanguard Must Not Copy

The study found concrete source defects or unsafe contracts:

1. Unreal's heap `Flush` appears to assign `PreviousHandle` after advancing
   `Handle`, preventing adjacent free-range coalescing
   (`RHICoreTransientResourceAllocator.cpp:298-323`).
2. Unreal's cached graphics-discard workaround bit is set but not reset by
   `Acquire` (`RHITransientResourceAllocator.h:222-263,392`).
3. Unreal's placed-object cache treats a 64-bit hash as identity.
4. Unreal uses unchecked `uint16` range/page indices and unchecked size math.
5. An internal buffer path narrows native `uint64` size to `uint32`.
6. Heap exhaustion and native creation use fatal/check-style failure rather
   than a transactional result.
7. `MinimumFirstHeapSize` is declared but unused.
8. Cache capacity is soft enough to remain exceeded indefinitely while entries
   are young; an empty hit-rate sample divides by zero.
9. Validation mutates/deallocates before checking its ownership map and forces a
   reused object back to discard state instead of verifying the prior discard.
10. Transition equality does not first compare array lengths and ignores alias
    predecessor contents.
11. CPU GC cycle age is used as cache policy but cannot prove GPU completion.
12. Pinned NVRHI validation uses overflow-prone `offset + size` and Vanguard
    does not instantiate that wrapper anyway.
13. Pinned NVRHI's Tier-1 heap-category behavior is not represented truthfully
    by Vanguard's current coarse capability.
14. Vanguard payloads retain a bound heap but not the immutable offset/range,
    preventing release audit of alias relationships.

These are design evidence, not requests to patch Unreal or NVRHI during this
study.

## 79. Unreal U2 Decision Ledger

### Copy

- Compile physical allocation from logical queue schedule positions.
- Keep logical resource, native RHI object, and physical memory lifetimes
  separate.
- Let a free range retain prior-owner/fence provenance until alias actions are
  compiled.
- Use native requirements and real placed resources, with no shader lookup.
- Tie a cached placed object to exact descriptor, heap, and offset.
- Cache both placed objects and empty heaps.
- Keep backend alias/discard lowering outside allocator policy.
- Allow optional parallel object creation with an explicit join.

### Adapt

- Use typed schedule points and checked 64-bit ranges.
- Preserve exact predecessor identity in release plans.
- Follow every hash with exact equality.
- Use truthful heap classes and per-resource-kind capabilities.
- Use hard/soft budgets and structured pressure/OOM results.
- Make Resolve transactional and publish atomically.
- Use existing submission fences for destruction safety and CPU age only for
  victim choice.
- Roll out same-queue aliasing first, then validated graphics/compute handoffs.
- Keep U1 whole-resource reuse as the mandatory fallback.

### Reject

- Fatal allocation failure or unbounded heap creation.
- Hash-only cache identity.
- Debug-only correctness, compact unchecked handles, and unchecked arithmetic.
- GC age as evidence of GPU completion.
- Caller-owned forgotten creation-task joins.
- Hidden backend fallbacks or one vague universal alias capability.
- Reserved-page remapping in V1 without a real provider and measured need.
- Cross-kind or copy-queue alias reuse that the current RHI cannot prove.
- Unreal's broken range-coalescing implementation and stale cached flags.

### Vanguard-specific

- Reuse `CreateHeap`, deferred-binding resources, native memory requirements,
  `BindMemory`, alias/discard barriers, command-list retention, and the existing
  queue-fenced lifetime manager.
- Do not introduce a second placed-resource API or lifetime tracker.
- Harden current placement metadata, validation, compatibility reporting, and
  failure classification before enabling the provider.
- Keep the heap/range ownership ledger, logical overlap proof, cache policy, and
  atomic plan publication in the Resource Flow Allocator.
- Treat `virtualResource` as the current RHI spelling only; allocator design
  documentation calls it deferred-binding or placed.

## 80. Unreal U2 Exit Gate

U2 is complete. The study can now explain:

- the exact difference between whole-object reuse, placed-heap aliasing, and
  backend page remapping;
- how queue-aware logical fences permit a physical byte range to change owners;
- how Unreal's first-fit heap allocator records prior-owner provenance;
- how D3D12 creates and activates placed resources without shader lookup;
- which cache, budget, validation, and failure behaviors Vanguard must improve;
- which low-level Vanguard/NVRHI primitives already exist and must be reused;
- why V1 keeps whole-resource fallback, enables placed aliasing conservatively,
  and defers page remapping.

No production code was changed at the U2 checkpoint. The authoritative final
design now follows in Sections 81 through 90.

## 81. Authoritative V1 Authority And Supersession

This section begins the normative Vanguard V1 design. Sections 2 through 80
remain the cited RED, Unreal, NVRHI, and current-engine evidence. They are not a
second competing specification.

The final authority order is:

```text
Sections 81-90       authoritative Vanguard V1 contract
Sections 63-65       source of the compile-once execution-packet decision
Sections 69-79       source of placed-heap and fallback decisions
Sections 41-50       source of ownership, job, queue, and cleanup boundaries
all earlier sections historical evidence
```

The following older sketches are explicitly superseded:

| Historical sketch | Authoritative V1 replacement |
| --- | --- |
| RED full request replay and `TapeState::Replaying` | Immutable candidate plan, compiled execution generation, and a narrow step cursor |
| replayed decisions | Decision evaluated once during Planning and captured by `DecisionId` |
| `get_resolved(tag, current request time)` | Typed resolved binding addressed by `ResourceUseId` |
| RED `Startup` phase | Explicit `BeginFrame`, starting from `Idle` |
| offset-zero `AliasHeapBlock` | Checked arbitrary byte range in a native heap |
| global size-descending allocation | Chronological first-acquire event sweep with deterministic same-event ordering |
| one exact native alias `before` policy | Exact semantic predecessor records; backend chooses legal lowering |
| transferred-in/out and external exchange as normal paths | Retained imports and terminal publish-after-success exports |
| blanket async-region alias support | Same-queue first; explicit proven graphics/compute handoff later |

V1 retains the useful RED architecture:

- `PreConsume -> Resolve -> Consume -> Cleanup`;
- renderer-owned persistent physical caches and a frame-local session;
- stable GPU-flow order independent of CPU worker order;
- the common nonvirtual `process_node` wrapper around virtual execution;
- explicit queue scopes and real fork/join submission dependencies;
- exact command-recording positions for resource boundary actions.

It does **not** retain the error-prone requirement that the same node body run in
both PreConsume and Consume.

## 82. Ownership Boundary And Frame State Machine

### Ownership

```text
RenderingServiceImpl
  -> FrameRenderer
       -> RenderFlowResourceAllocator             device lifetime
            -> whole-resource pools               persistent
            -> placed-object and heap caches       persistent, optional
            -> one FrameResourceSession            frame-local

Render Graph / executor
  -> topology, producer dependencies, culling, and CPU/GPU schedule
  -> node implementations and process_node
  -> jobs, command recorders, queue submission, and terminal join

FrameResourceSession
  -> candidate resource operations
  -> logical allocation versions and temporal mappings
  -> retained imports and pending exports
  -> physical assignments and ownership ledger
  -> immutable execution generation and packets

RHI
  -> texture, buffer, heap, and deferred-binding objects
  -> state, discard, alias, and submission operations
  -> command-list retention and queue-fenced native destruction
```

The allocator never owns graph topology, node implementations, the job system,
command lists, queue submission, or another deferred-destruction system.
`FrameRenderer` initializes the allocator after RHI initialization, quiesces the
CPU rendering chain before allocator shutdown/recreation, and releases allocator
caches before RHI shutdown.

### State machine

```text
Idle
  -> Planning                    BeginFrame / PreConsume
  -> CandidatesSealed            every planning writer joined

     graph compiler consumes candidate accesses,
     culls a frame-local overlay, and supplies the surviving schedule

  -> Resolving                   complete plan built in scratch ownership
  -> Ready(generation)           one atomic publication; Consume gate opens
  -> Executing                   virtual node Execute runs once
  -> TerminalJoined              all node/child/epilogue recording work joined
  -> FinishingPrepare            validate terminal receipt, prepare exports/retirement
  -> Committing                  infallible atomic publication and ownership commit
  -> Idle

Planning | CandidatesSealed | Resolving
  -> AbortBeforePublication
  -> Idle

Ready | Executing | TerminalJoined | FinishingPrepare
  -> AbortDuringExecution
  -> Idle

any active state + device loss
  -> DeviceUnavailable           invalidate allocator caches and export slots
                                  hand native ownership to RHI device teardown

DeviceUnavailable
  -> Idle                         only after RHI/device and allocator recreation
```

`Resolve` is a named fallible operation, not a hidden side effect of changing a
phase. It cannot start until all planning writers join. `Ready` cannot be reached
until logical compilation, physical creation, all child creation jobs, validation,
and publication are complete.

Pre-publication abort:

- keeps the Consume gate closed;
- releases scratch imports, objects, and heaps through normal RHI ownership;
- publishes no execution generation, cache ownership, mapping, or export;
- returns the session to `Idle` exactly once.

Execution abort:

- stops launching avoidable work and joins work already dispatched;
- discards command recorders that were not submitted;
- retains or quarantines anything touched by submitted work until its recorded
  queue fences complete;
- publishes no exports and performs no normal cache aging;
- clears the execution generation and returns to `Idle` exactly once.

`Finish` receives a validated terminal execution receipt from the executor. The
receipt proves the terminal join and classifies every compiled command scope as
submitted with its real queue-fence receipt or discarded before submission. A
device-loss receipt may instead mark scopes `UnknownDueToDeviceLoss`; that status
forces the poisoned-device teardown path and is never accepted for ordinary
retirement. The allocator derives each physical assignment's retirement-fence
union from the receipt and the immutable ownership ledger; it never fabricates
fence values.

All fallible terminal validation and export preparation occurs in
`FinishingPrepare`. Failure there follows `AbortDuringExecution` before any
export slot changes. `Committing` is a no-fail commit: it atomically publishes
all prepared exports, transfers eligible entries to retirement/cache ownership,
clears the generation, and returns to `Idle`. Partial export publication is
forbidden.

Device loss is not an ordinary abort. Lost-device fences may never signal. The
allocator enters `DeviceUnavailable`, invalidates unpublished/prepared exports,
drops its cache indexes, and transfers native-handle teardown to the RHI's
device-loss path without waiting for normal retirement fences. `BeginFrame`
fails until the RHI device and allocator caches have been recreated.

Terminal execution means all CPU node work, child jobs, deferred epilogues,
command recording, and required submissions completed successfully. It does not
mean GPU idle.

## 83. Typed Identity, Descriptors, Views, And Access

### Identity

These identities are distinct and cannot be replaced by one hash, integer, raw
pointer, or `rhi::ResourceRef`:

```cpp
FlowSpaceId
GpuFlowGroupId
RenderNodeId
CommandScopeId

ResourceNameId
LogicalResourceId
LogicalAllocationId
LogicalTextureViewId
LogicalBufferViewId
ResourceUseId
ResourceScopeId
DecisionId
ExternalResourceToken
ImportedResourceId
ExportSlotId

PlanPosition
CompiledStepId
ExecutionGenerationId

PhysicalResourceId
PlacedHeapId
PlacedAllocationId
PlacementGeneration
```

A named logical key is structurally equivalent to:

```text
(FlowSpaceId, ResourceNameId)
```

The resource name may be interned and hashed for lookup, but full structured
identity decides equality. A temporary resource derives identity from its stable
declaration `PlanPosition`; its display name is diagnostic only. `PlanPosition`
is:

```text
(GpuFlowGroupId, sequential operation ordinal within that entire GPU flow group)
```

All ordered subnodes inside one command-list group share the parent's one
sequential ordinal domain. Planning may run in parallel across GPU flow groups,
not by creating independent colliding ordinal domains inside one group. CPU
worker id, arrival order, completion time, and thread id never participate.

`LogicalResourceId` names a temporal mapping. `LogicalAllocationId` identifies
one declared/imported allocation version. `ResourceUseId` identifies one stable
authored use of the version active at that position. An RHI handle identifies
only the resolved physical object.

### Descriptors

The logical descriptor is a typed variant:

```cpp
FrameResourceDesc = FrameTextureDesc | FrameBufferDesc

FrameTextureDesc
  active creation shape and existing rhi texture fields
  maximum reusable capacity
  explicit initialization / clear policy

FrameBufferDesc
  active byte/element shape and existing rhi buffer fields
  maximum reusable capacity or explicit capacity class
  explicit initialization policy
```

Vanguard reuses existing RHI format, dimension, extent, usage, and state enums.
It does not copy RED's packed union, bit widths, borrowed names, or implicit
initialization.

Four comparisons remain named and separate:

```text
DeclarationEquality
LogicalSwapCompatibility
CanonicalWholeObjectCompatibility
PlacedMemoryCompatibility
```

A hash may select a candidate bucket. Full canonical equality is authoritative
in every build.

### Views and access

Texture and buffer views are typed children of a logical resource. They do not
own independent physical memory lifetimes. A planned use contains at least:

```cpp
ResourceUseId
LogicalResourceOrViewId
LogicalAccessIntent
required rhi state
texture subresource range, when applicable
initialization / load / preservation intent
QueueType
CommandScopeId
begin and end PlanPosition
RequestOrigin
```

Textures track state per mip/layer/plane subresource. Buffers use whole-buffer
state in V1. Logical access intent remains separate from backend state: lifetime,
content provenance, and an RHI transition are related but not identical.

New whole or placed assignments begin with undefined contents. `Load` or
preservation is legal only when content provenance comes from an import or a
prior write in the same logical allocation version. Reusing memory never
implicitly transfers the previous logical owner's contents.

## 84. Planning Contract, Culling Boundary, And Logical Resolve

### Planning surface

External handles enter through the frame session, not through parallel node
writers:

```text
RegisterImport(
  ExternalResourceToken,
  retained typed RHI owner,
  physical-authoritative descriptor,
  initial state / queue ownership,
  incoming same-queue continuation or explicit wait token,
  required terminal state / ownership)
    -> ImportedResourceId
```

Registration retains the physical object once. Repeating a token coalesces only
when the complete typed contract is identical; otherwise it fails. Planning
operations carry only `ImportedResourceId`, so a `PlanningContext` never owns or
passes raw RHI handles.

Ordinary node planning may express these semantic operations:

```text
DeclareTexture / DeclareBuffer
DeclareLike
DeclareTemporary
CreateTextureView / CreateBufferView
ImportTexture / ImportBuffer(ImportedResourceId)
RequestExport(logical resource, TerminalMapping, ExportSlotId)
BeginUse / EndUse
SwapLogicalMappings
CaptureDecision
```

A declaration or import whose logical destination is already mapped is rejected,
even when the descriptors happen to match. There is no silent duplicate merge or
last-writer-wins rule. The same `ImportedResourceId` cannot be installed into
multiple logical destinations in V1.

An export request captures the logical resource's terminal mapping after all
surviving swaps. Resolve turns that mapping into the exact
`LogicalAllocationId`. Exporting an arbitrary mid-timeline mapping is deferred;
node authors cannot guess a Resolve-created allocation id during planning.

Executor/graph compilation, not arbitrary node code, supplies:

```text
QueueBegin / QueueEnd
QueueSync / Submit
node and command-scope boundaries
```

V1 intentionally does not expose:

```text
Free
IsAllocated / IsDeclared
SwapWithExternal / ExchangeExternal
manual untracked external access
eager convert-to-external
arbitrary resolved lookup by logical tag during execution
```

An ergonomic scoped-use helper may wrap `BeginUse` and `EndUse`, but the plan
still contains explicit balanced endpoints and an explicit `ResourceUseId` or
`ResourceScopeId`. A destructor that silently suppresses a fallible end is not
the correctness boundary.

### Planning and culling

Each candidate execution unit instantiates its resource plan exactly once per
frame. An immutable static schema may be copied from the graph cache, but its
per-frame descriptors, decisions, imports, and uses are instantiated only once.
Planning workers write worker-local candidate storage and then join.

The graph compiler owns producer dependencies and node culling. It consumes the
sealed candidate accesses, chooses roots such as presentation, external side
effects, and requested exports, and creates a frame-local surviving overlay. It
does not mutate cached topology. The allocator owns neither that backwards walk
nor the survivor decision.

Cross-node use scopes carry a stable `ResourceScopeId`. Culling must preserve all
required endpoints through dependencies or reject the broken scope. Only after
culling may the allocator finalize first/last uses or allocate physical storage.

### Logical Resolve

Resolve merges surviving operations by stable `PlanPosition` and:

1. validates descriptors, ids, decisions, imports, queue scopes, and balanced
   use endpoints before mutating resolved state;
2. creates a new `LogicalAllocationId` for every surviving declaration/import;
3. builds time-varying logical-tag mappings;
4. makes mapping changes effective only after their swap position;
5. binds each `ResourceUseId` to the allocation version active at its begin;
6. derives per-subresource texture state and whole-buffer state chains;
7. computes actual use intervals and conservative queue-safe occupancy;
8. omits declarations with no surviving use unless an export contract retains
   them;
9. passes the resulting allocations to physical providers;
10. compiles immutable execution packets and validates them before publication.

Frame-dependent decisions are evaluated once while planning and stored under a
`DecisionId`. Execution reads the captured value. A decision that changes graph
topology or static use schema selects a different graph-cache key instead.

Two uses at the same `PlanPosition` overlap for V1. An old after-action at one
compiled step may precede a new before-action only at a distinct later step.
Integer equality alone never proves ordered alias safety.

## 85. Physical Assignment And Provider Selection

Every used logical allocation resolves to exactly one assignment:

```cpp
PhysicalAssignment =
    ImportedRetained
  | WholeResourceAssignment
  | PlacedResourceAssignment
```

### Imported retained

The execution generation holds one strong frame reference. The external physical
object is descriptor authority, is never pooled or aliased by the allocator, and
must carry explicit initial access, queue ownership, incoming readiness, and
required final access/ownership. Incoming readiness is either an ordered
continuation on the owning queue or an explicit wait token that the executor will
really submit; a retained reference proves lifetime, not visibility.

### Whole-resource provider

This is the mandatory baseline provider on every supported backend. It reuses or
creates one complete ordinary RHI texture/buffer. Reuse requires:

- canonical texture equality, or the explicit compatible buffer capacity class;
- pool-only allocator ownership;
- non-overlapping compiled logical lifetimes and queue-safe occupancy: same-queue
  order or a dependency/wait already present in the executor's submitted schedule;
- cross-frame retirement safety;
- explicit previous-state continuity and valid content provenance.

Whole-object reuse needs ordinary state transitions, not an alias barrier.
"Mandatory fallback" means the provider must exist; it does not promise success
after a hard budget, device-loss, or native allocation failure.

### Placed-heap provider

This provider is disabled by default until the active RHI profile truthfully
proves, per resource kind and heap class:

```text
deferred-binding resource creation
authoritative size and alignment
heap category / memory type / compatibility class
overflow-safe one-time binding validation
alias and discard lowering
immutable placement metadata
supported queue handoff modes
conformance tests for the exact backend profile
```

The current two coarse capability booleans are insufficient.

Initial V1 placed policy is:

- separate texture and buffer heap classes/pools;
- same-kind alias predecessor chains;
- same-queue handoffs only;
- no copy-queue alias reuse;
- no cross-kind heap reuse even where D3D12 tier 2 could permit it;
- graphics/compute handoffs remain disabled until explicit fork/join proof and
  backend conformance are enabled as a later profile feature.

Physical planning is a chronological event sweep:

```text
sort acquisition events by first PlanPosition
for acquisitions at the same position:
    descending required bytes
    descending alignment
    stable LogicalAllocationId

for each acquisition:
    search compatible heaps in stable order
    choose the first aligned free range whose prior owners are ordered complete
    otherwise admit a new compatible heap under the shared budget
    record the exact range and every predecessor fragment crossed

process releases only after all acquisitions at the same position
```

Each placement uses checked `u64` offset, size, alignment, and end arithmetic.
It carries immutable heap identity/class, range, native requirements, placement
generation, and per-predecessor provenance:

```cpp
PreviousPlacedOwner
  physical resource
  overlapping range
  discard / end step
  source queue

PlacedAliasHandoff
  destination placed allocation
  previous owners[]
  acquire step
  destination queue
```

A cached placed object matches the exact canonical descriptor, heap, and offset
after hash lookup and may be assigned at most once per frame.

Capability absence or resource ineligibility selects the whole-resource provider
and records the reason. Fragmentation may select whole fallback only if the same
allocator-wide hard-byte ledger admits it. `BudgetExceeded`, device loss, and
native OOM cannot be hidden by trying another provider outside the budget.

An internally allocated terminal export is forced to a standalone whole object
and removed from allocator cache ownership when published. It is never a placed
alias in V1. If the terminal mapping is `ImportedRetained`, export republishes
that same retained external object with its release/readiness contract; it is
not copied, made allocator-owned, or inserted into a cache. Reserved-resource
page remapping is outside V1.

## 86. Compiled Execution Generation And Step Cursor

Successful Resolve atomically publishes one immutable generation:

```cpp
CompiledExecutionGeneration
  id
  node packets[]
  retained imports[]
  pending exports[]
  physical assignments[]
  placement / ownership ledger
  queue schedule reference

CompiledExecutionPacket
  execution generation id
  render node id
  GPU flow group
  ordered steps[]
  captured decisions[]
  validated import indices[]
```

Resource use is a two-sided compiled protocol:

```text
UseBegin
  ResourceUseId
  LogicalAllocationId
  typed resolved texture/buffer/view
  before_actions[]

UseEnd
  ResourceUseId
  after_actions[]
```

Other compiled steps are executor-owned:

```text
QueueBegin / QueueEnd
QueueSync / Submit
OwnershipAction
ExportFinalStateAction
```

`process_node` installs the node packet and execution cursor, then calls virtual
`Execute` exactly once. No templated virtual function or `Resolved<T>` virtual
parameter is required.

The cursor exposes the equivalent of:

```text
BeginTextureUse(ResourceUseId) -> ResolvedTextureUse
BeginBufferUse(ResourceUseId) -> ResolvedBufferUse
EndUse(ResourceUseId)
CapturedDecision(DecisionId)
```

On begin it validates generation, node, next step, resource kind, active queue,
and command scope; executes ordered before-actions; and returns a move-only,
non-owning scoped use. The scoped use exposes only the typed view/binding needed
by the active recorder. It cannot be converted into or copy out an owning RHI
reference, and it becomes invalid at the matching `EndUse`. Derived access passed
to child recording jobs remains within that scope, so those jobs must join before
the end. On end the cursor executes ordered after-actions and marks exactly-once
completion.

The cursor cannot declare resources, change decisions, alter logical mappings,
look up an arbitrary tag, skip/reorder a step, or use another node's packet.
Control steps are advanced by the executor, not reproduced by node code.

Before a node is considered successful or its recorder becomes submission-
eligible, the `process_node` epilogue performs `FinalizePacket`: no authored
steps may remain, no use/scope may remain active, and no use-scoped child job may
remain unjoined. The executor performs the equivalent exhaustion check for its
control steps at explicit packet/scope boundaries. Failure is
`IncompleteExecution`; it cannot be deferred until cache cleanup.

The execution generation, rather than every packet, owns retained imports and
physical assignments. Child jobs retain the generation until their command
recording and associated after-actions finish.

## 87. Queue, State, Alias, Import, And Export Actions

Three clocks remain separate:

```text
CPU dependency counters
  -> decide when node jobs may run

compiled GPU schedule + queue/command scope
  -> decide where commands and physical actions are recorded

submission fence sets
  -> decide when cross-frame cache entries and native objects may be reused or
     destroyed
```

Total GPU flow order does not prove happens-before between concurrent queues.
The cursor verifies that every physical action is recorded on the planned queue
and command scope. The allocator never invents synchronization; it relies only
on waits the executor will really submit.

Whole-resource owner handoff carries the previous final state and emits ordinary
state transitions. Placed handoff compiles the semantic sequence:

```text
old UseEnd
  -> after old commands and child recording
  -> FinalizePreviousOwner(state/content no longer required)

proven queue dependency, when applicable

new UseBegin
  -> backend-neutral AliasActivation(previous owners -> new)
     lowered once into the required alias visibility and discard/undefined work
  -> transition new resource to the declared first-use state
  -> new commands
```

Exact predecessor identity remains in release plans even when legacy D3D12
lowers the barrier using null `before`. The RHI/backend owns the choice between
legacy alias barriers, enhanced undefined-layout initialization, or another legal
backend form. The plan contains one predecessor-finalization semantic and one
alias-activation semantic per destination. A backend may lower that package to
multiple native commands, but the allocator does not independently emit a second
native discard for the same handoff.

Imports and exports are explicit contracts:

```text
ImportResource
  ImportedResourceId registered and retained by the session
  ExternalResourceToken
  physical-authoritative descriptor
  initial state and queue ownership
  incoming readiness: same-queue continuation or explicit wait token
  required final state / ownership

PlannedExport
  LogicalResourceId
  capture = TerminalMapping
  typed ExportSlotId
  required final state / ownership

PendingExport (created by Resolve)
  LogicalAllocationId
  typed ExportSlotId
  required final state / queue ownership
  outgoing readiness: same-queue continuation or explicit signal/fence token
  publish only after successful TerminalJoined
```

Repeated import of the same external identity must agree on kind, descriptor
expectation, ownership, and terminal contract. Export compilation must place a
real final state/ownership action on a queue with a proven handoff; otherwise
Resolve fails. Publishing an export does not wait for GPU idle. The published
slot therefore carries both the retained typed RHI owner and its outgoing
readiness contract. A strong reference proves lifetime only; an external queue
or CPU consumer must obey the same-queue continuation or wait the published
signal/fence token before access. The terminal execution receipt supplies the
actual submitted signal/fence evidence.

Logical mapping swaps are allowed between declared/imported allocations. They
change which allocation a later `ResourceUseId` sees; they do not exchange C++
references or silently transfer external ownership.

## 88. Persistent Caches, Budgets, Retirement, And Failure

### Cache states

Whole objects, placed objects, and heaps are persistent allocator-owned cache
entries. A reusable entry follows:

```text
Assigned
  -> PendingRetirement(ResidencyFenceSet)
  -> Reusable
  -> Assigned
       or
     EvictedPendingNativeDestruction(RhiRetirementObservation)
       -> NativeReleased
```

Within-frame placed aliasing uses the compiled schedule proof and need not wait
for hardware completion between logical owners. Cross-frame reuse is different:
V1 reuses a cached object/range only after all stored submission fences complete.
A future explicit inter-frame queue-wait policy may relax this without changing
the logical design.

Dropping allocator ownership never proves immediate native destruction. The
existing RHI lifetime manager remains the only native retirement mechanism. The
allocator needs a narrow completion observation from that manager so its byte
ledger knows when an evicted native allocation was actually released; it does
not create a second destruction queue.

`ClearPersistentCaches` is legal only while the allocator is device-ready and
the frame session is `Idle`. It evicts `Reusable` entries, marks
`PendingRetirement` entries `EvictWhenReusable`, and never mutates an active or
assigned generation. Cache lookup ownership may disappear immediately, but hard
budget accounting remains until native-release observation. Device loss uses the
separate `DeviceUnavailable` teardown path.

### One byte ledger

The allocator maintains one native-byte admission ledger, with optional
per-memory-class targets, across whole objects and native heaps. It distinguishes:

```text
assigned bytes
reusable cached bytes
pending-retirement bytes
evicted-pending-native-destruction bytes
heap capacity / committed bytes
requested and aliased peak bytes
high-water marks
```

Pending-retirement and evicted-pending-native-destruction bytes count against the
hard limit until their respective completion observations. Evicting a placed
child may reduce object/view pressure but does not free heap bytes. Evicting an
eligible empty heap or whole object requests native reclamation, but admission
cannot subtract those bytes until the RHI confirms release. If the current RHI
lacks that observation, the execution plan must add the narrow observation hook
before claiming a hard live-native-byte budget.

Soft targets control trimming. Hard limits control admission. The stable pressure
order is:

```text
trim eligible reusable objects/views
trim eligible empty heaps and whole objects
try the selected eligible provider under the same hard ledger
try a permitted fallback under that same ledger
return a structured failure
```

CPU age/LRU ranks victims but proves no safety. Entries assigned this frame,
pending retirement, imported, exported, or still strongly referenced are not
eligible victims.

### Failure classes

The semantic error set distinguishes at least:

```text
InvalidPhase
InvalidOrStaleIdentity
DescriptorConflict
InvalidUseOrScope
QueueOrCommandScopeMismatch
UnsupportedCapability
ArithmeticOverflow
InvalidPlacement
BudgetExceeded
NativeOutOfMemory
DeviceLostOrBackendFailure
IncompleteExecution
```

Validation occurs before mutating candidate, ownership, cache, binding, or export
state wherever possible. Native deferred binding is one-shot; rollback releases
unpublished handles and recreates on retry rather than inventing `UnbindMemory`.

After an ordinary execution failure, every touched/submitted entry is
quarantined until the real submission fences from the terminal receipt complete.
Unsubmitted scopes are canceled and their unpublished recorders discarded;
uncertain entries are never returned immediately to the reusable cache. On
device loss, normal quarantine is bypassed because the fences may never signal:
the allocator becomes `DeviceUnavailable`, invalidates caches and exports, and
hands all native ownership to RHI device teardown/recreation.

## 89. Semantic Allocator And Context Surface

Exact C++ spelling and file placement belong to the execution plan. The required
semantic surface is:

```cpp
RenderFlowResourceAllocator::BeginFrame(
    FrameSerial,
    FrameResourcePolicy) -> Result<FrameResourceSession>

FrameResourceSession::RegisterImport(
    ExternalResourceToken,
    RetainedTextureOrBuffer,
    PhysicalAuthoritativeDesc,
    ExternalAcquireContract,
    ExternalTerminalContract) -> Result<ImportedResourceId>

FrameResourceSession::CreatePlanningWriter(
    RenderNodeId,
    GpuFlowGroupId,
    CommandScopeId) -> PlanningContext

FrameResourceSession::SealCandidates(PlanningJoinToken)

FrameResourceSession::Resolve(
    SurvivingGraphOverlay,
    CompiledQueueSchedule,
    jobs::Builder&) -> Result<ExecutionGenerationRef>

FrameResourceSession::PacketFor(RenderNodeId)
    -> Result<CompiledExecutionPacketView>

FrameResourceSession::Finish(
    TerminalExecutionReceipt) -> Result<void>

RenderFlowResourceAllocator::ClearPersistentCaches(CacheClearPolicy)
```

The terminal receipt contains one terminal-join proof, a completion kind
(`Completed`, `Aborted`, or `DeviceLost`), every submitted
`CommandScopeId -> (QueueType, real fence/signal receipt)` mapping, and every
discarded-before-submission scope. Only `DeviceLost` may additionally classify a
scope as `UnknownDueToDeviceLoss`. `Finish` validates complete scope coverage
against the immutable generation and derives retirement fence sets; callers do
not pass guessed per-resource fences.

The integration capabilities are deliberately narrow:

```text
PlanningContext
  logical declarations, views, uses, swaps, imports, exports, decisions
  imports reference only pre-registered ImportedResourceId values
  no raw or retained RHI handles
  no resolved getter, command recorder, or GPU work

RenderNodeExecutionContext + installed packet cursor
  typed ResourceUseId bindings and captured decisions
  active command recorder supplied by the executor
  no declaration, allocation, decision mutation, or arbitrary tag lookup
```

The graph/executor must supply:

- stable render-node, GPU-flow-group, queue, command-scope, and step identities;
- joined candidate planning and a frame-local surviving overlay;
- an immutable graph/node lifetime through terminal execution;
- the active command recorder at every compiled physical action;
- actual queue sync/submission matching the compiled schedule;
- a unique normal-or-abort terminal epilogue and complete terminal execution
  receipt with real queue fence/signal evidence.

The allocator supplies:

- structured logical identities and temporal mappings;
- validated descriptors, access intervals, and content provenance;
- physical provider selection, assignments, and cache ownership;
- immutable execution generations, per-use bindings, and boundary actions;
- deterministic diagnostics, budgets, finish/abort cleanup, and atomically
  published export owners plus readiness contracts.

## 90. V1 Scope, Non-Negotiable Invariants, And Synthesis Exit Gate

### Included in V1

- textures and buffers;
- typed logical resources and child views;
- active shape and maximum reusable capacity;
- declare, declare-like, temporary, use begin/end, logical swap, decision,
  retained import, and terminal export;
- culling-aware logical lifetime compilation;
- per-subresource texture and whole-buffer state planning;
- mandatory whole-resource pooling;
- optional same-kind/same-queue placed-heap aliasing after RHI hardening;
- persistent caches, shared budgets, diagnostics, atomic Resolve, and abort;
- compiled per-use packets consumed through virtual node execution once.

### Explicitly deferred or excluded

- full declaration replay in Consume;
- shader virtual texturing or shader-visible page tables;
- reserved-resource physical page remapping;
- cross-kind placed aliasing;
- copy-queue placed alias reuse;
- graphics/compute placed handoff until separately enabled by truthful capability
  and conformance tests;
- mid-frame external ownership exchange, eager convert-to-external, and untracked
  external access;
- allocator-owned graph topology, scheduler, command lists, submission, or native
  destruction queue;
- backend-specific barriers exposed as node-authored allocator policy.

### Non-negotiable invariants

1. Planning is instantiated once and virtual execution runs once.
2. Every planning writer joins before sealing; every creation job joins before
   publication.
3. Culling completes before final lifetimes and physical assignment.
4. CPU worker order never defines allocator time.
5. Resolution during execution is per `ResourceUseId`, never arbitrary tag lookup.
6. On successful execution, all steps, scopes, queue actions, and before/after
   actions complete exactly once in compiled order. On abort, every started step
   settles once and every unstarted step is explicitly `Canceled` without
   running its GPU action.
7. Uses sharing one `PlanPosition` cannot alias.
8. No hash is authoritative without full equality.
9. All byte, alignment, capacity, and range arithmetic is checked at 64 bits.
10. Imports are session-registered and retained once; exports remain empty until
    atomic terminal success and carry an explicit readiness contract.
11. Resolve is scratch-built, fallible, fully joined, validated, and atomically
    published.
12. The whole-resource provider always exists, but hard budget/native failure is
    still reported rather than hidden.
13. Placed aliasing stays disabled until requirements, heap classes, binding
    validation, placement records, barriers, and tests are truthful.
14. Logical schedule proof and hardware submission completion remain different
    clocks.
15. Cross-frame reuse obeys recorded submission fences; hard-budget bytes remain
    charged until the RHI lifetime manager confirms native release.
16. The allocator never duplicates the existing RHI lifetime manager.
17. Normal and non-device abort cleanup each run exactly once and return the
    session to `Idle`; device loss enters `DeviceUnavailable` until recreation.
18. `process_node` and executor control boundaries reject unexhausted packets,
    active scopes, or unjoined use-scoped jobs.
19. A declaration/import never silently replaces or merges an already-mapped
    logical destination.
20. A resolved use is non-owning and cannot outlive its compiled use scope.
21. Retirement and export publication use a complete, validated terminal
    execution receipt; the allocator never guesses submission fences.

### Exit gate

The authoritative V1 design is complete. It now defines one coherent answer for:

- ownership and phase transitions;
- logical ids, descriptors, views, temporal mappings, and culling;
- physical provider selection and fallback;
- placed range allocation and alias provenance;
- node-facing compiled execution without declaration replay;
- imports, exports, queues, actions, caches, budgets, retirement, and failure;
- the exact allocator/graph/executor/RHI boundary.

No production code was changed. The next permitted task is the separate
file-level execution plan after review. Implementation remains blocked until
that plan is explicitly approved.
