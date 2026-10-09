# Vanguard Render Graph Study Plan

Date: 2026-09-03

Status: original studies preserved; RED-faithful revisions R0 through R9 are
complete; the authoritative design and execution plan are approved; production
implementation has not started

## 1. Objective

Define the complete Render Graph and render-node architecture that will connect
the existing frame, scene, viewport, RHI, and resource-flow systems:

```text
retained RenderFrameInfo and prepared view family
  -> select or build a frame-local graph instance
  -> declare passes, resources, outputs, and explicit dependencies
  -> validate the complete selected definition
  -> compile CPU dependencies, command scopes, queue waits, and resource uses
  -> resolve RenderFlowResourceAllocator execution packets
  -> record each scheduled node exactly once
  -> submit or discard every command scope with truthful receipts
  -> transition and release or present the acquired frame output
  -> join all CPU work and finish the allocator session exactly once
```

The study must result in an architecture and file-level implementation plan
that can be reviewed before production code is changed.

## 2. Hard Boundary

This is a Render Graph study, not a continuation of allocator implementation.

Included:

- graph, pass, node, edge, and stable-identity contracts;
- graph construction, caching, frame instantiation, and invalidation;
- pass/resource declaration and graph validation;
- structural graph selection and validation without mandatory post-build culling;
- CPU job compilation and execution lifetime;
- graphics, compute, and copy command scopes;
- cross-queue dependency lowering and submission receipts;
- integration with `RenderFlowResourceAllocator`;
- external resources, frame-output acquisition, extraction, and presentation;
- failure, abort, device-loss, shutdown, and diagnostic behavior;
- a staged implementation and verification plan.

Excluded until separately designed or already owned elsewhere:

- rewriting the completed resource allocator;
- shader compilation, PSO-cache internals, and material semantics;
- mesh batching, visibility algorithms, lighting, and post-processing content;
- a temporary mini graph or fake executor installed just to make a frame pass;
- a second CPU scheduler, RHI submission system, or GPU lifetime manager;
- speculative multi-GPU, reserved-resource paging, or arbitrary queue DAG
  optimization in the first usable baseline.

Representative render passes may be used in tests, but this project defines the
machinery that schedules passes; it does not attempt to build the renderer's
entire pass library at the same time.

## 3. Confirmed Vanguard Starting Point

The following are inputs, not open redesign questions:

1. `RenderingServiceImpl` owns `FrameRenderer` for the renderer/device
   lifetime.
2. `FrameRenderer` privately owns the initialized
   `RenderFlowResourceAllocator`.
3. `RenderCommandSystem` owns one serialized CPU render chain and passes its
   existing Jobs continuation through `RenderFrameContext`.
4. `FrameRenderer::RenderFrame` retains or prepares the view family and then
   deliberately fails because no Render Graph executor is installed.
5. `ViewportManager` already owns output acquisition, abandonment, and
   presentation contracts.
   Acquisition and lifecycle reconciliation remain main-thread-only, while a
   valid dispatched acquisition is presented or abandoned by a thread-safe
   terminal node in the current frame's Render Graph. Presentation is not
   deferred to the next frame boundary.
6. The RHI already owns command-list creation/binding, submission receipts,
   queue fences, fork/join synchronization, presentation transitions, and
   fence-safe native destruction.
7. The allocator already owns logical resource planning, physical assignment,
   per-node execution packets, transitions/alias actions, terminal exports,
   and terminal receipt validation.
8. Graph preparation must register every resource request, GPU flow group,
   command scope, and explicit queue-sync request directly with the allocator.
   The allocator then resolves its complete collected request stream without
   receiving graph topology, a survivor overlay, or a public graph schedule.
   Total graph order is not a substitute for GPU synchronization.

The graph must reuse these boundaries instead of creating competing owners.

## 4. Terminology To Resolve Explicitly

The studies will keep these concepts separate even if RED or Unreal uses
different names:

