# Editor foundation: architectural scan and proposed roadmap

Date: 2026-09-10.

Status: architectural study and E0A through E0E implementation are complete. E0F now passes Premake generation, Debug and Shipping editor compilation and native two-host presentation/resize/closure proofs, with texture/clipping visuals inspected in Debug. Interactive docking/redocking and mixed-DPI/minimized updates remain open. The Shipping SDL3 prerequisite has been built and its link failure is resolved. Concurrent geometry-rendering work remains outside this pass. The detailed UI contract and current validation boundary are recorded in [editor-ui-integration.md](editor-ui-integration.md).

## 1. Verdict

Editor work can start now. Geometry rendering is a dependency of the final visible scene-editing workflow, not a prerequisite for project identity, importing, the asset browser, documents, commands, docking, or selection models.

We are not starting the asset backend from scratch. We have a substantial headless cooking and delivery backend, a generic project bootstrap, runtime world/entity contracts, and rendering viewport infrastructure. What is missing is the authored-project layer that connects those pieces and the editor shell that exposes them coherently.

The target remains:

```text
Project -> source assets + metadata -> import/cook -> derived data -> runtime resources
                                  -> editable scene document -> viewport -> selection -> inspector -> edit/save
```

These are connected ownership domains, not one enormous editor object. An imported image and an authored world document enter the same project system, but do not need identical editing behavior.

## 2. Existing decisions to preserve

### Directory ownership

The current repository authority is [docs/REPOSITORY_LAYOUT.md](../REPOSITORY_LAYOUT.md), linked at the top of the repository README. It places engine modules in `source/`, the editor product in `editor/`, tools in `tools/`, and engine-agnostic game projects in `games/`. Applications own composition and native launch points; reusable headless asset behavior stays below the editor.

The older [architecture/repository-layout.md](../architecture/repository-layout.md) still sketches `source/editor`, `source/tools`, and `source/applications`. That differs from the current authority and actual implementation. Do not use that older sketch to relocate the sealed product directories. A later documentation alignment can mark its obsolete placement explicitly.

The existing editor feature directories are:

```text
editor/
  framework/       workspace/       extensions/
  commands/        documents/       transactions/
  selection/       inspectors/      assetBrowser/
  viewport/        worldEditor/
```

All eleven currently contain only `.gitkeep`; they express intended ownership, not implemented subsystems. Preserve them and activate them in bounded slices. Do not replace them with a new catch-all module. Private headers and implementation files must follow the repository layout rules when these directories become real modules.

### Project and asset layout

[project-and-runtime-layout.md](../architecture/project-and-runtime-layout.md) and [vproject-format.md](../formats/vproject-format.md) already establish the central decisions:

| Domain | Authority and lifetime |
| --- | --- |
| `.vproject` | Authored project identity, relative roots, targets, plugins, cooking/packaging policy, startup references. Shared parser for editor and CLI. |
| `Assets/` | Original imported files and editable authored documents. Source-controlled authority. |
| `source.ext.vmeta` | Stable asset identity and authored import policy/settings. Sidecar includes the source extension. |
| `DerivedData/` | Disposable generated artifacts, successful build records, indexes, and local DDC. Reconstructable, never authored truth. |
| `Intermediate/` | Incomplete/temporary processing output. |
| `Saved/` | Project-local user state, recovery files, logs, and other non-authoritative output as specified by each feature. |
| `Config/` | Authored project/cooking/packaging policy, not a dumping ground for individual users' layouts. |
| `Builds/` | Deployable runtime images, assembled from committed artifacts. |
| Runtime | Typed resource identity, resource streaming/decoders, VPAK packages, live world and GPU resources. No `.vproject` or `.vmeta` requirement. |

`DerivedData` is already the local DDC home; a separate new cache system is not needed. If an importer needs an intermediate processed representation, classify it explicitly: disposable processing belongs in derived/intermediate storage; user-editable processing settings belong in metadata or an authored document. Do not create an unexplained second authoritative `Imported/` tree.

The layout's illustrative fingerprint-named `.vtex` files are not a reason to replace the implemented VDDC artifact-set storage. Use the existing artifact reader and loose-resource materializer to deliver runtime files where required.

Cardinal is a project descriptor and content consumer. Nothing in these services should select its name, directory, world, renderer settings, or asset conventions implicitly. Use another project outside the engine repository as a genericity acceptance case.

## 3. Current implementation, not just intent

