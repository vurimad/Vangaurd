# Model Import Workflow — P1 Planning and Reconciliation Study

Date: 2026-10-09

Status: P1 IMPLEMENTATION CLOSED. This document began as the read-only exact-
current implementation study below; section 11 records the subsequent implementation
closure and validation. Durable schema commit and commit-time stale-plan mutation
revalidation remain P2 responsibilities, and P2 has not been started.

## 1. Scope and phase boundary

P1 adds a versioned `ModelImportSettings` representation and an immutable,
previewable `ModelImportPlan` that reconciles a current inspected model manifest
against persisted model import state and current project reference/catalog state.

P1 must produce deterministic preserved/new/removed/ambiguous decisions and
external-reference blockers with **zero durable mutation**. It must not write
source files or `.vmeta`, begin an `AssetFileTransaction`, recook, publish, or
modify the source catalog.

The exact-current design boundary is:

```text
P0 canonical inspection
    -> P1 versioned settings + reconciliation + immutable plan
    -> P2 stale-safe composite durable commit
    -> P3 model build routing/reference scanning
    -> later cohort build/publication work
```

Existing `RequestAssetReimport()` remains recook of the currently persisted
output schema. Schema-changing refresh/reimport is a distinct path.

## 2. Existing foundations that P1 should reuse

### Asset identity and metadata

`source/assets/include/vanguard/assets/asset_metadata.hpp` already provides:

- opaque 16-byte `AssetId`;
- `AssetOutput { key, type }`;
- importer/version;
- one source-wide versioned settings blob;
- many declared outputs.

`source/assets/src/asset_metadata.cpp` already derives canonical resource paths
from `AssetId + output key`; typed resource identity then includes the declared
resource type. `.vmeta 1` therefore does not need a schema change for P1. The
model-specific settings blob can be versioned internally through
`AssetMetadata.settingsVersion`.

Current limits that P1 must respect:

- `MaximumAssetSettingsBytes = 256 KiB`;
- `MaximumAssetOutputs = 4096`;
- output keys are non-empty, at most 64 bytes, and restricted to
  `[a-z0-9-_]`.

### Multi-output catalog and references

`source/assets/include/vanguard/assets/source_database.hpp` and
`source/assets/src/source_database.cpp` already provide:

- exact typed `LookupOutput(...)`;
- `GetForwardReferences(...)`;
- `GetBackwardReferences(...)`;
- a catalog/reference generation witness through `GetGeneration()`;
- per-source `SourceReferenceScanState`.

P1 must reuse this reference database. Do not add a model-specific second
reference database.

### Exact source/sidecar loading

`source/assets/include/vanguard/assets/project_source_data.hpp` and
`source/assets/src/project_source_data.cpp` already load the exact source and
sidecar represented by one catalog row. They reject a sidecar that no longer
matches the catalog metadata, preventing accidental mixing of new `.vmeta`
bytes with stale catalog identity/settings evidence.

### Existing mutation paths prove the P1/P2 boundary

`source/assets/src/asset_operations.cpp::ExecuteAssetUpdateSettings` changes the
settings blob while intentionally preserving the existing output schema.

`source/assetStreaming/src/project_asset_pipeline.cpp::RequestAssetReimport`
resolves and rebuilds the complete current accepted output set; it does not
rediscover/reconcile outputs.

The new P1 plan therefore must not be forced through either operation.

## 3. Severity-ranked findings

### BLOCKER

**0 intrinsic P1 blockers.**

P1 can be implemented now as a pure deterministic planner. P2 transaction and
publication work must not be pulled forward into the P1 implementation.

### HIGH 1 — persistent correspondence cannot use inspection indices as identity

`source/meshTools/include/vanguard/mesh_tools/model_import.hpp` correctly treats
node/material/primitive indices as inspection-local structural identity.

Exact-current decoder evidence varies by format:

- glTF provenance currently records `sourceGeometryKey = mesh index + 1` in
  `external/assimp/code/AssetLib/glTF2/glTF2Importer.cpp`;
- FBX provenance can use the stronger native `Geometry::ID()` in
  `external/assimp/code/AssetLib/FBX/FBXConverter.cpp`;
- materials expose source table index plus names/properties/usages, but material
  names are display evidence rather than guaranteed persistent identity.

P1 must therefore reconcile with an evidence-ranked matcher:

1. trusted format-native stable identifier when its stability is actually
   defined for that source format;
2. otherwise unique structural/relationship/usage evidence;
3. weaker combined evidence only when it still yields one unique candidate;
4. explicit ambiguity when uniqueness cannot be proved.

Never preserve an output key merely because an array index happens to match.

