# Vanguard Texture Streaming and GPU Residency Study Plan

Date: 2026-08-30

Status: Phases 0 through 4 are studied and implemented. Cooked VTEX metadata can
be loaded through the normal resource pipeline, verified mip windows can be
acquired under shared streaming budgets, compact physical textures can be
uploaded in bounded shared batches, and initial or replacement installations
can be staged for the stable `GpuTextureResidency` table. Phase 5 runtime
integration is studied below. Phase 5A runtime ownership/demand, Phase 5B's
shared GPU Scene contribution, and Phase 5C engine/lifecycle validation are
implemented.

## 1. Objective and Boundary

The target path is:

```text
cooked .vtex metadata
  -> generic loose-file or package range source
  -> guaranteed mip-tail reads
  -> verified GPU-ready subresources
  -> GPU texture installation
  -> immutable bindless descriptor for the installed texture version
  -> stable GpuTextureResidency entry
  -> texture index usable by GpuMaterialResource
```

This work ends when a texture has a stable material-visible residency index and
an explicitly tracked resident mip range whose current physical installation
has an immutable bindless descriptor. It does not implement materials,
visibility-driven mip selection, rendering, draw submission, virtual texturing,
or shader page-table lookup.

Terminology is kept strict:

- **resource loaded** means the `.vtex` metadata and its source are available;
- **mip data loaded** means selected cooked subresource bytes are in CPU staging;
- **GPU resident mip** means the mip is present in the currently usable GPU
  texture;
- **bindless-ready** means a live descriptor index resolves to that usable
  texture;
- RHI `MakeResident`/`Evict` controls whole D3D12 pageable allocations. It is
  not ordinary per-mip texture streaming.

## 2. Phase 0 — Broad Architecture Study

### Result

```text
Study work: complete
Exit gate: passed
Production changes: Phase 1 foundation implemented after the study
Next allowed work: Phase 1 study on explicit request
```

### 2.1 Vanguard Current State

Vanguard already has most of the lower and upper seams, but not the texture
residency owner that joins them.

#### Cooked texture data is already suitable

Evidence:

- `source/textures/include/vanguard/textures/textures.hpp:108-293`
- `source/textures/src/textures.cpp:600-866`
- `source/textureTools/src/texture_tools.cpp`
- `source/textureTools/src/texture_import.cpp`

The `VTEX` format already provides:

- texture dimension, format, color space, extent, array/cube layout, and mips;
- exact GPU upload row and slice pitches;
- one independently addressable record per subresource;
- content verification digests;
- an explicit `mipTailFirstLevel` and `RequiredForMipTail` storage segments;
- loose-file and package-planning segments through `BuildStorageSegments`;
- GPU-ready compressed formats and a `DirectGpuUpload` contract.

No intermediate vertex-style interpretation or runtime image transcoding is
needed for normal cooked textures. The residency path should validate and copy
cooked subresource bytes into an upload path.

#### Generic streaming machinery already exists

Evidence:

- `source/streaming/include/vanguard/streaming/resource_source.hpp:14-232`
- `source/streaming/src/resource_source.cpp:192-1406`
- `source/resources/include/vanguard/resources/resource_pipeline.hpp:10-240`
- `source/resources/src/resource_pipeline.cpp`

The existing generic systems already own:

- loose-file and packaged exact-range reads;
- decompression and verification before exposing bytes;
- cancellation, priority, retries, and structured failures;
- equal-range request coalescing;
- bounded FIFO staging admission;
- resource-request coalescing and dependency retention.

Texture code must use these systems. It must not create a second texture-only
IO scheduler, package reader, retry system, or staging-budget mechanism.

#### RHI upload and bindless primitives already exist

Evidence:

- `source/rhi/include/vanguard/rhi/rhi.hpp:19-36,195`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp:675-716,1310-1365`
- `source/rhi/nvrhi/src/common_backend.cpp:1220-1300,1607-1810,3123`
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp:700-740`

The RHI can create textures, initialize or write individual subresources,
transition/copy them, create bindless descriptor domains, and retire descriptor
slots behind graphics/compute/copy fences. D3D12 allocation residency is also
tracked independently.

One constraint is decisive: a Vanguard descriptor slot is currently
**write-once**. After `WriteDescriptor`, its state becomes `Populated`; a second
write is rejected. Therefore a changing physical texture cannot currently keep
the same descriptor index by rewriting that slot.

`TextureDesc::virtualResource` also does not by itself provide a public mip-tile
mapping API. Phase 0 found no Vanguard interface equivalent to Unreal's
`VirtualTextureSetFirstMipInMemory`. The flag is not sufficient evidence of
working sparse mip residency.

#### The material-facing GPU table already exists

Evidence:

- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp:281-301`
- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp`
- `source/rendering/src/gpu_scene_definitions.cpp:699`

`GpuMaterialResource` currently stores a bindless resource descriptor index and
a sampler descriptor index. Phase 0 identified this as the existing seam but
left its replacement semantics unresolved. Phase 1 proved that texture entries
must instead carry a stable `GpuTextureResidency` index; other resource kinds
may still use direct descriptor indices.

Phase 0 initially treated an extra GPU Scene table as optional. Phase 1
superseded that conclusion: the compact table is required to provide stable
logical identity without unsafe live-descriptor rewriting. It stores residency
metadata and a current immutable descriptor index, not a second texture object.

### 2.2 RED Findings

Source snapshot:

```text
D:\root\R6.Root\Mainline\dev\src\common
non-Git local RED snapshot
```

Primary evidence:

- `resourceMaterial/src/bitmapTexture.cpp:258-312,454-474`
- `resourceMaterial/include/bitmapTexture.h:29-168`
- `renderer/src/renderTextureBase.h:39-197`
- `renderer/src/renderTextureBase.cpp:14-187,191-305`

Useful RED ideas:

- The cook marks a guaranteed resident tail. RED keeps up to the lowest five
  mips (through 32x32) resident for streamable textures.
- Runtime creation begins at a selected resident mip, creates a physically
  smaller texture whose top level is that mip, and uploads the cooked bytes
  directly.
- The engine resource and the render texture wrapper are separate objects, so
  the GPU texture can be replaced without changing the asset identity.
- The GPU texture becomes current only after the resource blob is valid and upload has
  produced a real render resource.

Limitations:

- The inspected RED branch retains the resident/pending/streaming mip fields,
  but several `CRenderTextureBase` streaming methods are empty or commented out.
  It is useful evidence for cooked layout, resident-tail policy, ownership, and
  physical texture creation, but it is not complete source for Vanguard's
  asynchronous state machine.
- RED's raw `GpuApi::TextureRef` replacement does not solve Vanguard's stable
  bindless descriptor problem by itself.

Decision classification:

- **Copy:** guaranteed mip-tail concept and direct upload-ready cooked data.
- **Adapt:** physically create from the current first resident mip and replace
  the GPU resource transactionally.
- **Reject:** raw mutable GPU references as the material-visible identity.

### 2.3 Unreal Findings

Source snapshot:

```text
D:\UnrealEngine
Unreal Engine 5.8.1
CompatibleChangelist 55116800
```

Primary evidence:

- `Engine/Source/Runtime/Engine/Private/Streaming/StreamingManagerTexture.h`
- `Engine/Source/Runtime/Engine/Private/Streaming/StreamingManagerTexture.cpp:384-438,798,1465-1789,1975-2145`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DUpdate.h`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DUpdate.cpp:62-179`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DStreamIn.h`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DStreamIn.cpp:72-165`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DStreamIn_IO_AsyncCreate.cpp:11-82`
- `Engine/Source/Runtime/Engine/Private/Rendering/StreamableTextureResource.cpp:244-269`
- `Engine/Source/Runtime/RHI/Public/RHITextureReference.h:13-56`
- `Engine/Source/Runtime/RHI/Private/RHITextureReference.cpp:12-45`

Useful Unreal ideas:

- Policy and execution are separate. The manager computes wanted/budgeted mips;
  a per-texture update object performs one requested transition.
- State distinguishes wanted, requested, and resident mip counts.
- Stream-in builds an intermediate texture through asynchronous reallocation or
  creation, loads new mips, copies shared old mips, and only then finalizes.
- Cancellation discards incomplete intermediate state without corrupting the
  currently usable texture.
- `FStreamableTextureResource::FinalizeStreaming` atomically replaces the
  physical texture and calls `UpdateTextureReference`.
- `FRHITextureReference` is the stable indirection. Its bindless handle remains
  stable while the referenced physical texture changes.
- Temporary streaming memory and per-frame request issue are budgeted separately
  from final resident texture memory.
- Unreal can also use partially resident/tiled textures, but treats that as a
  separate RHI path.

Decision classification:

- **Copy:** wanted/requested/resident state separation, one in-flight update per
  texture, intermediate-resource transaction, cancellation rollback, and
  finalize-after-success boundary.
- **Adapt:** stable texture reference into a Vanguard-owned stable bindless
  identity primitive.
- **Reject for V1:** Unreal virtual texture page systems, shader page tables,
  and the full generic render-asset streaming manager.

### 2.4 Phase 0 Synthesis

The recommended Vanguard architecture is a hybrid:

```text
Vanguard ResourcePipeline
  loads TextureResource metadata and owns dependency lifetime

Vanguard ResourceRangeReadQueue
  reads/verifies required VTEX subresources from loose files or packages

TextureResidencyManager
  owns stable texture identity, wanted/requested/resident mip state,
  one active transition, staging admission, and fence-safe retirement

Texture mip transition
  builds a complete candidate GPU texture, uploads new mips,
  preserves shared resident mips, then commits atomically

stable texture-residency entry
  resolves to the committed physical texture's immutable descriptor;
  its stable table index is stored in GpuMaterialResource
```

RED supplies the simple resident-tail and cooked-resource shape. Unreal supplies
the robust transition and stable-reference model. Vanguard's existing generic
resource, package, streaming, RHI lifetime, and GPU material systems remain the
owners of their respective seams.

### 2.5 Locked Phase 0 Decisions

- Ordinary mip streaming is the V1 target. No virtual-texture shader lookup is
  introduced.
- The mip tail is the first usable GPU installation and must complete before a
  texture becomes bindless-ready.
- A material-visible logical texture index must remain stable across mip
  promotion and demotion. Phase 1 superseded the Phase 0 assumption that the
  physical descriptor index itself must be stable.
- An incomplete promotion must never replace the currently usable texture.
- Texture IO reuses the generic range source and package path.
- CPU staging and final GPU residency have separate budgets.
- Whole-allocation RHI residency and logical mip residency remain separate.
- GPU material texture resources will contain stable texture-residency indices;
  non-texture bindless resources may continue to contain direct descriptor
  indices where appropriate.
- V1 must support the `VTEX` shapes already promised by the format: 2D, cube,
  arrays, and 3D, subject to RHI capability validation.

## 3. Phase 1 — Stable Bindless Identity and Mip Transition

### Result

```text
Study work: complete
Exit gate: passed
Production changes: Phase 3A implemented
Selected identity: stable GpuTextureResidencyHandle
Selected physical binding: immutable descriptor per installed texture version
Selected allocation: non-sparse texture beginning at first resident asset mip
Required RHI descriptor rewrite feature: none
Next work: study the metadata/resource-loading and mip-tail installation seam
```

### Purpose

Phase 1 will resolve the single architecture question that blocks safe
implementation:

```text
How does one stable material-visible texture identity continue to resolve to the
current physical texture when the resident mip range changes?
```

This is deliberately one focused source study, not another broad engine survey.

### 3.1 Vanguard Descriptor and Lifetime Findings

Evidence:

- `source/rhi/nvrhi/src/common_backend.cpp:667-751`
- `source/rhi/nvrhi/src/common_backend.cpp:1583-1790`
- `source/rhi/nvrhi/src/common_backend.cpp:2388-2583`
- `source/rhi/nvrhi/src/common_backend.cpp:3123-3205`
- `external/nvrhi/upstream/src/d3d12/d3d12-resource-bindings.cpp:827-904`
- `source/rhi/nvrhi/src/resource_lifetime.cpp:815-952`

A Vanguard descriptor slot has these states:

```text
Free -> Allocated -> Populated -> Retiring -> Free(next generation)
```

`WriteDescriptor` only accepts `Allocated`. A successful write retains the
referenced texture and makes the slot `Populated`. `RetireDescriptor` waits for
the supplied graphics, compute, and copy fences before clearing the native
descriptor, releasing the referenced resource, increasing the descriptor
generation, and recycling the slot.

This is a sound immutable-installation model. It should remain the default.

NVRHI's D3D12 `writeDescriptorTable` writes the CPU descriptor and immediately
copies it into one static shader-visible heap. It does not create a new GPU heap
version. Overwriting a populated entry while an older command list may use that
heap would therefore be unsafe. Merely relaxing Vanguard's `Allocated` check
would create a race rather than a texture-reference feature.

Command lists retain directly recorded texture copy/upload operands through
submission and record their queue fence as last use. Descriptor slots retain
indirectly reached textures separately. These existing lifetime mechanisms are
sufficient when each installed texture version receives its own descriptor.

`CommandListType::CopySync` maps to the graphics queue in Vanguard;
`CopyAsync` maps to the copy queue. Existing GPU Scene uploads use `CopySync`,
so their residency updates are ordered with graphics work. Async compute readers still
require an explicit join/cutover boundary.

### 3.2 Unreal Bindless Update Findings

Additional evidence:

- `Engine/Source/Runtime/D3D12RHI/Private/D3D12TextureReference.cpp:5-124`
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12BindlessDescriptors.cpp:514-571`
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12BindlessDescriptors.cpp:700-805`
- `Engine/Source/Runtime/Engine/Private/Rendering/StreamableTextureResource.cpp:244-269`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DUpdate.cpp:62-179`
- `Engine/Source/Runtime/Engine/Private/Streaming/TextureStreamOut.cpp:1-208`

