# Vanguard Render Graph Architecture Study

Date: 2026-09-03

Status: RED-faithful study revisions R0 through R9 are preserved. Section 52 is
the current implementation contract after the RG1-RG3E direct-graph correction.
G0, RED, Unreal, R0-R9, and the former V1 synthesis remain evidence and history.

## 1. Authority And Scope

This document preserves the evidence and decisions for Vanguard's future Render
Graph and render-node machinery. It begins where the completed
`RenderFlowResourceAllocator` stops.

The allocator remains authoritative for logical resource plans, physical
assignment, alias activation, resource-state actions, execution packets, and
terminal resource receipts. Section 52 is authoritative for graph topology,
node compilation, CPU scheduling, command-scope construction,
queue-dependency lowering, frame-output handling, and the executor's terminal
transaction.

When reading the preserved study sections:

- observed source behavior is evidence;
- early Vanguard conclusions are historical where they conflict with Section 52;
- unresolved choices remain explicit;
- only Section 52 and `render-graph-execution-plan.md` approve implementation direction;
- superseded decisions will be marked instead of silently deleted.

The study protocol and source routes are defined in
`render-graph-study-plan.md`.

## 1A. RED-Faithful Revision Authority And Preservation Ledger (R0)

The former V1 synthesis in Sections 29-41 is preserved because its allocator,
RHI, ownership, failure, presentation, and validation work remains useful. It
is no longer the current Render Graph authority. In particular, its
one-pass-per-command-scope recording model and its rejection of RED's authored
node/group structure are withdrawn pending the RED-faithful revision.

The revision uses the following authority order:

1. RED is the default authority for Render Graph representation, node
   machinery, graph construction, command-list grouping, CPU/GPU dependency
   flow, per-camera composition, caching, and the frame terminal chain.
2. Vanguard's existing `RenderFlowResourceAllocator`, RHI, Jobs, viewport, and
   service contracts are authoritative at their integration seams.
3. RED's planning/consume execution branching is not copied. Resource uses are
   declared before allocator `Resolve`, and a node's recording `Execute()` runs
   exactly once.
4. RED behavior may otherwise change only for a demonstrated synchronization,
   lifetime, ownership, Shipping-correctness, or backend-contract defect. Every
   such correction must name the RED behavior and the reason for changing it.
5. Unreal is a secondary correctness reference for hazards, barriers, resource
   lifetime, and queue synchronization. It does not determine Vanguard's graph
   representation or renderer flow.

No former conclusion is silently deleted. Evidence from revision R1-R8 must
resolve each `Re-study` row below during the R9 rewrite before the execution plan
or authoring examples become implementation authority.

| Former V1 decision family | Preserved location | R0 classification | RED-faithful revision rule |
|---|---|---|---|
| Renderer ownership of graph state | Sections 29, 35, 38, and 39 | Keep | Retain `FrameRenderer` ownership unless the end-to-end RED lifetime trace proves a different owner is required. |
| Bounded structural graph cache and frame-local data | Sections 29 and 39 | Keep, align to RED | Preserve immutable lifetime safety, but reproduce RED's bounded lookup, per-camera build data, invalidation, whole-frame composition, and tick-based recency before adding Vanguard-specific behavior. |
| Reusable polymorphic render-node machinery | Sections 29 and 30 reject it | Replace with RED | Re-study `CRenderNodeGraph`, `NodesContainer`, node builders, node implementations, and execution contexts; adopt the RED responsibility split with Vanguard ownership safety rather than rejecting the model. |
| Pass as the only first-class scheduled unit | Sections 29 and 30 | Replace with RED | Restore authored render nodes, groups, subnodes, and command-list groups. Determine exact CPU-only, GPU-recording, and mixed-node behavior from RED. |
| One surviving pass per initial command scope | Sections 29, 33, and 40 | Replace with RED | A RED-authored command-list group is the initial Vanguard command scope and may contain multiple ordered node packets on one recorder. |
| Automatic scope merging as the route to shared recording | Section 33 | Replace with RED | Explicit authored groups establish shared recording. Cross-group merging, if ever useful, is a later optimization and cannot erase authored boundaries. |
| Separate CPU scheduling and GPU submission meaning | Sections 33, 35, 38, and 41 | Keep, align to RED | Reconstruct RED's `LinkCPU()` and `LinkGPU()` behavior exactly, then make any implicit cross-domain or cross-queue synchronization explicit. |
| Declaration ordinal as only an independent-pass tie-break | Sections 31 and 33 | Re-study | Inside an authored RED command-list group, subnode order is authoritative. Ordering between groups follows RED's explicit graph links and validated queue submission rules. |
| Canonical typed resource-use declaration | Sections 30, 31, and 34 | Keep as allocator seam | Nodes declare complete texture, buffer, view, state, and access uses before execution; the canonical records drive allocator planning and packet replay without a planning execution of the node. |
| Public exact content-version handles | Sections 31, 32, and 40 | Re-study | Retain only if required for Vanguard allocator correctness or valuable internal validation. Do not force Unreal-style public authoring onto the RED node/group model without evidence. |
| Resource-derived RAW/WAR/WAW graph topology | Sections 31 and 32 | Adapt beneath RED | RED's authored CPU/GPU topology remains primary. Resource analysis validates it, supplies required missing hazard constraints where policy allows, and rejects contradictions or unsupported synchronization. |
| Reverse-root culling and implicit epilogue construction | Sections 32 and 40 | Re-study against RED | Added RED nodes and terminal stages remain present by default. Any culling must reproduce RED behavior or require an explicit cullable contract; it cannot silently remove parameterless or side-effecting nodes. |
| Named side-effect, Present, and ExternalWrite roots | Sections 32 and 37 | Re-study at the seam | Preserve truthful ownership and terminal evidence, but translate RED's explicit final nodes and graph links before deciding which compiler-root vocabulary remains necessary. |
| Queue-neutral three-queue validation and truthful wait/signal lowering | Sections 33, 36, and 40 | Keep as correctness layer | Preserve RED's group/queue flow while rejecting any cross-queue ordering that the Vanguard RHI cannot lower to real signal/wait evidence. CPU order never fabricates GPU synchronization. |
| Allocator planning only after final graph topology | Sections 32 and 34 | Keep | Translate the completed RED-style graph into allocator writers, flow groups, command scopes, queue dependencies, and ordered per-node packets before `Resolve`. |
| Node execution at most once | Sections 30, 34, and 40 | Keep as required adaptation | Split RED discovery/planning responsibilities into declaration and preparation contracts. The recording `Execute()` has no planning/consume phase branch and runs once. |
| Retained execution envelope and exactly-once terminalization | Sections 35 and 36 | Keep | Re-map it onto RED's job and terminal-node sequence without weakening ownership, partial-submission evidence, or allocator `Finish` guarantees. |
| Submission receipts, abort classification, and device loss | Section 36 | Keep | RED determines the visible submission flow; Vanguard retains full RHI receipts and never relabels submitted or ambiguous work as discarded. |
| Presentation and external-output ownership | Sections 37 and 38 | Keep safety, corrected by Section 44 | Present is a terminal node of the current frame's Render Graph, matching RED. Main-thread acquisition and lifecycle reconciliation share explicit per-viewport serialization with worker-side Present/Abandon; there is no next-frame presentation drain. |
| Existing service and RenderPath integration | Sections 35 and 38 | Keep, verify against RED | Retain the existing scheduler and owners, but verify that the graph extends the render chain in the same dependency shape as RED rather than imposing an independently designed executor topology. |
| Owned node data, generation validation, typed failures, budgets, and diagnostics | Sections 30, 35, 36, and 39 | Keep as safety | Apply these guarantees to the RED-style graph objects without changing their architectural roles. Shipping validation remains mandatory. |
| Former post-baseline deferral list | Section 39 | Re-study | Reclassify every deferred item after the RED execution flow is reconstructed; do not defer authored grouping, node machinery, or CPU/GPU links merely because the former pass compiler did. |
| Unreal-led one-shot pass builder as the public architecture | Sections 29-33 and 40 | Replace as primary model | Keep Unreal only as a hazard/barrier/synchronization cross-check; RED supplies the public graph, node, grouping, caching, and execution structure. |
| Former V1 exit claim | Section 41 | Withdraw | V1 is not architecturally complete. Completion requires the RED R1-R8 evidence passes, a rewritten authoritative synthesis, and a regenerated execution plan. |

R0 is a preservation and authority correction only. It approves no final API
spelling and changes no implementation. The next study phase is R1: RED graph
representation and ownership.

## 2. G0 Vanguard Source Snapshot

This snapshot establishes the current integration boundary before RED or Unreal
is allowed to shape the design. Line references in the G0 sections refer to
these exact files.

```text
source/rendering/include/vanguard/rendering/frame_renderer.hpp
  lines: 42
  sha256: AEEF9A1B89C742DD72249FF038238041CCCA350ED911CD3719BA8AC4A929DE91

source/rendering/src/frame_renderer.cpp
  lines: 134
  sha256: 453DF26603E3ABC2CE25A42E61D7D7382186D243CEC0CCC6DAB9C4E96FA4A776

source/rendering/include/vanguard/rendering/render_command_system.hpp
  lines: 219
  sha256: DBC14793B2404ABBB17B124349687178863368B2FED63265C604ABE74D1F7F85

source/rendering/src/render_command_system.cpp
  lines: 599
  sha256: 20BE35EF200F1C17B30E68FCF022CAAF6126B7F21FDB397679BB68D40D4BF15E

source/rendering/include/vanguard/rendering/viewport.hpp
  lines: 482
  sha256: 307EB964E026E5241FA342633F7312DC86301C520CFA56FC96A5B3E9A2F2BE93

source/rendering/src/viewport.cpp
  lines: 929
  sha256: E90E9B610B43BE87FA0E8484C5C5CFA5269B0E3FC27924CEB3AAEFDC47C4A282

source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp
  lines: 642
  sha256: 4E3E90B39935DFF477973F493DD821B9CF1537561CF3E951D634CBBD7C333CE2

source/rendering/include/vanguard/rendering/render_flow_resource_execution.hpp
  lines: 282
  sha256: DA34ABA872631B5B3FB484C36F20F68C539BAD53FDE483D4C862A532AE728122

source/rendering/src/render_flow_resource_resolve.cpp
  lines: 1924
  sha256: 6528C39A12E7DCCE5504F429153F5AF3CDDCB99053286790CE1CB467D1574AC3

source/rendering/src/render_flow_resource_execution.cpp
  lines: 1053
  sha256: 1E1D1C8ED7BB19CAA80D41ABD23540D33E0977230C031A8C7737D7220A3D579B

source/rhi/include/vanguard/rhi/rhi.hpp
  lines: 423
  sha256: 5A70B586A0CDACB35328C50B94431E34283A350E28A9470BDF8E384BAC0D6C7B

source/rhi/include/vanguard/rhi/rhi_backend.hpp
  lines: 208
  sha256: 1D574F51293189BE9FD970C662041B1319FDC1D7F8F00AA1CD7F07C2F3F15B12

source/rhi/include/vanguard/rhi/rhi_types.hpp
  lines: 1796
  sha256: E79C816B92AD04A7BAF46A55D915C7C91166C452F2976C9F503A3115448726A9

source/rhi/src/rhi.cpp
  lines: 2302
  sha256: 66205D1454B85FAB5D0B85ED676CF54567D7FCE1C76BB745AE1CC4AE9FA4E927

source/rhi/nvrhi/src/d3d12_backend.cpp
  lines: 2809
  sha256: 9569523D71608B72DBBF8E130EB9F037794948FC80800223D00C1BC620FA4017

source/rhi/README.md
  lines: 119
  sha256: 0EF833262B5CA97A4C3C6EDB3BDDE04BBDCC180526DE3C5EC1D9467007F965F3

source/rhi/tests/rhi_tests.cpp
  lines: 1275
  sha256: 13485AAB6ECBE1AD880CE638B66D23C3DCC6C0533349A71DF660595C02E6CC7D

source/rhi/nvrhi/tests/d3d12_backend_tests.cpp
  lines: 2528
  sha256: 456291AC23B8C9EB47256288EC667AD52B75E8EED3767943128D96EC7A185B38

source/engine/src/rendering_service.cpp
  lines: 843
  sha256: CE4877441BEB2A3980FE40135ACC64ECC927ADACAB0D127708117241BDBCB927

source/engine/include/vanguard/engine/frame_pipeline_service.hpp
  lines: 207
  sha256: 66FD8957A2C23DDEC31C297423D18524760E82DE55ADF12285809AA34867324B

source/engine/src/frame_pipeline_service.cpp
  lines: 612
  sha256: DAD69E394755A9C6A1F75A599384F0973E84FFC28F42CBEFAF837B4C7802996D

source/rendering/include/vanguard/rendering/render_phase.hpp
  lines: 211
  sha256: 2C81BA20717409D8AE2825FB4384B55F069A63385128D9164E031B967BD6F627

source/rendering/include/vanguard/rendering/render_camera.hpp
  lines: 365
  sha256: 750EA7FD5B4BA0868BD6D0F4A77E4583694877F970AD3C5A0714D00DAABE0D03

source/rendering/src/render_camera.cpp
  lines: 1823
  sha256: 6995D224A20E4CD6C5570352B00C5BBE363E92421A8EEB777E6F03C453AEF62C

source/rendering/include/vanguard/rendering/render_scene.hpp
  lines: 698
  sha256: AFA0ABA9384C328950E8AD692F4B0C8A9B8E96EBD9C16FA317521EBB56A242D0

source/rendering/src/render_scene.cpp
  lines: 2761
  sha256: D400182BEDDE63B026E35E3991E185D67BB9970BFD7D465DE221A6BC9C1BFBD7

source/rendering/include/vanguard/rendering/gpu_scene_visibility.hpp
  lines: 186
  sha256: BFB61B31B10EDED79A970BA0AEC00EA17B30E80D52D235252AC18B0063BDEBE1

source/rendering/include/vanguard/rendering/gpu_scene_runtime.hpp
  lines: 147
  sha256: EA24EF94D1EC0327DFE9854F1C4A3B319582E150C7AE74C673F207F608EB0233

source/rendering/src/gpu_scene_runtime.cpp
  lines: 834
  sha256: 3F12B5CDC5DE56470774E7AB717312419E6C6D9D56F504E2F747BBCC366E8FC0

source/rendering/src/render_pipeline_factory.cpp
  lines: 292
  sha256: FD7C3AAC71D127B026441C28673427153E3965E0288068C27C87D3E41A184C17

source/rendering/include/vanguard/rendering/presentation_service.hpp
  lines: 152
  sha256: 98AD726D8FAC0C08155B51C1F13386CB52145A94DC282A2207424D30043E17CD

source/rendering/src/presentation_service.cpp
  lines: 524
  sha256: 4F25A23C5B81354EC005ADC8504024E1F9C384707B505F9798C305EBB4AC3B9E

source/rendering/src/gpu_scene_upload.cpp
  lines: 580
  sha256: F0FFEC85BCA490DE96AA300526E52CB8E16914A7568D24961AC10A1FA909547F

source/rhi/nvrhi/src/common_backend.cpp
  lines: 4068
  sha256: 57DD0FDB392F8E10D354D6AA2FD7D3A85477EA10DED6EBEF7BDABC674BD0A185

source/jobs/include/vanguard/jobs/jobs.hpp
  lines: 202
  sha256: 8FAEF445D0AC7D26DAC6F057712255A3FDE0F156434E26B457591AC751347703

source/jobs/src/jobs.cpp
  lines: 273
  sha256: E17384144BCC27F6DC24EA0922D609F9A68694AD8C5A35276B4873583B18429A

source/jobs/compat/red_jobs_backend.cpp
  lines: 621
  sha256: 0A3D883BB146A26C836896AED77BCEEC801066A50C93F935E37F1C7FDAA928EB

source/jobs/tests/jobs_tests.cpp
  lines: 363
  sha256: 8B96D82D4FF81A72DF55F68AC52B1A6E1770472F71DBA4290C2FC4A8A2399354

source/imported/common/redJobs2/include/jobBuilder.h
  lines: 325
  sha256: 4221D0FDC158AE549C8239B11BB2EA52D64E9D9FECA5A76C24A5D48357F46046

source/imported/common/redJobs2/src/jobBuilder.cpp
  lines: 142
  sha256: C8D45DE498D1044584BE33EBAE9B872B093E89F69E6A23463BC29AF05F71BD26
```

The working tree already contains ongoing material/runtime work. This study
must not treat unrelated diffs as Render Graph changes.

## 3. Existing Ownership Is Already Sufficient

`RenderingServiceImpl` owns the render-facing systems directly, including
`FrameRenderer`, `RenderCommandSystem`, and `ViewportManager`
(`rendering_service.cpp:758-777`). `FrameRenderer` in turn privately owns
`RenderFlowResourceAllocator` (`frame_renderer.hpp:38-40`). Stage 5 initializes
that allocator immediately after successful RHI initialization and shuts it
down before the RHI (`rendering_service.cpp:393-418,490-570`).

Provisional decision:

```text
RenderingServiceImpl
  -> FrameRenderer
       -> RenderGraph runtime               future owner
       -> structural-template cache         required baseline owner
       -> RenderFlowResourceAllocator       existing owner
  -> RenderCommandSystem                    existing CPU-chain owner
  -> ViewportManager                        existing output owner
  -> RHI                                    existing GPU/submission owner
```

The future graph runtime belongs inside `FrameRenderer`. It may receive
non-owning references to viewport/RHI-facing services through a narrow frame
transaction, but it must not become an engine service or global singleton.

`RenderCommandSystem` remains the only owner of the serialized CPU render tail.
The graph executor appends work through the supplied Jobs continuation; it does
not keep an independent scheduler or background tail.

## 4. Current Frame Lifetime And The Missing Execution Object

`RenderCommandSystem::RenderFrameDispatcher::Submit` validates the frame,
copies it into `RetainedFrame`, retains any opaque frame payload, and captures
that owner in one RenderPath task (`render_command_system.cpp:185-251`). The
task constructs `RenderFrameContext` over the retained copy and calls
`FrameRenderer::RenderFrame` (`216-233`). It is ordered behind the existing CPU
tail (`237-248`).

`RenderFrameContext` exposes:

- a reference to the retained `RenderFrameInfo`;
- the dispatcher worker index;
- a Jobs builder continuing the existing render chain
  (`render_command_system.hpp:117-154`).

`FrameRenderer::RenderFrame` currently prepares or reuses a view family,
prepares custom data, verifies readiness, creates a `FrameCustomData` retention
handle, and then deliberately returns the missing-graph failure
(`frame_renderer.cpp:51-74`).

The callback-local objects are not sufficient for a real graph. The future
executor will dispatch work that can continue after the callback's stack is
gone. It therefore needs one frame-execution owner that retains:

- scalar frame facts copied from `RenderFrameInfo`;
- the prepared view family and `FrameCustomData` lease;
- the selected graph definition/instance;
- the allocator session and published execution generation;
- every command-scope recorder and terminal receipt slot;
- the acquired frame-output transaction;
- the first structured terminal failure.

That owner must remain alive through the executor epilogue. A child task must
never capture `RenderFrameContext`, a callback-local `PreparedRenderViewFamily`,
or a raw `RenderFrameInfo&`.

Success commits the prepared view family exactly once. Any pre-commit failure
releases it without commit. This is separate from allocator completion and
separate again from presentation.

## 5. CPU Completion And Failure Propagation

The existing command system has one strong CPU boundary:
`FlushPreviousFrameProcessing` waits the current `cpuTail`, then clears it
(`render_command_system.cpp:550-562`). `RenderingServiceImpl::RenderUpdate`
performs that flush before collecting asynchronous subsystem failures and
retirements (`rendering_service.cpp:681-737`).

The command system also owns a bounded, thread-safe first-failure latch
(`render_command_system.cpp:128-171`). Today the outer RenderFrame task reports
only the status returned directly by `FrameRenderer::RenderFrame`
(`216-233`). A graph failure discovered by a later child or terminal epilogue
cannot safely point at a transient failure message and cannot be lost after the
outer callback returns.

Provisional requirements:

1. The graph execution owner stores a copied structured failure with stable
   message storage.
2. The terminal epilogue reports the first failure into the existing command
   system failure channel.
3. `cpuTail` must cover the terminal graph epilogue, not merely the initial
   graph-dispatch callback.
4. Quiesce and the next `RenderUpdate` observe graph completion through that
   same tail.
5. No graph task calls `WaitOnProcessFrame`; waits belong to the existing main
   lifecycle boundary.

The Jobs-semantics question is resolved. `RenderFrameContext` constructs its
builder from the active `JobContext` (`render_command_system.hpp:117-154`). The
backend maps that constructor to RED Jobs' continuation builder
(`red_jobs_backend.cpp:380-400`). RED's builder stores the running job's
continuation counter and, on destruction, links its final child counter into the
parent (`jobBuilder.cpp:35-39,56-84,132-139`). Vanguard's Jobs test explicitly
waits the originally extracted parent counter and verifies that a child spawned
through a continuation builder completed before it became ready
(`jobs_tests.cpp:205-229`). This is direct executable evidence that the existing
`cpuTail` covers graph children dispatched through `context.GetBuilder()`.

Hard execution invariant:

> Every asynchronous graph task, including the one terminal epilogue, must be
> dispatched through the supplied continuation builder or a descendant
> continuation. The graph must not extract that continuation builder's counter,
> replace it with an unrelated builder, or create an independent CPU tail.

`jobs::Builder::ExtractCounter()` is intentionally unsuitable on a continuation
builder: the imported implementation replaces the extracted child counter with
a counter tied to the running continuation (`jobBuilder.cpp:108-129`). The
graph may use separate ordinary builders for compiler-internal sub-DAGs only if
their terminal counters are added back to the supplied continuation before the
callback returns. The simplest first implementation is to use the supplied
builder as the sole root and let its scoped destructor perform the parent link.

## 6. Frame Output Is A Retained Current-Frame Transaction

`ViewportManager::AcquireOutput`, `AbandonOutput`, and `Present` currently all
reject a non-main-thread caller (`viewport.cpp:462-533`). Presentation
acquisition owns an `rhi::AcquiredBackBuffer`; texture outputs carry a
non-owning texture ref without a back-buffer token (`475-489`). A presentation
token must be consumed by either `Present` or `AbandonOutput`.

`FrameRenderer::RenderFrame` executes on an `AnyWorker` RenderPath task, so
acquisition and mutable viewport/swap-chain lifecycle work stay before dispatch
on the main thread. Present, abandonment, device-loss consumption, and texture
completion become thread-safe terminal operations serialized against those
main-thread operations by the per-viewport presentation gate selected in
Section 44. `ViewportManager::SubmitFrame` currently forwards the frame to
`RenderCommandSystem` and immediately clears the caller's frame, but it does not
yet acquire output (`viewport.cpp:678-711`).

This is a required pre-graph integration seam, not optional polish.

Selected transaction:

```text
main-thread SubmitFrame
  -> acquire exact RenderOutputAcquisition
  -> attach it to an owning retained-frame transaction
  -> dispatch the existing RenderCommandSystem task

RenderPath graph execution
  -> register acquired texture as a retained allocator import
  -> render and transition it to Present when applicable
  -> submit scopes and validate the allocator terminal receipt
  -> execute the current-frame terminal Present/CompleteOutput operation
  -> on failure execute the matching Abandon/DeviceLost terminal path
  -> consume the ticket exactly once before the graph tail completes
```

If dispatch fails synchronously, `SubmitFrame` must abandon the acquisition
before returning. If native work was issued and fence signaling then fails, the
result is device loss/unknown completion, never an unsubmitted discard.

There is a second, more subtle output constraint. A swap-chain texture cannot
be finalized through the allocator's ordinary `TransitionTexture` action.
`rhi::TransitionSwapChainPresent()` requires the exact
`AcquiredBackBuffer` token (`rhi.cpp:2217-2223`). The D3D12 backend records the
Present transition, retains the swap chain, and installs a submission callback
that acknowledges the acquisition (`d3d12_backend.cpp:2668-2699`). `Present()`
then rejects the token unless that special transition was both recorded and
submitted (`2702-2723`). The RHI tests exercise this exact sequence
(`rhi_tests.cpp:1211-1214`; `d3d12_backend_tests.cpp:1635-1668`).

Consequently, none of these shortcuts is valid:

- ask the allocator for a generic `RenderTarget -> Present` action and also
  call `TransitionSwapChainPresent()`; that records two transitions;
- leave the allocator's declared terminal state at RenderTarget and perform the
  special transition silently; that makes the compiled terminal contract false;
- pass only the acquired texture to the graph; the RHI also needs the exact
  acquisition serial carried by the token.

The selected integration contract is a typed presentation import whose terminal
allocator action is `TransitionSwapChainPresent(acquisition)` while the
allocator still records Present as the truthful terminal state. The retained
frame execution owns the exact output transaction. The current-frame terminal
graph node consumes it through the serialized `ViewportManager` ticket API; it
is not returned to a next-frame presentation drain. This requires a small,
explicit allocator/executor integration seam, not a redesign of physical
allocation.

## 7. RHI Queue And Submission Reality

The RHI exposes graphics, compute, and copy queues. `CommandListType::Compute`
maps to compute and `CopyAsync` maps to copy; other current list roles map to
graphics (`rhi_types.hpp:295-323`). A submission returns
`SubmissionReceipt { residency, completion, workSubmitted }`
(`rhi_types.hpp:1660-1705`). The `workSubmitted` bit deliberately distinguishes
post-execution fence-signal failure from work that was never issued.

The current synchronization surface is narrower than a general three-queue
DAG:

```text
CommandListSyncType::None
CommandListSyncType::ForkAsyncCompute   graphics -> compute
CommandListSyncType::JoinAsyncCompute   compute  -> graphics
```

The NVRHI backend implements those waits during serialized submission. Copy
command lists can be executed and fenced, but there is no public explicit
copy-to-graphics, graphics-to-copy, copy-to-compute, or compute-to-copy wait
primitive in this snapshot.

Consequences:

- each graph command scope must be homogeneous: one queue identity and one
  allocator `CommandScopeExecutionReceipt`;
- same-queue scope order may rely on queue submission order;
- the first usable graph may use graphics-only scopes safely;
- the current bounded graphics/compute fork/join model may be enabled only when
  the compiled schedule exactly matches the submitted sync operations;
- cross-queue Copy dependencies must fail compilation until the RHI exposes a
  truthful executable wait edge;
- graph topology or a CPU dependency must never be accepted as proof of GPU
  ordering.

The graph IR should preserve all three queue identities so a later RHI
extension does not require redesign. The first compiler must nevertheless
reject schedules it cannot lower today.

## 8. Existing Allocator Contract The Graph Must Drive

The public allocator state machine already defines the graph handoff
(`render_flow_resource_allocator.hpp:575-607`):

```text
BeginFrame(frameSerial, policy)
  -> register retained imports and reserve exports
  -> CreatePlanningWriter(node, flowGroup, commandScope) per execution unit
  -> RequestBeginQueue / RequestEndQueue around command-list groups
  -> RequestQueueSync at explicit submission boundaries
  -> join all declarations
  -> SealPlanning(join token)
  -> Resolve(creation jobs)
  -> join any physical creation jobs
  -> BeginExecution()
  -> PacketFor(node)
  -> execute/finalize every compiled packet
  -> Finish(terminal receipt)
```

The execution generation exposes compiled queue dependencies, and each packet
records its stable node, flow group, command scope, and queue
(`render_flow_resource_execution.hpp:139-186`). An execution cursor validates
the bound command list and consumes typed use-begin/use-end steps exactly once
(`256-280`).

Terminal completion requires:

- one receipt for every compiled command scope;
- acknowledgements for compiled queue dependencies;
- a generation-bound CPU terminal join token;
- `Completed`, `Aborted`, or `DeviceLost` classification
  (`render_flow_resource_execution.hpp:20-93`).

Graph preparation therefore cannot expose only a list of nodes. Before Resolve
it must register directly with the allocator:

- stable allocator node IDs;
- deterministic GPU flow-group order;
- homogeneous command-scope IDs and queues;
- RED-shaped queue begin/end and Sync requests from which executable dependencies are compiled;
- a terminal receipt layout that survives partial submission.

## 9. Final Graph Preparation Must Precede Resource Resolve

The complete composed `RenderNodeGraph` is the selected executable graph, as in RED. Structural feature choices happen while that graph is built; production does not perform a second resource-root culling pass and does not send a survivor overlay back into the allocator.

Required order:

```text
build and validate the composed graph
  -> assign GPU flow groups
  -> BeginFrame on the allocator
  -> prepare every resource-recording graph occurrence
  -> register command-group queue boundaries and explicit Sync requests
  -> SealPlanning
  -> call allocator Resolve directly
```

Removing a pass after allocator Resolve would invalidate lifetimes, transitions,
alias decisions, packet exhaustion, and terminal scope receipts. It is not a
legal optimization phase.

The graph must separately classify:

- outputs/extractions that make a producer live;
- presentation as an external terminal side effect;
- explicit never-cull work;
- resource-independent explicit ordering;
- debug/instrumentation passes that are enabled by frame policy.

## 10. Producer Systems Waiting For The Graph

The existing GPU Scene path already uses the shared CPU chain for publication.
Its documentation reserves future graph passes for counter clears, descriptor
materialization, copy/compute dispatch, and visibility without giving the graph
semantic knowledge of those operations. Mesh residency, material residency,
and scene binding remain persistent producers/consumers; they do not transfer
ownership of their caches to the graph.

Provisional rule:

> Renderer features contribute opaque pass declarations plus typed resource
> accesses. The graph interprets dependencies, queues, and lifetimes, not
> lighting, material, visibility, or streaming semantics.

This keeps the first graph milestone small. A no-op/clear/copy test pass can
prove the machinery before the production visibility and draw pipeline is
connected.

## 11. G0 Critical Gaps

The following gaps are expected graph-project work rather than allocator bugs:

1. No Render Graph definition, builder, compiler, instance, or executor exists.
2. No render-node/pass API separates declaration from execution.
3. No graph-owned frame execution object retains child-job state.
4. The command system cannot yet receive a late terminal graph failure through
   a deliberate public/private seam.
5. Viewport output is not acquired into the retained render-frame transaction.
6. Dispatched output transactions do not yet have the thread-safe current-frame
   Present/CompleteOutput/Abandon terminal path required by Section 44.
7. No compiler derives roots, culls passes, or produces the allocator survivor
   overlay.
8. No compiler produces command scopes or queue dependencies.
9. No executor creates/binds/discards/submits recorders and builds truthful
   per-scope receipts.
10. Cross-queue Copy waits are not expressible in the current RHI.
11. No graph capture/dump or Shipping runtime validation exists.
12. No representative end-to-end graph test owns an acquired output through
    terminal cleanup.
13. Allocator-generated generic texture transitions cannot satisfy the RHI's
    token-specific swap-chain Present acknowledgement. A typed external
    terminal-action seam is required before viewport graph execution.

## 12. G0 Decision Ledger

### Copy from existing Vanguard

- `FrameRenderer` as graph/allocator lifetime owner.
- `RenderCommandSystem` as the sole CPU render-tail owner.
- `RenderFrameContext::GetBuilder()` as the continuation path.
- retained `RenderFrameInfo`, view-family, and custom-data ownership.
- `ViewportManager` as sole acquisition/presentation state owner.
- RHI command recording, submission, queue fences, and lifetime retirement.
- allocator survivor/schedule/packet/receipt contracts.
- structured Shipping-valid failure reporting.

### Adapt

- extend frame submission into an explicit output transaction before worker
  dispatch;
- extend asynchronous failure reporting so terminal child work reaches the
  existing latch;
- let the graph describe three queue identities while compiling only currently
  executable dependencies;
- retain frame facts in one graph execution owner instead of callback locals.

### Reject

- acquiring or presenting a viewport output from the worker callback;
- finalizing an acquired back buffer with an ordinary texture transition;
- recording both the allocator's generic Present transition and the token-aware
  RHI Present transition;
- a graph-private thread pool, CPU tail, submission queue, allocator, or native
  retirement manager;
- treating `RenderFrameSubmission.serial` as GPU completion evidence;
- mixed-queue command scopes with one ambiguous terminal fence;
- culling after allocator Resolve;
- capturing stack references in child jobs;
- reporting issued work as discarded after fence-signal failure;
- installing a fake executor just to remove the current deliberate failure.

### Vanguard-specific

- fail closed for cross-queue Copy dependencies until the RHI grows explicit
  wait edges;
- preserve exactly-once view-family commit/release, allocator Finish, and output
  present/abandon as three distinct terminal responsibilities;
- carry the exact acquired-back-buffer token through the frame transaction and
  compile exactly one token-aware Present transition;
- keep graph machinery private to rendering even when diagnostics are exposed;
- use bounded capacities and runtime validation in Shipping.

## 13. Questions Passed To RED And Unreal

1. Is the reusable unit a persistent node graph, a frame-local pass list, or a
   cached recipe instantiated into frame-local passes?
2. Should one high-level node map to one allocator packet, or may it emit
   several independently scheduled passes?
3. Which dependencies are explicit, which derive from resources, and how are
   conflicting declarations diagnosed?
4. How are side-effect roots, extraction, and presentation kept alive?
5. When are command scopes partitioned relative to culling and resource
   lifetime resolve?
6. How are child Jobs joined without making worker completion order semantic?
7. How are async-compute forks/joins selected and validated?
8. How are partial submissions represented when later recording/submission
   fails?
9. Which graph cache keys are stable, and what mutable data must never enter the
   cache?
10. Which debug visualizations/captures are essential in the first production
    version?

## 14. G0 Exit State

