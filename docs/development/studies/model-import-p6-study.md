# Model import P6 — Editor integration preflight (2026-10-11)

Status: read-only preflight preserved before implementation. P0–P5 headless contracts are closed; this note is a P6 implementation guide, not a claim that P6 has been built or tested.

## Scope and boundaries

P6 connects the existing headless model inspection, immutable plan, stale-safe durable planned commit, exact model-generation cohort recook/publication, and retry/cancel/status contracts to the Editor. The initial production source formats are self-contained glTF 2.0 and GLB. P7 owns the generic Model Asset Editor, whole-model rendered preview, scene placement, exact logical-child search/selection/reference integration; P8 owns batch/scale hardening. Do not invent a second Editor-only cooker, scheduler, asset database, or publication owner. Keep one `ProjectAssetPipeline` under `ProjectWorkspaceService`.

## Exact-current integration evidence

- `editor/src/editor_project_service.cpp::ManagedProjectWorkspaceService::OnInitialize` initializes only `TextureAssetCompiler` and a single texture importer (`~1180–1213`). `DriveAssetFrame` (`~2714–2766`) already owns observation, automatic recook and publication progression. Pipeline-first shutdown and failure-preserved callback lifetime matter.
- `editor/private/vanguard/editor/editor_project_service.hpp::EditorProjectAssetAccess` (`~185–295`) offers generation-fenced browser commands and output delivery/publication snapshots, but lacks reviewed model-plan commit, exact-source refresh, and explicit retry/cancel/status facade methods.
- `editor/extensions/editor_asset_browser.cpp` implements bounded native file drop and queued external-file capture (`PollExternalDropOncePerFrame`, `ClaimExternalDropForPanel`, `BeginExternalImport`, `PumpExternalImport`). Its current terminal path calls `EditorProjectAssetAccess::ImportExternalSource` and durably mutates immediately; model sources must hand off captured source+destination to a review session *before* committing. The current queue implementation is browser-private.
- `editor/extensions/editor_production_extensions.cpp` registers production UI extensions; `editor/extensions/editor_asset_browser.cpp::ActivateAssetBrowser` and `DeactivateAssetBrowser` demonstrate built-in multi-instance panel creation, teardown, command contexts and persistence. Do not make importer session state a modal/global singleton.
- `tools/nanovanguard/src/project_asset_context.cpp::ProjectAssetContext::Open` (`~1258–1350`) proves the six compiler descriptors (`TextureAssetCompiler`, `ModelAssetCompiler::GetMeshDescriptor`, `PrefabCompilerDescriptor`, material program/pipeline/material), two source importers (texture, model), `mesh_tools`/`texture_tools` lifetime, imported material canonical context, model callbacks and generated intermediate fallback. `CommitModelImportPlan` (`~1468`), `RefreshModelFromSource` (`~1494`) prove headless source inspection, dependency witnesses, plan, durable commit, exact accepted cohort request.
- Essential composition glue (`GeneratedRequestStore`, `ResolveGeneratedFallback`, `PrefabCompilerDescriptor`, `ComposeMaterial`, `ComposeEmbeddedTexture`, `ComposeComposition`) is currently file-local to Nanovanguard. Factor it into a shared production tool owner; copying private CLI glue into Editor creates divergent semantics. Avoid a meshTools↔materialTools dependency cycle.
- `source/meshTools/include/vanguard/mesh_tools/model_import.hpp` exposes cancellable/progress-reporting `InspectModelFile/Memory`, candidate `ModelImportSettings`, `PlanModelImport`, immutable reconciliation/blocker/texture-resolution spans and `BuildModelPlannedImportRequest`.
- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_pipeline.hpp` exposes `CommitPlannedImport`, `RequestAssetCohortDelivery`, `GetAutomaticRecookStatus`, cancel/retry/publication retry, per-output delivery/publication truth. Preserve source/accepted generation witnesses through review and commit.
- `editor/premake5.lua` lacks meshTools/materialTools links/includes at Editor targets; direct `editor_project_service.cpp` test compilation (e.g. `editorAssetRetirementTests`) also requires closure.

## Read-only preflight risk assessment

**0 BLOCKER / 2 HIGH / 2 MEDIUM**, implementation readiness only (not post-implementation closure).

1. HIGH — cross-tool composition/generated-input adapters still Nanovanguard-private. Extract/rehome a reusable owned adapter and descriptor set before registering the Editor.
2. HIGH — Editor-facing generation-fenced reviewed model-plan/refresh and explicit build lifecycle facade absent. Expose thin methods delegating to the existing workspace pipeline, never raw state mutation from a panel.
3. MEDIUM — Asset Browser external drop/capture queue is local and currently auto-commits. Reuse its bounded snapshot/cancel semantics with a nonmutating model review handoff.
4. MEDIUM — broaden Editor build/test dependencies and carefully preserve pipeline-first shutdown, initialized-failure rollback, and callback ownership.

## Execution and tests

1. Shared model composition/descriptor owner, parity tests against Nanovanguard; exact six compiler and two importer registrations under the single Editor `ProjectAssetPipeline`.
2. Reusable per-source model session holding bounded source snapshot, manifest, candidate settings, plan/witnesses and asynchronous progress/cancellation. Only explicit accepted `Import`/`Apply-Reimport` commits through the P2/P5 authoring/publication path.
3. Built-in Editor model-import panel, OS-drop/queued-file and in-project candidate handoff; missing texture resolution, bulk project-image adoption, reconciliation diagnostics, stale-plan `Rescan`.
4. Show `Imported`, `Compiling`, `Ready`, `Build Failed` from pipeline evidence. Build failure cannot undo durable import or expose partially built candidate; old ready generation survives failed reimport. Retry/cancel use the pipeline owner.
5. Validate self-contained `.glb` from outside project: managed source+`.vmeta`, typed mesh/material/texture/composition outputs, coherent published generation and exact persistent IDs after restart; test stale source, missing/adopted images, cancellation, failure/retry, schema addition/removal. Run relevant headless and Editor tests in Debug/Development using serialized outer VS2022 x64 MSBuild `/m:1`.

Preflight only; no source edits/build/tests were performed while creating these findings. Preserve concurrent dirty-checkout changes.