Unreal's stable bindless handle is safe because its D3D12 backend versions the
GPU-visible descriptor heap. `UpdateDescriptor` changes a shared CPU heap,
marks contexts for a heap refresh, requests a new active GPU heap, copies dirty
indices into that heap, and defers the old heap until the GPU is finished.

This proves two things:

1. a stable physical descriptor handle is possible;
2. it requires backend heap-versioning machinery that Vanguard/NVRHI does not
   currently have.

Copying only Unreal's public `TextureReference` API without copying that heap
lifetime mechanism would be incorrect. Adding complete descriptor-heap
versioning is disproportionate to Vanguard's immediate texture-residency need.

Unreal's mip transaction remains valuable independently of its descriptor
implementation:

```text
allocate/reallocate candidate
  -> obtain new mip data
  -> copy shared mips
  -> finalize on render authority
  -> switch stable reference
  -> release intermediate state
```

Stream-out uses the same allocator/finalize boundary, and cancellation releases
the candidate without changing the currently resident resource.

### 3.3 RED Confirmation

Additional evidence:

- `common/renderer/src/renderBlobUpload.cpp:40-118`
- `common/renderer/src/renderTextureBase.cpp:14-218`
- `common/resourceMaterial/src/bitmapTexture.cpp:258-312,454-474`

RED dispatches cooked texture blobs by 2D, 3D, array, and cube shape. It chooses
a first resident mip, creates a physical texture whose extent begins at that
mip, uploads the remaining cooked chain, and stores the resulting `TextureRef`
behind a render-resource wrapper that permits controlled replacement.

This confirms the non-sparse allocation direction. RED does not provide the
bindless lifetime solution; Vanguard must adapt the wrapper identity into its
own GPU-visible residency entry.

### 3.4 Selected Identity Model

Phase 1 rejects both risky descriptor rewriting and full-chain upfront
allocation. Vanguard will use two identities:

```text
Stable logical identity                 Immutable installed version

TextureResidencyHandle                 TextureInstallation
  index                                  rhi::TextureRef
  generation                             rhi::DescriptorHandle
                                         firstResidentMip
GpuTextureResidency[index]               residentMipCount
  current descriptor                     installationRevision
  current first mip
  current mip count
  generation/flags
```

Every physical installation receives a new write-once descriptor. The stable
GPU table entry changes from the old descriptor index to the new descriptor
index only after the candidate texture is complete.

Proposed V1 GPU ABI:

```cpp
struct alignas(16) GpuTextureResidency
{
    u32 descriptor;
    u32 firstResidentMip;
    u32 residentMipCount;
    u32 generationAndFlags;
};
```

Exact flag packing is an implementation detail to validate with the CPU/shader
ABI tests. The semantic fields are locked.

`GpuTextureResidency` is an independently owned GPU Scene definition table, not
a parallel table attached to `GpuMaterialResource`. A
`GpuTextureResidencyHandle` is generational on the CPU. Texture dependencies
keep its allocation alive while a material references it, so the GPU material
record only needs the stable table index.

For a texture parameter, the first word of `GpuMaterialResource` becomes a
generic resolved resource value:

```text
Texture                 -> GpuTextureResidency index
Buffer/other bindless   -> direct descriptor index where appropriate
Sampler                 -> existing sampler descriptor field
```

`type` determines how the value is interpreted. This avoids creating reverse
lists of every material that must be rewritten whenever one texture changes.

The shader cost is one GPU Scene metadata read when resolving a texture
parameter, followed by ordinary bindless sampling. It is not virtual texturing:
there is no page address translation per texel, no physical-page lookup, and no
shader-visible mip page table.

### 3.5 Selected Non-Sparse Physical Layout

An installed texture begins at its first resident asset mip:

```text
asset mips:       0 1 2 3 4 5 6 7
tail install:             [4 5 6 7]  physical mips 0..3
promoted install:     [2 3 4 5 6 7]  physical mips 0..5
```

For target first mip `T`:

```text
physical extent = asset extent at mip T
physical mip count = asset mip count - T
physical mip P represents asset mip T + P
```

Promotion creates a larger candidate texture, uploads newly requested asset
mips, and copies the lower-resolution mips shared with the current
installation. Demotion creates a smaller candidate and copies only the retained
mips. Once the old installation retires, its actual GPU allocation is released,
so stream-out produces real VRAM savings.

For cubes, RHI array slice is `arrayLayer * 6 + face`. For ordinary arrays it is
the array layer. A 3D mip is one subresource whose depth belongs to that mip.
Every shared subresource is copied explicitly because Vanguard's RHI copy API
copies one selected source/destination subresource per call.

Candidate textures require at least:

```text
TextureUsage::ShaderResource
TextureUsage::CopySource
TextureUsage::CopyDestination
```

The manager should create an empty texture and use command-list `WriteTexture`
rather than `CreateTexture(initialData)`: explicit recording returns a
submission fence and therefore supports a real commit transaction.

`TextureDesc::virtualResource` is not used for this V1 path. It currently means
an unbound/placed resource requiring explicit heap binding and aliasing; it is
not a public tiled-mip mapping system.

### 3.6 Ownership and State Machine

CPU ownership is locked as:

```text
TextureResourceObject
  immutable VTEX metadata
  owned generic ResourceRangeReadQueue/source
  strong dependencies and source lifetime

TextureResidencyManager
  stable records and generations
  GPU TextureResidency table allocation
  one active transition per texture
  current installation
  pending retirement installations

TextureTransition
  target first mip
  coalesced range-read interests
  bounded staging bytes
  candidate texture and descriptor
  upload/residency-update fences
```

State machine:

```text
MetadataReady
  -> TailReading
  -> CandidatePreparing
  -> CandidateUploading
  -> UploadPending
  -> ResidencyPublishing
  -> BindlessReady

BindlessReady
  -> PromotionReading | DemotionPreparing
  -> CandidatePreparing
  -> CandidateUploading
  -> UploadPending
  -> ResidencyPublishing
  -> BindlessReady

failure before residency-update submission
  -> retire candidate only
  -> preserve prior BindlessReady installation

successful residency-update submission
  -> candidate becomes authoritative
  -> retire old installation after the update/use fence set
```

Only one transition may own a candidate for one texture. A new wanted mip can
coalesce with, supersede before GPU submission, or wait behind that transition;
it must not create competing candidates.

Initial tail failure leaves the texture not bindless-ready. Consumers that are
allowed to proceed must select one explicit global missing-texture residency
entry. This is genuine fallback selection, not a residency state named
"fallback."

### 3.7 Commit and Retirement Rules

Installation commit order is:

```text
1. Read and verify every required candidate subresource.
2. Create the candidate physical texture.
3. Upload new mips and copy shared mips on a graphics-ordered CopySync list.
4. Submit and wait/poll for candidate upload completion.
5. Allocate and populate a new immutable texture descriptor.
6. Upload the new GpuTextureResidency record on graphics-order authority.
7. After successful residency-update submission, make the candidate current.
8. Retire the old descriptor and texture after every queue that could have
   consumed the old residency record is past the cutover.
```

The descriptor may be allocated before upload completion, but it must never be
published before completion. Delaying descriptor allocation reduces descriptor
pressure and is the preferred V1 order.

The residency manager must receive the renderer's full graphics/compute/copy
safe-after fence set for old installations. It cannot guess indirect descriptor
use from the texture alone. A graphics table update does not by itself exclude
an asynchronous compute reader; graph/runtime authority must provide a join or
completed compute cutoff before cutover.

Descriptor exhaustion is non-destructive. Promotion/demotion keeps the old
installation and retries later. Initial tail installation remains not ready.
Every transition temporarily needs one additional descriptor until the prior
version is retired, so capacity accounting must reserve update headroom.

RHI `MakeResident`/`Evict` is not called as the logical mip transition. The
candidate allocation enters the normal RHI working set when recorded/submitted;
stream-out frees memory by fence-safe release of the larger old texture.

### 3.8 Failure and Cancellation Contract

- IO/decode/verification failure releases only the transition's staging and
  request interests.
- Candidate creation or recording failure releases the candidate texture.
- An allocated but unwritten descriptor is retired from `Allocated` state.
- A populated but unpublished descriptor is retired after its upload/resource
  fences; the stable GPU entry remains unchanged.
- GPU residency table upload failure leaves the old entry authoritative.
- Cancellation is accepted until residency-update submission. After submission,
  completion and retirement must finish; it is a commit, not a cancellable
  request.
- Stale texture handles, transition generations, callbacks, and table upload
  owners are rejected.
- Resource eviction cancels pre-commit work, then retires the current GPU entry,
  descriptor, and texture behind the complete queue fence set.

### 3.9 Non-Bindless Capability Path

If bindless resources are unsupported, the residency manager may still produce
a current `rhi::TextureRef` for explicit-binding consumers. It must not publish
a forged GPU descriptor index. The GPU material bindless path reports
`Unsupported`; a later explicit-binding material path may consume the texture
reference separately.

### 3.10 Answers to the Phase 1 Questions

1. Vanguard uses stable GPU table indirection plus immutable descriptors. It
   does not add `TextureReference` or populated-descriptor rewrite in V1.
2. NVRHI can physically rewrite a descriptor, but its current single visible
   heap cannot make that safe under frames in flight. Vanguard will not expose
   that operation.
3. Non-sparse promotion/demotion creates a different-sized texture and copies
   shared mips.
4. `TextureResidencyManager` owns the stable record; one `TextureTransition`
   owns staging and the candidate; one `TextureInstallation` owns each committed
   physical texture/descriptor pair.
5. The prior installation retires only after the residency update and all
   relevant graphics/compute/copy use fences.
6. 2D, cube, arrays, and 3D use the explicit asset-to-RHI subresource mapping in
   section 3.5.
7. Without bindless support, explicit texture references remain possible but
   the GPU material path is unsupported.

### Studied Vanguard source corpus

- `source/rhi/include/vanguard/rhi/rhi.hpp`
- `source/rhi/include/vanguard/rhi/rhi_backend.hpp`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp`
- `source/rhi/nvrhi/src/common_backend.cpp`
- `source/rhi/nvrhi/src/d3d12_backend.cpp`
- `source/rhi/nvrhi/src/resource_lifetime.cpp`
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp`
- `source/textures/include/vanguard/textures/textures.hpp`
- `source/streaming/include/vanguard/streaming/resource_source.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp`

### Studied Unreal source corpus

- `Engine/Source/Runtime/RHI/Public/RHITextureReference.h`
- `Engine/Source/Runtime/RHI/Private/RHITextureReference.cpp`
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12TextureReference.cpp`
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12BindlessDescriptors.h/.cpp`
- `Engine/Source/Runtime/Engine/Private/Rendering/StreamableTextureResource.cpp`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DUpdate.h/.cpp`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DStreamIn.h/.cpp`
- `Engine/Source/Runtime/Engine/Private/Streaming/Texture2DStreamOut.h/.cpp`

### Studied RED source corpus

- `common/renderer/src/renderTextureBase.h/.cpp`
- `common/renderer/src/renderTexture.h`
- `common/resourceMaterial/include/bitmapTexture.h`
- `common/resourceMaterial/src/bitmapTexture.cpp`
- the exact `UploadBlobTexture` implementation reached from
  `CRenderInterface`, if present in this snapshot

RED scope is intentionally narrow because its inspected async streaming body is
incomplete.

### Phase 1 question checklist (answered in section 3.10)

1. Should Vanguard add an RHI `TextureReference`, add a fence-safe populated
   descriptor replacement operation, or use another stable indirection?
2. Can NVRHI update one bindless table entry in place while prior submissions
   safely retain the old resource, and what fences/refcounts are required?
3. For non-sparse V1 textures, does promotion use async reallocation or create a
   new smaller/larger texture and copy shared mips?
4. What exact transaction owns staging bytes, candidate texture, upload fences,
   current texture, stable residency entry, immutable descriptor, cancellation,
   and rollback?
5. When may the old texture be released, and when may a descriptor slot be
   retired or recycled?
6. How are 2D, cube, array, and 3D subresources mapped without special-case
   ambiguity?
7. What is the safe degraded path when bindless resources are unsupported?

### Required Phase 1 output

Phase 1 produced the locked state machine and ownership model above. Its
original shorthand was:

```text
MetadataReady
  -> TailReading
  -> CandidateUploading
  -> CommitPending
  -> BindlessReady

BindlessReady
  -> PromotionReading / DemotionPreparing
  -> CandidateUploading
  -> CommitPending
  -> BindlessReady

any incomplete transition
  -> rollback to previous usable BindlessReady state
```

