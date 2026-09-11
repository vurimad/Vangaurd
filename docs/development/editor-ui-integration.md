# Editor UI integration contract

Date: 2026-09-10.

Status: E0A through E0E are implemented. E0F now builds and runs both Debug and Shipping native editor lifecycle proofs: two hosts present, the detached output resizes and closes, and each run exits successfully after 120 frames. Debug captures show the font, registered image and clipped child content; Shipping exited before screenshot capture. Premake generation passes. Building the Shipping SDL3 prerequisite resolved its link failure. E0F is not fully closed: interactive docking/redocking and mixed-DPI/minimized atlas updates remain unverified. This is an integration proof, not another rendering architecture phase.

## 1. Accepted direction

The Vanguard editor uses Dear ImGui from its docking branch. The repository copy is pinned to revision `ca49eff3980443a97c470e09fe55b1740cfb9584`, identified by its source as `1.92.9 WIP`, and preserved under `external/imgui/upstream` with its MIT license and upstream record.

Docking and native multi-viewport support are foundation requirements. The first executable proof must include the primary editor window and a panel detached into a second native window. Native multi-viewport support must not be postponed until after the shell is built because window ownership, input routing, DPI, layout persistence, render-output creation and destruction all depend on it.

Dear ImGui is an editor implementation dependency, not an engine API. Engine, rendering, RHI, assets, projects, documents, commands, selection and transactions expose Vanguard-owned types. Direct ImGui use is allowed inside editor UI implementations and shared editor widgets; Vanguard must not create a one-for-one wrapper around every ImGui call or a universal toolkit abstraction.

## 2. Terms that must not be conflated

| Term | Meaning and owner |
| --- | --- |
| ImGui platform viewport | One native host window used by Dear ImGui when a panel leaves the primary OS window. Its association is owned by the editor platform adapter. |
| Editor host window | Vanguard record connecting one native window to its eventual presentation output, presentation `RenderViewport`, DPI/focus state and the frame's UI draw data. E0D may register the native window before E0E completes the paired presentation fields. |
| Editor scene viewport | A panel that presents a rendered scene image and drives editor interaction. It is not necessarily a native window and does not own a swap chain. |
| `EngineViewport` | Existing stable renderer-facing viewport construction object. The editor controller associates with it rather than replacing it. |
| `RenderViewport` | Existing renderer-owned output surface. Texture output serves a scene panel; presentation output serves a native host window or ordinary runtime window. |
| Render camera | Existing owner of camera state, camera custom data and temporal history. A panel may associate with a camera but does not clone those responsibilities. |

A native editor host can contain any number of panels and scene images:

```text
Editor host window -> presentation RenderViewport -> swap chain
  +-- hierarchy panel
  +-- inspector panel
  +-- asset browser panel
  +-- scene panel A -> texture RenderViewport -> existing camera A
  +-- scene panel B -> texture RenderViewport -> existing camera B
```

Moving a scene panel between host windows changes UI layout and the host that consumes its image. It does not implicitly create a camera, duplicate custom data, reset temporal history, create another scene or turn the panel into a swap-chain owner.

## 3. Ownership boundaries

### Vanguard application and platform ownership

- `EditorApplication` continues to compose the existing `EngineHost`, Frame Pipeline, rendering, window and input services. Dear ImGui must not introduce another application or event loop.
- Vanguard's window layer remains the authority for native-window creation, destruction, sizing, focus, DPI and platform handles.
- `PresentationService` remains the authority for presentation outputs. Dear ImGui must not create swap chains, acquire back buffers, submit queues or call Present directly.
- ImGui platform callbacks translate requests into Vanguard window operations and store only the association required to route later callbacks. They do not acquire engine ownership through raw SDL window manipulation.
- Window creation, destruction and accepted resize/rebind operations run on their established owning thread and retain the existing render-tail join requirements.

### Editor ownership

