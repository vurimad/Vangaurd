# Vanguard Model Import Workflow Design

Status: pre-implementation design, grounded in exact-current Vanguard asset architecture.

This document defines the intended workflow and ownership model for importing 3D model sources such as FBX, OBJ, and glTF before model-import UI or project integration is implemented. No one model format is special-cased by this architecture. The design keeps authoring logic headless and shared between the Editor, CLI, automation, and other tools.

### Implementation verification rule

This is a durable design record, not a substitute for reading the code at implementation time. Every implementation chunk must re-inspect the exact-current source paths it depends on, verify that the contracts described here still hold, and actively look for counterexamples, hidden ownership assumptions, changed APIs, and existing behavior that makes a proposed seam unnecessary or unsafe. Treat the source observations recorded here as evidence to re-prove, not facts to cargo-cult. If exact-current code contradicts this document, stop and resolve the contradiction explicitly rather than forcing the implementation to match stale prose.

## Non-negotiable source/derived ownership rule

The project `Assets` tree contains authored source files and `.vmeta` sidecars only. Cooked Vanguard resources do not live beside source files.

Example source layout:

```text
Assets/Characters/soldier.<fbx|obj|gltf|glb>
Assets/Characters/soldier.<fbx|obj|gltf|glb>.vmeta
```

Generated `.vmesh`, `.vmat`, `.vtex`, `.vshader`, and other cooked `.v*` resources remain in DerivedData/DDC/publication/package storage according to the existing project/runtime layout rules.

## Native Vanguard multi-output model

Vanguard already has the correct primitive for model-owned imported children. One physical source asset owns one stable `AssetId`, and its `.vmeta` declares one or more typed `AssetOutput` records.

Each declared output has a persistent importer-owned key and a resource type. Its canonical logical resource path is derived from:

```text
assets/<AssetId>/<output-key>
```

Therefore model-owned imported children should use the existing `AssetId + AssetOutput.key + type` model rather than introducing a second parent-GUID/local-ID system.

A model may conceptually declare outputs such as:

```text
car.<fbx|obj|gltf|glb>
  mesh-body       -> MeshResourceType
  mesh-wheel      -> MeshResourceType
  mesh-door       -> MeshResourceType
  mat-a13c...     -> MaterialResourceType
  mat-82bd...     -> MaterialResourceType
  tex-771e...     -> TextureResourceType
  composition     -> PrefabResourceType
```

These are logical outputs, not source-side filenames.

Different output types and multiple outputs of the same type are both supported by the current metadata and source database model. Exact output references remain unambiguous. Type-only resolution is intentionally ambiguous when more than one output of that type exists.

Do **not** force an imported model into one `mesh-main` output merely to avoid same-type output ambiguity. The importer should preserve the source's semantic distinction between reusable geometry, render/material primitives, and placement hierarchy:

```text
source geometry definition  -> candidate Vanguard MeshResource output
source primitive/material partition -> candidate VMESH submesh
source node/instance/transform -> imported hierarchy/component placement
```

This is a semantic starting point, not an unconditional format-specific 1:1 rule. Import settings may deliberately regroup or merge geometry, but the default import should not destroy independent placement/reuse information merely because several pieces arrived in one physical model container.

For example, a car may legitimately produce one `mesh-wheel` output referenced by four hierarchy nodes, while `Tire` and `Rim` remain submeshes inside that one VMESH. `Body`, `Wheel`, and `Door` are separate mesh outputs only when the inspected source represents them as independently reusable/placeable geometry definitions or when explicit import policy creates that grouping.

## User-visible import flow

Importing a model is not one synchronous success/failure operation. The user-visible lifecycle separates inspection/planning, durable project import, compilation, and publication:

```text
Inspect
   |
   v
Choose settings
   |
   v
Build import plan
   |
   v
Validate plan is still current
   |
   v
Atomic source/.vmeta commit
   |
   v
Compile required output closure
   |
   v
Publish complete ready generation
```

Nothing mutates project source state during inspection or planning.

The UI must therefore distinguish at least these states rather than treating the Import button as equivalent to a ready runtime asset:

```text
Inspecting...
Ready to Import
Imported / Compiling
Ready
Imported / Build Failed
```

`Imported` means the authored source and its Vanguard metadata/identity are durably present in the project. `Ready` means the required generated resource closure for the current publishable model generation compiled successfully and was published for normal use. Those are deliberately different facts.

### 1. Inspect

The backend performs a read-only inspection of the physical source and produces a model import manifest.

Inspection may be substantial for large model files and must be treated as cancellable asynchronous work with visible progress/diagnostics rather than a blocking UI call. The frontend should be able to show coarse-grained progress such as source parsing, geometry/material discovery, texture/dependency discovery, and manifest finalization where the backend can report it. Cancellation during inspection/planning/preview is safe because no durable project mutation has occurred.

The manifest contains, as available from the source format:

- source node hierarchy, local transforms, and node-to-geometry instance relationships;
- reusable geometry definitions and their primitive/submesh structure;
- stable structural evidence for source elements;
- material slots;
- imported material descriptions;
- embedded texture records;
- external texture references;
- joints/skeleton information;
- future animation information;
- warnings and unsupported/unmapped material properties;
- source fingerprint and diagnostics.

The manifest is a backend/tool contract. The Editor only visualizes it. A CLI consumer must be able to request the same inspection.

The current low-level Assimp-backed mesh importer already sees much of the required source information, including imported materials, embedded textures, external dependencies, joints, Assimp node paths, mesh references, and a source fingerprint. However, its current `ImportedMesh` representation is already too flattened for the production compound-model workflow: it recursively accumulates parent/node transforms into vertex positions and emits encountered Assimp meshes as `ImportedSubmesh` records. That preserves enough provenance for a geometry-only cook, but it discards the reusable geometry-definition versus placement-instance distinction needed for authored component hierarchy and resource reuse.

The production inspection boundary should therefore introduce or evolve toward a canonical model manifest before VMESH cooking, conceptually separating:

```text
ModelImportManifest
  Nodes / hierarchy / local transforms
  GeometryDefinitions
    Primitives
  Node -> GeometryDefinition instances
  Materials
  Textures
  Skeleton / joints
  future animation data
```

For glTF/GLB specifically, this corresponds naturally to nodes as hierarchy/placement, meshes as reusable geometry definitions, and mesh primitives as submesh-like render/material partitions. FBX, OBJ, and other supported formats should be decoded into the same format-neutral manifest rather than leaking format-specific concepts above the decoder boundary.

### 2. Serializable model import settings

The user chooses policies over the inspection result. Those decisions are encoded in one versioned model-import settings blob stored in the model source `.vmeta`.

The settings should contain decisions, not duplicated imported payloads. They include:

- mesh import/cook settings;
- source-element to persistent output-key correspondence;
- material import policy;
- embedded versus external-to-container texture provenance and resolution policy;
- per-texture semantic/color-space/cook policy;
- stable material-slot identity and canonical source-material association;
- material mapper version/policy;
- future skeleton and animation policy;
- stable resource references chosen by the user.

The imported model-source bytes remain the authoritative source of imported material and embedded texture content. The `.vmeta` does not store full embedded image payloads or a complete copy of the scan manifest.

The current asset system has one source-wide settings blob rather than first-class per-output settings. Model import therefore owns a structured settings table keyed by stable output/source-element identity and synthesizes the appropriate compiler input per requested output.

### Post-import settings editing and dirty state

`Edit Import Settings` must edit a **working copy** of the persisted `ModelImportSettings`; changing a checkbox, scale, axis policy, hierarchy policy, texture policy, or other setting must not immediately rewrite `.vmeta` or recook/publish the real asset graph.

Conceptually there are two states:

```text
Persisted settings
  = settings currently stored in the model `.vmeta`
  = settings that produced the currently published imported asset

Working settings
  = temporary Editor-owned candidate settings
  = may differ from persisted settings while the user is editing
```

As soon as the working copy differs from the persisted settings, the settings view enters an explicit **Modified/Dirty** state. The currently imported model and its published resource generations remain unchanged until the user explicitly applies the candidate settings.

Dirty working settings may still drive the transient rendered preview described later in this document. This is an intentional distinction:

```text
working-settings edit
    -> transient preview may update
    -> durable project state does not change

Apply/Reimport
    -> validate/plan/reconcile
    -> commit settings
    -> rebuild/publish affected real outputs
```

The primary commit action should therefore be explicit and communicate that it may rebuild a resource graph. `Apply/Reimport` is an appropriate working label; a final UI may use `Apply Changes` if the surrounding copy clearly states that applying can reimport/rebuild affected outputs.

Applying must use the same safe planning path as reimport rather than writing settings first and discovering consequences afterward:

```text
current source + working settings + current persisted output correspondence
    -> fresh inspection as required
    -> candidate ModelImportPlan / reconciliation
    -> validate structural and dependency consequences
    -> surface additions/removals/ambiguities/destructive effects
    -> atomic commit of accepted `.vmeta` settings/output schema changes
    -> rebuild/publish affected resources
```

A setting change may invalidate many downstream resources, so `Apply/Reimport` must be allowed to report the affected scope before commit, for example changed mesh outputs, materials/textures, generated composition, removed hierarchy nodes, or stable-reference implications.

The settings view should also provide two distinct non-committing actions:

- **Revert**: discard the current working edits and restore the working copy from the currently persisted `.vmeta` settings. This does not reimport the real asset because its persisted state never changed.
- **Reset**: replace the working copy with the importer's current defaults. Resetting makes the view dirty when those defaults differ from persisted settings, but it still does not commit or reimport until `Apply/Reimport` is chosen.

Closing or otherwise destroying a settings view with unapplied dirty settings must never silently apply or silently discard them. The user must be given an explicit resolution such as:

```text
Import settings have unapplied changes.

[ Apply/Reimport ] [ Discard Changes ] [ Cancel ]
```

`Apply/Reimport` commits through the validated plan; `Discard Changes` closes while retaining the currently persisted/imported state; `Cancel` aborts the close. Navigation that does not destroy the working settings state may leave the view dirty, but any lifecycle event that would lose the working copy must follow the same rule.

### 3. Declarative import plan

Inspection plus settings plus current project/source state produce a `ModelImportPlan`.

The plan resolves every requested action before mutation, including:

- the model source destination under `Assets`, including copying a source selected/dropped from outside the project into that destination;
- the model `.vmeta` contents;
- declared model-owned outputs;
- imported material outputs;
- imported embedded texture outputs;
- reuse of existing managed project textures;
- adoption of unmanaged texture source files that are already under `Assets`;
- resolution of external-to-container texture sources that are also under `Assets`;
- missing dependencies;
- stable source-derived material-slot/output associations;
- warnings, lossy mappings, and unsupported properties;
- output additions/removals/reconciliations during reimport.

The plan must be previewable by any frontend before commit.

The plan is valid only for the source state it inspected. At minimum it carries the inspected source fingerprint plus any other revision/witness data required to prove that the accepted source/settings/dependency inputs have not changed underneath it. Immediately before durable commit, Vanguard must revalidate those witnesses. If the source changed after inspection, the operation must stop before mutation and report a specific stale-plan state such as:

```text
The source changed since it was inspected.

[ Rescan ]
```

Do not surface an opaque transaction/compiler error for this case, and do not silently commit a plan produced from different source bytes.

### 4. Atomic commit

The complete accepted plan commits as one logical durable transaction.

For example, an import may need to perform all of these together:

```text
Create <model-source>.vmeta
Adopt body_normal.png in place
Create body_normal.png.vmeta
```

These operations must not be implemented as an unsafe sequence of unrelated asset imports where an intermediate failure leaves half the import committed.

The existing `AssetFileTransaction` already supports bounded multi-entry create/write/move/delete staging across authorized writable roots. Model import therefore needs a higher-level composite authoring operation that prepares and commits the whole import plan through one durable transaction.

The Editor does not own this transaction. Editor and CLI call the same backend operation.

Once the atomic durable commit has actually begun, it is not an operation that may be abandoned halfway through merely because the user presses Cancel. It must either commit the accepted source/metadata transaction completely or fail/roll back according to the transaction contract. Cancellation applies naturally to pre-commit inspection/planning/preview and to cancellable queued build work after commit, not to leaving a half-written durable asset.

Successful durable commit does **not** imply successful resource compilation. If source + `.vmeta` commit succeeds and a later mesh/material/texture/prefab build fails, the model remains a valid imported project asset whose build state is failed and retryable. The UI must report the failing output/build diagnostics rather than reclassifying the whole durable import as if its transaction had failed.