| Area | What exists | What remains for this editor goal |
| --- | --- | --- |
| Project contract | `projects::ProjectDescriptor`, parser/validator/writer; `nanovanguard project create/inspect/validate`; staged, no-replace project creation. | Source-content/build validation and project UI. The CLI currently advertises `document|layout`, not the complete four-level normative draft. Plugin declarations are not a complete extension implementation. |
| Editor bootstrap | `EditorApplication`, `ProjectWorkspaceService`, native entry, generic `.vproject` argument, resolved roots, shared EngineHost and Frame Pipeline. | Actual shell, docking, commands, documents, asset browser, inspectors, viewport UI. The editor's Premake target does not currently integrate a UI toolkit. |
| Editor world startup | Configured `startup.editorWorld` enters World Session through a cooked world and development package location; empty startup is legal. | Opening/editing an authored world and using current derived output without requiring a preassembled game build. Loading a cooked world is not an authoring model. |
| Source asset database | `SourceAsset` accepts a typed resource reference plus content and metadata bytes. `.vmeta`/stable `AssetId` are documented. | No implemented `.vmeta` parser or project source-asset registry was found in the searched owned source/editor/tool code. Identity assignment, inventory, source-to-output mapping, and file operations need implementation. |
| Cooking and DDC | `BuildSystem::Prepare/Execute`, compiler registration/versioning, dependency fingerprints, multi-output artifacts, memory cache, persistent VDDC publication/validation. | Project-driven importer/settings selection and file resolution; local disk-budget policy. Do not rebuild the cooker/cache. |
| Build graph and incremental work | `BuildGraph`, `DependencyIndex`/VADI, required/optional/soft dependency policy, reverse queries, `IncrementalRecooker`, index transactions and rollback. | Source inventory integration, first import of untracked assets, watcher reconciliation, delete/move policy, editor progress/errors. VADI describes builds; it cannot replace the source registry. |
| Concrete processors | Texture, mesh, shader and material tooling/adapters; generic derived-artifact reading, loose materialization, package planning/assembly. | Register the existing adapters from project/tool composition. Inventory each supported format/settings contract as it is exposed; do not claim every future asset type is supported. |
| Material preview preparation | `MaterialPreviewService` already coordinates revisioned material/program/pipeline builds, last-valid output, cancellation, diagnostics and authored Apply callbacks. | This is a tool-side build coordinator, not an implemented preview viewport. Reuse it when adding a material tool rather than creating another material build queue. |
| Runtime loading | ResourceRegistry, ResourceStreamer, loose resources, packages, existing format decoders and residency ownership. | Project overlay publication and explicit update/reload orchestration for the supported live consumers. Replacing a file does not automatically replace a loaded resource. |
| World and properties | Stable schemas/field IDs, VOBJ, cooked world/cell/prefab contracts, GameWorld, ECS transactions, transform/runtime bridge. | Editable source documents, dirty/save semantics, authoring commands, undo, selection contexts, property presentation metadata/customization. Runtime schema offsets alone are not a safe property-edit command API. |
| View/rendering | `EngineViewport`, `RenderViewport`, `ViewportManager`, camera/view-family infrastructure, scene-owned `SceneCustomData`, camera-owned `CameraCustomData`, retained `FrameCustomData` access, camera history, presentation/texture/headless outputs, graph-terminal presentation and joined mutation boundaries. | Editor viewport UI/controllers that drive these existing owners, dock integration, local authoring preferences, input routing, gizmos, selection visuals, and UI composition. No second camera, custom-data, preparation or history system. Geometry thread supplies scene drawing, not these UI owners. |

### Important seams revealed by the scan

1. **Source identity versus resource identity.** Runtime `ResourceId` is currently 64-bit, and `ResourcePath::FromString` hashes a canonical logical path. That is not the proposed persistent 128-bit source-asset identity. Keep the runtime contract; specify a stable mapping from asset identity plus stable output/subasset identity to a logical typed runtime identity. Never hash the asset's current filesystem path for rename-sensitive references. Collision handling and imported subasset stability must be explicit. A mesh source can produce multiple outputs; array order is not durable identity.
2. **Inventory versus successful-build index.** New, failed, unsupported, or deleted source assets must appear in project queries even when no successful VADI record exists. DependencyIndex remains authoritative for committed build dependency facts; the source registry owns source facts. Authored references needed before a successful build must also remain inspectable without pretending old build records describe the latest source. The current recooker explicitly returns `UntrackedChange` for a source with no indexed output: initial imports must resolve and submit an ordinary BuildGraph request before later changes can use incremental recooking.
3. **Watcher events versus project transactions.** No owned public source-directory watcher implementation was found in the inspected filesystem/IO APIs. A watcher reports hints, not authoritative rename/deletion transactions. Rescan after overflow, coalesce bursts, wait out partial writes, and suppress our own writes. Source and sidecar changes may arrive separately.
4. **Recook versus live replacement.** `ResourceStreamer::RegisterLoose` rejects duplicate identities; `UnregisterLoose` refuses active loads. The registry exposes acquisition/eviction, not a turnkey editor hot-reload coordinator. Connect safe release/reload and dependency updates explicitly; keep the old resource visibly marked stale when a replacement cannot yet be applied. Never declare hot reload complete merely because the DDC contains new bytes. Renderer feature catalogs currently have startup-only mutation contracts.
5. **World capability versus service scope.** GameWorld supports preview/headless modes, but the current managed GameWorld/WorldSession services own one active world/session. Several views of one scene are different from several simultaneously active editing/play/preview worlds. Keep the document/context API capable of explicit world ownership; add a scoped multi-world integration only when the first independent preview/play consumer needs it.
6. **Viewport capability versus finished scene renderer.** Camera/selection/debug graph builder entry points exist, but the inspected `FrameRenderer` still rejects an unimplemented selected per-view builder. Do not turn those modes into functioning UI promises. The geometry thread owns the production drawing path; do not build a disposable direct-draw renderer beside it.