The study also defined the minimal RHI change (none) and the first
implementation slice. It did not proceed into mip-demand policy, feedback, or
material loading.

### Exit gate

The original Phase 1 identity and lifetime study gate is passed:

- stable bindless identity across physical texture replacement is proven;
- residency-update and retirement fence ownership is explicit;
- the non-sparse V1 allocation strategy is selected;
- cancellation cannot invalidate the last usable mip tail;
- no new generic IO/package/budget subsystem is proposed;
- the implementation boundary is small enough to build and test independently.

The implementation later exposed an incorrect per-texture GPU submission
boundary. Therefore the production foundation is not complete until the Phase
1A exit gate below passes; Phase 1A supersedes any earlier implication that the
current installation path is ready to build upon.

### 3.11 Implemented Foundation

The initial Phase 1 implementation is intentionally foundational and contains
no file IO, mip policy, material loading, or rendering:

1. CPU/shader `GpuTextureResidency` and `GpuTextureResidencyHandle` ABI.
2. The independent `TextureResidency` GPU Scene table and layout version 6.
3. `TextureResidencyManager`, which allocates one stable entry and
   stages and commits immutable descriptor-backed installations.
4. Real-D3D12 tests for descriptor headroom, stale handles, installation order,
   rollback, and multi-queue retirement using synthetic textures.
5. The minimum typed `GpuMaterialResource` interpretation required by the new
   table; material resource loading remains a later cycle.

No RHI descriptor rewrite API is added. No `.vtex` resource loader or mip reader
is added in this slice; those begin only after the identity/installation
foundation is verified.

## 3A. Phase 1A — Batched Residency Installation Refactor

Phase 1A corrects one foundation mistake before texture acquisition work
continues: `TextureResidencyManager::Allocate` and `Install` currently open and
submit a one-record `GpuSceneUploader` batch themselves. `GpuFence` is only a
lightweight queue/value token, but the current API still creates one command-list
submission and one new queue timeline value per texture operation. That is not
an acceptable production boundary.

The transferable RED rule is centralized GPU upload scheduling: an individual
resource prepares its data and state, while renderer upload authority controls
recording and submission. RED has no equivalent bindless texture-residency
table to copy literally, so Vanguard must apply that rule through its existing
batched `GpuSceneUploader` and renderer-owned GPU Scene update boundary.

### 3A.1 Locked Boundary

```text
texture residency manager
  validate candidate installation
  allocate/write immutable descriptor
  retain pending CPU installation
  expose one GPU Scene upload request and payload
  commit or cancel after the shared batch result

renderer-owned GPU Scene update batch
  collect requests from texture residency and other GPU Scene producers
  call GpuSceneUploader::Begin once
  let producers write their assigned reservations
  call Complete for every live reservation
  call Submit once
  return one shared completion fence
```

`TextureResidencyManager` must never call `GpuSceneUploader::Begin`, `Submit`,
or `Cancel` from `Allocate` or `Install`. It is a batch contributor, not a GPU
submission owner.

### 3A.2 Allocation and First Installation

`Allocate` becomes CPU/GPU-identity allocation only:

```cpp
bool Allocate(
    GpuTextureResidencyHandle& handle,
    TextureResidencyFailure* failure = nullptr);
```

It no longer returns `initializationCompletion` and does not upload a synthetic
not-ready record. The `GpuSceneLifetime` allocation remains in `Allocated`
state until the first real installation is submitted. Therefore:

- an allocated handle is stable but not shader-usable;
- the first installation is the allocation's initial GPU Scene upload;
- retiring before first installation cancels the allocation rather than
  retiring an active GPU record;
- the missing-texture entry remains the explicit rendering fallback; an
  uninstalled handle must never be consumed by a shader.

This removes an otherwise unnecessary submission before the texture has any
physical installation.

### 3A.3 Installation Admission

`Install` becomes a non-submitting admission operation. It validates the
complete candidate texture, allocates and writes its immutable descriptor, and
stores one pending installation on the stable slot. It returns identity for the
accepted operation, not a fence:

```cpp
struct TextureInstallationTicket
{
    GpuTextureResidencyHandle texture;
    u32 revision = 0;
};

bool Install(
    GpuTextureResidencyHandle handle,
    const TextureInstallationDesc& installation,
    TextureInstallationTicket& ticket,
    TextureResidencyFailure* failure = nullptr);
```

The current installation remains authoritative until the pending operation's
shared GPU Scene batch submits successfully. An initial pending installation
leaves public residency state `Allocated`; a replacement leaves it
`BindlessReady`. Internal pending state must not falsely report that the new
descriptor is usable.

V1 permits one pending installation per stable texture. A second installation
for the same texture is rejected as busy; supersession belongs to the later mip
transition controller, where candidate byte and upload ownership are known.

### 3A.4 Composable Batch Contributor API

The manager exposes a sealed batch token and producer-style operations matching
the existing `RenderSceneGpuPublisher` split:

```cpp
struct TextureResidencyBatch;

bool PrepareBatch(
    u32 maximumInstallations,
    TextureResidencyBatch& batch,
    ...);
bool BuildUploadRequests(
    const TextureResidencyBatch& batch,
    DynamicArray<GpuSceneUploadRequest>& requests,
    ...);
bool WriteBatch(
    const TextureResidencyBatch& batch,
    ArraySpan<const GpuSceneUploadReservation> reservations,
    ...);
void AcceptSubmittedBatch(
    const TextureResidencyBatch& batch,
    rhi::GpuFence sharedCompletion) noexcept;
bool RetryBatch(const TextureResidencyBatch& batch, ...);
bool DiscardBatch(const TextureResidencyBatch& batch, ...);
```

Names may be adjusted to existing container spelling during implementation,
but the responsibilities are locked:

- `PrepareBatch` freezes at most the renderer-supplied remaining contribution
  capacity and leaves excess installations pending;
- `BuildUploadRequests` appends exactly one table update per frozen texture;
- `WriteBatch` writes final `GpuTextureResidency` values directly into the
  assigned mapped reservations;
- the renderer-owned coordinator completes reservations and submits the shared
  uploader batch;
- `AcceptSubmittedBatch` runs only after successful submission, is mechanically
  infallible, and makes every candidate current under the same completion token;
- `RetryBatch` returns frozen candidates to the front of the pending queue after
  transient shared-uploader pressure;
- `DiscardBatch` permanently releases candidate descriptors/resources while
  preserving the prior usable installation.

An empty texture batch contributes no request and causes no submission.

### 3A.5 Shared Completion and Retirement

One batch completion token may cover any number of texture entries:

```text
texture A update --+
texture B update --+--> one GPU Scene submission
texture C update --+        GpuFence { queue, value }
```

The completion token belongs to the renderer-owned batch result. It is not a
field of every `TextureInstallationTicket`. The coordinator uses it to order
later compute/copy consumers of newly installed entries.

`AcceptSubmittedBatch` records the shared completion as part of the old installation's
retirement floor. Later `SealRetirements` still supplies the renderer-owned
graphics/compute/copy cutover for all prior consumers. Descriptor or texture
retirement must satisfy both:

```text
residency-table update completion
and
last possible old-installation consumer on every relevant queue
```

The manager must not infer those consumer fences per texture.

### 3A.6 Failure and Capacity Rules

- Descriptor allocation/write failure rejects only that installation before it
  enters a frozen batch.
- Batch preparation freezes only work that fits the supplied contribution cap.
- Transient shared-uploader admission/submission pressure uses `RetryBatch` and
  preserves candidate descriptors and textures.
- Permanent planning or reservation failure uses `DiscardBatch`; every previous
  current installation remains authoritative.
- No CPU slot becomes `BindlessReady`, changes revision, or increments successful
  installation statistics before successful shared submission.
- Pending-installation and frozen-batch capacities are explicit and bounded.
- `Shutdown`, `Retire`, and slot recycling reject or cancel outstanding pending
  and frozen work deterministically; no descriptor or texture is leaked.
- Stale batch serials, stale handles, mismatched reservation spans, and duplicate
  writes fail before submission. Post-submit acceptance has no recoverable
  failure path and asserts programmer misuse.

### 3A.7 Implementation Order

1. Remove the private single-record `UploadResidency` helper.
2. Remove `initializationCompletion` from `Allocate` and defer initial GPU Scene
   activation until the first installation batch.
3. Replace `TextureInstallationResult` with a fence-free
   `TextureInstallationTicket`.
4. Add pending/frozen installation ownership and bounded configuration.
5. Add the composable prepare/request/write/accept/retry/discard batch API.
6. Connect the batch through renderer-owned GPU Scene update scheduling; do not
   add a texture-private submit or fence timeline.
7. Update retirement bookkeeping so the shared batch completion is retained as
   a retirement floor.
8. Update tests and statistics, then remove all assertions that assume one
   uploader batch per texture.

### 3A.8 Exit Gate

Phase 1A is complete only when tests prove:

- allocating many texture handles performs zero RHI submissions;
- installing many textures performs zero submissions until the owner flushes;
- at least two initial installations and one replacement share one
  `GpuSceneUploader::Submit` and the same completion token;
- the batching seam can coexist with another GPU Scene producer in the same
  request array rather than opening a competing uploader batch;
- failed planning, writing, or submission preserves every previous usable
  installation and releases all new candidates;
- a first-install failure leaves the handle allocated but not shader-usable;
- retirement cannot reclaim an old descriptor/resource before both the shared
  update completion and the renderer-supplied multi-queue cutover;
- uploader statistics scale with flushed batches, not texture count;
- there is no `GpuSceneUploader::Submit` call anywhere in
  `texture_residency.cpp`.

### 3A.9 Implemented Result

Phase 1A is implemented. `TextureResidencyManager` no longer retains a
`GpuSceneUploader` pointer, and neither `Allocate` nor `Install` records or
submits GPU work. The implemented boundary is:

```text
Allocate
  -> stable inactive GPU Scene allocation

Install
  -> pending descriptor-backed candidate

PrepareBatch / BuildUploadRequests / WriteBatch
  -> one composable contribution to an owner-supplied request/reservation span

owner completes and submits the shared GpuSceneUploader batch

AcceptSubmittedBatch(shared completion)
  -> candidate becomes current
  -> previous installation enters fenced retirement
```

The focused real-D3D12 test proves that texture installations can be capped to
one contribution while excess work remains pending, retried without losing the
candidate, and combined with an independently owned GPU instance update. It
also covers replacement, descriptor exhaustion, explicit discard, an
uninstalled handle remaining unusable, immediate cancellation of that
uninstalled identity, and multi-queue retirement.

The batch path uses explicit pending and frozen slot-index arrays; preparing a
batch is proportional to pending installation count rather than
`maximumTextures`. There is no `GpuSceneUploader::Submit` call and no private
uploader ownership in `texture_residency.cpp`.

The Phase 1A unit-test gate passes. The production audit below found and closed
four additional batching and scaling seams.

### Phase 1A Corrective Audit

The fundamental ownership model is sound:

- one stable generational texture identity;
- one immutable descriptor per physical installation;
- GPU Scene table writes contribute to a renderer-owned shared batch;
- the batch has one completion fence, not one fence per texture;
- the previous installation remains authoritative until replacement submission;
- descriptors and texture references retire only after multi-queue cutover.

The audit found no duplicate texture uploader, private submission queue, or
per-texture fence allocation. Pending and frozen slot-index arrays also keep
normal installation batching proportional to pending work rather than texture
capacity.

Four production corrections were required before wiring the manager into the
renderer:

1. **Bound the frozen contribution.** `PrepareBatch(maximumInstallations, ...)`
   now freezes only the caller's remaining shared-batch capacity and preserves
   pending FIFO order for later batches.
2. **Separate retry from discard.** `RetryBatch` restores the frozen work ahead
   of newer pending work without releasing its descriptor or texture;
   `DiscardBatch` is the explicit permanent rollback path.
3. **Make post-submit acceptance non-failing.** `AcceptSubmittedBatch` is a
   `void noexcept` terminal step. Reservation and batch validation remain in
   pre-submit operations; post-submit misuse is an assertion, not a second
   recoverable transaction.
4. **Track retiring identities explicitly.** A dense `retiringSlots` list now
   drives sealing and collection, so both paths are proportional to actual
   retiring identities rather than `maximumTextures`.

The renderer-owned coordinator must also propagate the shared upload completion
as a copy-to-consumer dependency before graphics or compute reads the new table
record. This remains one batch dependency; exposing or polling a fence per
texture would recreate the Phase 1 error.

These bounded Phase 1B corrections are implemented without changing the
residency-table ABI or descriptor ownership model. The focused D3D12 target
builds with warnings-as-errors and its runtime test passes.

## 4. Phase 2 Restudy — Bounded VTEX Acquisition

### Restudy Result

The original Phase 2 payload design is rejected. It proposed issuing every
subresource read in a mip range, retaining every completed read, and reporting
success only when the whole range was resident in CPU memory. That can deadlock
Vanguard's existing FIFO staging admission:

```text
staging budget = 256 MiB
requested tail = 400 MiB

first reads complete and retain 256 MiB
later reads remain queued waiting for budget
aggregate waits for later reads before releasing the first reads
```

