# Vanguard Texture Cooking Integration Study Plan

Date: 2026-08-30

Status: Phase 0, Phase 1, and Phase 2 study are complete. Phase 1 implementation
and Phase 2 generic cooked-artifact delivery are complete. The low-level
texture importer, cooker, VTEX format, runtime loader, streaming path, GPU
upload, and bindless residency path already exist. Editor integration remains
outside this plan.

## 1. Objective and Boundary

The target chain is:

```text
authored texture bytes + resolved build settings
  -> generic assets::BuildSystem
  -> TextureAssetCompiler
  -> textureTools import and cook
  -> canonical segmented VTEX artifacts
  -> existing derived-data cache and dependency index
  -> loose VTEX or VPAK assembly
  -> existing TextureResourceLoader
  -> existing TextureResidencyRuntime
  -> bindless-ready GpuTextureResidency entry
```

This is headless machinery. A command-line tool, automated build, or future
editor may construct the same `BuildRequest`, but the compiler must not depend
on editor services, project windows, file watchers, or editor sidecar classes.

This work does not implement:

- editor import UI, thumbnails, property panels, or change notifications;
- materials or material compilation;
- visibility-driven wanted-mip policy;
- virtual textures, sparse tiles, or shader page tables;
- rendering, culling, indirect commands, or draw submission.

## 2. Phase 0 Result: What Already Exists

| Chain section | Current state |
|---|---|
| PNG, JPEG, TIFF, OpenEXR import | Implemented in `textureTools` |
| DDS direct GPU-data import | Implemented without decompress/recompress |
| profile-driven mip generation and compression | Implemented |
| 1D, 2D, array, 3D, cube, and cube-array VTEX data | Implemented by the cooker contract |
| deterministic self-validating VTEX document | Implemented in `textures` |
| metadata, mip, and guaranteed-tail segmentation | Implemented by `BuildStorageSegments` |
| generic build fingerprints, DDC, graph, index, and recooker | Implemented in `assets` |
| generic VPAK planning and assembly | Implemented in `assets` and `packages` |
| loose/VPAK metadata and subresource loading | Implemented |
| bounded GPU upload and bindless residency installation | Implemented through Texture Residency Phase 5 |
| registered source-image-to-VTEX compiler | Missing |
| production bridge from indexed DDC artifacts to loose/VPAK bytes | Missing |
| source-image-to-bindless-ready headless proof | Missing |

The low-level cooker is therefore not being redesigned. The next work connects
the already tested halves through the existing general asset machinery.

## 3. Existing Vanguard Contracts to Preserve

### 3.1 Generic build ownership

`assets::BuildSystem` already owns:

- compiler lookup by source and output resource types;
- target platform in the build key;
- source content, source metadata, and settings fingerprints;
- dependency discovery and sorted prerequisite fingerprints;
- compiler and tool version invalidation;
- exact in-memory and persistent derived-data cache hits;
- cancellation and asynchronous build-graph execution;
- multi-resource, multi-segment artifacts.

The texture compiler must register with this system. It must not add a texture
cache, texture dependency graph, texture recooker, or texture package database.

The existing `MeshAssetCompiler` is the closest Vanguard adapter pattern, but
texture import is simpler because ordinary texture decoders consume the exact
`BuildRequest::source.content` bytes and do not need Assimp-style file access.

### 3.2 VTEX is the runtime ABI

`TextureResourceLoader`, `TextureMipAcquisition`, `TextureUploader`, and
`TextureResidencyRuntime` already consume VTEX. The asset compiler must produce
that exact format through `texture_tools::CookTexture` or
`texture_tools::CookGpuTexture`; it must not produce an intermediate runtime
texture representation.

The artifact mapping is fixed by `textures::BuildStorageSegments`:

```text
VTEX metadata segment
  -> ArtifactFlags::Primary | ArtifactFlags::MemoryResident

ordinary higher-mip segment
  -> ArtifactFlags::Streamable

required mip-tail segment
  -> ArtifactFlags::Streamable | ArtifactFlags::MemoryResident
```

Segments contain the exact VTEX bytes, including required alignment padding.
Dense segment order reconstructs the original VTEX document byte-for-byte.
The compiler must not emit a complete VTEX blob plus duplicate per-mip
artifacts.

### 3.3 Runtime and cooker must agree

The following values are cooker-authored and runtime-validated:

- dimension, format, color space, extent, layers, faces, and mip count;
- direct-upload row and slice pitches;
- canonical subresource order;
- streamable flag and first guaranteed mip-tail level;
- source and metadata fingerprints;
- subresource byte ranges and digests;
- VTEX and metadata wire versions.

Runtime code does not reinterpret source pixels or regenerate mips. A compiler
change that alters any of these fields must invalidate derived data through its
compiler/tool/profile version.

## 4. Phase 1 Canonical Build Settings

The compiler uses one authored texture source type, `VTSR`, and the existing
`VTEX` output type. There is one compiler for that pair. PNG, JPEG, TIFF,
OpenEXR, cube-cross, and DDS variants are routes inside that compiler, not
separate asset compilers.