## 4. Useful Unreal patterns

Reference: the local `D:/UnrealEngine` tree, whose `Engine/Build/Build.version` declares 5.8.1. This is evidence about that checkout, not a claim about the latest released engine. The study is selective, not a complete Unreal audit. CryEngine is the requested UI/workflow inspiration; its implementation was not inspected in this pass.

| Pattern observed | Local source | Vanguard lesson, not a port mandate |
| --- | --- | --- |
| Project/plugin descriptors declare modules, platforms, dependencies and compatibility. | `Runtime/Projects/Public/ProjectDescriptor.h`, `PluginDescriptor.h` | Reuse `.vproject` and EngineHost; activate project extensions by explicit descriptors. No game-name branching or second service framework. |
| Asset Registry queries indexed metadata and dependencies without requiring every asset object to load. | `Runtime/AssetRegistry/Public/AssetRegistry/IAssetRegistry.h:243`, dependency queries around 517, events around 929 | The browser queries lightweight source records and build status. Opening a folder or context menu must not import/load all contained assets. |
| Content Browser separates data providers, virtual paths, item queries and UI operations. | `Editor/ContentBrowserData/Public/ContentBrowserDataSubsystem.h:114`, `ContentBrowserDataSource.h` | Keep browser presentation separate from asset operations. A project source root and a future read-only engine/plugin root can use the same UI. Do not port the whole virtual-item abstraction before a second provider requires it. |
| Import provenance is separate from resource use; source files and reimport history are explicit. | `Runtime/Engine/Classes/EditorFramework/AssetImportData.h:15` | Preserve source/settings/provenance, but keep Vanguard's sidecars and derived-record division. Unreal's package/UObject and path conventions do not replace Vanguard GUID policy. |
| Watcher events include rescan-required; auto-reimport has staged, time-budgeted processing and ignores known internal changes. | `Developer/DirectoryWatcher/Public/IDirectoryWatcher.h:8`; `Editor/UnrealEd/Private/AutoReimport/AutoReimportManager.cpp:47` | Reconcile filesystem observations before invoking existing incremental cooking. Do not cook from an OS callback or rescan everything every UI frame. |
| DDC is an asynchronous keyed record/value service with maintenance. | `Developer/DerivedDataCache/Public/DerivedDataCache.h:46` | Treat derived bytes as rebuildable cache products. Reuse VDDC/Jobs; defer distributed infrastructure, Zen, and remote workers. |
| Tab factories and layout restoration are independent of concrete panels. | `Runtime/Slate/Public/Framework/Docking/TabManager.h:1001` | Stable panel type and instance IDs, registered factories, versioned user layouts, graceful missing-extension restoration. |
| Commands bind actions, enablement/check state and contextual input separately. | `Runtime/Slate/Public/Framework/Commands/UICommandList.h:32` | Menus, toolbar buttons, shortcuts and context menus invoke the same command. Focus/document/view context selects the receiver. |
| Menus/extensions have owners and explicit removal; asset editors register their own tabs. | `Developer/ToolMenus/Public/ToolMenus.h:122`; `Editor/UnrealEd/Public/Toolkits/AssetEditorToolkit.h:142` | Give each extension its registrations and teardown. A material editor should register a document/toolset, not patch the core shell's switch statements. Start statically linked. |
| AssetDefinition explicitly avoids asset loading just to build context-menu actions. | `Editor/AssetDefinition/Public/AssetDefinition.h:362` | Asset-type metadata/actions should remain cheap and independent of loading. This is a concrete production warning against browser-driven dependency loads. |
| Viewport widget, editor client and rendering surface have separate roles. | `Editor/UnrealEd/Private/SEditorViewport.cpp:91`; `EditorViewportClient.h:344`; `Runtime/Engine/Public/Slate/SceneViewport.h:31` | Add the missing editor controller above existing Vanguard viewport/rendering owners; do not turn RenderViewport into a UI subsystem. |
| Camera transforms, view flags and view history are client-specific. | `Editor/UnrealEd/Public/EditorViewportClient.h:1965` | Study which views need independent state, but do not copy Unreal's storage ownership: Vanguard already has persistent cameras, custom data and history. Multiple panels presenting one rendered image need no independent rendering history. |
| Property customization, transactions and tool/gizmo/input contexts are separate facilities. | `Editor/PropertyEditor/Public/PropertyEditorModule.h:284`; `Editor/UnrealEd/Classes/Editor/Transactor.h:510`; `Runtime/InteractiveToolsFramework/Public/InteractiveToolsContext.h:31` | Shared property/edit commands, explicit undo boundaries, scoped tool input. Do not import UObject transactions, an alternate reflection system, or the entire Interactive Tools Framework. |