- The editor owns the single ImGui context used by its primary and secondary platform viewports, panel instances, layout persistence, focus routing and UI frame construction.
- Editor framework APIs own panel type and instance identities, panel registration, host-window identity, editor texture identity and frame input/output. Later command, document, selection and transaction modules remain separate domain owners.
- Shared editor widgets may use ImGui internally. Their public operations act on Vanguard document, command, selection, asset or viewport contracts rather than exposing ImGui callbacks as domain APIs.
- ImGui headers and types may appear in the pinned external project and editor-private UI implementation. They must not enter `source/` public APIs or authored project/runtime formats.

### Rendering ownership

- Existing cameras, view families, scene and camera custom data, temporal histories, graph scheduling, render-flow resource allocation, command lists, queues and terminal completion remain renderer-owned.
- An editor scene viewport controller owns editor-only interaction such as navigation gestures, focus, panel visibility and the association with an existing camera/output. It does not add another camera-state or custom-data preparation system.
- The UI renderer adapter consumes immutable UI render data and records through Vanguard RHI command lists supplied by the render graph. It must not use the official ImGui DirectX 12 backend to bypass Vanguard RHI/NVRHI.
- The geometry path remains independent. A scene texture is an ordinary input to host composition regardless of whether geometry, a material preview, a diagnostic renderer or another producer created it.

## 4. Platform multi-viewport contract

Native multi-viewport is enabled as part of the first proof. The editor adapter implements the required `ImGuiPlatformIO` behavior through Vanguard owners:

```text
ImGui requests a platform viewport
  -> editor platform adapter requests a Vanguard native window
  -> editor creates a PresentationService output for that window
  -> editor creates or binds its presentation RenderViewport
  -> adapter associates ImGui platform data with the EditorHostWindow identity
```

Destruction runs in the reverse ownership order after the host's outstanding rendering and presentation work is joined:

```text
stop accepting UI work for host
  -> join/retire its submitted UI and texture references
  -> destroy/unbind presentation RenderViewport
  -> destroy PresentationService output
  -> destroy Vanguard native window
  -> release ImGui association
```

The adapter must cover creation, destruction, show/hide, position, size, focus, minimized state, title, monitor/DPI information, cursor, clipboard, IME and event routing. `ImGui::UpdatePlatformWindows` may drive platform requests, but the default renderer path must not present secondary windows. UI rendering and presentation are scheduled through Vanguard after finalized draw data has been collected.

The primary window follows the same host-window model even if its native lifetime begins during normal application startup. Secondary windows are not special renderer owners; they are additional instances of the same host contract.

## 5. Scene viewport and runtime viewport supply

An editor scene viewport renders into a texture:

```text
existing render camera
  -> camera/view graph
  -> texture-output RenderViewport
  -> completed scene texture
  -> editor texture identity
  -> host UI draw data
  -> host-composition graph
  -> host presentation RenderViewport
  -> existing terminal Present
```

The current texture-output `RenderViewport` retains an externally supplied output texture. `RequestRenderExtent` changes requested dimensions but is not a complete allocation/replacement owner. The editor viewport phase must therefore add a narrow owner for creating, replacing, rebinding and retiring panel output textures through existing rendering/RHI contracts. Dear ImGui never owns those textures.

An ordinary runtime viewport normally renders to presentation directly:

```text
existing render camera
  -> camera/view graph and final runtime composition
  -> presentation RenderViewport/back buffer
  -> existing terminal Present
```

Both paths reuse the same renderer. The editor adds a texture boundary because a scene image is one element inside a larger host UI; it does not add an alternate scene renderer.

## 6. Graph integration

Editor UI must not attach specifically to `BuildRenderGraphBlank`, a particular camera graph or one placeholder node. Camera/view graphs produce resources. A host-composition graph consumes the resources referenced by that host's UI draw data and writes the host presentation output.

```text
scene/material/diagnostic graph -> texture outputs --+
                                                    +-> host UI composition -> terminal output -> Present
editor-generated UI draw data ----------------------+
```