The ownership map, Jobs continuation semantics, output transaction, RHI queue
limits, allocator handoff, and token-specific Present requirement are
established. G0 remains open only until the full list of graph-waiting producer
seams is cross-checked. RED and Unreal studies may proceed in parallel, but no
cross-engine design choice is authoritative until V1 synthesis.

## 15. RED Source Snapshot

The RED study is a static reading of the local R6 source. These hashes pin the
source behind Sections 16-20; they are evidence, not a request to import RED
implementation code.

```text
D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraph.h
  lines: 399
  sha256: 33DBCE13082A095BB724CF80CDACDE919FAB032127BBBAB12D7797DEC8ACECC6

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraph.cpp
  lines: 938
  sha256: 98BEBCF439C51698B6194EF1450966E32F5ED6A20559DED2EC3C2FC2D2049962

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphArray.h
  lines: 211
  sha256: 6C1FE53A8DBD3B79DBCC40DDE52A85485CA4CC26753C77C71A873A15567FAB06

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphFactory.h
  lines: 188
  sha256: ED18E368D72AF245C64E98D8D927186816B90112D0D1638B964A7205E2DFA5D4

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphFactory.cpp
  lines: 548
  sha256: FF47C138F3D48EF83E1A071C3C8179FC481A4347A31C02941C1C2BE439B58495

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphCache.h
  lines: 82
  sha256: F24870EB257B38D9B3776BC248C5CC04BE30C023F39A8DC19E3C5AD41EA13D78

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphCache.cpp
  lines: 72
  sha256: 9F6F769FD904801CDE3DEDCC8A85C666DC4444F42722564A0289764A69495DC9

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeJob.h
  lines: 42
  sha256: F8099083E395668B9F4D6F765704E6F1E21B7C3E22931A252CAD9A8827BA8BA0

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeJob.cpp
  lines: 54
  sha256: 1103A6E2917A1C24C8E692FAC9A8722D77186198847E0D3E7E99976E5088D6DA

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.h
  lines: 1135
  sha256: 40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.cpp
  lines: 413
  sha256: B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphNodes.h
  lines: 2310
  sha256: 2AA2662032784371C5ECD8A5D7C65FAB0F05DD8A84F013E03281E0A268097391

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphNodes.cpp
  lines: 2444
  sha256: 88CDF545C5C438A2807D85ED73310F1F608AB6D916DABF659E4123ABE64F7169

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderRenderFrame.cpp
  lines: 4980
  sha256: F2D36A9CF070D5CC0AFDBC828728781BC3BBE6FD7BC069FD5C8A487B9139E6D8

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderInterface.h
  lines: 1319
  sha256: 3858B296F3429F1A769726D8FAA9851C95812093EF9DCDAA527ECA1DF4BDBDCC

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderInterface.cpp
  lines: 3585
  sha256: 5E98A04FF8E0DCD281A532F51BA6F63226326E5CDF2366A93B219F2FDBDAC9DD

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderFlowResourceAllocator.h
  lines: 362
  sha256: DA68B9BD5667450B0F3952D8BC2CCC978A6C82DA1F1A905FBE385C324B61AEE6

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderFlowInternalData.h
  lines: 393
  sha256: 1426C35474FA7F5EE1725881B39B9646C4F35A44EEE440153E202246DB0ED333

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderFlowInternalData.cpp
  lines: 2027
  sha256: CDF7E3C296EF9D6FD465504E8C509EC25D6BCD9F4204F29FF39AE9B2E4A77AD4

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNode_AntyAliasing.cpp
  lines: 519
  sha256: 365B2799B1B29C32E44C8F5ABD3708E6F86DC7F4738D9CC8D5BD5934EFA4E85E

D:/root/R6.Root/Mainline/dev/src/common/gpuApi/include/gpuApiInterface.h
  lines: 2500
  sha256: 5805B58F7CC06FC30FE30EE31F0974D3FCC926717F009D6155D875569FAECA74

D:/root/R6.Root/Mainline/dev/src/common/gpuApi/src/dx12/gpuApiDX12CommandList.cpp
  lines: 1570
  sha256: 14A5895ADA4CD8D86618AFF1EDB9C3B033CD89F9256E6A97958561C2461CB8D4
```

## 16. RED Graph Representation, Construction, And Cache

RED's graph is a cached polymorphic node DAG, not a modern resource-centric
pass compiler. `CRenderInterface` owns both the graph cache and resource
allocator (`renderInterface.h:473,591`; construction at
`renderInterface.cpp:1408-1422`; teardown at `1538-1559`). A cache entry owns a
final graph plus per-camera build graphs and `NodesContainer` owners
(`renderGraphCache.h:39-70`). The final graph stores raw `CRenderNodeBase*`
implementations; `PostBuildClear()` discards the temporary graphs while keeping
the separate node owners alive (`renderGraphCache.cpp:46-55`).

The cache is a four-entry structural LRU-like cache keyed by a camera/features
hash (`renderGraphCache.h:72-77`; `renderGraphCache.cpp:11-43`). The frame path
hashes camera count, display mode, camera features, and selected async-compute
configuration (`renderRenderFrame.cpp:4631-4669`), rebuilds per-camera variants
on a miss, merges them, and seals the final graph
(`renderRenderFrame.cpp:4672-4799`). Graph mutation is excluded while render
jobs use it (`4823-4835,4969-4977`).

Node records contain a raw implementation pointer, type/subtype, camera index,
separate CPU/GPU flow-group indices, and adjacency heads
(`renderNodeGraph.h:223-259,287-309`). Edges are explicitly CPU or GPU
(`45-50,313-331`). Unique nodes and paired sequence helpers support merging
camera graphs (`19-43`); `AddGraph()` copies and reindexes each graph, then
unique/sequence nodes are merged (`renderNodeGraph.cpp:350-527`). Factory APIs
and macros author nodes, groups, and explicit links
(`renderNodeGraphFactory.h:28-108,151-188`).

`BuildRenderFlowGroups()` computes topological depth independently for CPU and
GPU edges and converts depth into dense per-node group indices
(`renderNodeGraph.cpp:640-713`). It does not derive resource edges, cull dead
passes, compile barriers, or generate general queue waits. Its disabled sanity
check admits that CPU and GPU edge sets do not necessarily form one consistent
order (`715-728`).

RED therefore supports a useful distinction Vanguard should retain:

```text
reusable structural topology
  != frame-local mutable execution state
  != resource lifetime/physical allocation plan
```

Vanguard must make the ownership direct: a cached graph generation owns stable
pass records or handles. It must not rely on raw implementation pointers whose
lifetime is secretly supplied by a second container.

## 17. RED Frame Pipeline, Jobs, And Resource Replay

After the graph is selected, RED begins building CPU jobs early but holds roots
behind a counter that includes draw-buffer and allocator setup
(`renderRenderFrame.cpp:4811-4835`). It initializes node context and command-list
storage (`4840-4861`), advances the allocator through Startup and PreConsume,
and invokes `ExecuteParallel()` over every graph node (`4940-4949`). Allocator
Resolve follows; only its completion releases the real node jobs
(`4951-4961`).

Despite its name, `ExecuteParallel()` is declaration replay. It distributes
nodes into quasi-random batches and calls the same node `Process()` method used
for actual work (`renderNodeGraph.cpp:813-878`). Resource operations such as
`RTAlloc`, `RTInject`, `RTUseBegin`, `RTUseEnd`, swaps, and decisions forward
from `SRenderNodeImplContext` into the allocator
(`renderNodeImplContext.cpp:223-293`). A scoped use tag begins and ends a use via
RAII (`renderNodeImplContext.h:114-185`). Concrete nodes gate native GPU commands
on `IsConsumePhase()` while allowing declarations during PreConsume
(`renderNode_AntyAliasing.cpp:211-245`).

This contract requires the request sequence in PreConsume and Consume to match
exactly (`renderFlowResourceAllocator.h:291-294`). Consume checks each request
against the recorded request for the same GPU group
(`renderFlowInternalData.cpp:881-915`), and `RTDecision()` captures a branch in
PreConsume for reuse during Consume (`928-930,998-1004`).

The technique avoids a separate declaration API, but its cost is fundamental:
pass logic is run twice, declaration purity is implicit, and a missed branch or
side effect becomes a correctness problem. Vanguard already has explicit
planning writers and execution packets, so it must declare once, compile once,
and execute the callback once.

Only RED CPU edges are converted into Jobs counters
(`renderRenderFrame.cpp:4298-4320`). Roots additionally wait for the allocator
kickoff dependency (`4322-4335`), and the code expects exactly one terminal job
(`4337-4351`). The node jobs are created at `4402-4436`. This validates
continuation-based fork/join scheduling, not RED's assumption that one unique
sink happens to exist.

No resource-aware pass culling exists. Build-time conditions omit nodes, cache
different structural variants, and individual nodes may early-out. The
"culling" groups in the camera graph concern scene visibility rather than graph
liveness. `RemoveHelperNodes()` has no renderer call site and its debug
verification loop never executes because its condition starts false
(`renderNodeGraph.cpp:530-554`, especially `547`). Vanguard still requires
output/side-effect-rooted pass culling before allocator Resolve.

## 18. RED Command Scopes, GPU Ordering, And Terminal Path

RED separates CPU recording dependencies from GPU ordering. In the common
construction path, however, `LinkGPU()` and `LinkCPU()` connect nodes by factory
registration order and are themselves labeled temporary
(`renderNodeGraphFactory.cpp:479-501`). GPU order is therefore largely a total
source order, not a resource-hazard-derived schedule.

`RenderNodeCommandListUsage` distinguishes no list, a required surrounding
list, an owned list, and a synchronization node (`renderNodeGraph.h:109-115`). A
`CRenderNodeCommandListGroup` owns one graphics or compute command list, runs a
sequence of subnodes, and emits allocator queue begin/end requests
(`renderNodeGraphFactory.cpp:18-67,146-167`). Nested groups are rejected
(`352-368`). Lists are stored by GPU flow-group index
(`renderNodeImplContext.cpp:176-191`). Synchronization nodes register a queue
sync request and submit every still-unsubmitted list through their group index
(`renderGraphNodes.cpp:279-297`; `renderInterface.cpp:169-186`).

RED exposes only `None`, `ForkAsyncCompute`, and `JoinAsyncCompute`
(`gpuApiInterface.h:1246-1262`), with a comment that arbitrary waits were
avoided because automatic resource-state handling was difficult (`1253-1254`).
On D3D12, fork waits compute on direct-queue work and join waits direct on
compute work (`gpuApiDX12CommandList.cpp:1459-1497`); invalid nesting is checked
mainly by assertions (`1503-1517`). Renderer source manually places sync nodes
around async regions, for example hair clear
(`renderRenderFrame.cpp:2226-2269`), shadow overlap (`2416-2438,2553-2557`), and
SSR (`2637-2665`). These are real waits, but region markers are not a general
compiled queue-dependency DAG.

The camera graph explicitly creates EndRender, final flush, Present, optional
extraction flush, cleanup, and EndFrame nodes
(`renderRenderFrame.cpp:3133-3147,3274-3291`). Present calls the viewport directly
(`renderGraphNodes.cpp:359-371`). EndFrame advances allocator cleanup and clears
global frame-job state (`1601-1628`). This validates explicit terminal stages,
but not RED's untyped viewport side effects or global state.

Ordinary resource barriers are not a graph plan. The allocator emits aliasing
activation, while UAV barriers and some cross-queue state repairs are authored
manually inside renderer code (examples at
`renderNode_Composition.cpp:236-306`, `renderNode_Histogram.cpp:87-273`,
`renderNode_Lighting.cpp:1234-1259`, and
`renderRenderFrame.cpp:2567-2580`). Binding-time GpuApi state handling covers
much of the remaining transition work. Vanguard must keep its allocator's
compiled transition/UAV/alias actions and must not regress to implicit binding
side effects.

## 19. RED Correctness And Maintenance Warnings

The following are observed hazards or unsuitable inherited assumptions:

1. Graph mutation and most invalid input are guarded by assertions, not typed
   Shipping-valid failures (`renderNodeGraph.cpp:105-174`).
2. `ExecuteParallel()` performs `1021 % nodeCount` and assumes a non-empty graph;
   its fixed prime also assumes fewer than 1021 nodes
   (`renderNodeGraph.cpp:828-845`). The class separately declares a 512-node
   fixed capacity (`renderNodeGraph.h:390-395`).
3. One terminal job is assumed rather than compiled from an explicit terminal
   join (`renderRenderFrame.cpp:4337-4351`).
4. There is no resource-derived pass liveness, typed output/export contract,
   general queue wait graph, or partial-submission/device-loss receipt.
5. `NodesContainer` is described in its own source as a wrapper that should
   probably be removed (`renderNodeGraphFactory.h:15-24`).
6. `CRenderNodeGraph::Execute()` and `RemoveHelperNodes()` have no renderer call
   sites; the latter includes the dead validation loop noted above.
7. Command-list ownership and submission policy are spread across base-node
   modes, group nodes, global list storage, sync nodes, and GpuApi.
8. The public resource allocator states that allocation requests are assumed to
   succeed (`renderFlowResourceAllocator.h:320-322`).
9. RED's alias event table and async lifetime widening have known static risks
   already recorded in the allocator study; they are not graph patterns to
   migrate.

These findings do not mean RED's renderer is unusable. They identify places
where its surrounding engine, assertions, and established content provide
implicit guarantees that Vanguard deliberately requires as explicit contracts.

## 20. RED Decision Ledger And Exit State

### Copy from RED

- renderer-lifetime ownership of graph cache and allocator;
- immutable cached structural topology separated from frame execution state;
- distinct CPU scheduling and GPU submission relationships;
- command scopes capable of grouping multiple small passes;
- resource planning before command recording is released;
- continuation Jobs with one explicit terminal join;
- named graph, pass, scope, resource, and profiling diagnostics;
- explicit final submission, output, present, and cleanup stages.

### Adapt

- cache structural recipes, but instantiate mutable resources/conditions per
  frame and use explicit invalidation epochs in addition to a complete key;
- replace RED's node implementation pointers with generation-scoped stable IDs
  and graph-owned records;
- compile resource hazards into edges rather than relying on source order;
- compile queue-neutral wait/signal edges rather than hand-authored fork/join
  regions;
- retain command-scope grouping while making each scope queue-homogeneous and
  receipt-producing;
- capture branch decisions during graph declaration/compile, then execute once;
- model terminal output as a retained typed transaction consumed by the
  current-frame graph terminal path through the viewport presentation gate.

### Reject

- replaying the same `Execute()` callback for planning and execution;
- string/name tags as primary resource identity;
- registration order as the normal GPU schedule;
- raw pass pointers owned by a parallel container;
- assertion-only capacity, topology, allocation, and lifecycle safety;
- a magic fixed node limit or permutation prime;
- manual pass-level resource-state repair as the ordinary path;
- a single assumed sink rather than a compiled terminal epilogue;
- global frame pointers and unserialized worker-side viewport presentation;
- copying RED wrapper classes and macros merely because they exist.

### Vanguard-specific consequence

RED confirms the value of persistent topology, command scopes, preplanned
resource lifetimes, and explicit terminal work. It does not supply Vanguard's
missing resource-centric compiler. That compiler must produce live pass
packets, typed resource identities, CPU edges, homogeneous command scopes,
queue wait/signal edges, allocator plans, and an explicit terminal transaction.

R1, R2, and R3 are complete for architectural planning. Representative RED
pass content may still be consulted during implementation, but no unresolved
RED graph concept blocks Vanguard synthesis.

## 21. Unreal RDG Source Snapshot

The local Unreal checkout identifies itself as 5.8.1
(`D:/UnrealEngine/Engine/Build/Build.version:2-4`). These hashes pin the source
behind Sections 22-27.

```text
D:/UnrealEngine/Engine/Build/Build.version
  lines: 11
  sha256: FA0355D0DAFD1D4BD34983841799CD9E1093A273EC8EE93E71B73984E40055D9

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h
  lines: 1200
  sha256: 177C74FFC847EB6899127263B0E3B79298CF2D57C87877877EF9AB9A6A67B580

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.inl
  lines: 757
  sha256: 3C3FC4FB833123A35975A34109077AD9A29A9A2506237933A14AF1967B81FD1B

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraph.h
  lines: 65
  sha256: 959F8274BECB7EC1403FCBEC40386A86438846B0CDED66D3E8FAA624451F85AA

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphPass.h
  lines: 841
  sha256: 11E8F5648303D28BF59B9B69E00C440980493552C9EB4215D70A4BFB9BDD7856

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h
  lines: 1515
  sha256: 367CA11F59C00DB846121871CF1C4B52DBE7E424D4047D09C89B8505BA5ECB29

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphDefinitions.h
  lines: 801
  sha256: AC8270F578290FD8879ECCB1DF3E13671657F4737F5591098EAC51C03AD7CCC4

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphValidation.h
  lines: 181
  sha256: E13DEC9AED5AE39A8CF21CCAD01CC69DB0C5B791AE7433264AB2B7C7A7D916DC

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Public/RenderGraphTrace.h
  lines: 63
  sha256: D8FB78AF165BFE2589138917B4B5A02DCBF236A5D746B4548814D1EE3EF31A88

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp
  lines: 5259
  sha256: 49FE0A44086B74DFDFED4E9C1462B8B0FE7B5787232CDB4835497B1A1B2D5246

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/RenderGraphPass.cpp
  lines: 465
  sha256: E68D8AC86D2E279496425682B0C0B69A0180E4B7B64300D9F093EC300B85803D

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/RenderGraphPrivate.cpp
  lines: 703
  sha256: DB28EFE1FA7AB4A30D6B9DE3C60FB3C6E9EC9269C8D288F58275DADA365BFDC1

D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/RenderGraphValidation.cpp
  lines: 1208
  sha256: 8E46FC537D134FA44F42AACB01CF5BF62FDD14574AD2AE4571764926242A942B

D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIPipeline.h
  lines: 79
  sha256: 3B831CE1A918EA110F36BF133A536DFCA9588DB0782B1D237C4B8DEFB6D8CC75

D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHITransientResourceAllocator.h
  lines: 559
  sha256: C1CB029794883DCBABDCDBD5E876F2E2FD37EA8C359CFDC7E2B8403F0A8B6E70
```

Epic's public architectural overview is retained as a secondary source:
<https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine?lang=en-US>.
The local source is authoritative for the observed 5.8.1 behavior.

## 22. Unreal Builder, Pass, And Resource Model

`FRDGBuilder` is a one-shot frame-local object which must execute before it is
destroyed (`RenderGraphBuilder.h:45-55`). Graph-created resources initially
allocate descriptors and graph records, not necessarily native RHI objects;
their native access is constrained to passes that declared use
(`RenderGraphBuilder.h:99-134`; `RenderGraph.h:13-30`). Pass parameter storage is
allocated in the graph arena, becomes immutable after `AddPass`, and the
execution lambda is deferred (`RenderGraphBuilder.h:155-221`).

Unreal discovers typed texture, buffer, view, and access declarations by
enumerating reflected pass parameters and accumulating per-pass resource state
(`RenderGraphBuilder.cpp:2384-2510`). Textures track producer state by
subresource and pipeline; buffers maintain equivalent state.
`FRDGProducerState` exists specifically to support culling and pipeline fences
(`RenderGraphResources.h:45-86,642-700`).

The transferable contract is declaration immutability, not Unreal's reflection
machinery:

```text
AddPass(name, queue, explicit uses, flags, immutable payload, callback)
  -> pass and use IDs become stable within one builder generation
  -> declaration is sealed
  -> callback is invoked exactly once only if the pass survives
```

Vanguard should use explicit C++ use descriptors and forward them into
`ResourcePlanningWriter`. It must not introduce Unreal-style shader-parameter
reflection merely to build the graph, and it must not create a second resource
state compiler beside `RenderFlowResourceAllocator`.

## 23. Unreal Dependencies, Roots, And Culling

Unreal builds dependencies from prior resource producers as passes are added.
Cross-pipeline use also records the latest producer and ordered consumers
(`RenderGraphBuilder.cpp:1160-1308`). Culling initially marks candidates dead,
roots external or extracted outputs, untracked output access, and `NeverCull`
passes, then walks `Pass->Producers` in reverse to keep all required ancestors
(`1310-1435,2373-2382`). Prologue and epilogue sentinel passes give compilation
explicit anchors for imports, initial/final barriers, extraction, and roots
(`RenderGraphBuilder.h:458-500`).

Vanguard should derive resource edges at the finest range its allocator can
truthfully represent:

- RAW: a read depends on the last overlapping writer;
- WAW: a write depends on the last overlapping writer;
- WAR: a write follows all overlapping readers that must complete first;
- explicit control edges: retained only when resource hazards cannot express
  the required side-effect order.

The initial root classes should be explicit and typed:

```text
Present(output)
Export(resource, slot)
Readback(resource, request)
SideEffect(reason)
```

An imported resource is not automatically a root; a surviving output or
explicit side effect must demand the work. A parameterless pass must not become
implicitly non-cullable. Unreal currently applies that convenience rule in
`RenderGraphBuilder.inl:274-292`; Vanguard will require explicit intent.

Culling must complete before allocator planning is sealed and before
`Resolve()`. Only surviving passes contribute planning writers, stable order,
command scopes, and queue dependencies. This directly preserves the existing
allocator boundary rather than asking it to undo dead work.

## 24. Unreal Execution, Barriers, Transients, And Extraction

Unreal compiles compatible subresource states and emits only required
transitions (`RenderGraphBuilder.cpp:3783-4072`, transition lowering at
`4439-4550`). During execution it switches to the pass pipeline, submits
prologue transition work, invokes the pass once, then submits its epilogue work
(`3424-3605`). Its transient allocator creates and deallocates resources from
compiled lifetime fences; alias acquisition is emitted as a real transition
(`RHITransientResourceAllocator.h:535-558`;
`RenderGraphBuilder.cpp:4074-4236`).

Vanguard copies the shape, not the ownership:

```text
RenderNodeGraph::PrepareResourcesParallel
  -> resource uses, GPU flow groups, command scopes, queue boundaries, Sync requests

RenderFlowResourceAllocator::Resolve
  -> validate and compile the complete registered request stream
  -> physical assignment
  -> dedicated/placed reuse and alias activation
  -> transition and UAV actions
  -> per-pass execution packets
```

The graph executor consumes allocator actions around each pass. It must not
independently infer or issue an overlapping ordinary barrier plan.

Unreal extraction marks a resource external and a culling root, extends its
lifetime, and writes the destination only after execution tasks complete
(`RenderGraphBuilder.inl:441-497`; `RenderGraphBuilder.cpp:2194-2255`). Vanguard
already has a stronger ownership boundary: request an export during planning,
publish it only after successful terminal receipts and allocator `Finish()`, and
let the consumer explicitly take the owned export. Extraction never means a
raw output pointer is valid before terminal completion.

The first implementation should keep declaration/compile data immutable but
record command scopes serially. Parallel pass recording can be added later
without changing graph semantics once lifetime and failure behavior are proven.

## 25. Unreal's Two-Pipeline Model Must Not Define Vanguard

Unreal's `ERHIPipeline` contains only Graphics and AsyncCompute
(`RHIPipeline.h:12-35`). `ERDGPassFlags::Copy` means copy commands on the
graphics pipeline (`RenderGraphDefinitions.h:128-149`), and a pass maps to
AsyncCompute only when that specific flag is present; every other pass maps to
Graphics (`RenderGraphPass.cpp:440-448`). Async work is lowered into balanced
graphics fork/join intervals using the latest graphics producer and earliest
surviving graphics consumer (`RenderGraphBuilder.cpp:1562-1725`). Transient
lifetime fence structures are likewise hard-coded around Graphics plus
AsyncCompute (`RHITransientResourceAllocator.h:16-75`).

Vanguard already creates real DIRECT, COMPUTE, and COPY D3D12 queues with
separate fences (`source/rhi/nvrhi/src/d3d12_backend.cpp:1332-1405`), exposes all
three queue identities (`rhi_types.hpp:293-307`), and retains all three kinds of
fence evidence in submission and residency receipts (`1660-1705`). Its current
submission synchronization is nevertheless still limited to None,
ForkAsyncCompute, and JoinAsyncCompute (`309-323`), and the allocator explicitly
rejects Copy queue crossings
(`render_flow_resource_allocator.hpp:498-514`).

The target graph IR must therefore remain queue-neutral:

```text
UploadVertices [Copy]
  -> wait copy fence
GPUCull [Compute]
  -> wait compute fence
Draw [Graphics]
```

The compiled representation needs arbitrary acyclic producer-scope to
consumer-scope edges, multiple predecessors, deterministic cycle detection,
and one fence receipt per submitted scope. Same-queue edges lower to submission
order. Cross-queue edges lower to waits on producer receipts before consumer
execution and signal the consumer queue afterward.

The RHI eventually needs a generic primitive equivalent to:

```cpp
SubmitScope(
    rhi::QueueType consumerQueue,
    ArraySpan<const rhi::CommandListRef> commandLists,
    ArraySpan<const rhi::GpuFence> waits,
    rhi::SubmissionReceipt& receipt);
```

Existing fork/join calls may remain compatibility wrappers. Adding a third
entry to Unreal-style fork/join arrays is rejected because their interval math
assumes exactly Graphics versus AsyncCompute. Until the generic wait contract
exists, the compiler must fail closed for cross-queue Copy edges. A conservative
graphics-only executable baseline and a later graphics/compute region lowering
are both compatible with the final IR.

Aliasing cannot use global stable order as proof of GPU non-overlap. Across
queues, `last(A)` must happen-before `first(B)` through same-queue order or the
transitive queue dependency graph before the allocations may share memory. The
allocator's current same-queue placed-aliasing rule remains the correct initial
restriction.

## 26. Unreal Validation And Diagnostics

Unreal validates graph API use, production-before-use, pass execution, and
barrier batches (`RenderGraphValidation.h:24-180`), but the primary user
validation layer is compiled out in Test and Shipping
(`RenderGraphDefinitions.h:16-27`). Debug controls include immediate mode,
per-pass GPU flush, lifetime extension, transient disabling, resource
clobbering, transition logging, and RDG tracing
(`RenderGraphPrivate.cpp:16-160`; `RenderGraphTrace.h:21-62`).

Vanguard should adopt the observability categories while keeping correctness
checks in Shipping. At minimum a compiled graph dump must show:

- pass ID/name, root reason, culled/live state, and stable order;
- resource ID/name/descriptor and every declared access range;
- derived and explicit edges with their reason;
- command scope, queue, and ordered pass membership;
- cross-queue producer/consumer waits;
- allocator physical assignment and action ranges;
- output/export terminal action;
- actual submission/discard/device-loss receipt.

Immediate mode and per-pass flush are diagnostic modes only. Neither may alter
which hazards or terminal obligations the compiler recognizes.

## 27. Unreal Decision Ledger And Exit State

### Copy from Unreal

- one-shot frame-local builder and generation-scoped identities;
- immutable declarative pass/resource usage and one execution callback;
- per-range producer tracking and resource-derived hazards;
- reverse reachability from explicit output and side-effect roots;
- prologue and epilogue sentinels;
- late physical allocation and explicit alias activation;
- extraction publication only after execution completion;
- rich graph/resource/transition tracing categories.

### Adapt

- replace reflected parameter traversal with explicit Vanguard use descriptors;
- let the graph compile topology/scopes while the existing allocator compiles
  physical assignment and resource actions;
- make `NeverCull` an explicit side-effect classification with a reason;
- compile a validated stable topological scope order, not insertion order;
- publish owned allocator exports rather than filling caller raw pointers;
- design immutable parallel-capable data now, but prove serial recording first;
- retain sentinel concepts without exposing artificial passes to feature code.

### Reject

- Unreal's two-entry pipeline enum and Copy-on-Graphics assumption as the final
  Vanguard model;
- balanced graphics/async-compute fork/join as the universal queue abstraction;
- the process-global single-builder validation flag
  (`RenderGraphValidation.cpp:21-47`);
- implicit `NeverCull` for a parameterless pass;
- immediate external conversion in the initial public API because it forces
  allocation and extends lifetime;
- Shipping-critical validation compiled out;
- fatal/void graph execution where Vanguard needs transactional failure and
  truthful partial-submission recovery.

### Vanguard-specific consequence

The clean authority boundary is:

```text
RenderGraph
  owns topology, dependency derivation, culling, stable pass order,
       command scopes, queue edges, callbacks, and terminal orchestration

RenderFlowResourceAllocator
  owns logical resource plans, physical assignment, native allocation,
       transition/UAV/alias actions, imports/exports, and retirement
```

U1 and U2 are complete for architectural planning. Unreal provides the primary
model for resource-centric compilation, while RED remains stronger evidence for
how Vanguard's existing Jobs continuation, command scopes, and renderer-owned
cache can fit the surrounding engine.

## 28. G0 Closure: Vanguard Integration Inventory

The repository inventory is now complete enough for architecture synthesis.
Vanguard has no production graph builder, graph instance, pass/node base,
compiler, `process_node` wrapper, command-scope recorder, graph submission
coordinator, or output terminal transaction. Existing allocator tests construct
synthetic graph identities and schedules; they do not constitute hidden graph
machinery. The deliberate failure in `FrameRenderer::RenderFrame` remains the
truthful production boundary.

The engine frame pipeline executes synchronously on the main thread and resets
Frame pools immediately after all phases (`frame_pipeline_service.cpp:225-230,
301-327`). Its global order places RenderUpdate before Presentation before
Render (`frame_pipeline_service.hpp:13-28`). `RenderingService` currently
registers only a main-thread RenderUpdate participant and a Render-phase frame
tick (`rendering_service.cpp:238-278`). RenderUpdate joins the previous CPU
render tail before collecting async failures and native retirements (`681-737`).

This order provides lifecycle reconciliation and prior-tail observation without
moving Present into the next frame:

```text
frame N RenderUpdate
  -> join frame N-1 graph CPU terminal epilogue for failures and retirement
frame N Presentation
  -> reconcile window/swap-chain state
frame N Render
  -> publish current GPU Scene/frame inputs
  -> acquire the output on the main thread
  -> dispatch graph execution
  -> terminal graph node consumes Present/CompleteOutput/Abandon exactly once
```

`PresentationService` already owns the window-to-render-viewport-to-swap-chain
chain by contract (`presentation_service.hpp:124-147`) and performs main-thread
reconciliation (`presentation_service.cpp:300-444`), but no production service
currently owns or registers it. It is an integration seam, not dead code. V1
must decide whether `RenderingService` owns it directly or a separate engine
service owns it with explicit ordering. The graph must not absorb window or
swap-chain reconciliation policy.

### Producer and consumer seams

- Prepared camera/view-family state has explicit retain, commit, and release
  behavior (`render_camera.hpp:177-223`; `render_camera.cpp:1744-1750`).
- `RenderScene` already produces synchronous/batched CPU visibility and
  GPU-candidate data (`render_scene.hpp:639-658`;
  `render_scene.cpp:2350-2510`).
- GPU visibility has storage, reservation, and finalized-plan contracts, but no
  graph-owned dispatch/binding/submission (`gpu_scene_visibility.hpp:76-159`).
- GPU Scene publication already appends range writers and an epilogue through
  the RenderPath continuation (`gpu_scene_runtime.hpp:112-114`;
  `gpu_scene_runtime.cpp:453-731`).
- Mesh, texture, and material residency own persistent native resources and
  strong handles. Graph passes consume them; the graph does not mirror their
  caches or take ownership.
- Pipeline creation/resolution exists independently
  (`render_pipeline_factory.cpp:243-291`). A pass requests or consumes a
  pipeline, but the graph does not become a PSO cache.
- `RenderPhaseRegistry`, `RenderPhaseId`, and `RenderPhaseSet` are stable feature
  classification/sorting metadata (`render_phase.hpp:11-127,168-210`), not graph
  topology or synchronization.

Feature systems therefore contribute opaque pass payloads plus typed resource
accesses. The graph understands identity, dependencies, queues, lifetimes, and
terminal effects; it does not understand visibility, material, lighting, or
streaming semantics.

### Completion and failure caveats

`RenderCommandSystem` increments `completedFrames` and publishes
`lastCompletedSerial` when the root renderer callback returns
(`render_command_system.cpp:217-232`). Continuation children may still be
running. Those fields currently mean "root callback returned," not "graph CPU
terminal complete" and certainly not "GPU complete." They must not drive
presentation, reuse, or retirement. The graph needs its own terminal outcome,
and late child failure needs a thread-safe route into the command system's
first-failure latch.

The NVRHI submission backend sets `workSubmitted` before fence signaling. If a
signal fails, native execution has already occurred; it releases the closed
payloads and returns device loss with submitted-without-completion evidence
(`common_backend.cpp:2794-2847`). The graph executor must always use the full
`SubmissionReceipt`, classify affected scopes as
`UnknownDueToDeviceLoss`, and finish the allocator as DeviceLost. It must never
rewrite this as `DiscardedBeforeSubmission`.

Three upstream producers still use the older fence-only overload: texture upload
(`texture_uploader.cpp:1012-1016`), geometry upload
(`geometry_upload.cpp:567-570`), and GPU Scene sparse upload
(`gpu_scene_upload.cpp:519-522`). RG4 migrates all three to full
`SubmissionReceipt` handling so post-execution signal failure is preserved.

`RenderingService::SealResidencyRetirements()` requires nonzero graphics,
compute, and copy cutovers (`rendering_service.cpp:129-143`). A graph frame may
legitimately submit only one queue and must not fabricate missing fences. V1
needs a renderer/device-wide accumulated last-submitted cutover for persistent
residency retirement, separate from the exact per-frame/per-scope receipt set
used by the graph allocator.