`CoalescedResourceReadRequest` releases its admitted bytes only when the final
request reference is reset or destroyed. Therefore an aggregate whose retained
admission exceeds the global budget can never finish. It also creates an
unacceptable CPU-memory spike for large non-streamable textures, cube arrays,
or a full-chain initial load.

Phase 2 must instead provide a **bounded acquire/consume/release stream**. GPU
visibility remains atomic later, but CPU bytes do not all need to coexist.

### 4.1 Existing Foundation Kept

The following decisions remain correct:

- `TextureResourceObject` means parsed metadata plus a pinned source generation,
  not GPU readiness.
- `ResourcePipeline` and `ResourceStreamer` resolve both loose VTEX files and
  packaged resources.
- metadata loading reads only the serialized-document prefix through `META`.
- `TextureFile` remains the immutable canonical map from asset mip/layer/face to
  byte offset, byte size, pitches, dimensions, flags, and SHA-256 digest.
- runtime consumes only cooked `DirectGpuUpload` subresources; no runtime image
  transcoding or block compression is introduced.
- generic streaming continues to own package decoding, retry classification,
  source-generation pinning, cancellation, and staging accounting.

The VMSH metadata loader already proves the prefix-read pattern. Phase 2 should
extract a small serialized-document-prefix helper only if reuse is clean; it
must not duplicate the resource registry or package machinery in `textures`.

### 4.2 Runtime Objects

```text
TextureResourceObject
  immutable TextureFile
  pinned ResourceSource generation
  TextureSubresourceSource
    ResourceRangeReadQueue

TextureMipAcquisition
  strong TextureResourceObject ownership
  requested [firstAssetMip, onePastLastAssetMip)
  canonical list of required subresource indices
  next unconsumed subresource
  at most one live TextureMipReadWindow

TextureMipReadWindow
  bounded set of exact verified reads
  immutable subresource views after completion
  explicit Release/Reset after the consumer copies them
```

`TextureMipAcquisition` describes the complete logical transition, but it does
not own the complete transition's bytes simultaneously. Only one window may be
live per acquisition in V1. This makes byte lifetime obvious and prevents a
caller from accumulating completed windows accidentally.

### 4.3 Window Planning and Admission

Before issuing a window, the source adapter calls `ResourceSource::PlanRead`
for each candidate record and computes the same conservative admission charged
by `ResourceRangeReadQueue`:

```text
loose source admission   = requested output bytes
package source admission = requested output bytes + stored bytes + decoded bytes
```

The window ends when either limit would be exceeded:

- configured maximum admitted bytes per texture window;
- configured maximum subresource requests per window.

Both limits must be non-zero, overflow checked, and chosen below the global
streaming staging budget. A single subresource whose required admission exceeds
the global budget fails explicitly; silently bypassing admission or falling back
to synchronous whole-file I/O is forbidden.

The sum of all reads retained by one window must fit its admission cap. Other
resource traffic may delay it, but after that traffic releases its reservations,
the whole window can be admitted and cannot deadlock itself.

Exact verified reads remain the V1 primitive. Production packages already store
one independently decodable segment per texture subresource, so exact reads
avoid decoding unrelated mip data. Loose editor files may issue several small
reads, but the request-count cap prevents an unbounded burst. Adjacent loose-file
read gathering is a possible generic streaming optimization later; it is not a
reason to add a texture-only I/O scheduler now.

### 4.4 Acquire, Consume, Release

The operation advances as follows:

```text
plan next bounded canonical window
  -> issue exact ReadVerified requests
  -> wait/poll until every read in this window finishes
  -> expose immutable views to one consumer
  -> consumer copies/records every view into its destination
  -> ReleaseWindow drops request interests and staging bytes
  -> continue with the next window
```

Each exposed view contains:

```text
asset mip / array layer / face
width / height / depth
row pitch / slice pitch
verified byte span
```

There is no second concatenated tail buffer. Package reads may still require
stored and decoded scratch internally, and the later RHI uploader may copy into
mapped upload memory; those are explicit backend costs, not hidden texture-side
copies. A future generic read-into-caller-buffer API could reduce a copy, but it
must be designed in the generic streaming layer and is not required for this
phase.

### 4.5 Failure and Atomicity

All reads in one window must verify before any of that window is handed to the
consumer. On failure or cancellation:

- all live interests in the window are released;
- no later window is issued;
- the acquisition records a terminal structured failure;
- any candidate GPU texture owned by the later installation phase is discarded;
- the currently authoritative texture installation is unchanged.

The complete mip transition is still atomic at the GPU-facing boundary:

```text
many bounded CPU windows
  -> populate one private candidate texture
  -> all required windows uploaded successfully
  -> upload completion and GPU Scene table batch
  -> candidate becomes authoritative
```

Thus bounded CPU streaming does not expose partially initialized textures to
materials. Phase 2 itself performs no RHI allocation, descriptor write, GPU
Scene update, or residency installation.

### 4.6 Mip Range and Subresource Rules

The initial acquisition range is:

```text
[TextureFile::GetMipTailFirstLevel(), TextureFile::GetMipCount())
```

For a non-streamable texture this begins at mip zero, but it is still consumed
through bounded windows rather than one full-chain CPU allocation. General
promotion later uses the same acquisition primitive for another contiguous
asset-mip range.

Every layer and face in every selected mip is mandatory. Canonical order is
asset mip, then array layer, then cube face. A 3D mip remains one subresource
whose depth slices are described by its depth and slice pitch.

The later physical mapping remains:

```text
physical mip        = asset mip - candidate first asset mip
ordinary array slice = array layer
cube array slice     = array layer * 6 + face
3D array slice       = 0
```

### 4.7 RED and Unreal Findings

RED provides a useful physical layout rule: it creates a smaller physical
texture whose mip zero corresponds to the chosen resident asset mip, and uses
precomputed cooked offsets. It also clears the large CPU blob after texture
creation. The inspected RED branch does **not** provide a scheduler to copy:
its `HasStreamingSource`, `InitStreaming`, and recalculation paths are stubs.
RED therefore validates the physical representation, not Vanguard's transition
or memory-admission algorithm.

Unreal provides the stronger transition lesson. Its stream-in update owns the
intermediate texture and per-mip destination memory, issues I/O into those
destinations, copies shared mips, and calls `FinalizeStreaming` only after the
update succeeds. The streaming manager separately limits temporary memory and
flushes/adopts work in bounded amounts. Vanguard should preserve those
principles:

- private transition state;
- explicitly budgeted temporary bytes;
- destination bytes released as soon as their upload step no longer needs them;
- one final authority switch;
- cancellation discards the candidate, not the current texture.

Vanguard does not copy Unreal's legacy RHI lock APIs or virtual-texture path.
Its exact-digest verification, package generation pinning, and shared staging
accounting remain Vanguard-specific strengths.

### 4.8 Locked Phase 2 Decisions

1. Metadata readiness and GPU readiness remain separate states.
2. Metadata acquisition reads only the prefix through `META`.
3. One acquisition represents a contiguous asset-mip range and retains one
   immutable source generation.
4. CPU payload is streamed through bounded windows; no whole-transition byte
   aggregate is allowed.
5. Only one window may be live per acquisition in V1.
6. Window admission is planned with overflow-safe `PlanRead` accounting and a
   request-count cap.
7. Each window succeeds only after all of its exact reads verify.
8. The consumer must explicitly release a window after copying/recording it.
9. Every selected mip/layer/face is mandatory and processed canonically.
10. Generic streaming owns retries, cancellation, package decode, coalescing,
    source pinning, and staging budgets.
11. Runtime accepts cooked direct-upload bytes only.
12. Phase 2 performs no RHI allocation or residency-table mutation.

### Phase 2 Implementation Slice

```text
textures runtime
  TextureResourceObject
  TextureResourceLoader
  TextureSubresourceSource
  TextureMipAcquisition
  TextureMipReadWindow

tests
  loose and packaged metadata-prefix loading
  canonical 2D/array/cube-array/3D window membership
  package PlanRead admission calculation
  range larger than one window completes over multiple release cycles
  full-chain non-streamable texture larger than the window budget completes
  proof that retained staging never exceeds the configured texture-window cap
  cancellation at queued, reading, ready, and between-window states
  retryable I/O versus permanent digest failure
  pinned generation across reload
  single-subresource-over-budget rejection
```

The test that uses a logical mip range larger than the staging budget is the
critical regression test. The old aggregate design would hang; the bounded
window design must make forward progress.

### Phase 2 Exit Gate

Phase 1B has closed the shared-batch issues listed above, so Phase 2 is ready to
implement. Phase 2 itself is complete when metadata can load through the normal
resource pipeline and a simulated consumer can drain an arbitrarily large valid
mip range with bounded retained staging and no RHI calls.

### Phase 2 Implementation Result

Phase 2 is implemented.

- `TextureResourceLoader` registers VTEX with the normal `ResourcePipeline`,
  asynchronously reads only the document header, section table, and metadata
  prefix, and preserves the resolved loose/package `ResourceSource` generation.
- `TextureResourceObject` owns immutable `TextureFile` metadata and the pinned
  `TextureSubresourceSource`; it owns no GPU image, descriptor, or residency
  entry.
- `TextureSubresourceSource` reuses generic coalescing, retry classification,
  package decoding, digest verification, and global staging admission. It adds
  only VTEX subresource addressing.
- `TextureMipAcquisition` retains a strong resource handle, constructs the full
  canonical layer/face set for the requested mip interval, and permits at most
  one bounded read window. `ReleaseWindow` releases its read interests and
  staging admission before the next window.
- The default window contains one subresource, preventing one admitted read
  from retaining budget while another request in the same transition waits.
  Callers may explicitly allow larger windows while bounding both admission
  bytes and request count. Runtime sources clamp that byte ceiling to the
  actual shared `ResourceStreamer` staging budget, so one window cannot retain
  admitted reads while another read from that same window waits forever.
- VMSH and VTEX metadata loaders share the generic `ResourcePrefixReader`;
  their format-specific validation and pipeline state remain separate.
- The managed engine streaming service registers the texture metadata loader
  beside the existing mesh metadata loader.

`texturesTests` verifies loose and segmented-package reads, package metadata
loading through `TextureResourceLoader`, digest rejection, metadata-only
pipeline loading, source-generation pinning after catalog removal, cancellation
from idle/reading/ready states, explicit release, oversize rejection, and a
5,120-byte mip interval progressing in two windows under a real 4,096-byte
global staging budget.
`engineServicesTests` verifies composition-root integration.

No RHI texture creation, bindless descriptor update, GPU residency-table write,
material integration, or rendering work is part of this phase.

## 5. Phase 3 — Candidate GPU Texture Upload and Initial Installation

### Result

```text
Study work: complete
Production changes: none
Selected physical representation: compact non-sparse mip chain
Selected upload path: shared renderer-owned CopySync batches
Selected visibility boundary: completed candidate, then existing GPU Scene installation batch
Next allowed work: implement Phase 3B
```

### Purpose and Boundary

Phase 3 joins the two boundaries Vanguard already has:

```text
Phase 2
  bounded verified TextureMipAcquisition windows

Phase 3
  one private physical candidate
  incremental batched GPU upload
  upload-completion validation

Phase 1
  immutable descriptor installation
  stable GpuTextureResidency table commit
```

This phase implements the first usable mip-tail installation only. It does not
select wanted mips, promote or demote an existing texture, copy shared mips,
change materials, build rendering work, or implement virtual texturing.

### 5.1 What Vanguard Already Has

The study found that Phase 3 must not duplicate existing ownership:

- `TextureMipAcquisition` already owns pinned source generation, verified range
  reads, bounded CPU staging, cancellation, and explicit window release.
- the RHI already creates uninitialized textures and writes individual
  subresources through a bound command list;
- command lists already retain upload destinations until their submission
  fence;
- `TextureResidencyManager` already owns stable identities, immutable
  descriptors, pending installation replacement, shared GPU Scene batching,
  rollback, and fence-delayed retirement;
- `GpuSceneUploader` uploads only the compact residency table record. It is not
  the raw texture-data uploader.

The missing owner is therefore deliberately narrow: a texture uploader that
turns verified windows into one complete private GPU candidate and hands that
candidate to `TextureResidencyManager` only after upload completion.

The concrete owner should be `TextureUploader`. It owns bounded in-flight
candidate records and their `TextureMipAcquisition` objects. A caller supplies
the loaded texture resource and stable residency handle, advances the uploader
from the renderer update, drains completed candidates into
`TextureResidencyManager`, and performs the existing shared GPU Scene batch.
This is execution plumbing for initial residency, not wanted-mip policy.

### 5.2 Physical Texture Representation

V1 uses a compact ordinary texture. If the asset has 10 mips and the tail begins
at asset mip 7, the candidate is:

```text
asset mips:       0 1 2 3 4 5 6 | 7 8 9
physical mips:                  | 0 1 2

candidate extent = asset extent at mip 7
candidate mipCount = 3
firstResidentMip = 7
residentMipCount = 3
```

This is not a full-size texture with seven uninitialized mips. Creating the
full chain would waste resident memory and a full SRV could expose undefined
data. The compact candidate contains exactly the initialized interval, so its
ordinary full-resource SRV is valid.