## 5. Viewport architecture to settle before implementation

### The distinction

A viewport is a particular view presented by the editor, not the scene itself, not necessarily a native window, and not merely a camera. Per-viewport state is the state that makes that particular view behave differently from another view of the same document.

### Existing camera/custom-data ownership comes first

Clarification after the user's review: the editor must extend Vanguard's existing RED-derived camera/custom-data machinery, not reproduce Unreal's viewport-client storage alongside it. Separate responsibilities do not automatically require separate state objects or new preparation stages.

- `RenderCameraStorage::Impl::SceneState` owns scene custom-data instances shared at scene scope. A new dock panel must not instantiate its own copy of that scene data.
- Each existing registered render camera owns its `RenderCameraState`, camera custom-data instances, previous matrices/origin/jitter, and temporal bookkeeping (`render_camera.cpp`, `Impl::RenderCamera`).
- `RenderCameraStorage::PrepareCustomData` already prepares scene custom data followed by the participating cameras' custom data. `FrameRenderer` calls it on the existing serialized rendering chain. `FrameCustomData` retains the prepared family and provides access to those objects; it is not a cloned feature-data store. The editor must not add a parallel prepare/tick/cache path.
- Camera- or scene-scoped rendering feature data belongs in the corresponding existing custom-data extension where appropriate. Dock layout, keyboard focus, navigation gestures and document undo remain editor concerns; do not put all UI state into rendering custom data either.
- Before adding any proposed per-viewport field, identify its current owner, its true scope, and the missing editor behavior. A UI control normally changes an existing camera/frame setting or custom-data input; it does not justify another authoritative copy of the renderer's state.

The source comparison is concrete: RED's `renderRenderFrame.cpp:4882` prepares scene custom data, then loops over each camera's `m_customData` at 4891. Vanguard's equivalent combined preparation is in `RenderCameraStorage::PrepareCustomData` at `render_camera.cpp:1316`, called by `FrameRenderer` at `frame_renderer.cpp:660` in the inspected tree. These source locations are evidence of the existing path, not an instruction to copy a new one.

Use these responsibilities; the proposed editor-side names are descriptive, not frozen APIs:

```text
Scene document / editing context
  owns authored objects, dirty state, edit history and selection
  references a live preview world / RenderScene
    |
    +-- Viewport A: dock panel + editor controls -> existing camera/custom-data owner
    |      -> existing EngineViewport -> existing RenderViewport -> texture
    |
    +-- Viewport B: dock panel + editor controls -> existing camera/custom-data owner
           -> existing EngineViewport -> existing RenderViewport -> texture

Host editor window composites visible viewport textures and UI, then presents.
```

| State | Correct owner |
| --- | --- |
| Authored entities, component values, hierarchy and asset references | Scene document; reflected into the live world through existing synchronization points. |
| Dirty state, save revision and undo history | Document/editing context. Closing a view must not destroy the document's undo stack. |
| Selected objects | Usually document/editing context, shared by views of that document. Asset-browser selection and an independent asset editor get their own contexts. |
| Camera pose and projection | Existing renderer camera state, driven through the existing scene/camera update path. A controller holds the association, not a competing render-camera state store. An authored scene camera is a separate object; piloting it is an explicit edit mode. |
| Navigation speed, orbit gesture/target and other authoring controls | Editor controller where these are genuinely editor-only inputs, not duplicated renderer state. |
| Render mode, exposure override, debug view, grid/overlay visibility | Per-view choices applied through existing frame/camera settings or appropriate custom-data inputs. Persist user preferences without creating a second live renderer configuration. |
| Selection visualization, hover hit, gizmo hit/drag state | UI interaction belongs to the originating view/tool; renderer-side visualization data uses existing camera/scene custom-data ownership where appropriate. Selection remains in its editing context. |
| Transform tool choice, coordinate system, snapping | Explicit editing/tool context policy with optional per-view overrides. No accidental process-global variable. Only one transaction writes a given document through an active drag. |
| Keyboard focus, pointer capture and active tool routing | Input owner plus originating viewport identity. Capture survives leaving the viewport rectangle until release/cancel. |
| Pixel extent, render scale, output texture and native presentation binding | Existing rendering viewport/output owner, driven by the UI controller at the accepted update boundary. |
| Temporal history, previous matrices and jitter progress | Existing renderer camera state and relevant camera custom data. Independently rendered temporal streams need independent state there; additional panels displaying the same rendered output do not. No editor history cache. |
| Dock layout and view preferences | User workspace persistence keyed by stable panel/view instance IDs, not runtime slot numbers or pointers. |
| Tool implementation, terrain/material/animation semantics | The specialized extension, using the shell/document/viewport contracts. |

Example: two panels show the same world. A is perspective/shaded; B is top/orthographic/wireframe. Selecting a crate in A selects it in the shared hierarchy and inspector and can highlight it in B. Orbiting A, hiding A's grid or changing A's exposure does not alter B. Dragging the crate records one document transaction, so both views observe the changed transform. Closing A leaves B and the document alive.

