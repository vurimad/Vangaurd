# RED Render Graph Port Baseline

Date imported: 2026-09-04

This directory is the permanent mechanical baseline for Vanguard's RED-faithful Render Graph implementation. Files ending in `.red-source` are intentionally unchanged source snapshots and do not compile in Vanguard. A source snapshot remains here after its Vanguard counterpart is translated; translation never consumes or deletes the RED evidence.

The directory is outside the rendering project's `include/**.hpp`, `private/**.hpp`, and `src/**.cpp` file globs. Nothing here is part of a production or test target until an execution-plan stage explicitly makes it so.

## Source

```text
D:/root/R6.Root/Mainline/dev/src/common/renderer/src
```

## Imported Core

```text
render_node_graph.hpp.red-source                 33DBCE13082A095BB724CF80CDACDE919FAB032127BBBAB12D7797DEC8ACECC6
render_node_graph.cpp.red-source                 98BEBCF439C51698B6194EF1450966E32F5ED6A20559DED2EC3C2FC2D2049962
render_node_graph_array.hpp.red-source           6C1FE53A8DBD3B79DBCC40DDE52A85485CA4CC26753C77C71A873A15567FAB06
render_node_graph_factory.hpp.red-source         ED18E368D72AF245C64E98D8D927186816B90112D0D1638B964A7205E2DFA5D4
render_node_graph_factory.cpp.red-source         FF47C138F3D48EF83E1A071C3C8179FC481A4347A31C02941C1C2BE439B58495
render_graph_cache.hpp.red-source                F24870EB257B38D9B3776BC248C5CC04BE30C023F39A8DC19E3C5AD41EA13D78
render_graph_cache.cpp.red-source                9F6F769FD904801CDE3DEDCC8A85C666DC4444F42722564A0289764A69495DC9
render_node_job.hpp.red-source                   F8099083E395668B9F4D6F765704E6F1E21B7C3E22931A252CAD9A8827BA8BA0
render_node_job.cpp.red-source                   1103A6E2917A1C24C8E692FAC9A8722D77186198847E0D3E7E99976E5088D6DA
render_node_impl_context.hpp.red-source          40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A
render_node_impl_context.cpp.red-source          B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7
```

Hashes are SHA-256 values of both the source and imported copy at import time.

Every imported filename preserves the RED component name, changing only to Vanguard snake_case and `.hpp` conventions. The `.red-source` suffix keeps the byte-exact evidence inert because Vanguard's Premake audit rejects foreign source-lineage headers in active engine source extensions.

```text
renderNodeGraph.h          -> include/vanguard/rendering/render_node_graph.hpp
renderNodeGraphArray.h     -> include/vanguard/rendering/render_node_graph_array.hpp
renderNodeGraphFactory.h   -> include/vanguard/rendering/render_node_graph_factory.hpp
renderNodeGraph.cpp        -> src/render_node_graph.cpp
renderNodeGraphFactory.cpp -> src/render_node_graph_factory.cpp
```

RG1's representation/ownership/factory/composition translation is complete at the source-comparison gate. `render_node_graph_array.hpp` preserves RED's masked-ID storage and separate usage-order array. The graph and factory counterparts preserve RED's direct topology, composition, external node-container, `NodeGraphFactory`, `NodeGroupId`, group-tag, lazy dummy-boundary, and ordered command-list-child model. Later execution, allocator/RHI, Jobs, profiling, and cache behavior remains deliberately untranslated until its owning RG stage.

RG2 is complete at its source-comparison gate: `render_node_impl_context.hpp/.cpp` preserve RED's per-node identity and dispatcher-thread copy shape, while `RenderNodeImpl::Process()` owns the single preparation-versus-execution branch required by Vanguard's separate `DeclareResources()` callback. `RenderNodeGraph::PrepareResourcesParallel()` preserves RED's deterministic bucketed preparation traversal and command-list groups preserve ordered child declaration on one writer.

RG3C establishes the approved allocator seam. Graph preparation calls RED-shaped `RequestBeginQueue`, `RequestEndQueue`, and `RequestQueueSync` operations directly on `RenderFlowResourceAllocator`; direct `Resolve()` consumes those requests and closed planning writers without a survivor overlay, graph definition, or caller-authored queue schedule. Only real command-list owners receive planning writers and execution packets. Packetless Sync declarations still register their submission boundary. Execution-side Sync submission remains deliberately untranslated until its owning stage.

RG3E is complete at its source-comparison gate. The node context exposes RED-shaped typed resource calls backed by frame-owned occurrence bindings, while ordinary nodes can no longer access the allocator or writer directly. Command-list groups privately register queue begin/end, and `RenderNodeSynchronize` privately registers queue sync. Representative authoring shapes are kept in the authoring document instead of adding dead renderer nodes. Command recording and sync submission remain RG4 work.

RG3D closes the allocator-seam correction pass without translating later node execution. Queue requests cannot race past planning seal, queue requests participate in the aggregate operation budget, stale survivor/output artifacts are removed, and RED's default Graphics behavior remains intact for ordinary command-list owners outside explicit Compute groups.