The mapping is fixed:

```text
physical mip          = asset mip - firstResidentMip
2D/1D array slice     = array layer
cube array slice      = array layer * 6 + face
3D array slice        = 0
```

For cube textures, the RHI descriptor's `arraySize` is the total face count,
`arrayLayers * 6`. For 3D textures it is one and each mip's `depthPitch`
describes all depth slices. Candidate width, height, and depth are calculated at
`firstResidentMip`; array/cube cardinality does not change.

The installation contract is consequently strict:

```text
TextureInstallationDesc.texture
  is a compact physical chain with exactly residentMipCount mips

physical mip 0
  represents logical asset mip firstResidentMip
```

`GpuTextureResidency.firstResidentMip` preserves the logical-to-physical mip
offset for later material/shader integration. There is no shader page table and
no second texture identity.

### 5.3 Metadata-to-RHI Conversion

Candidate creation uses an explicit conversion layer. Enum ordinals must never
be cast between `textures` and `rhi`, because RHI has additional linear/sRGB
format variants and its ordinal layout is different.

The conversion must:

- map every supported `textures::PixelFormat` with an explicit switch;
- choose the RHI sRGB variant only when `TextureFile::GetSpace()` is `SRgb`;
- reject sRGB for formats that do not support it;
- explicitly map 1D, 2D, 3D, and cube dimensions;
- overflow-check cube face count and all mip/extent calculations;
- request `TextureUsage::ShaderResource` plus the internal copy-destination
  capability required by the RHI upload path;
- create with no initial data and a state compatible with later copy writes.

One current format mismatch must be handled honestly:
`textures::PixelFormat::R9G9B9E5SharedExponent` has no Vanguard RHI/NVRHI
equivalent. Phase 3A must return a structured unsupported-format failure for it
instead of casting to a nearby ordinal. Adding that format to the RHI/backend is
a separate capability extension if a production cooking profile begins to emit
it; the current built-in profiles do not.

Row pitch, slice/depth pitch, byte count, width, height, depth, mip, layer, and
face are revalidated against the cooked metadata before recording a write.
Cooked bytes are already in GPU format; Phase 3 performs no decompression,
transcoding, swizzle, or image interpretation.

### 5.4 Why `CreateTexture(desc, initialData)` Is Rejected

Vanguard's current NVRHI backend implements initial texture data by creating an
internal graphics command list and submitting it inside each `CreateTexture`
call. Using that convenience path would produce one hidden submission per
texture and would require retaining a complete tail in CPU memory.

Phase 3 instead uses:

```text
rhi::CreateTexture(compactDesc, {})
  -> one shared renderer-owned CopySync command list
  -> many rhi::WriteTexture calls for many candidates
  -> one CloseAndSubmitCommandLists for the batch
```

`CopySync` currently maps to Vanguard's graphics queue, matching the existing
GPU Scene upload ordering. This avoids introducing an async-copy ownership and
cross-queue handoff protocol before it has demonstrated value.

The RHI contract used here must be explicit: after `WriteTexture` returns
successfully, the backend has copied the source span into command-list-owned
upload storage. The Phase 2 window may then be released. The current NVRHI path
has this behavior; the RHI test suite must lock it so a future backend cannot
retain the caller pointer accidentally.

### 5.5 Incremental Candidate and Shared Upload Batch

One tail can be larger than the CPU staging budget, so a candidate must accept
several Phase 2 windows and may span several GPU submissions. Holding all tail
bytes until a single submit would undo Phase 2's bounded-memory design.

The candidate tracks:

```text
stable source resource handle and content fingerprint
first asset mip and resident mip count
owning rhi::Texture candidate after the first verified window is ready
expected physical subresource count
written-subresource coverage
last upload fence
state and structured failure
```

For every ready Phase 2 window:

```text
validate every view
  -> reject duplicate or out-of-range physical subresources
  -> record WriteTexture into the current shared upload batch
  -> mark coverage only after every write in the window succeeds
  -> ReleaseWindow immediately
```

The renderer-owned uploader batch has independent hard ceilings for texture
writes, bytes, and participating candidates. A source window that cannot fit an
otherwise empty batch is a structured capacity failure; it is never admitted as
a special first window and unsigned remaining-capacity arithmetic is never
allowed to underflow. These are per-update GPU submission budgets, not
replacements for the generic streaming staging budget. When a ceiling is
reached, the uploader submits the current batch and continues in a later update.
No command list is kept open while waiting for I/O.

Starting source windows is independently capped per tick. Slot traversal begins
from a rotating cursor, so a large request set cannot repeatedly favor low slot
indices. Ready windows are also bounded by the candidate batch cap. The compact
GPU texture is created only when its first verified window is ready and has
passed the hard batch limits; requests waiting on I/O therefore do not reserve
idle VRAM.

All writes for one candidate use the same `CopySync` queue. Therefore its last
submission fence dominates its earlier upload submissions. The final batch
transitions the complete candidate from copy destination to graphics and
compute shader-resource state.

### 5.6 Completion and Atomic Installation

A candidate is complete only when:

- every required mip/layer/face has been written exactly once;
- the acquisition reached `Complete`;
- the final shader-resource transition was submitted;
- the candidate's last upload fence is complete.

Completion is polled asynchronously with `IsGpuFenceComplete`; no render-thread
blocking wait is allowed. Waiting for upload completion before descriptor/table
installation deliberately costs a possible update of latency but gives a
simple invariant: once the table can expose the descriptor, every queue may
read a fully initialized texture without carrying a second upload fence through
material or draw state.

The authority switch then reuses Phase 1 unchanged:

```text
completed TextureUploadCandidate
  -> TextureResidencyManager::Install
  -> immutable descriptor allocated and written
  -> residency record joins renderer-owned GpuSceneUploader batch
  -> AcceptSubmittedBatch
  -> stable GpuTextureResidencyHandle becomes BindlessReady
```

The candidate uploader does not allocate descriptor slots, write residency
records, or submit private GPU Scene batches. `TextureResidencyManager` does not
read VTEX data or record raw texture uploads.

### 5.7 State, Cancellation, and Failure

The minimal Phase 3 state is:

```text
Creating
  -> Acquiring
  -> Uploading
  -> WaitingForGpu
  -> Complete

any pre-complete state -> Failed or Cancelled
```

Failure rules:

- metadata/format/shape failure occurs before texture creation where possible;
- texture creation failure releases only the private candidate;
- one failed `WriteTexture` fails that candidate and discards the rest of its
  transition, but does not invalidate other candidates already recorded in the
  shared batch;
- cancellation before submission releases the candidate immediately;
- cancellation after submission marks it discard-on-completion; its owning
  reference is retained until the last upload fence completes;
- missing or duplicate subresources can never produce a complete candidate;
- installation or shared GPU Scene batch failure preserves the candidate for a
  bounded retry or discards it explicitly; it never fabricates BindlessReady;
- the old/current installation is untouched. For this initial-tail phase there
  normally is no old installation, but the same invariant is required for later
  promotion.

All candidate counts and retained completed candidates are bounded by explicit
configuration. A slow GPU or stalled installation batch must apply backpressure
instead of growing an unbounded completed queue.

### 5.8 RED and Unreal Findings

RED confirms the useful physical rule: create a render resource beginning at a
chosen resident asset mip, consume cooked GPU-ready bytes during creation, and
replace the render resource only after creation succeeds. RED also treats the
raw input pointer as temporary during the creation call and keeps a guaranteed
tail. Its inspected streaming methods are incomplete, so it is not copied as an
asynchronous scheduler.

Unreal provides the stronger lifecycle precedent. Its async-create stream-in
path owns an intermediate texture, uploads requested mip data, frees temporary
mip memory after the RHI has consumed it, and calls `FinalizeStreaming` only
after the intermediate texture is complete. Cancellation destroys the
intermediate and leaves the current texture untouched.

Vanguard adapts those ideas to its existing seams:

- Phase 2 verified windows replace engine-specific bulk-data I/O;
- compact `rhi::Texture` candidates replace Unreal intermediate textures;
- shared `CopySync` batches avoid Vanguard's per-texture initial-data submit;
- source-ready candidate creation follows RED's useful loaded-bytes-before-GPU-
  allocation boundary without copying its incomplete streaming scheduler;
- `TextureResidencyManager` and `GpuTextureResidency` provide the immutable
  bindless cutover rather than a mutable RHI texture reference.

### 5.9 Locked Phase 3 Decisions

1. The initial GPU candidate contains only the guaranteed mip-tail interval.
2. Physical mip zero represents `firstResidentMip`; table metadata preserves
   that logical offset.
3. Every physical mip/layer/face is initialized before installation.
4. Texture/RHI format and dimension conversion is explicit, never ordinal.
5. `CreateTexture(desc, initialData)` is forbidden for runtime streaming.
6. Raw texture uploads share renderer-owned `CopySync` command lists and
   submissions across candidates.
7. One candidate may span several bounded CPU windows and GPU submissions.
8. A Phase 2 window is released immediately after successful RHI staging.
9. Written-subresource coverage rejects omissions and duplicates.
10. A candidate becomes installable only after its last upload fence completes.
11. Candidate upload and GPU Scene table upload remain separate systems.
12. Descriptor and table ownership remain entirely in
    `TextureResidencyManager`.
13. Cancellation/failure never exposes partial texture state and never damages
    an existing installation.
14. Upload, waiting, and completed-candidate queues are explicitly bounded.
15. No mip policy, promotion/demotion, material integration, or rendering work
    enters this phase.
16. Source acquisition starts are independently bounded and scheduled with a
    rotating cursor.
17. Candidate GPU allocation waits until a verified source window is ready.
18. Active requests are indexed by exact resource generation and residency
    target, including the submitted-but-not-yet-taken state.
19. Vanguard command-list handles remain one-shot, while the backend may recycle
    only GPU-retired NVRHI wrappers in a bounded role/type pool.

### Phase 3 Implementation Slices

#### Phase 3A — Candidate Description and Validation

```text
TextureUploader configuration, candidate handles, and bounded ownership
explicit VTEX-to-RHI format/dimension conversion
compact candidate descriptor construction
asset-to-physical subresource mapping
overflow, pitch, byte-size, cube-array, and 3D validation
candidate state and exact subresource coverage
```

Phase 3A is implemented by `TextureUploadCandidatePlan`. It produces the compact
RHI descriptor without allocating a GPU texture, explicitly maps all supported
linear/sRGB VTEX formats, rejects the currently unsupported
`R9G9B9E5SharedExponent`, maps 1D/2D arrays, cube-array faces, and 3D mips, and
tracks coverage only after the future upload call reports success. The RHI now
also exposes and enforces `maximumTextureDimension3D`; previously its core
texture validation skipped the active backend's 3D extent limit.

#### Phase 3B — Batched Raw Texture Upload

```text
uninitialized candidate creation
renderer-owned CopySync batch with byte/write/candidate caps
incremental WriteTexture from ready acquisition windows
immediate ReleaseWindow after RHI staging
last-batch shader-resource transition
shared submission fence tracking
```

Phase 3B is implemented by `TextureUploader`. It retains the exact loaded
texture generation and drives `TextureMipAcquisition` through one-subresource
windows. The uninitialized compact texture from the Phase 3A plan is created
only after a verified window is ready and can fit an empty batch. A tick records
at most one renderer-owned `CopySync` command list, admitting candidates, writes,
and bytes through explicit hard caps. Acquisition starts have a separate cap,
request traversal rotates between ticks, and exact active requests use an O(1)
lookup that remains valid after submission. Several candidates share one batch;
one candidate may span several batches. Each ready CPU window is released only
after every corresponding `WriteTexture` call and coverage commit succeeds.

The NVRHI backend keeps external Vanguard command-list references one-shot and
generational. After a successfully submitted list is GPU-retired, its NVRHI
wrapper may return to a small type/role-keyed pool. Discarded or never-submitted
wrappers are destroyed, and the bounded pool prevents retained upload-chunk
high-water capacity from growing with command-list creation count.

The last batch for a candidate also transitions the complete texture from
`CopyDestination` to graphics/compute shader-resource state. The result is a
`SubmittedTextureCandidate` carrying the stable residency target and the shared
last-submission fence. No per-texture fence is created, and no descriptor or GPU
Scene installation happens in this layer. Same-queue ordering makes that last
`CopySync` fence cover all earlier upload batches for the candidate.

#### Phase 3C — Completion and Existing Residency Installation

```text
non-blocking completion polling
bounded complete-candidate queue
TextureInstallationDesc handoff
existing shared GPU Scene installation batch
cancel/failure cleanup and retry boundaries
```

Phase 3C is implemented without adding another coordinator or submission owner.
`TextureUploader::Tick` polls a configured maximum number of shared copy-fence
tokens without waiting and moves completed candidates into a separately bounded
`ReadyToInstall` queue. Queue removal is O(1), and the existing candidate-byte
budget remains charged until installation, cancellation, failure, or explicit
low-level ownership transfer, so a stalled residency batch cannot grow private
GPU candidates without bound.