The compiler recipe is a fixed 24-byte, little-endian V1 wire value. It follows
the existing mesh compiler pattern but contains no source path, editor state, or
execution policy:

| Offset | Wire field | Type |
|---:|---|---|
| 0 | magic, `VTCB` | `u32` |
| 4 | settings version, `1` | `u16` |
| 6 | reserved, must be zero | `u16` |
| 8 | source mode | `u8` |
| 9 | input color-space override | `u8` |
| 10 | red channel source | `u8` |
| 11 | green channel source | `u8` |
| 12 | blue channel source | `u8` |
| 13 | alpha channel source | `u8` |
| 14 | route flags | `u8` |
| 15 | DDS mip-tail count | `u8` |
| 16 | resolved cooking profile id | `u64` |

The source mode is authoritative:

```cpp
enum class TextureBuildSourceMode : u8
{
    Image2D,
    CubeCrossHorizontal,
    CubeCrossVertical,
    PreservedDds,
};
```

`Image2D` and both cube-cross modes require:

- a valid RGBA channel mapping;
- a nonzero, registered cooking profile id;
- zero route flags and zero DDS mip-tail count.

`PreservedDds` requires:

- a DDS signature and structurally valid DDS payload;
- identity channel mapping;
- a zero cooking profile id, because the DDS GPU format is preserved;
- no route flag except bit 0, `Streamable`;
- a nonzero mip-tail count when streamable, and the canonical value `1` when
  nonstreamable.

The input color-space field is common because it can deliberately override both
decoded images and DDS container metadata. Unknown enum values, unknown flags,
nonzero reserved fields, inactive-field values, trailing bytes, and
mode/signature mismatches are errors. This gives every semantic recipe exactly
one byte representation.

`TextureUsage` is deliberately absent. In the current importer it only chooses
`ImportedTexture::recommendedProfile`; the compiler already supplies an
explicit resolved profile to `CookTexture`. Storing both would give two names to
the same output decision. If usage ever changes cooked bytes independently, the
settings version must be raised.

A format hint is also absent. Every V1 format has a signature, and the hint is
currently diagnostic/probe guidance rather than output policy. The compiler
passes no hint into routing and reports the source resource identity in errors.

Execution mode, job batch size, diagnostics verbosity, cancellation state,
memory budgets, and safety ceilings are build configuration. They are not
authored texture identity and must not produce separate cache entries when the
output bytes are identical.

## 5. Phase 1 Compiler Identity and Route

### 5.1 Dependency discovery

Dependency discovery remains cheap and deterministic:

```text
parse and validate canonical settings
  -> validate the requested target against the desktop texture policy
  -> resolve the selected profile for image routes
  -> add one required texture-compiler tool dependency
```

It does not decode, inspect every mip, or compress the image. V1 recipes have no
external generated dependencies; the future six-source cube recipe is a
separate extension rather than an inactive dependency mechanism here.

### 5.2 Strict compilation route

Compilation follows the settings-selected route:

```text
PreservedDds + valid DDS signature
  -> ImportDdsTexture
  -> CookGpuTexture without filtering or recompression

Image2D + supported ordinary-image signature
  -> ImportTexture
  -> CookTexture

CubeCrossHorizontal/Vertical + supported ordinary-image signature
  -> ImportTexture
  -> ExtractCubeCross with the selected layout
  -> CookTexture
```

The source mode never changes after probing. A DDS payload presented as an image
mode, an ordinary image presented as `PreservedDds`, or a malformed DDS is a
terminal settings/source mismatch. There is no decoder fallback that silently
changes the requested route. Six independent cube-face assets are deferred; a
future generated-resource recipe can use six dependencies and the existing
`AssembleCubeFaces` helper.

After cooking, the compiler reopens the VTEX with `TextureFile`, builds storage
segments, and adds those ranges to `ArtifactWriter`. Reopening is intentional:
the artifact boundary is based on the runtime-validated document, not on
assumptions retained from the encoder.

### 5.3 Target policy

The current target table is intentionally small:

| Asset target | Texture policy |
|---|---|
| `WindowsD3D12` | desktop formats, including BCn |
| `WindowsVulkan` | desktop formats, including BCn |
| `LinuxVulkan` | desktop formats, including BCn |

Cooked texture bytes depend on this declared target policy, never on the GPU or
driver installed on the cook machine. The three targets currently select the
same profile/format rules, but generic build fingerprints continue to contain
the target and therefore remain separate. Vulkan runtime integration is not yet
present in Vanguard, so Vulkan output can be structurally cooked but cannot be
claimed as end-to-end runtime validated yet. A future target with different
format support must add an explicit policy and version; it must not fall back to
the host machine.

### 5.4 Frozen tool configuration

The current profile and importer registries seal themselves on the first
`CookTexture` or `ImportTexture` call. That is too late for asynchronous builds:
dependency discovery could hash one registry state while execution observes
another, and the current plain global seal writes are a data race.

Phase 1 requires an explicit, idempotent freeze operation after texture-tools
initialization and custom registration, but before compiler registration. After
freezing:

- profile and importer registration returns `RegistrySealed`;
- import and cook hot paths perform no writes to registry state;
- the frozen configuration exposes deterministic component fingerprints.

The required synthetic tool dependency, proposed as
`tools/vanguard-texture-compiler` with resource type `VTTL`, hashes canonical
little-endian fields, not C++ object bytes. Its route-specific content includes:

- texture compiler and routing-policy versions;
- VTEX document, metadata, and segmentation-policy versions;
- the selected target policy;
- importer ids, versions, resolution order, and declared immutable custom
  configuration fingerprints relevant to the route;
- every selected profile field, including canonical finite
  `alphaCoverageThreshold` bits, for image routes;
- compressor/codec policy versions;
- the cube-cross helper version for cube-cross routes;
- the DDS importer and byte-preserving cooker versions for DDS routes.

It never hashes structure padding, function pointers, `userData` addresses,
display names, or mutable process state. Profile registration must reject NaN
and infinity as well as out-of-range alpha thresholds. A custom importer whose
behavior changes must change its version or immutable configuration
fingerprint.

## 6. RED and Unreal Study Result

### RED

Useful source seams:

- `backendMaterial/src/importBitmapTexture.cpp`
- `backendMaterial/src/bitmapTextureCompilationSource.cpp`
- `renderBackend/src/renderTextureCompiler.cpp`
- `backendMaterial/src/textureCooking.cpp`
- `backendData/src/cooker.cpp`
- `renderData/src/renderTextureBlob.cpp`
- `renderer/src/renderBlobUpload.cpp`

RED confirms the useful separation:

```text
source/import settings
  -> texture compilation source adapter
  -> platform compiler
  -> versioned runtime blob
  -> generic cooker dependency/cache machinery
```

Its general cooker, rather than its texture code, owns prerequisite hashes,
tool versions, target platform, cache lookup, and output tracking. Vanguard
should copy that ownership boundary.

RED is not a source to copy for mip packaging in this branch. It stores one
autoloaded deferred texture payload, its streamable blob constructor has no
caller, and its render-texture streaming initialization is commented out.
Vanguard keeps the stronger existing VTEX segmentation instead.

### Unreal

Useful source seams:

- `Developer/TextureBuild/Public/TextureBuildFunction.h`
- `Developer/TextureBuild/Private/TextureBuildFunction.cpp`
- `Runtime/Engine/Private/TextureDerivedDataBuildUtils.cpp`
- `Runtime/Engine/Private/TextureDerivedDataTask.cpp`
- `Developer/TextureCompressor/Private/TextureCompressorModule.cpp`
- `Runtime/Engine/Private/TextureDerivedData.cpp`

Unreal confirms four decisions:

1. Runtime platform data is separate from editor-only source art.
2. Build workers receive fully resolved settings and explicit source bulk data,
   not hidden editor or local-machine configuration.
3. Build identity includes build-function, format, settings, source, and tool
   versions.
4. Metadata, independent high mips, and one packed guaranteed tail are separate
   derived values; generic BulkData/IoStore decides physical package placement.

Vanguard should also adopt Unreal's discipline of bounded cook working sets and
declared memory expectations. In particular, Unreal's texture build function
computes a physical texture-build memory estimate and gives it to the generic
build context before execution. That supports Vanguard's generic pre-dispatch
byte admission rather than a texture-only lock. Vanguard should not copy
Unreal's DDC compatibility layers, virtual-texture tiling, editor preview paths,
or platform object serialization.

## 7. Performance and Failure Contract

- One source is decoded once and cooked once per cache miss.
- Dependency discovery does not decode the texture.
- DDS blocks remain byte-exact and are never decompressed/recompressed.
- VTEX segmentation is one linear pass over subresources.
- Packaging reads each artifact once; it must not reload the complete artifact
  set for every mip.
- Metadata and the guaranteed tail remain obtainable without high-resolution
  mip payloads.
- No per-texture DDC, package writer, worker thread, or IO scheduler is added.
- All count, size, artifact, and BuildSystem limits fail explicitly; no offset,
  byte count, or segment index may truncate.

### 7.1 Artifact limits and exact mapping

The current limits do not line up by default:

```text
textureTools maximum subresources     1,048,576
textureTools maximum output bytes     16 GiB
BuildSystem maximum artifacts         4,096
BuildSystem maximum artifact bytes    2 GiB
Artifact::bytes capacity               u32
```

One metadata artifact is added to the subresource artifacts. Under the default
asset limit, a VTEX may therefore contain at most 4,095 subresources, not 4,096.
Every individual segment must also fit in `u32` bytes.

`ArtifactWriter` needs read-only maximum/remaining count and byte accessors so a
compiler can clamp its low-level limits before decoding or compression. The
texture compiler then inspects the source header, proves
`subresources + 1 <= remaining artifacts`, proves the predicted VTEX document
fits the remaining byte budget, and rejects any possible segment larger than
`u32` before the expensive cook.