## Embedded textures

Embedded texture provenance is clean under the existing source/derived rule:

- physical authored bytes live inside the model source;
- asset identity is a model-owned `AssetOutput` declared in the model `.vmeta`;
- cooked texture artifacts live in DerivedData/DDC/publication/package storage;
- no source image file is created automatically in `Assets`.

The Asset Browser can therefore present:

```text
soldier.<fbx|obj|gltf|glb>
  Textures
    Body_BaseColor    Imported · Embedded · Read-only
    Body_Normal       Imported · Embedded · Read-only
```

These children are real typed resource outputs, not Editor-only fake nodes.

The exact-current texture compiler can consume generated encoded image bytes, so embedded PNG/JPEG/TIFF/EXR/DDS payloads can be routed through it without creating a physical source image. Raw Assimp embedded pixels/custom containers do not currently pass the texture compiler's encoded-source inspection and require a canonical raw-image compiler input path.

## External textures

In this design, **external** means external to the model/container file, not external to the project's `Assets` tree.

For example, a model source such as FBX, OBJ, or glTF may reference `Assets/Characters/Textures/body_normal.png` rather than carrying those image bytes inside the model container/source. That is an external texture dependency of the model, but it is still a Vanguard project source because it lives under `Assets`.

Paths outside `Assets` are outside Vanguard's managed asset universe **after import**: they are not durable project dependencies and are never retained as absolute workstation paths. This does **not** mean an import must start from a file already under `Assets`. A user may choose or drag a model source from anywhere on disk; the import transaction copies the accepted authored source into the selected `Assets` destination before committing its `.vmeta` and durable dependencies. The invariant is that committed project sources and dependencies live under `Assets`, not that their pre-import origin must already be there.

External-to-container texture references under `Assets` should be resolved explicitly according to provenance and management state.

| Source reference | Default/recommended behavior |
| --- | --- |
| Existing managed project texture | Reuse its existing texture resource directly |
| Image already under `Assets` but without `.vmeta` | Offer explicit adoption as a normal project texture |
| Missing image | Show Missing and require resolution, a valid fallback, or an explicit supported ignore policy |
| Embedded image | Keep as a model-owned imported texture output by default |

The importer must not silently adopt arbitrary project files. It also must not create durable dependencies on paths outside `Assets`; such paths are simply outside the project asset model.

### Bulk project-image adoption

If a model references many image files that already exist under `Assets` but do not yet have Vanguard metadata/resource identity, the import workflow must not force one modal confirmation per image. The import window should provide a bulk action such as:

```text
[ Adopt All Unmanaged Project Images ]
```

This applies the normal texture-adoption policy to every compatible unresolved image row in the candidate import plan. Individual rows remain editable so the user can make exceptions before commit. Adoption means registering the existing image source in place as a normal Vanguard texture asset, including creation of its `.vmeta`; it does not copy or move the image.

The summary shown before commit should make the bulk consequences explicit, for example how many image sources will be adopted, how many existing managed textures will be reused, and how many references remain unresolved.

### Missing texture resolution

If a referenced image cannot be resolved to a valid project source/resource, its row should remain visibly `Missing` and expose explicit recovery actions rather than opening a sequence of modal dialogs. At minimum:

```text
Missing: glass_normal.png

[ Locate in Assets... ]
[ Reuse Existing Texture... ]
```

`Locate in Assets...` selects the correct image source already inside the project `Assets` tree. `Reuse Existing Texture...` selects an already-managed Vanguard `TextureResource`. Neither action may create or preserve a durable reference to an arbitrary path outside `Assets`.

### Import entry point

The model source itself does **not** need to originate under `Assets`. A user may select a model through an OS file picker or drag it from the operating system into a writable Asset Browser destination. The candidate import plan records the chosen `Assets` destination, and the accepted source bytes are copied there as part of the same durable import transaction that creates the model `.vmeta` and any explicitly accepted extra source operations.

For example:

```text
D:/Downloads/car.glb
        -> import to Assets/Vehicles/

Assets/Vehicles/car.glb
Assets/Vehicles/car.glb.vmeta
```

The outside file is only the **pre-import origin**. After commit, Vanguard's durable identity/dependency graph refers only to project sources/resources under `Assets`; it never retains `D:/Downloads/car.glb` or any other external absolute path as a project dependency.

Structural companion files require explicit per-format policy rather than being treated like ordinary texture dependencies.

For the first model-import version, **external glTF buffer files are not supported**. A `.gltf` is importable only when its required buffer payload is self-contained in the `.gltf` itself (for example through an embedded data URI). A `.glb`, which packages the glTF JSON and binary buffer data into one container, is also supported. If a `.gltf` declares a required external buffer URI such as `car.bin` or `buffers/geometry.bin`, inspection must reject the model with a clear unsupported-structure diagnostic before import.

Consequently, Vanguard must not create, adopt, or display `.bin` buffer companions beside an imported `.gltf` in the Asset Browser for this first version. Support for external glTF buffers may be revisited later as a separate source-closure feature; it is deliberately out of scope now rather than partially supported.

OBJ/MTL companion behavior is deliberately deferred for now and should not inherit glTF behavior by analogy.

## Imported materials

A material discovered from the model source becomes a model-owned `MaterialResourceType` output by default. Prefer the term **model-owned imported material** over **embedded material** at this layer: embedded versus companion/external-to-container is source provenance, while model-owned describes the durable asset ownership result.

It does not receive its own independently authored `.material` source file by default. Its source owner remains the model file, so it is semantically read-only.

The Editor may present this truth directly:

```text
Imported from Characters/soldier.<fbx|obj|gltf|glb>
Source-owned material — read only
```

This lock is structural rather than cosmetic: the imported material has no independent authored source row or independent `.vmeta`.

### Physical source ownership versus runtime resource identity

The absence of an independent `.material` file does **not** make the imported material a second-class or Editor-only object. Vanguard source ownership and runtime resource identity are separate concerns.

For example, the physical source tree may contain only:

```text
Assets/Characters/robot.glb
Assets/Characters/robot.glb.vmeta
```

while `robot.glb.vmeta` declares logical outputs such as:

```text
mesh-main   -> MeshResourceType
mat-a13c    -> MaterialResourceType
mat-b821    -> MaterialResourceType
tex-42fe    -> TextureResourceType
```

There is no requirement for `Body.material`, `Glass.material`, or cooked `.vmat` files beside the model source. Each declared output still has its own stable typed `ResourceReference`, derived through the normal `AssetId + output-key + type` rules, and participates in build, dependency, streaming, rendering, and packaging exactly as that resource type normally does.

Model-owned imported materials are **not** artificially restricted to model-internal use. Because they are ordinary typed `MaterialResource` outputs, another managed Vanguard asset may legitimately store a reference to one just as it may store a reference to a model-owned `MeshResource`. Scripts or other authored data participate in the same rule when their source format/importer exposes the reference through Vanguard's managed reference-scanning system. The importer must therefore use actual backward-reference information when deciding whether a material output's persistent identity is externally observable rather than assuming that imported materials are always private implementation details.

This does not make ordinary imported-material edits a reconciliation event. Changes to color, roughness, texture bindings, translated parameters, or any other content of an unambiguously matched material simply recook the same `MaterialResource` identity and propagate normally to all consumers. Referential review is required only when the persistent identity itself would otherwise become ambiguous or disappear.

Conceptually:

```text
SOURCE OWNERSHIP                    RUNTIME RESOURCE IDENTITY

robot.glb                           robot / mesh-main -> MeshResource
robot.glb.vmeta       ----------->  robot / mat-a13c  -> MaterialResource
                                   robot / mat-b821  -> MaterialResource
                                   robot / tex-42fe  -> TextureResource
```

Read-only therefore follows from ownership rather than from an Editor flag: there is no independently authored material source to edit, rename, move, or delete. Extraction creates such an independent authored source explicitly; until then, the parent model source owns the imported material.

### Material translation layer

Imported materials should use a versioned engine-level material mapper:

```text
Assimp/imported material description
        |
        v
versioned Vanguard material mapper
        |
        v
canonical Vanguard material input
        |
        v
VMAT compiler
```

The mapper does not live inside Assimp code or Editor UI.

It maps source material concepts such as:

- base color;
- normal;
- metallic;
- roughness;
- specular;
- emissive;
- opacity/blend policy;
- two-sided state;
- texture semantic and UV set;
- color-space policy;
- unsupported or lossy properties.

Vanguard materials are shader/reflection driven rather than a fixed baked PBR struct, so model import should target a defined Vanguard standard imported-surface material/domain/template and report any source property that cannot be represented faithfully.

The current material compiler architecture already supports canonical material inputs and material resource dependencies. Exact-current production code does not yet provide the persisted editable `.material` source codec/`SourceImporterDescriptor`, so normal editable material extraction remains dependent on finishing that authoring layer.

## Mesh material slots and composition-level overrides

The imported model/mesh asset owns only its canonical source-derived material-slot definition.

For example, the imported mesh may canonically contain:

```text
Body -> Imported(mat-a13c)
```

That relationship is part of the imported asset. The model `.vmeta` does not store a user replacement such as `Materials/my_body`, and changing the model import settings is not the mechanism for replacing a material in gameplay/editor composition.

Material replacement belongs to the entity/prefab composition that consumes the mesh. A component such as `StaticMeshComponent` may own a slot-override table conceptually like:

```text
Mesh: soldier mesh output
Material overrides:
  BodySlot -> Materials/my_body
```

The component/prefab therefore has only two semantic choices for each slot:

```text
Use Mesh Material
Override -> <MaterialResourceReference>
```

This does not require serializing an explicit mode value for every slot. The preferred composition representation is an override table only:

```text
StaticMeshComponent
  mesh = <MeshResourceReference>
  materialOverrides = {
    <stable-slot-id> -> <MaterialResourceReference>
  }
```

If a stable slot ID is absent from `materialOverrides`, the component uses the material reference stored canonically in that mesh slot. An entry exists only when the composition intentionally replaces that material. This keeps prefab/entity data sparse and prevents it from duplicating the mesh asset's defaults.

`Use Mesh Material` is deliberately provenance-agnostic. The mesh's canonical slot material may have been reconstructed from material data embedded in the model, from a companion/external-to-container source file under `Assets`, or from another importer-supported source relationship. The compositor does not need to know or encode that provenance. It simply uses the canonical material reference stored by the mesh.

Different entities may use the same mesh with different material overrides without cloning or mutating the mesh asset.

This ownership boundary has several consequences:

- the model importer never writes user material overrides into the model `.vmeta`;
- the model Asset Browser/inspector shows canonical imported slot/material relationships only;
- prefab/entity serialization owns replacement material references;
- clearing a component override naturally returns to `Use Mesh Material` and therefore the mesh's canonical material reference;
- model reimport never edits prefab/entity override values;
- stable material-slot identity must survive reimport whenever source correspondence is unambiguous so existing component overrides continue to target the same logical slot;
- if a source slot disappears or becomes ambiguous, affected component overrides become stale/unresolved and must not be silently rebound to a different slot.

The imported material output remains part of the model's output schema regardless of whether some or all consuming entities override that slot in their own composition.

## Persistent source-element identity and output keys

Output keys are persistent semantic identities. They are not display names, physical filenames, or transient array positions.

For example:

```text
mat-a13c...
```

may be the persistent key while `Body` remains the current display/source name.

The model import settings should retain compact source-element correspondence records so later scans can reconcile current source elements to existing output keys.

Matching order should prefer:

1. a format-native stable identifier when available;
2. otherwise structural evidence such as node/submesh relationship, source material relationship, usage, and names;
3. explicit ambiguity when evidence is insufficient.

The importer must not silently map an old output key to a different semantic object merely because an array index happens to match.

Existing `ImportedSubmesh::stableId`, source node paths, material names, and related data are useful matching evidence but should not automatically be declared eternal persisted identity without proving their stability under realistic DCC edits.

Removed output keys should not later be recycled for unrelated source elements.

## Reimport and output-schema reconciliation

The exact-current `RequestAssetReimport()` behavior is effectively recook of the existing persisted `.vmeta` output schema. It preserves the source `AssetId` and resolves the already-declared output set.

That behavior remains useful, but model import also requires a distinct source-refresh/reconciliation operation.

### Recook

Rebuild all currently declared outputs with the existing settings and output schema.

### Refresh/Reimport from Source

Rescan the physical model, compare the new manifest with persisted model import state, reconcile source elements to existing output keys, and preview a diff before changing metadata.

Example diff:

