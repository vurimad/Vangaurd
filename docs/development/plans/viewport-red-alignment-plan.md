# Viewport Direct-Ownership And Synchronization Alignment Plan

Date: 2026-09-06

Status: RG7C.2A through RG7C.2E complete

## Goal

Remove the viewport and presentation snapshot model and use stable, directly referenced viewport objects under the same broad frame-ordering contract as the reference renderer. Presentation remains a terminal node of the current Render Graph and is not deferred to the next frame. Vanguard's exact acquired-backbuffer transaction, generation validation at external handles, and NVRHI submission receipts remain required seams.

This plan is bounded to viewport, presentation, frame ownership, and their command-tail ordering. It does not authorize replacing unrelated camera, scene, ECS, input, or residency observation APIs merely because they also use the word `Snapshot`.

## Evidence And Current Difference

The reference renderer stores owned `RenderViewport*` objects, retains `IViewport*` in frame information, and calls `RenderViewport::Present()` directly from its graph Present node. `CRenderCommandHandler::RenderScene()` orders each frame after `m_flushCounter`. Viewport property application calls `GetRenderer()->Flush()` before resize, fullscreen, output, HDR, or swap-chain mutation.

Vanguard already has the first half of that ordering: `RenderCommandSystem` owns a serialized CPU tail, `RenderingService::RenderUpdate()` joins the previous tail, and `FrameRenderer` performs Present in the graph terminal chain. It diverges after that point:

- `ViewportManager` owns fixed slots whose primary state object is named `snapshot`.
- `RenderViewport` and `EngineViewport` are copied manager/handle facades rather than the owned objects.
- `PresentationService::Tick()` may reconcile while worker Present is active and handles this by returning `Busy`.
- Before this alignment, `Present()` dropped a per-slot lock around native Present and used `presentInProgress` to keep the slot alive.
- Callers read copied `RenderViewportSnapshot` and `EngineViewportSnapshot` values before invoking separate manager operations.

Consequently, the snapshot API cannot be deleted first. Direct references would be able to outlive the lock, observe concurrent mutation, or refer to a recycled slot. The ordering and ownership changes below remove those causes before deleting the snapshots.

## Permanent Vanguard Seams

The following differences are intentional and must survive the alignment:

1. `RenderOutputAcquisition` and `RenderFrameOutputTransaction` retain the exact acquired NVRHI back buffer until terminal Present, completion, abandonment, or device loss.
2. External viewport handles remain generation checked. They may resolve to a live object, but a stale handle must never resolve after destruction or slot reuse.
3. Allocator terminal receipts must prove that the Present transition command scope was submitted before native Present.
4. Texture and Headless outputs retain their distinct terminal paths.
5. Same-frame Present remains part of the Render Graph.

These seams affect resource and lifetime evidence, not the ordinary viewport authoring model.

## RG7C.2A - Owned Viewport Objects

- Replace `RenderSlot::snapshot` and `EngineSlot::snapshot` with actual manager-owned `RenderViewport` and `EngineViewport` objects.
- Move live state and behavior into those objects instead of keeping facade copies that call back through a manager and handle.
- Give each object a stable address for its complete active lifetime. A slot may be reused only after destruction has joined all frame work that retained the previous object.
- Preserve generation handles at service and external API boundaries. `ViewportManager::Resolve()` returns a pointer/reference to the live owned object rather than copying a facade.
- Make destruction invalidate the handle and object only after the command tail is joined and no output transaction is retained.
- Store a directly retained `RenderViewport*` in `RenderFrameInfo`, matching the reference frame-to-viewport relationship. Keep the existing exact output transaction beside it.

Exit gate: a submitted frame and every node job can hold the same stable viewport object until terminal cleanup; destruction and slot reuse cannot overlap that lifetime.

Implemented 2026-09-06: render and engine slots now contain the canonical `RenderViewport` and `EngineViewport` objects rather than snapshot state. The objects are non-copyable, have stable slot addresses, own their live fields, and are invalidated only after the existing command-tail/output-ownership destruction checks pass. Generation-checked `Resolve()` now returns the owned object pointer. `RenderFrameInfo` retains that exact output pointer beside the external handle, and the retained asynchronous frame plus node contexts preserve it through terminal completion. The temporary compatibility structures and manager forwarding retained at this intermediate point were subsequently removed by RG7C.2D. Focused `renderingTests` builds and executions pass in Debug and Shipping, including direct identity proof inside asynchronous frame execution.