A material-preview viewport instead belongs to its own preview/edit context. It must not replace the world editor's selection or camera just because it became active.

### Accepted UI and integration constraints

- Dear ImGui's docking branch is the editor UI toolkit. It remains an editor implementation dependency behind Vanguard-owned panel, host-window, texture, command, document, selection and transaction contracts; it must not enter engine public APIs or become a one-for-one wrapped UI framework.
- Native ImGui multi-viewport is a foundation requirement, not a deferred enhancement. The first executable proof must include the primary editor window and a panel detached into a second Vanguard-owned native window. ImGui platform callbacks route through existing window/input/presentation owners and never create or present independent GPU resources.
- Docked scene viewport panels use `Texture` outputs. Multiple panels in one host window do not imply multiple swap chains. Each native host window has its own presentation output; a panel can move between hosts without becoming a new document, scene, camera or temporal-history owner.
- UI logical coordinates, content rectangles, monitor DPI, render scale and output pixel dimensions must be mapped explicitly. Pointer picking uses the viewport's rendered content rectangle, not the full window.
- Vanguard pumps native events and owns host windows. Dear ImGui adds neither another event loop nor direct raw-window ownership.
- Preserve the established render-tail join before applied viewport mutation, resize/rebind and destruction. Keep graph-terminal Present in the same frame. The next frame boundary may join the preceding work; it must not become the place that performs the preceding frame's Present.
- Rendering a viewport texture, sampling it during UI composition, and retiring/replacing it require explicit GPU ordering/lifetime. A completed CPU graph job is not proof that the GPU is done with that texture. Use existing command/RHI fence ownership, not a lock around every panel paint or a full device idle every UI frame.
- `RequestRenderExtent` currently changes render dimensions; it is not a complete resizable dock-panel output-texture owner. Specify that narrow texture replacement/rebind integration when building editor viewports.
- Current camera history and camera custom data are stored per registered camera. First distinguish independent rendering from presenting the same rendered output in multiple panels. Only independent temporal streams need independent state; use the existing camera/custom-data ownership and lifecycle to represent them. Do not automatically allocate a camera/custom-data set per dock panel or invent another history cache. Before choosing a camera association, account for its custom-data allocation and preparation costs.
- Do not add lock-and-copy mirrors of the complete workspace, document or viewport collection. UI/editor mutation has one owner; workers own bounded build inputs/results and publish changes at defined boundaries. Existing retained per-frame rendering data keeps its legitimate lifetime role.
- Hidden/inactive viewports need an explicit realtime/on-demand policy. Multi-viewport support does not require every viewport to consume a full frame budget at all times, nor concurrent CPU execution of whole render frames. Use the current serialized frame-dispatch contract first.
- Editor UI composition is independent of `BuildRenderGraphBlank` and of every particular scene/camera graph. Producer graphs create textures; a frame-global host-composition node consumes the current host's UI draw data and referenced textures before that host's existing terminal output handling.
- One retained `RenderFrame` currently owns one output transaction. Preserve that contract in E0 by submitting one host-composition frame per native editor window; do not redesign terminal receipts for multiple outputs without demonstrated need.

## 6. Proposed phases

Eight phases, E0 through E7. E0 is a bounded decision gate, not an open-ended redesign. Implementation slices should remain small inside each phase. A phase is closed by its stated workflow, not by creating all of its folders.

### E0 - Freeze and prove the editor boundaries and ImGui integration

- **Build/define:** Follow the accepted [editor UI integration contract](editor-ui-integration.md): import the pinned ImGui docking source, implement the Vanguard editor UI foundation, platform/input/native-window adapter and RHI/render-graph adapter, then prove primary plus detached native host windows. Also define editor process/workspace/document/view lifetimes, authored versus user persistence, extension registration and the source-ID/output-ID mapping. Establish the affected-target baseline before implementation validation.
- **Why now:** Native window/pump ownership, embedding render textures and stable identity are expensive to reverse after panels and documents proliferate.
- **Depends on:** Existing repository/project decisions, EngineHost, Frame Pipeline, platform/window/input, viewport/presentation contracts. No geometry implementation prerequisite.
- **Decisions:** Dear ImGui docking is selected and pinned behind editor-owned contracts. Native multi-viewport is supported from the first proof through Vanguard window and presentation ownership. Direct ImGui widget use remains editor-private; core systems use Vanguard domain APIs. CryEngine-inspired organization is a workflow/style direction, not permission to duplicate renderer state or build another toolkit. Define text/IME/clipboard, DPI, accessibility limitations, styling, texture identity, color space, deployment and licensing explicitly.
- **Exit:** The accepted contract and module ownership map are reflected in code, the primary dockspace and a detached second native window render and tear down through existing ownership, and explicit renderer handoff points preserve same-frame graph-terminal presentation. Project root/case/containment, persistence and source/output identity policy are resolved without changing the sealed directory model.
- **Avoid early:** Pixel-perfect theme work, production panels, a plugin ABI, live DLL reload, an editor-specific scheduler, a replacement resource system, official ImGui backends bypassing Vanguard ownership, or exhaustive tool-framework interfaces.