After cooking, validation must prove that `BuildStorageSegments` returns a
dense, ordered, non-overlapping cover of `[0, VTEX file size)`. Segment 0 is the
metadata artifact with alignment log2 4. Payload segments use alignment log2 6.
The last segment must end exactly at the file size. No unchecked `u64` to
`usize` or `u32` conversion is allowed.

There is also a generic diagnostic bug: when a compiler returns `false` after
`ArtifactWriter::Add` has failed, `BuildSystem` currently reports
`CompileFailed` before inspecting the writer's sticky status. The writer status
must take precedence, followed by cancellation, then generic compiler failure.

### 7.2 Generic byte-weighted admission

Operation-count limits do not bound cook memory. `BuildGraph` currently copies
every request and dispatches every dependency-ready compile. Meanwhile one
4096x4096 RGBA8-to-BC7 cook can approach roughly 400 MiB before generic artifact
copies; several worker jobs can exhaust memory even though every individual
build is within its limit.

This is general asset-build scheduling, not a reason to add a texture mutex.
`CompilerDescriptor` needs a lightweight preparation-time estimator:

```cpp
struct BuildResourceEstimate
{
    // Peak compiler-owned work memory, excluding the graph-owned request and
    // the generic ArtifactWriter/BuildOutput/cache copies.
    u64 compilerTransientBytes = 0;
    u64 artifactBytes = 0;
};
```

The estimate is stored in `BuildPlan`. A compiler registered for asynchronous
graph execution must provide one; an unknown estimate must not bypass the cap.
The texture estimator inspects headers without decoding and conservatively
accounts for decoded pixels, codec scratch, current and next `Float4` mip
working images, the encoded mip chain, and the in-memory VTEX document. The DDS
route accounts for its copied GPU payload and VTEX output without float working
images.

Under the copies made by the current generic implementation, active reservation
is conservatively:

```text
max(compilerTransientBytes + artifactBytes, 3 * artifactBytes)
```

The first term covers cooking while `ArtifactWriter` receives its copy. The
second covers the worst current post-compile overlap among writer, output,
persistent-cache validation, and memory-cache copies. All arithmetic is checked
and saturating estimates are rejected as `LimitExceeded`.

`BuildGraphConfig` gains separate limits for active execution bytes and copied
queued-request bytes. Request content, metadata, and settings reserve queued
bytes before `OwnedBuildRequest` copies them. Once dependencies complete, a
small admission action either reserves active bytes and dispatches execution or
puts the operation in a priority/FIFO ready queue and returns. It never blocks a
Jobs worker on a semaphore, because the texture cook itself may need child jobs.

Reservations remain held through `BuildSystem::Execute` and dependency-index
publication and are released on every success, failure, cancellation, and
scheduling-failure path. A single estimate larger than the configured capacity
fails with `LimitExceeded`; it is not allowed to run outside the budget.
Phase 1 may reserve conservatively for cache hits as well. Splitting cache-hit
and cache-miss cost is a later throughput optimization, not a safety exception.

### 7.3 Terminal payload retention

The graph may keep stable `BuildOperation` control blocks until shutdown, but it
must not retain all large payloads for that entire lifetime:

- source content, source metadata, and settings are released when an operation
  becomes terminal;
- identities, state, fingerprints, counters, and failure data remain;
- dependency-only artifact arrays are released after successful dependency
  index publication because parents consume the content fingerprint, not the
  bytes;
- an externally requested root retains output while a `GraphRequest` can copy
  it, then releases the bytes after its last external interest is gone.

Completed operations are not reused by the current graph, so a later request
can create a fresh operation and obtain its output from the normal cache. This
retention change does not require unstable operation pointers.

### 7.4 Cancellation boundary

`CompileContext` already carries cancellation, but texture import and cooking do
not. Phase 1 must forward an optional cancellation view into those low-level
operations and check it:

- before and after an external decoder call;
- between extracted cube faces and DDS subresources;
- between generated mip levels and compression block batches;
- before VTEX validation and artifact copying.

A third-party decoder call cannot necessarily be interrupted in its middle, so
the API must not promise that. It must stop at the next boundary and never
publish a partial VTEX artifact set.

### 7.5 Deliberately deferred optimizations

Phase 1 stays within the current in-memory artifact model. These are valid later
generic improvements, but are not prerequisites for a correct first compiler:

- moving rather than copying artifacts into `BuildOutput`;
- validating persistent records without reading a second full artifact copy;
- a streaming artifact sink shared by textures, meshes, audio, and other large
  cooked formats;
- streaming encoded mips rather than retaining the complete encoded chain;
- reusing the generic source digest instead of recalculating it in adapters;
- replacing `ArtifactWriter`'s bounded linear duplicate scan if measurement
  shows it matters.

## 8. Phase 1 Study Result

The Phase 1 contract is now closed. The implementation is split into three
bounded passes:

### Phase 1A - Generic build safety seam

- add resource estimates to compiler plans and nonblocking byte admission to
  `BuildGraph`;
- bound queued request copies and release terminal heavy payloads;
- expose writer capacities and preserve its specific failure status;
- add focused admission, oversize, cancellation, and payload-release tests;
- give existing asynchronous compilers honest estimators rather than allowing
  an unestimated bypass.