Persistent Vanguard output identity must be importer-owned and monotonic.
Removed identities must never be silently recycled for unrelated elements.

### HIGH 2 — settings must be bounded, canonical, and deterministic

The P0 manifest intentionally supports far larger inspection state than the
persisted settings budget. P1 settings must therefore store compact decisions
and matching evidence, not a serialized copy of the manifest or imported
payload.

Exact-current metadata equality in:

- `source/assets/src/asset_operations.cpp::SameMetadata`;
- `source/assets/src/project_source_data.cpp::SameMetadata`;

compares output arrays in order. P1 must define one canonical ordering for
settings correspondence records and declared outputs so the same logical plan
always yields the same encoded settings and `SourceImportDescription`.

The settings codec must enforce bounded record counts, duplicate/collision
checks, finite numeric values, valid managed references, valid enum values,
strict trailing-byte rejection, and the 256 KiB outer settings cap.

### HIGH 3 — zero backward references is not sufficient retirement proof

`SourceDatabase::ScanReferences` deliberately keeps previous forward links when
a source cannot be scanned and records one of:

- `ImporterUnavailable`;
- `InputUnavailable`;
- `ScannerUnavailable`;
- `ScanFailed`.

That conservative retention protects known previous references, but it cannot
discover new references hidden inside an unscannable source. Therefore an empty
`GetBackwardReferences(oldOutput)` result is not by itself proof that an output
has no external consumers.

For destructive reconciliation P1 must require authoritative reference coverage.
If reference completeness cannot be established, removal/ambiguous retirement
must carry an explicit blocker such as `ReferenceStateUnavailable` rather than
being treated as consequence-free.

This matches the existing safety discipline in
`source/assets/src/asset_operations.cpp::PlanAssetReferenceReplacement`, which
rejects referencers whose source reference state is not fully scanned.

### HIGH 4 — `ModelImportPlan` must own all plan data and stale witnesses

The plan must not retain borrowed `SourceRecord*`, manifest spans, settings spans,
or `GetBackwardReferences()` spans. All of those are invalidated by later source
database/catalog changes.

The successful plan must own at least:

- the proposed `SourceImportDescription`;
- exact declared output schema;
- old->new correspondence;
- additions/removals/ambiguities;
- copied external-reference blockers;
- managed texture reuse/adoption/missing decisions;
- source destination/root information;
- diagnostics and friendly output descriptions;
- exact source fingerprint witnessed by P0 inspection;
- canonical candidate settings bytes or digest;
- exact prior `.vmeta` digest/existence state;
- `SourceDatabase::GetGeneration()` at planning time;
- existing parent `AssetId` for reimport or project-owned proposed parent
  `AssetId` for first import;
- importer/settings/material-mapper policy versions;
- managed dependency identities plus digest/existence witnesses needed to prove
  the accepted dependency choices remain current.

P2 must revalidate these witnesses immediately before mutation and return an
explicit stale-plan result when any material witness changed.

### MEDIUM 1 — stable material-slot consumers are not represented by `AssetReference`

`source/assets/include/vanguard/assets/assets.hpp::ScannedAssetReference` tracks a
typed resource target plus dependency kind, `Primary/Inner` location and
in-place flag. It cannot identify a prefab/entity override that targets a
specific persistent material-slot ID inside a mesh/composition.

Current backward references are therefore sufficient for whole imported resource
outputs (`MeshResource`, imported `MaterialResource`, imported `TextureResource`,
etc.) but do not by themselves prove that a stable material slot has no external
authored consumers.

The first P1 implementation must either:

- add corresponding managed slot-reference evidence through the existing source
  scanning/reference architecture; or
- conservatively mark slot retirement unresolved when that proof is unavailable.

Do not claim slot referential safety from `GetBackwardReferences()` alone and do
not create a parallel reference database.

### MEDIUM 2 — first-import parent identity needs one explicit planning seam

Reimport already has a persistent parent `AssetId`. First import does not.

Child output references are derived from the parent `AssetId + output key`, while
project identity generation/collision policy belongs to `ProjectAssetPipeline`.
The clean boundary is to let the project owner provide a proposed/reserved parent
`AssetId` before sealing the previewable plan. P1 consumes that identity but does
not independently generate project asset IDs.

P2 must re-check catalog collision/staleness before durable commit.

### MEDIUM 3 — output typing should not create module cycles in meshTools

`meshTools` already knows the mesh runtime type but should not gain hard
material/texture/prefab module dependencies merely so the P1 planner can describe
candidate outputs.

Prefer a small validated `ModelImportOutputTypes`/planning policy supplied by the
project/import orchestration layer. This also prevents declaring a composition
output before the corresponding compiler/router is actually available.