```text
Body material       preserved
Helmet material     renamed / matched
Eyes material       removed
Cape material       added
Normal texture      unchanged
```

Rules:

- preserve every unambiguous existing output key;
- preserve stable material-slot identities used by external prefab/entity compositions whenever correspondence is unambiguous;
- allocate new keys only for genuinely new source elements;
- never silently reuse removed keys for unrelated content;
- never guess an ambiguous correspondence for a persistent identity that has external managed consumers or other externally meaningful authored state;
- if an ambiguous old output has no external managed referencers and no other externally meaningful state, the importer may retire that old identity and allocate new identities for the newly discovered source elements instead of forcing the user to solve a consequence-free ambiguity;
- surface destructive removals before commit;
- check external referencers before removing imported outputs.

### Externally referenced identities are blocking reconciliation conflicts

A source-side deletion is **not** equivalent to an immediate project-side deletion. If a source element disappears but its persisted Vanguard output or stable slot is still referenced elsewhere in the project, reimport must treat that as a first-class blocking reconciliation conflict rather than merely listing the child as `Removed` and invalidating its referencers. This rule is resource-type agnostic: `MeshResource`, model-owned `MaterialResource`, model-owned `TextureResource`, and future model-owned typed outputs all receive the same referential safety when they have external managed consumers.

For example, if the currently imported model owns:

```text
car.glb
  mesh-body
  mesh-wheel
  mesh-windshield
```

and another prefab contains:

```text
BrokenCar.prefab
  WindshieldComponent.mesh -> car / mesh-windshield
```

then deleting the windshield in Blender and saving `car.glb` should produce a review entry conceptually like:

```text
REMOVED
  Windshield                         MeshResource
  output: mesh-windshield

  BLOCKING: referenced by 2 project assets
    Vehicles/BrokenCar.prefab
      WindshieldComponent.mesh -> car/mesh-windshield
    Garage/Display.prefab
      DisplayGlass.mesh -> car/mesh-windshield

  [ Find References ] [ Open Referencers ]
```

While that review remains unresolved, Vanguard should keep the **last-known-good published `mesh-windshield` resource active**. The physical source may no longer contain the windshield, but the project-visible imported state does not remove the output until the user has deliberately resolved the dependent references and accepted the reconciliation. This prevents an ordinary DCC save from immediately breaking already-authored project content.

The importer must not automatically retarget those referencers to another newly discovered child merely because names, indices, material usage, or topology look similar. Any deliberate replacement happens in the owning prefab/entity/resource that contains the reference, or through a future explicit user-authorized retarget operation; it is not inferred by model reimport.

The same rule applies to other externally meaningful stable identities, not only whole mesh outputs. For example:

- if `car / mat-body` is a model-owned imported `MaterialResource` and another managed asset refers to it, an ambiguous/removal reconciliation must surface that referencer exactly as it would for `car / mesh-body`;
- if the same material merely changes its translated content while correspondence remains unambiguous, it recooks automatically under the same `MaterialResource` identity and no review is required;
- if a stable material slot disappears while prefab/entity material overrides still target that slot, the review must surface those affected overrides before the slot identity is retired;
- future externally addressable animation clips, sockets, skeleton elements, or other imported identities should follow the same referential-impact rule.

References from the model's **own generated output closure** do not by themselves make an identity externally constrained. For example, the generated composition referring to a model-owned mesh, or a model-owned mesh referring to a model-owned imported material, is importer-owned structure that can be regenerated consistently as part of the same accepted model update. The blocking condition is authored/managed state outside that model-owned generated closure, or another explicitly preserved external identity such as a prefab material-slot override.

Once all blocking referencers are resolved, accepting the reimport may retire the removed output/slot. Its old persistent key/identity remains reserved or tombstoned according to the stable-identity rules above and must not later be recycled for unrelated source content.

A review surface should therefore distinguish at least:

```text
Removed, no external consumers        -> may retire/recreate without referential blocking
Removed, externally referenced        -> blocking; cannot retire identity yet
Ambiguous, no external consumers      -> may retire old identity and create new outputs
Ambiguous, externally referenced      -> blocking; requires explicit correspondence or removal resolution
```

This requires a new atomic metadata/output-schema reconciliation operation that updates the model's settings and declared outputs while preserving its parent `AssetId`.

## Extract imported children

Model-owned logical children are not independently duplicable assets. A mesh/material/texture output inside `car.glb` has no separate authored source file to duplicate, so the model Asset Editor must not expose a generic `Duplicate Asset` operation for such a child.

Duplicating usage of a model-owned mesh/material belongs in the consuming prefab/entity composition, where components/instances may be duplicated or added while continuing to reference the same logical resource. Generic duplication of `car.glb` itself is a parent-source operation and duplicates the whole model source asset and its output set.

When the user actually wants an independent authored asset derived from a model-owned child, that is an explicit `Extract Imported Output` operation instead.

### Extract material

Example:

```text
Extract Material...
-> Assets/Materials/Body.material
-> Assets/Materials/Body.material.vmeta
```

The extracted material becomes a normal independent authored project asset. The imported source-owned material remains unchanged.

The operation may offer:

- keep references to model-owned embedded textures;
- extract embedded texture dependencies too for a fully independent material.

Extraction does not mutate the model asset's canonical slot binding. If the user wants to use the extracted material, that assignment is made in the consuming prefab/entity component's material-override table.

This action depends on completion of Vanguard's normal persisted editable material authoring format/importer.

### Extract embedded texture

An embedded encoded image may be written as a normal editable image source only on explicit user request.

If the original embedded payload is already an encoded supported image, extraction should preserve exact original bytes where practical. Raw embedded pixels require a defined lossless source-image encoding policy before extraction is exposed.

## Build-system routing

The existing importer/build contracts already allow one authored source document to create build requests for several output types and several artifact compilers.

A model importer can therefore compose per-output build requests such as:

- model bytes/settings -> mesh compiler request;
- imported material description -> canonical material input -> VMAT compiler;
- embedded encoded image bytes -> texture source request -> VTEX compiler;
- future skeleton/animation canonical inputs -> their respective compilers.

Sibling generated dependencies, such as an imported material referring to a model-owned embedded texture output, use ordinary typed resource references and resolve through the normal generated-dependency graph.

No parallel model-specific runtime resource system is required.

### Build completion and atomic model publication

Per-output compilation may complete independently, but publication of a new model generation must not expose a partially updated generated closure. The model's required publishable closure includes the outputs needed for that generation to function coherently, such as its referenced meshes, model-owned materials/textures, generated composition, and their required generated dependencies.

For example, after a successful first import:

```text
car.glb                     Imported · Build Failed

mesh-body                   compiled successfully
mesh-wheel                  BUILD FAILED
mat-body                    compiled successfully
composition                 not publishable
```

Successful artifacts do not need to be discarded. They may remain in DDC/build caches and be reused by a retry. But Vanguard must not present that incomplete closure as a normal `Ready` model generation.

The distinction is especially important on reimport. If generation A is currently published and candidate generation B compiles `Body` and its materials but fails `Wheel`, publication of B is rejected as a whole and generation A remains the last-known-good active generation:

```text
Published generation A     remains active

Candidate generation B
  Body                      success
  Wheel                     FAILED
  Materials                 success

Result                      B not published
```

This is not a requirement to make every compiler invocation one monolithic transaction; build work and caching may remain granular. The invariant is that the externally observable model generation is coherent and publication is all-or-nothing for the required closure.

For a first-ever import there is no previous ready generation to retain. If build fails after durable import, the source and `.vmeta` stay managed under `Assets`, its declared logical outputs remain inspectable/addressable for diagnostics and retry, but there is no ready generated composition/model generation to place or render normally until the required closure succeeds.

Cancelling queued/in-progress build work after durable import leaves the asset imported but not `Ready`; it must remain retryable. A frontend should surface build progress and per-output diagnostics without conflating those with the earlier source-import transaction.

### Per-output build resolution

The intended model importer uses the existing output-oriented project build flow rather than inventing a model-subasset compiler framework. Given a requested typed output, the project build system resolves the owning source record and asks that source importer to compose the build request for that exact output.

Conceptually:

```text
ResourceReference(robot / mat-a13c : MaterialResourceType)
        |
        v
SourceDatabase::LookupOutput(...)
        |
        v
owning source = robot.<model-format>
        |
        v
model importer CreateSourceBuildRequest(..., requestedOutput = mat-a13c)
        |
        v
versioned imported-material mapper
        |
        v
canonical MaterialValueInput
        |
        v
existing MaterialResource compiler
```

Equivalent routing applies to mesh and model-owned texture outputs. Dispatch is by declared output identity/type and importer-owned correspondence, not by scattering format-specific `if (fbx)`, `if (gltf)`, or similar branches through the asset/build/runtime layers. Source-format decoding stays behind the model import/inspection boundary.

The exact-current material artifact compiler already discovers ordinary generated dependencies such as shader, pipeline, and resource-value references. A model-owned imported material should therefore become a normal `MaterialResource` artifact with ordinary texture/shader/pipeline dependencies once the model importer has synthesized its canonical material input.

### Mesh-to-material dependency requirement

The mesh side must likewise expose its canonical per-slot `MaterialResourceReference`s as ordinary generated dependencies. The exact-current mesh path still centers dependency discovery and cooked slot assignment around `MeshImportSettings.defaultMaterial`; the production model-import integration must rework that seam so a mesh containing, for example:

```text
Body  -> robot / mat-a13c
Glass -> robot / mat-b821
```

discovers those distinct material outputs as required generated dependencies. This is necessary for correct build invalidation, recursive compilation, runtime dependency metadata, and packaging. Do not special-case the renderer to recover missing dependency information later.

### Runtime and package behavior

After compilation, provenance is intentionally irrelevant to runtime systems. A material produced from a model-owned imported output is simply a `MaterialResource`; a texture produced from a model-owned texture output is simply a `TextureResource`. Rendering, streaming, and resource loading must not introduce `EmbeddedModelMaterial`, `FbxMaterial`, or other provenance-specific runtime resource classes.

The package planner should see the same ordinary generated dependency graph, for example:

```text
MeshResource
  -> MaterialResource Body
       -> ShaderResource
       -> PipelineResource
       -> TextureResource BaseColor
       -> TextureResource Normal
  -> MaterialResource Glass
       -> ...
```

With generated-dependency inclusion enabled, package planning already walks generated dependencies, resolves their indexed build records/artifacts, and adds those resources to the package plan. Therefore a VPAK can contain the mesh, its model-owned material resources, their textures, shaders, pipelines, and other runtime dependencies even though the source tree contains only the model source and its `.vmeta`. Packaging consumes compiled resource identities/artifacts and dependency metadata; it does not require one physical authored source file per packaged runtime resource.

### Generated prefab composition output

The imported model hierarchy should also be exposed as a model-owned logical `PrefabResourceType` output. This satisfies the exact-current world contract that persisted placements reference prefab resources without forcing a visible or hidden authored `.prefab` source file for every imported model.

Conceptually:

```text
car.glb
car.glb.vmeta
  outputs:
    mesh-body       -> MeshResourceType
    mesh-wheel      -> MeshResourceType
    mat-body        -> MaterialResourceType
    tex-body        -> TextureResourceType
    composition     -> PrefabResourceType
```

When `composition` is requested, the model importer reconstructs the imported component hierarchy from the source plus durable importer settings/correspondence stored in `.vmeta`, resolves its ordinary typed mesh/material dependencies, synthesizes a prefab `CookDescription`, and produces normal cooked prefab bytes. The runtime and `CellMaterializer` see only a normal typed prefab resource; they do not know or care whether those bytes originated from FBX, OBJ, glTF, GLB, or an authored prefab source.

Dragging the model into a world therefore creates a normal prefab-backed placement whose prefab reference targets the model-owned generated `composition` output. The default drag operation should not ask the user to create or place an authored prefab file and should not create a hidden/"ghost" prefab source. The generated composition is a logical build output, not another source-tree asset.

Creating an editable authored prefab remains an explicit higher-level user action. If the user chooses to turn the model instance/composition into a reusable gameplay composition, Vanguard may create a real project prefab source at a user-selected path; that authored prefab can then add gameplay, physics, audio, lights, overrides, or other composition-owned state while continuing to reference the model's ordinary typed outputs.

This workflow depends on extending prefab/component authoring so one prefab entity can persist the same-entity placed-component hierarchy described by the imported model. Exact-current runtime transform machinery already supports placed-component transforms and attachments, but exact-current prefab component records do not yet encode parent-component identity/local hierarchy as authored data. That is a prerequisite implementation seam, not a reason to materialize model nodes as separate entities or physical prefab source files.

