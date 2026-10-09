# Texture Preview Quality / Wanted-Mip QoS Plan

Status: authorized for back-to-back implementation on 2026-10-07.

This document is the durable handoff for the texture-preview quality work so the design survives chat compaction. Implement the phases continuously; do not build or run tests between phases. Audit the complete integrated change first, then perform one consolidated validation pass.

## Goal

Fix blurry editor texture previews by completing Vanguard's missing wanted-mip policy layer rather than adding an Asset Browser-only loader or forcing mip 0 everywhere. Consumers express required quality; `TextureResidencyRuntime` remains the sole owner that combines all demands into one safe physical mip residency behind the stable `GpuTextureResidencyHandle`.

The existing lower layers are retained: compact non-sparse mip suffixes, manager-owned transitions, immutable descriptor installation, atomic GPU Scene table cutover, promotion by uploading only newly needed mips plus copying the shared suffix, copy-only demotion, cancellation, bounded uploader work, and fence-safe retirement.

## Locked architectural rules

1. Editor-facing quality is presentation size in physical UI pixels, not `firstResidentMip` and not abstract `Thumbnail`/`Inspector` quality classes.
2. Preview priority and quality are independent. `Background` / `Interactive` / `Focused` control scheduling/admission urgency; presentation pixels control required detail.
3. The workspace never calls `TextureUploader` or `TextureResidencyManager` directly. It owns one `TextureDemandHandle` per exact-publication preview backing and updates that demand through `TextureResidencyRuntime`.
4. Runtime aggregation chooses the smallest requested first mip among all live demands on one exact resource generation, clamped to that generation's guaranteed mip tail.
5. One stable texture residency may own at most one physical quality/generation mutation at a time. New targets coalesce, safely supersede work that is still cancellable, or wait behind submitted/manager-owned work. Competing candidates are forbidden.
6. A quality transition failure must never invalidate an already usable texture. The old installation remains authoritative and sampleable; quality failure/retry is separate from overall residency failure.
7. Generation replacement never shares mips across generations. It uploads a fresh suffix targeted at the current aggregate wanted quality and then atomically cuts over. Quality changes arriving during replacement are recorded and reconciled after replacement completes.
8. `Ready` in project preview means exact publication is safely sampleable, not that every requested quality has arrived. Expose `qualitySatisfied` separately.
9. Same-publication mip promotion changes the physical RHI texture without changing resource generation or build/content fingerprints. Project preview therefore needs a monotonic `presentationRevision`, and Asset Browser must explicitly replace the registered `EditorTextureHandle` using the existing captured-UI-frame retirement path.
10. No editor-only PNG decode, no dedicated thumbnail GPU cache, no cooker/VTEX tail change, no sampler workaround, and no persistent thumbnail files.

## Phase 0 — renderer prerequisite fences

Before production mip transitions are enabled, fix `RenderingService` so `TextureResidencyRuntime::Tick` receives the real prior-reader queue cutover.

`ResolvePreviousFrame()` already joins the previous CPU rendering chain. Immediately after that join and before texture residency progress, call `rhi::GetSubmittedResidencyFences(priorReaderCutover)` and pass the result to `m_textureResidency.Tick(priorReaderCutover)`. The uploader already defers shared-mip copies until every supplied prerequisite fence is complete. Keep the later fresh submitted-receipt snapshot for residency retirement sealing. Do not add waits or synthetic per-texture submissions.

## Phase 1 — renderer per-demand quality

Keep existing `RequestTexture(resource, demand)` as a tail-compatible wrapper for current callers. Add a renderer-native request/update surface for explicit first-resident mip. Recommended shape:

```cpp
struct TextureDemandDesc
{
    u32 firstResidentMip = 0xffffffffu; // normalized/clamped by runtime
};

bool RequestTexture(const resources::ResourceHandle&, const TextureDemandDesc&, TextureDemandHandle&, ...);
bool SetDemandFirstResidentMip(TextureDemandHandle&, u32 firstResidentMip, ...); // main thread
```

`DemandSlot` stores requested first mip. `ResidencySlot` stores the aggregate desired first mip and bounded per-mip demand counts so demand release/update never scans unrelated textures. Mip count is tiny and bounded by cooked texture dimensions.

Initial installation should target the aggregate wanted mip directly; do not install the mip tail first and immediately replace it.

Add an independent quality-transition state to the runtime, e.g. Stable / Uploading / Cancelling / InstallationPending / Failed, plus target mip and failure/retry evidence. Keep the existing overall residency state for initial availability/retirement.

Promotion (`desired < current`) is eligible immediately. If a target changes while a request is still cancellable, cancel/reissue at the newest target. If already submitted, use the uploader's discard-on-completion behavior and reconcile after the manager transition releases. Once an installation is manager-owned/frozen, let the GPU Scene transaction accept/retry/discard normally, then reconcile the newest desired target.

## Phase 2 — generation replacement at wanted quality

Extend `TextureUploader::RequestGenerationReplacement` to accept a target first mip. It remains a fresh-generation upload and never attempts shared-mip reuse.

`TextureResidencyRuntime::ReplaceTexture` snapshots/clamps the current aggregate wanted mip against the new generation's metadata and requests that suffix directly. Replacement owns the residency mutation while Uploading/InstallationPending. Demand changes during replacement only update desired state. After the new generation becomes current, immediately reconcile any remaining difference between committed and desired quality.

Preserve current explicit replacement retry semantics and retained-candidate behavior.

## Phase 3 — project preview presentation-pixel QoS

Public project preview requests become a descriptor that carries priority and physical presentation extent independently, for example:

```cpp
struct ProjectTexturePreviewQuality
{
    u32 displayWidthPixels = 1;
    u32 displayHeightPixels = 1;
};

struct ProjectTexturePreviewRequest
{
    ProjectTexturePreviewPriority priority = ProjectTexturePreviewPriority::Background;
    ProjectTexturePreviewQuality quality;
};
```

Add an atomic update API for an existing preview interest. Existing priority-only overloads may temporarily map to a tiny/tail request for compatibility while call sites migrate.

After exact `TextureResourceObject` metadata is loaded, resolve each interest's presentation box to the coarsest asset mip that still gives approximately one source texel per physical display pixel after aspect fit. Clamp to `[0, mipTailFirstLevel]`. Source smaller than requested uses mip 0. Resolve each interest independently, then aggregate the backing by minimum first mip; do not component-wise merge pixel rectangles before resolution.

The existing exact-publication backing identity remains unchanged. The backing still aggregates priority separately by highest urgency and still owns one renderer texture demand.

Extend preview info with quality satisfaction and a monotonic `presentationRevision`. Source `width`/`height` remain source metadata, not presentation dimensions.

## Phase 4 — Asset Browser integration

Grid/compact-grid pass the actual final thumbnail slot extent in physical pixels after editor visual scaling and layout clamping. Selected preview passes its actual final aspect-fit display extent. Details/list remain metadata-only.

Store the last requested physical extent and last observed `presentationRevision` per thumbnail/selected presentation. Update the workspace request when physical extent changes; central mip quantization means changes inside one mip band cause no renderer transition.

When a same-publication presentation revision changes, register the new RHI texture first and retire the old `EditorTextureHandle` through the existing captured UI-frame lifetime mechanism. Never mutate an existing registration behind ImGui.

Reimport keeps the current exact-publication supersession model: old generation remains visible until the new publication is `Ready` / safely sampleable, then the new UI presentation replaces it and the old one retires after its captured frame. Requested quality is independent: the new Ready publication may continue refining afterward and reports that through `qualitySatisfied` / `presentationRevision` rather than delaying publication cutover.

## Phase 5 — bounded refinement and mature demotion

Reuse existing uploader bounds for acquisition starts, source bytes, copy bytes, candidate count/GPU bytes, completion polling and ready candidates, and manager bounds for pending installations/retirements.

Add a runtime `maximumMipTransitionsPerTick` budget so quality refinements cannot starve initial texture delivery. Prefer cleanup/cancellation work first. Promotion urgency may use demand scheduling priority later, but quality meaning must remain independent from priority.

If a Ready texture's quality transition enters the separate retry-required failure state, the workspace keeps the current sampleable presentation and re-arms the unchanged demand with bounded exponential UI-frame backoff. A permanent refinement failure must not become an unbounded per-frame retry loop.

Demotion (`desired > current`) is delayed by configurable hysteresis. Lowering one preview's extent or releasing a high-quality sibling records the lower target but does not immediately rebuild the texture. If demand rises again during hysteresis, cancel the demotion decision with zero GPU work. After hysteresis, demotion uses the existing copy-only compact transition. Final-demand release retires residency rather than demoting first.

Add committed texture GPU-byte telemetry and a configurable logical committed-texture budget using descriptor-based `rhi::GetMemoryRequirements(TextureDesc)` preflight/accounting. Budget pressure may defer a promotion while preserving the current usable image. It must not silently demote below the aggregate explicit demand. Memory-releasing eligible demotions are preferred under pressure.

This is not a full camera/view memory-pool solver. View/material wanted-mip policy remains future work and should reuse this demand primitive.

## Integrated audit gates

Before any compile/test run, reread all changed seams together and audit at least:

- prior-reader fence coverage for shared copies;
- one-mutation-per-residency invariant;
- no lower-layer call while runtime spinlock is held;
- stale demand/session handling;
- quality failure preserving current installation;
- target changes in Acquiring, Submitted, ReadyToInstall, pending and frozen states;
- replacement versus quality serialization and no cross-generation copying;
- shutdown/device-abandon and final-demand release in every transition phase;
- preview exact-publication/reimport invariants;
- same-publication presentation revision and UI-frame-safe old handle retirement;
- priority/quality independence;
- bounded work and committed GPU-byte accounting;
- no Details/list traversal residency regression.

## One consolidated validation pass

After implementation and audit only:

1. Renderer low-level transition tests: retain existing exact promotion/demotion copy-byte proofs and add supersession/pending/frozen/retry/discard cases as needed.
2. Engine texture residency service tests: multi-demand aggregation, duplicate-min release/update, direct high-quality initial install, promotion, hysteretic demotion, real prerequisite-fence gating, replacement quality, quality changes during replacement, budget deferral/fair retry, stale sessions, worker Reset/main-thread quality update, release in every state, shutdown/device abandon.
3. Editor retirement/workspace tests: Background+Focused interests with different pixel extents share one backing/demand; per-interest satisfaction; same-publication presentation revision; N->N+1 reimport keeps old UI until new generation reaches requested quality; captured-frame retirement remains correct.
4. Asset Browser policy tests: NPOT/extreme aspect, DPI scaling, compact/grid/selected extents, zoom changes within/across mip bands, scroll out/back retention, final release, Details/list zero-demand behavior.
5. Project Cardinal visual acceptance: Grid texture sharpness at multiple thumbnail sizes and selected preview, expected first-resident mip telemetry, one stable residency identity, bounded transition counts, no stale UI registration.

Only after all of the above is green is the blurry-preview fix considered mature.