## 4. Recommended `ModelImportSettings` ownership

Keep P1 next to P0 in `meshTools`, with a public engine/tool API independent of
the Editor and `assetStreaming` implementation details.

Suggested files:

```text
source/meshTools/include/vanguard/mesh_tools/model_import_settings.hpp
source/meshTools/src/model_import_settings.cpp
source/meshTools/include/vanguard/mesh_tools/model_import_plan.hpp
source/meshTools/src/model_import_plan.cpp
```

`ModelImportSettings` should own all persisted data. It should contain, at
minimum:

- serializable mesh import policy;
- serializable mesh cook policy, including owned LOD records rather than a
  borrowed `CookSettings::lodLevels` span;
- source-element -> persistent output correspondence;
- stable material-slot correspondence;
- material import/mapper policy and mapper version;
- external/embedded texture resolution choices;
- texture semantic/color-space/cook decisions;
- explicit managed resource selections;
- monotonically advancing output/slot identity allocation state or equivalent
  permanent retired-key reservation data;
- future-version extension room.

Settings store decisions and compact reconciliation evidence. They must not store
full geometry, full material payloads, embedded image bytes, or a complete copy
of the P0 manifest.

Suggested pure codec contract:

```cpp
inline constexpr u32 ModelImportSettingsVersion = 1;

ModelImportSettingsResult EncodeModelImportSettings(
    const ModelImportSettings& settings,
    containers::DynamicArray<u8>& output) noexcept;

ModelImportSettingsResult DecodeModelImportSettings(
    u32 settingsVersion,
    containers::ArraySpan<const u8> bytes,
    ModelImportSettings& output) noexcept;
```

Decode should build a temporary object and move-publish only after complete
validation. The exact wire format may use the existing serialization primitives,
but it must be deterministic and bounded.

## 5. Recommended immutable `ModelImportPlan`

The planner should be a pure headless operation conceptually shaped as:

```cpp
ModelImportPlanResult PlanModelImport(
    const ModelImportManifest& manifest,
    const ModelImportSettings& candidateSettings,
    const ModelImportPlanningContext& context,
    ModelImportPlan& output,
    ModelImportPlanReport* report = nullptr) noexcept;
```

`ModelImportPlanningContext` is borrowed only for the duration of planning and
should provide the current/persisted metadata evidence, source database read
view, existing/proposed parent identity, destination/root information, output
type policy, project-managed dependency choices and explicit planning limits.

`ModelImportPlan` should be move-only, privately own its staged data, and expose
only const values/`ArraySpan<const ...>` after successful construction.

Suggested owned plan categories:

- `SourceImportDescription proposedImport`;
- friendly model-output descriptions for frontend/search use;
- `Preserved`, `Added`, `Removed`, `Ambiguous` reconciliation rows;
- exact old and proposed output references;
- stable slot reconciliation rows;
- external referencer blocker copies;
- reference-state-unavailable blockers;
- managed texture reuse/adoption/missing decisions;
- source copy/destination decision;
- diagnostics/warnings;
- immutable source/settings/catalog/dependency witnesses.

No Editor type should appear in this API.

## 6. Reconciliation rules

For every persisted old correspondence:

1. find current candidates using the strongest available evidence;
2. preserve the old persistent key only for a proved unique 1:1 match;
3. mark an unmatched old identity `Removed`;
4. allocate a new persistent identity for an unmatched current element;
5. classify 1:N, N:1, or otherwise non-unique evidence as `Ambiguous`;
6. never fall back to array position merely to force a match.

For removed or ambiguous resource outputs:

- query `SourceDatabase::GetBackwardReferences(oldOutput)`;
- classify model-owned/generated closure references separately from external
  authored/managed consumers;
- copy blocking external referencers into the plan;
- require trustworthy reference-scan state before concluding that the identity
  is consequence-free;
- preserve externally constrained identities until the blocking conflict is
  resolved explicitly.

If an ambiguous old resource is proven to have no external meaningful consumers,
P1 may retire it and mint new identities for the newly discovered elements, as
the settled design allows.

## 7. Persistent key policy

Output keys are semantic persistent identities, not source names or filenames.

A safe family is conceptually:

```text
mesh-<persistent-id>
mat-<persistent-id>
tex-<persistent-id>
composition
```

The exact textual encoding is less important than these invariants:

- deterministic;
- valid under `IsValidAssetOutputKey`;
- unique within one parent asset;
- never derived solely from mutable display names;
- never recycled for unrelated source content after retirement.

Stable slot IDs require the same monotonic/non-recycling rule.

## 8. P1 headless exit tests

P1 should not be considered closed until headless tests cover at least:

- deterministic settings encode/decode roundtrip;
- malformed/unsupported-version/trailing-data handling;
- 256 KiB settings and 4096 output bounds;
- output-key and correspondence duplicate/collision rejection;
- source A -> source B reorder preserving identities;
- source rename preserving identity only when stronger correspondence proves it;
- duplicate names;
- reused source geometry;
- genuinely new outputs;
- removed outputs;
- ambiguous 1:N and N:1 matching;
- retired key/slot non-reuse;
- externally referenced output removal -> exact blocker;
- same-parent generated closure references -> not external blockers;
- incomplete/unavailable reference scanner -> conservative blocker;
- stable-slot retirement without authoritative slot-reference evidence ->
  conservative unresolved/blocking result;
- managed existing project texture reuse;
- explicit adoption of unmanaged images already under project `Assets`;
- missing texture decision;
- rejection of durable outside-`Assets` dependency choices;
- deterministic plan ordering and repeated-plan equality;
- plan ownership independent of later catalog/manifest mutation;
- zero source/sidecar/catalog/build/publication mutation during planning.

The core A/B oracle is:

```text
old .vmeta + source A
    -> inspect/plan baseline

same old .vmeta + source B
    -> deterministic preserved/new/removed/ambiguous decisions
    -> exact external-reference blockers
    -> zero durable mutation
```

## 9. Downstream work intentionally deferred

These are required by the full workflow but are **not P1 implementation
blockers**:

- P2 composite `ProjectAssetPipeline` commit consuming the already validated
  plan;
- source/sidecar digest or absence preconditions at commit time;
- metadata compare-and-swap/history/rescan/reconciliation;
- source-changed stale-plan rejection immediately before mutation;
- multi-file model-source copy/adoption transaction composition;
- model-specific build routing by exact output identity/type;
- complete model source-reference scanning;
- dependency-index output retirement in the same accepted update;
- cohort build/publication with last-known-good retention until accepted cutover.

Those phases should consume the P1 plan wholesale rather than rerunning planning
or calling `createImportDescription(SourceRecord)` again and risking different
decisions at commit time.

## 10. Final P1 study result

```text
BLOCKER: 0

HIGH:
  1. Persistent correspondence must be evidence-ranked, never index-based.
  2. ModelImportSettings must be bounded, canonical and deterministic.
  3. Destructive reconciliation requires authoritative reference-state proof.
  4. ModelImportPlan must own all reconciliation data and stale witnesses.

MEDIUM:
  1. Stable material-slot consumers need explicit managed evidence or a
     conservative retirement blocker.
  2. First-import parent AssetId must be supplied/reserved by the project owner.
  3. Output type policy should avoid meshTools module cycles.
```

P1 can proceed without reopening P0 architecture and without redesigning the
generic asset metadata/reference model.

## 11. Implementation closure — 2026-10-09

P1 was implemented in the existing model-import translation unit without project
regeneration and without reopening P0. The resulting public surface provides:

- versioned, deterministic and size-bounded `ModelImportSettings` with monotonic
  output/material-slot IDs, retired tombstones, policy/texture decisions and a
  registry-independent wire codec;
- move-only immutable `ModelImportPlan` ownership of the proposed import
  description, reconciliation/ambiguity records, slot decisions, blockers,
  dependency witnesses and exact source/settings/metadata/catalog witnesses;
- evidence-strength-aware Native -> Structural -> Relationship output matching
  that reserves globally bidirectional 1:1 strong matches before weaker fallback
  and quarantines non-1:1 components rather than guessing;
- stable-slot global strong/weak 1:1 reconciliation with no ordinal fallback and
  conservative `StableSlotReferenceUnavailable` retirement blockers;
- exact external-reference blockers and conservative incomplete-reference coverage,
  including missing/unreadable/invalid/conflicted metadata rows whose authored
  reference truth cannot be proven;
- canonical source root/destination/identity and live-catalog checks for first
  import and reimport, exact persisted `.vmeta` snapshot digest proof, and owned
  outside-origin/copy evidence;
- exact managed dependency witnesses whose metadata digest is proven against the
  parsed metadata snapshot used for planning, with multi-output same-source
  selections retained separately.

Final identity/settings and reconciliation/reference read-only audits both reported:

```text
BLOCKER: 0
HIGH:    0
MEDIUM:  0
```

Final validation was **Debug only**, from `/vanguard`:

```text
VS2022 x64 outer MSBuild /m:1: 0 warnings, 0 errors
meshToolsTests:                 all checks passed
scoped git diff --check:       clean (line-ending notices only)
```

No Development configuration validation was run. No durable model commit path was
implemented in P1.

**P1 exit gate: CLOSED. P2 not started.**