E0 is executed in bounded slices: E0A records the accepted UI contract; E0B imports the pinned vendor tree; E0C implements the Vanguard editor UI foundation; E0D implements platform/input/native multi-window integration; E0E implements RHI/render-graph integration; and E0F proves docking, detachment, texture display, resize and teardown. E0A closes only the UI ownership decision branch; E0 still includes the project/source identity and persistence decisions required before E1.

### E1 - Project source registry and durable asset identity

- **Build:** Implement `.vmeta`, source inventory and indexed project queries; source type/importer discovery; asset-to-output/subasset identity mapping; duplicate/missing metadata diagnostics; explicit first import and rescan. Reuse the existing project parser and creation routines.
- **Why here:** Browser operations, dependencies and scene references must survive file renames before they are persisted by tools.
- **Depends on:** E0; `projects`, `filesystem`, `io`, `crypto`, existing typed runtime identities and assets contracts. Remain callable headlessly.
- **Decisions:** Stable 128-bit asset IDs; stable generated-output keys; duplicate ID handling after copying a file/sidecar; adopt-versus-copy semantics for files outside `Assets`; read-only engine/plugin roots; metadata schema/version migration. Moving source plus sidecar preserves identity; duplicating an asset creates new identity. Never infer identity from content equality alone.
- **Exit:** Both editor-facing services and CLI can enumerate/query a project with new, failed, unsupported and successfully built assets. Restart and project relocation preserve identities. A second project works without engine-root assumptions.
- **Avoid early:** Thumbnails, importing every file at launch, a second VADI, runtime GUID-format replacement, cloud asset services, or content-browser widget code in the registry.

### E2 - Project import, incremental changes and local cache lifecycle

- **Build:** Connect source records/settings to existing compiler adapters, BuildSystem, BuildGraph, VADI and IncrementalRecooker. Add directory change observation and bounded reconciliation, explicit rename/move/delete/duplicate operations, dependency invalidation, build status/reasons, cancellation, and local DDC maintenance. Expose matching headless commands.
- **Why here:** A browser must sit on reliable file/build operations. This makes the asset backend usable from real project files rather than only caller-supplied requests.
- **Depends on:** E1; existing texture/mesh/shader/material tooling, Jobs, DDC/index/recooker, filesystem publication and artifact readers.
- **Decisions:** Source+sidecar crash recovery; rename without metadata and ambiguous external moves; case-only renames; partial writes; watcher overflow/rescan; stale completion rejection when source/settings change during a build; one-in-flight recook batch with accumulated later changes. Delete means a missing asset plus explicit dependent diagnostics, not silently wiping references. Define authored/importer/runtime-format/build-fingerprint versions separately.
- **Exit:** Drop a supported source asset into a project, resolve its settings/identity, build it, restart and hit DDC, change it and rebuild affected outputs, move it without breaking identity, and delete it with clear dependent state. Failed builds preserve the last committed artifact while reporting it as stale. Cache cleanup never deletes sources or artifacts still needed by active delivery/readers.
- **Avoid early:** Distributed DDC, remote cooking, a persistent imported-data format without a consumer, every importer UI, full source-control integration, or treating OS notifications as reliable transactions.

### E3 - Editor shell, commands and extension registration

- **Build:** Activate framework/workspace/extensions/commands: dockable/floating panels, stable panel factories and instances, menus/toolbars/context menus, keyboard shortcut routing, console/log ingestion, progress/status, layout save/restore. Give extensions owned registrations and ordered teardown. Use a coherent CryEngine-inspired workspace organization.
- **Why here:** Later tools must plug into common infrastructure instead of each inventing input, docking or menu handling. This phase can run alongside E1/E2 after E0.
- **Depends on:** E0; EditorApplication, ProjectWorkspaceService, EngineHost/Frame Pipeline, chosen UI backend, window/input and diagnostics. Asset backend progress is a consumer, not a shell dependency cycle.
- **Decisions:** Process versus project-scoped services; panel type versus panel instance; contextual command precedence (focused control/tool, view/document, workspace); text input versus shortcut capture; persistent user preferences versus project policy; typed domain notifications and removable subscriptions. Use direct APIs for requests, not a universal message bus for every call.
- **Exit:** A usable docked shell restores its layout, remains responsive during background work, routes a command consistently from menu/button/shortcut, and hosts a separately registered demonstration extension without modifying shell dispatch logic.
- **Avoid early:** A per-panel service graph, hot-unloadable modules, scripting languages, marketplace infrastructure, complete advanced tools, or asset processing in UI callbacks. Register modules statically first; the `.vproject` plugin list is activation/configuration policy, not a reason to implement a dynamic ABI now.

### E4 - Asset browser and runtime delivery integration