Finally, Frame-pool storage is invalid after `RunFrame()` returns. Any graph
definition instance, compiled schedule, command-scope record, failure object, or
output acquisition captured by continuation jobs must live in renderer-owned
or explicitly retained storage. No asynchronous callback may retain a pointer
into the Frame pool.

### G0 final status

Every current owner now has a single graph-facing responsibility, and the
remaining seams are explicit. G0 is complete. The critical prerequisites for
executable output are:

1. retained frame/output transaction and thread-safe same-frame terminal
   consumption serialized against main-thread lifecycle work;
2. token-aware Present terminal action;
3. thread-safe late graph failure publication;
4. full RHI submission receipts and exact allocator terminal receipts;
5. device-wide accumulated retirement cutover distinct from frame receipts.

None requires a second scheduler, allocator, submission system, or presentation
owner.

## 29. Former V1 Architecture (Preserved, Superseded By R0)

> R0 preservation note: Sections 29-41 record the former V1 synthesis. Their
> claims of authority and completion are historical, not current. The ledger in
> Section 1A controls the RED-faithful revision.

The comparison phase is closed. The following sections replace the provisional
choices in Sections 6, 12, 14, and 28 wherever they differ. RED supplies useful
surrounding-engine patterns; Unreal supplies the stronger resource-centric
compiler model; Vanguard's existing allocator, Jobs, RHI, viewport, and service
contracts remain the final authority.

The baseline uses a renderer-owned bounded cache of immutable structural graph
templates. Each selected template is instantiated into a one-shot frame-local
execution instance:

```text
feature/configuration key
  -> structural-template cache lookup
  -> cache miss: single-threaded template declaration and compilation
       -> sealed canonical pass/resource-use IR
       -> dependency compiler + roots + culling + stable topological order
       -> queue-neutral command-scope DAG
       -> executable queue lowering validation
       -> atomically published immutable template
  -> cache hit or newly published template
  -> frame-local instance binding of views, payloads, imports, and output
  -> RenderFlowResourceAllocator planning and resolve
  -> retained serial execution + truthful scope receipts
  -> exactly-once terminal transaction
```

There is no reusable polymorphic `RenderNode` ownership hierarchy in the
baseline. A feature-level "node" is ordinary code that contributes one or more
passes. The first-class scheduled unit is a pass. Every surviving pass maps to
exactly one `RenderFlowNodeId` and, initially, exactly one command scope and one
allocator execution packet.

This deliberately rejects RED's class-for-class node graph and Unreal's macro
and reflection surface while retaining their strongest correctness ideas.

## 30. Public Type And Declaration Model

Public handles are small, non-owning, generation-checked values. Separate types
exist for passes, textures, buffers, texture views, buffer views, and terminal
roots. A handle from another builder generation or an already-terminal
generation fails in Debug and Shipping.

The template builder owns copied names, descriptors, canonical uses, structural
conditions, and noncapturing `noexcept` execution thunks. The template records
the owned pass-data type contract but never captures frame-local payload values.
Each frame instance binds one owned pass-data value for every instantiated pass.
Arbitrary capturing callables are not part of either API. This mechanically
prevents a lambda capture such as `[&]` from becoming the retained callback.
Pass-data types must obey the owned-data contract and may not contain borrowed
`StringView`, spans, frame-pool allocations, stack references, or callback-local
pointers. Template declaration and frame-instance binding are single-threaded in
the baseline. Instance binding cannot add passes, uses, versions, roots, or
edges; it only supplies values for slots fixed by the immutable template.

A pass declaration contains:

- a copied name and stable declaration ordinal;
- one `Graphics`, `Compute`, or `Copy` queue;
- explicit diagnostic/phase metadata;
- a pass-data schema with explicit move/destroy operations and one noncapturing
  recording thunk; the corresponding owned value belongs to each frame
  instance, not the template;
- a canonical ordered list of typed texture or buffer uses;
- optional explicit control dependencies with a recorded reason;
- an explicit terminal-root reason when it has an external effect.

The public graph reuses the allocator's existing `FrameTextureDesc`,
`FrameBufferDesc`, `TextureUseDesc`, `BufferUseDesc`, view descriptors, access
intent, content intent, resource state, and queue vocabulary. It does not clone
these into nearly identical graph enums. Graph-only types are identities,
versions, bindings, roots, edges, payloads, compile state, and diagnostics.

The callback executes at most once and only for a surviving pass. Its
`noexcept` signature returns an explicit `RenderGraphPassStatus`; exceptions are
not a failure channel. Success is explicit. Failure carries a typed pass code
and may carry copied nested RHI evidence; the executor copies its message into
envelope-owned fixed storage before callback payload or command-list cleanup.
The callback may record commands using declared resolved uses; it may not add
graph structure, change a declared access, skip a declared write dynamically,
retain a resolved RHI reference after `EndUse`, or publish an irreversible
CPU-visible effect.

Every declaration returns a typed, pass-local binding key in addition to any
new resource version. The recording callback resolves textures, buffers, and
views only through those binding keys and its `RenderGraphPassContext`; a bare
graph resource handle cannot be resolved at runtime. The executor begins all
declared uses in canonical order, invokes the callback once, then ends them in a
fixed reverse order. This makes undeclared access and handle escape difficult by
construction and gives the allocator one deterministic packet-step sequence.

Pass declarations are data, not a second execution path. There is no separate
"resource planning callback" and no compile-time replay of the recording
callback. One canonical typed-use IR is consumed by all of:

1. hazard and culling analysis;
2. allocator planning-writer replay;
3. runtime `Begin*Use` / `EndUse` order;
4. callback binding slots and diagnostics.

This single-source rule is a hard invariant. A second declaration path would
eventually let the dependency compiler, allocator, and executor disagree.

## 31. Resource Identity, Content Versions, And Hazards

A graph resource has a stable allocation identity and monotonic content
versions. The ergonomic handle denotes both. Writes return the next version;
reads consume an exact version. Indicative surface syntax is:

```cpp
GraphTexture hdr = graph.CreateTexture("hdr", hdrDesc); // undefined version 0
hdr = pass.Write(hdr, renderTargetUse);                  // version 1
pass.Read(hdr, shaderReadUse);                           // consumes version 1
hdr = pass.ReadWrite(hdr, uavUse);                       // consumes v1, produces v2

GraphTexture backBuffer = graph.ImportPresentation(output); // defined Present v0
outputPass.Read(hdr, shaderReadUse);
backBuffer = outputPass.Write(backBuffer, renderTargetUse);  // terminal output v1
graph.Present(backBuffer, output);                           // roots exactly v1
```

Names in this example describe the contract, not approved spelling.

Version rules are authoritative:

- each produced version has exactly one producer;
- imported version zero is defined by its import contract;
- a newly created version zero is defined only when its initialization contract
  clears it; an undefined version zero cannot be read or preserved;
- a read depends on the producer of the consumed version (RAW);
- an in-place next version follows the preceding writer (WAW);
- an in-place next version follows every reader of the preceding version (WAR);
- a read-modify-write both consumes the old and produces the new version;
- Present and ExternalWrite roots name an exact version;
- "latest resource value at compile time" is never inferred after the fact.

A texture or buffer view is a typed descriptor-and-range projection of one
parent allocation lineage and the exact parent content version from which the
view was created. Reading through the view consumes that version. Writing or
read-modify-writing through the view produces the next version of the parent
resource; it does not create an independent view lineage or silently retarget
older view handles. A write through a stale parent version is rejected. Because
the baseline dependency compiler is whole-resource, every RTV/SRV/UAV or buffer
view of the same parent shares one conservative hazard history even when ranges
are disjoint. The allocator still receives the precise view/range descriptor for
state transitions and later alias planning.

`Present` does not imply a hidden copy or tonemap. Renderer feature code declares
the terminal output/composition pass; the graph roots that pass's exact acquired-
back-buffer version and supplies only the semantic Present transition and
terminal transaction. Likewise, a readback root names a pass/version whose
declared callback records the actual copy.

A frame carrying a presentation ticket must declare exactly one `Present` root
for that same ticket and acquired resource. Zero roots, duplicate roots, a root
for a foreign ticket, or any non-terminal use after the rooted back-buffer
version is a compile failure. Because compilation happens before native
submission, these failures publish `AbandonSafe`.

A frame-output root is also the graph's compiler-internal epilogue. After
culling, every other surviving pass receives a typed dependency into its one
terminal output pass; the resulting pass and scope DAGs are cycle-checked again.
This makes the output scope the final submission rather than relying on a name
or declaration ordinal. Multiple unrelated frame-output roots are rejected.

RAW/value edges and explicit required dependencies propagate liveness. WAR and
discard-WAW edges are ordering constraints among passes that survive for some
other reason; they do not by themselves keep an otherwise dead reader or
overwritten writer alive. A preserving/read-modify-write access consumes the
old version and therefore has a RAW/value edge as well. After culling, hazard
ordering is rebuilt over the live access history so removing dead work cannot
leave a dangling anti-dependency.

Declaration order assigns versions within one resource lineage. It is therefore
an explicit content-version rule, not an implicit global GPU schedule. Stable
topological order remains free to move independent passes. A deterministic
declaration ordinal is used only as a tie-break between otherwise independent
passes.

The first compiler is conservatively whole-resource for dependency hazards.
Texture subresource ranges and buffer ranges remain in the canonical use IR and
are replayed precisely to the allocator, but disjoint ranges do not initially
remove a dependency. Range-precise dependency analysis is a measured
optimization, not a correctness prerequisite.

Same-pass overlapping uses are allowed only when they are identical compatible
reads that can be normalized to one use. Conflicting reads/states, any
overlapping read/write pair, and duplicate writes are compile failures. This
keeps packet use order and backend states unambiguous.

The graph resource ID is the public correctness identity. The allocator's
`LogicalResourceKey { FlowSpaceId, StringView }` is an internal bridge: the
adapter creates a copied, graph-generation-unique key for each resource lineage.
User-visible strings are diagnostics and never decide graph identity.

## 32. Roots, Culling, And Validation Order

The only baseline roots are:

- `Present`;
- `ExternalWrite` where an imported owner observes the mutation;
- an explicitly named `SideEffect` reason.

Importing a resource does not root a pass. A parameterless pass does not become
a root implicitly. `SideEffect` prevents culling but never bypasses identity,
cycle, use, or queue validation.

`ExternalWrite` is not generic result delivery. In the baseline it is legal only
for an imported resource whose retained owner participates in the execution
envelope's terminal commit/abort contract and receives the exact terminal queue
fence. The concrete owners are a Texture viewport output ticket and
`FrameRenderer`-private persistent state. An arbitrary caller that needs to take
ownership or map data must use the deferred Export or Readback project instead
of treating CPU terminal submission as GPU completion.

Compilation is atomic and follows this order:

```text
Seal declarations
  -> validate handles, descriptors, payload ownership, uses, and versions
  -> derive typed value, WAR, and WAW resource candidate edges
  -> add separately typed explicit GPU-order edges
  -> reject cycles in the complete candidate graph
  -> reverse-mark from exact-version roots across liveness-propagating edges
  -> cull unmarked passes and resources
  -> rebuild RAW / WAR / WAW ordering over surviving accesses
  -> stable topological sort of the surviving graph
  -> form command scopes
  -> contract and independently validate the scope DAG
  -> validate executable queue lowering
  -> begin allocator session and replay only the surviving canonical IR
```

Explicit public dependencies mean GPU happens-before. CPU Job dependencies are
private executor machinery and cannot be expressed through the same untyped
edge. This prevents a same-thread coincidence from masquerading as cross-queue
GPU synchronization.

Candidate-graph cycle validation is deliberately performed before culling. A
cycle does not become acceptable merely because the current frame's roots do
not reach it; changing one output must not reveal a latent invalid topology.

## 33. Pass Order, Command Scopes, And Queue Lowering

The compiler produces two separate scheduling artifacts:

1. a queue-neutral scope DAG over `Graphics`, `Compute`, and `Copy`;
2. allocator-compiled command scopes and queue dependencies derived from the RED-shaped queue request stream.

They must never be conflated. CPU order and stable topological order cannot
stand in for a missing GPU wait.

The initial scope policy is one surviving pass per scope. It is intentionally
simple and establishes exact pass-to-packet and scope-to-receipt accounting.
Later scope merging may combine only a contiguous interval in stable
topological order, on one queue, with compatible recording policy. After any
grouping the compiler rebuilds, deduplicates, and cycle-checks the contracted
scope DAG. An acyclic pass DAG can otherwise become a cyclic scope graph.

The baseline abstract DAG represents all three real Vanguard queues. Executable
lowering accepts only:

- isolated single-queue schedules on Graphics, Compute, or Copy;
- the allocator/RHI's existing balanced, non-nested Graphics-to-Compute fork
  and Compute-to-Graphics join region with advancing unique consumers.

It rejects, before allocator `Resolve`:

- every cross-queue edge involving Copy;
- nested or overlapping async-compute regions;
- unsupported fan-in or fan-out;
- any schedule that cannot be represented without inventing a fence or wait.

This is not a graph-IR limitation. General queue crossings become executable
only after the RHI and allocator expose generic producer-fence/consumer-wait
edges. Copy remains in the IR now so that later support does not require an API
redesign.

The initial production executor is serial even though its compiled data is
immutable and parallel-capable. It creates one command list per scope, requires
an empty thread-local RHI recorder slot, binds through an RAII guard, consumes
the allocator packet in canonical use order, unbinds on every path, and then
submits. Parallel recording is enabled only after the serial lifetime and
failure matrix is complete.

## 34. Allocator Boundary

The division of authority is fixed:

```text
RenderGraph
  topology, exact resource versions, hazards, roots, culling, stable order,
  scopes, queue DAG, pass payloads/callbacks, submission orchestration,
  terminal outcome

RenderFlowResourceAllocator
  logical resource plans, imports/exports, physical assignment, dedicated and
  placed reuse, transition/UAV/alias actions, execution packets, retirement
```

After graph compilation and culling, the adapter:

1. begins one allocator frame session;
2. registers all surviving imports before it creates a planning writer;
3. assigns each live pass one stable `RenderFlowNodeId`, `GpuFlowGroupId`, and
   `CommandScopeId`;
4. replays the pass's canonical uses into exactly one writer and closes it;
5. seals candidates with valid planning-join evidence;
6. resolves the complete registered request stream directly;
7. retains the resulting execution generation until terminal completion.

Graph content versions map to one allocator logical resource lineage unless a
feature explicitly creates a different resource. Version edges control order;
they do not manufacture extra physical resources. The allocator remains free
to reuse dedicated resources and activate placed aliases for non-overlapping
lifetimes.

The adapter does not reinterpret access intent, infer missing uses, or invoke
feature code. Its replayed sequence must byte-for-byte agree in kind and order
with the execution bindings exposed to the callback.

## 35. Retained Execution And Terminal State Machine

The cache-miss template builder may be destroyed after atomic publication, and
the frame-instance binder may be destroyed after successful instance sealing. A
separate retained execution envelope owns everything that can outlive them:

- one strong reference to the immutable compiled structural template, which
  owns copied pass/resource names, descriptors, IR, and schedules;
- frame-local pass payloads and their exactly-once destructors;
- retained external resource owners;
- prepared view family/custom data;
- allocator frame session and execution generation;
- command lists and full RHI submission receipts;
- exact output acquisition and revision;
- fixed-storage first failure and terminal outcome.

Its phase machine is:

```text
TemplateLookup -> InstanceBinding -> Resolving -> Ready
               -> Recording -> Submitting -> Terminal
```

Failure is an outcome, not an escape from ownership. Before publication, the
envelope calls `CancelBeforePublication`. After an execution generation is
published, every cursor is finalized or canceled, every scope receives one
truthful completion classification, and `RenderFlowResourceAllocator::Finish` is
called exactly once as Completed, Aborted, or DeviceLost.

Serial execution performs packet work synchronously inside one graph execution
job and uses `TerminalJoinToken::CompletedSynchronously(generation)`. A terminal
task must never ask `FromReadyCounter` to prove readiness of a counter that
contains that terminal task; that is a self-cycle. Later parallel recording
uses a separate graph-work counter. The terminal task depends on that already-
ready work counter, constructs the join token from it, calls `Finish`, publishes
the outcome, and is itself retained by the outer continuation builder.

All graph work is dispatched through `RenderFrameContext::GetBuilder()`. Therefore
the existing render CPU tail covers the terminal task. The graph never extracts
that continuation counter or creates a detached Jobs chain.

Payload destruction occurs exactly once after the last execution reference for
success, culled passes, compile failure, abort, and device loss. Culled payloads
may be released as soon as the sealed compiler no longer requires them; their
recording callbacks never run.

## 36. Submission And Failure Truth

The executor always uses the full `rhi::SubmissionReceipt`. A scope is:

- `Submitted` only with its real queue fence;
- `DiscardedBeforeSubmission` only if native execution was not issued;
- `UnknownDueToDeviceLoss` if submission occurred but completion evidence was
  lost.

`CloseAndSubmitCommandLists` returning false is not enough to classify a
discard. If `WasSubmitted()` is true, the terminal outcome is DeviceLost and no
submitted resource may be treated as immediately reusable. Every remaining
scope still receives a terminal classification before allocator `Finish`.

A failed allocator `Finish` is itself terminalization failure. The executor
captures the allocator evidence, performs no view/history/output success commit,
does not retry `Finish`, and releases the still-published session coordinator.
The allocator's existing fail-closed abandonment path then marks it
`DeviceUnavailable`, invalidates packet uses, and applies device-loss handling
to both resource pools. The graph outcome is DeviceLost/recovery-required even
when no earlier native submission was observed, because terminal ownership can
no longer be proved.

Fork submission has a subtle receipt rule: its aggregate completion fence may
be Compute even though the producer scope is Graphics. The Graphics scope's
receipt is taken from `SubmissionReceipt::residency.graphics`, not blindly from
`completion`. Each scope uses the residency fence for its own queue.

Failure reporting is stable and structured. `RenderGraphFailure` records at
least phase, code, generation, pass, resource/version, scope, queue, and nested
allocator or RHI evidence. Messages live in fixed or envelope-owned storage;
they are never borrowed from a worker stack. Concurrent children publish only
the first failure through a thread-safe latch, and the terminal task forwards
that failure to the existing command-system failure path. To make that possible,
`RenderFrameContext` supplies a small copyable execution-failure sink backed by
`RenderCommandSystem`'s existing latch. The retained envelope copies the sink;
it never retains the stack context or reaches into command-system internals.
Command-system shutdown already joins the CPU tail before destroying the sink
owner.

Recording callbacks are transactional with respect to external CPU state.
External-write visibility, history advancement, presentation, and view-family
commit are published only by terminal commit hooks. Abort hooks release or
invalidate them. A callback may record commands; it cannot claim that those
commands were submitted. If it returns failure, the executor copies the status,
calls `CancelRemaining` on the current packet, invalidates every callback-visible
binding, unbinds and discards the current command list, classifies all current
and unsubmitted scopes, and then follows the normal Aborted or DeviceLost
terminal path according to prior submission evidence.

## 37. Truthful Frame Outputs And Presentation Import

Presentation is a specialized imported-texture terminal action, not a normal
texture transition and not a generic allocator export.

The allocator receives an immutable copy of the exact
`rhi::AcquiredBackBuffer` through a narrow presentation-import descriptor. It
validates that the acquisition is live and that its texture is exactly the
imported texture. Such an import is fixed to Present-to-Present, Graphics queue,
and same-queue continuation. Resolve emits exactly one semantic
`SwapChainPresentTransition` action at the terminal use instead of an ordinary
`TextureTransition`. Packet execution calls
`rhi::TransitionSwapChainPresent(acquisition)`. The backend requires the exact
acquisition, rejects duplicate recording, retains the swap chain, and registers
submission acknowledgement (`d3d12_backend.cpp:2668-2699`).

The allocator never acquires, presents, or abandons a back buffer. The retained
frame execution envelope owns a frame-output ticket containing the output kind
and complete `RenderOutputAcquisition`. `ViewportManager` remains the sole
ticket-consumption authority, but after dispatch its terminal operations are
thread-safe and serialized with main-thread lifecycle work by the per-viewport
presentation gate. The terminal action is one of:

```text
Present         graph completed, allocator Finish succeeded, terminal Graphics
                scope submitted with valid evidence, then native Present consumes
                the acquisition in the current frame
CompleteOutput  graph completed, allocator Finish succeeded, and texture output
                completion records its real terminal queue fence
Abandon         no submitted work touched the acquisition
DeviceLost      submission may have occurred; recovery consumes the ticket
```

The main thread reserves and acquires the ticket before dispatch. The retained
graph execution owns it after dispatch and calls the matching terminal
`ViewportManager` operation before its graph tail completes. That operation
acquires the viewport presentation gate, validates the viewport generation,
output revision, acquisition serial, and terminal receipt, then consumes the
ticket exactly once. It performs no terminal allocation and publishes any late
failure through the existing thread-safe render failure path.

After the prior frame's CPU continuation has joined, RenderUpdate collects late
failures and retirement state only; it does not present or abandon the previous
frame. Resize, rebind, viewport destruction, and shutdown quiesce or defer
behind the owning graph tail without holding the gate, then acquire the gate and
verify that no acquisition remains active before mutating the output.

The same settlement is part of explicit `EngineViewport::FlushFrame`; a caller
that deliberately flushes outside the engine frame boundary must not leave the
acquisition waiting for a later Presentation tick.

Presentation viewports acquire only when `RenderFrameInfo::ShouldPresent()` is
true. Texture viewports acquire their retained output for every submitted frame
and root the final imported version with `ExternalWrite`; headless viewports do
not manufacture an output ticket. A successful texture frame completes its
output in the current-frame terminal path with its actual terminal fence.
Failure before any submitted work touches that output may abandon it; ambiguous
completion or submitted use of that output uses DeviceLost rather than
pretending the texture is immediately reusable.

A Texture viewport descriptor and its stable owned viewport carry an explicit
same-queue continuation contract: supported continuation state,
Graphics-or-Compute queue, and last submitted ready fence. Creation validates
that the RHI texture supports the declared state. `AcquireOutput` freezes all
three fields in the immutable `RenderOutputAcquisition`; the ticket and graph
import use that exact per-frame contract, never later mutable viewport state.
Each graph import begins and ends in that same
state/queue; intermediate uses may transition normally. `CompleteOutput`
validates the copied contract and records the real terminal fence before
consuming the ticket. An external GPU user must continue on that queue or wait
for the reported fence; CPU access is not implied.

The initial graph renders to an offscreen color resource and touches the back
buffer only in one terminal Graphics pass. This sharply bounds the specialized
transition and makes abort reasoning tractable.

### Implemented RHI correction

The D3D12 presentation acknowledgement callback now distinguishes a real
Graphics submission fence from a default invalid fence. A default invalid fence
has `queue == Graphics` and `value == 0`, so checking the queue alone would let
discard or post-submit signal failure incorrectly acknowledge a present
transition. The callback requires both:

```cpp
completion.IsValid() && completion.queue == QueueType::Graphics
```

Discard supplies an empty fence, and a post-execution signal failure can
finalize callbacks before any valid queue fence exists. The corrected predicate
therefore leaves the acquisition unacknowledged on both paths. Present checks
the recorded and acknowledged acquisition, and abandonment additionally rejects
an acquisition whose transition has already received valid submission evidence.

The later authorized contract-test gate must prove transition-then-discard rejects Present,
post-execution fence-signal failure rejects Present and enters device recovery,
and valid Graphics submission permits Present. This is a presentation blocker,
not a reason to redesign the graph core.

Native Present failure is also a terminal ownership boundary. Once D3D12 marks
a swap chain Failed because the native Present, present-fence signal, or current-
buffer validation failed, it consumes its active acquisition and resets the
transition acknowledgement. `ViewportManager::Present` clears the caller token,
marks the viewport failed, and requests swap-chain or device recovery; it never
leaves the ticket pending for a retry and never follows a submitted Present with
`AbandonBackBuffer`. Precondition failure on a supposedly presentable ticket
is an internal contract violation and takes the same recovery-required path.
For a recoverable backend Present failure, the viewport releases/unbinds the
failed swap chain, increments its output revision, and enters `AwaitingOutput`.
The next Presentation-phase reconciliation therefore observes an invalid swap
chain and recreates/rebinds it before any new acquisition. Device-loss failures
take device abandonment instead. Recovery must never mark the same still-bound
failed swap chain Ready again.

## 38. Service And Frame Integration

`FrameRenderer` owns the graph runtime next to its resource allocator. It does
not own window or swap-chain reconciliation. `RenderingServiceImpl` becomes the
single owner of the existing `rendering::PresentationService`, alongside its
renderer, command system, and viewport manager. The engine service declares an
optional `WindowService` dependency, initializes presentation only when that
capability and a rendering device are available, exposes its generation-checked
output API through `RenderingService`, and registers one main-thread
Presentation-phase participant for window/swap-chain reconciliation.
Device-disabled/headless tool profiles keep the presentation owner inactive. A
second engine presentation service is not added. RenderUpdate observes terminal
failures and retirement after the CPU join; it does not drain tickets or perform
Present. The Presentation participant owns main-thread lifecycle reconciliation,
not graph completion.

The main-thread frame sequence is:

```text
RenderUpdate   join previous CPU tail, publish late failures and retirement
Presentation   reconcile window, viewport, and swap-chain state
Render         RenderingFrameTick publishes renderer-global inputs and creates
               the frame's CPU tail
Render         a dependent frame source acquires the output immediately before
               SubmitFrame appends worker execution to that tail; the terminal
               graph node consumes the output in this frame
```

The frame execution receives an owned ticket rather than an unowned token copy
inside `RenderFrameInfo`. Dispatch failure abandons an untouched acquisition on
the main thread immediately. Once worker execution can submit, only the terminal
outcome determines disposition.

Persistent retirement uses a separate device-wide successful-submission
watermark. The RHI exposes a `ResidencyFenceSet` snapshot updated only after a
queue signal succeeds; the backend's pre-incremented submission counters are
never treated as proof. Device initialization bootstraps one truthful no-op
submission on Graphics, Compute, and Copy so every cutover is nonzero, or fails
device initialization/recovery.

At RenderUpdate, after the previous CPU tail and output dispositions are joined
and reconciled, `RenderingServiceImpl` samples the successful watermark, calls
`SealResidencyRetirements(cutover)`, and only then calls collection. A frame
that submits one queue retains the last proven values for the other two; it
never fabricates a fence. Device-loss or post-execution signal failure takes
device abandonment without sealing unproved counters. Exact frame/scope
receipts remain the graph allocator's terminal evidence.

`RenderCommandSystem::completedFrames` and `lastCompletedSerial` keep their
current meaning—root renderer callback returned—unless deliberately renamed in
the integration stage. Graph terminal completion and GPU completion have
separate counters and must not reuse those fields implicitly.

## 39. Cache, Diagnostics, Budgets, And Deliberate Deferrals

The structural-template cache is required baseline behavior and is owned by
`FrameRenderer`. It is bounded by configuration and stores only immutable,
successfully sealed and compiled templates. A cache entry is addressed by a
fully comparable `RenderGraphTemplateKey`; a hash accelerates lookup but is
never accepted as equality proof.

The key contains every fact that can change topology or invariant compilation:
renderer/feature revision epochs, rendering mode and frame purpose, output kind
and structural format/sample properties, queue-policy flags, ordered view-slot
purposes/feature sets, camera-dependency shape, and every captured structural
decision. Exact extents or descriptors are included whenever the template does
not explicitly parameterize them. No topology-affecting global or console value
may be read without entering the key or an explicit invalidation epoch.

Templates may own copied names, resource/version schemas, canonical use lists,
edge reasons, root schemas, pass-data type operations, execution thunks, culled
structure, stable order, scopes, and executable queue lowering. They never own
frame-local pass payload values, `RenderViewId` mappings, imports, native
resource references, output acquisitions, allocator IDs/generations, submission
receipts, or terminal state. A frame instance maps ordered template view slots
to the current prepared view family and binds those mutable values without
changing topology. Instance binding is not a second planning callback and may
not redeclare or reinterpret resource access.

Cache misses build privately and publish atomically only after complete
validation and compilation. Failure leaves the prior cache unchanged. Eviction
removes lookup ownership but cannot invalidate an execution instance already
retaining the immutable template. Replacement and LRU metadata are protected by
the cache's own narrow lock; template compilation occurs outside that lock.
`ClearPersistentCaches` and renderer shutdown clear lookup ownership after the
normal render-tail quiescence boundary, while retained in-flight references
remain valid until their executions terminalize.

The graph has explicit configured budgets for passes, resources, versions,
uses, edges, scopes, roots, imports, retained payload/callback bytes, and total
owned diagnostic/name text bytes. Every copied pass/resource/root name, edge
reason, and failure message also has a fixed per-string maximum. Callback status
capture is charged to the retained payload and text budgets before publication.
Arithmetic overflow, capacity exhaustion, allocation failure, and provider
failure return typed errors in Shipping. Failure injection is instance-scoped;
no process-global test switch is introduced.

Required diagnostics are a stable text dump and optional DOT export containing
passes, resources/versions, edge reasons, roots, culling results, stable order,
scopes, queues, and lowering rejection. Baseline runtime statistics separate
candidate from live passes and CPU compile, record, submit, and terminal
timings. GPU timestamp-query allocation, resolve, readback, and presentation are
deferred together; the baseline does not report estimated GPU timings.

Deliberately deferred beyond the baseline exit gate:

- parallel command recording;
- pass/scope merging beyond one-pass scopes;
- range-precise dependency pruning;
- unrestricted Copy-queue crossings;
- generic allocator Export roots and an owning asynchronous result-delivery
  mailbox;
- Readback roots, readback-object lifetime, GPU-ready polling/mapping, and result
  delivery;
- GPU timestamp queries and GPU timing diagnostics;
- multi-GPU and vendor-specific scheduling;
- feature-specific renderer pass libraries.

These are extensions, not unfinished hidden behavior. Unsupported queue shapes
fail explicitly. Dedicated-resource reuse and placed alias activation remain
the allocator's existing responsibility and are usable by the baseline.

The future generic Export contract is already bounded by allocator truth: an
Export must name the final live version of one allocation lineage, one lineage
may publish to at most one export slot per frame, and preserving an older value
requires an explicit copy to a distinct graph resource. It does not enter the
public graph API until a bounded owner can take every `PublishedResourceExport`,
retain its ready fence, expose consumption, and apply back-pressure without
leaking allocator publication capacity. Readback is likewise deferred until its
pending-submission, pending-GPU, ready, mapping, and result-lifetime states have
an owning consumer.

## 40. Former V1 Copy, Adapt, Reject, And Vanguard Ledger

### Copy

- Unreal's one-shot builder, immutable declarations, resource-derived hazards,
  reverse culling, sentinels, and late physical resolution;
- RED's renderer ownership, Jobs continuation integration, command-scope
  concept, and explicit terminal stages;
- Vanguard's existing stable identities, typed failures, allocator packets,
  retained imports/owned exports, and full submission receipts.

### Adapt

- explicit typed use records replace Unreal reflection;
- explicit content versions remove ambiguous multiple-writer/output semantics;
- RED/Unreal queue models become a queue-neutral three-queue scope DAG followed
  by Vanguard-specific lowering;
- sentinel prologue/epilogue behavior is compiler-internal;
- side effects are named roots rather than an implicit no-parameter flag;
- future output extraction becomes owned publication after terminal completion;
  it is not exposed until its result owner and back-pressure policy exist.

### Reject

- RED's cached raw polymorphic node ownership and implicit secondary node-owner
  container;
- RED's double execution for lifetime discovery and registration-order GPU
  sequencing;
- Unreal's Graphics/AsyncCompute-only pipeline model and Copy-on-Graphics rule;
- any second scheduler, allocator, resource-state planner, or retirement owner;
- generic transition of an acquired back buffer to Present;
- assertion-only Shipping correctness and process-global failure injection;
- callback-side irreversible commits and fabricated queue fences.

### Vanguard-specific

- one canonical IR drives graph hazards, allocator replay, and runtime binding;
- every live pass maps exactly to one allocator node and initial command scope;
- all child work extends the existing RenderPath continuation;
- an exact-token presentation action is recorded by the allocator packet while
  `ViewportManager` retains ticket authority; its thread-safe terminal methods
  perform Present, Texture `CompleteOutput`, Abandon, or DeviceLost consumption
  in the current-frame graph under the presentation gate;
- partial submission and post-execution signal failure always produce truthful
  allocator receipts;
- the abstract three-queue graph remains broader than currently executable RHI
  lowering.

## 41. Former V1 Exit Gate (Withdrawn By R0)

The former V1 study concluded that every edge had one explicit meaning:

```text
resource or explicit graph edge -> GPU happens-before in the pass/scope DAG
same-queue scope order           -> queue submission order
cross-queue scope edge           -> validated RHI synchronization lowering
executor Job edge                -> CPU preparation/recording/terminal order only
```

It also concluded that every asynchronous object had one retained owner, every
allocator generation had one terminal path, and every acquired output had one
main-thread disposition. R0 withdrew that completion claim. These statements
remain evidence inputs only and do not authorize the former execution plan.

## 42. RED-Faithful Revision R1: Graph Representation And Ownership

R1 studies representation and ownership only. It settles what the graph stores,
who owns render-node implementations, how command-list groups own subnodes, how
camera graphs are composed, and how cached/in-flight graph lifetimes interact.
R2 studies authoring and composition semantics in detail; R3 studies the
declaration/execute split; R4 and R5 study CPU/GPU links and recording. Those
later subjects are not inferred here merely because their records are visible.

### R1 RED source snapshot