### Shared inspection/extraction requirement

A model containing many outputs must not cause the source to be reparsed with Assimp independently for every requested output.

The implementation must share canonical source inspection/extraction across output request composition, keyed at minimum by exact source fingerprint plus relevant import-policy version/settings.

The initial implementation may use a bounded process-local cache; a later design may persist a canonical model-import intermediate in DDC. The required invariant is that N model outputs do not imply N complete Assimp imports.

## Editor Asset Browser presentation

The source remains one normal model source row. The normal Asset Browser should remain a project/source browser rather than becoming a hierarchical editor for the contents of compound assets.

### Browser interaction model

Do **not** use Unity-style disclosure/foldout arrows on normal model cards as the primary way to expose imported children. Expanding model contents inline would make card/list geometry unstable, mix source browsing with asset editing, and scale poorly for models with many materials, textures, skeleton data, or future animations.

Do **not** make a model masquerade as a real Asset Browser folder either. Double-clicking a real folder already means navigating the project's source-folder namespace. Treating a model as a virtual folder would create a second incompatible navigation namespace and make folder semantics such as breadcrumbs, search, recursive traversal, drag/drop destinations, source location, and persistence ambiguous.

The preferred interaction is:

```text
single click asset   -> select it in the Asset Browser and show its Properties
double click folder  -> navigate into that real project folder
double click asset   -> open that asset in an Asset Editor/Workspace panel
```

The model card/list row may show a subtle compound-asset indicator, output count, or compact type counts, but it should not expand in place. For example:

```text
robot.glb        Model · 8 outputs
```

The exact visual language can evolve, but it should communicate that deeper contents exist without turning the browser card itself into the content hierarchy.

### Logical-output discovery, typed assignment, and search

Keeping the Asset Browser source-oriented does **not** make model-owned outputs discoverable only after opening the model Asset Editor. Addressable logical outputs are ordinary typed resources and must participate in Editor-wide resource discovery independently of their source presentation.

Dropping or selecting a compound source such as `car.glb` into a typed resource-reference field resolves against the source's declared logical outputs that are compatible with the requested type:

```text
drop car.glb into MeshResourceReference

0 compatible MeshResource outputs -> reject as incompatible
1 compatible MeshResource output  -> resolve that output automatically
2+ compatible MeshResource outputs -> open a chooser filtered to those outputs

drop exact child car.glb / Wheel   -> resolve directly; no chooser
```

The same rule applies generically to `MaterialResourceReference`, `TextureResourceReference`, and future typed resource references. Resolution is by compatible declared output identity/type, never declaration order or an importer-defined "primary" child.

Typed resource pickers and Editor-wide/global resource search must likewise index addressable logical outputs directly. A search for `Body`, for example, may return:

```text
Body    Material in robot.glb
```

Selecting that result yields the exact logical `MaterialResourceReference`; opening/navigating to it should open the owning model Asset Editor and focus `Materials / Body` where practical. `Find References` operates on that same exact resource identity.

This does not turn logical outputs into filesystem children. Ordinary Asset Browser folder/list browsing, including its source-oriented file filtering, continues to present authored project sources such as `robot.glb`. If a future unified search UI mixes source results and resource results, logical outputs must remain visibly identified as resources owned by a parent source rather than masquerading as files or folders.

### Selection and Properties panel

Vanguard does **not** currently have the Properties panel described in this section. This is a desired Editor interaction contract, not an exact-current capability.

Do **not** implement a new generic Properties panel as part of the model-import work. Model import should expose the backend/importer description data and selection semantics needed for such a panel later, but the panel itself is a separate Editor feature and must be designed/implemented independently when explicitly authorized.

Opening the Asset Editor must **not** be required just to inspect ordinary information about a model. Selecting the model in the Asset Browser should drive the normal Properties panel with a useful lightweight summary of the selected compound asset.

For a model source, that Properties view should be able to show information such as:

- source name/path and detected model format;
- asset/import status and current build diagnostics;
- high-level content counts such as meshes/submeshes, materials, textures, skeleton/joints, and animations where known;
- declared logical output count and compact output-type summary;
- current importer/settings version and whether the source is stale or requires refresh/reimport;
- important warnings or missing linked dependencies;
- actions such as `Open Asset Editor`, `Edit Import Settings`, and `Refresh/Reimport from Source` where valid.

The Properties panel is therefore the fast inspection surface, while the Asset Editor/Workspace is the deeper navigation/editing surface. The Properties panel should not reproduce the entire internal model hierarchy or become a miniature Asset Editor.

The same principle should apply to logical children selected inside an Asset Editor: selection should feed the normal Properties panel with the selected child's typed resource information, provenance, diagnostics, and applicable actions. This keeps selection behavior consistent across the Editor instead of requiring each compound-asset workspace to invent a separate inspector.

### Generic Asset Editor / Workspace

Opening a compound asset should enter a dedicated asset-focused panel/workspace rather than changing the Asset Browser's current folder. This should be a generic editor concept, not a panel specifically named around "embedded" model content. The same shell/lifecycle can later host model, prefab, material, and other rich asset editors while allowing each asset type to contribute its own layout and tools.

Conceptually:

```text
Asset Browser = project/source navigation
Asset Editor  = navigation and editing inside one asset
```

For a model, the Asset Editor should expose the source-authored hierarchy separately from the reusable Vanguard resources generated from that source. These major peer views should be explicit top-level tabs across the model Asset Editor rather than headers in one giant vertically scrolling inspector:

```text
robot.glb

[ Overview ] [ Hierarchy ] [ Meshes ] [ Materials ] [ Textures ] [ Skeleton ] [ Animations ]
```

The tabs are navigation within one compound asset, not separate source assets. `Overview` is the default summary/whole-model-preview surface; the other tabs provide focused views of that model's hierarchy or generated resource collections. Normal headings/panels may still be used **inside** a tab when that view needs further subdivision.

This separation is deliberate:

```text
Hierarchy = where/how source nodes are placed and related
Meshes    = reusable generated MeshResource outputs
Submeshes = render/material primitives inside one MeshResource
```

The hierarchy view should present the imported composition that feeds the model-owned generated prefab output. For example:

```text
Car
 |- Body
 |    `- Mesh -> Body
 |- FrontLeftWheelPivot
 |    `- FrontLeftWheel
 |         `- Mesh -> Wheel
 |- FrontRightWheelPivot
 |    `- FrontRightWheel
 |         `- Mesh -> Wheel
 |- RearLeftWheelPivot
 |    `- RearLeftWheel
 |         `- Mesh -> Wheel
 |- RearRightWheelPivot
 |    `- RearRightWheel
 |         `- Mesh -> Wheel
 |- LeftDoorHinge
 |    `- LeftDoor
 |         `- Mesh -> Door
 `- RightDoorHinge
      `- RightDoor
           `- Mesh -> Door
```

Selecting a hierarchy item should distinguish transform-only nodes from mesh instances and expose its imported local transform, referenced geometry, source-node identity, provenance, and applicable diagnostics/actions. The hierarchy is source-owned/read-only apart from changes made through import settings and reimport reconciliation.

The Meshes view should list unique generated geometry resources rather than repeating every hierarchy instance. For the same model it may look like:

```text
Meshes
  Body     2 submeshes   used by 1 hierarchy node
  Wheel    2 submeshes   used by 4 hierarchy nodes
  Door     1 submesh     used by 2 hierarchy nodes
```

Opening `Wheel` should expose the actual `MeshResource` plus its submeshes and material bindings, for example:

```text
Wheel                                      MeshResource

Used by:
  FrontLeftWheel
  FrontRightWheel
  RearLeftWheel
  RearRightWheel

Submeshes:
  Tire    -> Rubber
  Rim     -> Metal
```

This UI should make resource reuse obvious: four wheel placements may reference one `Wheel` mesh output rather than producing four VMESH resources.

### Large-model collection scaling

The model Asset Editor must remain usable when a source contains hundreds or thousands of internal records. A model with 250 materials, 800 texture references, or a very large hierarchy must not be represented as one eagerly expanded ImGui tree or as hundreds of simultaneously live preview widgets.

The `Meshes`, `Materials`, and `Textures` tabs should therefore behave as searchable/filterable collections. At minimum they need text filtering/search, useful state filters where relevant (for example warnings, missing, embedded, linked, or referenced), and list/table presentation that can scale without constructing expensive UI/resource state for every row every frame.

Conceptually:

```text
Materials (250)

[ Search materials... ] [ Warnings only ]

Body
Glass
Rubber
Chrome
...
```

and:

```text
Textures (800)

[ Search textures... ] [ Missing ] [ Embedded ] [ Linked ]

Body_BaseColor
Body_Normal
Glass_Normal
...
```

The frontend should use row virtualization/clipping, paging, or the equivalent bounded mechanism appropriate to the exact-current Editor UI so work is proportional to the visible/active subset rather than total logical child count. The architectural requirement is bounded work; the exact widget implementation may follow the UI framework available when this is built.

Expensive child details are lazy. Friendly metadata needed to populate/filter rows may exist for the full collection, but texture/material thumbnails, focused previews, dependency expansion, detailed diagnostics, or other GPU/CPU-heavy child state should be requested only for the selected item and a small bounded visible/recent set. Opening a tab containing 800 texture references must not create 800 live texture previews merely because the records exist.

The same scaling rule applies to the `Hierarchy` tab: very large source hierarchies must use bounded tree/list traversal and rendering rather than assuming every node is expanded and materialized as expensive UI state simultaneously.

Internal navigation may intentionally feel browser-like without pretending those sections are filesystem folders. A lightweight breadcrumb such as:

```text
robot.glb / Materials / Body
```

is appropriate inside the Asset Editor because it expresses navigation within the selected asset rather than project path navigation.

The default Overview should summarize the model without duplicating the full hierarchy or resource inspectors. For example:

```text
car.glb                                      Model

[ interactive model preview ]

Hierarchy
  12 nodes
  4 transform-only nodes
  7 mesh instances

Generated resources
  3 meshes
  5 materials
  8 textures

Meshes
  Body     2 submeshes
  Wheel    2 submeshes · 4 instances
  Door     1 submesh   · 2 instances

Import
  Preserve Authored Hierarchy
  Up to date
  No missing dependencies

[ Edit Import Settings ] [ Refresh/Reimport from Source ]
```

An **interactive rendered model preview is a required part of the intended model Asset Editor contract**, even if the first implementation lands before the viewport itself. Double-clicking a model should ultimately provide a real orbitable 3D preview of the currently imported composition using Vanguard's normal rendering/runtime resource path. This is not optional decorative polish: it is the primary visual verification surface for scale, orientation, hierarchy, materials, textures, normals/tangents, winding, and other import results. The surrounding Asset Editor architecture must therefore leave an explicit preview region/lifecycle rather than assuming a table-only inspector and trying to retrofit rendering later.

Later refinements may allow the preview to isolate a selected hierarchy node, `MeshResource`, material, or other child, but those focused modes are secondary to the required whole-model preview.

Linked existing project assets should be presented as linked dependencies rather than pretending to be model-owned outputs.

The model-owned generated `PrefabResourceType` composition is primarily an implementation/runtime output and should not be promoted as a normal user-facing child beside meshes, materials, or textures. The Asset Editor should represent it through the Hierarchy view. Its raw resource identity may still be exposed in advanced build/diagnostic tooling when useful.

The preferred longer-term shell supports multiple asset-editor instances/tabs so several assets may remain open independently. Exact panel creation, persistence, docking, and multi-instance behavior must still be re-verified against the current Editor panel system when this is implemented; the design should use existing panel machinery where it already satisfies these requirements rather than introducing a parallel window framework.

Imported child presentation should use explicit labels/badges, not color alone. Relevant states include:

- Imported;
- Embedded;
- Linked;
- Read-only;
- Missing;
- Stale/Removed;
- Ambiguous on reimport.

The current Asset Browser/backend foundation already has source rows and can enumerate an asset's declared outputs. Friendly imported-output names, provenance, categories, and diagnostics used by the Asset Editor should come from an importer/backend description API rather than teaching Editor UI code to decode model-specific settings.

An imported child is a real logical resource, not a fabricated filesystem entry. Selecting `Body` under the model's Materials group should select the actual typed material `ResourceReference` for that output. It should therefore participate normally in preview, reference browsing, compatible drag/drop and selection, typed resource pickers, Editor-wide resource search, build diagnostics, and any other operation that consumes a `MaterialResourceReference`.