- **Build:** Asset Folder/Project Browser backed by E1/E2 queries: folders, search/filtering, import state, dependencies/referencers, settings, move/rename/delete/import/reimport commands, contextual actions, and bounded preview/thumbnail jobs. Materialize committed artifacts and expose them through the ordinary loose-resource overlay and existing decoders.
- **Why here:** This closes the real asset workflow in the editor without embedding import logic into panels. Asset selection and settings also provide an early consumer of contextual commands.
- **Depends on:** E1/E2/E3, DerivedDataArtifactSource, LooseResourceMaterializer, ResourceStreamer/Registry and the supported runtime resource consumers. Native geometry display is not necessary for the first image/metadata preview.
- **Decisions:** Metadata-only browsing; multiple browser instances with independent filters and explicit selection contexts; cancellation on document/project close; bounded thumbnail identity/cache; applied-versus-built status. Implement safe update/reacquire for supported resource types through their actual owners. If a loaded resource cannot be replaced safely, require an explicit preview/world reload instead of pretending all references updated.
- **Exit:** A newly dropped asset appears, imports, reports errors/progress, can be previewed through runtime loading, survives restart/cache reuse, and can be reimported without silently displaying invalid content. Folder navigation and context-menu construction do not load an asset dependency closure.
- **Avoid early:** Every asset-specific editor, loading meshes for list rows, unlimited thumbnail GPU work, broad shader/PSO live reload, and a second editor-only resource loader. Basic engine-owned UI assets use explicit engine roots/catalogs, not Cardinal settings.

### E5 - Documents, edit transactions, hierarchy and inspector

- **Build:** Activate documents/transactions/selection/inspectors/worldEditor around a small editable scene document: stable object/component references, create/open/save/close, dirty state, hierarchy, selection contexts, reflected inspection, validated edits, undo/redo, drag coalescing and cancel. Add the minimal authored scene/prefab input needed to feed existing cookers/materialization.
- **Why here:** Property widgets and gizmos must share a correct edit model. A cooked `.vworld` loaded into ECS cannot preserve all source authoring semantics by itself.
- **Depends on:** E1 identity, E3 commands/shell, E4 asset references/delivery, reflection/schemas, existing world/prefab/ecs/entities/transform contracts.
- **Decisions:** Versioned editable source representation and migrations; document-to-live-world mapping; property display/range/unit/custom-editor metadata outside raw runtime offsets; read-only/unknown component handling; transaction ownership and memory limits; selection separate from visibility and transient Flecs handles. All UI modifications go through one document edit API and existing world sync points. Retain only data needed to reverse an edit, not a whole-world copy per mouse move.
- **Exit:** Create/open a small generic scene, select objects in its hierarchy, change valid properties, undo/redo, save, close and reopen without losing IDs/references. Dirty close prompts and conflicting external source edits are deliberate. A view closing does not close its document accidentally.
- **Avoid early:** Terrain/layer partitioning UI, full prefab inheritance, arbitrary object graph diff/merge, multi-user collaboration, full Play-In-Editor cloning, or an independent editor ECS. Preserve the distinction between editable source documents and the same cooked runtime formats used by the game.

### E6 - Multiple editor viewports and renderer/UI composition

- **Build:** Activate viewport panels/controllers by driving existing camera/custom-data and EngineViewport/RenderViewport ownership. Add perspective/orthographic navigation, display-setting controls/overlays, realtime/on-demand redraw, correct focus/capture, DPI-aware dimensions and layout persistence. Connect host UI composition, resize/docking/floating lifecycle and terminal presentation; do not duplicate renderer state or preparation.
- **Why here:** Real documents and selection now exist, so the viewport contract can be exercised without global shortcuts. This is the main convergence point with the geometry thread.
- **Depends on:** E0 native/UI choice, E3 shell, E5 document/context model, existing camera/viewport/presentation/graph machinery. The production camera/geometry render path is needed to claim visible scene rendering, but not to implement/controller-test panel ownership and input.
- **Decisions:** Map proposed editor fields to existing camera/scene/custom-data owners first. Distinguish one image in several panels, independent renders of one scene, and separate preview worlds; preserve existing temporal/custom-data lifetimes. Decide tool context, texture output replacement/UI sampling order, stable panel identity, hidden-view scheduling and disabled unsupported modes. Integrate through the existing renderer, not a second state/preparation system.
- **Exit:** Two independently rendered views of one scene have different camera/render settings, shared intended selection and independent hover/navigation, using existing camera/custom-data ownership. Resize, undock/redock, hide and close either view safely. Present occurs in the current render chain, not at the next frame boundary. Demonstrate temporal isolation through existing owners for independent renders, and no duplicate camera/custom-data/history allocation merely to show the same rendered image in another panel.
- **Avoid early:** Stereo/VR, every debug mode, cinematic preview frameworks, lock-and-copy viewport mirrors, concurrent whole-frame execution redesign, or a fallback direct-draw geometry implementation.

### E7 - Manipulation and the complete foundation exit gate