```text
D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraph.h
  lines: 399
  sha256: 33DBCE13082A095BB724CF80CDACDE919FAB032127BBBAB12D7797DEC8ACECC6

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraph.cpp
  lines: 938
  sha256: 98BEBCF439C51698B6194EF1450966E32F5ED6A20559DED2EC3C2FC2D2049962

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphArray.h
  lines: 211
  sha256: 6C1FE53A8DBD3B79DBCC40DDE52A85485CA4CC26753C77C71A873A15567FAB06

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphFactory.h
  lines: 188
  sha256: ED18E368D72AF245C64E98D8D927186816B90112D0D1638B964A7205E2DFA5D4

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeGraphFactory.cpp
  lines: 548
  sha256: FF47C138F3D48EF83E1A071C3C8179FC481A4347A31C02941C1C2BE439B58495

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.h
  lines: 1135
  sha256: 40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.cpp
  lines: 413
  sha256: B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphCache.h
  lines: 82
  sha256: F24870EB257B38D9B3776BC248C5CC04BE30C023F39A8DC19E3C5AD41EA13D78

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphCache.cpp
  lines: 72
  sha256: 9F6F769FD904801CDE3DEDCC8A85C666DC4444F42722564A0289764A69495DC9

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderGraphNodes.cpp
  lines: 2444
  sha256: 88CDF545C5C438A2807D85ED73310F1F608AB6D916DABF659E4123ABE64F7169

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderRenderFrame.cpp
  lines: 4980
  sha256: F2D36A9CF070D5CC0AFDBC828728781BC3BBE6FD7BC069FD5C8A487B9139E6D8

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderInterface.h
  lines: 1319
  sha256: 3858B296F3429F1A769726D8FAA9851C95812093EF9DCDAA527ECA1DF4BDBDCC

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderInterface.cpp
  lines: 3585
  sha256: 5E98A04FF8E0DCD281A532F51BA6F63226326E5CDF2366A93B219F2FDBDAC9DD
```

### Exact RED object model

RED separates a scheduled node record from the polymorphic object that performs
its work:

```text
CRenderNodeGraph
  owns node records
  owns CPU/GPU dependency records
  does not own CRenderNodeBase implementations

NodesContainer
  owns top-level CRenderNodeBase implementations through UniquePtr

SRenderNodeData
  stores SRenderNodeParameters
    -> raw CRenderNodeBase* implementation
    -> node type and subtype
  stores SRenderNodeContext
    -> camera index
    -> CPU render-flow group
    -> GPU render-flow group
  stores adjacency-list heads for CPU and GPU dependencies

CRenderNodeCommandListGroup : CRenderNodeBase
  is one top-level graph implementation
  owns its ordered child CRenderNodeBase implementations
```

`CRenderNodeBase` is RED's reusable node-implementation interface. Its virtual
contract supplies `Execute()`, name, command-list usage, optional command-list
creation, and job-builder usage (`renderNodeGraph.h:117-190`). The lightweight
graph record stores the implementation pointer separately from per-occurrence
camera and flow-group context (`223-259,287-331`). This distinction is important:
one implementation object may be referenced by multiple composed graph records
and executed once for each occurrence with different camera context.

The graph's nodes and edges live in separate ID-masked sparse arrays.
`TRenderNodeGraphArray` keeps stable item IDs while allocated, separately tracks
usage order, appends new allocations to that order, and may reorder usage entries
when an item is removed (`renderNodeGraphArray.h:10-40,57-98,113-178`). The IDs
distinguish node and dependency records by bit masks, but they have no generation
component. Stale-ID protection and capacity failures are primarily assertions.

RED node types are structural composition markers, not C++ ownership kinds:

- `RNT_Stage` is an ordinary graph occurrence;
- `RNT_Unique` merges one subtype across composed graphs;
- paired `RNT_SequenceBegin`/`RNT_SequenceEnd` helpers splice sequences when
  graphs are composed;
- `RNT_Temporary` is a helper-node category.

The type/subtype and the implementation object are deliberately separate. R1
preserves that separation rather than encoding uniqueness or sequence behavior
in the implementation's C++ type.

### Construction and top-level ownership

`NodeGraphFactory` receives both `CRenderNodeGraph*` and `NodesContainer*`. Its
constructor resets both immediately (`renderNodeGraphFactory.cpp:280-294`). A
new node is allocated polymorphically, adopted into a `UniquePtr`, pushed into
`NodesContainer`, and also registered as a graph record containing the same raw
pointer (`313-340`). The factory owns neither object after registration.

That produces this lifetime relationship:

```text
camera setup/cache entry
  owns NodesContainer
    owns CRenderNodeBase object
  owns CRenderNodeGraph
    stores non-owning pointer to that object
```

The `NodesContainer` comment calls it a glorified dynamic-array wrapper that
should probably be removed (`renderNodeGraphFactory.h:15-24`). The wrapper is
not a graph concept; its owned implementation arena is. Vanguard therefore
copies the owned arena responsibility, not the redundant wrapper type or the
raw-pointer coupling.

### Command-list group ownership

`BeginCommandListGroup()` creates a `CRenderNodeCommandListGroup`, registers it
as one top-level node, and temporarily retains a raw pointer while the group is
open (`renderNodeGraphFactory.cpp:352-369`). Nodes created before or after the
group are owned by `NodesContainer`. Nodes created while the group is open are
not registered as top-level graph records: the group adopts and owns them in an
ordered child array (`18-43,313-323`). Nested command-list groups are rejected.

The ownership shape is therefore:

```text
NodesContainer
  owns CRenderNodeCommandListGroup
    owns ordered subnode implementation 0
    owns ordered subnode implementation 1
    owns ordered subnode implementation N

CRenderNodeGraph
  contains one scheduled record for the group
  contains no scheduled records for those subnodes
```

This is an essential RED structure and is retained. The command-list group is a
first-class scheduled graph node and its subnodes are an ordered internal node
sequence, not independent passes that a compiler may later choose to merge.
R5 will settle its Vanguard recorder and allocator-packet execution contract.

### Composition, reuse, and merge ownership

`CRenderNodeGraph::AddGraph()` allocates destination records, copies node and
dependency data, reindexes every adjacency link, optionally replaces the copied
camera index, and then merges special nodes (`renderNodeGraph.cpp:350-406`). It
copies implementation pointers; it does not clone or transfer the owned
implementations.

Consequently, the source camera setup must outlive the final composed graph.
This is why `CacheEntry::PostBuildClear()` clears only the temporary camera graph
topologies and explicitly keeps their node owners alive
(`renderGraphCache.cpp:46-55`). The final graph remains valid only because its
raw pointers still point into those retained `NodesContainer` objects.

Composition can deliberately make several final graph occurrences reference the
same implementation. If two cameras have the same camera-setup hash,
`FindCameraSetup()` returns the already-built setup and `AddGraph()` copies its
records again with another camera index (`renderGraphCache.cpp:57-68`;
`renderRenderFrame.cpp:4705-4722,4792`). The implementation must therefore keep
per-frame/per-camera mutable state outside itself or otherwise be safe for this
reuse. A Vanguard node's single-execution rule means exactly once per surviving
scheduled occurrence per frame, not once per shared implementation object.

Unique-node merging keeps the original graph's implementation record, copies
dependencies from the incoming occurrence, and removes the incoming record
(`renderNodeGraph.cpp:490-503`). The unused incoming implementation remains
owned until its camera setup is destroyed. Sequence merging similarly rewires
and removes helper records without transferring implementation ownership
(`505-527`). Vanguard must validate that occurrences carrying the same unique
identity have compatible implementation type and structural configuration;
silently choosing the first incompatible object would preserve RED mechanics
but not a safe contract.

### Cache and in-flight lifetime

`CRenderInterface` owns `CRenderGraphCache` through one `UniquePtr`
(`renderInterface.h:591`; `renderInterface.cpp:1421,1559`). Each of its four
entries owns a final graph plus a dynamic array of owned per-camera setup
records. A cache miss first resets the final graph, then replaces every camera
setup owner, so old implementation objects are destroyed only after old graph
records stop referencing them (`renderGraphCache.cpp:11-43`).

After graph selection, RED acquires the graph's exclusive update flag before
building node jobs and releases it only in a cleanup job after those jobs finish
(`renderRenderFrame.cpp:4823-4835,4969-4977`). `Reset()` takes the same exclusive
guard before clearing graph records (`renderNodeGraph.cpp:921-928`). RED thus
prevents mutation while jobs access a cached graph, but the lifetime depends on
serialized cache reuse plus the external owner relationship described above.

Vanguard retains the behavioral guarantee with immutable strong ownership:
cache lookup returns a retained `RenderGraphDefinitionRef`; eviction removes
cache ownership but cannot destroy a definition held by a frame execution.
This is a lifetime correction at the existing cache/execution seam, not a change
to RED graph composition or renderer flow.

### Frame-local execution context ownership

`SRenderNodeImplContext` is copied for graph execution and for worker jobs. It
retains the render frame through `TRenderPtr`, but its allocator, camera storage,
camera data, and command-list access are borrowed from longer-lived frame or
renderer state (`renderNodeImplContext.h:94-110,486-509`;
`renderNodeImplContext.cpp:355-408`). Before each graph occurrence, RED installs
that occurrence's camera and GPU flow group in the context
(`renderNodeImplContext.cpp:313-333`; `renderNodeGraph.cpp:784-810,813-878`).

Per-node binding/unbind masks are mutable context state and are reset at each
node boundary, not cached implementation state (`renderNodeImplContext.cpp:
335-353`). RED does retain profiler state mutably inside `CRenderNodeBase`; that
is not suitable when one cached implementation can serve multiple occurrences.
Vanguard keeps all frame, view, resolved-resource, recorder, diagnostics, and
profiling state in the retained frame or its per-occurrence implementation
context. The cached implementation remains structurally immutable after
publication.

### R1 Vanguard representation and ownership mapping

Names remain provisional until the authoring study, but the responsibilities are
settled:

| RED type/responsibility | Vanguard R1 responsibility | Ownership decision |
|---|---|---|
| `CRenderGraphCache` | renderer-owned bounded graph cache | `FrameRenderer` owns cache lookup entries; each entry strongly owns a published definition. |
| `CacheEntry` | cached complete graph definition plus RED-equivalent per-view build metadata | Cache publication is atomic; replacement cannot invalidate retained executions. Exact lookup/rebuild policy is R8. |
| `SCameraSetupData` | per-view graph build/reuse record | Owns or references one build fragment inside the same candidate generation; no pointer may outlive that generation. |
| `CRenderNodeGraph` | graph topology: scheduled occurrence records plus separately typed CPU/GPU edges | Mutable only while building; immutable after successful publication. |
| `SRenderNodeData` | `RenderNode` occurrence record | Stores stable implementation identity, structural kind/subtype, view slot, CPU/GPU group metadata, and adjacency—not an owning/raw implementation pointer. |
| `CRenderNodeBase` | `RenderNodeImpl` polymorphic implementation | Retained exactly as RED's named implementation object, but owned by the same graph-definition generation and immutable after publication. |
| `SRenderNodeParameters` | implementation identity plus structural kind/subtype | Unique/sequence identity remains explicit graph data rather than RTTI or a diagnostic name. |
| `SRenderNodeContext` | occurrence-local view slot and compiled CPU/GPU group identity | Stored per occurrence so one implementation can be reused by multiple cameras. |
| `SDependencyData` | separately typed CPU/GPU edge record | Uses generation-checked node identities and bounded owned adjacency. Edge meaning is settled in R4. |
| `NodesContainer` | graph-definition-owned node implementation arena | No standalone wrapper and no graph-to-external-owner raw pointer. The owning arena and topology belong to one retained generation. |
| `CRenderNodeCommandListGroup` | command-list-group node implementation | Is one scheduled top-level occurrence and directly owns its ordered subnode implementations, as RED does. |
| `NodeGraphFactory` | bounded graph factory/builder | Mutates one private candidate generation and cannot publish partially built topology. Detailed authoring surface is R2. |
| `SRenderNodeImplContext` | per-occurrence `RenderNodeImplContext` | Frame-local; borrows retained-frame state under the serialized CPU tail, exposes only phase-valid capabilities, and never becomes cached node data. Detailed declaration/execution callbacks are settled in R3. |

The resulting ownership topology is:

```text
FrameRenderer
  owns RenderGraphCache
    owns published RenderGraphDefinitionRef

RenderGraphDefinition generation
  owns immutable node implementation arena
    owns top-level RenderNodeImpl objects
      command-list-group implementations own ordered subnode implementations
  owns immutable scheduled RenderNode occurrence records
    records reference implementations by generation-checked ID
  owns immutable CPU/GPU edge records and composition metadata

Retained frame execution
  strongly retains RenderGraphDefinitionRef
  owns frame/view bindings and per-occurrence execution state
  owns or retains the allocator generation and terminal transaction
```

During a cache-miss build, one private candidate generation owns both its node
implementation arena and all temporary per-view/final topology records. Graph
composition copies and reindexes occurrence records while retaining stable
implementation IDs, exactly matching RED's ability to reuse an implementation
for several camera occurrences. Sealing clears unneeded fragment topology only
after the final topology has been validated, then atomically transfers the whole
owning generation into the immutable published definition. There is never a
moment when a published graph record depends on an owner stored elsewhere.

### R1 copy, adapt, and reject decisions

Copy from RED:

- polymorphic, named render-node implementations;
- separate scheduled occurrence records and implementation objects;
- explicit node kind/subtype for regular, unique, and sequence composition;
- per-occurrence camera/view and CPU/GPU flow metadata;
- separately represented CPU and GPU dependency records;
- command-list groups as scheduled nodes that own ordered subnodes;
- reusable per-camera graph construction followed by whole-frame composition;
- reuse of one immutable implementation by multiple camera occurrences;
- renderer ownership of the bounded cache and graph lifetime.

Adapt only for Vanguard safety and existing seams:

- the graph definition generation directly owns its implementation arena instead
  of depending on a sibling `NodesContainer` and raw pointers;
- graph records use generation-checked stable IDs and typed capacity/allocation
  failures rather than bit-masked assertion-only IDs;
- cached definitions are immutable and strongly retained by in-flight execution,
  so eviction need not block on or invalidate readers;
- duplicate unique identities require compatible implementation type and
  structural configuration;
- mutable per-frame state belongs to execution contexts/instances, never cached
  implementations;
- cache-miss construction publishes atomically only after successful validation.

Reject as representation/ownership defects, not as RED graph concepts:

- the standalone `NodesContainer` wrapper;
- raw implementation pointers whose true owner is external to the graph;
- stale IDs without generation validation;
- assertion-only mutation, capacity, and identity protection;
- unused implementation objects left behind after unique-node merging when they
  can be destroyed safely during candidate compaction;
- cached mutable profiling or execution state inside a shared implementation.

R1 deliberately does not reject RED's `RenderNodeImpl`, execution-context,
command-list-group, subnode, unique-node, sequence-node, or per-camera composition
models. Those are now required Vanguard architecture.

### R1 exit state

R1 is complete. Every RED representation has an explicit Vanguard owner and no
required RED graph concept was removed. The only ownership changes eliminate
hidden raw-pointer lifetime, stale identity, partial publication, and in-flight
eviction hazards without changing RED's visible renderer structure.

At the R1 exit, R2 remained next: trace RED graph authoring and composition line
by line, including factory groups, unique/sequence helpers, command-list macros,
`AddGraph()`, and representative blank/no-scene/camera graph builders. No
authoring API spelling was approved by R1.

## 43. RED-Faithful Revision R2: Graph Authoring And Composition

R2 studies how RED authors a graph fragment and combines per-camera fragments
into the cached final graph. It does not decide node declaration/execution,
command-list recording, resource allocation, or synchronization lowering. Those
belong to R3 through R7.

R2 reuses and has reverified the pinned RED files in the R1 source snapshot. Their
SHA-256 values are unchanged. The primary evidence is
`renderNodeGraphFactory.h:28-188`, `renderNodeGraphFactory.cpp:280-548`,
`renderNodeGraph.cpp:350-528`, `renderGraphCache.cpp:11-68`, and
`renderRenderFrame.cpp:1449-1488,1829-3295,3683-3752,4482-4507,4644-4798`.

### RED factory authoring model

`NodeGraphFactory` authors one fresh graph fragment. Constructing the factory
resets both the graph topology and its separate node-owner container. Every
top-level creation performs four operations:

1. construct a polymorphic `CRenderNodeBase` implementation;
2. transfer implementation ownership to `NodesContainer`;
3. add one graph occurrence containing type, subtype, implementation pointer,
   and an initially zero camera index;
4. tag the occurrence with a `NodeGroupID` and remember its declaration order.

`CreateCustom()` accepts already assembled node parameters but follows the same
registration path. Direct links are deduplicated by `LinkImpl()` and retain an
explicit `RNDT_Cpu` or `RNDT_Gpu` domain.

The RED macros are shorthand, not a separate graph representation:

| RED authoring form | Graph meaning |
| --- | --- |
| `ADD_NODE` | Add one ordinary top-level stage occurrence. |
| `ADD_UNIQUE` | Add one frame-global merge candidate identified by its unique subtype. |
| `ADD_SEQ_BEGIN` / `ADD_SEQ_END` | Add a matched helper pair delimiting a sequence that will be concatenated across composed fragments. |
| `RENDER_COMMAND_LIST` | Add one graphics command-list-group occurrence, then append ordered child implementations to it. |
| `COMPUTE_COMMAND_LIST` | The same ownership shape with a compute command-list type. |
| `RENDER_UNIQUE_COMMAND_LIST` | Add a command-list group that is also a unique merge candidate. |
| `RENDER_SIMPLE_COMMAND_LIST` | Add a command-list group containing exactly one child. |
| `SYNC_SUBMIT` | Add a synchronization node and make it CPU-dependent on preceding command-list-using top-level nodes. |

The textual `name` argument to RED's `ADD_NODE`, `ADD_UNIQUE`, `ADD_SEQ_BEGIN`,
and `ADD_SEQ_END` macros is not forwarded by those macros. Command-list-group
names are forwarded and used. Vanguard must not reproduce an accepted-but-
ignored parameter: a supplied debug name is stored, and identity remains a
separate typed key.

### RED has two different group mechanisms

`NodeGroupID` is a dependency tag, not a command-list or ownership group. Every
top-level occurrence may be tagged with one group. Linking node-to-group,
group-to-node, or group-to-group lazily creates synthetic input/output nodes and
fans CPU dependencies across all tagged members. RED explicitly permits only
CPU dependencies through this facility. The synthetic endpoints are connected
even for an empty group, allowing optional feature groups to remain linkable.

The lazy RED implementation snapshots the members known when a synthetic
endpoint is first created. Its builders avoid adding later members by creating
all nodes before the link block. Vanguard preserves the visible behavior but
does not preserve this ordering trap: group membership is collected during
authoring and expanded or represented when the candidate graph is sealed.
Adding members after sealing is an error.

A command-list group is different. It is one top-level executable occurrence
that owns an ordered array of child implementations. While such a group is
open, `Create()` adds children to the group and returns no graph node identity.
Only one group may be open, nesting is unsupported, and RAII closes the group.
The final graph links the command-list group as a unit; its children are not
independently linkable top-level occurrences. R5 will study how that ordered
child list is recorded and submitted.

Vanguard therefore keeps two visibly different concepts:

```text
dependency set       = named/tagged collection used by CPU graph links
command-list group   = one executable graph occurrence owning ordered child nodes
```

They must not share an identifier type or a generic `Group` API.

### Declaration-order helpers

RED records every top-level occurrence and synthetic dependency endpoint in
`m_nodeIds`. `LinkGPU()` walks adjacent entries and creates a GPU edge chain.
`LinkCPU()` does the same for CPU edges. `LinkCPUToNextGPU()` and
`LinkCPUToPreviousGPU()` scan declaration order and connect one CPU node to all
later, or all earlier, command-list-using top-level nodes.

These helpers are real RED authoring features and remain in the Vanguard model.
They must have explicit names such as `LinkGpuInDeclarationOrder` rather than
being hidden inside finalization. R4 and R7 must determine whether each RED use
is a scheduling edge, a submission edge, or an implicit synchronization
assumption before the helpers receive final lowering semantics. R2 does not
pretend declaration order alone synchronizes independent GPU queues.

`NodeGraphFactory` also contains `LinkIf`, which scans top-level graph
occurrences and links those whose implementation satisfies a predicate. No RED
call site uses it in the audited renderer. Vanguard does not need to expose it
as a first-class public operation unless a concrete authoring case appears;
typed collections of returned node identities provide the same capability
without runtime implementation inspection.

### Unique-node composition

`AddGraph()` first copies and reindexes all source occurrences and dependency
records. It may overwrite every copied occurrence's camera index. When merging
is enabled, RED then identifies special nodes on the old and appended sides.

For each matching `RNT_Unique` subtype, the old occurrence survives. Dependencies
from the appended occurrence are copied to the survivor and the appended graph
record is removed. This is how `StartRender`, `EndRender`, final submission,
Present, extraction flushes, cleanup, and end-of-frame nodes become one
frame-global occurrence when multiple camera fragments are composed.

The preserved semantic rule is:

```text
first unique occurrence wins implementation and occurrence identity
later matching occurrences contribute dependencies but do not execute
```

RED rejects duplicate special subtypes inside either input graph and uses a
fixed 64-entry subtype table. Vanguard uses a typed `RenderNodeUniqueKey`, has no
arbitrary 64-key limit, and validates that colliding occurrences have compatible
implementation type and global-node contract. A collision between unrelated
node types is a build failure, not a silent merge. A unique node must be
frame-global; it cannot read a per-camera execution context whose selected value
would otherwise depend on which camera was composed first.

### Sequence composition

A RED sequence is a matched begin/end helper pair sharing a sequence subtype.
When both the accumulated graph and appended fragment contain the pair,
`MergeNodes()` rewires the boundaries so the old sequence body precedes the new
sequence body. Helper nodes are later removed while their compatible
dependencies are bridged. Repeated `AddGraph()` calls therefore concatenate
camera regions in camera-array order:

```text
global start -> camera 0 body -> camera 1 body -> ... -> global end
```

Work outside the matched sequence is not automatically serialized this way.
Frame-global unique nodes merge independently, and ordinary nodes outside a
sequence remain distinct occurrences.

Vanguard preserves paired typed sequence keys and append-order concatenation.
Sealing fails if a fragment has a missing partner, duplicate begin/end key,
crossed pair, or incompatible nesting. RED only uses one camera sequence in the
audited builders, but Vanguard's validation must make the general paired-marker
contract truthful rather than depending on fatal assertions.

### Camera-fragment caching and final composition

RED builds an ordered whole-frame hash from camera count, display mode, selected
global structural settings, and every camera's structural hash. A camera hash
includes rendering mode, display mode, full-scene availability, and every word
of the camera feature bitset. Frame-dynamic values that do not alter topology
are not part of graph authoring.

On a whole-frame cache miss, RED prepares one or more `SCameraSetupData` slots.
Within that entry, cameras with equal setup hashes reuse the same built fragment
and node implementations. For every camera occurrence, including reuse of the
same setup, the final graph calls `AddGraph()` with the actual camera index and
merge enabled. This is the concrete reason R1 separated a node implementation
from a scheduled occurrence.

Final composition is therefore:

```text
select whole-frame cache entry
  -> build each distinct structural camera fragment at most once
  -> append one fragment occurrence for each camera in camera-array order
  -> force that occurrence set to the camera index
  -> merge unique nodes and concatenate camera sequences after every append
  -> build render-flow groups on the composed graph
  -> discard temporary fragment topology while retaining referenced implementations
```

Vanguard keeps the same two-level reuse: a whole-frame definition cache plus
deduplication of equal camera definitions inside a build. R8 will finalize key,
eviction, and concurrent publication details. R2 requires keys to be complete
structural values and composition to be transactional: an invalid fragment or
merge leaves no partially published definition.

### Representative RED builders

`BuildRenderGraphBlank()` proves the smallest complete authoring shape. It adds
unique start/end command-list groups, one graphics command-list group containing
ordered blank/prewarm/composition/HUD/extraction children, then frame-global
submission, Present, flush, cleanup, and end nodes. It closes with declaration-
order GPU and CPU chains.

`BuildRenderGraphNoScene()` proves that CPU preparation and GPU recording are
not flattened into one pass list. It creates `PreRender` and `Render` dependency
sets, a matched camera sequence, standalone CPU preparation nodes, a graphics
command-list group with ordered children, and explicit CPU fan-in/fan-out links.
The render group, not every child, is connected to the surrounding graph.

`BuildRenderGraphCamera()` proves the full model. Camera feature bits select
which nodes and command-list groups exist while the fragment is built. The
builder creates independent culling, particle, render-preparation, render, and
post-render dependency sets; explicit node and set links form CPU branches and
joins; command-list groups preserve local recording order; special nodes define
frame-global work and the camera sequence; and declaration-order GPU links are
added separately. Feature conditions change cached topology and are not runtime
branches hidden inside a node execution function.

The Vanguard translation must retain these authoring capabilities:

| Required capability | Vanguard R2 contract |
| --- | --- |
| Structural feature branching | Ordinary C++ conditions choose nodes and groups while a private definition candidate is built. Structural inputs are part of the definition key. |
| Named polymorphic nodes | Typed creation constructs a named `RenderNodeImpl` in the candidate's owned arena and returns a stable occurrence identity. |
| CPU-only preparation nodes | They remain graph nodes and may branch or join independently of command scopes. |
| Ordered GPU subnodes | A graphics or compute command-scope node owns them in authoring order. They are not converted into unrelated passes. |
| Dependency sets | Nodes can be tagged and linked through explicit CPU set boundaries, including empty optional sets. |
| Direct dependencies | Node-to-node CPU and GPU domains remain distinct at authoring time. |
| Frame-global nodes | Typed unique keys merge compatible occurrences and all incident dependencies. |
| Per-camera serialization | Typed sequence pairs concatenate camera bodies in composition order. |
| Fragment reuse | Equal structural camera definitions can share one implementation arena while producing distinct camera-indexed occurrences. |
| Final graph assembly | Append, reindex, merge, validate, and publish are one transactional operation. |

### R2 copy, adapt, and reject decisions

Copy from RED:

- a factory that authors one reset/private graph fragment;
- named polymorphic top-level nodes and returned occurrence identities;
- explicit CPU and GPU link domains;
- dependency-tag sets with node/set overloads and empty-set support;
- non-nestable graphics and compute command-list groups with ordered children;
- structural C++ feature branching during graph construction;
- unique nodes, paired sequences, per-camera fragments, and `AddGraph()`-style
  composition;
- declaration-order helper operations as visible author intent;
- two-level whole-frame and distinct-camera-definition reuse.

Adapt for Vanguard correctness:

- the candidate definition owns node implementations directly;
- authoring returns generation-checked typed identities;
- debug names are stored instead of accepted and ignored;
- dependency-set membership is sealed before expansion;
- special identities use typed keys without RED's fixed subtype-table limit;
- unique collisions validate compatible implementation/global semantics;
- sequence pairs and composition are validated before mutation;
- cache publication is atomic and in-flight definitions remain retained;
- resource-use declaration is attached before execution through the allocator
  seam studied in R6.

Reject as non-semantic RED accidents:

- the macro-only public surface;
- a `NodesContainer` separate from the graph-definition owner;
- ignored node-name arguments;
- raw numeric group/subtype collisions;
- lazy group endpoints that omit members added later;
- fatal/assert-only handling of malformed unique or sequence composition;
- an implicit destructor-only CPU chain controlled by the disabled
  `ENABLE_DEBUG_COMMANDLISTS_EXECUTION` define;
- interpreting `LinkGPU()` as sufficient cross-queue synchronization before the
  R4/R7 audit.

### R2 exit state

R2 is complete. RED's full graph-construction vocabulary and camera-composition
behavior are retained. The Vanguard differences are bounded to typed identity,
owned lifetime, atomic publication, explicit naming, and fail-closed validation;
none removes nodes, CPU branches, command groups, unique work, camera sequences,
or fragment caching.

R3 is next: study `CRenderNodeBase`, `Process()`, `Execute()`, consume/non-consume
phases, `SRenderNodeImplContext`, and representative CPU/GPU node classes to
define Vanguard's node declaration and exactly-once execution contract. Final
API spelling and edits to the provisional authoring examples remain deferred.

## 44. Presentation Threading Decision: Present Is A Current-Frame Graph Node

This section supersedes the former V1 proposal in Sections 6, 28, 37, and 38
that returned a completed presentation ticket to a main-thread drain at the next
frame boundary. That extra handoff is rejected. It breaks RED's visible terminal
graph chain, delays presentation ownership into another frame, and makes the
frame that recorded the work differ from the frame that consumes its output.

RED supplies direct evidence for the intended structure. Every representative
builder adds a unique Present node after final submission, and
`CRenderNode_Present::Execute()` calls the viewport during graph execution when
the frame requests presentation (`renderGraphNodes.cpp:359-372`). Vanguard keeps
that topology.

### Same-frame terminal chain

For a presentation viewport, the current frame owns this chain:

```text
main-thread frame preparation
  -> reconcile window, viewport, and swap-chain lifecycle state
  -> acquire the exact back-buffer ticket
  -> bind that ticket as frame-local data to the selected graph definition
  -> dispatch the RenderPath graph

current-frame graph execution
  -> record the exact token-aware Present transition
  -> submit the terminal graphics scope and retain its truthful receipt
  -> execute the terminal Present node
  -> consume the acquisition exactly once as Presented, Abandoned, or DeviceLost
  -> complete the graph terminal chain
```

The Present node does not wait for GPU completion. It becomes CPU-runnable only
after the command list containing the token-aware Present transition has been
successfully submitted with the required queue ordering. Native Present then
queues presentation against that submitted work through the RHI/backend
contract. A submitted-without-signal result remains device-loss/unknown evidence
and is never relabeled as an unsubmitted abandonment.

There is no `PresentReady` mailbox whose normal success path is consumed during
the next frame's RenderUpdate or Presentation phase. The next frame boundary may
join the previous RenderPath tail for failure collection, allocator retirement,
or lifecycle mutation safety, but it does not perform the previous frame's
Present.

### Thread ownership

The agreed split is:

| Operation | Thread contract |
| --- | --- |
| Window-state capture and reconciliation | Main thread only. |
| Viewport creation and destruction | Main thread only. |
| Swap-chain creation, binding, unbinding, recreation, and resize | Main thread only. |
| Render-output acquisition | Main thread only, after reconciliation and before graph dispatch. |
| Token-aware transition and command submission | Render Graph worker/recording path. |
| Present of a valid retained acquisition | Thread-safe terminal Render Graph node in the current frame. |
| Abandonment or device-loss consumption of a dispatched frame's acquisition | Thread-safe graph terminal/failure path in the current frame. |
| Synchronous dispatch failure before graph ownership transfers | Main-thread caller consumes the acquisition before returning. |

`PresentationService` therefore remains the main-thread owner of window and
swap-chain reconciliation. It does not own a queue of successful frames waiting
to be presented. `ViewportManager` remains the sole authority for acquisition
identity, output revision, exactly-once consumption, presentation statistics,
and viewport state, but its ticket-terminal operations can no longer reject all
non-main-thread callers.

### Required serialization

Making Present thread-safe does not mean allowing it to race resize or
destruction. Each render viewport requires one narrow presentation-operation
gate shared by:

- acquisition;
- Present, abandonment, and device-loss consumption;
- swap-chain bind/unbind/recreate/resize;
- viewport destruction.

The gate protects the output revision, active acquisition identity, swap-chain
binding, lifecycle state, and exactly-once disposition. The global viewport
registry lock, if retained, is used only to resolve and retain the per-viewport
state; it is not held across native Present, waiting, or window callbacks.

Main-thread lifecycle mutation follows this rule:

```text
request mutation
  -> mark or observe the viewport as unavailable for new acquisition
  -> wait for or defer behind the owning graph tail without holding the presentation gate
  -> acquire the gate
  -> verify that no acquisition remains active
  -> resize, rebind, recreate, or destroy
```

Worker-side terminalization acquires the same gate, validates viewport
generation, output revision, back-buffer acquisition serial, and expected
terminal state, then performs exactly one RHI Present/Abandon/DeviceLost action.
No path may both present and abandon. No lifecycle operation may invalidate the
swap chain underneath an active terminal node.

The current `ViewportManager::Present()` and `AbandonOutput()` implementations
enforce `concurrency::IsMainThread()` (`viewport.cpp:492-533`). Production graph
integration must replace those checks with the serialized ticket-terminal
contract above. `AcquireOutput()`, viewport mutation, and
`PresentationService::Reconcile()` remain main-thread-only. Renderer shutdown
must quiesce the RenderPath tail before destroying the manager or its retained
per-viewport operation state.

### Graph and cache consequences

Present is a frame-global unique node in presentation-capable definitions, just
as it is in RED's composed camera graph. The cached node implementation contains
no viewport pointer or acquired back-buffer token. The execution occurrence
reads the current frame's retained output transaction from
`RenderNodeImplContext`; therefore graph-definition reuse never reuses an
acquisition, output revision, or presentation result.

Texture and headless outputs keep their own terminal nodes or terminal outcomes;
they are not routed through native Present. A frame that does not transfer a
valid acquisition to graph execution remains the caller's responsibility and
must consume or abandon it synchronously.

R3 must preserve Present as an ordinary named node implementation with one
current-frame execution occurrence. R5 must place it after truthful terminal
graphics submission. R6 must provide the exact acquisition-aware allocator
transition. R8 must integrate shutdown, recovery, cache reuse, and the terminal
chain without reintroducing a next-frame Present drain.

## 45. RED-Faithful Revision R3: Node Declaration And Single Execution