What remains unavailable are operations that imply independent source ownership. A model-owned imported material cannot be independently source-edited, moved, renamed, or deleted because there is no standalone `.material` source or child `.vmeta` to mutate. Appropriate actions instead include `Open Parent Model`, `Edit Import Settings`, and `Extract Material...`. `Open Parent Model` should focus/open the parent model's Asset Editor at the relevant child where practical. Extraction is the operation that creates a new independently authored material asset.

Selecting an imported child may expose:

- source owner;
- current source material/texture name;
- imported/embedded/linked provenance;
- material mapper warnings;
- current slot/reference usage;
- Edit Import Settings;
- Extract...;
- Open Parent Model.

## Model import window

The first-version UI may open a dedicated single-model import window before commit. That is a frontend choice for the initial workflow, not an ownership boundary in the import architecture.

The import backend must remain per-source/headless and independently orchestratable: inspection, candidate settings, `ModelImportPlan`, stale-plan validation, durable commit, build observation, and result reporting must not require the lifetime of one modal window or encode global singleton "current import" state. The Editor window consumes those operations; it does not own them.

Batch import and shared presets do **not** block the first implementation. However, the architecture must permit a later frontend to orchestrate many sources through the same per-source operations without spawning one mandatory modal workflow per file. A future batch surface may inspect several sources independently, apply a shared preset/default policy where valid, surface per-source exceptions, commit accepted plans, bound concurrent builds, and aggregate progress/results.

Conceptually:

```text
20 model sources
    -> inspect independently
    -> apply shared/default policy where appropriate
    -> surface per-source problems/exceptions
    -> commit accepted plans
    -> build with bounded concurrency
    -> aggregate Ready / Build Failed results
```

No concrete batch UI or preset system is required for v1. The requirement is only that the single-model window be one frontend over reusable operations rather than an API shape such as `OpenModelImportModalAndDoEverything(...)` that would make later batch orchestration structurally difficult.

### Pre-import rendered preview

The intended import UX includes an interactive 3D preview before commit. It may be delivered after the first import-window implementation, but the backend/frontend contract must support it from the start. The preview answers a different question from the post-import Asset Editor preview:

```text
pre-import preview  = what will these current settings produce if I import?
Asset Editor preview = what does the currently imported asset produce now?
```

Pre-import rendering must **not** require first committing a fake asset, hidden prefab, temporary `.vmeta`, or other durable project state. Instead, the Editor should request a transient **preview build** from the same inspected `ModelImportManifest`, candidate `ModelImportSettings`, and import-plan/cook machinery used by the real import. That preview build produces an ephemeral resource closure sufficient to render the prospective model, conceptually:

```text
physical source + inspected manifest + candidate settings
    -> transient preview plan
    -> temporary cooked MeshResource / MaterialResource / TextureResource outputs
    -> temporary generated PrefabResource composition
    -> normal Vanguard runtime/resource/rendering path
    -> preview viewport
```

The temporary closure is Editor-owned and disposable. It must not register durable `AssetOutput`s in the project `SourceDatabase`, mutate `Assets`, consume final persistent output identity, or become package-visible merely because it was previewed. A preview-scoped identity/catalog may be used internally so transient mesh/material/texture dependencies resolve exactly as they would after import.

Exact-current VMESH runtime streaming is backed by `MeshPageSource`, which currently opens loose-file or package-backed `ResourceSource`s rather than arbitrary in-memory mesh pages. Meanwhile the mesh cooker already produces VMESH bytes through `MemoryFileWriter`, and `PrefabResource` can be opened from bytes. Therefore the narrowest exact-current implementation shape is likely an Editor preview-artifact/cache area: cook candidate resources without committing them, materialize the transient VMESH/dependency closure into temporary loose artifacts as needed by the normal loaders, render it, and retire/delete those preview artifacts when superseded or when the window closes. A future memory-backed `ResourceSource` could remove that physical temporary-file detail without changing the UX or importer contract.

Changing a visual import setting should request a new preview generation. The old ready preview should remain visible until the replacement is ready, and stale/cancelled preview generations must never publish over a newer setting revision. Repeated edits should be coalesced/cancelled rather than producing an unbounded queue of cooks.

The preview should visualize the **prospective committed result**, not silently use source data that the current policy would reject. For example, a texture reference that cannot become a valid Vanguard dependency under the selected policy should appear missing/fallback in the rendered result with a visible diagnostic rather than being loaded only for preview and making the import look healthier than it will be.

### Source

- source path/name;
- detected format;
- source fingerprint/status;
- detected content counts;
- global warnings and missing dependencies.

### Mesh

- uniform scale;
- source up axis / coordinate policy;
- hierarchy import policy;
- generate normals;
- generate tangents;
- UV flipping;
- winding;
- smoothing behavior;
- cook profile;
- LOD-related settings where supported.

Hierarchy handling should be an explicit import setting rather than a hard-coded importer behavior. The safe default is **Preserve Authored Hierarchy**, retaining transform-only nodes that may represent pivots, sockets, mounts, animation targets, or other semantic structure. An optional **Optimize Redundant Transform Nodes** mode may collapse transform-only nodes only where the importer can prove that doing so preserves the model's observable hierarchy semantics; the import preview should make any such collapses visible before commit. This setting is separate from mesh-combination policy: collapsing a redundant transform node is not the same operation as merging independent mesh parts.

### Materials

One row per discovered source material, including:

- source/display name;
- slot/submesh usage;
- mapping warnings;
- source-owned imported output policy;
- Default/fallback policy when valid;
- current output identity when reimporting.

The model import window does not assign user-authored project materials to mesh slots. Such replacements are composition-level `StaticMeshComponent`/prefab/entity overrides, not model-import settings.

### Textures

One row per discovered texture reference, including:

- semantic;
- embedded versus external-to-container provenance;
- resolved/missing state;
- intended color-space/usage policy;
- current managed project target, if any;
- Keep Imported/Embedded;
- Reuse Existing;
- Adopt In Place;
- Ignore only where semantically supported.

For external-to-container images already under `Assets` but not yet managed by Vanguard, the window should also provide a bulk action such as `Adopt All Unmanaged Project Images`. This applies `Adopt In Place` to every compatible unresolved image row while preserving per-row overrides before commit; it must not open one confirmation dialog per texture.

Missing rows must expose direct recovery actions. `Locate in Assets...` binds the row to the correct image source already under the project asset tree. `Reuse Existing Texture...` binds it to an existing managed `TextureResource`. The UI must never offer a choice that would leave the committed model depending on an absolute/outside-`Assets` path.

### Skeleton

Show information already discoverable from current model inspection, such as joints, even before the runtime skeleton asset system exists. Unsupported creation options remain disabled/clearly unavailable rather than being hidden from the architecture.

### Animations

Reserve the section in the manifest/settings/plan architecture. Do not present fake detected animation data until the scan layer actually exposes it.

### Summary

Before commit, show the exact planned effects:

- source files to copy/create;
- `.vmeta` files to create/update;
- existing assets to reuse;
- model-owned outputs to declare;
- dependencies to link;
- missing resources;
- warnings/lossy mappings;
- reimport additions/removals/ambiguities;
- slot-identity changes that could affect external prefab/entity overrides.

The summary is the transaction preview, not merely an asset count.

## Headless/frontend separation

The model import window is a frontend only.

The engine/tool layer owns:

- inspection;
- manifest types;
- serializable settings;
- planning;
- validation;
- material translation;
- source-element matching;
- output-key allocation/reconciliation;
- transaction composition;
- commit;
- build scheduling/progress/result state;
- coherent-generation publication / last-known-good retention;
- extraction;
- reimport diff/reconciliation.

The Editor owns presentation, user interaction, file dialogs, selection, and command surfaces only.

This frontend separation also applies to orchestration cardinality. A single-model window, a future multi-model batch queue, a CLI loop, or automation should all be able to invoke the same per-source inspection/plan/commit/build contracts without changing importer semantics.

A CLI must be able to perform the same conceptual flow:

```text
inspect model
load or modify ModelImportSettings
build ModelImportPlan
print diagnostics/summary
validate plan is still current
commit durable source/metadata plan
request/observe generated output build
report Ready or Build Failed
```

No import policy or source-format business logic should be embedded in `editor_asset_browser.cpp` or any other Editor-only implementation.

## Expected default user experience

For an ordinary model source such as FBX, OBJ, or glTF, with imported material data and a mixture of embedded or external-to-container textures under `Assets`, the intended default workflow is:

1. The user chooses a supported model source through the OS or drops it into a writable Asset Browser destination. The source may originate outside the project; Vanguard records the chosen `Assets` destination and does not mutate anything yet.
2. Vanguard scans it and opens the model import window. A `.gltf` that requires an external `.bin` buffer is rejected for the first version; self-contained `.gltf` and `.glb` remain valid.
3. The window shows mesh structure, source materials, embedded and external-to-container textures, joints, dependencies, and warnings.
4. Model-owned imported material/texture outputs are created where policy requires them; external-to-container project sources may instead resolve to independently managed project assets.
5. Existing managed project textures are reused when explicitly resolved as such.
6. Unmanaged image files already under `Assets` can be adopted in bulk with per-row exceptions. Missing image references can be resolved with `Locate in Assets...` or `Reuse Existing Texture...`.
7. Before commit, Vanguard verifies that the source still matches the inspection/plan fingerprint. If it changed, the import stops before mutation and asks the user to rescan rather than failing later with an opaque transaction/build error.
8. Pressing Import copies an outside-origin model into its chosen `Assets` destination when necessary, then commits the model `.vmeta` and any explicitly selected extra source operations as one durable transaction. No committed dependency points outside `Assets`.
9. After durable commit, the model enters `Imported / Compiling` while its required generated resource closure builds. Durable import success is not presented as equivalent to `Ready`.
10. If compilation succeeds, Vanguard atomically publishes the coherent model generation as `Ready`. If any required output fails, the asset remains imported and retryable, successful artifacts may remain cached, but the incomplete candidate generation is not partially published. On reimport, the previous ready generation remains active; on first import, the model remains `Imported / Build Failed` until a complete generation succeeds.
11. The Asset Browser shows the model as one normal compound asset row/card; double-clicking it opens the model Asset Editor/Workspace, where imported children, build diagnostics, and linked dependencies are browsed. No cooked files are written into `Assets`.
12. Imported materials/textures are visibly and structurally read-only because the model source owns them.
13. The imported mesh keeps its canonical material slots; a prefab/entity component chooses `Use Mesh Material` or overrides a slot with another Vanguard material without mutating the model asset.
14. Reimport rescans and previews a source diff, preserving stable output keys and material-slot identities whenever correspondence is unambiguous so composition-owned overrides remain valid.
15. Ambiguous matches and destructive removals are surfaced to the user rather than guessed.

## Exact-current implementation gaps relevant to this design

The architecture above maps cleanly onto current Vanguard foundations, but several concrete integration pieces do not exist yet:

- the Editor project pipeline currently registers the texture compiler/importer but not the mesh/model importer;
- the current mesh importer descriptor does not provide `createImportDescription` or `validateSettings` for generic project import;
- the current generic `createImportDescription` callback cannot inspect source bytes and therefore is insufficient for a model inspection/review workflow by itself;
- the current Assimp-backed mesh import path flattens source node transforms into vertex data and collapses encountered source meshes directly into one `ImportedMesh::submeshes` list, so it cannot yet preserve reusable geometry instances plus authored hierarchy for generated prefab composition;
- the low-level mesh importer parses materials/textures but cooked submeshes currently bind only `MeshImportSettings.defaultMaterial`;
- the exact-current mesh compiler discovers `defaultMaterial` as a generated dependency rather than a set of canonical per-slot imported material output references;
- the exact-current mesh compiler descriptor does not provide a resource estimator, while `BuildGraph` rejects graph operations whose prepared compiler plan has no resource estimate; simply registering the current mesh compiler in the project pipeline is therefore not sufficient to make project mesh builds work;
- no conversion from imported source materials into Vanguard material outputs exists yet;
- no project-level generation of model-owned embedded texture outputs exists yet;
- `prefabs::CookPrefab(...)` exists, but there is no project asset compiler/adapter for producing `PrefabResourceType` through the project build graph;
- prefab `ComponentBuildRecord` / `ComponentRecord` currently persist only component identity and owning entity identity; there is no authored same-entity parent-component relationship, and the materializer currently attaches every placed object component directly to the entity transform root;
- the current asset operation that updates settings intentionally preserves the existing output schema; there is no atomic operation that updates model settings and declared outputs together while preserving the parent `AssetId`;
- `AssetFileTransaction` can conditionally write/delete by expected digest, but it has no read-only digest assertion/precondition for proving at commit time that an inspected model source remained unchanged when the transaction only needs to mutate metadata/other files;
- existing high-level asset executors each own their own transaction boundary, so model-source creation/update plus `.vmeta` schema changes plus selected extra source adoptions cannot yet be composed under one durable commit;
- `DependencyIndex` replacement builds are transactional, but removal of a retired output does not currently participate in that transaction; destructive model reimport therefore needs transactional retirement/removal support rather than publishing replacement records and retiring old records in separate observable steps;
- project publication already has prepared batch visibility, but current automatic grouping is driven by generated-dependency connectivity rather than an explicit model-generation/cohort contract; model reimport must prove that the required model closure cuts over coherently and retains the previous published generation on candidate failure;
- exact-current model reimport is recook of the existing persisted output schema rather than source-output rediscovery/reconciliation;
- raw embedded Assimp texture pixels need a canonical texture compiler input path;
- normal editable persisted `.material` authoring/import integration is not yet implemented.