Implementation status: complete in the generic asset layer. `BuildGraph`
requires estimates, performs byte-weighted nonblocking admission, bounds queued
request copies, and releases terminal heavy payloads. Existing graph-driven
compilers and graph tests provide estimates; direct-only compiler descriptors
remain source-compatible but cannot bypass graph admission.

### Phase 1B - Frozen texture-tools boundary and compiler

- add explicit registry freeze/fingerprint and finite profile validation;
- add cheap source inspection and conservative texture memory estimates;
- add canonical V1 settings encode/decode;
- add and register the single `VTSR -> VTEX` `TextureAssetCompiler`;
- implement the three strict image routes and preserved-DDS route;
- emit the exact validated VTEX storage segments.

Implementation status: complete. Texture tools now require an explicit,
idempotent configuration freeze after custom profile/importer registration.
The frozen profile and ordered importer registries expose canonical
fingerprints; profile registration rejects non-finite authored values. The
single `VTSR -> VTEX` compiler uses the generic BuildSystem, canonical 24-byte
settings, allocation-free source-header inspection, conservative byte
estimates, strict image/cube-cross/preserved-DDS routing, the existing texture
import/cook functions, and the existing VTEX storage segment builder. No
texture-specific cache, scheduler, package writer, or streaming path was added.
Cancellation uses the shared read-only system cancellation view and is checked
at decoder, cube extraction, mip, DDS subresource, VTEX validation, and artifact
copy boundaries.

### Phase 1C - Contract proof

- reconstruct VTEX byte-for-byte from emitted artifacts and reopen it;
- test settings canonicality and route/signature mismatches;
- test tool/profile/importer invalidation and deterministic cache hits;
- test default artifact boundaries, oversized sources, and cancellation;
- run concurrent large-estimate tests proving the generic byte cap is never
  exceeded and Jobs workers are not blocked.

Implementation status: complete. `textureToolsTests` now concatenates the
compiler's dense ordered artifact set and proves it is byte-identical to the
directly cooked VTEX before reopening it through `TextureFile`. The same test
reconstructs and reopens the preserved-DDS route, verifies its default
metadata/high-mip/resident-tail boundaries, rejects noncanonical magic,
version, reserved bytes, enum values, route fields, and trailing settings
bytes, and rejects both PNG-as-DDS and DDS-as-image route mismatches before
cooking.

The cache proof covers deterministic hits plus source-byte, selected-profile,
and target/tool-identity invalidation. Frozen profile and importer registry
fingerprints remain deterministic and are incorporated in the compiler tool
dependency established by Phase 1B. Oversized source dimensions fail during
resource estimation. Compiler-chain cancellation returns no artifacts or content
fingerprint, stores no cache entry, and the same prepared request rebuilds
successfully on retry.

The concurrent execution-budget proof remains in the generic
`assetGraphTests`, where two 60-byte executions under a 60-byte cap produce one
active operation and one nonblocking admission waiter, never exceed the cap,
then both finish after admission is released. That suite also proves oversized
execution rejection and dependency-graph cancellation. Texture Phase 1C reuses
that general machinery instead of adding a texture-specific scheduler or a
duplicate admission test framework.

Exit gate:

```text
BuildSystem request from real encoded bytes
  -> admitted bounded cook
  -> ordered VTEX artifact set
  -> byte-exact reconstruction
  -> TextureFile validation succeeds
```

## 9. Remaining Study/Implementation Phases

### Phase 2 - Generic Cooked-Artifact Delivery

Study status: complete. No second cooked-artifact owner is needed.

#### Existing Ownership And Reusable Seams

The current generic asset layer already has three complementary records:

```text
VDDC record
  build fingerprint
  artifact-set content fingerprint
  exact descriptors and payload bytes

VADI DependencyRecord
  source/output/compiler/dependency identity
  same build and content fingerprints
  IndexedArtifact descriptors without payload copies

PackagePlanner / PackageAssembler
  closure and placement planning from VADI
  artifact-read callback
  VPAK validation and staged replacement
```

VDDC is therefore the authoritative cooked-byte owner. VADI locates and
describes those bytes. Loose files and VPAKs are derived delivery forms, not
new caches.

`ReadPersistentRecord` already validates the complete VDDC contract, but it is
private to `assets.cpp` and materializes every artifact payload into a
`BuildOutput`. Copying that parser into package or loose-file code would create
two format implementations and would load unrelated mips. The implementation
must instead factor this reader so `BuildSystem` and delivery use the same VDDC
decoder.

Recommended general shape:

```cpp
struct ArtifactSetKey
{
    BuildFingerprint build;
    BuildFingerprint content;
};

class DerivedDataArtifactSource
{
public:
    Result Open(ArtifactSetKey key, ArtifactSetReader& reader) const;
};

class ArtifactSetReader
{
public:
    ArraySpan<const IndexedArtifact> Artifacts() const;
    Result Read(const IndexedArtifact& artifact, DynamicArray<u8>& bytes);
    Result CopyTo(const IndexedArtifact& artifact, IFile& output,
                  ArraySpan<u8> scratch);
};
```