- **Build:** Add selection picking, focus/frame selection, translate/rotate/scale gizmos, basic snapping and asset placement using E5 transactions. Connect hierarchy, viewport and inspector to that same edit path. Complete local recovery/save behavior and exercise a second extension and project through the workflow.
- **Why last:** Picking and manipulation now have stable identity, document ownership, camera geometry, input capture and undo semantics. This validates the foundation rather than starting a collection of specialized tools.
- **Depends on:** E1-E6 and the geometry thread's production scene rendering. Agree picking outputs/identity with that thread; do not make GPU picking a prerequisite for hierarchy selection. A bounded CPU query may be used where existing query data supports it, without becoming a second renderer or an inaccurate hidden selection fallback.
- **Decisions:** Exact pick versus bounds selection; stale asynchronous pick results identified by scene/view revision; transform spaces and parent transforms; one gesture/one undo step; Escape cancellation; blocked/read-only edits; bounded recovery files; safe preview/play reload policy. An explicit separate preview context must not hijack the world editor's single managed world.
- **Exit:** In a generic project: drop supported mesh/texture sources, import/cache, place an object, see it in two views, select it, edit with gizmo and inspector, undo/redo, save, reopen and cook/package/load through runtime contracts. Reimport and rename preserve references; delete reports dependants; restart with cold/warm cache works. A small independent tool registers its panel, action, property customization or document handler without shell modifications. Record responsiveness/memory on a representative asset tree and close the project while work is active.
- **Avoid early:** Building terrain, animation, particles, cinematics, full material graph authoring, multiplayer editing or a profiling suite just to prove extensibility. These become consumers of the completed foundation, not prerequisites for it.

## 7. Scheduling and scope control

```text
E0 -> E1 -> E2 -----+
 |                  +-> E4 -> E5 -> E6 -> E7
 +------> E3 -------+             ^
                                 |
                   production geometry rendering
```

E3 may be developed alongside the asset phases after E0 freezes native/UI ownership. E5's data/command tests do not require scene drawing. E6's controllers can progress before the geometry handoff, but E6/E7 visible-scene exit gates cannot be declared complete without it. Do not disturb the other thread's renderer files during the asset/shell phases.

Use bounded integration changes when an actual consumer exposes a missing capability: a filesystem watcher adapter, safe loose-resource replacement, editor-owned world context, or texture-output replacement. These are real seams discovered by this scan, not justification to reopen the allocator or create a new graph architecture.

The first implementation choice should be E0, followed by E1. UI appearance work can proceed after the toolkit/window decision, but the core browser and scene references should not be implemented before stable source identity is settled.

## 8. Source pointers for resuming

Vanguard files inspected, in addition to the normative documents linked above:

- [Editor application](../../editor/src/editor_application.cpp), [workspace owner](../../editor/src/editor_project_service.cpp), [editor build dependencies](../../editor/premake5.lua), [project API](../../source/projects/include/vanguard/projects/project.hpp), [CLI project commands](../../tools/nanovanguard/src/project_commands.cpp).
- [Cooking API](../../source/assets/include/vanguard/assets/assets.hpp), [build graph](../../source/assets/include/vanguard/assets/asset_graph.hpp), [dependency index](../../source/assets/include/vanguard/assets/asset_index.hpp), [incremental recooker](../../source/assets/include/vanguard/assets/asset_recooker.hpp), [artifact reader](../../source/assets/include/vanguard/assets/derived_data_artifact_source.hpp), [loose materializer](../../source/assets/include/vanguard/assets/loose_resource_materializer.hpp). Existing implementation entry points include `BuildSystem::Prepare/Execute`, `BuildGraph::Request`, `IncrementalRecooker::Request`, and `DependencyIndex::CommitTransaction`.
- [Runtime resource identity/registry](../../source/resources/include/vanguard/resources/resources.hpp), [loose registration implementation](../../source/streaming/src/streaming.cpp), [schema metadata](../../source/reflection/include/vanguard/reflection/reflection.hpp), [world contract](../../source/world/README.md), [prefab contract](../../source/prefabs/README.md), [entity/materialization contract](../../source/entities/README.md), [WorldSession](../../source/engine/include/vanguard/engine/world_session_service.hpp), [GameWorld service scope](../../source/engine/include/vanguard/engine/game_world_service.hpp).
- [Viewport API](../../source/rendering/include/vanguard/rendering/viewport.hpp), [viewport mutation](../../source/rendering/src/viewport.cpp), [camera ownership](../../source/rendering/include/vanguard/rendering/render_camera.hpp), [camera history implementation](../../source/rendering/src/render_camera.cpp), [FrameRenderer](../../source/rendering/src/frame_renderer.cpp), [frame participant contract](../../source/engine/include/vanguard/engine/frame_pipeline_service.hpp).
- [Viewport alignment decisions](viewport-red-alignment-plan.md), [geometry-thread scope](geometry-rendering-study.md), [shader/pipeline startup boundary](../../source/rendering/docs/shader-pipeline-runtime.md).

Older README/development history sometimes describes superseded snapshots, unimplemented concrete compilers or earlier startup gates. Prefer current implementations and the most recent explicit decisions; do not turn stale historical notes into new work. This report records source-level availability, not fresh test results or a claim that every existing subsystem is production-complete.