These are implementation gaps, not reasons by themselves to change the ownership/workflow model. They also are not an exhaustive implementation checklist. Before changing any of these areas, re-read the exact-current source, tests, and relevant package/build/runtime contracts and try to disprove the assumptions above. Prefer existing machinery when it already satisfies the invariant; change the design if exact-current evidence exposes a real contradiction.

## Design decision summary

The model source is the authored owner. Its `.vmeta` is the durable identity/settings/output authority. Imported materials, embedded textures, reusable mesh resources, and the generated prefab composition may all be real model-owned `AssetOutput`s without requiring one physical source file per output. External-to-container sources are still inside `Assets`; independently managed project assets remain independently owned and are linked rather than absorbed. Anything outside `Assets` is outside Vanguard's managed asset model. Import inspection preserves the semantic separation between hierarchy/node placement, reusable geometry definitions, and render/material primitives; VMESH submeshes are not used as a substitute for independently placeable hierarchy parts. The mesh stores canonical source-derived material slots only; consuming prefab/entity composition, such as `StaticMeshComponent`, chooses `Use Mesh Material` or supplies an override. The Asset Browser remains project/source navigation rather than expanding compound assets inline or treating them as virtual folders; double-clicking a model opens a generic Asset Editor/Workspace where its hierarchy and generated resources are browsed separately. Reimport preserves stable output keys and material-slot identities but never owns or rewrites composition-level overrides. Output-schema changes are reconciled explicitly rather than inferred from transient array positions. All import policy and mutation logic remains engine/tool-level and reusable by any frontend.

## Execution plan — exact-current source priority

This order is derived from the exact-current implementation rather than from UI visibility. The highest-priority work is the earliest place where the current code destroys information or cannot express the required durable operation. **Do not start by building the model Asset Editor or import-window chrome.** Those frontends would otherwise be forced to invent temporary ownership, identity, hierarchy, and build behavior that the backend cannot yet represent.

The existing systems that should be reused rather than replaced are substantial:

- `AssetMetadata` / `SourceDatabase` already support one `AssetId` with many typed `AssetOutput`s and exact logical-output lookup;
- a custom `SourceImporterDescriptor::createBuildRequest` can already compose a different exact `BuildRequest` for each declared output;
- `BuildSystem`, `BuildGraph`, VDDC and `DependencyIndex` already own compiler dispatch, generated-dependency recursion, fingerprints, caching and transactional replacement recooks;
- `SourceDatabase` already indexes authored backward references and the asset-operation layer already has retarget/delete reference-safety machinery;
- `AssetFileTransaction` already owns durable multi-file staging/journaling/recovery;
- `ProjectAssetPipeline` already owns project authoring history, automatic recook, per-output build/publication status, cancellation/retry and prepared loose-resource batch visibility;
- existing material and texture artifact compilers already consume canonical generated inputs; model import should synthesize those inputs rather than invent provenance-specific runtime resources;
- runtime prefab loading, placed-component transforms and `HardAttachment` already exist; the missing piece is authored component-parent persistence/materialization, not a second transform hierarchy system.

The execution sequence below is therefore intentionally backend-first.

### P0 — Canonical model inspection and extraction — highest priority

**Why first:** `mesh_import.cpp::AppendNodeGeometry` currently accumulates parent/node transforms into geometry and emits every encountered node mesh into one flat `ImportedMesh::submeshes` list. `ImportedMesh::BuildSourceView()` then collapses that list into one `SourceMesh`. Once this happens, reusable source geometry versus placement instances cannot be reconstructed reliably. Every later requirement—stable output identity, generated hierarchy, shared wheel meshes, reimport matching, resource lists and preview—depends on fixing this boundary first.

Implement a headless, format-neutral canonical inspection representation matching the design's `ModelImportManifest` contract. It must retain, with explicit bounds and diagnostics:

- source nodes, parent relationships and **local** transforms;
- reusable geometry definitions independent from node instances;
- primitives/material partitions inside each geometry definition;
- node -> geometry-definition instance relationships;
- imported source materials with stable provenance/matching evidence;
- embedded texture records and external-to-container texture references with provenance;
- joints/skeleton evidence needed by current import;
- source fingerprint and importer diagnostics.

Assimp/glTF/FBX decoding belongs below this boundary. The canonical manifest must not expose Assimp object identities to the rest of the asset/editor architecture.

Refactor geometry extraction so one selected manifest geometry definition can produce the `SourceMesh`/submesh data consumed by the existing mesh cooker **without baking placement-node transforms into the reusable geometry definition**. Preserve a compatibility path for existing low-level mesh-import tests/callers while migrating model import to the canonical representation.

The first fixtures should prove at minimum:

1. several source nodes referencing one source mesh produce one geometry definition plus several placement instances;
2. transform-only/pivot nodes survive inspection;
3. several primitives of one geometry definition remain submeshes/material partitions rather than separate placeable mesh resources;
4. material and embedded/external texture provenance survives inspection;
5. source fingerprinting is deterministic;
6. self-contained `.gltf` / `.glb` are accepted and v1 `.gltf` requiring an external `.bin` is rejected explicitly.

**Exit gate:** a headless test can inspect representative model bytes and reconstruct the intended hierarchy/geometry/material/texture manifest without touching `Assets`, `.vmeta`, the Editor, or the runtime.

#### P0 implementation-readiness study — exact-current source findings

This study was performed read-only against the exact-current Vanguard tree before implementation. No model-import source was edited and no build, test, profile or native run was performed. Its purpose is to make P0 concrete enough that implementation can begin without re-deriving the architecture after a conversation/context reset.

##### Current information-loss boundary

`source/meshTools/src/mesh_import.cpp::AppendNodeGeometry(...)` is the critical loss point.

Today it:

- recursively walks `aiNode`;
- accumulates `parentTransform * node.mTransformation`;
- bakes that accumulated transform into positions, normals and tangents;
- folds determinant sign into winding;
- emits one flattened `ImportedSubmesh` for every encountered `node.mMeshes[]` entry;
- records a display-oriented `sourceNodePath`;
- replaces the source material-table identity with a name hash plus `MeshImportSettings.defaultMaterial`;
- then `ImportedMesh::BuildSourceView()` exposes the whole flattened list as one `SourceMesh`.

This destroys reusable geometry-definition versus placement-instance structure before any model workflow can use it. Repeated references to one source geometry become duplicated transformed payloads.

The existing node-path/stable-ID mechanism is not suitable as canonical identity:

- Assimp node names may be empty or duplicated;
- `/` is legal in names, so slash-separated paths are ambiguous;
- duplicate named siblings may produce identical paths;
- repeated references to the same `aiMesh` are not distinguished by occurrence ordinal;
- current stable-ID construction can therefore collide;
- VMESH validation requires non-zero unique submesh stable IDs.

P0 canonical identity must therefore use explicit structural indices/ordinals. Names and paths are display/provenance only.

##### Assimp `aiMesh` is a primitive/material partition, not always a reusable source mesh

One generic `aiMesh` must not blindly become one Vanguard `MeshResource`.

For glTF 2:

- Assimp creates one `aiMesh` per glTF primitive;
- `glTF2Importer::meshOffsets` records the exact source glTF mesh -> contiguous primitive-level `aiMesh` range;
- nodes referencing one glTF mesh receive the entire range.

Therefore the intended mapping is:

```text
glTF mesh       -> ModelImportManifest::GeometryDefinition
glTF primitive  -> GeometryDefinition::Primitive
glTF node       -> GeometryInstance
```

For FBX:

- one source `MeshGeometry` may be split into several `aiMesh` records by material;
- `FBXConverter::meshes_converted` retains the exact source Geometry -> ordered split-`aiMesh` relationship;
- later FBX model nodes may reuse those converted indices.

Both exact grouping tables are decoder-internal and are gone by the time Vanguard receives the final public `aiScene`.

Therefore P0 needs a **narrow decoder provenance bridge/sidecar** below the format-neutral boundary. It must export only generic structural facts such as:

```text
sourceGeometryKey
ordered primitive/partition ordinal
final decoded aiMesh index
source node/instance relation
source material index
format-neutral binding provenance
```

The rest of Vanguard must not depend on glTF/FBX object types or Assimp object addresses.

Do **not** reconstruct source geometry grouping later from:

- `aiMesh::mName`;
- adjacency/contiguous indices alone;
- material names;
- node names;
- declaration order.

Those are not reliable source identity.

Assimp post-processing is also part of this contract. Exact-current `SortByPTypeProcess` may drop, split and renumber `aiMesh` records and rewrite node mesh references. The first P0 implementation should either:

1. disable topology-changing `aiProcess_SortByPType` for canonical model inspection and explicitly validate supported topology; or
2. carry/remap decoder provenance through every topology-changing post-process.

The first option is the smaller and safer initial cut.

For formats where no richer native grouping is available, a singleton `aiMesh` geometry definition is acceptable only when that correspondence is explicitly the strongest decoder evidence available. P0 must never heuristically merge ambiguous partitions.

##### Canonical manifest contract

P0 should introduce an owned, format-neutral representation approximately shaped as:

```text
ModelImportManifest
  sourceFingerprint
  decoderProvenance
  coordinateProvenance
  nodes[]
  geometryDefinitions[]
  geometryInstances[]
  materials[]
  embeddedTextures[]
  externalTextureReferences[]
  dependencies[]
  joint / skin evidence
  diagnostics[]
```

The exact public names may change, but the semantic ownership should not.

Each node should retain at least:

- inspection-local node index;
- parent node index;
- sibling ordinal / child ordering evidence;
- raw/source name and display name;
- exact full local 4x4 matrix in canonical inspection space;
- provenance flags for decoder-generated/synthetic nodes where known;
- references to geometry-instance records.

Node indices are inspection-local structural identity, not persistent output identity. Persistent correspondence is P1.

Each geometry definition should retain at least:

- inspection-local definition index;
- decoder-origin grouping key/provenance;
- source/display name;
- ordered primitive list;
- untransformed geometry in definition-local space;
- skin/bone binding evidence that belongs to the definition/primitive rather than a placement node.

Each primitive should retain at least:

- definition-local primitive ordinal;
- decoded/source partition index;
- explicit source material table index;
- vertex streams and indices in geometry-local space;
- topology/flags;
- per-primitive bone bindings and source influence evidence;
- source provenance needed for diagnostics/reconciliation evidence.

Each geometry instance should retain at least:

- node index;
- geometry-definition index;
- node-local occurrence ordinal when needed;
- per-instance binding provenance where the source format supports it;
- no duplicated geometry payload.

Each material record should retain:

- explicit source material-table index;
- source/display name;
- translated source properties;
- texture bindings to canonical texture-reference records;
- unsupported/lossy translation diagnostics.

Material names are display data, not identity.

Each texture/reference record should preserve:

- embedded versus external provenance;
- embedded source index;
- embedded encoded/raw payload kind and bytes;
- authored/source URI or path;
- resolved source-relative path when inspection can resolve it;
- semantic and UV set;
- missing/unresolved state;
- source relation/provenance.

Inspection provenance may contain an outside-origin path. P0 must not convert that into a durable project reference; durable managed resolution remains P1/P2.

##### Dependency categories and glTF external-buffer policy

Current `TrackingIOSystem` records every file Assimp opens into one generic dependency list, while material scanning separately discovers external texture paths. That conflates source categories.

P0 should classify dependencies at least as:

- main source;
- structural decoder companion;
- external texture/image;
- other decoder-specific auxiliary, if one is encountered.

This classification is required for the settled glTF v1 policy:

- `.glb` is allowed;
- self-contained `.gltf` using embedded/data-URI buffers is allowed;
- `.gltf` requiring an external `.bin` structural buffer is rejected explicitly;
- external image URIs are not mistaken for forbidden structural buffers.

The vendored glTF loader already distinguishes these paths: data-URI buffers decode in memory, whereas a non-data buffer URI causes decoder IO. External image URIs remain separate image references.

##### Exact source fingerprint witness

Exact-current `ImportMeshFile(...)` lets Assimp parse the source and then separately reopens the file in `HashFile(...)`. The resulting digest does not necessarily witness the exact main-source bytes that were parsed if the file changes between those operations.

`InspectModelFile(...)` should instead:

1. read a bounded snapshot of the main source once;
2. hash exactly that snapshot;
3. decode exactly that snapshot through `ReadFileFromMemory(...)`;
4. keep the tracked source-directory IO handler installed for permitted auxiliary opens.

Assimp's memory-import path already delegates non-main-file IO to the previously installed IO system, so this does not require giving up dependency tracking.

The inspection fingerprint therefore becomes an exact witness usable by later stale-plan checks.

##### Transform and coordinate-space contract

P0 must not replace the current accumulated transform bake with a lossy conversion into `math::WorldTransform`.

Exact-current `WorldTransform` and placed-component hierarchy represent translation + quaternion rotation only. They cannot generally represent:

- non-uniform parent scale;
- negative scale/reflection;
- shear;
- singular/non-affine transforms.

`StaticMeshComponentData::scaleX/Y/Z` is visual mesh scale, not a general scale-bearing hierarchy transform.

Therefore the canonical manifest must preserve the **full local 4x4 source/decoder matrix** for every node. P0 may additionally compute diagnostic/representability facts, but it must not discard the matrix to fit current runtime representation.

Useful diagnostics include:

- non-finite matrix;
- non-affine matrix;
- singular transform;
- negative determinant;
- non-uniform scale;
- shear / non-TRS decomposition.

Runtime/component representation policy belongs to later hierarchy/composition work. P0 preserves evidence.

Coordinate conversion must also be staged explicitly.

FBX exact-current behavior already:

- preserves complex pivot/offset chains through synthetic `_$AssimpFbx$` nodes by default;
- writes FBX axis/unit metadata;
- applies FBX axis/unit correction to the decoded root transform when `ignoreUpDirection == false`.

Therefore P0 must distinguish:

```text
source-format coordinates
-> decoder-applied normalization
-> canonical inspection space
-> later Vanguard import-space/user policy
```

Do not blindly reapply current `MeshImportSettings.sourceUpAxis` or `uniformScale` during canonical inspection.

If a later explicit basis conversion matrix is `C`, then the conceptual conversion is:

```text
geometry point:       p' = C * p
local node transform: M' = C * M * C^-1
```

Synthetic FBX pivot nodes should be preserved/provenanced, not collapsed by their generated display names.

##### Skinning/joint preservation

Current `AddSkinning(...)` is intentionally too lossy for canonical inspection:

- global joints are pooled by bone **name**;
- first encountered inverse-bind matrix wins;
- per-vertex influences are manually reduced to the top four and renormalized;
- lower source influences are discarded;
- `aiProcess_PopulateArmatureData` is not enabled;
- `AI_CONFIG_PP_LBW_MAX_WEIGHTS = 4` is configured, but `aiProcess_LimitBoneWeights` is not active.

P0 inspection should preserve:

- full source influence lists;
- explicit per-primitive/per-geometry bone bindings;
- inverse-bind matrix per binding;
- node/armature identity when available;
- display/source bone name as provenance only.

The model-inspection Assimp profile should enable `aiProcess_PopulateArmatureData` if it can be integrated without destroying the decoder provenance contract. It should not run weight limiting during inspection.

The existing runtime/cooker limits of four influences and at most 255 joints remain extraction/cooking constraints, not canonical-inspection constraints.

##### Conditional decoder-loss cases that require explicit v1 policy

There is no unconditional P0 architectural blocker, but two source cases cannot be allowed to silently degrade.

**glTF repeated geometry with conflicting skins**

Exact-current glTF importer shares primitive-level `aiMesh` objects between node instances, then writes `node.skin` data into those shared `aiMesh::mBones` records. A later node referencing the same mesh with a different skin can overwrite the earlier binding in the final `aiScene`.

A plain final `aiScene` therefore cannot reconstruct all per-instance skin identities.

Safe v1 policy:

- accept a reused source glTF mesh when all references are unskinned; or
- accept it when every reference uses the same source skin identity;
- reject mixed skinned/unskinned reuse or differing skin identities **unless** the decoder provenance bridge captures the per-node skin binding before Assimp overwrites it.

Never use last-skin-wins.

**FBX reused Geometry with instance-dependent bindings**

FBX caches converted source Geometry and reuses the first converted `aiMesh` indices for later model nodes. Material selection and skin/inverse-bind generation can depend on the first model/absolute transform used for conversion.

Therefore the decoder bridge must capture/check instance-specific material/skin semantics. If two uses of one source Geometry require incompatible bindings that cannot be represented canonically, v1 should reject the ambiguous case rather than guess.

##### Proposed P0 public API

The preferred new API is model-specific rather than expanding the flattened mesh API:

```cpp
struct ModelImportLimits;
struct ModelImportOptions;
struct ModelImportManifest;
struct ModelImportReport;
struct ModelGeometryExtractionSettings;

ModelImportResult InspectModelFile(
    const filesystem::AbsolutePath& source,
    const ModelImportOptions& options,
    const ModelImportLimits& limits,
    ModelImportManifest& output,
    ModelImportReport* report = nullptr) noexcept;

ModelImportResult InspectModelMemory(
    const void* sourceData,
    usize sourceSize,
    containers::StringView formatHint,
    containers::StringView sourceName,
    const ModelImportOptions& options,
    const ModelImportLimits& limits,
    ModelImportManifest& output,
    ModelImportReport* report = nullptr) noexcept;

ModelImportResult ExtractModelGeometry(
    const ModelImportManifest& manifest,
    u32 geometryDefinitionIndex,
    const ModelGeometryExtractionSettings& settings,
    ImportedMesh& output,
    ModelImportReport* report = nullptr) noexcept;
```

Exact names are not frozen. The important contract is:

- inspection owns the full canonical model representation;
- extraction selects a **geometry definition**, never a placement node;
- extraction does not bake placement hierarchy into reusable geometry;
- extraction returns owned cooker-compatible data so existing `SourceMesh` / `CookMesh(...)` stay unchanged.

Assimp progress/cancel support already exists through `Assimp::ProgressHandler::Update(float)`. P0 should expose a tool-level callback/token and install a progress handler rather than inventing a second parser cancellation mechanism. Cancellation must leave no partial published manifest.

##### Existing API compatibility

Existing low-level callers/tests currently use:

```cpp
ImportMeshFile(...)
ImportMeshMemory(...)
ImportedMesh
ImportedMesh::BuildSourceView()
```

P0 should not break those callers in its first slice.

Preferred migration:

```text
new model path:
  InspectModelFile / InspectModelMemory
      -> ModelImportManifest
      -> ExtractModelGeometry(selected definition)
      -> existing SourceMesh / CookMesh

legacy mesh path:
  ImportMeshFile / ImportMeshMemory
      remains behaviorally compatible first
      -> later may be reimplemented as inspect + compatibility flattening
         only after parity tests prove old semantics
```

The legacy compatibility wrapper, when migrated, may deliberately reproduce the old preorder accumulated-transform bake, winding behavior, `defaultMaterial`, flat dependencies and legacy stable-ID behavior for old geometry-only consumers. The new model workflow must never use that compatibility flattening.

##### Aggregate limits

Current mesh import primarily limits depth plus per-submesh counts. Canonical model inspection needs aggregate bounds because it can retain substantially more source structure and skinning evidence.

`ModelImportLimits` should include explicit non-zero limits for at least:

- source snapshot bytes;
- total owned decoded/manifest bytes or equivalent aggregate memory budget;
- node count;
- hierarchy depth;
- geometry-definition count;
- primitive count;
- geometry-instance count;
- material count;
- texture-binding count;
- embedded texture count;
- aggregate embedded texture bytes;
- dependency count;
- vertices per primitive;
- indices per primitive;
- aggregate vertices;
- aggregate indices;
- bone binding count;
- aggregate influence count.

The existing node depth cap of 1024 and current per-submesh vertex/index caps are reasonable compatibility starting points, but exact aggregate defaults should be chosen during implementation with overflow-safe accounting and focused tests. `SourceTooLarge` / `LimitExceeded` must become real bounded outcomes rather than mostly dormant enum values.

##### P0 first implementation slices

Implement in this dependency order:

1. Add the owned canonical manifest, structured diagnostics, limits and inspection API beside the existing mesh API.
2. Add bounded exact-source snapshotting/fingerprinting, progress/cancel and typed dependency tracking.
3. Copy hierarchy/local matrices, materials, textures and primitive-local geometry without placement-transform baking or skin truncation.
4. Add the Assimp decoder provenance bridge for exact glTF/FBX geometry grouping and node-instance relations.
5. Add full skin/bone evidence and the explicit conflicting-instance rejection policy where decoder provenance is insufficient.
6. Add `ExtractModelGeometry(...)` for exactly one reusable definition, producing existing cooker-compatible owned data.
7. Only then consider reimplementing `ImportMeshFile/Memory` through canonical inspection plus compatibility flattening, guarded by parity tests.

P0 does **not** require changes to VMESH hierarchy semantics, the runtime prefab/component hierarchy, P1 persistent output identities, `.vmeta`, Asset Browser UI or Asset Editor UI.

##### P0 focused tests

The first implementation should cover at least:

1. several nodes reference one source geometry -> one geometry definition, several instances, no duplicated geometry payload;
2. transform-only and pivot/synthetic nodes survive with correct parent/child indices;
3. one glTF mesh with several primitives -> one geometry definition with several primitives/submeshes;
4. duplicate node/mesh/material display names do not alias structural identity;
5. repeated same geometry reference within/among nodes remains explicit and collision-free;
6. source material table index is retained independently of material name;
7. embedded texture payload/index/provenance survives;
8. external image authored URI plus resolved/missing state survives;
9. `.glb` succeeds;
10. self-contained/data-URI `.gltf` succeeds;
11. `.gltf` requiring external `.bin` is rejected explicitly while an external image URI alone is allowed;
12. exact main-source snapshot fingerprint is deterministic and witnesses the parsed bytes;
13. source skinning with more than four influences is preserved in the manifest;
14. extraction/runtime policy may later reduce those influences, but inspection does not;
15. repeated glTF mesh with conflicting skins is rejected unless per-instance skin provenance is captured;
16. local translation/rotation, non-uniform scale, negative scale and shear matrices are preserved exactly with diagnostics;
17. selected geometry extraction stays definition-local and does not bake placement transforms;
18. legacy importer parity remains intact for existing geometry-only tests/callers;
19. progress cancellation aborts cleanly with no partial output;
20. every aggregate limit fails deterministically and overflow-safely.

A minimal self-contained glTF JSON/data-URI fixture can be constructed directly inside `mesh_tools_tests.cpp`; a dedicated FBX fixture is only required when proving FBX pivot/grouping/instance-specific behavior that cannot be covered by source-level decoder tests.

##### P0 study conclusion

P0 is implementation-ready.

There is **no unconditional architectural blocker** to beginning the canonical model-inspection layer.

The implementation must, however, preserve or explicitly reject decoder cases whose semantics the final generic `aiScene` has already collapsed. In particular:

- source geometry grouping must come from proved decoder provenance, never from names/adjacency heuristics;
- glTF conflicting per-instance skins must not silently become last-skin-wins;
- FBX reused Geometry with conflicting instance-specific bindings must not silently inherit first-instance semantics;
- full local matrices must survive even when current runtime hierarchy cannot represent them;
- inspection must preserve full skinning evidence before runtime reduction;
- the inspected source fingerprint must witness the exact parsed source snapshot.

With those constraints, the existing VMESH cooker remains a valid downstream target and P0 can be implemented entirely as a new preservation/extraction layer above it.

### P1 — Versioned model settings, stable identities and immutable `ModelImportPlan`

**Why next:** current generic authoring can store many outputs, but it has no model-specific durable correspondence or reviewed plan. We need to decide exact output identities before any source mutation or build routing is trustworthy.

Add a versioned `ModelImportSettings` codec containing the durable decisions from this design, especially source-element correspondence to persistent output keys, import/cook policy, texture-resolution choices, material mapper policy/version, stable slot identities and user-selected managed references. Settings must store decisions/evidence, not copied model payloads.

