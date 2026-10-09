# Render-Flow Resource RED API Alignment

## Purpose

This is a bounded comparison of RED's render-flow allocator and node-facing resource API against Vanguard after RG3D. The objective is not to replace Vanguard's allocator internals. It is to remove avoidable API differences that would otherwise make every translated render node stitch the same seam by hand.

The useful gap is small. Alignment is limited to three passes before the remaining RG3 RHI work resumes.

## Current Mapping

| RED surface | Vanguard surface | Decision |
| --- | --- | --- |
| `SetPhase(RFP_Startup)` | `BeginFrame(frameSerial, policy)` | Keep Vanguard. The named operation owns validation and establishes one frame generation. |
| `SetPhase(RFP_PreConsume)` plus graph traversal | `PrepareResourcesParallel` plus per-occurrence `ResourcePlanningWriter` | Keep Vanguard's separate `DeclareResources()` callback, but hide writer mechanics behind a RED-shaped node-context facade. |
| `SetPhase(RFP_Consume, builder)` | `SealPlanning()` then direct `Resolve(builder)` | Keep Vanguard. Resolve is an explicit synchronization boundary rather than an implicit phase side effect. |
| `RequestBeginQueue`, `RequestEndQueue`, `RequestQueueSync` | Same allocator request names and meaning | Already aligned. Keep queue requests allocator-owned. |
| `SetProcessEviction(bool)` | `FrameResourcePolicy::processEviction` | Align per frame. Polling remains unconditional; only discretionary soft-target trimming is skipped. |
| `ClearAllCaches()` | `ClearPersistentCaches()` | Semantically aligned. Keep the more exact Vanguard name because externally published exports are not allocator cache entries. |
| `RequestAlloc`, `RTAlloc`, `RTTempAlloc`, `RTUseBegin`, `RTUseEnd`, `RTSwap`, `RTDecision` | RED-shaped typed calls on `RenderNodeImplContext` | Aligned through the context facade with Vanguard descriptors and Shipping-active failure reporting. |
| `RequestInjection` / `RTInject` | retained import registration plus writer import | Keep Vanguard. Import ownership, initial state, queue, and readiness must be established before parallel node declaration. |
| `GetTexture`, `GetBuffer`, `RTGet` | packet cursor plus `ResolvedTextureUse` / `ResolvedBufferUse` | Keep Vanguard lifetime enforcement, but make it available through the node-context facade so ordinary nodes do not manipulate packet cursors. |
| `RequestFree` / `RTFree` | no equivalent | Do not copy. RED's lifetime-splitting path is incomplete and no renderer call sites were found in the audit. Scoped use end and logical remapping cover the supported semantics. |
| `RequestIsAlloc` / `RTIsAlloc` | no equivalent | Do not copy. RED itself says this cannot be implemented safely with parallel command-list building, and no renderer call sites were found. Use captured decisions or explicit graph inputs. |
| `RequestSwapRef` | retained imports and terminal exports | Do not copy. External ownership transfer remains explicit and receipt-backed. |
| raw allocator access from every node | private allocator/writer pointers inside the context | Ordinary-node access is removed. Queue groups and Sync use private orchestration helpers. |

## The Critical Seam To Preserve

RED identifies resources through stable name tags and replays the same resource calls during PreConsume and Consume. Vanguard deliberately separates `DeclareResources()` from `Execute()` and assigns generation-local `LogicalResourceId` and `ResourceUseId` values while planning.

A cached node implementation can appear more than once in the composed graph, including once per camera. Generation-local IDs therefore must not be stored in the persistent node object. Doing so would make one camera overwrite another camera's bindings and would leave cached definitions holding stale frame identities.

The context facade must use frame-owned, per-occurrence resource bindings:

- declaration writes the IDs returned by the existing planning writer into that occurrence's binding record;
- execution reads the same occurrence's record and advances through its compiled packet;
- command-list-group children receive distinct ordered binding ranges inside the shared group packet;
- the retained frame owns the records until all declaration, execution, submission, and terminal jobs join;
- node code sees RED-shaped resource operations, not the binding table, writer, allocator generation, or packet cursor.

This is the one nontrivial adapter required by Vanguard's declaration/execution split. It belongs at the node-context boundary and does not require redesigning allocator resolution, physical pooling, aliasing, or receipts.

## Vanguard Contracts That Must Not Be Weakened

Only these architectural differences are intentional:

1. Resource declaration remains separate from node `Execute()`. RED's Prepare/Consume branch exists once in `RenderNodeImpl::Process()`, never in every concrete node.
2. Frame lifecycle remains `BeginFrame` -> declaration -> `SealPlanning` -> `Resolve` -> `BeginExecution` -> `Finish`, with generation and terminal evidence.
3. Resources remain typed as texture or buffer with explicit views, state, access, subresources, imports, exports, and queue readiness.
4. Execution resource access remains scoped. A raw RHI reference cannot be obtained from the allocator and cached beyond the compiled use interval.
5. Failures, capacities, ownership, device loss, and terminal receipts remain active contracts in Shipping.