The exact names may follow existing asset naming, but the ownership boundary is
locked:

- one opened immutable VDDC record;
- bounded descriptor storage and bounded copy/hash scratch;
- no retained whole-record payload copy;
- header, reserved fields, limits, descriptor uniqueness, payload extents,
  build fingerprint, and artifact-set content fingerprint validated once;
- reads require an exact match for resource, segment, flags, alignment, and byte
  count, not only a matching segment number;
- `BuildSystem` cache hits use this same decoder when they need a complete
  `BuildOutput`.

The VDDC payload layout is descriptor-order contiguous, so the reader can hash
the canonical artifact-set stream with a fixed scratch buffer, then seek to
individual payload extents. It does not need to allocate the combined VTEX or
VMESH merely to validate the record.

#### Loose Resource Materialization

Loose materialization takes an explicit `DependencyRecord`, logical resource,
target path, and sibling temporary path. It does not resolve editor project
paths and does not register the result with `ResourceStreamer`; those are later
composition responsibilities.

The operation is:

```text
find artifacts for the requested logical resource
  -> require one VDDC artifact-set key
  -> sort by segment
  -> require dense 0..N-1 segment identity
  -> verify every descriptor against the open VDDC record
  -> stream-copy each payload into the sibling temporary file
  -> flush and close
  -> reread with bounded scratch and verify size/digest of what was written
  -> replace the target
```

Failure removes only the temporary file and preserves the previous target.
There is no format-specific `TextureFile` or `MeshFile` validation in this
generic layer; byte-exact reconstruction is its contract. Phase 3 reopens the
result through the real VTEX reader.

An atomicity defect was found during this study. Vanguard's
`CFileManager::MoveFile` deletes the destination before calling the lower-level
replace-existing move. Consequently the VADI and single-VPAK code paths do not
currently preserve the old file across the final rename despite their stated
contract. VDDC correctly calls `CSystemIO::MoveFile` directly. Phase 2 must add
one explicit same-directory replace operation in the Vanguard filesystem seam
and use it for loose materialization, VADI, and single-package replacement.
Sibling temporary files guarantee the move remains on one volume. Package-set
assembly is different: it requires absent DATA targets and commits DATA000
last, so it does not replace an existing file in place.

#### Package Assembly Bridge

`PackagePlanner` already supplies sorted `IndexedArtifact` records and
`PackageAssembler` already validates callback byte counts. The missing identity
is the VDDC artifact-set key that owns each planned resource.

Add the originating build/content key once to `PlannedPackageResource`, not to
every segment. Planning must reject attempts to merge one logical resource from
different build records. The read callback then receives:

```cpp
read_artifact(
    ArtifactSetKey origin,
    const IndexedArtifact& artifact,
    DynamicArray<u8>& bytes);
```

The adapter keeps the current artifact-set reader open while all segments of a
resource are read. A 12-mip texture therefore causes one VDDC open/validation,
not 12 complete record reads. Existing `PackageAssembler` memory remains
bounded to one planned resource by `maximumResourceBytes`; Phase 2 does not
redesign `PackageWriter` or introduce a package-wide payload cache.

Artifact flags need one conservative rule: a serialized logical resource must
not become incomplete when editor artifacts are filtered. Mixed editor-only
and runtime segments for the same logical resource must be rejected unless a
future format explicitly models optional byte ranges. Editor-only outputs
should be separate logical resources. This prevents package filtering from
silently shifting or deleting bytes inside VTEX, VMESH, or another serialized
file.

#### RED Cross-Check

RED's `RenderTextureBlobCompiler` produces a platform header and one cooked data
buffer. `IRenderTextureBlob` then places that buffer in the general
`DeferredDataBuffer`/resource serialization path; streamable loading is issued
through that generic buffer. The useful rule is the ownership split, not RED's
exact serialized format:

```text
texture compiler creates exact cooked bytes
general resource storage owns and delivers those bytes
renderer consumes the cooked blob
```

Vanguard's explicit VTEX segments are more suitable for mip-range streaming,
but they should still flow through the generic VDDC, loose-file, and VPAK
machinery. No `TextureCookedStore`, texture package writer, or texture-specific
file publisher should be added.

#### Practical Implementation Slices

Phase 2A - factor and expose the VDDC artifact reader:

- one canonical VDDC parser shared with `BuildSystem`;
- bounded streaming fingerprint validation;
- exact descriptor lookup/read/copy APIs;
- missing, corrupt, wrong-build, wrong-content, and descriptor-mismatch tests.

Implementation status: complete. `DerivedDataArtifactSource` opens one immutable
VDDC record, validates its canonical artifact-set fingerprint with fixed-size
scratch, indexes exact descriptor identities, and supports selective reads and
bounded stream copies without retaining the full payload set. `BuildSystem`
cache hits and temporary-record validation now use this same decoder; the old
private parser and its quadratic duplicate scan were removed.

Phase 2B - add generic loose materialization and the safe replacement seam:

- dense ordered resource reconstruction with bounded scratch;
- staged-file reread verification;
- preserve the old target on every pre-replace failure;
- correct VADI and single-package replacement to use the same safe operation.

Implementation status: complete. `LooseResourceMaterializer` selects one
logical resource from a `DependencyRecord`, places dense segments directly in
O(n) order, validates them against one open VDDC reader, and reconstructs the
resource using bounded scratch. It hashes the source stream while writing,
rereads and hashes the completed sibling temporary, and replaces the target
only after size and digest match. The filesystem `ReplaceFile` seam rejects
non-sibling paths and calls the lower-level replace-existing operation without
deleting the target first. VADI and single-VPAK publication now use that seam.

Phase 2C - connect package assembly to the real artifact source:

- carry one artifact-set key per planned resource;
- reject cross-record resource merging and unsafe mixed editor/runtime segments;
- replace fixture-only byte callbacks with a `DerivedDataArtifactSource` adapter;
- keep one VDDC reader open across all segments of the current resource.

Implementation status: complete. Every `PlannedPackageResource` now carries
the exact build/content `ArtifactSetKey` of its owning immutable VDDC record.
Planning rejects a logical resource assembled from multiple record origins and
rejects mixed runtime/editor-only segments even when editor artifacts would
otherwise be filtered. Editor-only data must be emitted as a separate logical
resource. `DerivedDataPackageArtifactReader` is the production package callback:
it validates and opens an origin once, performs exact descriptor reads, and
reuses that reader for consecutive segments. Path resolution and artifact
reading now have separate callback contexts, so this adapter does not know
about project/editor paths. The package planner test assembles from real VDDC
records and proves that a multi-segment resource does not reopen its record per
segment.

Phase 2D - prove the generic delivery contract:

- one synthetic segmented asset reconstructed loose and inside VPAK;
- byte identity between VDDC payload concatenation, loose file, and decoded VPAK
  logical resource;
- corruption, missing segment, wrong size/alignment/flags, temporary cleanup,
  and old-target preservation tests;
- an open counter proving a multi-segment resource does not reopen VDDC per
  segment;
- existing VMESH and VTEX segment shapes checked without adding format-specific
  delivery code.

Implementation status: complete. The package-planner proof now takes one real
two-segment cached resource and compares the descriptor-order VDDC bytes, the
safely materialized loose file, and the decoded VPAK logical resource byte for
byte. It also proves one VDDC open per planned resource rather than per segment.
The persistent-cache/materialization proof covers corruption, missing segments,
size, flag, and alignment mismatches, abandoned temporary cleanup, and
preservation of an existing target on failure. The existing `textureToolsTests`
and `meshToolsTests` pass with their native VTEX and VMESH segment shapes; no
texture- or mesh-specific delivery store, writer, or publisher was added.

This is general asset machinery. It must work for VMESH and future segmented
formats as well as VTEX. It must not create a second texture artifact store.

Exit gate:

```text
one cached segmented build
  -> byte-exact loose VTEX
  -> byte-exact logical VTEX inside VPAK
```

### Phase 3 - Headless Source-to-Residency Proof

Study status: complete. No missing production abstraction was found. The two
halves of the proof already exist and meet at one clear seam:

```text
textureToolsTests
  encoded PNG/JPEG or DDS
    -> TextureAssetCompiler
    -> BuildSystem persistent VDDC artifact set

packagePlannerTests
  indexed VDDC artifact set
    -> LooseResourceMaterializer or PackagePlanner/PackageAssembler

textureResidencyServiceTests
  loose VTEX or mounted VPAK VTEX
    -> ResourceStreamer / ResourcePipeline
    -> TextureResidencyRuntime
    -> GPU Scene texture-residency table
    -> BindlessReady
```

The missing proof is only the connection between these existing paths.
`textureResidencyServiceTests` currently creates VTEX bytes directly with
`textures::WriteTexture` and creates a VPAK directly with `PackageWriter`.
Those two fixture helpers bypass the compiler, DDC, dependency index, and
generic delivery machinery. The runtime half of that test is already the right
one and must be retained.

#### 3.1 Chosen source fixtures

Use the existing small encoded ordinary-image fixture from texture-tools tests
and the existing deterministic four-mip BC1 DDS fixture shape. They exercise
different compiler routes:

```text
ordinary encoded image + Color profile
  -> decode, mip generation, platform block encoding

preserved DDS + Streamable + two-mip tail
  -> no pixel decode or recompression; preserve the existing BC1 mip bytes
```

The ordinary route proves that the source importer is in the chain. The DDS
route proves the segmented streaming shape used by residency. Test bytes are
embedded or loaded from the existing texture-tools fixture directory; no
editor asset database or new authored project layout is introduced.

#### 3.2 Exact build and delivery sequence

The implementation should extend the existing
`textureResidencyServiceTests` target instead of creating another service
bootstrap or another resource source:

```text
1. Start the existing EngineHost service graph.
   This provides filesystem, jobs, ResourcePipeline, ResourceStreamer,
   RenderingService, RHI, GPU Scene, and TextureResidencyRuntime.

2. Initialize texture tools, one generic BuildSystem with persistentCacheRoot,
   one TextureAssetCompiler, one DependencyIndex, and one
   DerivedDataArtifactSource over that same cache root.

3. Encode canonical TextureBuildDescription settings.
   BuildSystem::Prepare + Execute produces the VTEX artifact set.
   DependencyIndex::Publish records the exact descriptors and fingerprints.

4. Execute the identical request again and require CacheHit.
   Execute an ordinary-image request with a different valid profile and require
   Built with a different build fingerprint. Do not publish that alternate
   recipe over the record selected for delivery.

5. Find the selected DependencyRecord in the index.
   LooseResourceMaterializer reconstructs one loose VTEX from its VDDC set.

6. PackageManifest selects the same indexed VTEX.
   PackagePlanner prepares it, DerivedDataPackageArtifactReader reads it, and
   PackageAssembler writes the VPAK. No direct PackageWriter fixture path is
   used.

7. Register the loose file and mount the package through ResourceStreamer.
   Exercise both through the existing ordinary ResourcePipeline path.
```

The loose and package forms therefore originate from immutable VDDC artifacts
described by the same dependency index. Phase 2 already proves byte identity;
Phase 3 proves that both delivered forms are accepted by the real runtime.

#### 3.3 Exact runtime pump and fence sequence

Keep the already-proven service-test sequence unchanged:

```text
ResourceStreamer::Request
  -> wait for PipelineRequest
  -> acquire TextureResourceObject
  -> TextureResidencyRuntime::RequestTexture
  -> run FramePipeline frames until BindlessReady or Failed
  -> verify one accepted shared GPU Scene contribution

release TextureDemandHandle
  -> run one frame
  -> submit graphics, compute, and copy retirement fences
  -> seal TextureResidencyRuntime and GPU Scene lifetime retirements
  -> wait all three queue fences
  -> retire RHI resources
  -> collect GPU Scene and texture retirements
  -> run one final frame
```

After releasing the resource handle and pipeline request, unregister the loose
generation or unmount the package and wait until ResourcePipeline and
ResourceStreamer counters reach zero. Close the package before deleting test
files. Shut down the derived-data reader and dependency index, then the texture
compiler and BuildSystem, before shutting down the EngineHost. A compiler
shutdown already unregisters itself; no duplicate unregister path is needed.

#### 3.4 Implementation boundary

Phase 3 is one bounded integration pass, not another subsystem:

- replace the direct VTEX/VPAK fixture construction in the existing service
  test with real compiler/DDC/index/materializer/assembler calls;
- add only the test target's required texture-tools/assets include and link
  dependencies;
- preserve the existing resource request, frame pumping, bindless-ready check,
  retirement, drain, and shutdown helpers;
- add no editor integration, asset browser, source watcher, cooker service,
  resource loader, streaming scheduler, texture upload path, or residency
  manager.

The test must use a fresh bounded temporary root so an artifact left by an old
run cannot turn the first build into an accidental cache hit. It must also
remove loose/package temporary files and the test DDC/index data after all
readers and services have released them.

Implementation status: complete. `textureResidencyServiceTests` now:

- compiles one real encoded JPEG fixture through the registered texture
  compiler and persistent BuildSystem cache using a deterministic uncompressed,
  streamable proof profile;
- publishes its dependency record, proves an identical request is a cache hit,
  and proves a valid profile change rebuilds under a different fingerprint;
- publishes and saves that JPEG record in the generic dependency index;
- materializes a loose VTEX and assembles a VPAK from the same immutable JPEG
  VDDC artifact set, without substituting a synthetic runtime texture;
- loads both through the ordinary ResourceStreamer and ResourcePipeline;
- reaches `BindlessReady` through the shared GPU Scene update for both routes,
  reads every resident physical mip back from D3D12, compares its canonical rows
  byte-for-byte with the cooked subresources, and reads back the exact
  `GpuTextureResidency` table row;
- releases demand, seals and waits graphics/compute/copy retirement fences,
  drains streaming state, shuts down build/delivery objects, and removes the
  complete temporary DDC/index/delivery tree.

The focused Debug build completed with zero warnings and zero errors, and the
end-to-end executable passed. No production loader, cooker store, streaming
scheduler, uploader, or residency manager was added or duplicated.

No material, draw, culling, or editor object is needed for this proof.

Exit gate:

```text
encoded image source (the proof uses JPEG)
  -> BuildSystem/DDC/index
  -> loose or VPAK VTEX
  -> ResourcePipeline
  -> TextureResidencyRuntime
  -> stable bindless-ready texture residency handle
```

## 10. Next Action

Phase 1 compiler integration, Phase 2 generic cooked-artifact delivery, and
Phase 3 headless source-to-GPU-residency proof are complete. The texture cook
and runtime-delivery chain is closed for the current scope. Editor discovery,
asset-browser UX, source watching, and project-level cook orchestration remain
separate future editor/tooling work; they are not missing texture runtime
machinery.