`TextureUploader::InstallReadyCandidates` converts ready candidates directly to
`TextureInstallationDesc` and calls the existing
`TextureResidencyManager::Install`. It does not allocate table reservations,
open a GPU Scene batch, complete reservations, or submit GPU work. The renderer
still combines the manager's frozen contribution with other producers and calls
`AcceptSubmittedBatch` once for the resulting shared table submission.

Post-submit cancellation now marks the candidate for discard, retains its
physical texture and byte admission until the shared copy fence completes, then
releases it without installing a descriptor. A residency-admission failure
leaves the ready candidate owned by the uploader for bounded retry or explicit
cancellation; it never exposes partial state.

### Phase 3 Validation

Tests must prove:

- a 10-mip asset with tail first mip 7 creates a 3-mip physical texture whose
  mip 0 receives asset mip 7;
- 1D/2D arrays, cube arrays, and 3D textures map every subresource correctly;
- linear and sRGB formats map explicitly, including BC formats;
- malformed pitch, extent, byte size, layer, face, duplicate, and missing
  subresources are rejected;
- a tail larger than the Phase 2 window budget uploads across multiple windows
  without retaining prior window bytes;
- several candidates share one submission rather than submitting per texture;
- the first ready window cannot exceed either hard batch cap, and cap arithmetic
  cannot underflow;
- acquisition bursts obey the per-tick start cap and rotate fairly through live
  request slots;
- duplicate demand coalesces before and after submission without scanning all
  live requests;
- a request waiting on source I/O owns no candidate GPU texture;
- a successfully submitted and retired command-list wrapper can be reused for
  the same type/role, while a discarded wrapper cannot enter the pool;
- a candidate spanning several submissions uses its last fence as completion;
- cancellation before and after submit releases resources safely;
- no descriptor or GPU Scene record exists before candidate completion;
- the completed candidate installs through the existing residency manager and
  becomes BindlessReady only after the shared table batch is accepted;
- injected texture-upload, descriptor, and table-batch failures preserve the
  prior installation or leave an initial identity non-ready;
- the source bytes can be released immediately after successful `WriteTexture`
  recording without corrupting the eventual GPU contents.

### Phase 3 Exit Gate

Phase 3 is complete when a normally loaded VTEX can stream its guaranteed tail
through bounded verified windows, create and fill one compact physical texture
through shared upload submissions, wait asynchronously for completion, and
install that texture into its stable `GpuTextureResidencyHandle` through the
existing shared GPU Scene batch. The resulting entry must be ready for future
material reference, but no material or rendering code is added.

## 6. Study/Implementation Cadence

After each completed phase, work proceeds in short cycles:

```text
focused study
  -> lock one ownership/state boundary
  -> implement only that boundary
  -> compile and test it
  -> audit against the full path
  -> define the next focused study
```

Phase 3 now passes its guaranteed-tail exit path: verified VTEX bytes become a
compact physical texture, completion is polled asynchronously, the candidate is
admitted to the existing residency manager, and it becomes `BindlessReady` only
after the shared GPU Scene table batch is accepted. Phase 4 below now locks the
physical mip-transition boundary. Material integration remains later and begins
only after that boundary is implemented and verified.

## 7. Phase 4 — Compact Mip-Range Transitions

### Study Result

```text
Study status:           Complete
Implementation status: Phase 4A, 4B, and 4C complete
Physical model:         Compact non-sparse replacement texture
Promotion:              Upload newly requested mips + copy shared resident mips
Demotion:               Copy retained mips; no source I/O
Cutover:                Existing immutable descriptor + GPU Scene installation
Target selection:       Explicit caller-provided first mip in Phase 4
```

Phase 4 is not mip generation. The cooker has already generated the complete
mip chain and VTEX stores each mip independently. This phase changes which
cooked suffix is physically resident on the GPU.

Phase 4 also does not add:

- virtual or sparse textures;
- sampler-feedback policy;
- camera-, visibility-, or distance-driven wanted-mip selection;
- material binding;
- rendering, culling, or indirect-command generation.

A future policy layer will decide which mip is wanted. Phase 4 accepts that mip
as an explicit target and performs the transition safely.

### 7.1 Existing Vanguard Foundation

The required architecture already exists. Phase 4 extends it instead of adding
another texture system.

- `TextureMipAcquisition::Open(firstMip, mipCount)` already acquires an
  arbitrary contiguous asset-mip interval. `OpenMipTail` is only a wrapper.
- `TextureUploadCandidatePlan::Initialize(firstAssetMip, residentMipCount)`
  already describes an arbitrary compact physical interval.
- `TextureResidencyManager` already owns the stable
  `GpuTextureResidencyHandle`, immutable descriptor allocation, GPU Scene table
  installation, and multi-queue-safe retirement.
- The RHI already supports per-subresource texture writes, copies, and range
  transitions.
- Phase 3 already batches many candidates into one `CopySync` command list and
  one shared completion fence. Phase 4 keeps that ownership model.

What is missing is the transaction joining those pieces:

```text
current compact texture
  + explicit target first mip
  -> build private candidate
  -> upload only newly needed asset mips
  -> GPU-copy the shared resident suffix
  -> verify complete candidate coverage
  -> atomically install the candidate
  -> retire the old texture after every consuming queue is safe
```

There must be one active transition per stable residency handle. A second
coordinator, per-texture command list, or per-texture GPU fence is unnecessary.

### 7.2 RED and Unreal Comparison

#### RED

The inspected RED branch confirms the compact physical layout but does not
provide a runtime promotion/demotion state machine.

`CRenderTextureBase::LoadFromRenderTextureBlob` in
`renderTextureBase.cpp:14-187` selects a resident source mip, shifts the physical
base extent to that mip, creates only the remaining suffix, and uploads it into
a new texture. `CRenderTexture::Create` in `renderTexture.cxx:95-171` applies a
fixed mip drop and installs that texture.

The apparent runtime streaming API is inactive in this branch:

- `InitStreaming`, `CancelStreaming`, and `RecalculateMipStreaming` are empty;
- `HasStreamingSource` and `HasStreamingPending` return false;
- renderer streaming queries return false or zero;
- the intended `InitStreaming` calls are commented out;
- reload recreates whole textures and does not preserve shared GPU mips.

Therefore RED contributes two useful rules only:

1. physical mip zero may represent a non-zero asset mip;
2. texture uploads should share a batched copy/upload path.

Copying a RED runtime transition scheduler would be impossible because this
source branch does not contain one.

#### Unreal

Unreal's ordinary cooked, non-virtual texture streamer is the useful transition
reference. `UTexture2D::StreamIn` and `StreamOut` select separate compact
replacement paths; virtual and partially resident paths are explicitly gated
elsewhere.

The relevant behavior is:

```text
promotion
  allocate a larger compact intermediate texture
  read only the newly added high-detail mips
  GPU-copy the shared lower-detail suffix
  finish the update and switch the stable texture reference

demotion
  allocate a smaller compact intermediate texture
  GPU-copy only the retained lower-detail suffix
  switch the stable texture reference
  perform no source read
```

Evidence in Unreal 5.8.1:

- `Texture2DMipDataProvider_IO.cpp::GetMips` reads only
  `[StartingMipIndex, CurrentFirstLODIdx)` into allocator-provided memory;
- `Texture2DMipAllocator_AsyncCreate.cpp` and
  `Texture2DMipAllocator_AsyncReallocate.cpp` create a target-sized intermediate
  and populate only newly added mips;
- `UE::RHI::CopySharedMips` in `RHI.cpp:2146-2161` computes the shared suffix
  offsets and copies it;
- `FStreamableTextureResource::FinalizeStreaming` switches the stable texture
  reference before recording the new resident count;
- RHI deletion and D3D12 allocation paths defer release until the relevant GPU
  queues and frame fences are complete.

Vanguard should copy that transaction shape, not Unreal's object hierarchy.
Vanguard's existing residency table is the stable reference, and its existing
shared table batch is the atomic cutover.

### 7.3 Exact Compact-Range Mapping

Let:

```text
N = total asset mip count
C = current first resident asset mip
T = target first resident asset mip
```

Every Vanguard V1 installation is a complete suffix:

```text
[firstResidentMip, N)
```

Holes are not allowed. Let `G` be the first mip of the cooked guaranteed tail.
A valid target satisfies `0 <= T <= G`, so `[T, N)` always contains the complete
tail `[G, N)`. A coarser request with `T > G` is clamped to `G`.

The operation is:

```text
T == C  -> no-op
T < C   -> promotion; upload asset mips [T, C), copy shared mips [C, N)
T > C   -> demotion; no source I/O, copy retained mips [T, N)
```

For any shared asset mip `S`:

```text
old physical mip       = S - C
candidate physical mip = S - T
```

Equivalently:

```text
sharedFirst = max(C, T)
sharedCount = N - sharedFirst
sourceBase  = sharedFirst - C
targetBase  = sharedFirst - T
```

Example, promoting a 10-mip asset from `C = 7` to `T = 4`:

```text
load asset mips          4, 5, 6
copy asset mips          7, 8, 9
old physical source      0, 1, 2
candidate destinations   3, 4, 5
```

Example, demoting it from `C = 4` to `T = 7`:

```text
source I/O               none
copy asset mips          7, 8, 9
old physical source      3, 4, 5
candidate destinations   0, 1, 2
```

The copy is repeated for every array slice or cube face. Cube-array slices use
`arrayLayer * 6 + face`. A 3D texture has one subresource per mip and copies the
full mip depth. Vanguard's RHI copies one subresource per call, so Phase 4 emits
an explicit bounded loop rather than assuming Unreal's bulk helper.

### 7.4 Candidate Completeness

Phase 3 currently treats candidate coverage as CPU uploads only. That is too
narrow for Phase 4.

Each destination subresource must reach exactly one initialized state:

```text
Missing
UploadedFromSource
CopiedFromCurrent
```

Installation is legal only when every expected destination subresource is
covered exactly once. Duplicate upload/copy, missing coverage, or a copy outside
the planned shared interval is an error.

Accounting must also stop using `uploadedBytes != 0` as candidate validity.
A demotion can be completely valid with zero source-upload bytes. Keep separate
statistics:

```text
sourceBytesUploaded
gpuBytesCopied
candidateGpuBytes
```

### 7.5 Transition Ownership and Content Identity

The residency manager owns a small active-transition record for each stable
texture identity. This is a token, not a lease, and it does not grant general
resource ownership.

The token captures:

- the residency handle;
- the current installation revision;
- the current first mip and mip count;
- a strong reference to the current physical GPU texture;
- the committed content fingerprint and source generation identity;
- the requested target first mip.

The strong GPU reference keeps the copy source alive. The expected revision
prevents a candidate prepared from revision `R` from replacing a newer revision
`R + 1`.

Shared mips may be copied only when the committed and requested content
fingerprints match. Otherwise a hot reload could combine new high-detail mips
with old low-detail mips. A changed fingerprint must perform a complete
replacement from the new source or reject and restart; it must never reuse the
old texture's shared suffix.

The existing submitted candidate already carries a weak source generation and
content fingerprint, but `InstallReadyCandidates` currently discards them.
Phase 4 must carry that identity into the committed residency state and validate
it again at cutover.

Request behavior is deterministic:

- the same handle and same target coalesce;
- only one different target may be active for a handle;
- before GPU submission, an obsolete target may be cancelled and replanned;
- after submission or table admission, the newer target waits for the current
  transaction to finish;
- target selection remains outside the residency manager.

### 7.6 NVRHI State Correction Required Before Copying

The Phase 3 candidate descriptor currently declares:

```cpp
usage = ShaderResource | CopyDestination;
initialState = CopyDestination;
```

That is not a safe persistent state. Vanguard's NVRHI backend sets
`keepInitialState = true` for textures. NVRHI documents and implements this as:
every command list restores a touched texture to its declared initial state when
the list closes. D3D12 calls `keepTextureInitialStates()` in
`d3d12-commandlist.cpp:310-314`; Vulkan does the same.

Therefore Phase 3's final explicit shader-read transition can be restored back
to `CopyDestination` at command-list close. It also cannot be applied as a
whole-resource `CopyDestination -> shader-read` assertion when a candidate spans
several lists, because mips written by earlier lists have already returned to
their persistent state.

All streamable compact textures must instead declare:

```cpp
usage = ShaderResource | CopySource | CopyDestination;
initialState = ShaderResourceGraphics | ShaderResourceCompute;
```

`WriteTexture` temporarily moves the written subresource to copy destination;
NVRHI restores it to shader-read when the list closes. Shared-mip copying
temporarily moves the old subresource to copy source and the new subresource to
copy destination, then command-list close restores both to shader-read. The
current final whole-resource transition is removed or replaced by validation of
the persistent state.

This is a Phase 4 prerequisite correction to the Phase 3 implementation, not a
new RHI architecture.

### 7.7 Queue Ordering and Atomic Cutover

The current physical texture may be read by graphics or asynchronous compute.
`CopySync` records on the graphics queue, so graphics ordering alone cannot
prove that a compute reader is finished.

The transition executor must run at an explicit renderer-owned safe copy point
where every queue that could consume the old texture has been ordered before
the graphics copy. It must not hide a CPU fence wait inside `TextureUploader`.
Before rendering integration exists, tests have no external readers and can
supply an empty prerequisite set; the API still preserves the future contract.

The complete transition is:

```text
1. Begin transition and retain the current texture/revision/content identity.
2. Create a private compact candidate for T.
3. For promotion, acquire and upload only [T, C).
4. At the safe copy point, copy every shared subresource into the candidate.
5. Verify exact candidate coverage and submit through the shared CopySync batch.
6. Poll the shared completion fence without blocking.
7. Compare the retained revision and content identity again.
8. Admit the immutable descriptor/table installation to the shared GPU Scene batch.
9. Make the candidate current only after that shared batch is accepted.
10. Retire the old descriptor and texture after graphics, compute, and copy are safe.
```

Cancellation or failure before table admission leaves the current installation
unchanged. Once the shared table update is submitted, the transaction is
committed and must finish normal retirement rather than attempting rollback.

### 7.8 Bounded Work and Performance Corrections

Phase 4 keeps all work bounded:

- maximum active candidates;
- maximum source-window bytes and subresources;
- maximum upload writes and bytes per shared batch;
- maximum shared-copy operations and bytes per shared batch;
- maximum candidate GPU bytes, including old/new overlap;
- descriptor and residency-table admission limits.

The correction audit keeps two deliberately separate ledgers:

- `maximumPendingCandidateBytes` reserves cooker-authored source bytes for live
  requests;
- `maximumPendingCandidateGpuBytes` bounds exact physical allocation bytes
  retained by uploader-owned candidates across acquisition, submission, and
  installation backpressure.

The uploader queries the created texture's authoritative RHI memory
requirements, caches that exact size, and charges it until candidate ownership
leaves the uploader. A candidate that can never fit either physical cap fails
with `CapacityExceeded`; an unavailable/zero RHI requirement fails instead of
falling back to cooked bytes. Deferral resets the first candidate allocation but
retains its exact size, so later ticks test admission without repeated
create/destroy churn. Cancellation, failure, installation, and explicit take all
release the charge exactly once.

One boundary remains explicit: the current RHI exposes authoritative texture
memory requirements only after texture creation. Therefore the retained ledger
is hard after reconciliation, but the first discovery can transiently create one
allocation before its exact size is known. A strict pre-creation no-overshoot
guarantee requires a general descriptor-based RHI requirement query. Do not
duplicate backend allocation math inside the texture system to hide that missing
general contract. Promotion still budgets the temporary coexistence of old and
new textures once the candidate size is known.

The audit also found four real hot-path corrections:

1. acquisition is hardcoded to one subresource per window and each request is
   advanced only once per tick. A four-mip cube therefore needs at least 24
   ticks even when budgets are empty. Use a configurable bounded
   `maximumSubresourcesPerWindow` and a fair per-request progress cap while
   preserving the hard byte limit;
2. arbitrary mip acquisition scans every subresource and allocates an index
   list even though validated VTEX layout is canonical mip-major. Calculate the
   contiguous index range directly;
3. residency `PrepareBatch` shifts the remaining pending array on every normal
   batch. Replace it with a FIFO head/ring queue; the bounded cancellation-only
   linear removal is not urgent;
4. completed candidates are consumed LIFO and one transiently busy candidate
   can block the entire pass. Use FIFO order and rotate/retry a busy entry.

One `CopyTexture` call per mip/slice is acceptable when all calls share the same
command list. Existing bounded command-list recycling and non-blocking shared
fence polling are already correct and must not be redesigned.

### 7.9 Locked Phase 4 Decisions

1. Phase 4 executes an explicit target first mip; it does not choose that mip.
2. Every physical installation is a complete compact suffix with no holes.
3. Promotion uploads only newly requested high-detail mips.
4. Promotion GPU-copies the already resident shared suffix.
5. Demotion performs no source I/O and GPU-copies only the retained suffix.
6. The current texture remains authoritative until the candidate is complete and
   the shared residency-table batch is accepted.
7. Candidate completeness includes both uploaded and GPU-copied subresources.
8. Shared-copy reuse requires identical committed content fingerprints.
9. One manager-owned transition token retains the source texture and expected
   revision; no lease abstraction is introduced.
10. All streamable textures support shader read, copy source, and copy
    destination, with shader read as the persistent initial state.
11. Phase 4 uses shared command lists and shared completion fences, never one per
    texture.
12. Source copying requires an explicit multi-queue-safe renderer boundary and
    never hides a CPU wait.
13. Candidate memory admission accounts for physical allocation and old/new
    overlap, not merely cooked upload bytes.
14. Sparse textures, mip generation, desired-mip policy, materials, and rendering
    remain outside this phase.

### 7.10 Practical Implementation Slices

#### Phase 4A — State and Transition Plan

```text
correct persistent shader-read state and add CopySource usage
remove the invalid final whole-resource transition
add no-op/promotion/demotion range planning
add UploadedFromSource/CopiedFromCurrent exact coverage
store committed content identity
optimize canonical contiguous mip acquisition
add mapping and NVRHI final-state regression tests
```

Phase 4A is implemented. Streamable candidates now declare shader-read as
their persistent initial state and include both copy-source and copy-destination
usage. The invalid final whole-resource transition was removed; NVRHI returns
each written subresource to shader-read when the command list closes.

`TextureMipTransitionPlan` now produces deterministic no-op, promotion, and
demotion intervals from total mip count, guaranteed-tail boundary, current
first mip, and requested first mip. It clamps requests that would omit part of
the guaranteed tail and maps every shared asset mip to its old and candidate
physical mip.

Candidate coverage now distinguishes `UploadedFromSource` and
`CopiedFromCurrent`, allowing a future copy-only demotion to become complete
without pretending that source bytes were uploaded. Committed residency records
also retain the weak source generation and content fingerprint, and uploader
installation rejects stale or changed source content.

The Phase 2 acquisition implementation no longer scans all texture
subresources or allocates an index list for a canonical contiguous mip range.
It calculates the first subresource and count directly while retaining the same
bounded-window API.

#### Phase 4B — Bounded Transition Execution

```text
add an explicit target-first-mip request and target-aware coalescing
add one manager-owned active transition token with source retention
upload only the promotion delta; issue no I/O for demotion
record shared-subresource copies at the explicit safe copy point
add copy-operation, copy-byte, and physical candidate-memory budgets
advance requests fairly with bounded multi-subresource windows
keep one shared CopySync command list and fence per batch
```

Implementation status: complete.

`TextureUploader::RequestMipTransition` now accepts an explicit target first
mip. The target is clamped to retain the guaranteed mip tail, and request
coalescing includes that target. A request equal to the current compact suffix
is a true no-op and creates neither a candidate nor GPU work.

`TextureResidencyManager` owns one `TextureTransitionToken` record per stable
texture identity. The record retains the current physical texture, committed
source generation/content fingerprint, current compact interval, target, and
expected installation revision. A second target is rejected while that record
is active. This is a bounded transition token, not a lease or a second resource
ownership system.

The executor now performs the exact studied operations:

```text
initial installation -> source-upload [target, total mip count)
promotion            -> source-upload [target, current), GPU-copy [current, total)
demotion             -> no source acquisition, GPU-copy [target, total)
```

Shared copies map every mip and array slice through
`TextureMipTransitionPlan`, explicitly transition only those source and
destination subresources, then call the existing RHI `CopyTexture`. Uploads and
copies for all candidates admitted in one tick share one `CopySync` command
list, one submission, and one completion fence.

Execution is bounded independently by acquisition starts, source writes/source
bytes, copy operations/copy bytes, newly created candidates, actual RHI-reported
candidate allocation bytes, ready candidates, and completion polls. Source
acquisition windows may contain several subresources but remain bounded by both
the shared staging admission and the configured subresource count.

`Tick(prerequisiteFences)` is the explicit safe-copy boundary. It only polls the
caller-provided graphics/compute/copy fences and defers shared copies while any
reader is outstanding; it never performs a CPU fence wait. Empty prerequisites
remain valid for tests and for initial uploads with no external readers.

Accounting now separates:

```text
sourceBytesUploaded
gpuBytesCopied
candidateGpuBytes (actual RHI memory requirement)
```

The D3D12 regression builds a four-mip VTEX and proves the physical sequence:
tail `2..3`, promotion to `0..3` with 320 source bytes plus 20 copied bytes, and
copy-only demotion back to `2..3` with zero additional source bytes. The test
also passes each prior table completion as the next transition's safe-copy
prerequisite and verifies the committed compact mip interval after every shared
GPU Scene batch.

#### Phase 4C — Compare-and-Install

```text
validate source generation, content fingerprint, and expected revision
retain transition ownership through shared GPU Scene batch acceptance
reuse immutable descriptor/table replacement and multi-queue retirement
convert normal pending and ready queues to bounded FIFO behavior
verify cancellation/failure always preserves the old installation
```

Implementation result:

- `TextureInstallationDesc` carries the manager-issued transition token for
  streamed replacements. Admission compares that token, the retained resource
  generation, content fingerprint, target first mip, current physical texture,
  compact interval, and expected installation revision in constant time.
- Direct/manual installation remains available when no transition is active,
  but it cannot bypass an active manager-owned transition.
- Successful admission transfers transition ownership from `TextureUploader`
  to `TextureResidencyManager`. The transition remains active while the table
  contribution is pending, frozen, written, or retried. Only shared-batch
  acceptance or discard resolves it.
- Batch acceptance makes the candidate current and starts the existing
  immutable-descriptor/multi-queue retirement path. Batch discard releases the
  candidate descriptor and texture, clears the transition, and leaves the old
  installation unchanged.
- The residency pending queue and uploader ready queue are intrusive bounded
  FIFOs. Enqueue, dequeue, cancellation removal, and retry reinsertion are
  allocation-free `O(1)` operations; batch retry preserves original FIFO order.
  Completion polling remains bounded and avoids head-of-line blocking by
  enqueueing candidates in observed completion order.

The D3D12 transition regression additionally proves that an un-tokened install
cannot bypass an active transition, residency retirement remains blocked after
candidate admission but before table acceptance, a discarded promotion keeps
the previously resident mip interval, and the same target can be requested
again after discard/cancellation.

### Phase 4 Validation

Tests must prove:

- no-op, promotion, and demotion ranges for a known 10-mip asset;
- exact shared-mip physical offsets in both directions;
- promotion reads only the added asset mips;
- demotion performs zero source reads and remains a valid candidate;
- 1D/2D arrays, cube arrays, and 3D textures copy every subresource correctly;
- candidate coverage rejects missing, duplicate, and out-of-plan writes/copies;
- command-list close leaves uploaded and copied textures in shader-read state;
- a stale revision or stale resource generation cannot replace a newer install;
- a changed fingerprint never mixes old and new texture content;
- cancellation and injected upload/copy/descriptor/table failures preserve the
  prior installation;
- descriptor pressure, source-window pressure, copy caps, and physical-memory
  pressure remain bounded;
- several transitions share one command list and one completion fence;
- a safe multi-queue cutover is required when an old texture has external
  readers;
- FIFO queues do not starve older ready or pending work.

### Phase 4 Exit Gate

Phase 4 is complete when a caller can request a valid target first mip and the
system can promote or demote an already installed texture through bounded work,
preserve shared mips with GPU copies, atomically replace the residency-table
entry, and retire the old physical texture safely. The stable
`GpuTextureResidencyHandle` must not change, and failure must leave the previous
texture usable.

Only after this exit gate should the next study define wanted-mip policy and its
inputs. Material residency remains after the physical texture path is complete.

## 8. Phase 5 — Runtime Texture Integration

### Study Result

Phase 5 is an integration phase, not another texture storage or upload system.
The physical texture path is already complete in isolation. What is absent is a
renderer-owned object that keeps it alive, receives runtime texture demand,
advances it every frame, and contributes completed installations to the one
shared GPU Scene update batch.

Implementation status: complete through Phase 5C.

The required result is:

```text
loaded TextureResourceObject
  -> renderer texture demand
  -> stable GpuTextureResidencyHandle
  -> guaranteed mip-tail request
  -> bounded TextureUploader progress
  -> TextureResidencyManager installation
  -> shared GPU Scene update batch
  -> bindless-ready residency table entry
```

Phase 5 does not implement material loading, shader sampling, visibility-based
wanted-mip selection, render-graph execution, culling, or drawing. Those systems
will consume this boundary later.

### 8.1 Vanguard Audit

The runtime resource side is already engine-owned:

- `ResourceStreamingService` owns one generic `ResourceStreamer` and registers
  `TextureResourceLoader` with the normal `ResourcePipeline`;
- `TextureResourceObject` owns VTEX metadata and a pinned loose/package source;
- `TextureMipAcquisition` uses the generic source, decompression, verification,
  coalescing, retry, and staging-budget machinery.

Phase 5 must not add a texture-only asset loader, package reader, IO queue,
retry system, or decoded-byte budget. Its input is an already loaded
`resources::ResourceHandle` whose object is a `TextureResourceObject`.

The physical GPU side also already exists:

- `TextureResidencyManager` owns stable table identities, immutable descriptor
  installations, compare-and-install state, and fence-safe retirement;
- `TextureUploader` owns bounded verified-byte acquisition, physical candidate
  creation, shared uploads/copies, completion polling, and ready candidates;