| Concept | Meaning in this study |
| --- | --- |
| Graph definition | Reusable topology or recipe independent of one frame's mutable execution state |
| Graph instance | Frame-local inputs, conditions, resources, and execution state |
| Node | Optional higher-level renderer object that may contribute one or more passes |
| Pass | Smallest scheduled unit with declared accesses and one execution callback |
| Explicit edge | Author-authored ordering that is not derivable from resource hazards |
| Resource edge | Compiler-derived dependency caused by declared access |
| Command scope | Ordered command-list recording/submission unit on one RHI queue |
| CPU dependency | Jobs readiness relation; it does not by itself synchronize GPU queues |
| Queue dependency | Executable submission wait/signal relation between command scopes |
| External resource | RHI resource retained outside the frame and registered for graph use |
| Extraction/export | Transfer of a graph result to an owner beyond graph execution |
| Frame output | Acquired viewport image that must be presented or abandoned exactly once |

Whether Vanguard's public authoring surface should expose both `Node` and
`Pass`, or only one first-class scheduled type, remains an explicit study
question. Names will not be fixed before their ownership and lifetime differ.

## 5. Evidence Rules

For every RED or Unreal claim, preserve:

- repository path;
- symbol and line range;
- source line count and SHA-256 for the studied snapshot;
- whether the statement is directly observed or inferred;
- whether Vanguard should copy, adapt, or reject the behavior;
- any correctness defect or engine-specific assumption that must not migrate.

Primary local source is preferred over summaries. Existing Vanguard allocator
research may be cited for already-settled allocator facts, but it cannot stand
in for studying graph-owned behavior.

Each completed study set updates the durable design document and the resume
checkpoint before the next set begins.

## 6. Study Sets

### G0 — Vanguard inventory and non-negotiable seams

Trace the current code from engine frame submission through the missing graph
boundary and record:

- exact object ownership and shutdown order;
- lifetime of `RenderFrameInfo`, prepared view families, and payloads;
- the Jobs continuation and asynchronous failure-reporting path;
- viewport acquire/present/abandon invariants;
- RHI command-list, queue, synchronization, and receipt capabilities;
- allocator planning, resolve, execution-packet, and finish inputs;
- producer systems waiting for graph integration.

Exit gate: every existing owner has one graph-facing responsibility and there
is no proposed duplicate scheduler, allocator, submission path, or output owner.

### R1 — RED graph representation, construction, and cache

Study `CRenderNodeGraph`, its factory, node storage, graph cache, and render-frame
builders:

- node identity and ownership;
- explicit edges and validation;
- construction macros/factories and grouped subnodes;
- reusable topology versus per-frame data;
- camera/mode graph selection and cache invalidation;
- roots, sinks, reachability, and topology ordering;
- limits, assertion-only assumptions, and stale-cache hazards.

Exit gate: reconstruct one RED camera graph from build to selected cached
instance without relying on allocator behavior.

### R2 — RED node processing, Jobs execution, and submission

Study the common process wrapper, node contexts, command-list groups,
synchronization nodes, and terminal frame path:

- planning versus consume execution;
- one-node/one-pass execution semantics;
- CPU dependency counters and child-job joins;
- worker-local mutable state and immutable shared state;
- command-scope creation, binding, closing, and submission;
- fork/join async-compute behavior;
- terminal completion, presentation, and cleanup;
- failure paths and lifetime assumptions.

Exit gate: write the exact RED sequence from selected graph to final CPU join
and identify every place where GPU work is actually ordered.

### R3 — RED production graph ergonomics and defects

Sample representative graphics, compute, utility, and output nodes to learn:

- authoring ergonomics and repeated boilerplate;
- resource and parameter declaration patterns;
- conditional nodes and explicit dependencies;
- instrumentation and graph debugging;
- how graph structure scales across renderer features;
- which legacy assumptions Vanguard must reject.

Exit gate: the RED decision ledger distinguishes transferable architecture from
project-specific pass content and known defects.

### U1 — Unreal RDG builder, passes, resources, and compilation

Study `FRDGBuilder`, pass/resource types, parameter metadata, dependency setup,
producer tracking, culling, graph validation, and compile order:

- builder and graph lifetime;
- pass registration and immutable execution payloads;
- resource/view identity and declared access;
- automatic dependencies plus explicit ordering escape hatches;
- side effects, roots, extraction, and culling;
- immediate mode versus deferred compilation;
- validation and diagnostic surfaces.