## RG7C.2B - Reference-Style Mutation Boundary

- Establish one explicit main-thread viewport update phase after the previous render-command tail is joined and before new frame construction/submission.
- Integrate presentation/window reconciliation into that boundary instead of letting it probe an active worker Present and return `Busy` as normal control flow.
- Separate requested window properties from applied renderer properties. Requests may accumulate; applying resize, surface replacement, swap-chain recreation, binding, unbinding, or destruction first joins the render tail, just as the reference viewport flushes before mutation.
- Preserve no-op updates without a flush when requested and applied state already agree.
- Ensure frame construction occurs only after reconciliation and captures the live viewport's applied extents, output kind, and presentation policy.
- Keep Present on the worker graph terminal. The next main-thread mutation boundary joins that terminal work before changing the viewport; it does not perform or defer Present itself.

Exit gate: no viewport or swap-chain mutation can overlap a retained frame or worker Present, and `Busy` is no longer the ordinary resize/reconciliation path.

Implemented 2026-09-06: applied viewport mutation now follows the render-command tail boundary. `UpdatePresentation()` and `RequestRenderExtent()` first detect unchanged state and return without flushing. A real presentation, extent, swap-chain bind/unbind, or viewport-lifecycle mutation joins the previous CPU render tail before changing the manager-owned viewport object. Mutation is rejected after any `BeginFrame()` until every building frame is submitted or abandoned, so reconciliation precedes frame construction and the next frame captures the applied state. `PresentationService::Tick()` no longer treats `Busy` as its expected resize retry path; a changed window request drains the prior graph, including terminal Present, before it is applied. Same-frame worker Present and the exact acquired-output transaction remain unchanged; the compatibility readouts and per-slot locks retained at this intermediate point were subsequently removed by RG7C.2D and RG7C.2E. Focused `renderingTests` builds and executions pass in Debug and Shipping, including proof that an unchanged request does not flush, a changed request does flush, mutation cannot cross the frame-construction boundary, and the next frame observes the new extent. The native `rhiNvrhiTests` presentation and deferred-lifetime suites also pass in Debug and Shipping on the available D3D12 adapter; that gate additionally caught and corrected minimized-window suspension being omitted from the no-op comparison.

## RG7C.2C - Direct Render-Path Access

- Change render-frame and node-facing APIs from viewport handles plus copied fields to the retained `RenderViewport*` used by the reference renderer.
- Put read-only live getters on `RenderViewport` for applied width, height, visibility, output kind, swap chain, and texture output.
- Keep mutation methods on the live object and restrict them to the main-thread update boundary.
- Make the graph Present node call the retained viewport's terminal Present path directly. That path still consumes the exact `RenderOutputAcquisition` and validates allocator submission evidence.
- Remove redundant copies of live viewport state from `RenderFrameInfo`; retain only genuinely immutable per-frame choices such as mode, purpose, feature policy, and the accepted frame serial.
- Keep frame-owned extents only where they intentionally freeze rendering dimensions for the accepted frame rather than pretending to be live viewport state.

Exit gate: ordinary graph code follows `frame -> viewport -> operation` directly, without manager snapshot lookup or a second state representation.

Implemented 2026-09-06: `RenderFrameInfo`, `RenderFrameContext`, and `RenderNodeImplContext` now expose the same retained manager-owned `RenderViewport*` directly. The frame no longer duplicates the render-viewport handle or output kind; accepted render and output extents remain frame-owned because they freeze the dimensions used to build that frame's graph and resources. `RenderViewport` provides direct read-only access to its applied extents, dimensions, visibility, occlusion, output kind, swap chain, and texture output. The output transaction now retains the viewport object instead of a manager pointer and copied kind. Terminal abandonment, texture completion, device-loss consumption, and same-frame Present all enter through that retained viewport while preserving the exact acquired-output token and allocator terminal ordering. Focused `renderingTests` and native D3D12 `rhiNvrhiTests` build and execute successfully in Debug and Shipping. The compatibility APIs and per-slot synchronization retained at this intermediate point were subsequently removed by RG7C.2D and RG7C.2E.

