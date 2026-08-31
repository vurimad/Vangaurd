# Vanguard Rendering Foundation

The `rendering` module will connect cooked rendering formats to the format-independent RHI. It does not expose NVRHI and does not make the RHI depend on `.vshader` or any other asset schema.

Vanguard's production renderer is bindless-first. Shader reflection remains representation-neutral, while renderer policy will compile it into pipeline interfaces, global descriptor domains and compact GPU-visible records. Materials and draw submissions will not construct immutable per-material descriptor packs or execute a parallel fixed-binding path.

Fixed binding may be introduced later as an isolated policy for constrained platforms. It must use the same logical material and shader interfaces and must not alter cooked asset schemas or the bindless runtime path.

## Pipeline ownership

`rendering::PipelineCache` is the renderer-facing asynchronous pipeline entry point. It deep-copies pipeline creation
descriptions, retains every referenced shader, binding layout and global descriptor domain for the duration of the redJobs creation task, and
coalesces requests by the concrete SHA-256 key produced by `vpipeline`. Successful requests expose a borrowed
`rhi::PipelineRef` while the request remains alive. Invalidating an entry retires the underlying RHI pipeline only after
the final request releases its generation.

The cache must be shut down before RHI shutdown. Backend-native disk persistence remains a separate private adapter;
neither native cache blobs nor NVRHI objects enter this module's public API.

## Viewports and frame submission

`ViewportManager` separates an engine-facing view from its rendering destination. An `EngineViewportHandle` owns the
`BeginFrame`, frame-packet population, `SubmitFrame`, and `FlushFrame` contract. A `RenderViewportHandle` describes
where that frame is rendered: a presentation attachment, an externally owned texture, or a headless output.

`RenderCommandSystem` owns one Jobs `RenderPath` CPU tail shared by scene `FrameTick` work and viewport `RenderFrame`
submission. Every submitted frame packet is retained before dispatch, and its optional retain/release payload keeps
renderer-owned scene and camera state alive until execution finishes. `FlushPreviousFrameProcessing` is the explicit
RED-style CPU barrier used by viewport destruction and exceptional ownership transitions; it never waits for GPU queue
completion. GPU safety remains an RHI-fence contract.

`RenderingService` owns the command system and viewport manager. `WorldRenderBridge` and future editor/tool bridges are
producers of scene mutations, not hard-coded stages of this generic rendering service. A retained `RenderFrame` now enters
the service with a Jobs continuation builder, allowing future Render Graph work to extend the same CPU command tail. Until
that graph is installed the destination fails explicitly instead of reporting an unrendered frame as successful. Automatic
engine-frame ordering, camera/frame publication, and Render Graph execution remain the next integration layer.

Native windows remain owned by the window module. Presentation viewports retain a generation-safe
`PresentationAttachmentHandle`; the presentation service creates the platform swapchain and binds its RHI reference
to the render viewport. Consequently neither SDL objects nor `WindowManager` enter the RHI.

`RenderViewport` keeps requested presentation state separate from successfully applied swapchain state. Pixel-extent
and native-surface revisions are tracked independently: an extent revision resizes an idle compatible swapchain,
whereas a surface revision retires that swapchain and returns the viewport to `AwaitingOutput`. Only applied revisions
are exposed for acknowledgement to the window module, so suspended, busy, failed, or not-yet-created presentation
work is never acknowledged prematurely.

Output access is explicit. `AcquireOutput` produces a `RenderOutputAcquisition` tied to the viewport's current output
revision. Texture outputs return their stable target, while presentation outputs contain the exact RHI back-buffer
acquisition required by command recording and presentation. The caller must pass that acquisition to `Present` or
`AbandonOutput`; resize, surface replacement, unbinding, and destruction reject an outstanding presentation image.

## Presentation service

`PresentationService` owns the complete window-output transaction. Creating an output attaches presentation ownership
to a `WindowHandle`, creates its presentation `RenderViewport`, and records an explicit swap-chain policy. Its main-thread
reconciliation tick propagates window revisions, resolves the backend-owned native surface, creates or replaces the RHI
swap chain, applies pixel-size changes, and acknowledges only revisions that the viewport applied successfully.

Minimized or hidden outputs enter `Suspended` without destroying a compatible swap chain. Native-surface revisions retire
the old chain before creating its replacement, using the RHI's explicit maintenance barrier because a native window may
not accept a replacement while its prior presentation object remains alive. This barrier is restricted to rare native
ownership changes and never enters normal frame presentation. Output destruction is ordered: engine viewports, acquired
images, swap chain, render viewport, presentation attachment. Generation-checked handles and telemetry make stale editor
panels and partial failures observable.

Each output owns a color and latency policy. The service resolves SDR, HDR10 or scRGB from that policy and the live window
output state, recreates the chain when display color state changes, and records any permitted HDR-to-SDR fallback. Required
HDR modes fail explicitly. SDL-provided SDR white level and HDR headroom, resolved RHI output metadata, active swap-chain
format/color space, and pacing policy remain visible through the output snapshot without exposing native platform objects.

## RenderScene updates and queries

`RenderSceneManager` owns stable live proxies, typed payload pools, and the CPU spatial broad phase. Producers submit bounded
relink requests; `PrepareSceneUpdate` swaps the ingress queue and `ExecuteSceneUpdate` performs newest-request reduction,
parallel proxy-local updates, batched structural movement, and one spatial repair boundary through Jobs `RenderPath`.

Visibility queries consume the live spatial state only after that update dependency completes. A request names the exact
completed mutation epoch it expects, so stale or overlapping work fails explicitly without immutable CPU scene copies, read
leases, typed collector packets, or hidden synchronization. Cold proxy and payload snapshots remain available for editor
inspection, validation, recovery, and tests; they are not the runtime draw-data path.