Exit gate: reconstruct the declaration-to-compiled-pass pipeline and state the
minimum safe Vanguard equivalent without copying Unreal reflection machinery.

### U2 — Unreal RDG execution, barriers, async compute, and transients

Study execution and physical scheduling:

- prologue/epilogue barriers and subresource state merging;
- graphics/async-compute overlap and cross-pipeline fences;
- command-list partitioning and pass merging;
- transient allocation, aliasing, pooled fallback, and retirement;
- external registration, extraction, upload/readback, and final access;
- execution failure assumptions and debug validation.

Exit gate: separate reusable compiler ideas from Unreal RHI/threading and
reflection-specific machinery.

### V1 — Vanguard architecture synthesis

Choose and document:

- graph/node/pass type model;
- ownership and lifetime state machines;
- build, instantiate, compile, execute, and terminal APIs;
- deterministic identity and ordering rules;
- resource declaration bridge to allocator planning writers;
- culling and side-effect/root semantics;
- CPU job and command-scope compilation;
- queue dependency model and V1 limitations;
- frame-output import/present transaction;
- failure reporting and terminal cleanup;
- graph cache/invalidation policy;
- diagnostics, captures, and tests.

Exit gate: every execution edge is classified as CPU, same-queue order, or
cross-queue synchronization, and every frame-owned object has a terminal owner.

### V2 — File-level implementation plan

Produce independently reviewable stages with:

- exact files and public/private surfaces;
- prerequisites and invariants;
- tests and failure injection;
- Debug and Shipping exit gates;
- explicit deferrals;
- correction/review passes between dependent stages.

No production implementation begins until this plan is reviewed and explicitly
approved.

## 7. Primary Source Routes

### Vanguard

```text
source/rendering/include/vanguard/rendering/frame_renderer.hpp
source/rendering/src/frame_renderer.cpp
source/rendering/include/vanguard/rendering/render_command_system.hpp
source/rendering/src/render_command_system.cpp
source/rendering/include/vanguard/rendering/viewport.hpp
source/rendering/src/viewport.cpp
source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp
source/rendering/include/vanguard/rendering/render_flow_resource_execution.hpp
source/rendering/src/render_flow_resource_resolve.cpp
source/rendering/src/render_flow_resource_execution.cpp
source/rhi/include/vanguard/rhi/rhi.hpp
source/rhi/include/vanguard/rhi/rhi_backend.hpp
source/rhi/include/vanguard/rhi/rhi_types.hpp
source/engine/src/rendering_service.cpp
```

### REDengine

Root:

```text
D:/root/R6.Root/Mainline/dev/src/common/renderer/src
```

Initial core set:

```text
renderNodeGraph.h/.cpp
renderNodeGraphArray.h
renderNodeGraphFactory.h/.cpp
renderGraphCache.h/.cpp
renderNodeJob.h/.cpp
renderNodeImplContext.h/.cpp
renderGraphNodes.h/.cpp
renderRenderFrame.cpp
renderInterface.h/.cpp
renderFlowResourceAllocator.h
renderFlowInternalData.h/.cpp
renderNode_AntyAliasing.cpp
```

Support files will be added only when a core symbol directly requires them.

### Unreal Engine

Root:

```text
D:/UnrealEngine/Engine/Source/Runtime/RenderCore
```

Initial core set:

```text
Public/RenderGraphBuilder.h/.inl
Public/RenderGraphPass.h
Public/RenderGraphResources.h/.inl
Public/RenderGraphDefinitions.h
Public/RenderGraphParameter.h
Public/RenderGraphValidation.h
Private/RenderGraphBuilder.cpp
Private/RenderGraphPass.cpp
Private/RenderGraphResources.cpp
Private/RenderGraphPrivate.h/.cpp
Private/RenderGraphValidation.cpp
Private/RenderGraphTrace.cpp
Private/RenderGraphResourcePool.h/.cpp
```

`RenderGraphAllocator`, blackboard, utilities, and representative call sites are
secondary sets selected when the core path refers to them.