## RG7C.2D - Remove Snapshot APIs And Flatten Presentation

- Delete `RenderViewportSnapshot`, `EngineViewportSnapshot`, their `GetSnapshot()` overloads, facade forwarding methods, and snapshot visitor callback types.
- Replace viewport enumeration with direct `RenderViewport&` / `EngineViewport&` visitors whose lifetime is bounded by the serialized main-thread update phase.
- Replace `PresentationService`'s before/current snapshot comparisons with direct applied-revision counters returned by the live viewport update operation.
- Replace `UpdatePresentation(const PresentationAttachmentSnapshot&)` with direct presentation/window ownership or a narrow applied-property request. Do not copy swap-chain or texture handles through public observation structures.
- Remove `PresentationOutputSnapshot` if the service remains; expose the owned output object or narrow getters instead. If the service adds no ownership beyond the viewport/window relationship after flattening, fold it into viewport update ownership rather than retaining a parallel state mirror.
- Update tests and public callers to use live objects and explicit operations.

Exit gate: no viewport or presentation API, storage field, visitor, test, or document depends on a copied snapshot structure. Window snapshot removal is limited to the presentation seam unless a separate window-system pass authorizes broader change.

Implemented 2026-09-06: `RenderViewportSnapshot`, `EngineViewportSnapshot`, `PresentationOutputSnapshot`, their query methods, facade forwarding, and snapshot visitor types are removed. Viewport enumeration now visits the stable manager-owned objects directly. Presentation reconciliation sends a narrow property update to the live viewport and receives explicit resize/surface-replacement results instead of comparing copied before/after state. `PresentationService` owns stable `PresentationOutput` objects with narrow getters and retains the resolved `RenderViewport*`; it no longer mirrors viewport or swap-chain handles through a public observation structure. The window attachment snapshot remains confined to the WindowManager-to-presentation boundary as authorized. Focused `renderingTests` and native D3D12 `rhiNvrhiTests` build and execute successfully in Debug and Shipping.

## RG7C.2E - Remove Redundant Synchronization And Close

- Remove `RenderSlot::operationLock` and `presentInProgress` after the frame-tail and stable-object invariants make them redundant.
- Retain synchronization only where another real producer remains. `RenderOutputAcquisition` ownership checks and command-tail counters are not redundant locks.
- Audit every viewport read/write site for its permitted phase and thread. Fail fast on lifecycle misuse rather than silently reading stale data.
- Exercise same-frame Present, queued resize during Present, surface replacement, create/destroy, slot reuse, Texture and Headless output, failed Present, device loss, shutdown in flight, and multiple viewports.
- Prove that a queued resize is applied after the prior Present and before the next frame captures dimensions.
- Run the complete seven-target Debug and Shipping matrix and the real D3D12/NVRHI presentation suite.
- Remove superseded snapshot/concurrency wording from the Render Graph design, execution plan, checkpoint, and authoring examples.

Exit gate: viewport synchronization is dominated by the same broad command-tail ordering as the reference renderer, the render path holds direct stable viewport objects, and snapshot state plus its compensating locks are gone.

Implemented 2026-09-06: the per-slot operation spinlock and `presentInProgress` state are removed. Main-thread lifecycle and applied-property mutations still join the serialized render-command tail before changing a viewport; retained terminal work remains the sole owner of its move-only output acquisition until Present, completion, abandonment, or device-loss disposition consumes it. The output-owned bit is retained as the one required atomic ownership latch, not an operation lock, so duplicate acquisition fails safely without serializing unrelated viewport work. Unchanged presentation and render-extent requests still avoid a tail flush, and suspension comparison uses main-thread-owned requested state rather than worker-written terminal state. The complete seven-target matrix builds and runs successfully in Debug and Shipping; native `rhiNvrhiTests` exercised the real D3D12 adapter.

## Required Order

The passes are intentionally ordered:

```text
Stable owned objects
    -> serialized mutation boundary
    -> direct frame/node access
    -> snapshot deletion
    -> lock deletion and validation
```

Deleting snapshots or locks before the first three passes would create dangling references or data races. Adding a second scheduler, deferring Present to the next frame, or replacing the exact output transaction is outside this plan.