R3 studies the contract of one named node implementation. It preserves RED's
polymorphic node classes, CPU-only nodes, GPU-recording nodes, synchronization
nodes, child-job-producing nodes, context helpers, and engine-controlled wrapper.
It changes one deliberate seam: resource planning is no longer obtained by
calling the node's execution body once to discover resources and again to do the
work.

### Exact RED processing contract

RED's `CRenderNodeBase` exposes one virtual `Execute(const
SRenderNodeImplContext&, job::Builder*)` plus static capability queries for
command-list and child-job usage (`renderNodeGraph.h:109-168`). Its nonvirtual
`Process()` wrapper performs the surrounding engine work
(`renderGraphNodes.cpp:107-235`):

1. read the allocator phase and command-list usage;
2. create an `Own` command list or bind the inherited command list when the
   current phase is Consume;
3. begin profiling and temporarily unlock declared global-binding slots;
4. reset per-node binding state and call the virtual `Execute()`;
5. unbind resources, restore global bindings, close profiling, and unbind the
   command list;
6. when the node launches child jobs, dispatch that epilogue behind the child
   continuation instead of running it immediately.

The frame invokes that same node body in two distinct traversals. It first sets
the allocator to PreConsume and calls `ExecuteParallel()`
(`renderRenderFrame.cpp:4940-4949`); that traversal shuffles occurrences into
parallel buckets and calls `Process()` under the job name
`FlowAllocator_PreConsume_Batch` (`renderNodeGraph.cpp:813-878`). After allocator
Resolve, the CPU dependency graph dispatches one RenderPath job per occurrence,
and `RunRenderJob()` calls `Process()` again (`renderRenderFrame.cpp:4254-4279,
4298-4474`). `SRenderNodeImplContext::SetupNodeData()` snapshots
`IsConsumePhase()` from the allocator (`renderNodeImplContext.cpp:313-332`).

That is why RED production nodes contain many `IsConsumePhase()` branches. For
example, `CRenderNode_StartRender` performs real frame startup only during
Consume but issues resource allocations outside that branch
(`renderGraphNodes.cpp:301-345`). `CRenderNode_Synchronize` declares the queue
sync in both traversals but submits command lists only during Consume
(`renderGraphNodes.cpp:279-297`). `RTDecision()` stores the PreConsume result and
replays it during Consume (`renderNodeImplContext.h:273-275`,
`renderNodeImplContext.cpp:291-299`, `renderFlowInternalData.cpp:929-1003`).

This dual invocation is functional, but it mixes two contracts inside every
node body. Omitting or misplacing one phase branch can duplicate CPU side
effects, issue GPU calls during planning, or make the resource request stream
diverge from recording. Vanguard's already-built allocator gives us a precise
way to retain the behavior without retaining that hazard.

### Vanguard node interface

Vanguard keeps named polymorphic implementation classes. The following is the
settled shape, while exact header spelling remains an R9 task:

```cpp
class RenderNodeImpl
{
public:
    virtual ~RenderNodeImpl() = default;
    virtual bool Declare(RenderNodeImplContext& context, RenderGraphFailure* failure) const noexcept { return true; }
    virtual bool Execute(RenderNodeImplContext& context, RenderGraphFailure* failure) const noexcept = 0;
    virtual RenderNodeCommandListUsage GetCommandListUsage() const noexcept = 0;
    virtual bool UsesChildJobs() const noexcept { return false; }
};
```

There is no public pass lambda and no generic callback standing in for a node
implementation. Builders construct concrete `RenderNodeImpl` objects, as RED's
factory constructs concrete `CRenderNodeBase` objects. A node's main behavior
remains in its named `Execute()` method.

There is also no mandatory third `Prepare()` virtual. CPU preparation is real
node work, not graph construction. A culling or collector-preparation node uses
`RenderNodeCommandListUsage::None`, declares whatever resource relationships it
needs, and performs its CPU work once in `Execute()`. Adding a separate virtual
would split RED's node vocabulary without buying a correctness boundary.

### Exactly-once meaning

After structural definition construction has omitted disabled topology,
`Declare()` is called exactly once for each candidate scheduled occurrence in a
frame. Immutable frame and camera inputs are attached before that call. The
resulting declaration tapes let the graph derive resource hazards and roots,
cull execution occurrences, and then replay only surviving tapes into allocator
planning writers before `Resolve()`. Reusing one implementation in two camera
occurrences therefore produces two declarations with two camera identities.
Cached graph definitions retain implementations and topology; declaration
output is retained by the retained frame, never written into the cached
implementation.

`Execute()` is called exactly once for each surviving scheduled occurrence in
that frame. It is not called for resource discovery, compilation, replay, retry,
or validation. A failed occurrence terminalizes according to the compiled graph
failure policy; the scheduler never calls `Execute()` again in an attempt to
recover it.

```text
cached definition + immutable frame/camera inputs
  -> instantiate structurally enabled candidate occurrences
  -> Declare each candidate occurrence once and seal declarations
  -> derive resource hazards and roots, then cull execution occurrences
  -> replay surviving declaration tapes into allocator planning writers
  -> RenderFlowResourceAllocator resolve and publish packets
  -> schedule CPU/GPU dependency graph
  -> Execute occurrence once
  -> engine epilogue and occurrence completion
```

### Declaration-mode context facilities

`RenderNodeImplContext` is narrow and side-effect-free when initialized for `Declare()`. It identifies the
definition generation, scheduled occurrence, camera or frame-global scope, flow
group, intended queue/command-list mode, and immutable frame inputs needed to
describe resources. It can:

- declare texture and buffer creation, import, use, and terminal disposition;
- open and close explicit resource-use scopes;
- capture a stable decision from immutable frame/camera policy;
- report a typed failure.

It cannot obtain native resource handles, bind or create command lists, issue
RHI calls, mutate scene/frame runtime state, dispatch jobs, present, or change
graph topology. Declaration produces frame-local allocator input. This makes it
impossible to hide a resource use behind an execution-only branch.

A resource-affecting conditional is evaluated during `Declare()` and assigned a
typed captured-decision identity. `Execute()` reads the compiled value from its
execution packet. Resource selection may not be recomputed from mutable runtime
state. Conditions that affect only CPU calculations or draw count, while using
the same declared resource set, may remain ordinary execution logic. R6 will map
this rule to the allocator's existing `DecisionId` and captured-decision API.

### Execution-mode context facilities

The same `RenderNodeImplContext` is initialized with execution capabilities before `Execute()`. It remains a frame-local per-occurrence object analogous to RED's `SRenderNodeImplContext`, but concrete nodes never branch on `IsConsumePhase()`, and execution receives no resource-allocation methods. It provides only validated execution facilities:

- the retained frame and immutable frame/camera view;
- the scheduled node, flow group, command scope, queue, and worker identity;
- the exact compiled allocator packet cursor and resolved-use accessors;
- the already-created or inherited command recorder allowed by the node mode;
- captured-decision lookup;
- a child-job continuation only when `UsesChildJobs()` is true;
- the exact current-frame output transaction for a terminal output node;
- typed failure reporting and occurrence-local diagnostics.

The context may borrow state only while the serialized retained-frame lifetime proves its
lifetime. A worker copy changes worker identity while retaining the same frame,
definition, occurrence, camera, and packet identities. Raw allocator access is
not exposed: resource access must pass through the compiled cursor, preserving
the allocator's ordered begin/end protocol.

RED stores binding cleanup masks in `SRenderNodeImplContext`
(`renderNodeImplContext.h:486-509`) but stores the active GPU profiler ID as a
mutable member of `CRenderNodeBase` (`renderNodeGraph.h:123-137`). Vanguard keeps
all such mutable recorder, cleanup, profiler, and failure state in the execution
occurrence. A cached implementation can therefore be reused concurrently across
cameras without an implementation-level data race.

### Command-list capability is preserved

R3 retains RED's four command-list meanings; R5 will determine their exact
lowering and submission mechanics:

| Mode | Vanguard contract |
| --- | --- |
| `None` | The node performs CPU or terminal work and cannot access a command recorder. |
| `Require` | The node records into the command scope already assigned to its occurrence. |
| `Own` | The occurrence is an explicit command-scope owner and receives the recorder created for that scope. It cannot create an untracked recorder itself. |
| `Sync` | The node performs an explicit submission/synchronization operation and cannot record ordinary commands. |

The scheduler's nonvirtual processing wrapper validates the mode in Shipping,
opens the allocator packet cursor when required, binds the assigned recorder,
begins diagnostics, invokes `Execute()` once, waits for the node's declared child
continuation, performs cleanup, finalizes the packet, and only then completes the
occurrence. Node code cannot bypass this wrapper.

### CPU-only and child-job nodes remain first-class

RED proves that a render node is not synonymous with a GPU pass.
`CRenderNode_ExtractionSelectionProcess` and
`CRenderNode_SimulateOnScreenCPUParticles` use no command list and perform CPU
work during Consume (`renderNode_Extractors.cpp:313-324`,
`renderNode_Particles.cpp:13-22`). `CRenderNode_PrepareCollector` dispatches a
culling preparation job (`renderGraphNodes.cpp:2037-2065`), and
`CRenderNode_SimulateOffScreenCPUParticles` dispatches four child jobs followed
by a fence (`renderNode_Particles.cpp:36-51`). Vanguard preserves all of these
forms as ordinary node implementations.

RED always passes a builder to `RunRenderJob()`, while
`GetJobBuilderUsage()` only tells `Process()` whether a command-list epilogue
must be dispatched behind work appended by `Execute()`
(`renderGraphNodes.cpp:175-189`, `renderRenderFrame.cpp:4260-4279`). This is an
indirect contract: CPU-only `CRenderNode_PrepareCollector` dispatches a child job
without overriding the default flag because it has no command-list epilogue.

Vanguard makes that latent contract explicit with `UsesChildJobs()`. When false,
requesting a child continuation is a typed contract failure. When true, all
child jobs and the engine epilogue belong to the occurrence's completion
continuation. Downstream CPU nodes cannot start merely because the parent
`Execute()` method returned while its descendants are still running. R4 will
prove the exact Jobs counter topology.

### Terminal nodes remain ordinary named implementations

`Present`, output extraction, end-frame cleanup, and submission synchronization
are node kinds, not out-of-band callbacks. In particular, the current-frame
Present decision in Section 44 maps to a `RenderNodeImpl` with command-list mode
`None`: its `Declare()` identifies the terminal output relationship, and its
single `Execute()` consumes the retained acquisition only after R5's submission
dependency is satisfied. It stores no viewport pointer or acquisition in the
cached implementation.

### R3 copy, adapt, and reject decisions

Copy from RED:

- named polymorphic node implementations and a nonvirtual engine wrapper;
- CPU-only, command-recording, command-owner, synchronization, terminal, and
  child-job-producing nodes;
- a rich per-occurrence context carrying frame, camera, flow, recorder, and
  resource facilities;
- wrapper-owned recorder binding, profiling, binding cleanup, and epilogue
  ordering;
- child continuations as part of node completion;
- stable conditional choices between resource planning and execution.

Adapt for Vanguard correctness:

- split resource `Declare()` from one work-producing `Execute()`;
- make declaration and execution contexts different types with different
  capabilities;
- resolve resources through the allocator's compiled packet cursor;
- represent conditional resource choices with typed captured decisions;
- retain frame, definition, and occurrence lifetime explicitly;
- move every mutable profiler/cleanup field out of cached implementations;
- return typed failures under `noexcept` instead of depending on assertions.

Reject as RED implementation hazards, not RED features:

- invoking the same virtual execution body in PreConsume and Consume;
- `IsConsumePhase()` branches in node implementations;
- allocation/use discovery interleaved with GPU and CPU side effects;
- direct raw allocator access from the execution context;
- cached-node mutable profiler state;
- global static frame handoff through `CRenderNodeJob::ms_jobInitFrame`;
- nullable child-job builders whose validity is enforced only by assertions;
- an epilogue-deferral flag that only indirectly describes child-job use;
- retrying or replaying execution after a partial side effect.

### R3 exit state

R3 is complete. Vanguard follows RED's node-class model and preserves its full
node vocabulary, context richness, engine wrapper, command-list modes, and child
jobs. The only structural departure is the already-approved allocator seam:
each occurrence declares resources once, then executes once. CPU preparation is
not demoted or hidden; it remains explicit graph-node execution.

R4 is complete. The graph retains RED's separately typed CPU-completion and GPU-order domains, ordinary dependency groups remain CPU boundary sets, command-list groups remain distinct recorder-owning boundaries, and child continuations are proven to participate in occurrence completion. Command-list recording and submission details remain R5.

## 46. RED-Faithful Revision R4: CPU Dependencies, GPU Order, And Group Boundaries

### R4 boundary

R4 defines what the two dependency domains mean and where ordinary dependency groups and command-list groups begin and end. It does not yet select the recorder implementation, batching policy, queue submission algorithm, or fence-signaling mechanism; those belong to R5. R4 nevertheless fixes the contracts R5 must satisfy, so an implementation cannot quietly treat a CPU counter, a recorded command list, a submitted command list, and completed GPU work as the same event.

### RED stores two independent dependency domains

RED declares `RNDT_Cpu` and `RNDT_Gpu` as separate dependency kinds and stores separate parent and child adjacency heads for each kind (`renderNodeGraph.h:45-50`, `renderNodeGraph.h:243-260`, `renderNodeGraph.h:287-332`). `CRenderNodeGraph::AddDependency()` updates only the selected domain (`renderNodeGraph.cpp:116-145`). Node removal, graph copying, unique-node merging, and sequence composition preserve the dependency type instead of collapsing both graphs into one (`renderNodeGraph.cpp:234-315`, `renderNodeGraph.cpp:350-399`, `renderNodeGraph.cpp:490-527`).

Vanguard copies that representation. A sealed definition owns two typed edge sets over the same scheduled occurrence identities:

```cpp
enum class RenderNodeDependencyKind : u8 { CpuCompletion, GpuOrder };
struct RenderNodeDependency final { RenderNodeOccurrenceId parent{}; RenderNodeOccurrenceId child{}; RenderNodeDependencyKind kind = RenderNodeDependencyKind::CpuCompletion; };
```

The exact public spelling remains an R9 API decision. The semantic split is final: a CPU-completion edge controls when node work may begin; a GPU-order edge constrains the logical order in which GPU work and resource uses are lowered. Neither edge silently changes kind.

### Four events that must never be conflated

For a GPU-recording occurrence, the engine observes four different milestones:

| Milestone | Exact meaning | What it proves |
| --- | --- | --- |
| CPU occurrence completion | `Execute()` returned, all child continuations completed, and the engine wrapper completed its occurrence epilogue | CPU data and recording work owned by that occurrence are finished |
| command-scope recording completion | every occurrence assigned to the scope finished recording and the recorder was closed successfully | a command list is ready for submission, not that it was submitted |
| queue submission completion | the queue accepted the command list and a truthful submission receipt/fence was produced | GPU work is enqueued on that queue |
| GPU completion | the submitted fence reached the recorded value, or device loss provided a terminal failure | the device no longer uses the submitted work's resources |

A normal CPU dependency waits only for the first milestone. It does not prove submission or GPU completion. A GPU-order dependency by itself waits for none of them; it is a compile-time scheduling constraint. Nodes needing submission or GPU completion depend on typed submission/synchronization results produced by R5, not on an ordinary CPU edge disguised as a fence.

### Exact CPU-dependency semantics

RED builds one Jobs counter per graph occurrence and one incoming-dependency counter. Every `RNDT_Cpu` parent counter is added to the child's incoming counter, while root jobs additionally wait for the external render kickoff (`renderRenderFrame.cpp:4282-4351`, `renderRenderFrame.cpp:4402-4474`). GPU edges are not added to these counters.

RED's continuation builder is the essential detail: a builder created from a running job carries the parent's continuation counter, its final synchronization attaches the produced continuation to that parent, and its destructor performs that link if necessary (`redJobs2/include/jobBuilder.h:42-55`, `redJobs2/src/jobBuilder.cpp:35-39`, `redJobs2/src/jobBuilder.cpp:56-85`, `redJobs2/src/jobBuilder.cpp:132-139`). Therefore RED node completion already includes child jobs dispatched through the node's continuation builder; it is not merely the return of `Process()`.

Vanguard preserves this rule using the existing Jobs continuation builder. For an edge `A --CpuCompletion--> B`, B becomes runnable only after all of the following succeed or terminate truthfully:

1. A's engine wrapper enters and invokes A's single `Execute()`;
2. every child job dispatched through A's declared continuation finishes;
3. recorder/resource cleanup owned by A's wrapper completes;
4. A's allocator packet is finalized or failed/canceled;
5. A's occurrence completion counter is released.

If A fails, its counter must still reach a terminal state so the graph cannot deadlock, but B does not execute normally. When B wakes it observes the frame failure/cancellation state and completes through the cancellation path. Counter readiness is therefore scheduling evidence, not success evidence.

The executor synthesizes one terminal CPU join over every surviving CPU sink rather than depending on RED's runtime assertion that exactly one sink exists (`renderRenderFrame.cpp:4337-4351`). A definition may still deliberately author one logical terminal node, but valid parallel terminal branches cannot escape lifetime accounting.

### Exact GPU-order semantics

RED calculates flow-group levels independently for CPU and GPU dependency types, compacts them, and flattens the GPU domain into the order used by serial execution and PreConsume (`renderNodeGraph.cpp:578-710`, `renderNodeGraph.cpp:731-810`). Parallel PreConsume remains indexed by GPU flow group even when work inside a level is distributed for load balancing (`renderNodeGraph.cpp:813-878`).

RED's common factory then calls `LinkGPU()`, whose own comment describes it as temporary; it inserts a GPU edge between each consecutive top-level node in factory registration order (`renderNodeGraphFactory.cpp:479-489`). This produces a convenient total order, but it is not a queue wait and does not prove GPU synchronization. RED also disables its CPU/GPU ordering consistency check because its GPU relation does not describe perfect ordering and historically covers only command-list-related work (`renderNodeGraph.cpp:715-728`). That disabled check is evidence that the two domains cannot be merged by assumption.

Vanguard retains RED's explicit GPU graph and declaration-order convenience without inheriting the ambiguity:

- explicit `GpuOrder` edges form an acyclic partial order;
- an explicit `LinkGPU()`-style helper may expand the sealed top-level occurrence order into consecutive `GpuOrder` edges, preserving RED's authoring capability and making the serialization visible;
- compilation performs a deterministic stable topological sort, using the sealed authoring ordinal only to break otherwise independent ties;
- the resulting stable GPU order feeds allocator `GpuFlowGroupId`, resource-use ordering, and command-scope lowering;
- occurrences without GPU work may retain a stable schedule ordinal for identity and diagnostics, but that ordinal does not manufacture GPU synchronization;
- same-scope order becomes recorder order; same-queue cross-scope order becomes submission order only when R5 emits that relationship; cross-queue order requires an explicit compiled queue dependency and wait;
- no total graph order is accepted as a substitute for a queue wait, submission receipt, or completion fence.

Vanguard's allocator exposes the target seam through RED-shaped begin/end-queue and Sync requests, the `(node, flowGroup, commandScope)` planning-writer identity, and immutable `CompiledCommandScope` / `CompiledQueueDependency` execution output. R5 and R6 must consume that output truthfully; `GpuOrder` does not bypass it.

The CPU and GPU graphs are each checked for cycles. Vanguard does not reject every opposite cross-domain pair in isolation: `A --CpuCompletion--> B` and `B --GpuOrder--> A` can be legal when separate recorders allow A to finish recording before B while B is submitted first. A contradiction is rejected when R5 lowers both constraints onto the same serial recorder, command scope, or queue schedule and no valid schedule remains.

### RED's ordinary dependency groups

RED's `NodeGroupID` is an authoring construct with a lazily created dummy input node and dummy output node. The input is linked to every member and the output depends on every member; node-to-group, group-to-node, and group-to-group links are explicitly restricted to `RNDT_Cpu` (`renderNodeGraphFactory.h:26-34`, `renderNodeGraphFactory.h:73-84`, `renderNodeGraphFactory.cpp:390-475`). Production rendering uses these groups for culling and other CPU dependency sets (`renderRenderFrame.cpp:1883-1893`, `renderRenderFrame.cpp:3169-3233`).

The Vanguard equivalent is a sealed, non-nested set of scheduled occurrence identities. It is an authoring boundary, not a renderer feature object and not a command-list group. Its expansion is exact:

| Authored edge | Expanded CPU meaning |
| --- | --- |
| `A -> Group` | A must complete before every surviving member may start |
| `Group -> B` | B may start only after every surviving member completes |
| `GroupA -> GroupB` | every surviving A member completes before any surviving B member starts |

Groups contain occurrences, not other groups. Group-to-group edges connect boundaries rather than creating nesting. Membership is sealed before dependency expansion. This repairs RED's late-membership trap: RED creates boundary links only for members known when a dummy boundary is first requested, while later registration merely records the group tag (`renderNodeGraphFactory.cpp:313-340`, `renderNodeGraphFactory.cpp:419-475`). Vanguard either rejects late membership after sealing or rebuilds the expansion transactionally before publication; it never leaves a partially linked group.

The compiler may use structural entry/exit vertices while expanding groups, but they are not executable user nodes. They do not receive allocator packets, command scopes, profiler events, or fake GPU ordinals. After structural culling, boundaries are rebuilt over surviving members. An empty group preserves transitive boundary semantics: its incoming CPU parents connect to its outgoing CPU children. A downstream rooted use of a group retains every member needed by the expanded boundary; a disconnected group does not become a root merely because it was named.

Ordinary groups remain CPU-only in the RED-faithful baseline. RED itself asserts this restriction and leaves a GPU-group TODO. GPU batching and command-scope boundaries are represented by the separate command-list-group mechanism; inventing GPU dependency groups here would blur those contracts rather than preserve a missing feature.

### Command-list groups are a different boundary

RED's `CRenderNodeCommandListGroup` is one top-level graph node that owns one command list and an ordered array of subnodes. While the group is open, created subnodes are inserted into that wrapper and receive no independently linkable graph item ID. The group cannot be nested. Its Consume path executes ordered subnode ranges and uses child jobs only where the shared-recorder epilogue can remain correctly sequenced (`renderNodeGraphFactory.cpp:18-255`, `renderNodeGraphFactory.cpp:313-369`).

R4 preserves the following boundary contract while R5 decides the concrete recorder lowering:

- outside CPU edges address the command-list-group boundary, not private children;
- an incoming CPU edge gates entry into the scope;
- an outgoing CPU edge waits for all internal node work, child continuations, recorder cleanup, and successful recording completion;
- internal children retain their authored linear order because they share one serial command recorder;
- incoming and outgoing GPU-order edges attach to the scope's first and last GPU-visible work when lowered;
- the group owns one queue and one command scope;
- command-list groups do not nest;
- leaving the boundary proves recording completion only, never submission or GPU completion.

Ordinary dependency groups and command-list groups therefore solve different problems. The first denotes an all-members CPU synchronization set. The second denotes ordered work sharing a recorder and command scope. One must never be implemented as an alias for the other.

### RED's CPU-to-GPU bridge helpers

RED preserves CPU parallelism by not projecting all GPU edges into the Jobs graph. Where a CPU or synchronization node must surround command-recording work, `LinkCPUToNextGPU()` scans later registered nodes and adds CPU edges to every node whose command-list usage is not `None`; `LinkCPUToPreviousGPU()` adds CPU edges from every earlier command-list-using node to the selected child (`renderNodeGraphFactory.cpp:503-546`). RED uses the latter before camera-end and final-flush synchronization, including an explicit multi-camera/async-compute correctness comment (`renderRenderFrame.cpp:3235-3238`, `renderRenderFrame.cpp:3274-3276`).

Vanguard preserves these capabilities as explicit sealed-definition operations, but defines them against logical GPU-visible occurrences and command-scope boundaries rather than mutable raw registration storage:

- the forward bridge means the chosen CPU occurrence completes before every later selected GPU-recording scope may begin recording;
- the backward bridge means the chosen CPU/synchronization occurrence waits for recording completion of every earlier selected GPU-recording scope;
- expansion happens once at seal time and the resulting CPU edges appear in diagnostics;
- neither bridge creates a queue wait or claims GPU completion;
- a synchronization/submission node converts recording completion into typed submission receipts according to R5;
- Present consumes the successful terminal graphics submission relationship, not merely the CPU completion of a preceding render node.

The familiar `LinkCPU()` and `LinkGPU()` declaration-order conveniences may remain, but they are explicit authoring calls whose expanded typed edges are inspectable. Incidental container order alone is never a dependency.

### Composition and camera fragments

Both dependency domains compose with occurrence remapping. Fragment-local ordinary group IDs do not leak into the final definition: groups expand after membership is sealed, and composition remaps the resulting occurrence relationships or structural boundaries. Unique-node merging unions both typed edge sets around the retained occurrence and validates each domain again. Sequence composition connects the declared fragment entry/exit boundaries without silently converting CPU edges into GPU edges.

For multiple cameras, the same rules apply independently to each copied occurrence even when implementations are shared. Camera-local groups contain camera-local occurrence IDs. Cross-camera ordering is authored at fragment boundaries or by explicit synchronization nodes; equal logical resource names, implementation identity, or matching group names do not create a dependency.

### R4 validation and diagnostics

Sealing fails transactionally when any of these conditions is found:

- an edge endpoint is invalid, stale, or belongs to another definition generation;
- a self-edge or duplicate edge violates the selected authoring policy;
- either typed dependency domain contains a cycle;
- ordinary group membership changes after sealing or references an invalid occurrence;
- an ordinary group is used as a GPU-order group;
- a command-list group is nested or external code addresses a private child;
- lowering requires mutually impossible order on one recorder, command scope, or queue;
- a cross-queue dependency reaches publication without an executable queue wait;
- a node claiming submission or GPU completion is backed only by CPU-counter readiness.

Diagnostics report the dependency kind, parent and child occurrence IDs, definition generation, group/scope boundary where expansion occurred, and the lowering rule that failed. Validation is runtime-correct in Shipping and does not depend on RED-style assertions.

### R4 copy, adapt, and reject decisions

Copy from RED:

- separate CPU and GPU dependency domains over the same occurrences;
- CPU Jobs counters formed only from CPU edges;
- child continuations participating in parent occurrence completion;
- ordinary CPU dependency groups with input and output boundaries;
- command-list groups as distinct non-nested recorder-owning boundaries;
- explicit declaration-order and CPU-to-GPU bridge helpers;
- composition that preserves both dependency kinds;
- stable GPU-flow ordering used by resource planning and recording.

Adapt for Vanguard correctness:

- name the stored GPU relation `GpuOrder` so it cannot be mistaken for completion;
- seal ordinary group membership before transactional boundary expansion;
- keep structural group vertices out of execution, allocation, and profiling;
- join every surviving CPU sink for frame lifetime accounting;
- propagate failure/cancellation separately from counter readiness;
- expand declaration-order and bridge helpers into inspectable typed edges;
- lower same-scope, same-queue, and cross-queue order through different explicit mechanisms;
- defer cross-domain contradiction checks until constraints meet on a concrete recorder or queue schedule;
- require typed receipts for submission and fence-backed GPU completion.

Reject as RED implementation hazards, not RED features:

- treating `RNDT_Gpu` as if it were a fence dependency;
- relying on raw insertion order without an explicit linking operation;
- late ordinary-group membership that misses already-created boundary links;
- dummy group nodes masquerading as executable render work;
- assertion-only group-kind, nesting, sink-count, or ordering validation;
- assuming CPU and GPU orders must always be identical;
- accepting total graph order as cross-queue synchronization;
- allowing a CPU completion counter to stand in for command submission or GPU completion.

### R4 exit state

R4 is complete. Vanguard now has a RED-faithful two-domain dependency model, exact child-aware CPU completion, deterministic GPU logical ordering, sealed CPU dependency groups, distinct command-list-group boundaries, explicit bridge semantics, and a four-milestone execution vocabulary. No production implementation changed.

R5 is complete. RED's recorder ownership, command-list groups, flow-ordered flush ranges, synchronization nodes, fork/join regions, and final-flush-to-Present chain are preserved. Vanguard replaces RED's implicit mutable flush cursor, assertion-only queue state, void submission API, and hidden copy synchronization with immutable compiled batches and truthful per-scope/dependency receipts.

## 47. RED-Faithful Revision R5: Command Groups, Recording, And Submission

### R5 boundary

R5 defines recorder ownership, command-list-group execution, synchronization-node behavior, submission batching, queue lowering, receipts, and partial failure. R6 owns the exact replay adapter between node declarations and `RenderFlowResourceAllocator`; R7 performs the wider audit for synchronization RED obtains implicitly from global renderer/GpuApi behavior. No production implementation is authorized by this section.

### RED command-list modes and recorder ownership

RED exposes four node modes: `None`, `Require`, `Own`, and `Sync` (`renderNodeGraph.h:109-115`). `CRenderNodeBase::Process()` creates a command list only for `Own`, stores it in the node context, binds a command list for `Own` or `Require`, runs the node, then unbinds and performs binding/profiler cleanup in an epilogue. If the node created child work, that epilogue is dispatched behind the child continuation (`renderGraphNodes.cpp:107-235`).

In the studied renderer, `Own` is used by `CRenderNodeCommandListGroup`. The group creates one Default or Compute command list, requests an allocator queue begin, executes its ordered children, and requests the queue end only after any child continuation (`renderNodeGraphFactory.cpp:18-180`). Its `Require` children inherit the group's recorder. The group is therefore a deliberate authoring boundary, not an optimization that a later compiler may arbitrarily reconstruct.

The context stores the created command list in `RenderFrameCommandLists` at `GpuFlowGroup + ReservedCls::CL_COUNT` (`renderNodeImplContext.cpp:176-191`). This gives each top-level GPU-flow position one command-list slot. RED prepares enough slots for the graph before execution (`renderRenderFrame.cpp:4860-4876`).

Vanguard preserves the four capabilities with stronger ownership:

| Mode | R5 lowering |
| --- | --- |
| `None` | No recorder is created or exposed. CPU, terminal, and Present work remains possible. |
| `Require` | The node must be an ordered child of an explicit recorder owner and inherits exactly that owner's scope and queue. |
| `Own` | The occurrence owns one compiled command scope and the executor creates exactly one tracked RHI command list for it. |
| `Sync` | No ordinary recorder is exposed. The node executes one precompiled submission boundary and publishes receipts. |

An ungrouped `Require` node is invalid. A normal singleton GPU operation can be represented by an explicit `Own` occurrence or a one-child command-list group. A node cannot create, bind, submit, or discard an untracked command list through the graph context.

### One group occurrence, one scope, one ordered allocator packet

R1 established that a RED command-list group is one top-level scheduled occurrence and its children are not independently linkable graph occurrences. R5 completes that contract for Vanguard:

```text
CommandListGroup occurrence
  -> one RenderFlowNodeId
  -> one GpuFlowGroupId
  -> one CommandScopeId and queue
  -> one ordered allocator planning writer / execution packet
  -> one RHI command list
  -> ordered internal child implementations