## 8. Required Decision Ledger

Every study set records four categories:

1. **Copy** — the ownership or correctness rule transfers directly.
2. **Adapt** — retain the idea but reshape it around Vanguard's Jobs, RHI, or
   explicit runtime validation.
3. **Reject** — do not reproduce a defect, hidden global, assertion-only rule,
   accidental ordering dependency, or engine-specific burden.
4. **Vanguard-specific** — behavior demanded by existing Vanguard contracts.

A source being mature or production-proven is not enough reason to copy its
surface syntax or hidden assumptions.

## 9. Early Risk Register

The following risks must be resolved before implementation:

- confusing CPU graph order with GPU queue synchronization;
- acquiring a back buffer before the graph can guarantee abandonment on every
  failure path;
- allowing a pass callback or frame reference to outlive retained frame state;
- executing declaration code twice or letting execution mutate the sealed plan;
- culling after resource lifetimes or command scopes have been finalized;
- using unstable worker completion order as graph or allocator order;
- allowing explicit side effects to bypass dependency validation;
- publishing allocator execution packets before graph compilation is atomic;
- failing after some command scopes submit without producing truthful terminal
  receipts and safe resource retirement;
- conflating resource export with viewport presentation;
- caching frame-local mutable state inside a reusable graph definition;
- relying on debug assertions for Shipping correctness;
- importing RED globals or Unreal reflection/threading assumptions wholesale;
- expanding the first stage into batching, visibility, or renderer pass content.

## 10. Durable Artifacts

This project maintains six files:

```text
render-graph-study-plan.md       scope, source routing, questions, and gates
render-graph-design.md           accumulated evidence and authoritative decisions
render-graph-execution-plan.md   staged files, tests, corrections, and exit gates
render-graph-execution-plan-former-v1.md preserved superseded implementation plan
render-graph-resume-checkpoint.md exact current state, verification, and next action
render-graph-authoring-examples.md R9-aligned RED-to-Vanguard authoring examples
```

The design document is append-oriented during research. Earlier provisional
decisions are marked superseded rather than silently rewritten. The checkpoint
is updated at every completed study set and before any pause.

## 11. Current Progress

```text
Original G0/R1-R3/U1-U2 studies                         PRESERVED
Former V1 Vanguard synthesis                            SUPERSEDED BY REVISION R0
Former V2 execution plan                                PRESERVED AS HISTORY
Revision R0 preservation/equivalence ledger             COMPLETE
Revision R1 RED representation and ownership            COMPLETE
Revision R2 RED graph authoring and composition         COMPLETE
Revision R3 RED node declaration and single execution   COMPLETE
Revision R4 RED CPU graph and GPU graph                 COMPLETE
Revision R5 command groups, recording, and submission  COMPLETE
Revision R6 Vanguard resource-allocator seam            COMPLETE
Revision R7 RED implicit-synchronization audit          COMPLETE
Revision R8 cache, cameras, context, and terminal chain COMPLETE
Revision R9 rewrite, regenerate plan, and verify        COMPLETE
```

The RED-faithful revision proceeds in this order:

```text
R0  preserve and reclassify the former synthesis
R1  graph representation and ownership
R2  graph authoring and composition
R3  node declaration and single execution
R4  CPU graph and GPU graph
R5  command groups, recording, and submission
R6  Vanguard resource-allocator seam
R7  RED implicit-synchronization audit
R8  cache, cameras, execution context, and terminal chain
R9  rewrite, regenerate the execution plan, and verify
```

RED is the default Render Graph authority. The only pre-approved architectural
departures are Vanguard's explicit pre-execution resource-use declaration and
one recording execution per scheduled node occurrence. Existing Vanguard
allocator, RHI, Jobs, viewport, service, ownership, and Shipping-correctness
contracts remain authoritative at their seams. Other departures require a
specific RED behavior and a demonstrated synchronization, lifetime, ownership,
or backend-contract reason.

The study is complete. The next authorized action is Stage RG1, Definition
Ownership And RED Authoring Model, in the regenerated execution plan. The
former CPU-pass compiler plan remains historical and must not be implemented.