Everything else at the node-authoring boundary should follow RED unless a translated call site demonstrates a real incompatibility.

## Alignment Passes

### RG3E.1 - Lifecycle And Policy Parity

Status: complete at the source-only gate.

- Add `FrameResourcePolicy::processEviction`, defaulting to `true`.
- Always poll dedicated and placed retirement queues at frame startup.
- Apply oldest-reusable soft-target trimming to both dedicated resources and placed heaps when eviction processing is enabled.
- Retain direct allocator lifecycle calls and the existing `ClearPersistentCaches` contract.

One allocator-wide projected texture/buffer total is carried through the dedicated and placed trim operations, so the two pools cannot each independently consume the full soft target. Placed trimming operates at whole-heap granularity. A reusable heap is eligible only when every live placed object on it is reusable and owned solely by the pool. Eviction resets those objects, observes native heap destruction, and keeps the heap bytes charged until that observation completes. This preserves Vanguard's byte-ledger and placed-object lifetime contracts while matching RED's decision to retain the working set on blank or offscreen game frames.

### RG3E.2 - RED-Shaped Node Resource Facade

Status: complete at the source-only gate.

- Compare representative RED nodes before freezing overloads.
- Add the familiar allocation, temporary allocation, scoped use, swap, and decision operations to `RenderNodeImplContext`.
- Preserve Vanguard's typed descriptors and explicit failure results beneath those calls.
- Add execution-side scoped texture/buffer resolution through the same context.
- Introduce the frame-owned per-occurrence binding record required to connect declaration to execution.
- Do not add `RTFree`, `RTIsAlloc`, raw `RTGet`, direct injection, or reference swapping merely for superficial parity.

The comparison found one important semantic detail in RED's real nodes: `RTUseBegin(name)` and `RTUseEnd(name)` may occur in different nodes, and a named resource may be opened again inside an already-open outer interval. Vanguard therefore records a RED-shaped named scope separately from the packet-local access needed by the node that records commands. Resolve matches named scope endpoints as an ordered stack on the currently mapped allocation, after logical swaps. The declaring node's packet-local uses are closed automatically in reverse order at the concrete `Process()` boundary, so every packet remains independently executable while the named scope extends allocation lifetime across nodes.

`RenderNodeImplContext` now provides `RTNameTag`, `RTSharedNameTag`, typed texture/buffer `RTAlloc`, `RTSharedAlloc`, `RTTempAlloc`, typed `RTUseBegin`, `RTUseEnd`, `RTSwap`, `RTDecision`, and scoped execution lookup through `RTTexture` / `RTBuffer`. Temporary declare-like allocation is supported directly instead of manufacturing a named logical slot. The context records use and decision IDs into `RenderNodeResources`; `RenderNodeResourceBindings` owns one such record per composed graph occurrence, and command-list-group children receive ordered subranges in their shared packet. The graph initializes and writes that external frame-owned table during parallel preparation. Cached node implementations receive none of these generation-local IDs.

Execution setup remains owned by the later RG4 `RenderNodeImpl::Process()` translation: the private context hooks accept the occurrence record and packet cursor, consume exact use/decision order, close resolved uses at the node boundary, and reject declaration/execution drift. This is the required execution seam, not an early translation of RG4 command recording.

### RG3E.3 - Surface Closure

Status: complete at the source-only gate.

- Translate a representative texture node, buffer node, conditional node, temporary-resource node, and command-list group against the facade.
- Move `GetResourceWriter()` and general `GetResourceAllocator()` out of the public node API once no ordinary node needs them.
- Keep queue Begin/End/Sync access private to graph/group orchestration.
- Remove duplicate adapters and update the provisional RenderFrame example.
- Perform source comparison only; compilation and tests remain deferred to the authorized integration gate.

The ordinary-node surface now exposes only the RED-shaped typed facade. `GetResourceAllocator()` and `GetResourceWriter()` are private context implementation details. The command-list group reaches private queue begin/end helpers, and the translated `RenderNodeSynchronize` reaches the private queue-sync helper; neither operation is available to ordinary node implementations. `SYNC_SUBMIT` therefore names a real node again, while its submission-side execution remains fail-closed for RG4.

The provisional authoring document now exercises the facade with texture, buffer, captured conditional, declare-like temporary, cross-node lifetime, and ordered command-list-group shapes. These are source-contract examples rather than dead renderer classes. The RenderFrame example uses the cache entry's built `RenderNodeGraph` directly, retains per-occurrence bindings and preparation failures in the frame, checks asynchronous preparation only from the ordered Resolve job, and seals planning before Resolve. The rejected `RenderGraphDefinition` resource indirection is absent.

## Non-Goals

Beyond RG3E.1's cache-policy correction, this alignment does not redesign the dedicated or placed pools, alias planner, native-byte ledger, descriptor validation, RHI state transitions, execution receipts, exports, or device-loss behavior. It also does not introduce RED's known unused or unsafe resource calls merely to make the header look identical.