```

During declaration, the group calls each child's `Declare()` in authored child order against a child-scoped view of the same graph-owned declaration tape. After whole-group survival is known, the adapter replays that tape into one planning writer, producing one canonical packet step stream. During execution, the group opens that packet cursor once and invokes each child exactly once in the same order. Child names and ordinals remain in diagnostics even though they do not become external graph IDs.

The shared recorder and shared packet cursor are serial resources. A later child cannot begin until the preceding child's execution and declared child continuation have completed. A child job that needs to record must acquire the same scope through an engine-controlled exclusive continuation wrapper; raw RHI binding is not exposed, and no two workers may bind or mutate the same command list concurrently. Parallel command recording requires separate command scopes, not concurrent access inside one group.

Every worker entry that records uses a scoped binding guard around Vanguard's thread-local RHI command-list binding (`rhi.cpp:34`, `rhi.cpp:1490-1507`). The guard requires an empty or expected thread-local slot, binds the compiled recorder, opens the correct allocator cursor position, and unbinds on every exit path. Moving a continuation to another worker never relies on a binding left on the previous thread.

Group recording completion means all children and their continuations finished, the packet was exhausted, GPU events/bindings were closed, the recorder was unbound, and the scope entered `Recorded`. It still does not mean submitted.

### RED synchronization nodes and contiguous flushes

`CRenderNode_Synchronize` first declares an allocator queue-sync request. In Consume it calls `RenderFrameCommandLists::Submit()` through its Jobs builder (`renderGraphNodes.cpp:279-297`). `Submit()` selects the contiguous command-list-slot range from `m_nextFlushStart` through the synchronization node's GPU flow group, calls `GpuApi::CloseAndSubmitCommandLists()`, inserts a Jobs fence, then clears that range and advances the mutable flush cursor in a continuation (`renderInterface.cpp:137-186`).

This establishes an important RED execution rule: a synchronization node submits all command-list groups since the previous synchronization boundary, in GPU-flow order. Its CPU completion includes the close/submission job and range-clearing epilogue. A CPU edge from the synchronization node to another node therefore proves that the submission call ran, although RED exposes no success receipt.

Vanguard copies the boundary behavior but compiles it before execution. Each surviving `Sync` occurrence owns an immutable `CompiledSubmissionBatch` containing the exact ordered command-scope IDs since the preceding boundary, its synchronization mode, stable batch ordinal, and diagnostic name. Every scope belongs to exactly one batch. Runtime execution never scans a mutable command-list array, accepts null holes as topology, or advances a shared `nextFlushStart` cursor.

The R4 CPU bridge into a `Sync` node expands to CPU-completion edges from every scope owner in that batch. The synchronization node cannot run until every listed scope is `Recorded` or a prior failure has selected cancellation. Its successful execution consumes those recorded command lists through the RHI, writes scope/dependency receipts, clears the consumed handles, and only then completes its CPU occurrence.

A `None` synchronization boundary with no scopes is a legal no-op and emits no fabricated fence. A Fork or Join boundary may not be empty because it must lower a real cross-queue relationship. Submission capacity is checked at compile time. Oversized same-queue `None` batches may be split into deterministic consecutive native submissions; a Fork/Join batch that cannot fit the RHI limit fails compilation rather than changing the overlap region.

### RED's bounded async-compute model

RED explicitly supports a simplified non-nested Graphics/Compute overlap interval. A Fork submission makes later Compute work wait for prior Graphics work. The following submission may contain Default and Compute command lists and must Join, making later Graphics work wait for Compute. Compute lists are legal only inside that interval (`gpuApiInterface.h:1246-1262`).

The DX12 backend partitions a submitted range into Graphics and Compute lists, connects resource states, closes lists in Jobs, submits the queues, and inserts a queue wait according to Fork or Join (`gpuApiDX12CommandList.cpp:1268-1520`). The implementation asserts that overlap is not nested, Compute does not appear outside a region, and no intermediate submit occurs inside the region (`1328-1331`, `1503-1517`). RED also flushes pending copy-queue work before render submission (`1288-1291`), which is implicit synchronization and is not part of its Render Graph edges.

Vanguard's current RHI exposes the same `None`, `ForkAsyncCompute`, and `JoinAsyncCompute` modes, and its allocator already validates balanced, non-nested Graphics-to-Compute and Compute-to-Graphics dependencies (`rhi_types.hpp:309-323`; `render_flow_resource_resolve.cpp:684-736`). R5 therefore adopts this executable baseline:

| Batch mode | Valid compiled meaning |
| --- | --- |
| `None` | All scopes in the native submission use one queue. Same-queue order is the listed scope order. |
| `ForkAsyncCompute` | A Graphics producer batch is submitted, then the Compute queue waits for it before the open overlap region. |
| `JoinAsyncCompute` | The open region's Compute scopes and its Graphics consumer scopes are submitted with a Compute-to-Graphics wait, closing the region. |

Regions are balanced, non-nested, and contain no intervening submission boundary. Sequential regions are allowed. The compiled definition, not a process-global mutable flag, proves whether a Fork or Join is legal. The RHI backend's submission lock remains the physical serialization point; the Render Graph does not add a second queue-submission system.

The graph IR may still name Copy scopes, but the current Fork/Join contract cannot express Copy-to-Graphics, Graphics-to-Copy, Copy-to-Compute, or Compute-to-Copy waits. An isolated Copy schedule or same-Copy-queue chain is executable. A cross-queue Copy edge fails closed before allocator Resolve. RED's hidden copy flush is specifically rejected; general Copy crossings wait for a future explicit producer-fence/consumer-wait RHI contract.

### Recording and submission may overlap only where ownership permits

RED records top-level node work through its CPU dependency graph and allows independent command-list groups to execute on worker jobs. Vanguard preserves that concurrency. Independent scopes may record in parallel when their CPU dependencies permit, because each owns a different RHI command list and allocator packet. Children within one scope remain serial.

Submission begins only at explicit compiled `Sync` nodes. A later scope may record while an earlier batch is already submitted if no CPU/resource/recorder constraint forbids it. Same-queue GPU order is preserved by deterministic batch and scope order. Cross-queue execution is permitted only inside the compiled Fork/Join region. Backend submission remains serialized with Present and other queue operations through the existing RHI submission lock.

This supersedes the withdrawn former-V1 proposal to make the entire initial executor serial. The safe unit of parallelism is the independently owned command scope, exactly as RED's command-list groups imply.

### Truthful scope and dependency receipts

RED's `CloseAndSubmitCommandLists()` is a `void` operation whose work may be dispatched through Jobs (`gpuApiInterface.h:1380-1386`). That API cannot tell the graph whether native execution occurred or which fence protects each queue. Vanguard must use the full `rhi::SubmissionReceipt`.

The current RHI marks `workSubmitted` immediately after native queue execution and before signaling completion fences. If signaling later fails, it returns failure while preserving submitted-without-completion evidence (`common_backend.cpp:2794-2847`; `rhi.cpp:1514-1546`). A successful receipt contains a `ResidencyFenceSet` with every signaled queue fence plus an aggregate completion fence (`rhi_types.hpp:1675-1705`).

For a successful batch, each `CommandScopeExecutionReceipt` receives the fence from `SubmissionReceipt::residency` matching that scope's queue. All scopes on one queue in the same native batch may share that queue fence. The aggregate `completion` field is not blindly copied to every scope: a Fork may return Compute completion while its producer scope is Graphics. Each compiled Fork/Join edge also receives one `QueueDependencyExecutionReceipt::Submitted` only after the RHI call that lowered its wait succeeds.

The synchronization node stores the complete native receipt as diagnostic evidence even after deriving allocator receipts. Command-list handles are cleared after successful submission or any result with `WasSubmitted() == true`, because native execution consumed them. When submission fails with `WasSubmitted() == false`, every unbound list in that attempted batch is explicitly discarded.

### Failure matrix and terminal classification

Every scope reaches exactly one terminal classification, even when graph execution aborts midway:

| Failure point | Current/remaining scope classification | Frame outcome |
| --- | --- | --- |
| command-list creation fails | current and never-created scopes `DiscardedBeforeSubmission` | `Aborted`, unless the RHI reports device loss |
| node declaration/recording/packet action fails before submission | current list unbound and discarded; current and later scopes `DiscardedBeforeSubmission` | `Aborted` or `DeviceLost` from the failure |
| close/submission fails and `WasSubmitted()` is false | attempted lists discarded; attempted and later scopes `DiscardedBeforeSubmission`; earlier scopes retain real submitted fences | `Aborted` unless failure is device loss |
| submission succeeds | scopes `Submitted` with their real queue fences; lowered dependencies `Submitted` | execution may continue |
| native execution occurred but completion signaling failed | every scope and lowered dependency in that atomic native batch `UnknownDueToDeviceLoss`; earlier batches retain valid submitted receipts; later scopes are discarded | `DeviceLost` |
| native Present fails after successful render submission | no scope receipt is rewritten; output is consumed by recovery | presentation failure or `DeviceLost` according to RHI evidence |

The atomic-batch rule for post-execution signal failure is deliberate. The RHI may retain some partial queue fences before a later signal fails, but the current allocator dependency schema cannot always express a mixed Join batch where one queue has a fence and the other has only ambiguous completion. Vanguard retains those partial fences in diagnostic evidence, classifies the complete failing native batch conservatively as `UnknownDueToDeviceLoss`, and lets device-loss recovery own its resources. It never invents the missing queue fence.

`Aborted` does not mean no GPU work was submitted. Earlier successful batches remain `Submitted` and their fences protect retirement; only unsubmitted scopes are discarded. `DeviceLost` is selected whenever native execution may have happened without complete terminal fence evidence. The terminal task waits until all node/scope/submission continuations have ended, constructs the complete scope and dependency receipt arrays, and calls allocator `Finish()` exactly once.

After the first failure latch is set, no new ordinary node or submission starts. Already-running recorders finish only enough engine-owned cleanup to unbind, cancel their packets, and discard unsubmitted lists. Every downstream CPU occurrence wakes into cancellation so the terminal join cannot deadlock.

### Final submission and same-frame Present

RED places `CRenderNode_Present` after the unique final `CRenderNode_Synchronize` with a CPU edge (`renderRenderFrame.cpp:3137-3138`, `renderRenderFrame.cpp:3274-3276`). The final synchronization's submission jobs belong to its continuation, so Present runs after the native submission call, not after GPU fence completion. RED's Present node then calls the viewport directly (`renderGraphNodes.cpp:359-371`).

Vanguard preserves that same-frame chain:

```text
final Graphics scope records the exact acquisition-aware Present transition
  -> final Sync submits that scope and publishes a valid Graphics scope receipt
  -> allocator Finish validates all packets, scopes, and queue dependencies
  -> Present node consumes the retained acquisition on the worker-side presentation gate
```

Present does not wait for the render fence to complete; queue ordering makes the submitted transition precede native Present. It may run only after a successful final Graphics submission with a valid matching fence and after allocator `Finish()` succeeds. A pre-submission failure can publish `AbandonSafe` only when no submitted command list touched the acquisition. If submitted work touched the acquisition or completion is ambiguous, the output follows recovery/DeviceLost rather than abandonment.

The D3D12 backend's submission callback is the exact-token proof that the Present transition's command list was submitted (`d3d12_backend.cpp:2668-2699`). A current prerequisite defect remains visible: `MarkPresentTransitionSubmitted()` tests only `completion.queue == Graphics`, but a default invalid fence also reports Graphics (`d3d12_backend.cpp:388-392`; `rhi_types.hpp:1661-1667`). It must additionally require `completion.IsValid()` before Render Graph presentation integration. The graph's receipt checks prevent Present on the failure path, but the backend acknowledgement must still be truthful in isolation.

Present, submission, and main-thread swap-chain reconciliation all use the previously selected per-viewport/RHI serialization rules from Section 44. Present remains a current-frame terminal graph node; R5 does not restore a next-frame mailbox.

### R5 validation and diagnostics

Compilation or execution fails transactionally when:

- `Require` has no owning scope, or a node's compiled queue disagrees with its recorder;
- a scope has zero or multiple owners, duplicate membership, or incompatible ordered children;
- one child continuation could overlap another user of the same recorder or packet cursor;
- a scope appears in zero or multiple submission batches;
- a Sync batch references a missing, later, unrecorded, already-submitted, or wrong-queue scope;
- a `None` native batch spans multiple queues;
- a Fork/Join region is empty, nested, overlapping, unbalanced, or contains an intermediate submission;
- a cross-queue Copy edge or another unsupported queue relation reaches lowering;
- a native batch exceeds the RHI limit and cannot be split without changing synchronization semantics;
- submission succeeds without a valid per-queue fence for every submitted scope;
- submitted work is classified as discarded, or a fence is fabricated after signal failure;
- Present lacks the final Graphics receipt, exact transition acknowledgement, successful allocator terminalization, or retained acquisition ownership.

Diagnostics identify definition/execution generation, node/group child, command scope, batch, stable order, queue, sync mode, RHI receipt, allocator receipt, and the first failure phase. Dumps show scope membership and submission-batch membership separately; sharing a batch never implies sharing a recorder.

### R5 copy, adapt, and reject decisions

Copy from RED:

- the `None`, `Require`, `Own`, and `Sync` capability split;
- command-list groups as explicit one-recorder owners with ordered internal nodes;
- flow-ordered command scopes collected between explicit synchronization nodes;
- synchronization-node CPU completion including close/submission continuation work;
- balanced, non-nested Graphics/Compute Fork and Join regions;
- independent worker recording for independently owned scopes;
- final submission followed by same-frame Present without a CPU GPU-completion wait.

Adapt for Vanguard correctness:

- compile immutable scope and submission-batch membership instead of indexing a mutable frame list;
- declare and execute grouped children through one ordered allocator packet;
- bind recorders with thread-local scoped guards on every worker entry;
- serialize all access inside one scope while allowing different scopes to record concurrently;
- validate Fork/Join state in the compiled definition rather than a global assertion-only flag;
- use full RHI receipts and derive per-queue scope receipts from residency fences;
- classify pre-submit discard, prior successful submissions, and post-submit device-loss ambiguity separately;
- retain partial native receipt evidence while conservatively terminalizing an ambiguous atomic batch;
- gate Present on the final submitted Graphics scope and successful allocator Finish.

Reject as RED implementation hazards, not RED features:

- a `void` submission API with no failure or fence evidence;
- null command-list slots as a runtime representation of graph topology;
- shared mutable `m_nextFlushStart` correctness state;
- implicit copy-queue flushing outside graph dependencies;
- process-global async-region state validated only by assertions;
- concurrent mutation of one command list by command-group children;
- aggregate completion fence copied to scopes on other queues;
- treating a failed submission call as proof that nothing was submitted;
- Present after an invalid/default fence acknowledgement;
- a second graph-private submission queue beside the RHI backend.

### R5 exit state

R5 is complete. Vanguard now preserves RED's command-list-group and synchronization-node architecture while giving every recorder, scope, batch, queue edge, failure, and terminal output an explicit owner and truthful state transition. The design supports parallel recording across scopes, serial recording inside a group, bounded Graphics/Compute overlap, partial prior submissions, post-execution signal failure, and same-frame Present without inventing synchronization.

That allocator-seam trace is completed by R6 in Section 48.

## 48. RED-Faithful Revision R6: Resource Declaration And Allocator Replay Seam

### R6 boundary

R6 defines the exact adapter between RED-style node occurrences and Vanguard's completed `RenderFlowResourceAllocator`. It owns declaration tapes, graph-to-allocator identity translation, post-cull replay, packet binding, and terminal receipt assembly. It does not redesign allocation, recorder submission, graph caching, or presentation ownership. Exact public API spelling remains an R9 task; the cardinalities and phase ordering below are final architecture.

### Source snapshot

The RED files used here match the previously pinned R1-R5 snapshot:

```text
D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.h
  lines: 1135
  sha256: 40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderNodeImplContext.cpp
  lines: 413
  sha256: B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderFlowResourceAllocator.h
  lines: 362
  sha256: DA68B9BD5667450B0F3952D8BC2CCC978A6C82DA1F1A905FBE385C324B61AEE6

D:/root/R6.Root/Mainline/dev/src/common/renderer/src/renderFlowInternalData.cpp
  lines: 2027
  sha256: CDF7E3C296EF9D6FD465504E8C509EC25D6BCD9F4204F29FF39AE9B2E4A77AD4
```

The Vanguard seam was re-read from this working-tree snapshot:

```text
source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp
  lines: 642
  sha256: 4E3E90B39935DFF477973F493DD821B9CF1537561CF3E951D634CBBD7C333CE2

source/rendering/include/vanguard/rendering/render_flow_resource_execution.hpp
  lines: 282
  sha256: DA34ABA872631B5B3FB484C36F20F68C539BAD53FDE483D4C862A532AE728122

source/rendering/src/render_flow_resource_allocator.cpp
  lines: 1129
  sha256: A149D87F7D364A504D89DBEA278267B1036016311C64CB55E134AD619D617677

source/rendering/src/render_flow_resource_resolve.cpp
  lines: 1924
  sha256: 6528C39A12E7DCCE5504F429153F5AF3CDDCB99053286790CE1CB467D1574AC3

source/rendering/src/render_flow_resource_execution.cpp
  lines: 1053
  sha256: 1E1D1C8ED7BB19CAA80D41ABD23540D33E0977230C031A8C7737D7220A3D579B
```

### RED behavior retained at the seam

RED's node context forwards `RTAlloc`, `RTInject`, `RTUseBegin`, `RTUseEnd`, `RTSwap`, and `RTDecision` directly to its flow allocator using the occurrence's GPU flow group (`renderNodeImplContext.cpp:223-299`). PreConsume appends requests into one per-flow-group tape, while Consume checks each request against the same ordinal and replays the captured decision (`renderFlowInternalData.cpp:895-1003`). The frame runs all PreConsume work, resolves the allocator, and only then releases the node execution jobs (`renderRenderFrame.cpp:4940-4963`). A command-list group calls all child nodes with the same flow group and brackets them with one queue begin/end (`renderNodeGraphFactory.cpp:18-180`).

Vanguard retains the semantic sequence and group cardinality: one deterministic operation tape per resource-recording top-level occurrence, one resolve before recording, the same operation order during execution, and one shared tape for ordered command-group children. It does not retain RED's second call to the same `Execute()` body, assertion-only tape comparison, raw resource lookup by tag during execution, or implicit queue requests from node code.

### One declaration, then deterministic adapter replay

`RenderNodeImpl::Declare()` writes one graph-owned, frame-local declaration tape. It does not receive a `ResourcePlanningWriter` and does not mutate the cached node implementation. Each declaration operation records its complete typed descriptor, graph resource/view identity, authored binding slot, child diagnostic ordinal, and source operation ordinal. Resource-affecting decisions record both the captured value and a node-local decision slot.

After every structurally enabled candidate occurrence declares successfully, the graph derives resource hazards and roots, validates both dependency domains, culls execution occurrences, rebuilds live hazard ordering, selects stable GPU order and command scopes, and validates executable queue lowering. Only then does the allocator adapter replay the surviving declaration tapes. Replay is data translation, not a second node callback and not a second resource-state planner.

```text
instantiate structurally enabled graph occurrences
  -> Declare() each candidate occurrence exactly once
  -> seal canonical declaration tapes
  -> derive resource hazards and roots
  -> cull and rebuild live ordering
  -> compile packet-owner flow groups, command scopes, and queue dependencies
  -> BeginFrame on RenderFlowResourceAllocator
  -> register live retained imports and reserve live export slots
  -> declare each resource-recording occurrence into one ResourcePlanningWriter
  -> close and join every writer
  -> SealPlanning
  -> Resolve directly from the registered allocator request stream
  -> BeginExecution
  -> execute graph occurrences once
  -> assemble complete receipts and Finish exactly once
```

This ordering resolves an earlier ambiguity. Structural feature conditions are part of definition construction and may omit an occurrence before declaration. Resource-root culling cannot happen before declaration because the graph needs the declared accesses to derive its hazards and liveness. Section 45's earlier shorthand saying all culling preceded `Declare()` is superseded by this two-step distinction.

The canonical graph declaration tape remains the authoring authority used for hazards, culling, binding slots, diagnostics, and replay. The allocator's private candidate batch is a derived backend representation. The adapter verifies replay cardinality and kind record-by-record; it never asks node code to repeat its declaration and never infers an undeclared access.

### Exact occurrence, scope, and packet cardinality

Not every live graph occurrence owns an allocator packet. The allocator requires every planning writer to have a valid `(RenderFlowNodeId, GpuFlowGroupId, CommandScopeId)`, and `PacketFor()` can return only a compiled packet created from such a writer (`render_flow_resource_allocator.hpp:521-600`; `render_flow_resource_execution.cpp:838-851`). CPU-only nodes, dependency-set endpoints, sequence helpers, Sync nodes, and the terminal Present node must not receive fake command scopes merely to satisfy that API.

The exact mapping is:

| Graph object | Allocator mapping |
| --- | --- |
| Resource-recording `Own` occurrence | One node ID, one GPU flow group, one command scope, one planning writer, and one execution packet. |
| Graphics/Compute command-list group | The top-level group is the packet owner; all ordered children share its IDs, writer tape, cursor, and recorder. |
| `Require` child inside a group | No independent graph occurrence, writer, packet, or scope; it owns a child-local diagnostic span and binding slots inside the group tape. |
| CPU-only `None` occurrence | No allocator writer or packet. CPU dependencies and retained frame data describe its work. |
| `Sync` occurrence | No allocator packet. It consumes a compiled submission batch and produces scope/dependency receipts. |
| Present terminal occurrence | No allocator packet. The final Graphics packet records the exact Present transition; Present consumes the retained ticket after final submission and allocator `Finish()`. |

Production creates planning writers only for real resource-recording packet owners. Command-list groups register their begin/end queue around child declaration, and packetless Sync occurrences register their explicit submission request at their assigned GPU flow group. Resolve consumes those records directly and publishes immutable command scopes and executable cross-queue dependencies; the caller supplies neither survivors nor a schedule.

The current allocator rejects duplicate node or GPU-flow-group writer ownership and sorts batches by `GpuFlowGroupId`, independent of writer completion order (`render_flow_resource_allocator.cpp:788-846`; `render_flow_resource_resolve.cpp:632-667`). R6 assigns each packet owner a unique flow group in stable compiled GPU order. Values may have gaps for non-packet graph occurrences, but relative order must agree with command-scope `stableOrder`, as Resolve already validates (`render_flow_resource_resolve.cpp:740-752`).

### Grouped-child declaration and execution

A command-list group owns one canonical tape divided into ordered child spans:

```text
group packet owner
  child 0 span: declarations, uses, decisions, diagnostic bindings
  child 1 span: declarations, uses, decisions, diagnostic bindings
  child 2 span: declarations, uses, decisions, diagnostic bindings
```

The group calls each child's `Declare()` once in authored order against a child-scoped tape view. Culling retains or removes the whole group because RED children are not independently linkable occurrences. Feature-dependent child membership is therefore fixed when the structural definition is built; R6 does not silently prune individual children from a surviving group.

Replay opens one planning writer for the group and translates every child span in the same order. Execution opens `PacketFor(groupNode)` and one `ExecutionPacketCursor`, then invokes every child once in that order. Node-local binding slots map to the replay-created `ResourceUseId`, `DecisionId`, logical-view, and diagnostic records stored in the retained frame occurrence. A cached child implementation never stores frame-generation allocator IDs. The cursor is finalized only after the last child and every serialized child continuation complete.

This matches the allocator's local identity model: logical resources, views, uses, and decisions are generation-bound to the packet owner's flow group, and packet steps enforce exact Begin/End order (`render_flow_resource_allocator.hpp:50-107`; `render_flow_resource_execution.cpp:631-784`). It also preserves R5's rule that a shared recorder and cursor are exclusive serial resources; migrating a deferred child span to another worker requires an engine-controlled recorder rebind and cannot overlap another child.

### Graph identity to allocator identity

Graph display names never become allocator correctness identity. Every graph resource lineage receives one adapter-owned `LogicalResourceKey` composed from its explicit `FlowSpaceId` and a collision-free copied internal key derived from the graph definition/resource identity. Camera-local composition receives distinct flow spaces; intentional cross-node or cross-camera sharing reuses the same explicit graph resource identity. Equal display strings, node implementation types, or camera-local ordinals do not alias resources.

Within each writer replay, the adapter maintains generation-local maps:

```text
graph resource lineage -> LogicalResourceId for that writer
graph texture view      -> LogicalTextureViewId for that writer
graph buffer view       -> LogicalBufferViewId for that writer
graph use binding slot  -> ResourceUseId
graph decision slot     -> DecisionId
graph scope identity    -> ResourceScopeId
graph export request    -> ExportSlotId
```

The declaring writer uses `DeclareTexture`, `DeclareBuffer`, `DeclareLike`, or import installation. A later writer that accesses the same lineage uses `ReferenceResource` with the identical internal key and receives its own flow-local handle. Occurrence-local temporaries use `DeclareTemporary*` and cannot be referenced across packet owners. Cross-node resource scopes use graph-generated stable `ResourceScopeId` values; culling must retain both endpoints or fail before replay.

Writes and mapping swaps preserve their canonical graph order. Graph content versions determine dependency and liveness edges; replay uses the allocator's time-varying logical mapping operations to realize those versions physically. The adapter cannot collapse two graph lineages merely because their descriptors match—the allocator alone decides physical reuse or placed aliasing after lifetime compilation.

### Imports, outputs, and captured decisions

The retained frame binds graph import slots before declaration. After culling, the coordinator registers only imports referenced by live packet-owner tapes, before creating any planning writer, as required by `RegisterImport()` (`render_flow_resource_allocator.hpp:585-596`; `render_flow_resource_allocator.cpp:651-768`). One external token maps to one graph import lineage. If several nodes consume it, they reference that lineage; they do not install the same `ImportedResourceId` into several logical destinations, which Resolve correctly rejects.

The currently executable allocator contract remains same-queue continuation. An incoming fence or initial/final queue mismatch is rejected before publication; R6 does not reinterpret CPU graph order as an external GPU wait. Generic cross-queue imports remain blocked until R7/RHI work provides an executable wait primitive.

Every resource-affecting conditional is captured once during declaration. Replay calls `CaptureDecision(value)` at the canonical tape ordinal and stores the returned `DecisionId` in the occurrence binding table. Execute queries the compiled packet through `CapturedDecision()` and never recomputes the resource-affecting predicate from mutable state. A culled occurrence creates no allocator decision, and a group child resolves only its own decision slots.

Presentation exposes one concrete missing seam rather than a reason to redesign replay. The current allocator retains only generic texture/buffer imports, and its compiled action enum contains ordinary transitions, UAV barriers, clears, and alias activation—no exact-acquisition `SwapChainPresentTransition` (`render_flow_resource_internal.hpp:173-204`; `render_flow_resource_execution.cpp:64-105`). Therefore the architecture selected in Sections 37 and 44 requires one narrow production extension before viewport graphs can execute:

- register a retained presentation-texture import carrying the exact `rhi::AcquiredBackBuffer`, not just its texture;
- constrain it to Graphics, Present-to-Present continuation, and one logical installation;
- compile exactly one acquisition-aware terminal action into the final Graphics packet;
- execute that action with `rhi::TransitionSwapChainPresent(acquisition)` instead of a generic texture transition;
- retain the acquisition through submission and same-frame terminal Present.

No node may add a second manual Present transition. The final Present node owns ticket consumption, not transition recording. The pre-existing D3D12 acknowledgement-validity correction from Section 37 remains a prerequisite.

### Packet execution and terminal receipts

After `Resolve()` publishes the generation, the coordinator calls `BeginExecution()` exactly once. Only packet-owner wrappers call `PacketFor(node)` and `OpenCursor(compiledScope, compiledQueue)`. Each declared use binding drives the matching `BeginTextureUse` or `BeginBufferUse`, node code consumes only the returned scoped native reference, and the matching `EndUse` occurs at the canonical tape position. The wrapper finalizes the cursor only when all steps are exhausted and no use remains active. Dropping an open cursor or canceling it invalidates all outstanding resolved-use witnesses and prevents successful frame completion.

R5 submission batches produce one `CommandScopeExecutionReceipt` for every allocator-published scope and one `QueueDependencyExecutionReceipt` for every allocator-published queue dependency. R6 assembles them by compiled identity, never by completion order and never from an aggregate fence belonging to another queue. Earlier submitted batches keep real Submitted receipts after a later failure; unsubmitted scopes are DiscardedBeforeSubmission; a native execution with incomplete fence evidence is UnknownDueToDeviceLoss.

The terminal continuation waits for every occurrence, child continuation, recorder cleanup, and submission continuation. It then constructs one generation-matching `TerminalExecutionReceipt` and calls `RenderFlowResourceAllocator::Finish()` exactly once. Completed requires every packet finalized and every native scope/dependency submitted. Abort requires no cursor still Executing and still reports all partial submission truth. DeviceLost owns ambiguous native completion. These are enforced directly by the allocator's `Finish()`; the graph execution path supplies the complete evidence rather than weakening those checks.

The same-frame Present node becomes runnable only after the final Sync submission continuation and successful allocator `Finish()`. If allocator `Finish()` fails after submission, allocator destruction remains the fail-closed recovery boundary, the output is consumed as DeviceLost/recovery-required, and native Present is not attempted.

### Transactional failure rules

- Declaration, graph compilation, import validation, writer replay, seal, or Resolve failure publishes no executable generation. Any begun allocator frame leaves through `CancelBeforePublication()`; the retained output follows the current-frame untouched-abandon or device-loss rule appropriate to the evidence.
- One rejected replay operation poisons its writer. The coordinator stops normal replay, joins/abandons all writers, and cancels the allocator frame; it never retries `Declare()` or drops only the failed operation.
- A mismatch between canonical declaration kind/order and replay-created binding kind/count is a graph-adapter contract failure before execution.
- Packet lookup, scope, queue, captured-decision, or Begin/End mismatch fails the owning occurrence, cancels its cursor, and enters the common R5 cancellation path.
- No failure manufactures a resource handle, queue fence, packet completion, import wait, or Present acknowledgement.

### R6 copy, adapt, and reject decisions

Copy from RED:

- one flow-ordered resource request tape per recording occurrence;
- one shared tape for all ordered children of a command-list group;
- resolve before recording starts;
- exact decision and request-order replay during execution;
- group-level queue ownership rather than child-owned recorders.

Adapt for Vanguard:

- split RED's PreConsume/Consume double execution into one `Declare()`, deterministic data replay, and one `Execute()`;
- use graph-owned typed identities and binding slots instead of raw tag lookup;
- prepare every resource-recording occurrence in the finalized composed graph without a survivor overlay;
- give packets only to real resource-recording command-scope owners;
- register retained imports centrally before writers and bind them through graph import lineages;
- assemble complete typed scope/dependency receipts for allocator `Finish()`.

Reject as incorrect or unnecessary:

- calling node execution code to discover resources;
- giving CPU, Sync, or Present nodes fake command scopes or empty packets;
- one planning writer per command-group child;
- storing frame-local allocator IDs in cached implementations;
- display-name equality as resource identity;
- independently authored graph and allocator use descriptions;
- recomputing captured resource decisions during Execute;
- generic transition of an acquired back buffer to Present;
- treating a Jobs counter or total GPU flow order as a queue fence.

### R6 exit state

R6 is complete at the architecture level. The active seam is the finalized composed graph declaring directly into the allocator: one packet/cursor per real command-list owner, ordered child declarations inside group packets, centrally registered imports, RED-shaped queue and Sync requests, frame-local binding maps, and complete terminal receipts. CPU-only, Sync, and Present nodes remain first-class graph occurrences without fabricated allocator packets. Earlier tape-culling language is superseded.

That implicit-synchronization audit is completed by R7 in Section 49.

## 49. RED-Faithful Revision R7: Implicit Synchronization And Lifetime Audit

### R7 boundary

R7 audits guarantees that RED receives outside its authored CPU and GPU graph edges: command-list state connection, copy-queue flushing, global submission serialization, resource retention, frame-global mutable state, swap-chain serialization, and backend failure behavior. It does not redesign node authoring, allocator placement, graph caching, cameras, or the terminal frame context. R8 owns the complete cache/camera/context/terminal-chain composition. No production implementation is authorized by this section.

### Source snapshot

The decisive RED sources for this pass are:

- `gpuApi/src/dx12/gpuApiDX12CommandList.cpp`, 1,570 lines, SHA-256 `14A5895ADA4CD8D86618AFF1EDB9C3B033CD89F9256E6A97958561C2461CB8D4`;
- `gpuApi/include/gpuApiInterface.h`, SHA-256 `5805B58F7CC06FC30FE30EE31F0974D3FCC926717F009D6155D875569FAECA74`;
- `renderer/src/renderGraphNodes.cpp`, SHA-256 `88CDF545C5C438A2807D85ED73310F1F608AB6D916DABF659E4123ABE64F7169`;
- `renderer/src/renderInterface.cpp`, 3,585 lines, SHA-256 `5E98A04FF8E0DCD281A532F51BA6F63226326E5CDF2366A93B219F2FDBDAC9DD`;
- `renderer/src/renderViewport.cpp`, SHA-256 `4D3E9E7B188D2F99EA0A85DE2F9F32D51E960E6C4467F62233B93057C4161ACC`;
- `renderer/src/renderFlowInternalData.cpp`, SHA-256 `CDF7E3C296EF9D6FD465504E8C509EC25D6BCD9F4204F29FF39AE9B2E4A77AD4`.

The corresponding Vanguard snapshot is `common_backend.cpp` SHA-256 `57DD0FDB392F8E10D354D6AA2FD7D3A85477EA10DED6EBEF7BDABC674BD0A185`, `d3d12_backend.cpp` SHA-256 `9569523D71608B72DBBF8E130EB9F037794948FC80800223D00C1BC620FA4017`, `render_flow_resource_allocator.cpp` SHA-256 `A149D87F7D364A504D89DBEA278267B1036016311C64CB55E134AD619D617677`, `render_flow_resource_resolve.cpp` SHA-256 `6528C39A12E7DCCE5504F429153F5AF3CDDCB99053286790CE1CB467D1574AC3`, `render_flow_resource_execution.cpp` SHA-256 `1E1D1C8ED7BB19CAA80D41ABD23540D33E0977230C031A8C7737D7220A3D579B`, `viewport.cpp` SHA-256 `E90E9B610B43BE87FA0E8484C5C5CFA5269B0E3FC27924CEB3AAEFDC47C4A282`, and `render_command_system.cpp` SHA-256 `20BE35EF200F1C17B30E68FCF022CAAF6126B7F21FDB397679BB68D40D4BF15E`.

### RED's authored graph is not its complete synchronization model

RED's `CRenderNode_Synchronize` requests allocator queue synchronization and submits the renderer-owned command-list range (`renderGraphNodes.cpp:279-297`; `renderInterface.cpp:137-186`). The graph describes which work reaches that boundary, but the DX12 backend supplies several additional guarantees:

- it flushes pending copy-queue work before every non-empty render submission (`gpuApiDX12CommandList.cpp:1286-1291`);
- it back-propagates initial resource states, merges Graphics/Compute initial states, and connects adjacent command-list state trackers (`1341-1379`);
- it collects resources that may be released only after command-list completion (`1382-1397`);
- it closes lists and submits them through deferred Jobs owned by the synchronization node's continuation (`1399-1456`);
- it inserts Fork/Join queue waits using process-global queue fences (`1459-1498`);
- Submit and Present share `m_globalSubmitMutex`, so native queue operations do not overlap (`1491-1497`; `gpuApiDX12Render.cpp:259-316`).

Those are real correctness properties, but most are not authored graph edges. Vanguard copies their semantics only where its RHI already owns the equivalent contract; it does not reproduce the hidden globals or infer that CPU completion means GPU visibility.

### Explicit ordering domains

R7 confirms four distinct ordering domains:

| Domain | Vanguard proof |
| --- | --- |
| CPU readiness | R4 CPU dependency and occurrence continuation. |
| Recorder readiness | Scope owner reached `Recorded`, its child continuation and cleanup completed, and its command list is unbound. |
| Queue execution order | Immutable R5 submission-batch order on one queue or an executable Fork/Join wait. |
| GPU completion | Valid queue fence completion; never a Jobs counter or completed submission API call alone. |

The RHI submission lock is a physical exclusion mechanism, not a scheduler. Two unrelated workers racing to call `CloseAndSubmitCommandLists()` obtain an unspecified order even though each call is serialized. Therefore every order required by the frame must be represented by submission-batch dependencies before native calls begin. Same-queue FIFO then preserves the listed order. Cross-queue visibility requires a compiled executable wait; `GpuOrder`, CPU edges, declaration order, and lock acquisition order are insufficient.

### Required command-scope resource-state contract

RED connects independently recorded command-list state trackers during submission. Vanguard currently cannot rely on that behavior. Its RHI opens NVRHI command lists with automatic barriers disabled and every resource with `keepInitialState = true` (`common_backend.cpp:264-286,2648-2655`). `TransitionTexture()` and `TransitionBuffer()` validate the caller's `before` state against the current command-list tracker (`3716-3763`). A newly opened second command list initially sees the resource descriptor's initial state, not the terminal state compiled for a preceding packet. Consequently a legal allocator transition such as packet A `RenderTarget -> ShaderResourceGraphics`, followed by packet B `ShaderResourceGraphics -> UnorderedAccess`, can fail while recording packet B or produce an invalid boundary if its tracker is not seeded.

This is a Render Graph integration blocker, not an allocator-planning defect. Before production graph execution, Resolve must publish an immutable resource-state entry contract for every command scope. For every physical buffer and texture subresource first observed by that scope, the contract records the state produced by the preceding compiled use, import, initialization, or alias activation. After opening the native command list and before executing any packet action or node command, the RHI adapter seeds NVRHI tracking with `beginTrackingBufferState()` or `beginTrackingTextureState()`. Seeding declares the known incoming state; it does not emit a barrier. Existing compiled actions then validate and emit only the required transitions and UAV barriers.

The first scope's entry state comes from resource creation or the retained import contract. A later scope's entry state comes from the allocator's deterministic logical state timeline. Its predecessor scope must be ordered before it by the compiled same-queue batch sequence or an executable queue wait. Entry contracts are generation-bound, physical-resource-bound, subresource-exact, applied once before recording, and rejected if two entries disagree. The RHI may validate the scope's observed exit states for diagnostics, but the allocator remains the state-planning authority. Process-global permanent-state mutation is not used to connect frame-local scopes.

This adaptation preserves RED's state-continuity guarantee while fitting NVRHI's explicit tracker API and Vanguard's single declaration/single execution rule. Until it exists, physical Render Graph execution across multiple resource-sharing command scopes remains fail-closed; logical-only allocator tests do not prove that seam.

### Copy work and external producers

RED's unconditional copy flush hides a dependency from every graph. Vanguard rejects that model. A graph submission never flushes an unrelated producer queue as a side effect.

The current renderer's texture, geometry, and GPU Scene uploaders use `CommandListType::CopySync`, which maps to the Graphics queue (`rhi_types.hpp:301-322`; `texture_uploader.cpp:710,1015`; `geometry_upload.cpp:531,569`; `gpu_scene_upload.cpp:481,521`). They may feed the initial Graphics-first Render Graph when their submission call is proven to happen before graph submission and their published state matches the retained import contract. Same-queue FIFO supplies GPU order; GPU completion need not be waited on by the CPU.

A real `CopyAsync` producer is different. The current public RHI has CPU fence waiting and the bounded Graphics/Compute Fork/Join submission modes, but no general `queue waits on GpuFence` operation (`rhi.hpp:137-149`). The allocator correctly rejects `ExplicitFenceWait`, incoming fences, Copy continuation queues, and mismatched first/final queues (`render_flow_resource_allocator.cpp:651-767`; `render_flow_resource_resolve.cpp:1381-1411`). R7 keeps that fail-closed boundary. Supporting `Copy producer -> Graphics graph` later requires a narrow RHI queue-wait contract, a compiled external dependency record, execution receipt evidence, and allocator import lowering. It must not be simulated with graph order or a submission mutex.

An already completed external fence may be consumed by the owning provider before graph planning, but completion alone does not authorize silently relabelling the resource's state or continuation queue. The provider must publish an explicit handoff contract that matches the registered import.

### Resource retention, residency, and aliasing

RED collects pending releases from submitted command lists. Vanguard's RHI instead retains direct command operands in each `CommandListPayload`, records their queue-fence use after submission, and releases them on discard or submitted-payload finalization (`common_backend.cpp:2670-2703,2794-2822`). The allocator generation additionally retains imported and allocator-owned resources through terminal `Finish()`. R7 keeps those owners; graph node code receives non-owning resolved views only while its packet use is active.

Resources reached indirectly through bindless indices are not discoverable from command operands. The execution adapter must add every declared indirect resource to the current command list's residency working set before the draw or dispatch that can reach it, using the existing `AddToResidencyWorkingSet()` contract (`rhi.hpp:150-153`). An undeclared or unretained indirect reference is an execution error, not a reason for the graph to scan global descriptor tables.

Placed-resource reuse remains governed by allocator lifetimes plus explicit predecessor finalization and alias activation. R7 adds no hidden lifetime extension. If predecessor completion cannot be ordered before activation in the same queue or through an executable wait, Resolve rejects the alias schedule. Dedicated resources may still be reused for strictly non-overlapping compiled lifetimes; that is physical reuse, not an implicit synchronization mechanism.

### Global mutable renderer state

RED node wrappers repair global texture bindings before and after node execution (`renderGraphNodes.cpp:140-165,193-234`), and several RED paths rely on renderer/GpuApi globals. Vanguard does not copy that implicit mutable-state lane. Frame-invariant data is captured in the retained frame context. Mutable CPU services require explicit CPU dependencies or an identified exclusive service boundary. Recorder-local bindings belong to the command scope and are cleaned before it reaches `Recorded`. A node that mutates unmodelled global GPU or CPU state is rejected from parallel scheduling until that side effect receives a declared owner and dependency.

Manual RED state overrides such as `TellTextureUAV()` are likewise not copied. Vanguard node declarations provide the authoritative required state; exceptional external mutations must enter as typed imports with truthful state/queue evidence.

### Submission, failure, and terminal lifetime

Vanguard's RHI already serializes Close/Submit and Present through the same `m_submissionLock` (`common_backend.cpp:2705-2709,2861-2867`). It retains closed payloads, reports `workSubmitted`, publishes per-queue fences, and converts post-execution fence-signal failure to DeviceLost while releasing submitted payload ownership (`2764-2858`). R5-R6 receipts preserve that truth: prior successful batches remain Submitted, untouched scopes are DiscardedBeforeSubmission, and fence-less native execution becomes UnknownDueToDeviceLoss. No failure path may rewrite submitted work as discarded.

The terminal continuation remains responsible for joining all occurrence, child, recorder, submission, and cleanup continuations before allocator `Finish()`. CPU completion permits destruction of graph frame metadata only after all callbacks stop accessing it. GPU-backed resources retire against the complete submitted fence set, not against terminal CPU completion. Device loss owns cases where safe completion cannot be proven.

### Presentation and viewport mutation

RED's final Sync-to-Present CPU edge relies on backend submission/Present serialization and a mutable viewport whose resize path performs a renderer-wide flush (`renderGraphNodes.cpp:359-371`; `renderViewport.cpp:421-505,571-588`). Vanguard retains same-frame Present but uses its stronger exact-acquisition state machine:

```text
main-thread acquisition
  -> retained presentation import
  -> final Graphics scope records exact Present transition
  -> successful final submission receipt
  -> allocator Finish
  -> terminal Present node consumes the same acquisition under the viewport operation gate