Build a headless `ModelImportPlan` from:

```text
exact source snapshot/fingerprint
+ ModelImportManifest
+ candidate ModelImportSettings
+ current persisted model metadata, when reimporting
+ current managed project dependency/reference state
```

The plan owns no mutation. It should contain at least the proposed `SourceImportDescription`, exact declared output schema, stable old->new correspondence, additions/removals, unresolved/ambiguous identities, dependency/adoption decisions, source destination, diagnostics and immutable source/revision witnesses.

During refresh/reimport, classify removals and ambiguous identities using the existing `SourceDatabase` backward-reference machinery. Externally referenced identities become explicit blocking reconciliation conflicts; consequence-free model-owned identities may retire/recreate according to the settled design. Do not build a second reference database.

This phase also defines friendly model-output description data for later Asset Editor/search UI without making the Editor decode `ModelImportSettings` directly.

**Exit gate:** given old `.vmeta` + source A and then source B, headless tests produce deterministic preserved/new/removed/ambiguous output decisions and exact external-reference blockers, with zero durable mutation.

### P2 — Stale-safe composite durable model commit

**Why before compilers/UI:** the design promises that the reviewed plan is what gets committed. Exact-current `createImportDescription` cannot inspect source bytes, `ExecuteAssetUpdateSettings` cannot change the output schema, and existing high-level executors each own their transaction. Those seams must be fixed before the first real model import is safe.

Keep inspection/planning outside the Editor and do **not** force model behavior into the old `createImportDescription(SourceRecord)` callback. Add a backend planned-import/commit seam that consumes the already validated `ModelImportPlan` and leaves `ProjectAssetPipeline` as the owner of identity generation, history, transaction admission, rescan/reconciliation and project state.

The minimum durable transaction work is:

- add a transaction precondition/read-only digest assertion so a plan can prove that the exact inspected source still matches at commit time even when that source file itself is not being rewritten;
- add an atomic metadata/output-schema reconciliation operation that updates model settings + declared outputs while preserving the existing parent `AssetId`;
- add one composite model-authoring transaction path able to create/copy the model source, create/update its `.vmeta`, and stage explicitly selected auxiliary source operations such as adoption metadata in the **same** `AssetFileTransaction`;
- retain the existing project asset-operation history/postcondition/rescan boundary around that one commit;
- return a specific stale-plan/source-changed result before mutation when a fingerprint/revision witness no longer matches.

Outside-origin import should reuse the source byte snapshot already captured by `ProjectAssetExternalImportRequest`; a pre-existing candidate under `Assets` should be inspected from one exact loaded snapshot. Neither frontend should be allowed to re-open/re-parse a different file version between review and commit without triggering a rescan.

**Exit gate:** headless tests cover first import, already-under-`Assets` adoption, outside-origin copy, multi-file adoption in the same transaction, injected transaction failure/rollback, source-changed stale rejection and schema-changing reimport while preserving `AssetId`.

### P3 — Compound model build routing and real mesh/material/texture outputs

**Why now:** once output identities are durable, Vanguard can make those identities buildable using the existing output-oriented project graph. Do not invent a parallel model artifact system.

Introduce a custom model source importer/build router. For every requested exact output it must compose the appropriate canonical `BuildRequest`; declaration order is irrelevant. Its source-reference scan must understand all managed references in model settings rather than using the generic primary-output fallback.

Before relying on the mesh path through `BuildGraph`, fix resource estimation: exact-current `MeshAssetCompiler::GetDescriptor()` has no estimator, while `BuildGraph::ResolveOperation()` rejects any prepared plan without `HasResourceEstimate()`. Every new model-output compiler path must provide a bounded estimate before it is registered with the project graph.

Build the real typed outputs in this order:

1. **Mesh outputs:** extract exactly one persistent geometry definition from the shared manifest; cook it through existing VMESH cooking logic; write canonical per-submesh `MaterialResourceReference`s instead of `MeshImportSettings.defaultMaterial`.
2. **Imported material outputs:** translate the source material into Vanguard's canonical material representation and feed the existing material artifact compiler/canonical builder. Shader/pipeline/texture dependencies remain ordinary typed dependencies; provenance must not leak into runtime material classes.
3. **Embedded texture outputs:** route supported encoded payloads through the existing texture compiler with explicit semantic/color-space settings. Managed external-to-container textures resolve to their independent project `TextureResource`s instead. Raw embedded pixel/custom-container support remains an explicit canonical-input task rather than silently materializing source files under `Assets`.

Add a bounded shared inspection/extraction cache keyed by exact source fingerprint plus relevant importer/settings/policy version so N requested model outputs do not run N complete Assimp parses. A process-local cache is sufficient for the first implementation; a persistent canonical DDC intermediate is a later optimization if profiling justifies it.

The generated dependency graph must become ordinary and complete:

```text
MeshResource -> model-owned MaterialResource
MaterialResource -> TextureResource / shader / pipeline
```

Register and validate these routes first in headless asset/build tests. Do **not** wire the Editor project service until one-source/many-output build, VDDC caching, dependency invalidation and package planning are proven.

**Exit gate:** one model source can declare and independently build several `MeshResource`, `MaterialResource` and `TextureResource` outputs through `SourceDatabase::LookupOutput` / `ProjectAssetBuildSystem`, with correct generated dependencies, caching and no repeated full source parse per child.

### P4 — Persisted same-entity component hierarchy and generated composition output

**Why separate:** runtime placed components and transform attachments exist, but exact-current prefab data does not persist a parent component and `CellMaterializer` attaches every placed object component directly to the entity root. The generated model composition cannot represent the settled one-entity component hierarchy until this is fixed.

Extend the prefab/component authoring contract with stable same-entity placed-component parent identity. The exact encoding may be a parent-component stable ID on the component record or an equivalent explicit relation, but it must validate:

- parent component exists on the same prefab entity;
- only compatible placed components participate;
- no self-parent/cycle;
- deterministic stable IDs and serialization;
- missing or malformed parent relationships fail cooking rather than silently flattening.

Extend materialization/transform binding so a placed component with an authored parent is hard-attached to that sibling placed component; only component roots attach directly to the entity placeholder. Preserve current lifecycle/assembly barriers and rollback safety.

Add a project-build adapter for `PrefabResourceType` that reuses `prefabs::CookPrefab(...)`. If a generic canonical prefab-input compiler cleanly serves future authored prefabs, use it; otherwise the first model-composition compiler may be model-specific, but runtime output remains an ordinary `PrefabResource`.

Then implement the model-owned `composition` output:

- one entity for the imported model;
- same-entity component hierarchy reconstructed from manifest nodes/local transforms;
- `StaticMeshComponent`s reference the reusable mesh outputs;
- transform-only source nodes remain meaningful component/pivot nodes as required;
- generated dependencies from composition to meshes make the runtime/package closure explicit.

**Exit gate:** a headless cooked model `composition` prefab instantiates one entity whose component transform hierarchy reproduces a test model with shared wheel mesh instances and pivot/door hierarchy, and world placement can consume the resulting ordinary `PrefabResource`.

### P5 — Reimport retirement, coherent generation publication and last-known-good

**Why before shipping Editor workflows:** exact-current recook already has strong transactional replacement behavior, but schema-changing model refresh adds output removals and a model-generation cutover rule that current per-output behavior does not fully express.

Reuse `IncrementalRecooker`/`BuildGraph`/`DependencyIndex` rather than creating another scheduler. Existing replacement builds already stage records under one `DependencyIndex` transaction and roll back on any build/precommit validation failure. Extend that foundation only where model refresh requires it:

- allow output retirement/removal/tombstone changes to participate in the same dependency-index transaction as replacement records;
- validate source/settings/output-schema generation again immediately before accepted candidate records commit;
- define an explicit required model publication cohort from the accepted plan instead of depending accidentally on whatever outputs happen to be connected by current generated-dependency traversal;
- use existing prepared loose-resource batch visibility for coherent cutover;
- if any required candidate output fails, do not publish the candidate cohort; retain the previous ready cohort as last-known-good;
- on a first-ever import failure, retain durable source/metadata identity but expose no fake Ready composition;
- only retire old externally addressable outputs after reconciliation has explicitly cleared their blocking referencers.

The existing `RequestAssetReimport()` remains useful as **recook current schema**. Add a separate model `Refresh/Reimport from Source` path that runs fresh inspection -> plan -> reconciliation -> atomic schema commit -> cohort build/publication.

**Exit gate:** tests cover successful content-only refresh, schema addition, unreferenced removal, referenced blocking removal, ambiguous referenced mapping, candidate build failure with old generation still active, first-import build failure, cancellation/retry and successful later cutover.

### P6 — Editor pipeline registration and first functional model import frontend

Only after P0-P5 headless contracts are stable should the Editor register/use the model pipeline.

Expand `editor_project_service.cpp` beyond the current texture-only compiler/importer registration to the exact compiler/importer set required by model outputs. Preserve one `ProjectAssetPipeline` owner; do not create an Editor-only cooker or second asset database.

Build the first dedicated model import window/workspace as a frontend over the headless operations:

- asynchronous inspection with progress/cancel;
- candidate working `ModelImportSettings`;
- dependency/missing-texture resolution and bulk project-image adoption;
- plan summary and reconciliation diagnostics;
- stale-plan `Rescan` handling;
- explicit Import / Apply-Reimport commit;
- `Imported / Compiling / Ready / Build Failed` status driven from project pipeline truth;
- retry/cancel without conflating durable import with build success.

Reuse existing Asset Browser OS-drop and queued-file infrastructure. The first model workflow may remain single-source, but its backend/session state must not become a singleton/modal ownership boundary that prevents later batch orchestration.

**Exit gate:** a real Editor import of a self-contained supported model produces managed source + `.vmeta`, builds its typed logical outputs asynchronously, reports failures truthfully and survives restart with the same persistent identities.

### P7 — Model Asset Editor, rendered preview and editor-wide logical-resource integration

Implement the generic Asset Editor/Workspace shell and the settled model tabs only after logical children and composition are real resources:

```text
[ Overview ] [ Hierarchy ] [ Meshes ] [ Materials ] [ Textures ] [ Skeleton ] [ Animations ]
```

Large collections use search/filter plus bounded row clipping/virtualization; expensive child previews are lazy and bounded. The first model Asset Editor must be fully usable without a generic Properties panel and must not create a model-specific fake Properties replacement.

Add the required whole-model rendered preview through the normal resource/rendering path. Then add transient pre-import preview using the same manifest/settings/build machinery and disposable preview artifacts; stale preview generations must never replace newer settings revisions.

Finish the editor-wide resource semantics already settled in this design:

- logical output search/indexing and exact child navigation;
- `Find References` on model-owned logical resources;
- typed reference-field parent drop: 0 reject, 1 auto-resolve, 2+ compatible-output chooser, exact child direct;
- model drag to scene -> generated `composition` prefab placement;
- move/rename preserving child identity; delete accounting for references to every child output; duplicate operating on the whole parent asset;
- child selection publishes the exact typed logical resource to future Editor-wide selection infrastructure without depending on a generic Properties panel today;
- composition-owned material overrides on `StaticMeshComponent` remain separate from model import state.

**Exit gate:** users can import, inspect, search, reference, place and safely reimport a compound model without treating generated children as source-tree files.

### P8 — Scale/batch hardening and deferred policy work

After the complete single-model path is correct, profile large manifests and large output sets. Tighten bounded caches, row virtualization, inspection/build concurrency and preview budgets from measurements rather than guesses.

The existing headless/per-source architecture should then allow a future batch frontend to orchestrate many independent plans, presets/default policies, exceptions and bounded builds without changing importer semantics.

The second-tier policy questions intentionally deferred earlier—editor-only aliases, selective sub-resource import, importer/material-mapper upgrade presentation, reimport undo policy and old-vs-new visual comparison—remain **non-blocking** and should be decided only when their implementation phase is approached.

### First implementation cut

The first authorized implementation should stop well before Editor UI and deliver these three proof points in order:

```text
1. P0: canonical ModelImportManifest preserves reusable geometry + placement hierarchy
2. P1: deterministic ModelImportPlan allocates/reconciles stable logical outputs without mutation
3. P2: stale-safe atomic durable commit writes exactly the reviewed source/.vmeta/output schema
```

Only after those pass focused tests should work proceed into model output compilation. This is the shortest route that avoids building UI or compiler adapters on top of the current flattened/unsafe authoring seams.