- `GpuSceneRuntime` owns the only renderer-wide `GpuSceneUploader`;
- `GpuTextureResidency` has matching CPU/HLSL layout and
  `GpuMaterialResource::Texture` already names its stable table index.

The missing ownership is visible in `RenderingService`: it owns GPU Scene,
mesh residency, the global resource descriptor domain, and the frame driver,
but owns neither `TextureResidencyManager` nor `TextureUploader`. Consequently
the production engine currently never allocates a texture residency identity,
never requests a mip tail, never ticks the uploader, and never installs a
texture table entry. The managers are exercised by tests only.

Shader files currently declare `GpuTextureResidency`, but no production shader
loads that table. That is expected at this boundary: shader/material consumption
belongs to the following material integration work, not Phase 5.

### 8.2 RED and Unreal Guidance

RED confirms the ownership split that Vanguard already selected:

```text
resource texture identity and cooked blob
  != render-side physical texture object
```

`CBitmapTexture` remains the asset-facing object while `CRenderTextureBase`
creates the compact physical texture from a selected resident mip. The inspected
RED branch retains resident/pending/streaming mip state but its streaming
methods are largely disabled, so it is useful confirmation of ownership and
initial-tail creation, not a complete scheduler to copy.

Unreal supplies the useful runtime scheduling lesson. One central render-asset
streaming manager owns per-asset resident/requested/wanted state, spreads work
over bounded update stages, and advances many in-flight updates together.
Physical replacements use a private intermediate texture and become current
only at the render-side finish step. Unreal does not create an independent
manager, command list, or fence for every texture.

Vanguard should adapt those ideas as follows:

- one renderer-global texture residency runtime;
- one coalesced record per exact resource path and generation;
- bounded progress over many records;
- one shared texture upload command list/fence per admitted batch;
- one shared GPU Scene table submission for scene and texture updates;
- current physical texture remains valid until the table update succeeds.

Unreal's wanted-mip calculation, view heuristics, pool solver, and virtual
texture paths are deliberately not copied in Phase 5. They are a later policy
study.

### 8.3 Runtime Composition Owner

Add one `TextureResidencyRuntime` in the rendering layer. It owns:

```text
TextureResidencyRuntime
  TextureResidencyManager
  TextureUploader
  bounded residency records
  bounded demand handles
  path -> same-path generation chain
```

This is composition and scheduling state; it is not a second residency manager.
`TextureResidencyManager` remains the authority for stable GPU identities and
physical installations. `TextureUploader` remains the authority for candidate
work.

The public demand boundary should mirror the already established mesh pattern:

```cpp
RequestTexture(resource, TextureDemandHandle& demand)
CancelDemand(demand)
GetInfo(demand.GetResidency(), info)
```

`TextureDemandHandle` is move-only and releases its demand on reset or
destruction. It is not called a lease. The runtime record retains one strong
`ResourceHandle` while demand exists, so metadata and the exact physical source
generation remain alive through upload and future mip transitions.

Requests coalesce only when both resource path and resource generation match.
Hot-reloaded generations must not share an identity or physical transition.
The first demand allocates one `GpuTextureResidencyHandle` and requests the
cooked guaranteed mip tail. Further equal demands increment a bounded reference
count and reuse that handle.

The handle is available before its table record is bindless-ready. Callers must
query readiness; Phase 5 must not claim that allocation alone makes a texture
sampleable. Future material integration may retain the handle while using a
default texture until the `BindlessReady` state is visible.

When the final demand is released, the runtime cancels uncommitted work when
possible and moves committed physical state through normal residency
retirement. Busy/frozen work is marked cancelling and completed through the
existing transaction rules; it is never force-freed.

### 8.4 One Shared GPU Scene Update

`TextureResidencyManager` already exposes the correct contribution protocol:

```text
PrepareBatch
BuildUploadRequests
WriteBatch
AcceptSubmittedBatch / RetryBatch / DiscardBatch
```

The missing code is in the `GpuSceneRuntime::Publish` coordinator. Today that
coordinator opens one batch only for `RenderSceneGpuPublisher` changes and
returns immediately when no scene changed. Texture-only table work would never
be submitted.

Phase 5 must extend this coordinator to accept bounded external GPU Scene
contributions. The interface should be producer-neutral because mutable mesh
placement and later material systems need the same serialized uploader. It must
be a small preallocated callback/token record, not a heap-allocated virtual
object per update.

Texture batch preparation and resolution are main-thread operations, while
`GpuSceneRuntime::Publish` runs on the renderer Jobs chain. Therefore the
coordinator contract has three explicit stages:

```text
main thread before FrameTick dispatch
  freeze contribution and copy its upload requests into preallocated state

renderer Jobs chain
  write reservations and submit the combined GPU Scene batch
  record success/failure and the shared fence; do not resolve owner state

next main-thread RenderUpdate after the CPU tail is flushed
  accept the submitted texture batch, or retry/discard a failed frozen batch
```

This preserves `TextureResidencyManager`'s thread contract and keeps frozen
state immutable while worker jobs use it. The main thread must not tick, cancel,
or retire the same runtime until the previous renderer CPU tail has completed.

For every frame tick the coordinator performs one transaction:

```text
freeze bounded texture installations
append their GpuSceneUploadRequests
append bounded scene requests
GpuSceneUploader::Begin once
write scene ranges and texture table records
complete all reservations
GpuSceneUploader::Submit once
record the shared completion fence
resolve texture and scene owner state at their valid serialized boundary
```

Before submission failure retries the frozen texture batch and cancels prepared
scene changes on the next main-thread boundary. After a successful submission,
contributor acceptance must be non-failing: GPU bytes are committed and cannot
truthfully be rolled back.

Texture requests receive a small reserved per-frame installation cap before
scene requests consume the remaining shared update capacity. This prevents a
large scene mutation burst from starving texture readiness. Unused reserved
capacity remains available to scene changes. There is still exactly one mapped
GPU Scene segment, one copy command list, and one shared completion fence.

`GpuSceneDefinitions` currently also uses the shared uploader synchronously.
Phase 5 does not redesign immutable-definition acquisition, but all calls must
remain serialized with the renderer CPU chain so it cannot open a batch while
the asynchronous frame update owns the uploader.

### 8.5 Frame Progress and Fence Ownership

The renderer-owned tick order is:

```text
RenderUpdate/main-thread boundary
  flush the previous renderer CPU tail
  resolve its submitted or failed texture table contribution
  collect completed texture retirements
  advance TextureUploader with real prior-reader fences
  admit completed candidates to TextureResidencyManager

main thread before renderer FrameTick dispatch
  freeze and stage bounded texture table records

renderer CPU FrameTick Jobs chain
  write the already staged texture table records
  submit one shared GPU Scene update
```

Initial mip-tail uploads have no old physical texture and therefore require no
prior-reader fence. Promotion and demotion copy from the current texture and
must receive fences for every queue that may still read it.

Vanguard currently has no installed render-graph executor and no production
frame-submission object that exposes a graphics/compute/copy cutover. The RHI
tracks submitted fence values internally, but its public API does not expose a
truthful renderer frame cutover. Phase 5 must not work around this by:

- passing empty fences for a texture with readers;
- creating three artificial submissions per texture;
- waiting for the GPU on the CPU;
- teaching `TextureUploader` to guess which queues used a resource.

The future frame executor must produce one general renderer-owned cutover value,
for example:

```cpp
struct RenderSubmissionCutover
{
    rhi::ResidencyFenceSet submitted;
};
```

The texture runtime consumes that shared value for transition prerequisites and
retirement sealing. Mesh, descriptor, GPU Scene, and other physical-resource
retirement can consume the same value. This is general renderer/RHI machinery,
not texture-specific code.

Until the frame executor exists, Phase 5 validation may supply real test queue
submissions. Production integration can safely prove initial tail upload and
shared table installation, but must defer reader-dependent transitions and
retirement rather than weaken the fence contract.

### 8.6 Performance Contract

Phase 5 adds no unbounded frame work:

- residency and demand storage reserve configured hard capacities;
- path lookup is hashed and generation validation is constant time;
- equal demands coalesce instead of starting duplicate IO/upload work;
- every tick retains the existing acquisition, byte, candidate, copy, poll,
  ready, and installation caps;
- completed candidates enter the existing allocation-free FIFO;
- texture table records join the existing mapped GPU Scene upload segment;
- no per-texture command list, fence, thread, job chain, or staging arena exists;
- no whole-texture scan is needed to find active runtime work.

The runtime may scan only a bounded active-work queue, not all registered
textures. Idle demanded textures remain in the hashed residency directory and
cost no per-frame processing.

### 8.7 Failure and Lifecycle Rules

Failure remains local and retryable where possible:

- resource generation changes before admission reject the candidate;
- IO, verification, format, allocation, and upload failures leave the residency
  record non-ready and expose structured failure information;
- GPU Scene capacity defers the frozen installation without creating another
  physical texture;
- shared update failure retries or discards according to the existing manager
  transaction without replacing the current texture;
- releasing the final demand cancels pending work and retires committed state;
- shutdown requires the CPU rendering tail drained, no live demands, no active
  upload requests, no frozen table batch, and all retirement epochs collected.

The `RenderingService` owns and initializes the runtime only when an RHI device,
global resource descriptor domain, and GPU Scene runtime exist. Device-disabled
profiles keep it uninitialized, matching mesh residency. Shutdown order is:

```text
drain renderer CPU chain
resolve its final staged texture table outcome
release/cancel texture demand
seal resulting retirement with the last real renderer cutover
wait device idle at the existing service boundary if fences are incomplete
collect and shut down texture residency/runtime
shutdown GPU Scene
release global descriptor domain and RHI
```

### 8.8 Practical Implementation Slices

#### Phase 5A — Runtime Ownership and Demand

Implementation status: complete.

```text
add TextureResidencyRuntime composition owner
add bounded generation-aware TextureDemandHandle records
initialize manager/uploader from RenderingService dependencies
request guaranteed mip tail on first demand
tick uploader and admit ready candidates
cancel/retire when the final demand disappears
expose state/stats without exposing mutable internals
```

No GPU Scene coordinator change belongs in this slice. Tests stop with a pending
residency installation.

#### Phase 5B — Shared GPU Scene Contribution

Implementation status: complete.

```text
add a bounded producer-neutral contribution record to GpuSceneRuntime
stage texture contributions on the main thread before FrameTick dispatch
include texture installations even when no RenderScene changed
reserve a configured texture-installation share of the batch
write texture and scene reservations into one mapped segment
submit once and retain the shared completion fence in coordinator state
resolve accept/retry on the next main-thread boundary after the CPU tail flush
retry pre-submit failures without losing the prior physical installation
```

The implemented coordinator reserves bounded contribution/request storage at
initialization. Texture installations are frozen on the main thread, their table
writes run beside scene write ranges, and `GpuSceneUploader` still creates one
batch, one copy submission, and one completion fence for the whole tick. The
next main-thread update accepts submitted batches or restores pre-submit
failures to the texture installation queue. Texture-only ticks are supported;
no synthetic scene mutation is required.

#### Phase 5C — Engine Path and Lifecycle Validation

Implementation status: complete.

```text
load a cooked VTEX through ResourcePipeline from loose and packaged sources
request it through TextureResidencyRuntime
pump engine FrameTick without direct test-only manager calls
verify the physical subresources and GpuTextureResidency table entry
release the demand and verify fence-safe descriptor/texture/table retirement
verify device-disabled RenderingService behavior and clean shutdown
```

Tests use a real shared renderer cutover fence set. Phase 5C does not add a
temporary rendering pass or shader merely to prove residency.

`textureResidencyServiceTests` validates this boundary through the managed
engine services. It starts a real D3D12-backed `RenderingService`, loads both a
loose VTEX generation and a segmented packaged VTEX generation reconstructed
from the same real-JPEG DDC record through the registered
`TextureResourceLoader`, and advances the ordinary frame pipeline until each
texture is bindless-ready. Each installation contributes to the shared GPU Scene
transaction without a synthetic scene mutation. The proof then reads every
resident physical mip back and compares it byte-for-byte with the cooked
subresource rows, and reads the `GpuTextureResidency` entry back to verify its
descriptor, mip range, generation, and `BindlessReady` flags. The test then
releases demand, seals texture/descriptor/table retirement with one real
graphics/compute/copy cutover set, drains the resource pipeline, unmounts the
package, and shuts down the complete service/RHI ownership chain with no live
state. The existing `engineServicesTests` continues to validate that a
device-disabled `RenderingService` leaves mesh and texture residency
uninitialized.

### 8.9 Phase 5 Exit Gate

Phase 5 is complete when a normally loaded loose or packaged VTEX can be
demanded through the renderer service, acquire one stable texture residency
handle, upload its guaranteed mip tail, join the normal GPU Scene update, and
become bindless-ready without direct test orchestration of the lower-level
managers.

Equal resource generations must coalesce, different generations must not,
frame work must remain bounded, scene and texture table updates must share one
GPU Scene submission, and release/shutdown must retire all GPU ownership behind
real renderer-provided queue fences.

After this gate, material residency can store and resolve stable texture
residency indices. Wanted-mip policy remains a separate later study because it
needs view/visibility/material-use inputs that Phase 5 intentionally does not
invent.