RG3E.2 closes the node-resource facade at its source-only gate. `RenderNodeImplContext` now exposes RED-shaped typed allocation, temporary allocation, use, swap, and decision calls while generation-local identities live only in the retained frame's per-occurrence `RenderNodeResourceBindings`. RED's cross-node and nested named uses are preserved as ordered named allocation scopes; each concrete node also receives automatically balanced packet-local accesses for safe command recording. `RTTexture` and `RTBuffer` return liveness-checked witnesses rather than RED's cacheable raw allocator getters. RG3E.3 owns representative feature-node translation and removal of the temporary public raw writer/allocator escape hatch.

RG4A has started with the frame-lifetime seam. The command system now creates one stable reference-counted retained-frame allocation, and `RenderFrameContext` owns and can share that allocation instead of borrowing a frame stored in the root task capture. Locally prepared view-family, frame custom-data, per-occurrence resource bindings, and preparation-failure state live in the same allocation. The job-visible current-frame slot is translated but remains unwired until graph-branch joining and terminal cleanup can prove its complete borrowed lifetime. Execution-side node processing remains the next RG4A slice.

RG4 is complete at its source-only boundary. The renderer now owns a private cache-entry-neutral execution handoff, reserved `StorageData` recording, direct allocator declaration and Resolve sequencing, packet publication, node kickoff, retained branch joining, graph-lock release, and exact job-visible frame cleanup. Cache selection remains RG6-owned.

RG5A closed synchronization-node recorder and queue-dependency disposition. The frame command-list container submits the same contiguous indexed ranges, while compiled scope, queue, producer, consumer, and sync identity are registered privately after allocator publication rather than added to the node-facing command-list API. Each Fork/Join must match its exact compiled flow boundary before the existing RHI submission performs the queue wait. Empty dependency-free ranges are no-ops, native-limit overflow fails before submission, and joined leftover scopes or dependencies become discard receipts. Both Fork and Join are acknowledged by the producer-side synchronization submission; later consumer submission remains separate completion evidence. An unexplained leftover is a missing authored final Sync; the renderer does not synthesize a tail submission.

RG5 is complete at its source-only gate. The joined frame terminalizer produces the exact allocator receipt, preserves first failure, closes graph and Jobs-frame ownership, and publishes frame completion statistics only at the true terminal boundary.

RG6A translates `render_graph_cache.hpp/.cpp` directly into `render_graph_cache.hpp/.cpp`. The same four-entry linear lookup, oldest-entry replacement, final graph ownership, per-camera graph/node ownership, camera-setup reuse, and post-build temporary-graph clearing are preserved. Vanguard's complete structural keys replace hash-only correctness, allocation failure leaves an entry invalid, and `FrameRenderer` owns the cache directly. Frame-specific key construction and the renderer build/composition body remain RG6B.

RG6B has started by translating frame/view key construction and direct cache selection into `FrameRenderer`. The immutable frame packet now carries its exact Presentation, Texture, or Headless output kind; prepared view identity maps ordered camera-dependency endpoints into the full structural key. The zero-view blank builder preserves the source node order, command-list grouping, final synchronization, composition, flow-group build, and cache publication. Its frame-global and blank feature nodes are explicit no-op placeholders for their dedicated implementation passes. The renderer-owned HitProxies, GBufferOnly, Camera, SafeMode, NoScene, Todvis, and DebugVisualization builder entry points and the prepared-view `FindCameraSetup`/`DoesNeedRebuild` selection body are also present. Their node topologies remain deliberately unimplemented, and the frame fails before composing or publishing such a graph.

## Referenced But Not Yet Imported

These remain primary evidence at their RED source paths and will be imported only when the relevant port stage reaches their seam:

```text
renderGraphNodes.h/.cpp       renderer feature-node declarations and implementations
renderRenderFrame.cpp         frame orchestration and graph integration
renderInterface.h/.cpp        renderer ownership and lifecycle
renderFlowResourceAllocator.h allocator-facing RED contract
renderFlowInternalData.h/.cpp allocator internals
renderNode_AntyAliasing.cpp    representative authored feature graph
```

## Port Rules

1. Preserve RED behavior and structure during mechanical conversion. Do not redesign while resolving names, containers, ownership, or Jobs APIs.
2. Make every intentional departure explicit against `docs/development/render-graph-design.md` Section 51.
3. Keep Vanguard's deliberate seams: the existing `RenderFlowResourceAllocator`, RHI, Jobs, cache-entry lifetime for the composed graph and its external node containers, explicit queue synchronization and receipts, Shipping-active validation, and same-frame graph-owned presentation.
4. Convert files in bounded execution-plan stages. Do not import the complete renderer feature-node library to satisfy a core dependency.
5. Record temporary compatibility adapters and remove them before the RG7 exit gate.
6. Keep the RED filename/component boundary during translation. Do not split one RED file into newly invented public components unless a demonstrated Vanguard seam makes that unavoidable and the departure is approved explicitly.
7. Do not add this directory to Premake wholesale. The port remains inert; Vanguard counterparts enter production paths only through an explicit promotion step.
8. Do not compile or run tests during the file-by-file mechanical translation passes. Validation begins only at an explicitly authorized integration gate after the required RED files have been translated.

## Approved RG1 Behavioral Correction

RED's group boundary nodes capture only members registered before the boundary is first requested. Vanguard preserves the same factory API and lazy dummy nodes, but `Register` also connects a newly registered member to already-existing input/output boundaries. This closes RED's late-membership hole without adding a public seal, builder, or alternate group model.