```

The RHI submission lock orders the native Present call against native submissions, while the per-viewport operation gate orders worker Present against main-thread resize, rebind, and destruction. Neither lock substitutes for the graph's final submission dependency. Resize may perform backend GPU-idle and outstanding-present-fence waits only after it owns the gate and no acquisition is active (`d3d12_backend.cpp:2500-2539`).

Two prerequisites from R5-R6 remain mandatory: the allocator needs the acquisition-aware `SwapChainPresentTransition` action, and `MarkPresentTransitionSubmitted()` must require a valid Graphics completion fence rather than testing the default queue field alone (`d3d12_backend.cpp:388-392`). Present is forbidden after discard, incomplete final submission, allocator terminal failure, stale acquisition, or DeviceLost. There is no next-frame presentation drain.

### R7 disposition ledger

| RED behavior | Vanguard disposition |
| --- | --- |
| CPU and GPU graph edges | Copy their distinct meanings. |
| Adjacent command-list resource-state connection | Adapt to compiled scope-entry state contracts and explicit NVRHI tracker seeding. |
| Graphics/Compute Fork/Join waits | Copy through immutable R5 dependencies and existing RHI lowering. |
| Global submission/Present mutex | Keep the existing RHI lock as physical serialization only. |
| Automatic copy flush before render submission | Reject; require same-queue order or an explicit external queue wait. |
| Command-list-owned resource retention | Keep the existing RHI lifetime and residency evidence. |
| Hidden global bindings and manual state repair | Reject; use retained context, scope-local bindings, or declared side effects. |
| Renderer-wide flush before viewport mutation | Adapt to per-viewport operation gating plus backend idle/present-fence waits where mutation requires them. |
| Assertion-only queue and state legality | Reject; fail compilation or execution in Shipping. |
| Submission-call completion as GPU completion | Reject; require a valid queue fence. |

### R7 exit state

R7 is complete at the architecture level. RED's hidden synchronization has been classified rather than accidentally copied. The first production Render Graph remains viable with Graphics and `CopySync` producers, explicit R5 Graphics/Compute regions, retained resources, truthful receipts, and same-frame Present. Before physical multi-scope execution, implementation must add command-scope entry-state seeding. Before real Copy-queue imports, it must add a general RHI queue-wait primitive and allocator lowering. Presentation additionally requires the two narrow prerequisites already identified above. All unsupported paths fail closed.

That cache, camera, execution-context, and terminal-chain study is completed by R8 in Section 50.

## 50. RED-Faithful Revision R8: Cache, Cameras, Execution Context, And Terminal Chain

### Boundary

R8 closes the last evidence pass before the authoritative R9 rewrite. It settles the complete relationship among RED-style cached graph definitions, prepared multi-camera state, one-shot frame execution, per-occurrence node contexts, and the StartRender-to-EndFrame chain. It does not implement the graph, redesign the resource allocator, add renderer features, or revive the former independent pass-compiler plan.

### Source snapshot

The RED evidence for this revision is pinned to the following local source snapshot:

```text
renderGraphCache.h          F24870EB257B38D9B3776BC248C5CC04BE30C023F39A8DC19E3C5AD41EA13D78
renderGraphCache.cpp        9F6F769FD904801CDE3DEDCC8A85C666DC4444F42722564A0289764A69495DC9
renderRenderFrame.cpp       F2D36A9CF070D5CC0AFDBC828728781BC3BBE6FD7BC069FD5C8A487B9139E6D8
renderNodeImplContext.h     40589186AE79A12C45FA4DE6A6F7F3733E50395E2568DBCDE5F1330095C92B6A
renderNodeImplContext.cpp   B7B34F37488FFAE4EC596037FDA7FF3B80EC3E9983AED991937BD0758FCEE5A7
renderGraphNodes.cpp        88CDF545C5C438A2807D85ED73310F1F608AB6D916DABF659E4123ABE64F7169
renderNodeGraph.cpp         98BEBCF439C51698B6194EF1450966E32F5ED6A20559DED2EC3C2FC2D2049962
```

The current Vanguard seam was checked against `frame_renderer`, `render_command_system`, `render_camera`, `viewport`, and render-flow execution headers and sources. These files remain production inputs, not files changed by R8.

### What RED actually caches

`CRenderInterface` owns one `CRenderGraphCache` for the renderer lifetime (`renderInterface.cpp:1421,1559`). The cache is a fixed array of four entries. `GetGraph()` linearly scans for a matching whole-frame camera hash and camera-setup count while remembering the least-recently-used entry by `m_lastUsedFrame`; a miss immediately resets the oldest entry and allocates its camera setup records (`renderGraphCache.h:59-77`; `renderGraphCache.cpp:11-44`). This is a bounded fully associative cache, not a hash map.

Each entry owns the composed final graph and an array of `SCameraSetupData`. Each camera setup owns a temporary camera graph, its node implementations, and one camera-setup hash. Equal hashes within the entry reuse the same built fragment; `AddGraph()` still creates a distinct camera-indexed occurrence for every camera. After composition, `PostBuildClear()` discards temporary fragment topology but retains the node containers because the final graph stores raw implementation pointers (`renderGraphCache.cpp:46-68`; `renderRenderFrame.cpp:4686-4798`).

RED uses a hash as identity, mutates the selected entry before rebuilding succeeds, and depends on external serialization plus `CRenderNodeGraph`'s exclusive update flag while jobs read the cached graph (`renderRenderFrame.cpp:4823-4835,4969-4977`; `renderNodeGraph.cpp:921-938`). These are implementation shortcuts, not features Vanguard must reproduce.

### Vanguard definition cache

`FrameRenderer` owns one bounded `RenderGraphDefinitionCache`. The baseline capacity is four entries, preserving RED's inexpensive deterministic scan and LRU replacement. A cache entry owns:

- a complete canonical `RenderGraphKey` and its cached hash;
- a strong immutable `RenderGraphDefinitionRef`;
- the last-used non-zero render-frame serial;
- a definition generation used only for diagnostics and stale-identity rejection.

`RenderGraphKey` is fully comparable. Hash equality only selects a candidate; complete key equality proves a hit. The key contains the structural inputs consumed by the current graph builders: rendering mode and purpose, output kind and extents, ordered `RenderViewGraphKey` occurrences, camera dependency shape and output kinds, frame-global feature policy, and renderer feature revision. Dynamic matrices, jitter values, scene contents, native resources, allocator identities, payload pointers, acquisitions, fences, and profiling state are never cached. A queue or backend capability may become a key field only when graph construction actually branches on it.

The render-frame serial, rather than wall-clock time or RED's `GetLastTickCounter()`, updates recency. It directly describes cache use by accepted rendering frames, is monotonic within the renderer lifetime, and is already carried by `RenderFrameInfo` and `PreparedRenderViewFamily`.

Lookup and publication are distinct:

```text
Lookup(complete key)
  -> hit: retain immutable definition and update recency
  -> miss: build a private candidate without touching live entries
  -> validate, compose, compile, and seal candidate
  -> Publish rechecks for a concurrent hit
  -> only then replace the least-recently-used lookup owner
```

A failed build leaves every live entry unchanged. The baseline `RenderCommandSystem` CPU tail prevents cache mutation or eviction while the preceding frame still uses its selected definition. Lookup/publication metadata may use one narrow lock, but graph construction and compilation never occur under it.

There is no second renderer-global cache of per-view graphs in the baseline. Like RED, equal `RenderViewGraphKey` values are deduplicated while building one whole-frame candidate. Their implementations can be shared by multiple view occurrences in that definition, but a different whole-frame key does not consult an independently persistent view cache. Adding one now would increase invalidation and ownership complexity without preserving a RED feature.

`ClearPersistentCaches` for the graph drops cache lookup references only after the RenderPath CPU tail is quiescent. Renderer shutdown and device recreation explicitly clear old lookup ownership before allocator/RHI teardown; an already retained definition remains alive only long enough for its execution to terminalize. Cached definitions own CPU structure and node implementations, never native device objects.

### Multi-camera selection and composition

RED first allocates all camera data, including dependent cameras, then hashes the ordered complete camera set. Its per-camera hash includes rendering mode, display mode, whether a full scene exists, and every word in the camera feature bitset (`renderRenderFrame.cpp:4482-4507,4614-4669`). On a miss, each distinct camera setup is built once and appended once per camera index; blank/no-camera frames reserve a setup slot but do not represent a real camera (`4686-4798`).

Vanguard starts from the stronger existing contract: `PreparedRenderViewFamily` is an immutable retained generation whose views are already dependency ordered child-before-parent, carry exact scene identity/version and frame serial, and cannot be invalidated by camera mutation while referenced (`render_camera.cpp:1214-1311,1556-1657`). R8 maps that family into a definition as follows:

```text
prepared dependency-ordered views
  -> build one RenderViewGraphKey for every ordered view occurrence
  -> build one RenderGraphKey from the ordered occurrences, dependency edges, and frame policy
  -> on a miss, build each distinct view definition once
  -> append one occurrence for every prepared view in family order
  -> assign the occurrence's exact view slot, FlowSpaceId, and dependency outputs
  -> merge frame-global unique nodes and compile the complete graph once
```

Equal per-view keys share immutable implementation objects, not occurrence identity. Each occurrence keeps its own `RenderViewId`, view slot, camera dependency edges, flow space, resource bindings, and diagnostics. Child-to-parent camera outputs become explicit graph resource/version edges; equal names or equal implementations never imply cross-camera sharing. Frame-global nodes have no arbitrary camera index and may not read camera-local state.

Custom-data instances remain owned by scene/camera storage and persist independently of cached graph definitions; their prepared values are frame-local. They may affect execution values and captured resource decisions, but they may not silently alter cached topology. Any custom-data fact that genuinely changes node membership, group structure, resource schema, or queue lowering must be promoted into the canonical feature policy and graph key. This replaces stale-cache behavior with an explicit structural contract without cutting RED's camera modes or features.

### Frame-wide custom-data preparation

RED treats scene/camera custom-data preparation as general renderer work rather than graph topology. After selecting the cached graph, it creates the reserved Graphics command list `CL_STORAGE_DATA`, binds it, invokes every scene custom-data `Prepare`, then invokes every custom-data object for every camera. Only afterward does it enter render-flow PreConsume and Resolve (`renderRenderFrame.cpp:4857-4961`). The reserved command-list prefix places `StorageData` before graph flow-group command lists when a later Sync node submits the contiguous range (`renderInterface.cpp:137-188`). Vanguard preserves this boundary.

`FrameRenderer` owns one named `ReservedFrameCommandList::StorageData` recording slot in the retained frame. Its allocator command-scope identity and receipt remain separate terminal evidence. It is not a `RenderNodeGraph` occurrence and is not stored in the cached graph. `RenderNodeImplContext::InitData` carries a borrowed `RenderCameraStorage`, and `RenderCameraStorage::PrepareCustomData(RenderNodeImplContext&, RenderCameraFailure*)` performs the RED-shaped scene pass followed by the ordered prepared-view pass. Before each camera callback it applies that view to the context; no dummy graph occurrence or fake allocator packet is created.

The existing fixed-index polymorphic directories, scene/camera ownership, `Initialize`, `Prepare`, `Evict`, readiness query, and per-frame prepared marker remain. `SceneCustomData::Prepare` receives the node implementation context, camera storage, and failure sink; `CameraCustomData::Prepare` receives the same context, current prepared view, and failure sink. Both return `bool`, and `RenderCameraFailureCode::CustomDataPreparationFailed` identifies the exact custom-data kind and type index. This preserves Vanguard's Shipping-active failure contract instead of copying RED's assertion-only `void Prepare`. The temporary CPU-only `CustomDataPrepareInfo` contract is retired when RG installs the context-aware path. This is one polymorphic preparation mechanism, not parallel CPU and GPU interfaces.

`StorageData` may record creation, initialization, upload, and state work for renderer-owned persistent resources. It may not read or mutate graph transient/aliased allocations because those do not exist before allocator Resolve. A persistent resource later consumed by the graph is imported with the state and queue produced by preparation. Same-queue submission order is explicit in the frame command plan; a consumer on Compute or Copy receives a compiled queue-wait edge from the `StorageData` submission receipt. Thus Vanguard follows RED's ownership and call placement while making synchronization and allocator boundaries truthful.

### Allocator startup and resource preparation

After `StorageData`, `FrameRenderer` derives the frame's `FrameResourcePolicy` and calls `RenderFlowResourceAllocator::BeginFrame` directly. The policy includes `processEviction`: normal onscreen game frames process pool trimming, blank or offscreen game frames preserve the normal rendering working set, and non-game/tool frames process eviction. This is independent of `enablePlacedResources`. RED's Durango ESRAM permission is not mapped onto placed-resource enablement; any future platform fast-memory tier requires a separate explicit policy.

`RenderNodeGraph::PrepareResourcesParallel(RenderNodeImplContext&, RenderFlowResourceAllocator&, RenderNodeResourceBindings&, RenderNodeResourcePreparationFailures&, jobs::Builder&)` calls the separate declaration side of each resource-recording node through RED's `Process()` orchestration boundary. GPU flow group, command scope, queue begin/end, and explicit Sync information arrive as part of that allocator request stream. The operation schedules independent preparation work through the supplied Jobs builder, but it does not execute node GPU work, record commands, or perform Resolve. A following named job checks the retained preparation-failure latch, seals allocator planning after those predecessors join, and calls `RenderFlowResourceAllocator::Resolve` directly with no graph object, survivor overlay, or public queue-schedule getter; the ordered finish job then releases the node-kickoff deferral.

### One retained frame

RED installs the current `IRenderFrame` in Jobs-global state, creates an `SRenderNodeImplContext` per job, and fills it with a strong frame pointer plus borrowed allocator, camera storage/data, camera index, dispatcher index, visibility ID, render-flow space/group, command list, dimensions, and mutable binding cleanup flags (`renderRenderFrame.cpp:4260-4279,4802-4853`; `renderNodeImplContext.h:94-110,278-365,486-509`). `EndFrame` clears that global frame pointer (`renderGraphNodes.cpp:1601-1628`). Vanguard preserves this visible job contract with a non-owning `RenderNodeJob::JobsRenderFrame` pointer. The pointer addresses the `RenderFrameInfo` copy inside one stable `RenderCommandSystem::RetainedFrame` allocation. `RetainedRenderFrameRef` is the cheap strong ownership handle used by the command-system root, node branches, and terminal continuation; those handles share one frame state rather than copying it. Clearing the job-visible slot compares against that exact address, so delayed cleanup cannot silently clear a different installed frame; last-reference destruction performs the same clear when ordinary terminal scheduling fails.

The command system's one move-only retained-frame record owns or retains:

- copied immutable frame scalars and the retained application payload;
- the exact `PreparedRenderViewFamily` and `FrameCustomData` view of it;
- the selected graph definition and frame-local occurrence bindings for the duration of the serialized frame chain;
- the exact move-only output transaction, including acquisition identity and revision;
- frame-local packet/decision binding tables and command-scope records for the allocator's internally owned active generation;
- every command list, submission/discard receipt, dependency receipt, and first-failure record needed until terminalization;
- the continuation/completion sink that extends and reports into the existing `RenderCommandSystem` CPU tail.

The retained frame does not retain the stack `RenderFrameContext`. It is allocated once at a stable address; copying or moving a `RetainedRenderFrameRef` never relocates the frame or invalidates pointers borrowed by already-dispatched jobs. The terminal continuation and each independently scheduled root branch retain the allocation. `RunRenderNodeJobs` carries that reference into its joined branch-cleanup continuation, while individual node children borrow the frame under that proved branch lifetime instead of taking one strong reference each. Node jobs access immutable frame data, the exact prepared family, frame-local allocator bindings, and failure members under that ownership. The allocator itself owns the active generation. Normal terminalization destroys the frame only after allocator, camera-family, output, command-list, and failure-reporting responsibilities have each reached an exactly-once disposition.

All required task objects are allocated before the first independent node branch is dispatched. If later counter extraction or terminal dispatch fails, the retained frame is armed for fail-closed terminalization: after the last outstanding job reference releases, its owner performs the same exactly-once terminal dispositions instead of leaving `JobsRenderFrame`, allocator state, or output ownership stranded. This fallback is for job-system failure only and never replaces the normal explicit terminal job.

`RenderNodeImplContext` is constructed for one scheduled occurrence and contains borrowed retained-frame access, occurrence identity, optional view slot and `RenderViewId`, CPU/GPU group identities, dispatcher thread index, Jobs continuation, and—only for a recording occurrence—the compiled queue/scope, bound recorder, packet cursor, and typed resolved bindings. It exposes renderer services through narrowly typed accessors whose owners outlive the serialized RenderPath chain. It does not expose raw allocator mutation, store frame state in the cached node implementation, or infer resources by name.

Declaration and execution remain different callbacks but use the same RED-shaped `RenderNodeImplContext` type. The wrapper initializes it in declaration mode to capture immutable structural decisions and the canonical resource-use tape before Resolve, then initializes it in execution mode to expose only compiled decisions and bindings during the node's single execution. Concrete nodes do not branch on the mode: `Declare()` declares and `Execute()` performs work. A child job receives another occurrence context with an updated dispatcher index and contributes to that occurrence's completion before its packet can finalize.

### Output ownership transfer

The current production seam does not yet carry `RenderOutputAcquisition` inside `RenderFrameInfo`, and `ViewportManager::Present()` and `AbandonOutput()` still reject non-main-thread callers (`viewport.hpp:196-322`; `viewport.cpp:462-533`). R8 therefore makes the transfer explicit rather than pretending the agreed same-frame Present already exists.

Immediately before command-system dispatch, `ViewportManager::SubmitFrame()` acquires the exact output and creates one move-only `RenderFrameOutputTransaction`. Synchronous acquisition or dispatch failure leaves ownership on the main thread, which abandons the acquisition before returning. Successful dispatch moves the transaction into the command system's retained frame; no other owner may present, abandon, resize, rebind, or destroy that acquisition.

The transaction retains the viewport terminal authority and exact viewport generation, output revision, output kind, texture, and optional acquired back-buffer token. Worker-side `Present`, texture completion, abandonment, and device-loss terminalization use the per-viewport operation gate settled in Section 44. The manager must outlive the RenderPath tail, which matches the existing shutdown order: viewport shutdown first flushes the command chain before destroying viewport state (`viewport.cpp:166-195`).

### Exact normal terminal chain

RED keeps named `StartRender`, `EndRender`, final `Synchronize`, `Present`, cleanup, and `EndFrame` nodes. The full camera graph orders final GPU submission before Present and joins Present/optional grabs with independent batch cleanup at EndFrame (`renderRenderFrame.cpp:3119-3147,3231-3291`). `StartRender` opens renderer/scene state, `EndRender` closes it, Present calls the viewport, and EndFrame advances allocator cleanup, notifies the scene, and clears RED's global frame (`renderGraphNodes.cpp:301-371,1601-1628`; `renderRenderFrame.cpp:4022-4250`). Vanguard preserves those visible named responsibilities.

The successful Vanguard chain is:

```text
main thread: reconcile viewport -> acquire output transaction -> dispatch retained frame
RenderPath: prepare/retain view family -> select or build definition -> bind frame-local graph state to the retained frame
StorageData: record scene/camera custom-data preparation into the reserved frame scope
Resources: BeginFrame -> PrepareResourcesParallel applies the finalized definition's declarations and explicit Sync requests -> named allocator Resolve job -> release node-kickoff deferral
StartRender: open frame-global renderer and scene state
Body: execute CPU nodes and record every finalized-definition resource-owning occurrence once
EndRender: close scene/frame producer state after all authored predecessors
FinalSubmission: join recorder completion -> submit compiled batches -> collect truthful receipts
AllocatorTerminal: join every packet owner -> Finish(Completed)
CameraCommit: commit the prepared family exactly once
Present/CompleteOutput: consume the same output transaction in the current frame
Cleanup: run named renderer cleanup branches
EndFrame: join Present/output completion and cleanup
FrameTerminal: publish completion/failure and release the retained frame
```

`RenderFlowResourceAllocator::Finish(Completed)` occurs after all packet work and command-scope receipts are joined but before Present. A successful Finish proves the back buffer reached its compiled terminal state and its physical ownership is settled. `PreparedRenderViewFamily::Commit()` follows successful completed allocator terminalization and precedes output consumption. If native Present later fails, camera history remains committed because the rendered frame was submitted; the viewport/output enters its defined failed or retry state instead of rewriting camera history.

Present never waits for GPU completion. The final Graphics submission dependency plus RHI queue ordering makes the submitted exact Present transition precede native Present. The two prerequisites from R6-R7 remain: the allocator must emit the exact acquisition-aware `SwapChainPresentTransition`, and the backend must acknowledge it only with a valid Graphics completion fence.

### Failure and finally semantics

Normal graph nodes do not own global unwinding. One executor terminalizer runs exactly once even when declaration, Resolve, recording, submission, allocator Finish, node execution, output consumption, or cleanup fails. It preserves named RED-style cleanup nodes on the success graph while guaranteeing the equivalent mandatory dispositions on every failure path.

| Failure point | Required terminal behavior |
| --- | --- |
| Before output ownership transfer | Caller retains or synchronously abandons the acquisition; no graph execution exists. |
| After transfer but before any native submission touches output | Cancel unopened/executing packets, emit truthful discarded receipts, `Finish(Aborted)`, abandon output, do not commit camera history. |
| After partial submission | Join all dispatched CPU work, preserve submitted/discarded receipts, `Finish(Aborted)` when valid, and route the touched output to recovery rather than falsely abandoning it. |
| Device loss or post-execution signal ambiguity | `Finish(DeviceLost)`, invalidate device-bound output state, clear cache lookup ownership for the old capability epoch, and never Present. |
| `Finish(Completed)` fails | Do not Present or commit the family; retain the exact output for recovery because submitted work may have touched it. |
| Present fails after completed submission | Keep allocator completion and camera commit, mark/report output failure, and follow the RHI ticket retry/device-loss contract; never follow failed Present with abandonment. |
| Cleanup or EndFrame service callback fails | Preserve already completed allocator/output dispositions, report the first failure, continue mandatory remaining cleanup, and release only after the terminal join. |

The command-system CPU tail remains the only outer rendering chain. Work dispatched through `RenderFrameContext::GetBuilder()` extends that chain; the graph does not create a detached scheduler or synchronously wait inside a worker. The current `RenderFrameDispatcher::completedFrames` and `lastCompletedSerial` are updated when the root callback returns, before asynchronous graph terminalization (`render_command_system.cpp:216-248`). R9 must rename those as dispatch/callback statistics or move completion publication into the retained frame's terminal continuation. They must not be presented as completed rendered frames.

Shutdown and cache clearing follow one direction:

```text
stop accepting frames
  -> ViewportManager shutdown stops acquisition and flushes the RenderCommandSystem CPU tail
  -> consume the asynchronous execution failure latch
  -> verify no retained output transaction or prepared family remains and release viewport output resources
  -> shut down RenderCommandSystem after the tail is quiescent
  -> clear RenderGraphDefinitionCache lookup ownership
  -> clear allocator persistent caches and retire native resources
  -> shut down allocator and RHI
  -> destroy camera and scene owners in their established order
```

No lock is held while waiting for the render tail. Device-loss abandonment performs the same logical terminalization without requiring successful GPU waits.

### R8 disposition ledger

| RED behavior | Vanguard disposition |
| --- | --- |
| Four-entry linear LRU-like graph cache | Preserve as the baseline bounded `RenderGraphDefinitionCache`. |
| Hash-only whole-frame and camera identity | Strengthen to full comparable keys with hash acceleration. |
| Destructive miss before rebuild | Replace with private build and atomic successful publication. |
| Per-entry camera setup array | Preserve candidate-local distinct-view-definition reuse. |
| Persistent independent per-camera cache | Do not add; RED does not provide one. |
| Raw implementation pointers retained by camera node containers | Replace with definition-owned implementation arena and strong definition references. |
| Exclusive mutation of a cached graph during execution | Replace with immutable published definitions and frame-local execution state. |
| Ordered multi-camera composition | Preserve using prepared-family order and explicit dependency outputs. |
| One implementation reused at multiple camera indices | Preserve with distinct occurrence identity and contexts. |
| Jobs-global current frame | Preserve as a non-owning pointer to the command system's move-owned retained frame; jobs borrow it under the serialized CPU-tail lifetime. |
| Rich `SRenderNodeImplContext` | Preserve as one per-occurrence `RenderNodeImplContext` used by both the declaration and execution callbacks. |
| StartRender/EndRender/final flush/Present/cleanup/EndFrame nodes | Preserve as named graph responsibilities with explicit dependencies. |
| EndFrame clears global frame and allocator phase | Preserve the named clear on success; the exactly-once executor terminalizer performs the same clear on every abnormal exit. |
| Tick counter for cache recency | Use monotonic accepted render-frame serial. |
| Present inside the same graph execution | Preserve with the Section 44 thread-safe output transaction. |

### Exit state

R8 is complete at the architecture level. The RED-faithful graph now has a settled owner, bounded cache and invalidation policy, exact multi-camera composition model, one move-owned retained frame, per-occurrence `RenderNodeImplContext`, current-frame output transfer, success chain, and failure terminalizer. No production code was changed.

R9 follows in Section 51 and replaces every conflicting provisional conclusion.

## 51. Preserved R9 Immutable-Definition Proposal

### Authority and vocabulary

This section preserves the completed R9 proposal. RG1 source comparison subsequently rejected its immutable definition, graph builder, generation-ID family, survivor overlay, and definition-owned arena. Section 52 and `render-graph-execution-plan.md` supersede it. The evidence and seam analysis below remain useful history but do not authorize implementation vocabulary.

The architecture uses `node`, `group`, `command-list group`, `definition`, `occurrence`, and `execution`. It does not use *pass* as the universal scheduled-unit name, and it does not call cached graphs *templates*. A definition is immutable reusable graph structure. An occurrence is one placement of a node implementation in a definition. An execution is one frame-local run of that definition.

RED is the default authority for graph representation, polymorphic render-node implementations, graph construction, CPU and GPU links, authored groups, command-list groups, per-camera composition, bounded caching, and the visible frame chain. Vanguard changes RED only at demonstrated seams:

- nodes declare complete resource uses before allocator `Resolve`; there is no discovery execution;
- each occurrence in the finalized definition executes once;
- definitions own implementations and executions retain definitions;
- identities are typed and generation checked;
- cross-queue order requires executable RHI waits and truthful receipts;
- output, allocator, camera, and failure ownership terminalize exactly once;
- validation remains active in Shipping.

Unreal RDG remains a secondary check for hazards, barriers, aliasing, queue synchronization, and diagnostics. It does not replace RED's public graph model.

### Ownership model

```text
RenderingService
  -> FrameRenderer
       -> RenderGraphDefinitionCache (four-entry baseline)
       -> RenderCameraStorage
       -> RenderFlowResourceAllocator
       -> RenderCommandSystem integration