The graph sees a Vanguard render-node implementation, declared texture uses and ordinary dependencies; it does not know Dear ImGui. The editor supplies the node implementation or contribution through the smallest graph-construction seam established in E0E. That seam is not a generic plugin callback system and must not require renderer code to include or instantiate ImGui.

The host UI composition is frame-global and has no camera identity. It may sample zero, one or several scene viewport textures. Its existence and order do not depend on which graphs produced those textures.

At the inspected boundary, one retained `RenderFrame` owns one `RenderFrameOutputTransaction`. E0 preserves that contract: each native editor host submits one host-composition render frame with one presentation output. Multiple host frames may be accepted through the existing render-command ordering. E0 does not redesign terminal receipts or add multiple presentation outputs to one retained frame without an independently demonstrated need.

Same-frame scene production and host sampling require explicit GPU ordering and resource state. The initial path should use the supported ordered graphics-queue continuation where possible. CPU job completion is not proof that the producer texture is ready, and unsupported cross-queue incoming waits must not be assumed. A narrow renderer capability may be coordinated if a real producer cannot use the supported path.

## 7. UI frame and draw-data lifetime

The ImGui context and panel drawing execute on the editor UI owner through the existing frame pipeline. After UI construction, the editor finalizes one draw-data set per visible ImGui platform viewport.

Worker render nodes must not borrow mutable ImGui arrays that the next `NewFrame` can overwrite. Before dispatch, the adapter converts the required vertices, indices, commands, clip rectangles and editor texture identities into bounded frame-owned UI render data. This is not a copy of the workspace, scene, document or panel state; it is the immutable transient geometry consumed by that frame's GPU recording.

The retained UI render data must:

- preserve ImGui command order, clipping and callback policy;
- deduplicate and retain referenced editor textures/descriptors for the host frame;
- use bounded reusable upload storage rather than unbounded per-widget allocation;
- remain alive until command recording has consumed it;
- retain GPU resources and upload ranges until their actual GPU consumers have retired;
- reject stale texture identities after output replacement instead of sampling an unrelated allocation.

Cached graph nodes obtain current UI render data through retained frame input. They must not retain draw-data pointers, panel pointers, host-window pointers or resized output textures from an earlier frame.

E0E-A stores only the latest completed `EditorUiFrameData` for each host. The frame owner acquires a retained copy before dispatch; replacing the service's latest entry does not invalidate a copy already held by a render frame or worker. Capture preserves vertex/index data, command order, vertex offsets, clipping and deduplicated texture bindings. Each binding retains its resolved texture and sampler, so later registry mutation cannot redirect an in-flight command. The only callback admitted across this boundary is the renderer-state reset marker; arbitrary ImGui callbacks are rejected because their code and user-data lifetime cannot be proven after the mutable ImGui frame ends.

E0E-B keeps toolkit texture work frame-global rather than duplicating it in every native host. The private adapter translates ImGui texture identities into generation-checked editor texture handles, creates the backing RHI texture without submitting private GPU work, and copies pending pixel data into one bounded, reference-counted `EditorUiTextureFrameData`. The current RHI write contract addresses a complete subresource, so multiple atlas dirty rectangles are deliberately coalesced into one complete texture upload for that texture and frame. This is rare atlas maintenance, not a per-widget path. The next recording slice must write this batch once before any host draw data from the same frame; it must not use the stock backend's private command-list submission and CPU fence wait.

Destroying an ImGui-owned texture unregisters its editor identity immediately, while retained UI frames and retained texture batches keep their concrete RHI references alive through their consumers. Ordinary editor and scene textures still use `RegisterTexture`; their resources are never mutated behind a stable public handle.

## 8. Input, focus and DPI

Vanguard pumps native events once. The editor adapter forwards the relevant events to ImGui and keeps the existing input service authoritative. Routing then distinguishes text editing, editor commands and viewport navigation:

- active text/IME input prevents ordinary editor shortcuts and camera controls from consuming the same keystroke;
- focused panel and active command context choose the command receiver;
- pointer capture remains associated with the originating host and scene panel through drag completion or cancellation;
- a scene panel receives navigation input only when its content rectangle and interaction state permit it;
- detached windows participate in the same routing rather than installing another global input system.

ImGui coordinates are logical UI coordinates. Each host records monitor scale and framebuffer scale explicitly. Scene picking maps from the panel's rendered content rectangle into the actual texture extent; title bars, borders, letterboxing and render scale are not part of the scene image.

## 9. Color and presentation

Scene viewport output must declare whether it is scene-linear, tone-mapped SDR or another explicit display space. Host composition must not accidentally run editor widgets through scene exposure, tone mapping or temporal post-processing.

The initial editor path should consume a display-ready scene texture, composite UI afterward, and convert the final host result according to the presentation output's declared format. UI colors, font atlas sampling, scene-image sampling and swap-chain encoding must have one documented convention in the RHI adapter before HDR editor output is attempted.

`RenderNodePresent` remains a terminal graph marker and `FrameRenderer::FinishBuiltGraph` remains responsible for completing or presenting the output after execution validation. An ImGui renderer callback must not duplicate that action.

## 10. Required Vanguard editor boundary

E0C implements the smallest editor-owned foundation necessary for later shell work. Exact spelling is finalized during implementation, but the ownership roles are fixed:

```text
EditorUiService        owns UI context and frame lifecycle
EditorPanelRegistry    owns panel types and instances
EditorPanelContext     exposes Vanguard document/command/selection/view context
EditorHostWindow       associates native window, presentation output and UI frame
EditorTextureHandle    stable editor-side identity resolved by the private renderer adapter
EditorUiFrameData      retained immutable per-host GPU input
```

This boundary does not provide `Button`, `Text`, `BeginWindow` or equivalent wrappers. Panel implementations and shared editor widgets use ImGui directly in editor-private code. The replaceable boundary protects domain behavior and engine ownership; it does not claim that changing UI toolkits would preserve widget implementation code.

The sealed editor directories retain their intended ownership:

```text
editor/framework/       UI lifecycle, panels, hosts, layout and private ImGui adapters
editor/extensions/      owned registrations and teardown
editor/commands/        contextual actions and shortcuts
editor/documents/       document lifetime and save/close policy
editor/transactions/    undo and redo
editor/selection/       scoped selections
editor/inspectors/      property presentation and edits
editor/viewport/        scene-panel controller and texture-output ownership
editor/assetBrowser/    asset-browser presentation and operations
```

## 11. E0A exit decision and following slices

E0A is complete when this contract and the editor foundation roadmap agree on the following non-negotiable points: ImGui docking is selected; native multi-viewport is supported from the first proof; Vanguard owns all windows, presentation and GPU work; scene panels consume texture outputs; host windows own presentation outputs; UI composition is graph-independent and frame-global; existing cameras/custom data/history are reused; draw data has a bounded retained lifetime; and core engine APIs remain free of ImGui types.

The subsequent bounded work is:

```text
E0B  COMPLETE - pinned ImGui vendor tree and isolated Premake project
E0C  SOURCE COMPLETE - Vanguard editor UI service and identity/lifetime API
E0D  SOURCE COMPLETE - ImGui context/frame/input/native-window adapter, including focus, cursor, clipboard and IME commands
E0E  SOURCE COMPLETE - retained draw/texture data, allocator-declared RHI recording and host-composition/presentation integration
E0F  IN PROGRESS - Debug and Shipping native lifecycle proofs pass; interactive gates remain
```

E0 still includes the project/source identity and persistence decisions required before E1. Completing E0A closes the UI ownership branch; it does not by itself complete all of E0.

## 12. E0E-C implementation checkpoint

`editor_ui_renderer.cpp` owns the two cached graphs (atlas upload and host composition), built with the ordinary node factory and external node containers. Nodes read retained frame payloads rather than storing panel-specific state. `RenderFrameInfo::SetGraph` is the only new dispatch seam in the renderer: a graph pointer, an optional import-registration callback and an eviction policy. It goes through `ExecuteBuiltGraph`, allocator declarations/resolve, node jobs, terminal receipts and graph-terminal Present. It does not modify any scene graph builder, introduce another scheduler or give ImGui a swap chain. Cached graph owners remain alive until the existing render-command CPU join.

The service authors UI in BeginFrame and submits composition in the same engine frame's EndFrame phase, after scene sources in Render. This is not next-frame presentation: each accepted host graph ends in its own Present on the existing render chain. The next BeginFrame only joins the previous CPU chain before UI/window/cache mutation. Scene-panel producers should submit their texture-output frames in Render; editor composition samples them through ordered Graphics continuation. Registered images must be retained, sampleable single-sample 2D textures whose producer returns them to their declared initial state on Graphics before composition. The adapter does not infer readiness of unrelated asynchronous queues. Scene cameras, custom data and temporal history stay with their existing owners.

Atlas pixels are submitted once in a headless graph frame, even when every host is minimized, before any host draw submission. A pending batch prevents another UI frame from overwriting it; acceptance transfers lifetime to the retained render frame. Geometry buffers grow only as needed per host and use device-local writes. The allocator declares CopyDestination then vertex/index reads at consecutive node boundaries; automatic NVRHI barriers are not assumed. Sampling textures and output writes are also declared. UI frames disable pool eviction so host rendering does not trim unrelated scene caches.

The renderer handles arbitrary draw-list lengths within capture limits, vertex offsets, indexed triangles, reset-state markers, per-command texture/sampler selection and framebuffer-scaled clipping. It knows no panel types, documents, gizmos or cameras. Descriptor slots are immutable while submitted work can reference them; old slots retire against the joined submission fences. Samplers are shared by RHI identity rather than duplicated per image. Pipeline objects are cached by target format. The editor compiles its small built-in shader once at startup using the existing shader-tools compiler; this does not add shader compilation to runtime rendering or to each panel/frame.

Native hosts use the existing PresentationService with SDR outputs, one EngineViewport per host, and the normal window-manager attachment. Host unregister joins rendering and destroys the frame viewport and presentation output before the platform adapter destroys the native window. Startup rollback and service quiesce/shutdown follow the same order. ImGui now advertises renderer multi-viewport support; its stock renderer callbacks are unused because finalized host data is submitted by the service.

Color scope: ordinary UI colors and DisplaySrgb images use conventional SDR UI blending. Linear inputs (including automatic sRGB SRV decode) are encoded for that output. Tone mapping/exposure of SceneLinear HDR content belongs to the producing view; E0E-C is not a second scene composition/color-management system. Arbitrary ImGui executable draw callbacks remain unsupported; complex panels use standard ImGui drawing and registered textures. Specialized GPU work belongs in a producer graph, not in a callback into mutable panel state.

Source review also corrected typed-array offsets in capture, the scissor extent conversion, and upload texture usage flags. Deferred E0F proof must cover font creation/update while minimized, primary plus detached hosts, multiple texture/sampler bindings and offsets, non-unit DPI/clipping, resize/redock/close, and shutdown with submitted work. No native success or performance claim is made by this source checkpoint.

## 13. Explicit non-goals

- No pixel-perfect CryEngine-inspired theme during E0.
- No production hierarchy, inspector, asset browser or scene manipulation panels.
- No alternate renderer, camera store, custom-data system, scheduler, window system or presentation path.
- No official ImGui DirectX backend controlling Vanguard GPU objects.
- No ImGui-created native windows outside Vanguard ownership.
- No per-widget locks, device-idle wait per frame or complete editor-state copies.
- No general plugin ABI, live DLL unloading, scripting system or universal UI event bus.
- No assumption that every hidden scene panel renders continuously.