RenderGraphDefinitionCache entry
  -> complete RenderGraphKey
  -> immutable RenderGraphDefinitionRef
  -> last-used accepted frame serial

RenderGraphDefinition
  -> external-to-topology RenderNodeImpl arena
  -> RenderNodeGraph topology and scheduled node occurrences
  -> sealed groups and command-list groups
  -> canonical declaration tapes
  -> CPU dependency graph
  -> GPU-order and executable queue-dependency graph
  -> command scopes and immutable submission batches

RenderCommandSystem retained frame
  -> selected RenderGraphDefinition for the serialized frame lifetime
  -> retained frame payload and PreparedRenderViewFamily
  -> per-occurrence frame bindings
  -> move-only RenderFrameOutputTransaction
  -> frame-local graph bindings and allocator generation-stamped packet bindings
  -> command lists, receipts, failure state, and terminal continuation
```

`RenderNodeGraph` owns topology, not implementation objects. Its occurrence records address implementations in a separate `RenderNodeArena` through generation-checked `RenderNodeImplId` values. The arena is external to the topology but is not external to its lifetime owner: `RenderGraphDefinition` owns the graph, the arena, and every temporary per-view topology as one retained generation. This preserves RED's cheap `AddGraph()` composition and implementation reuse without retaining raw pointers into an independently lived sibling container.

`RenderGraphDefinition` therefore owns every implementation object referenced by its occurrences. The serialized CPU tail prevents cache eviction while a frame uses the definition. Cached implementations contain immutable configuration only. Mutable per-frame state belongs to the command system's retained frame, and per-occurrence state belongs to `RenderNodeImplContext`.

The retained frame never retains the stack `RenderFrameContext`, borrowed builder data, or a mutable viewport pointer. Before the render callback returns, its ownership moves into a terminal continuation ordered after every graph child job. Child jobs borrow retained-frame data under that proved lifetime, and the terminal continuation releases it only after allocator, prepared-view-family, output, command-list, and result-reporting duties have reached a terminal disposition.

### RED-style definition authoring and composition

Authoring creates named node implementations and scheduled occurrences through typed builders. Node implementations derive from `RenderNodeImpl`; the engine wrapper invokes their nonvirtual declaration and execution protocol. Concrete renderer nodes keep their work in named implementation classes, not anonymous execution lambdas.

The authoring model preserves RED's capabilities:

- add named individual nodes;
- create ordinary dependency groups;
- create command-list groups with ordered child occurrences;
- link nodes or groups independently in the CPU and GPU domains;
- append sequences without losing their entry and exit sets;
- merge explicitly unique frame-global work;
- compose blank, no-scene, full-view, dependent-view, and multi-view definitions;
- reuse one immutable view implementation for multiple distinct view occurrences.

Names are diagnostic labels, not identity. Typed, generation-checked IDs establish identity. Group membership is sealed before compilation. Empty groups have defined entry/exit behavior. Ordinary groups expose an all-member CPU boundary. Command-list groups own one recorder boundary and serialize their ordered children on it. Composition validates name collisions, identity provenance, group legality, and dependency endpoints before publishing a definition.

Per-view construction produces reusable candidate-local `RenderViewGraphDefinition` values. The whole-frame builder appends one occurrence for every dependency-ordered prepared view, supplies its view slot and `FlowSpaceId`, explicitly connects child-camera outputs to parent consumers, merges frame-global unique nodes, and compiles the complete definition once. There is no renderer-global secondary view-definition cache in the baseline.

### Declaration and resources

Every `RenderNodeImpl` has two phases:

```cpp
bool Declare(RenderNodeImplContext& context, RenderGraphFailure* failure) const noexcept;
bool Execute(RenderNodeImplContext& context, RenderGraphFailure* failure) const noexcept;
```

`Declare` runs once while building a private definition candidate. It emits a canonical, definition-owned tape of typed texture/buffer/import/output declarations, views, states, accesses, decisions, side effects, and residency requirements. It performs no GPU recording and retains no frame-local native object. Conditional topology is decided by immutable structural policy already present in the graph key. Dynamic values become execution bindings or captured allocator decisions; execution never branches to discover undeclared resource use.

The baseline does not expose Unreal-style public SSA content-version handles. RED-style authoring passes typed logical resource and output-endpoint handles between node builders, while each declaration states its exact read, write, or read-write access. The compiler creates internal version records where needed for hazard, liveness, producer, and allocator validation. Those records are compiler evidence, not a second public graph topology that authors must manually maintain.

The complete composed graph is the selected executable graph, as in RED. Structural feature conditions and frame modes choose which nodes are authored and which cached graph is selected; there is no mandatory post-build resource-root culling pass in the production baseline. Every occurrence in the finalized graph executes, and every resource-owning occurrence performs its separate declaration callback directly into `RenderFlowResourceAllocator`. Historical sections that require `SurvivingGraphOverlay`, reverse-root culling, declaration-tape replay, or a graph-owned Resolve wrapper are superseded by this boundary.

Only a real resource-recording command-scope owner receives an allocator writer and compiled execution packet. Independent recording nodes normally own one command scope. Ordered children of one command-list group share its writer, packet, recorder, and scope. CPU-only, synchronization, presentation, and pure terminal nodes receive no fake packet. Within a shared packet, each child's declared use interval is emitted in child order and consumed once by that child's execution.

The allocator remains sole authority for physical assignment, placed-resource alias activation, resource-state actions, liveness of resolved uses, and terminal retirement. The graph cannot manufacture physical resources, extend liveness implicitly, or treat logical order as a barrier.

### CPU dependencies, GPU order, and command scopes

The compiled definition preserves two dependency domains:

- CPU dependencies determine when occurrence work may run and when Jobs continuations complete.
- GPU dependencies determine recorder order, executable queue synchronization, and submission order.

`GpuOrder` is never described as synchronization by itself. Same-queue order becomes submission FIFO. Cross-queue order is legal only when the RHI can lower it to a real signal/wait edge and the execution can report the corresponding receipt. Unsupported edges fail compilation with stable diagnostics.

Four milestones remain distinct:

```text
occurrence CPU completion
  != command-recorder completion
  != queue submission completion
  != GPU fence completion
```

Command scopes are authored ownership boundaries, not an automatic adjacent-node merge optimization. One independent recording occurrence is the baseline scope; one command-list group is one scope containing ordered children. Separate ready scopes may record concurrently when their CPU dependencies permit. Children sharing a scope never record concurrently.

The compiler emits immutable submission batches and explicit dependencies. The executor follows those batches; the RHI submission lock provides native serialization only and never supplies missing graph order. Graphics and bounded Graphics/Compute fork-join regions are supported through existing RHI capabilities. Existing `CopySync` producers may feed a Graphics-first graph only with proved prior same-queue submission and matching state. General `CopyAsync` imports and unsupported Copy crossings remain rejected until a real RHI queue-wait primitive, compiled edge, receipt, and allocator lowering exist.

Before independently recorded command scopes execute allocator actions, the implementation must provide immutable physical/subresource-exact scope-entry state metadata and seed NVRHI's command-list state tracker from it. Global or guessed state is forbidden. Declared indirect/bindless resources must be added to the command list residency working set before use.

### Execution contexts and one-shot node work

`RenderNodeImplContext` is rich and occurrence-specific, matching RED's useful execution ergonomics. Before node dispatch, `RenderNodeJob::JobsRenderFrame` is asserted empty and assigned a non-owning pointer to the command system's retained `RenderFrameInfo`. The same context type is initialized with declaration-only or execution-only capabilities before the corresponding callback. It contains or exposes:

- borrowed access to the command system's retained frame under the serialized CPU-tail lifetime;
- occurrence, optional view slot, and `RenderViewId`;
- CPU group, GPU flow group, and dispatcher identity;
- Jobs continuation access;
- retained immutable frame, scene, camera, and custom-data accessors;
- for recording work only, queue, command scope, bound command list, packet cursor, and typed resolved resource bindings.

The context offers narrow accessors; it does not expose raw allocator mutation. Declaration-mode accessors cannot record commands or resolve physical resources, and execution-mode accessors cannot add undeclared uses. For an independent recording occurrence, its scope executor opens the exact compiled packet, the occurrence begins and ends declared uses in tape order and performs its renderer work once, and the scope executor finalizes the packet. For a command-list group, the group scope executor opens the one shared packet once, invokes ordered child occurrences serially with access restricted to each child's compiled interval, and finalizes only after the last child consumes its interval. A child never opens or finalizes the shared packet. A CPU-only occurrence executes the same one-shot work protocol without a command list or packet. Cached implementations may not store mutable frame state after return.

Child-job-producing nodes extend their occurrence continuation explicitly. An occurrence is CPU-complete only when its `Execute` call and all retained child work complete. The terminalizer joins all occurrence, recorder, submission, and cleanup continuations before destroying retained-frame metadata.

The named `EndFrame` responsibility clears `RenderNodeJob::JobsRenderFrame` after the jobs that borrow it have joined. The exactly-once failure terminalizer also clears it when normal `EndFrame` is skipped. Tests that install the pointer without executing the full terminal chain must explicitly restore it to `nullptr`.

### Definition cache and multi-view identity

`FrameRenderer` owns a bounded, fully associative `RenderGraphDefinitionCache`; four entries are the baseline. Lookup linearly scans entries, checks cached hash, and then requires full `RenderGraphKey` equality. The key includes every structural fact consumed by the current graph builders: mode and purpose, output kind and extents, ordered `RenderViewGraphKey` values, camera dependency shape and output kinds, feature policy, and renderer revision. Matrices, jitter values, scene contents, native handles, allocator identities, payload pointers, acquisitions, fences, and profiling values are not key fields. Queue or backend capability fields are added only with a builder branch that consumes them.

Cache recency uses the accepted render-frame serial. On a miss, the renderer builds, validates, compiles, and seals a private candidate. `Publish` rechecks for an intervening hit and replaces the least-recently-used entry only after candidate success. Failed construction leaves the live cache unchanged.

`PreparedRenderViewFamily` supplies retained immutable dependency-ordered views. Equal `RenderViewGraphKey` values may share one implementation within a candidate, but every occurrence retains distinct view identity, flow space, dependencies, bindings, and diagnostics. Equal names or implementations never join camera resources. Frame-global nodes do not borrow an arbitrary camera index.

Device recreation explicitly releases old cache lookup ownership. `ClearPersistentCaches` runs only after the RenderPath CPU tail is quiescent. Cached definitions own CPU structure only, never native device resources.

### Current-frame output and presentation

Presentation is a frame-global terminal node in the same Render Graph execution. It is not delayed to the next frame and is not a main-thread mailbox drain.

The main thread retains authority for output acquisition, window reconciliation, swap-chain creation/recreation, resize/rebind, and viewport creation/destruction. Immediately before command-system dispatch, `ViewportManager::SubmitFrame` acquires the exact output and creates a move-only `RenderFrameOutputTransaction`. If dispatch fails synchronously, the caller still owns and abandons it. If the command system accepts the frame, its private retained-frame object owns the ticket; the ticket is not copied into public `RenderFrameInfo`. All later terminal operations occur through a per-viewport operation gate that serializes worker Present/Complete/Abandon/DeviceLost against main-thread lifecycle mutation.

The presentation path is exact:

```text
acquire exact output transaction on main thread
  -> bind exact texture/backbuffer as a graph import
  -> final Graphics scope records SwapChainPresentTransition
  -> submit final Graphics batch and obtain a valid Graphics fence
  -> report allocator terminal receipts
  -> allocator Finish(Completed)
  -> PreparedRenderViewFamily Commit
  -> Present or CompleteOutput in the same frame execution
```

`SwapChainPresentTransition` is an acquisition-aware allocator/RHI action, not a generic state transition inferred by the Present node. `MarkPresentTransitionSubmitted` requires a valid Graphics-owned fence. Present is forbidden after discard, missing final submission, stale acquisition, allocator terminal failure, or DeviceLost. Present need not wait for GPU completion; the native queue and swap-chain contract consumes the submitted work in Graphics order.

The CPU terminal `PresentNode` does not own an allocator packet. During compilation, its output dependency causes `SwapChainPresentTransition` to be appended to the real final Graphics recording scope that last owns the acquired backbuffer. The later CPU node only validates the final scope's Submitted receipt and consumes the transaction through the viewport operation gate.

Headless frames have no output transaction. Texture-output frames use `CompleteOutput`, not Present. Output kind is structural and belongs in `RenderGraphKey`.

### Exact frame and terminal chain

The successful path is:

```text
main-thread viewport reconciliation and output acquisition
  -> dispatch retained frame, which owns the move-only output transaction, into RenderCommandSystem
  -> prepare or retain PreparedRenderViewFamily
  -> build complete keys and select/build immutable definition
  -> bind the selected definition, prepared views, and output transaction to the retained frame before child dispatch
  -> bind frame-local occurrences and imports
  -> Declare-derived validation and compilation
  -> BeginFrame, graph resource preparation into the allocator, direct allocator Resolve
  -> StartRender responsibility
  -> CPU nodes, child jobs, and command recording
  -> EndRender responsibility
  -> join recorders
  -> submit immutable batches and collect truthful receipts
  -> join packet owners
  -> allocator Finish(Completed)
  -> prepared family Commit
  -> same-frame Present or CompleteOutput
  -> cleanup responsibilities
  -> EndFrame join
  -> publish terminal result and release execution ownership
```

Named StartRender, EndRender, final submission, Present, cleanup, and EndFrame responsibilities remain visible even when their engine wrappers use specialized execution paths. The terminalizer supplies finally semantics and runs exactly once on every outcome.

Failure handling preserves the first failure while continuing mandatory cleanup:

| Failure point | Required disposition |
| --- | --- |
| Before output ownership transfer | Caller retains or abandons the acquisition. |
| After transfer, before native submission | Cancel work, discard untouched scopes, `Finish(Aborted)` when a Ready generation exists, abandon output, do not commit cameras. |
| After partial submission | Join owners, preserve Submitted receipts and fences, discard only untouched scopes, `Finish(Aborted)`, recover the touched output without false abandonment. |
| Device loss or fence-signal ambiguity | Report unknown submitted scope state where required, `Finish(DeviceLost)`, invalidate output/backend epoch, never Present. |
| `Finish(Completed)` fails | Do not commit cameras or Present; execute terminal recovery. |
| Present fails after allocator completion | Keep allocator completion and camera commit; classify retryable output loss versus DeviceLost; never abandon an already submitted acquisition. |
| Cleanup fails | Preserve prior resource/output dispositions, record the first failure, and finish all mandatory joins. |

`RenderCommandSystem` remains the outer CPU render chain. Graph work uses `RenderFrameContext::GetBuilder()` and extends the RenderPath tail; it does not create a second renderer scheduler. Current dispatcher fields named as completed-frame statistics must either become callback/dispatch-completion statistics or move to the true graph terminal sink before they are presented as frame completion.

### Shutdown order

```text
stop accepting new frames
  -> stop new viewport acquisitions
  -> flush the RenderPath CPU tail without holding viewport/cache locks
  -> consume terminal failures
  -> verify every output transaction and prepared family is terminal
  -> release viewport output resources
  -> shut down RenderCommandSystem
  -> clear RenderGraphDefinitionCache lookup ownership
  -> clear RenderFlowResourceAllocator persistent caches and retire native resources
  -> shut down allocator and RHI
  -> destroy camera and scene owners in their established order
```

Device-loss shutdown follows the same logical ownership closure without demanding successful GPU waits. No lock may be held while waiting for the render tail.

### Baseline boundary and deferred work

The production baseline includes RED-style nodes and groups, multi-view composition, the four-entry definition cache, declaration-before-execution, deterministic allocator preparation, Graphics execution, the existing bounded Graphics/Compute model, truthful receipts, current-frame output, and full failure/shutdown terminalization. Post-build resource-root culling is not part of this baseline.

The following are explicitly post-baseline and do not justify weakening the baseline:

- general external producer-fence/consumer-queue waits and unrestricted Copy crossings;
- automatic cross-group command-scope merging;
- a second persistent per-view definition cache;
- generic asynchronous readback and export-result delivery;
- range-precision optimizations beyond correctness-required subresource tracking;
- multi-GPU scheduling and richer profiling/capture UI.

Unsupported behavior fails closed. Dedicated resource reuse and placed aliasing remain allocator decisions and are already usable where their compiled lifetimes and queue ordering are legal.

### R9 exit gate

R9 is complete when this authority section, the regenerated execution plan, the authoring examples, the study status, and the resume checkpoint agree on vocabulary, ownership, ordering, output timing, cache behavior, and implementation order. R9 authorizes starting Stage RG1 of the regenerated plan; it does not claim that production Render Graph code already exists.

## 52. Current Direct-Graph Implementation Contract

This section supersedes every conflicting implementation conclusion above. Vanguard follows RED's graph representation directly and changes only the seams required by the existing allocator, RHI, Shipping-active validation, and retained frame ownership.

### Graph and cache ownership

`FrameRenderer` owns one bounded `RenderGraphCache`, initially four fully associative entries. `GetGraph` performs RED-shaped linear hit lookup and least-recently-used replacement using the accepted frame serial, with full comparable keys after hash acceleration. Each entry directly owns its built composed `RenderNodeGraph`, reusable per-view graph storage, and the external `NodesContainer` objects that own every implementation referenced by those graphs. There is no `RenderGraphDefinition`, `RenderGraphDefinitionRef`, graph builder, private candidate publication protocol, definition generation, survivor overlay, or second graph compiler.

A miss selects and resets one entry, reports `needsRebuild`, and keeps it invalid until graph construction and `BuildRenderFlowGroups` succeed. The serialized RenderPath tail and graph exclusive-update ownership prevent entry reset while jobs use it. `PostBuildClear` may discard temporary topology after composition, but it must retain the node containers referenced by the final graph. Cached node implementations contain stable configuration only.

### Nodes, groups, and execution occurrences

`RenderNodeGraph` is both the composed topology and the executable graph. It preserves RED's node IDs, dependency arrays, CPU/GPU dependency domains, ordinary groups, sequence composition, unique-node merge, view-index override, command-list groups, and ordered command-list children. One graph item is one execution occurrence even when multiple occurrences share one implementation pointer. The retained frame owns mutable per-occurrence state and keeps the selected cache entry unavailable for rebuild until terminal join.

`RenderNodeImpl::Process()` remains the single orchestration boundary. In declaration mode it calls the concrete node's separate `DeclareResources()` callback; in execution mode RG4 adds RED's command-list, profiling, `Execute()`, and epilogue path. Concrete execution code never branches on allocator phase and resource discovery never runs GPU work.

### Resource allocator seam

`RenderNodeGraph::PrepareResourcesParallel` walks every real top-level occurrence using RED's deterministic bucket traversal. One command-list owner receives one generation-checked `ResourcePlanningWriter`; ordered children of a command-list group share it. The frame-owned `RenderNodeResourceBindings` table stores the logical/use/decision IDs for each occurrence and child range. The renderer then seals and resolves `RenderFlowResourceAllocator` directly. The graph supplies no survivor overlay or public queue schedule and never owns allocator Resolve.

Ordinary nodes use only the typed RED-shaped `RenderNodeImplContext` facade: `RTNameTag`, `RTSharedNameTag`, `RTAlloc`, `RTSharedAlloc`, `RTTempAlloc`, `RTUseBegin`, `RTUseEnd`, `RTSwap`, `RTDecision`, `RTTexture`, and `RTBuffer`. Raw allocator and writer access is private. Command-list groups privately register queue begin/end and `RenderNodeSynchronize` privately registers explicit queue synchronization. RED-style cross-node use intervals lower to a named allocation-lifetime scope plus packet-local resolved access, preserving Vanguard's packet ownership and liveness checks.

### Retained frame, submission, and output

The command system's stable retained frame owns prepared camera/custom data, the selected cache-entry execution lease, occurrence bindings, command-scope records, command lists, receipts, first failure, terminal continuation, and the move-only current-frame output transaction. The allocator owns its active generation. Node root branches retain the frame; child jobs borrow it under joined branch lifetime. No job retains the stack `RenderFrameContext`.

Queue and command-scope order must become executable RHI waits/signals and truthful receipts. Unsupported crossings fail closed. Presentation remains a named node in the same graph execution. Main-thread acquisition and viewport mutation serialize through the per-viewport operation gate; worker presentation consumes the exact acquired output after final submission and successful allocator terminalization, not at the next frame boundary.

### Current implementation boundary

RG1-RG2 ported graph representation, ownership, factory, groups, composition, node context, `Process()`, and parallel declaration traversal. RG3A-RG3D made allocator lifecycle and queue scheduling direct. RG3E closed the RED-shaped typed node-resource facade and removed raw allocator/writer access from ordinary nodes. RG3F is complete at its source-only correction gate: RHI policy/seeding, automatic-reset entry compilation, explicit dedicated/imported/placed texture and buffer state, and RED fork/join Graphics/Compute state continuity are implemented. Command recording, submission, cache integration, presentation, and service lifecycle remain owned by RG4-RG6 and must not be pulled forward opportunistically.

### RG3F: command-list entry and exit state

Source inspection found that `CommonBackend` previously set NVRHI `keepInitialState = true` unconditionally. NVRHI D3D12 `CommandList::close()` calls `keepBufferInitialStates()` and `keepTextureInitialStates()` even with automatic barriers disabled. Those functions emit transitions back to the descriptor's initial state. A logical use's final state therefore is not automatically the next command list's entry state, nor necessarily the submitted export's terminal state. Seeding alone cannot repair this mismatch.

`rhi::TextureDesc` and `rhi::BufferDesc` now expose `keepInitialState`, defaulting to true to preserve existing callers. False selects caller-owned state across recording scopes. Initial-data uploads explicitly restore their creation state in that mode. Explicitly tracked buffers currently require DeviceLocal memory and Common creation state; upload/readback heap state is not overridden. Dedicated-pool compatibility, placed-object compatibility, and descriptor identity include this policy.

`rhi::SeedCommandListStates()` accepts a complete table sorted by native RHI resource identity, then texture slice/mip. Each listed texture has one entry per subresource; each buffer has one whole-buffer entry. The NVRHI backend validates the entire table before tracker changes, retains its resources, then calls `beginTrackingTextureState` / `beginTrackingBufferState`. It rejects unknown states, incomplete or duplicate cells, stale identities, late/repeated seeding, and seeds inconsistent with automatic close-time restoration. This is tracking metadata only, not a barrier, queue wait, ownership transfer, or proof of GPU completion. An unsupported backend fails explicitly.

The next allocator slice now compiles those tables after physical assignment for the existing automatic-reset policy. Each packet owns immutable RHI entries, including all mip/slice cells of its textures and resources referenced by terminal/alias actions. `maximumCommandScopeEntryStates` bounds the aggregate generation metadata. A temporary physical-state table replays actions in actual packet step order and assigns exact transition before-states, including logical allocations reusing one native object within a command scope. Alias activation's native discard and predecessor Common finalization are included. Replay rejects an unrepresentable heterogeneous transition or a whole-resource UAV barrier that would silently alter other subresources. The scratch state table is discarded before publication.

`CompiledExecutionPacketView::OpenCursor` validates the scope and bound queue, seeds the fresh recorder, and only then exposes execution. Seeding failure fails the packet before node work; the coordinator must discard that command list. Logical-only validation does not invoke the native seeding path. No worker discovers entry state from another worker's recording progress.

The automatic-reset path retains `keepInitialState = true`. Initial and close states must be legal on the scope queue. Device-local buffers are currently limited to Common initial/close state so submission-boundary decay does not invalidate the metadata; upload/readback and non-Common buffer contracts remain unsupported in this path. Automatically reset imports must agree with the native initial/close state, and an automatically reset export requesting a different terminal state fails instead of publishing state that NVRHI will overwrite. Alias predecessors require Common close state so closing cannot undo finalization after alias activation. These restrictions are source-visible incomplete RG3F contracts, not removed parity targets.

The explicit-texture slice accepts `TextureDesc::keepInitialState = false` for dedicated textures and retained imports, plus allocator-selected placed textures under the narrower single-queue Common profile described below. Resolve carries each physical texture's mip/slice states across packets in deterministic RED flow order, rather than resetting the second scope to the creation state. Physical identity, not logical name, owns that history. Existing descriptor defaults remain true: this is an opt-in path, not a silent change to existing resources.

Resolve appends immutable exit actions to the last recording scope. `FinalizePacket()` executes them after every use ends and before marking the packet complete. Non-exported dedicated textures return to their creation state for the next pool assignment; imported textures reach their declared terminal state; exports retain their requested terminal state without NVRHI restoring it on close. Exit actions share the existing compiled-action budget and execution failure policy. Scratch physical history is destroyed after publication.

On Aborted completion, explicit dedicated resources never become reusable merely because fences complete. They remain retained until the submitted fence set completes, then enter the existing observation-before-native-release quarantine path. Native-byte charges remain until release observation completes. This deliberately also evicts an entirely discarded explicit resource; distinguishing untouched resources is a later optimization. Device-loss recovery retains its existing dedicated/placed teardown. Aborted imports do not promise their successful terminal state, and abort does not publish exports.

Each explicit consumer records its previous physical-state-producing packets. Terminal receipt validation requires every Submitted consumer's producers to be Complete and Submitted. Same-queue predecessors require a non-later fence; cross-queue predecessors require the matching compiled RED `ForkAsyncCompute` or `JoinAsyncCompute` dependency and its Submitted execution receipt. This validation is not a GPU scheduling primitive: RG4/RG5 must obey RED graph submission order, lower the same fork/join mode through the RHI, and suppress dependent submission if a producer or synchronization submission fails. Equal same-queue fence values are valid when ordered lists share one submission. Device loss may terminate with unknown completion.

The explicit-buffer slice accepts `BufferDesc::keepInitialState = false` for dedicated buffers and retained imports, plus allocator-selected placed buffers under the same single-queue Common profile. It is restricted to DeviceLocal and Common creation state, but Graphics and Compute scopes are both supported. Every buffer scope starts and explicitly finishes in Common. Resolve emits a budgeted `BufferTransition` exit action only when needed; `FinalizePacket()` runs it through the existing failure path. This deliberately avoids assuming that command-list boundaries are submission boundaries: [D3D12 buffer decay occurs after an ExecuteCommandLists operation, not between lists submitted together](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12). Normalization works for either arrangement, at the cost of explicit scope-end transitions; batching-aware barrier optimization is deferred. Buffer imports require Common entry/terminal state and exports require Common terminal state; either continuation queue may be Graphics or Compute. Physical predecessor receipts and conservative abort destruction apply to explicit buffers too. State normalization does not replace data dependencies or submission ordering. Upload/readback and non-Common external buffer contracts remain unsupported.

The explicit placed-resource slice is allocator-selected; node authors still declare ordinary logical resources and never supply heaps or offsets. A transient explicit resource is eligible for the existing placed planner only when its complete lifetime is on one authored Graphics or Compute queue and its creation/reuse state is Common. Unsupported explicit placement shapes, including one logical allocation crossing queues, fall back to the dedicated provider. A render-target/depth texture recorded on Compute also falls back unless it supports unordered access, because Compute alias activation needs a UAV-capable discard state. Alias predecessors are finalized to Common at their terminal `UseEnd`; successor activation consumes the existing exact predecessor-fragment set. The entry-state compiler models both actions and links a later packet to the prior state-producing packet. Explicit placed textures return to Common in their final packet, and explicit placed buffers already normalize every scope to Common, so a successfully completed batch has a truthful cache-reuse state.

Placed retirement is heap-granular. Successful generations retire the batch behind submitted fences and may reuse it afterward. If an aborted generation contains any explicit placed object, the entire placed batch waits for the submitted fence set and then takes the observation-before-release path; it is never returned to the cache with uncertain state. All child objects are released before their heap, and the native-byte ledger remains charged until heap-release observation completes. Failure to acquire the observation remains quarantined and is retried by polling. Resolve rollback before publication remains reusable because no command recording or GPU execution occurred.

The Graphics/Compute slice follows RED's authored synchronization exactly. `ForkAsyncCompute` and `JoinAsyncCompute` requests remain the sole cross-queue happens-before authority; the allocator does not infer handoffs from resource access. A carried state already legal on the consumer queue is seeded unchanged. When a Graphics producer leaves an explicit resource in a Graphics-only state, Resolve appends a transition to Common to that producer's exit actions before the authored fork, then seeds Common into the Compute consumer. Compute-to-Graphics carries the exact Compute exit state, matching RED's explicit UAV-state continuation cases. Cross-queue Copy and multi-queue placed alias planning remain unsupported. Review the baseline allocator policy before opting callers into explicit tracking automatically. No ordinary-node API or Declare/Execute separation changed.

These slices are source-inspected only. RG3F is closed at its bounded source gate and does not yet establish a runtime-verified graph-to-RHI execution path. The correction pass consolidated queue-direction policy, rejected unsupported Compute alias-discard placement before planning, and verified that production rendering `.cpp`/`.hpp` comments contain no RED-specific wording. Compilation and tests remain deferred by user instruction.

### RG7B reevaluation: failure boundaries and authoring integrity

The recording-cancellation policy in this historical correction record is superseded by the fail-fast decision below. The unrelated ownership, presentation, and authoring fixes remain in effect.

The two correction passes recorded on 2026-09-06 supersede earlier blanket claims about asynchronous failure safety. They retain RED's graph, `Process()`, group-child orchestration, authored synchronization nodes, external node containers, and terminal chain. They do not introduce a second scheduler or a graph-owned allocator interface.

Recording failure is visible before dependent execution or submission proceeds, not only when the terminal helper scans node entries. The existing frame command-list submission-failure latch also receives recording and scheduling failures. `Process()` checks it and the occurrence failure after resource-use entry actions, before concrete `Execute()`. Epilogues publish failures and cancel incomplete packets. Submission checks the latch before native submission; the joined terminal path discards remaining work and retains truthful receipts for work already submitted. An unrelated failure cannot retroactively cancel an in-flight native submission, and device loss still requires device recovery. Normal boundary checks are atomic reads; first-failure publication takes the existing lock and copies bounded diagnostic text.

Deferred epilogue allocation precedes any child dispatch that borrows occurrence state. The independent node-job branch is joined directly into the frame builder through the imported native counter operations, without allocating a public Counter wrapper or synchronously waiting on the current continuation. The Jobs adapter consumes only a distinct independent builder with closed fence groups. A dispatch-accepted frame remains accepted if public tail-counter allocation fails and the independent root builder must instead be joined synchronously. Failure text crossing a callback boundary must be retained before its originating stack object dies.

Viewport serialization is provided by the broad render-command tail, not a per-slot operation lock. The exact output acquisition remains owned by terminal work through native Present, and its atomic owned bit rejects duplicate acquisition without serializing unrelated viewports. Main-thread swap-chain and lifecycle mutation joins the tail before changing the stable manager-owned viewport. Present success consumes the acquisition; failure retains it for the existing terminal recovery path. Same-frame worker presentation remains unchanged.

Graph construction fails fast on invalid factory ownership, failed node/group allocation, invalid dependencies, or exceeded graph capacity. Each dependency domain is cycle-checked before recursive flow-level assignment. The original declaration traversal keeps stride 1021 when coprime to node count and otherwise chooses a coprime stride; it must visit every occurrence once rather than relying on a debug-only node-count limit. These are build-time integrity corrections, not new authored graph semantics. Unused validators/bucket metadata and unnecessary Jobs builders were removed.

Both passes are source-complete only. Build, runtime, allocation-failure, presentation-concurrency, and GPU matrix verification remain explicitly deferred; the execution plan lists the required regressions. Renderer feature-node bodies remain separate work.

### Current recording policy: fail-fast, without shared node polling

The user approved replacing recoverable recording-error propagation with Shipping-active fatal errors. `Process()` performs required command-list/packet setup, calls `BeginNewNode()`, then calls `Execute()` directly. `CanExecute()` and the shared recording-failure publisher are removed. Failed required setup, resource begin/end actions, packet finalization, resource/decision lookup, and execution-side `FailResourceOperation()` terminate instead of publishing an error and permitting later work. Scheduling failures after node dispatch has begun also terminate, including child-batch and epilogue allocation/dispatch failures. This is not log-and-continue behavior.

Planning and frame-setup failures remain recoverable before kickoff. Each occurrence checks its already-published setup status once before entering recording and skips if setup blocked it; this is ordinary job-ordered local state, not shared atomic polling. The low-level allocator still exposes its existing failure-returning API for other clients. Only the graph execution boundary applies the fatal policy.

Ordinary required submission failures are fatal. Native device loss or a submission whose execution may already have happened retains the existing receipt/recovery path; later ordered submissions skip using the submission-owned flag. That flag no longer receives concurrent recording writes and is a plain boolean; diagnostic readers retain the existing lock. Already-submitted fences, terminal receipts, output ownership, and joined cleanup remain unchanged. Failure while recording after device loss may itself terminate, so this does not promise complete device recovery.

No allocator locks, atomic primitives, resource witness ownership, or Jobs counters were optimized in this change. Those remain the separate lock/performance work. Source inspection only; no compilation or tests were run.
