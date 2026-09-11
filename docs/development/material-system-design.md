# Material System Design

## Scope

This document records the accepted material architecture as each study phase is
completed. It covers the path from a future authored material document to
Vanguard's existing shader, pipeline, material, resource, and GPU Scene
contracts.

It does not define the visual editor, render phases, culling, pipeline sorting,
indirect command generation, or draw submission.

## Phase 1: Material Contract and Typed IR

Status: foundational implementation complete.

The implemented Phase 1 boundary now includes:

- explicit Slang material-domain, parameter-type, resource-role, and entry-point annotations;
- reflected domain and program-layout metadata in `VSHADER`;
- separate full domain and material-layout fingerprints in `VSHADER`, `VPPL`, and `VMAT`;
- contract-derived `VMAT` parameters and ordered logical resource slots;
- runtime shader/pipeline stale-contract validation;
- a compact tool-only `MaterialIrModule` with bounded storage, iterative cycle detection,
  type/operation validation, stage propagation, reachable-value compaction, diagnostics,
  and deterministic semantic identity.

The authored source document, node registry, function expansion, optimization/lowering,
and generated Slang accessors remain compiler work for Phase 2. They were not guessed or
embedded into the runtime formats during this phase.

### Sources inspected

The Vanguard study covered the existing shader reflection and Slang compiler,
`VMAT` cooker and reader, `VPPL` shader references, GPU material records, and the
provisional material-interface notes. The important files were:

- `source/shaders/include/vanguard/shaders/shaders.hpp`
- `source/shaderTools/src/shader_compiler.cpp`
- `source/materials/include/vanguard/materials/materials.hpp`
- `source/materials/src/materials.cpp`
- `source/pipelines/include/vanguard/pipelines/pipelines.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp`
- `source/rendering/docs/gpu-scene-implementation.md`
- `docs/formats/vmat-format.md`

The Unreal comparison focused on its material expression model and newer MIR
path rather than its editor widgets or legacy runtime binding model:

- `MaterialExpression.h` and representative expression implementations
- `MaterialIR.h`, `MaterialIRTypes.h`, and `MaterialIRModule.h`
- `MaterialIRModuleBuilder.cpp`
- `MaterialExpressionsToMIR*.cpp`
- `MaterialIRToHLSLTranslator.cpp`
- material function and static parameter code

The local Slang source was also checked. Slang reflection can read user-defined
attributes from types, variables, and functions, and can obtain the layout of a
named arbitrary type. Vanguard can therefore extend its existing shader
compiler instead of inventing a second shader parser.

### Finding: material ownership must be reflected, not selected manually

The shader compiler sees a whole linked program, which can also contain frame
data, GPU Scene tables, pass resources, render targets, and unrelated entry
points. Material ownership therefore cannot be reconstructed safely by a caller
selecting buffers and resources after compilation.

Consequently, none of the following are valid ways to discover material state:

- descriptor space or binding number;
- declaration name conventions;
- all resources reachable from an entry point;
- every constant buffer in the linked shader;
- a renderer-maintained list of familiar PBR fields.

Material participation is declared explicitly in shader source and carried
through reflection. `BuildDescription` accepts only that sealed contract; the
former manual buffer/resource-selection path has been removed.

### Accepted terminology and separation

The system uses five distinct concepts.

#### Material domain contract

A `MaterialDomainContract` is the shader-authored ABI for one family of
materials. It describes:

- stable domain name and schema version;
- evaluation input type;
- evaluation result type;
- legal shader stages and evaluation frequencies;
- required shader capabilities;
- defaults for omitted result fields, where the domain permits omission.

Examples may include surface, decal, terrain, hair, UI, or a project-defined
compute material. These are registrations, not members of one permanent engine
enum. Adding a domain must not change `GpuMaterial`.

The contract is declared by an annotated Slang function signature. Semantically
it has this form:

```text
[VanguardMaterialDomain(stable_name, schema_version, legal_stages)]
DomainResult evaluate(DomainInput input)
```

The precise spelling of the generated function name is tool policy. The ABI is
the reflected attribute plus complete input and result type trees; it is not a
name convention. A shader that does not evaluate a material declares no domain
contract.

#### Material source document

The source document is editable authoring data. It owns nodes, connections,
parameter declarations, graph outputs, references to functions, editor
positions, and presentation metadata. It is never loaded by the renderer and is
not stored inside `VMAT`.

#### Material IR module

`MaterialIrModule` is the transient typed, backend-neutral compiler form made
from one flattened source graph. It contains only reachable values and
operations. It is suitable for validation, optimization, diagnostics, and
lowering to generated Slang. It contains no editor widget state and no HLSL,
DXIL, SPIR-V, descriptor, or NVRHI concepts.

#### Material program layout

`MaterialProgramLayout` is the concrete runtime value layout generated for one
compiled material program. It describes:

- dynamic numeric parameter byte layout;
- ordered logical texture, buffer, sampler, and acceleration-structure roles;
- defaults and required/optional classification;
- the material domain contract fingerprint;
- an accessor ABI version;
- one full deterministic layout fingerprint.

The domain contract says what the material computes. The program layout says
how the values for this compiled program are stored. Different programs may
implement the same domain while having different parameters and resources.

#### Material values

Material values are the flattened dynamic numeric bytes, logical resource
references, and compatible techniques stored in `VMAT`. They are data for one
exact `MaterialProgramLayout`; they are not shader source or a graph.

### Shader-declared contract

The shader compiler must reflect an optional material section in `VSHADER`.
That section contains:

```text
MaterialShaderContract
    domain fingerprint
    domain name and schema version
    legal stage/frequency mask
    material program layout fingerprint
    accessor ABI version
    numeric parameter records
    logical resource role records
```

The fingerprints are full content digests in cooked CPU artifacts. They include
all canonical type, offset, stride, resource-role, default, stage, and version
data. Hash collisions must never be resolved by accepting only a 32-bit value.

The material compiler generates an explicitly annotated Slang parameter type.
The existing Slang reflection path obtains that named type's real target layout.
This provides offsets, sizes, array strides, matrix strides, scalar types, rows,
columns, and matrix order without declaring a per-material constant-buffer
binding.

Logical resources are emitted as explicit material metadata, not inferred from
the program's descriptor bindings. They lower to positions in
`GpuMaterialResource`, while the actual textures and other resources continue
to use Vanguard's global bindless domains.

### Generated shader accessors

The hot shading path uses accessors generated for the exact program layout:

```text
load numeric value at material.parameterByteOffset + baked_offset
load resource at material.firstResource + baked_role_index
```

Offsets and role indices are compile-time constants in the compatible shader
variant. The primary path does not perform a `MaterialLayouts[]` lookup for each
parameter or texture and does not create one constant buffer or descriptor set
per material.

Pipeline artifacts must carry the exact domain and program-layout fingerprints
separately from the existing binding-layout fingerprint. These fingerprints
must not be folded into or confused with descriptor binding compatibility.

### Decision for `GpuMaterial::materialLayout`

The provisional field is retained in size and purpose and is now named
`materialLayout`:

```text
GpuMaterial.materialLayout -> compact runtime MaterialProgramLayoutId
```

The full digest remains the compatibility authority in `VSHADER`, `VPPL`, and
`VMAT`. The compact GPU id supports validation, diagnostics, classification,
and possible future generic paths. Compiled shading does not need to dereference
it for every material value.

This makes the field honest: it identifies the concrete storage layout, not an
editor material, shader object, descriptor table, or dynamically dispatched
interface.

### Type system

The authored graph and IR share one canonical type algebra.

Numeric leaf types mirror the shader reflection types already supported by
Vanguard:

```text
bool
i16, u16, f16
i32, u32, f32
i64, u64, f64
vectors and matrices with explicit dimensions and matrix order
```

The algebra also supports:

- fixed-size arrays with bounded counts;
- named aggregate types supplied by the material domain contract;
- typed textures, including dimension, array form, multisample form, and sample
  type;
- samplers;
- typed and byte-address buffers with declared access;
- acceleration structures;
- internal `void` and `poison` types for control and error propagation.

Unsupported backend types fail during target validation. The source and IR do
not silently narrow a value merely because one active backend lacks it.

Conversions are explicit IR operations except for a small compiler-defined set
of lossless conveniences such as scalar splatting. Boolean/numeric conversion,
signed/unsigned conversion, precision loss, vector truncation, and matrix shape
changes are never implicit.

### Source graph contract

Every source node has:

- a stable document-local node id;
- a stable registered node-type id and schema version;
- typed input and output pin ids;
- serialized node properties;
- optional editor-only presentation data stored outside compiler identity when
  it does not affect meaning.

Node types are registered in tool/compiler code. Serialized documents do not
depend on C++ class names or editor widget types. A node definition supplies pin
schema, property schema, validation, and IR emission behavior.

Graph outputs are the fields or values required by the selected
`MaterialDomainContract`. Vanguard does not own one universal BaseColor,
Roughness, Metallic, and Normal root. A domain can expose an aggregate result or
multiple named outputs while using the same graph and diagnostic machinery.

### Validation rules

Validation occurs before lowering and reports all independent errors that can be
found safely:

- exactly one selected material domain contract;
- required domain outputs are connected or have contract defaults;
- every connection references live nodes and pins;
- source and destination pin types are equal or have an allowed explicit
  conversion;
- value graph is acyclic;
- parameter stable names are unique in the flattened program;
- static and dynamic declarations do not collide;
- resource arrays and numeric arrays are bounded;
- node, edge, function depth, parameter byte, resource, and generated-code
  limits are enforced before allocation grows without bound;
- every operation is legal in every stage that can reach it;
- required backend capabilities are recorded and checked against each target;
- only nodes reachable from a domain output participate in the compiled IR and
  its identity.

Traversal uses an explicit work stack and a deterministic topological pass, not
unbounded C++ recursion. Cycles produce a diagnostic path when practical.

### Material IR contract

IR values and instructions use compact ids into contiguous, arena-owned
storage. Normal compilation does not allocate one polymorphic heap object per
node or value.

The initial operation set includes:

- constants and dynamic/static parameters;
- arithmetic, comparisons, selection, casts, vector and matrix construction;
- field extraction and aggregate construction;
- texture sampling and resource queries;
- common math intrinsics;
- domain input reads and domain result writes;
- function input/output boundaries during source expansion.

Concrete surface shading models are libraries built with these operations, not
IR opcodes embedded in the compiler core.

The builder interns equal constants and pure operations, performs safe constant
folding, propagates poison values after an error, and records stage/frequency
requirements from uses. Canonical IR identity includes operation kind, result
type, canonical operands, semantic properties, static values, selected domain
contract, referenced function digests, and compiler schema version. It excludes
editor coordinates, comments, selection state, and node creation order.

### Static and dynamic parameters

Dynamic parameters are values that may change without recompiling the material:

- numeric values become bytes in the material parameter arena;
- resource values become logical `VMAT` references and later resolved
  `GpuMaterialResource` entries.

Static parameters participate in graph simplification, generated shader source,
shader/permutation identity, and DDC identity. They do not consume runtime
parameter bytes and cannot be changed by a runtime material update.

A value is dynamic by default. Authors must deliberately mark a value static,
and tools must display the compilation/permutation consequence. Static parameter
counts and total requested variants are bounded. A static switch is not a cheap
runtime feature.

### Functions and subgraphs

A material function is a separately identified source document with typed input
and output pins. Compilation resolves a function by resource identity plus
content digest and instantiates it into a call-site context. Functions are
flattened into the transient IR; runtime never walks function calls.

Rules:

- direct and indirect function dependency cycles are hard errors;
- recursion is not supported in the first implementation;
- each call site has a distinct source context for diagnostics;
- equal pure IR produced by different call sites may be interned after source
  attribution has been retained;
- changing a referenced function changes material compilation identity and
  invalidates dependents through the generic dependency index.

### Diagnostics

Every compiler diagnostic contains:

```text
severity
stable diagnostic code
material source resource
node id
optional pin id
function call-site chain
message and optional target/backend details
```

Diagnostics refer to source node identities, not editor widgets and not only
generated Slang line numbers. Generated-code diagnostics are mapped back through
emitted source ranges. This permits headless builds and a future editor to show
the same errors.

### Lowering into existing artifacts

The accepted flow is:

```text
material source document
    -> validate and expand functions
    -> canonical MaterialIrModule
    -> generate Slang + explicit material metadata
    -> existing shader compiler
    -> VSHADER with MaterialShaderContract
    -> VPPL techniques validated against domain/layout fingerprints
    -> VMAT values validated against the exact MaterialProgramLayout
```

`VMAT` remains a small flattened runtime resource. It does not gain nodes,
connections, function calls, or editor metadata.

The current cooked formats carry explicit domain and program-layout
fingerprints. The shader binding-layout fingerprint is not sufficient because
descriptor bindings do not define the raw material parameter/resource layout.
This early-development contract uses one current format only: stale intermediate
or cooked data is rejected and recooked, with no compatibility decoder.

The compiler-driven cooker consumes the reflected `MaterialProgramLayout`
directly. There is no second manual material-interface description for tests or
tools, and a contract-free shader cannot produce a current-format VMAT.

### Unreal lessons adopted and rejected

Adopted principles:

- typed values before backend source generation;
- reachable-value compilation rather than translating every editor node;
- compact arena allocation and value interning;
- iterative dependency traversal;
- explicit subgraph instantiation contexts;
- constant folding and poison/error propagation;
- diagnostics associated with source expressions;
- clear static versus dynamic parameter semantics.

Rejected architecture:

- one fixed engine material-property enum as the universal output schema;
- UObject or editor classes as compiler/runtime ABI;
- one heap object and virtual dispatch per transient IR value;
- legacy HLSL emission directly from every authored expression;
- per-draw material render proxies and uniform buffers as Vanguard's primary
  bindless runtime path;
- a large runtime material-interface table consulted for every parameter load.

### Phase 1 implementation boundary

Phase 1 implements the shader/cooked-format contract and the typed IR foundation. It
intentionally stops before source serialization, the node registry, function expansion,
IR optimization and Slang generation, preview, runtime material loading, residency,
editor UI, or rendering. Those belong to later phases in
`material-system-study-plan.md`.

## Phase 2: Deterministic Compilation, Cooking, Caching, and Preview

Status: complete. Phase 2B, Phase 2C, Phase 2D.1 through Phase 2D.4, and the
final 2D.4.1 resource-value ABI correction are implemented. The canonical
frontend, independently cached artifact graph,
offline domain/capability/resource/dependency contracts, indexed package
closure, declared surface, transactional incremental-recook matrix, bounded
canonical-input parsers, headless preview lifecycle, and cross-target artifact
path are sealed for the Phase 3 handoff.

Phase 2 converts canonical material authoring data into independently cached
`VSHADER`, `VPPL`, and `VMAT` resources. It uses the existing asset build graph,
DDC, dependency index, incremental recooker, loose artifact materializer, and
package builder. It does not add a material-specific cache, job system, package
format, or source watcher.

### Sources inspected

The Vanguard audit covered:

- `source/assets` build, graph, DDC, dependency-index, recooking, and package
  contracts;
- the existing shader asset adapter and `ShaderCompiler`;
- `pipelines::WritePipeline` and `materials::WriteMaterial`;
- the Phase 1 material reflection and typed-IR implementation;
- the mesh and texture asset adapters as examples of bounded production
  compilers.

The Unreal comparison focused on its material IR builder and lowering path,
material shader-map identity and DDC behavior, static parameter handling,
function expansion, compile cancellation, diagnostics, and transient material
preview/apply path. The useful principles are retained without adopting its
UObject compiler ABI, fixed material-property catalog, large shader-map
permutation surface, dual translators, or per-draw material proxy model.

### Existing generic machinery remains authoritative

Vanguard already has the expensive general machinery Phase 2 needs:

- `BuildSystem` computes a deterministic key from source identity and bytes,
  output identity, compiler id/version, target, settings, and sorted dependency
  identities and contents;
- `BuildGraph` provides asynchronous execution, exact-request coalescing,
  cancellation, dependency-cycle rejection, fan-in, and byte admission;
- `DependencyIndex` provides invalidation and transactional replacement;
- `IncrementalRecooker` provides batch cancellation and all-or-nothing index
  changes;
- loose materialization and VPAK assembly already consume the generic artifact
  and dependency records.

Material compilation must register ordinary compiler adapters with those
systems. It must not hide another DDC or compilation queue behind the material
API.

### Authoring data and compiler input are different

The future `MaterialSourceDocument` owns both semantics and presentation. The
compiler consumes a canonical semantic projection, provisionally named
`MaterialCompilerInput`.

The compiler input contains:

- document kind (`material` or `function`) and schema version;
- selected material-domain identity and version;
- stable node type, node, and pin identities;
- semantic node properties and connections;
- dynamic and static parameter declarations;
- function resource references;
- domain outputs and selected pipeline techniques.

It excludes editor coordinates, comments, selection, folding, colors, widget
state, and serialization order. If authored files eventually store semantic and
presentation sections together, the asset adapter extracts only the canonical
semantic section and cook-affecting metadata before constructing a
`BuildRequest`. Presentation bytes must not be placed in `SourceAsset.content`,
`SourceAsset.metadata`, or build settings because all three currently enter the
generic build key.

The frontend produces three separately canonicalized inputs:

```text
MaterialProgramInput   graph, domain, types, static values, entry points
MaterialPipelineInput  one named technique and its fixed pipeline recipe
MaterialValueInput     dynamic values, logical resource references, techniques
```

This partition is what prevents a roughness default or texture assignment from
invalidating shader bytecode.

### Frozen domain and node registries

Phase 1 reflects a compact `MaterialDomainContract`, but its digests alone are
not sufficient to compile an authored graph. Tooling therefore needs a frozen
`MaterialDomainRegistry`. A domain descriptor supplies:

- complete typed input and result schemas;
- defaults for legal omitted outputs;
- legal stages and evaluation frequencies;
- required shader capabilities;
- the Slang wrapper/template dependencies;
- named technique requirements and their pipeline-template identities.

The final reflected VSHADER domain digest must exactly match the descriptor used
by the frontend.

A parallel frozen `MaterialNodeRegistry` supplies stable node type/schema ids,
typed pin and property schemas, validation, and IR-emission callbacks. Node
callbacks emit typed IR operations; they never emit final Slang snippets.

Both registries are immutable while compile jobs are active. Their schema and
code-generation fingerprints are required tool dependencies, so registering or
changing compiler behavior cannot silently reuse stale DDC entries. Material
domains and node libraries remain extensible registrations rather than one
permanent PBR enum.

### Frontend and function lowering

`MaterialFrontend` performs bounded source decoding, registry lookup, source
graph validation, function resolution, reachable-node lowering, static branch
selection, and source attribution. It produces the three canonical inputs plus
a source map and diagnostics.

Material functions use stable typed input/output pin ids independent of display
names and sort order. Calls are flattened at compile time. Each call has a
distinct attribution context; recursion and direct or indirect function cycles
are hard errors. Traversal is iterative and bounded by configured node,
function-instance, call-depth, edge, byte, and diagnostic limits.

Only nodes reachable from requested domain outputs are lowered. Static switches
choose a branch before the unselected branch is requested, matching the useful
part of Unreal's on-demand material lowering. Unreachable nodes cannot change
the program input or generated shader.

The existing compact `MaterialIrModule` is extended rather than replaced. Phase
2 must add:

- a code-generation-complete interned type table, including aggregate fields,
  arrays, matrix order, texture shape/sample type, and buffer kind/access;
- complete operation validation for construction, extraction, resource access,
  and texture sampling;
- canonical little-endian constant encoding and explicit boolean/floating-point
  policy;
- equal constant and pure-operation interning;
- safe constant folding, poison propagation, and unreachable-value removal;
- parameter coalescing when declarations are identical and a hard diagnostic
  when they conflict;
- a side source map capable of retaining more than one attribution for an
  interned value.

Source node and call-site identities do not participate in structural IR
identity and are not embedded into generated shader text. They live in the
source map used to translate compiler errors.

### Generated build graph and ownership

The accepted graph is rooted by the cooked material:

```text
MaterialProgramInput
    -> MaterialProgramAssetCompiler
    -> VSHADER

MaterialPipelineInput + VSHADER
    -> PipelineAssetCompiler
    -> VPPL (one per technique)

MaterialValueInput + VSHADER + VPPLs
    -> MaterialAssetCompiler
    -> VMAT
```

These are separate generated build operations, not unrelated sibling artifacts
from one compiler invocation. Separate operations give each resource a real
identity, dependency-index record, DDC entry, package edge, and reuse boundary.

Ownership is exact:

- `MaterialFrontend` owns source parsing, function expansion, typed IR,
  optimization, static specialization, canonical inputs, and source mapping;
- `MaterialProgramAssetCompiler` owns deterministic Slang generation and calls
  the existing `ShaderCompiler`/VSHADER writer;
- a general `PipelineAssetCompiler` owns the canonical pipeline recipe, opens
  the exact VSHADER, and calls `pipelines::WritePipeline`;
- `MaterialAssetCompiler` opens the exact VSHADER and VPPL resources, constructs
  reflected constant/resource/technique value records, and calls
  `materials::WriteMaterial`.

The VMAT compiler never reconstructs reflected offsets, slots, or compatibility
fingerprints itself. The existing shader, pipeline, and material writers remain
the final validation authorities.

For example:

```text
edit editor node position      -> no compiler input changes
change roughness 0.5 to 0.7   -> VMAT only; VSHADER and VPPL are DDC hits
assign another texture         -> VMAT only; VSHADER and VPPL are DDC hits
change raster/depth state      -> VPPL and VMAT; VSHADER is a DDC hit
change a static switch/graph   -> VSHADER, compatible VPPLs, and VMAT
```

The generic package model currently represents logical cooked-resource edges as
`Generated` dependencies. A texture recook can therefore make the small VMAT
operation dirty, but it must never invalidate the independently keyed VSHADER or
VPPL operations. Phase 2 deliberately does not introduce a second
material-specific package-dependency system. A finer generic distinction can be
added later only if measured VMAT recook cost justifies it.

### Required general build-graph seam

The current graph gives a parent compiler only generated dependency identities
and content fingerprints. A VPPL or VMAT compiler must also read the completed
VSHADER/VPPL bytes and open their reflection data. Phase 2 therefore requires a
general read-only generated-artifact view in `CompileContext`.

The contract is:

- a view names the exact prepared dependency, build/content fingerprints, and
  immutable artifact segments;
- views exist only for the duration of the compiler callback;
- the graph validates every identity and fingerprint against the prepared plan;
- diamond dependants share one child output; artifact bytes are not copied once
  per parent;
- a child output remains alive while external or dependent work still needs it
  and is released after the last dependant finishes or cancels;
- retained dependency bytes are counted once against an explicit graph budget;
- an optional failed dependency has no artifact view;
- direct synchronous execution either receives explicit resolved views or
  rejects a compiler that requires them.

A material-specific DDC lookup callback is rejected. It would duplicate the
graph's ownership and can observe the wrong state during transactional recooks.

The existing shader asset adapter also lacks a resource estimator, while
`BuildGraph` rejects unestimated asynchronous work. Every material, shader, and
pipeline compiler participating in this graph must provide truthful bounded
working-memory and artifact-byte estimates before admission.

### Reflection probe and final Slang generation

Bindless material accessors need numeric offsets from the shader-reflected
parameter layout. Those offsets are not safely knowable from the source graph,
and no suitable Slang `offsetof` contract was found. The program compiler uses a
two-step boundary:

```text
layout declaration/probe source
    -> reflection-only Slang link
    -> reflected parameter offsets and resource roles
    -> final generated accessors
    -> one native shader compilation
    -> require final domain/layout fingerprints == probe fingerprints
```

The first step is reflection-only, not a complete native-code compile. A probe
and final layout mismatch is a hard compiler error. Resource slots are assigned
densely and deterministically because the current shader format requires the
canonical resource index to equal the reflected slot.

Parameter identities must come from one shared canonical interface-name hashing
contract used by shader reflection and material cooking. The material compiler
must not independently guess the private hash currently used while reflecting
hierarchical constant members. Friendly editor names never become the runtime
identity by accident.

### Cache and dependency identity

There is no separate material cache key. Canonical program, pipeline, and value
inputs become the source/settings bytes of ordinary `BuildRequest`s, and the
generic build key remains authoritative.

The dependency layers are:

- authored material/function/domain inputs that can change frontend results are
  source dependencies of the material build;
- node-registry, frontend/codegen schema, pipeline-writer, and shader-compiler
  versions are tool dependencies;
- wrapper/include and pipeline-template contents are exact source dependencies
  of the artifact compiler that consumes them;
- VSHADER, VPPL, textures, and other cooked resources are generated
  dependencies of the appropriate downstream operation.

`MaterialIrModule::GetContentFingerprint()` participates in program/permutation
identity, but it does not replace the complete generic build key. A function
change first reruns the frontend; if the lowered program input is byte-identical,
the downstream VSHADER request remains a DDC hit.

Static values are explicit, sorted compiler inputs. One program request compiles
one complete static assignment. The compiler never enumerates the Cartesian
product of declared switches. Configurable limits bound static declarations,
static-value bytes, requested variants per batch, function expansion, generated
source bytes, and techniques. Overflow is rejected before graph scheduling with
a source-correlated diagnostic. Dynamic parameters remain VMAT values and do
not enter shader permutation identity.

### Structured asynchronous diagnostics

The present `bool` compiler result plus log line is insufficient for headless
material compilation. Phase 2 adds a bounded generic build-report channel so
other asset compilers can reuse it. A material diagnostic contains:

```text
severity and stable compiler code
source resource
node id and optional pin id
function call-site chain
generated source line/range, when applicable
target/backend detail
bounded human-readable message
```

Material-specific codes and attribution stay in `materialTools`; the transport
belongs to `assets`. `GraphRequest` must allow the caller to copy the completed
report even when the operation fails, and a dependency failure must retain the
causal compiler report. Counts, chain depth, and total text bytes are bounded.
Failures do not create persistent DDC entries.

Generated Slang has a deterministic side source map. Slang line/range errors are
translated back to the material node/pin and function call context; editor
object pointers and widget identities never enter compiler results.

### Headless preview and Apply

Preview uses the same frontend, compilers, canonical inputs, and DDC as
production cooking, but not the production dependency index or loose output
overlay:

```text
preview BuildGraph      dependency index = none
production BuildGraph   committed dependency index
```

The headless preview service follows these rules:

1. Each edit receives a monotonically increasing revision.
2. Submitting a newer revision cancels the old request's interest.
3. A synchronous Slang call may finish internally, but cancellation is checked
   before and after it and an obsolete result is discarded.
4. Completion is accepted only when its revision is still current and every
   VSHADER, VPPL, and VMAT artifact opens and validates.
5. Failure, cancellation, or stale completion never replaces the one bounded
   last-valid preview result.
6. Apply first commits the authored document through the future asset layer;
   that layer derives the exact canonical semantic input and submits it through
   the production graph or incremental recooker. Apply never copies transient
   preview bytes directly into the live loose overlay.

With identical production settings, Apply should normally reuse the preview's
DDC entries. Preview-only target/settings changes form different ordinary cache
keys rather than hidden flags.

### Phase 2 implementation slices

Implementation should remain four bounded slices:

1. **2A — build and shader prerequisites:** generated-artifact views with
   dependent retention/budgeting, structured build reports, truthful shader
   estimates, and the reflection-only shader path.
2. **2B — material frontend:** canonical source decoding, frozen domain/node
   registries, function expansion, IR hardening, optimization, three compiler
   inputs, and source maps.
3. **2C — artifact graph:** MaterialProgram-to-VSHADER, general
   pipeline-to-VPPL, and value-to-VMAT compiler adapters with exact dependency,
   DDC, loose-output, and package behavior.
4. **2D — headless preview and proof:** revision cancellation, last-valid and
   Apply behavior, deterministic bytes, invalidation matrix, dependency-view
   lifetime/budget tests, and loose/VPAK end-to-end tests.

The ordered Phase 2B subphases are fixed as:

1. **2B.1 — canonical frontend and hardened typed IR;**
2. **2B.2 — safe constant folding and poison propagation;**
3. **2B.3 — general IR-to-Slang generation and reflection-derived accessors;**
4. **2B.4 — generated-source diagnostics mapped to authored nodes and pins;**
5. **2B.5 — authored semantics partitioned into canonical MPGI/MPLI/MVLI inputs.**

The ordered Phase 2C subphases are:

1. **2C.1 — persistent-DDC loose-output reconstruction and cross-artifact validation;**
2. **2C.2 — offline contract sealing:** make the frozen frontend domain and
   final reflected VSHADER domain one checked identity, enforce target
   capabilities as an offline cook-target contract plus concrete resource-type
   compatibility, and preserve required/optional/soft dependency semantics
   exactly;
3. **2C.3 — indexed VPAK closure:** publish the real material artifact graph to
   the dependency index, reconstruct VPAK from persistent DDC, and prove
   standalone/package identity of the decoded logical resource bytes plus
   production-reader validation. Compressed/container VPAK bytes are not
   expected to equal standalone files.

The ordered Phase 2D proof subphases are:

1. **2D.1 — full declared-surface vertical proof — complete:** exercise the production
   canonical path across multiple techniques and domains, every currently
   declared type/resource family, arrays/aggregates/matrices, defaults, and
   positive and negative resource assignments;
2. **2D.2 — material-specific incremental recooking and reproducibility — complete:** proves
   the exact MPGI/MPLI/MVLI invalidation matrix through `DependencyIndex` and
   `IncrementalRecooker`, including restart, cancellation, failure rollback,
   request coalescing, and deterministic repeated builds;
3. **2D.3 — adversarial robustness and preview lifecycle — complete:** covers
   malformed and near-limit inputs plus replacement, cancellation, stale
   completion, retention-budget, last-valid, Apply, and shutdown behavior;
4. **2D.4 — cross-target proof and final freeze — complete:** run the full material path for
   every advertised target, remove any unsupported claim, pass the complete
   test matrix, and freeze the current Phase 3-facing contracts and docs;
5. **2D.4.1 — portable resource-value ABI correction — complete:** represent every
   logical resource value as a descriptor-index `uint` in generated values and
   domain outputs, retain the complete opaque type only in reflected resource
   roles, materialize typed resources at consuming operations, and rerun the
   full declared-surface graph on DXIL and SPIR-V. This is a handoff correction,
   not historical binary-compatibility support.

Phase 2 uses a clean early-development format policy. Cooked outputs, DDC
entries, dependency indexes, and intermediate compiler inputs are disposable.
Contract changes update the current format in place, reject stale data, clear
development caches, and rebuild. Phase 2 adds no migration, dual-version reader,
compatibility adapter, or fallback for older cooked data. Existing version
fields are stale-data guards only, not promises to support historical formats.

Phase 2 is closed because canonical material input produces independently cached,
mutually validated VSHADER, VPPL, and VMAT artifacts for every advertised
target; the sealed domain/resource/dependency contract survives DDC, loose, and
VPAK reconstruction; the material-specific recook matrix is transactional and
reproducible; and bounded adversarial plus headless preview/apply tests pass. It
performs no runtime loading, GPU residency, bindless resolution, parameter
upload, editor UI, rendering, culling, sorting, indirect command generation, or
draw submission.

### Phase 2 implementation checkpoint

Implemented and proven:

- `assets::CompileContext` exposes immutable generated dependency artifact views;
- `BuildGraph` retains child artifacts once, reports causal diagnostics, and
  accounts compiler working memory separately from bounded retained outputs so a
  valid fan-in graph cannot deadlock admission;
- `ShaderCompiler` has a reflection-only path and truthful graph estimates;
- canonical interface-name hashing is shared by shader reflection and material
  cooking;
- frozen node/domain registries, bounded reachable/function lowering, static
  branch pruning, source attribution, canonical scalar encoding, IR interning,
  compact-id remapping, and stable registry ordering are implemented in
  `source/materialTools`;
- the finalized IR owns a deterministic interned type table with ordered aggregate
  fields, fixed arrays, explicit matrix order, typed texture shapes/sample types,
  sampler kind, buffer kind/access, and strict Construct/Extract/TextureSample
  validation;
- finalized reachable IR now runs a bounded, deterministic optimization pass:
  target-stable integer arithmetic and comparisons, constant selects, same-type
  casts, and packed non-matrix numeric construction/extraction fold; integer
  divide-by-zero enters a side poison lattice, propagates to dependents, and
  reports its originating source site exactly once if it reaches an output;
  constant selects suppress poison in an unchosen branch, after which
  reachability is rebuilt so dead values do not affect parameters, stages,
  types, identity, or emitted IR;
- floating-point arithmetic deliberately remains unfurled until target compiler
  semantics are known; the material IR optimizer does not silently choose the
  host CPU's denormal, contraction, or fast-math policy;
- the deterministic IR-to-Slang bridge covers fixed arrays of 32-bit numeric
  and nested aggregate values, explicit row-major and column-major matrices,
  typed texture/filtering-sampler parameters, and `TextureSample`; dynamic
  parameter leaves use canonical hierarchical identities, resource roles are
  semantic-sorted into dense logical slots, and generated bindless accessors
  call the domain ABI's resource/sampler descriptor-index loaders;
- probe accessors carry reflected offset, matrix-stride, and array-stride
  markers, and `MaterialProgramAssetCompiler` resolves those markers from Slang
  reflection before its single native compile;
- numeric source generation covers Bool plus signed, unsigned, and floating
  16/32/64-bit scalars; parameter accessors use scalar-size-aware component
  addressing, mask 16-bit words, and combine two words for 64-bit values;
- generated resource roles cover typed, structured, and byte-address buffers
  with read/write access, acceleration structures, and filtering/comparison
  samplers in addition to textures; byte-address and acceleration reflection
  fingerprints explicitly encode their absent element type;
- no buffer load/store, comparison-sample, or ray-query operation was invented
  in source generation: those require corresponding typed IR opcodes and
  validation before they can become legal material graph operations;
- generation emits deterministic line ranges plus primary authored node/pin
  attribution for every finalized IR value; canonical MPGI v3 serializes a
  bounded, dense, ordered range table together with the exact expected material
  domain and required target capabilities, and the decoder accepts only that
  current encoding; reflection and native-compile Slang
  diagnostics preserve generated line/column and map their structured
  subject/detail back to authored node/pin;
- `MaterialCanonicalBuildSet` owns the production frontend bridge from a frozen
  authored graph to MPGI, one domain-required MPLI per technique, and MVLI; it
  validates domain/technique identities, sorts canonical records, owns all
  request bytes, and returns ready `BuildRequest` views without hand assembly;
- technique entries can now select distinct MPGI/VSHADER resources over that
  same finalized source, graph permutation, defines and compile settings.
  `Programs()` exposes the primary and pass programs; `Program()` remains the
  primary layout authority. Pipeline dependencies retain the variants, and VMAT
  admits them only with matching graph permutation and complete material ABI;
- dynamic parameter bytes remain available for MVLI construction but are
  excluded from finalized IR structural identity, so a dynamic value edit keeps
  MPGI and MPLI byte-identical while changing MVLI; authored value reordering is
  proven to leave all three inputs identical, and frontend failures retain their
  source resource plus node/pin;
- MVLI v3 is the sole current format and carries logical typed constants rather than pretending authored bytes
  already have reflected GPU layout. VMAT cooking resolves every constant by its
  VSHADER identity and packs bool widening, vector orientation, fixed-array
  stride, row/column-major matrix stride, and flattened aggregate leaves into
  the exact reflected footprint. Arrays of nested aggregates are transposed
  from authored aggregate-major bytes into reflected strided leaf values;
  interleaved array leaves are validated and copied sparsely so one leaf's
  padding cannot erase another. This is a current-format cut with no legacy
  decoder;
- the vertical artifact proof now builds finalized IR and generated probe Slang
  rather than supplying a pre-authored final material program; the existing
  probe/final layout fingerprint check remains authoritative;
- three ordinary `BuildSystem` compiler adapters create VSHADER, VPPL, and VMAT
  operations with exact generated edges and use the existing shader, pipeline,
  and material writers as validation authorities;
- the material vertical proof stores those independently cached outputs in the
  persistent DDC, reconstructs VSHADER, VPPL, and VMAT through the generic loose
  materializer, proves byte identity with each cached artifact, reopens them with
  the production readers, and revalidates shader/pipeline plus material
  domain/layout/technique identities; invalid indexed VMAT metadata cannot
  replace the last validated loose file;
- dynamic value edits reuse VSHADER and VPPL cache entries while rebuilding only
  VMAT;
- the isolated preview graph supports revision replacement, cancellation,
  fully validated acceptance, bounded last-valid retention, and Apply-by-revision.

The executable vertical proof now crosses the real two-pass generation seam and
uses the production canonical-input bridge. Phase 2B and Phase 2C are complete.
The 2C.2 closure establishes:

- one canonical `MaterialDomainContract` and SHA-256 identity shared by the
  frozen frontend registry, canonical MPGI, reflection probe, final native
  compile, and `VSHADER`; schema version, legal stages, input/output type
  identities, and required offline capability bits must all match exactly;
- an offline, target-indexed shader-capability policy that is independent of the
  active RHI/device, participates in material compiler identity, and rejects a
  canonical program before native compilation when its reachable IR types need
  an unsupported feature;
- a concrete resource compatibility policy. The currently supported assignment
  is Texture -> `VTEX`; Buffer, Sampler, and AccelerationStructure have no
  invented asset type and are legal only when optional and unbound. `VMAT`
  stores and revalidates every slot's expected concrete asset type;
- exact Required/Optional/Soft semantics across material discovery, build keys,
  graph execution, dependency-index persistence, recooking, and package records.
  Soft is a generated identity-only edge: it is never resolved, built, exposed
  as an artifact view, followed for invalidation, or pulled into package closure;
- current-format-only cuts for MPGI, VSHADER, VMAT, and the dependency index.
  Stale data is rejected and rebuilt; no legacy reader or migration path exists.

The 2C.3 closure additionally establishes:

- real compiler-produced VSHADER, VPPL, and VMAT records are published to the
  dependency index, persisted, and reopened by a fresh production index;
- a VMAT package root expands to the exact required VSHADER/VPPL/VMAT closure,
  retaining the material-to-shader, material-to-pipeline, and
  pipeline-to-shader dependency types and Required kinds;
- the generic package assembler reconstructs an LZ4 VPAK directly from the
  persistent DDC records and publishes it atomically;
- every decoded package resource is byte-identical to both its compiler artifact
  and standalone loose resource, then reopens through the production shader,
  pipeline, and material readers with matching domain, layout, and technique
  identities;
- invalid indexed artifact data is rejected before replacement, preserving the
  last validated VPAK and leaving no temporary publication behind.

The corrected 2D.1 closure (2D.1.1–2D.1.3) additionally establishes:

- a table-driven canonical fixture crosses the production frontend, optimized
  typed IR, generated/probed/final Slang, VSHADER, two required VPPL techniques,
  and VMAT for a second frozen domain;
- all ten declared scalar kinds, vectors, fixed arrays, row-major and
  column-major matrices, nested aggregate arrays with row/column-major matrix
  leaves, texture arrays, filtering samplers,
  read/write structured buffers, acceleration structures, authored defaults,
  and a domain-output default reach the final cook without adding a graph opcode
  or concrete PBR library;
- production readers verify reflected parameter/resource counts, bool widening,
  array/matrix/nested-aggregate-array values at reflected strides, both technique identities,
  and Optional/Soft texture assignments. A typed texture assigned to the buffer
  semantic and an unsupported acceleration capability are rejected before
  artifact cooking;
- the compiler-produced four-artifact set reconstructs byte-exactly from
  persistent DDC as atomic loose files. A VMAT package root closes over exactly
  VMAT + VSHADER + two VPPLs, excludes absent Optional/Soft VTEX resources, and
  reopens through the production readers after VPAK decoding.
- **2D.1.1 declared-surface correction:** logical aggregate arrays flatten into
  reflected leaf arrays, including nested aggregates and matrix leaves;
- **2D.1.2 contract de-bloating:** `MVLI` v3 and `BuildDescription` expose only
  the sealed shader-derived interface; the manual selector and compatibility
  reader are absent, and stale intermediate data must be recooked;
- **2D.1.3 proof hygiene:** failures are attributed to named IR/frontend,
  Slang/reflection, canonical-contract, indexed-DDC/VPAK, and preview suites,
  and both Phase 2 documents record the same closure boundary.

The 2D.2 closure establishes the material-specific behavior of the generic
incremental build machinery:

- canonical dynamic-value, pipeline-template, and structural-graph edits change
  only MVLI, MPLI, and MPGI respectively;
- the persistent `DependencyIndex` and real `IncrementalRecooker` expand those
  edits to exactly one output (VMAT), two outputs (VPPL + VMAT), and three
  outputs (VSHADER + VPPL + VMAT), while minimizing each transaction to one
  terminal VMAT root;
- build-setting and material-tool-fingerprint changes invalidate the complete
  closure. Per-request target identity is proven on VMAT; complete successful
  cross-target generation is closed by 2D.4;
- a generated VTEX source edit invalidates VTEX and its dependent VMAT, while an
  unavailable Optional VTEX is removed from the VMAT content closure without
  failing the material cook;
- identical material roots coalesce, active referenced-resource cancellation
  rolls back staged publications, an invalid VMAT build rolls back without
  replacing the last committed records, and transaction state is closed after
  both paths;
- persistent-index restart preserves the last committed closure, and reverting
  dynamic, pipeline, structural, settings, tool, and target edits reproduces the
  original build and content fingerprints. Repeated requests reuse the same DDC
  entries.

The 2D.3 closure establishes bounded adversarial handling and the complete
headless preview lifecycle:

- MPGI, MPLI, and MVLI reject bad magic, truncation, trailing bytes, and their
  first one-over-limit count before preparing a build; exact entry-point,
  vertex-stream, and technique limits remain accepted;
- a completed preview releases its transient graph requests and retains only a
  copy-then-commit last-valid VMAT, so failed acceptance cannot erase the prior
  valid material;
- pipeline-count and aggregate-artifact-byte budgets reject transactionally,
  while failed and explicitly cancelled revisions preserve the last-valid
  revision and its Apply behavior;
- active replacement cancels obsolete generated-resource work and cannot admit
  its stale completion; explicit cancellation and shutdown cancel and join all
  active roots, including a deliberately blocked generated VTEX dependency;
- successful Apply, owner-rejected Apply, failed-revision Apply, inert
  post-shutdown behavior, and repeated race-sensitive lifecycle runs are proven
  without editor UI or runtime residency.

The 2D.4 closure establishes the final cross-target boundary:

- the same full declared-surface material cooks through the production
  VSHADER-to-VPPL-to-VMAT graph for Windows/D3D12, Windows/Vulkan, and
  Linux/Vulkan;
- Vulkan outputs contain real SPIR-V payloads, Windows and Linux Vulkan retain
  distinct DDC build identities while producing byte-identical artifacts, and
  DXIL/SPIR-V preserve one material-domain fingerprint and accessor ABI;
- generated material values and output aggregates carry resource descriptor
  indices, while annotated resource-role declarations retain exact texture,
  sampler, buffer, access, and acceleration-structure types for reflection;
- resource-consuming graph operations resolve typed opaque resources locally;
  texture sampling is proven on both backends, while direct resource outputs,
  resource arrays, and every declared resource family cross the evaluator ABI
  as descriptor indices instead of opaque objects;
- reflected VMAT packing accepts backend-required trailing member padding while
  still requiring every addressed scalar, matrix stride, and array stride to
  fit inside the reflected member;
- platform/backend mismatches are rejected at preparation, and the affected
  asset, shader, pipeline, material, and material-tool test matrix passes.

Phase 2's compiler behavior and authored-to-artifact semantics are closed at
the offline/runtime boundary. Phase 3B's later runtime audit found one narrow
metadata omission: the reflected role must retain reconstructable resource
shape beside its existing full type fingerprint. 3B.1 made that clean-cut
current-format correction without reopening graph behavior or adding legacy
artifact compatibility. The next ordered phase is **Phase 3 — runtime loading
and bindless GPU residency**. Phase 2C's offline compiler/artifact seams and
every other Phase 2D closure remain in force.

These are intentionally not replaced with a hard-coded PBR emitter or editor
schema. Doing so would make the passing vertical test look complete while
breaking the project-defined material-domain requirement.

## Phase 3A: Loaded Resource Closure and Runtime Identity

Status: **complete and sealed**. Phase 3B and 3C.1 through 3C.4 are also
complete; Phase 3 is frozen.

Phase 3A turns the three frozen Phase 2 artifacts into one validated immutable
CPU-side resource closure. It deliberately stops before descriptor allocation,
parameter upload, `GpuMaterial` publication, native shader creation, or native
pipeline creation. Those are renderer-owned concerns in 3B and 3C.

### Evidence from the existing runtime

The accepted design follows mechanisms already present in the repository:

- `ResourceStreamer` already resolves the loose/VPAK overlay, reads and checks a
  bounded logical resource, verifies its CRC, fans in indexed dependencies, and
  invokes a registered decoder on a worker;
- `ResourcePipeline` already coalesces requests by resource identity, propagates
  priority and cancellation, rejects a failed Required dependency, and supplies
  an empty handle plus failure for a failed Optional dependency;
- Soft dependencies are intentionally omitted from the pipeline fan-in by both
  loose and package streaming paths;
- `MeshResourceObject` proves the required ownership rule: dependency handles
  supplied through `LoadContext` are temporary construction inputs, so a
  published object must copy every strong dependency handle it needs to retain;
- `ShaderFile`, `PipelineFile`, and `MaterialFile` already provide bounded
  readers and the frozen fingerprints/references needed for runtime validation;
- `RenderShader::Load` requires an initialized RHI, while
  `RequestRenderPipeline` may require a render-pass attachment signature. They
  therefore cannot be part of a generic worker-thread artifact decoder;
- `ResourceStreamingService` is already the process-wide, main-thread
  composition root for `ResourceStreamer`, mesh loading, texture loading,
  quiesce/drain, and reverse-order loader shutdown.

Consequently, `VSHADER`, `VPPL`, and `VMAT` use the streamer's ordinary
full-resource `DecoderDescriptor` path. They do not need the mesh/texture custom
paged-loader path, and they are not reflection-schema objects requiring
`SchemaDecoderDescriptor`.

### Accepted CPU resource objects

Each published object is immutable after construction:

- `ShaderResourceObject` owns one open `ShaderFile`;
- `PipelineResourceObject` owns one open `PipelineFile` and strong handles to
  the exact shader generations against which it was validated;
- `MaterialResourceObject` owns one open `MaterialFile`, the exact shader and
  pipeline generations against which it was validated, and a dependency
  snapshot for its resource parameters.

The object types belong with their format-owning runtime modules (`shaders`,
`pipelines`, and `materials`). The thin decoder-registration composition belongs
to `ResourceStreamingService`, which may depend on all three modules. The core
`streaming` module must not depend upward on material, pipeline, shader,
rendering, compiler, preview, or editor code.

A retained dependency snapshot contains the cooked reference, its
Required/Optional kind, the strong `ResourceHandle` when loading succeeded, and
the `resources::Failure` when an Optional load failed. Soft references remain in
the parsed `MaterialFile`; they have no pipeline edge and no retained handle.

The root `ResourceHandle` plus its registry generation is the CPU material
identity. Phase 3A does not add a second material request state machine or a
parallel `MaterialCpuHandle`. Holding the root handle retains the immutable root
object; the root object's copied strong handles retain the exact dependency
generations that form its closure.

### Dependency contract

The runtime meaning of the three cooked dependency kinds is fixed as follows:

| Kind | Pipeline edge | Parent construction | Published closure |
| --- | --- | --- | --- |
| Required | Required | Any dependency failure fails the parent; no object is published | Strong handle retained |
| Optional | Optional | Parent may publish after success or failure | Strong handle on success; empty handle and failure on absence/failure |
| Soft | None | Never requested and never gates the parent | Logical typed reference only; resolution is an explicit later request |

All non-Soft dependencies inherit the root operation's normal priority and
cancellation behavior. Phase 3A adds no private job graph and no blocking wait.
Cancellation before publication produces no object. Once an immutable object is
published, releasing caller interest follows the existing handle/registry
lifetime rules.

Cooked dependency metadata is an index, not an authority. Every decoder derives
the expected dependency list from the parsed artifact and compares it exactly
against `LoadContext`: reference path, expected type, Required/Optional kind,
count, and uniqueness must match. Soft entries are excluded on both sides. This
check applies equally to loose metadata and an indexed VPAK, so neither source
can substitute, omit, duplicate, or strengthen/weaken an edge silently.

### Runtime validation matrix

Individual file parsing is necessary but not sufficient. The following checks
run again after dependency fan-in and before publication:

| Object | Required runtime validation |
| --- | --- |
| `VSHADER` | Bounded `ShaderFile::Open`; stage formats and bytecode digests valid; dependency list empty. Active-backend compatibility is checked at renderer admission because headless resource streaming has no device/backend owner |
| `VPPL` | Bounded `PipelineFile::Open`; every shader reference resolves to a typed `ShaderResourceObject`; path, permutation, binding-layout fingerprint, pipeline-interface fingerprint, and any material domain/layout fingerprints match; `ValidateShaderCompatibility` succeeds for all checks legal before a deferred attachment signature exists |
| `VMAT` | Bounded `MaterialFile::Open`; shader and every technique pipeline resolve to the exact typed retained objects; shader material contract exists; domain and layout fingerprints, parameter byte size/layout, resource slots/roles/types, shader reference, pipeline technique reference, permutation, binding-layout fingerprint, pipeline-interface fingerprint, and material fingerprints agree wherever those fields are carried |

For an attachment-exact pipeline, compatibility includes its cooked attachment
signature. For a deferred-attachment pipeline, Phase 3A validates every stable
field and defers only the concrete attachment check to the renderer request that
actually supplies the signature. Deferral is not permission to skip shader or
material compatibility.

VMAT and VPPL authenticate the accessor ABI through their exact material-layout
fingerprint; the numeric ABI value itself is owned by the shader material
contract. It is copied into the canonical layout-registry record rather than
inventing a redundant authority in the material object.

Format/read failures map deterministically into the existing
`resources::Failure` vocabulary and `FailureTrace`. Phase 3A does not create a
second diagnostics channel. A successfully loaded material may expose its
Optional dependency results for later fallback decisions.

### Runtime identity, replacement, and fallback

`ResourcePipeline` continues to coalesce CPU requests by the existing typed
resource key. Phase 3A does not add content-digest coalescing: two paths may own
byte-equivalent CPU objects. Resolved-content coalescing, if useful, belongs to
the GPU material key studied in 3B.

Registry generation replacement is immutable and non-destructive. A loaded
generation cannot be evicted until its strong handles are released; after
release, a new load constructs and validates a new closure under a new
generation. Old weak handles and completion tokens cannot cross that generation
boundary. No published closure is patched in place and two generations of the
same path are not simultaneously exposed by the current registry.

The CPU loader does not inject default textures, null descriptors, fallback
pipelines, or a previous material generation. Required failure means no new CPU
object. Optional failure is recorded. Soft remains unresolved. Typed GPU
fallbacks are selected during 3B resolution, and last-valid resident replacement
is a renderer lifecycle rule in 3C. This separation prevents CPU loading from
claiming GPU readiness or hiding missing dependencies.

### Material program-layout identity

`GpuMaterial::materialLayout` needs a compact value, but the authority remains
the complete reflected shader contract. Phase 3A therefore introduces one
renderer-owned `MaterialProgramLayoutRegistry` with these rules:

- registration input comes only from a validated `ShaderResourceObject` material
  contract;
- renderer admission rejects any shader stage whose `NativeFormat` is not
  compatible with the active backend before returning a layout id; this is a
  read-only compatibility check and creates no native shader;
- the primary key is the complete 256-bit material-layout fingerprint;
- the interned canonical payload also includes the domain fingerprint, accessor
  ABI, parameter byte size and exact parameter records, plus exact resource
  roles, flags, and type fingerprints;
- a repeated full fingerprint must have a byte-equivalent canonical payload;
  any disagreement is an integrity/collision failure rather than aliasing;
- the returned `MaterialProgramLayoutId` is the compact value stored in
  `GpuMaterial::materialLayout`; lookup always reaches the full canonical record
  and never trusts a truncated hash;
- ids are bounded, allocated by the registry, and are not reused during one
  renderer/device lifetime. Records may be reference-counted, but a zero-use
  record remains interned until registry shutdown, avoiding ABA and GPU-fence
  coupling before 3B exists.

The registry is populated on the renderer's owning thread when a validated CPU
closure is admitted for runtime use, not inside the worker decoder. Registration
does not create RHI objects or descriptors. The VMAT layout fingerprint must
resolve to the canonical record derived from its retained shader.

### Threading, service lifetime, and shutdown

Artifact reading, parsing, metadata comparison, dependency-object inspection,
and immutable object construction may run on `jobs::AnyWorker`, as the existing
pipeline permits. Decoder code performs no RHI call, descriptor mutation,
pipeline-cache request, render-service callback, compiler invocation, or editor
callback.

`ResourceStreamingService::OnInitialize` registers loaders in dependency order:

1. `VSHADER`;
2. `VPPL`;
3. `VMAT`.

Any partial initialization failure unregisters the successful prefix in reverse
order. Normal shutdown first requires the package set to be unmounted and the
streamer to report no active loads/reads/staging memory, then unregisters
`VMAT`, `VPPL`, and `VSHADER` before existing lower-level loaders and finally the
streamer. A refused unregister is a real live-state failure; callback state is
never freed while a decode can still reach it.

This ownership keeps CPU material loading available to headless runtime tests
without requiring `RenderingService` or an initialized device. The later layout
registry and native residency services depend on resource streaming, not the
reverse.

### Unreal sanity check

The result preserves the useful Unreal separation without importing its object
model. The immutable ResourcePipeline closure corresponds to compiled material
and shader-map state, not to a render proxy. Renderer admission and the later
resident material record correspond to render-thread-facing state and are the
only layers allowed to choose fallback or own GPU lifetime. Like Unreal's
render-resource initialization, native creation is explicitly kept off generic
load workers. Unlike Unreal, Vanguard does not add UObject ownership, parent
chains, per-material uniform buffers, or a render-proxy hierarchy: VMAT is
already flat and its eventual endpoint is a compact global GPU record.

### Rejected designs

The following alternatives are explicitly rejected for 3A:

- creating `RenderShader` or requesting a native render pipeline in a decoder;
- treating a loaded VMAT as sufficient while resolving its shader/pipelines by
  path again later, which would lose exact generation closure;
- trusting loose/VPAK dependency indexes without comparing parsed payloads;
- treating Soft as Optional or injecting a hidden load edge;
- publishing a VMAT after Required dependencies load but before cross-artifact
  compatibility is checked;
- keying `materialLayout` by a truncated digest or recycling its id while the
  renderer/device remains alive;
- adding migration or compatibility paths for stale early-development cooked
  formats. Current readers reject them and content is rebuilt.

### 3A implementation slices and completion proofs

1. **3A.1 — artifact resource objects and decoders (complete)**
   - the three immutable objects and bounded ordinary decoders are implemented;
   - exact dependency handles and Optional failures are retained;
   - parsed non-Soft dependencies are compared exactly with stream metadata;
   - focused loose and indexed-VPAK tests prove byte-equivalent VMAT closure,
     transitive ownership, Required/Optional/Soft semantics, and rejection of
     incomplete dependency metadata.
2. **3A.2 — closure validation and layout identity (complete)**
   - VPPL publication validates each exact retained VSHADER generation and all
     stable compatibility fields; deferred attachments defer only their concrete
     render-target signature;
   - VMAT publication validates its exact shader generation, technique pipeline
     generations, domain/layout fingerprints, and complete reflected numeric and
     resource layout;
   - the bounded renderer-owned material program-layout registry admits only
     backend-compatible shader artifacts, retains the full canonical reflected
     payload, returns monotonic stable compact ids, and compares the full digest
     and payload before reuse;
   - focused proofs cover cross-artifact mismatch rejection, canonical payload
     collision dimensions, stable repeated ids, capacity, backend rejection, and
     zero RHI initialization.
3. **3A.3 — service and adversarial closure proof (complete)**
   - `ResourceStreamingService` owns VSHADER, VPPL, and VMAT decoder state,
     registers them in dependency order, and unregisters them in reverse before
     texture/mesh loaders and the streamer;
   - initialization tracks each successful registration, so an injected VPPL
     conflict removes VSHADER and every lower loader before host rollback can
     destroy callback state;
   - the material closure proof covers loose/VPAK byte parity, shared-operation
     coalescing and priority promotion, one-caller cancellation, exact
     Required/Optional/Soft behavior, loose-to-VPAK generation replacement,
     corrupt dependency metadata, corrupt VMAT payload bytes, and clean drain;
   - lower `resourcePipelineTests` and `streamingTests` remain the focused
     authority for transitive priority/cancellation mechanics, last-interest
     cancellation, and stored VPAK-segment corruption. The service proof does
     not duplicate those generic state machines.

Phase 3A's CPU closure and runtime layout identity are sealed. The completed 3B
study below defines parameter storage, typed resource resolution,
transactional `GpuMaterial` publication, and rollback. No draw is part of
either phase.

## Phase 3B: Bindless Resolution and Atomic GPU Materialization

Status: **complete and sealed**. Phase 3C.1 through 3C.4 are also complete;
Phase 3 is frozen.

Phase 3B takes an already loaded and validated `MaterialResourceObject`. It
resolves that immutable CPU closure into retained renderer-side resource
references, immutable parameter bytes, and one `GpuMaterialHandle`. It does not
request the root VMAT, create native material techniques, bind a Render Scene
consumer, replace a live material generation, or draw. Those service and
lifecycle policies remain 3C.

### Evidence and correction found by the study

The existing runtime already provides most of the transaction machinery, but
the scan found four concrete gaps:

| Finding | Repository evidence | Consequence |
| --- | --- | --- |
| Texture identity is ready for material use | `TextureResidencyRuntime` coalesces an exact resource path and generation, returns a stable `GpuTextureResidencyHandle`, retains demand, and exposes `BindlessReady` only after the shared texture-table publication is accepted | Texture roles must retain `TextureDemandHandle` and store the stable residency index, never the current physical descriptor |
| The shared publication path already exists | `GpuSceneRuntime::StageContribution` merges bounded producer requests with Render Scene updates and submits one `GpuSceneUploader` batch | 3B.1 made material publication another contribution and removed the duplicate immediate material-publication API |
| Parameter storage was absent | `GpuMaterial` had offsets but no backing table | 3B.1 added the paged `GpuMaterialParameterWord` table and fence-safe allocation through the existing GPU Scene lifetime owner |
| Descriptor and runtime type ownership were incomplete | The renderer lacked a Samplers domain and roles retained only opaque type fingerprints | 3B.1 added renderer-owned sampler-domain/layout-registry lifetime and canonical reconstructable reflected shapes without introducing a second type authority |

The last item is a necessary narrow correction at the Phase 2/3 boundary. The
full type fingerprint remains the compatibility authority, but the current
VSHADER material role also needs canonical, reconstructable runtime shape:

- texture dimension, arrayed/multisampled form, sampled scalar class and
  component count, and read/write access;
- filtering versus comparison sampler;
- typed, structured, or byte-address buffer form, read/write access, typed
  scalar/component form, and reflected structured-element stride;
- acceleration-structure family and required capability.

The shader compiler derives this record from the same Slang reflection object
that produces the type fingerprint. The record participates in the material
layout fingerprint and exact registry comparison. VMAT does not gain a second
manual type authority: it continues to identify the role slot and expected
asset type, while its retained VSHADER/layout record supplies the shape.

This is an early-development clean cut. Existing VSHADER/DDC/package outputs
are disposable and are rebuilt. No legacy reader, migration path, dual schema,
or compatibility adapter is added.

### GPU parameter storage

Numeric bytes use a new `GpuMaterialParameterWord` GPU Scene table whose element
is one `u32`. A material parameter range is a normal `GpuSceneLifetime` range:

```text
MaterialFile::GetParameterData()
  -> zero-padded u32 words
  -> GpuSceneTableKind::MaterialParameterWord allocation
  -> GpuMaterial.parameterByteOffset = allocation.first * 4
  -> GpuMaterial.parameterByteSize   = exact reflected byte size
```

The table is paged like the other GPU Scene tables. Generated access remains
layout-baked: `VanguardLoadMaterialParameterWord(localByteOffset)` adds the
material's logical byte offset, resolves the relevant parameter-word page, and
loads the requested four-byte window. A window ending at the exact material
byte size is zero-extended; it never reads the next material's range. This
preserves existing unaligned 16-bit loads without requiring a monolithic raw
buffer or per-material constant buffer.

V1 performs no live compaction. Allocation is bounded through the existing GPU
Scene range owner, ranges never move while referenced, and a retired range
returns only after the GPU Scene retirement fences complete. Fragmentation or
page/capacity exhaustion is explicit backpressure, not a reason to relocate
offsets visible to in-flight shaders. Parameter pages grow only within the
configured GPU Scene page cap and use the existing shared staging-segment
budget.

Adding the table advances the CPU/HLSL GPU Scene layout contract together. The
new table is appended after existing table ids; existing ids do not shift.

### Typed resolver and retained-reference boundary

The material system must not learn how every future asset family creates GPU
objects. 3B introduces a bounded renderer-owned resolver registry keyed by
`(ResourceParameterKind, expected ResourceTypeId)`. A registration supplies
main-thread begin/query/cancel/release callbacks and accepts the exact reflected
resource shape. It returns a generational operation token and eventually one
move-only retained reference. Registrations are device-lifetime state: they remain
index-stable until resolver shutdown. There is no mutable unregister path that
could invalidate provider indices held by pending operations or references.

The production texture adapter is concrete: it validates VTEX metadata against
the reflected shape, calls `TextureResidencyRuntime::RequestTexture`, retains
the resulting `TextureDemandHandle`, and reports ready only when the exact
residency is `BindlessReady`.

Buffer, sampler, and acceleration-structure asset formats do not currently
exist in Vanguard. 3B therefore does not invent material-private file formats
for them. Their owning future resource modules register adapters through the
same boundary. The material implementation and its adversarial tests still
exercise every declared family with real RHI objects and references, proving that
the material path is complete without pretending those absent asset formats
already exist.

For a ready non-texture resource, the central material descriptor cache—not the
asset adapter—allocates and populates one immutable descriptor in the proper
global domain:

- buffer and acceleration-structure descriptors use the Resources domain;
- sampler descriptors use the Samplers domain;
- the exact binding/view kind comes from the reflected shape and the provider's
  physical object;
- cache identity includes the complete CPU/RHI generation, exact view, and
  descriptor domain; a descriptor generation is retained on the CPU and never
  copied into the GPU record;
- lookup uses a bounded hash index and still compares the complete identity on
  every candidate, so neither linear cache walks nor truncated-hash acceptance
  are part of the contract;
- a last descriptor reference retires through real graphics/compute/copy fences.

`GpuMaterialResource` retains its existing interpretation:

| Role | `resource` | `samplerDescriptor` |
| --- | --- | --- |
| Texture | stable `GpuTextureResidencyHandle::index` | invalid |
| Buffer | immutable Resources-domain descriptor index | invalid |
| Sampler | invalid | immutable Samplers-domain descriptor index |
| Acceleration structure | immutable Resources-domain descriptor index | invalid |

The role's family selects the interpretation. An entry is never published with
an allocated-but-unpopulated descriptor or a texture identity whose initial
installation is still pending.

### Required, Optional, Soft, and fallback rules

The CPU dependency rules from 3A remain unchanged. GPU resolution applies them
as follows:

| Kind | 3B behavior |
| --- | --- |
| Required | The exact retained dependency generation must resolve and become ready. Failure fails this materialization transaction; no `GpuMaterialHandle` is exposed |
| Optional with a loaded handle | Resolve the exact retained generation. A provider/readiness failure may select the registered typed fallback and remains visible in diagnostics |
| Optional without a loaded handle | Select the registered typed fallback immediately |
| Soft | Do not add a hidden ResourcePipeline request in 3B. Select the typed fallback; a later explicit soft upgrade/replacement is 3C/Phase 4 policy |

A fallback is a real retained resource or sampler of the exact reflected shape.
Vanguard does not use descriptor index zero, `InvalidGpuDescriptorIndex`, or a
backend-dependent null descriptor as a universal fallback. Read-only fallbacks
may be renderer-global and shared. A writable role may use only a provider-
supplied private typed fallback reference; if none exists, missing Optional/Soft
resolution fails cleanly. Shared writable dummy UAVs are rejected because they
would introduce cross-material side effects and data races.

Fallback creation is capability checked. Examples are minimal shape-correct
zero textures/buffers, filtering or comparison samplers, and an empty
acceleration structure on a ray-tracing-capable device. A role for which the
active backend cannot provide either the requested resource or a legal fallback
is not publishable. The system reports that fact; it does not weaken the shader
contract.

### Canonical resolved-material identity

GPU coalescing is based on the exact resolved GPU image, not merely the VMAT
path or its content fingerprint. The 256-bit `GpuSceneDefinitionKey` is the
SHA-256 digest of a domain separator plus:

```text
full material-layout fingerprint
exact parameterByteSize and parameter bytes
resource count
for every dense role slot:
    reflected family and complete type fingerprint/shape
    resolved stable CPU identity
        texture residency index + generation, or
        descriptor index + descriptor generation + domain
    GPU resource flags
```

Padding outside `parameterByteSize`, transient request ids, descriptor-cache
reference counts, resource paths, and Required/Optional/Soft provenance are not
part of the key. They do not change shader-visible behavior. Distinct callers
may therefore share one GPU definition while retaining their own CPU closure
and retained-reference bundle. Equal keys are produced only after canonical payload
construction; the existing definition owner continues to treat equality as the
producer's byte-identity promise.

Including CPU generations in resolved identities prevents descriptor or stable
table-index ABA. Two reimports do not alias merely because a recycled GPU index
has the same 32-bit value.

### Atomic material publication

`GpuSceneDefinitions` is extended with a deferred material-publication batch
parallel to the established texture contribution contract. The definition now
owns one compound allocation:

```text
GpuMaterial slot
GpuMaterialResource range (possibly empty)
GpuMaterialParameterWord range (possibly empty)
```

The materializer executes this ordered transaction on the renderer-owning
thread:

1. retain the root `MaterialResourceObject` and register/find its exact
   `MaterialProgramLayoutId`;
2. begin or join every role-resolution operation and retain successful references;
3. wait by polling—not blocking—until Required and loaded Optional roles are
   ready, then choose all legal fallbacks;
4. build the final resource entries, parameter words, and canonical key;
5. prepare one bounded deferred `GpuSceneDefinitions` material batch. Reused
   definitions acquire references; new definitions reserve all three ranges;
6. stage its upload requests as one `GpuSceneRuntime` contribution;
7. write the material row, resource range, and parameter words directly into
   the assigned shared upload reservations;
8. after the shared submission succeeds, the infallible accept callback commits
   the definition records and only then publishes `GpuMaterialHandle` to the
   caller-visible result.

All caller-correctable validation and capacity checks occur before shared
submission. Before submission, cancellation or failure releases references,
reverses reused references, and cancels every unpublished allocation. A
transient shared-uploader failure returns the frozen batch to its bounded retry
queue without rebuilding or leaking it. After submission, accept is infallible;
a cancellation observed in that window accepts and immediately queues the
complete definition for retirement rather than exposing half state.

A reused-only batch needs no upload and may commit after all retained references are
installed. New material definitions never use the immediate, privately
submitting material path. One frame may publish scene mutations,
texture installations, and many materials in the same command list and fence.
No material owns a command list, submission, wait, descriptor set, staging
buffer, or fence.

### Lifetime, retirement, and threading

One successful materialization reference owns:

- its strong VMAT root handle and therefore the exact 3A CPU closure;
- one role reference per resource slot, including fallbacks;
- one reference to the content-addressed `GpuMaterial` definition.

Releasing the material reference retires the GPU Scene compound and moves the
root/role references into a bounded material retirement record. Those references are
not dropped when the CPU handle disappears. They remain alive through the same
renderer graphics/compute/copy cutover fences that protect the material table,
then release into their owning texture/descriptor/provider retirement paths.
The current `GpuSceneLifetime` epoch remains the authority for table-range
reuse; provider/descriptor owners remain the authority for physical-resource
reuse.

All transaction mutation, layout registration, descriptor allocation, GPU
Scene preparation, contribution staging, acceptance, and retirement sealing
run on the renderer-owning main thread. Resource providers may perform bounded
worker or GPU work internally, but 3B observes them only through non-blocking
tokens. No generic resource decoder calls RHI and no material tick waits for a
job or fence.

`RenderingService` ultimately owns, in dependency order, both descriptor
domains, GPU Scene, `MaterialProgramLayoutRegistry`, texture residency, the
resolver/descriptor/fallback owners, and the materializer. Shutdown is the
reverse: drain/release material transactions and retirement records before
texture residency, layout records, GPU Scene, or either descriptor domain can
disappear. Full service exposure and asynchronous failure routing are completed
in 3C; 3B proves the low-level owner directly.

### Unreal sanity check

The design keeps the useful Unreal separation but not its object model.
Unreal's `FMaterialRenderProxy` evaluates and caches uniform expressions on the
render side, and `FUniformExpressionCache` owns the resulting uniform buffer and
retained virtual-texture state. `FUniformExpressionSet` carries typed texture
categories, default parameter data, the uniform-buffer layout, and the routine
that fills that exact layout. That confirms three relevant rules: cooked
program state is distinct from render-ready values, those values are cached
rather than rediscovered per draw, and defaults must retain enough type/layout
information to be legal.

Unreal's public descriptor-range documentation also notes that descriptor view
types cannot always be mixed freely on every platform. That supports
Vanguard's separate Resources/Samplers domains and rejects the idea that one
opaque digest or universal null descriptor is sufficient. Unreal can fall back
to a complete default material when a shader map is incomplete; Vanguard keeps
that whole-material policy for 3C and uses only exact typed role fallbacks in
the 3B materialization transaction.

Vanguard deliberately does not copy one render proxy, uniform buffer, or
descriptor range per material. Its equivalent cached result is one immutable,
content-addressed GPU Scene compound whose parameter/resource data is reached
through global paged tables.

### Rejected designs

3B explicitly rejects:

- treating `MaterialFile::GetParameterData()` as though it already had a valid
  GPU address;
- a relocatable compacting arena that patches live material offsets;
- one constant buffer, descriptor set/range, uploader, command list, submission,
  or fence per material;
- publishing the stable texture index before its initial residency-table entry
  is accepted;
- storing a texture's current physical descriptor in `GpuMaterialResource`;
- letting asset-specific providers allocate from renderer-global descriptor
  domains independently;
- using invalid/zero/untyped null descriptors as cross-backend fallbacks;
- sharing writable dummy fallback resources between unrelated materials;
- loading Soft references implicitly while constructing the initial 3B image;
- keying a GPU material by VMAT path/content alone, or by descriptor indices
  without their CPU generations;
- adding material-private buffer, sampler, or acceleration-structure cooked
  formats merely to make a test appear production-complete;
- retaining an immediate privately submitting material API as a second
  production material-publication path.

### 3B implementation slices and completion proofs

1. **3B.1 — runtime role shape and GPU storage foundations (complete)**
   - reflect and serialize the compact canonical resource shape, include it in
     layout identity/validation, rebuild current artifacts, and add negative
     shape-tamper proofs;
   - add the global Samplers descriptor domain and integrate the existing
     `MaterialProgramLayoutRegistry` into renderer ownership;
   - append `GpuMaterialParameterWord` to CPU/HLSL GPU Scene tables, advance the
     layout contract, and prove exact/unaligned/cross-page reads, zero tail,
     capacity, fragmentation, and fence-delayed reuse;
   - extend material definitions with parameter bytes and a deferred compound
     batch that can be staged into `GpuSceneRuntime` without a private submit.
2. **3B.2 — typed role resolution and retained references (complete)**
   - implemented the bounded provider registry, production VTEX adapter,
     central resource and sampler descriptor caches, exact typed fallbacks, and
     move-only retirement-safe references;
   - proved production VTEX readiness gating, typed/structured/raw buffer
     descriptors, filtering/comparison sampler descriptors, stable generic
     texture-residency identities, acceleration-structure capability rejection,
     Optional/Soft fallback, Required failure, private writable fallback or
     clean rejection, descriptor exhaustion, and generation-safe reuse;
   - the 3B.2.1 correction makes provider registrations device-lifetime stable,
     removes unused fallback/writable GPU flags, indexes full-fingerprint layout
     and descriptor lookup with collision checks, and rejects Texture3D arrays
     plus non-Texture2D multisampling before runtime publication. A successful
     acceleration-structure descriptor remains a 3B.4 backend proof on a
     ray-tracing-capable test device, not a claim made by the current D3D12
     fixture.
3. **3B.3 — atomic materialization transaction (complete)**
   - `MaterialMaterializer` is the single bounded coordinator from a retained,
     validated VMAT closure through layout registration, typed role resolution,
     canonical resolved-image hashing, and the deferred definition batch added
     by 3B.1. It does not add another descriptor owner, uploader, command list,
     submission, fence, or immediate material-publication path;
   - caller results remain publication-pending until accepted handles are
     consumed. Prepared cancellation reverses reused references and compound
     allocations; cancellation after staging accepts and immediately releases
     the unseen definition; a successful move-only reference retires its
     definition and every exact role reference through complete graphics/compute/copy
     cutover fences;
   - the D3D12 integration proof covers equal-image reuse, parameter-byte and
     dependency-generation non-aliasing, mixed reused/new batches, both sides of
     the staging boundary, shared-runtime publication, and zero materializer work
     after retirement. The production VMAT adapter and layout validation remain
     covered independently by the 3A material-resource proof; 3B.4 joins them in
     the final readback/fault matrix.
4. **3B.4 — adversarial and cross-backend closure proof (complete)**
   - one D3D12 GPU Scene publication now joins six material requests, a pending
     texture installation, and a real scene-light mutation. Runtime counters
     prove one submitted batch accepted both external contributions while the
     scene publisher wrote through the same command path;
   - the proof reads back the accepted `GpuMaterial`, its exact
     `GpuMaterialResource`, and both words of a seven-byte parameter image. It
     covers equal-image coalescing, changed-parameter and changed-generation
     non-aliasing, the zero-padded tail word, operation-capacity exhaustion and
     immediate recovery, and retirement through real graphics/compute/copy
     fences;
   - `gpu_scene_types.hlsli` owns the canonical paged material-row and
     resource-role loaders beside the existing unaligned parameter-word loader.
     `materialToolsTests` compiles that actual include and an exercising entry
     point to both DXIL and SPIR-V, so the generated domain loaders have one
     portable runtime primitive contract rather than fixture-only stubs;
   - the existing D3D12 resolver matrix remains the live descriptor-family
     proof for textures, typed/structured/raw buffers, filtering/comparison
     samplers, writable isolation, fallbacks, exhaustion, and generation-safe
     reuse. There is no Vulkan RHI implementation in this repository, so 3B.4
     makes no false live-Vulkan or acceleration-structure-device claim: Vulkan
     is covered by real SPIR-V compilation and frozen layout identity until a
     Vulkan device backend exists;
   - the joined geometry/material proof reaches zero materializer work, demand,
     descriptor-cache, texture-residency, definition, GPU Scene lifetime, and
     pending-retirement state at shutdown. Lower owner tests remain authoritative
     for their table/upload/descriptor fault injectors instead of duplicating
     those state machines in the materializer.

Phase 3B now produces a low-level ready `GpuMaterialHandle` from an already
loaded CPU closure. It still does not make that handle visible to mesh/decal
consumers or choose native technique fallback. The sealed Phase 3C design below
owns those lifecycle decisions.

## Phase 3C: Lifecycle Integration and Final Runtime Proof

Status: **complete and frozen**. 3C.1 through 3C.4 are implemented and proven;
Phase 3 stops at the headless, no-draw runtime boundary.

Phase 3C composes the completed 3A CPU closure and 3B materializer into one
bounded renderer lifecycle. It decides when a material is safe to expose to a
consumer, keeps the active material alive while a replacement is incomplete,
and retires the displaced material only after both scene publication and the
renderer queue cutover are proven. It does not add another loader, descriptor
cache, GPU material allocator, scene database, pipeline cache, or submission
path.

### Evidence and gaps in the existing runtime

The accepted design follows the authorities already present in the repository:

- `ResourcePipeline` owns loading, dependency fan-in, cancellation, and exact
  immutable resource generations. Scene proxies already retain the optional
  `ResourceHandle` supplied by their producer;
- `MaterialMaterializer` owns the only transaction from one validated VMAT
  closure to an accepted `GpuMaterialReference`;
- `RenderShader`, `RequestRenderPipeline`, and `PipelineCache` already own native
  shader creation, reflected pipeline validation, and coalesced asynchronous PSO
  creation;
- `RenderSceneManager` owns logical proxy resources, while
  `RenderSceneGpuPublisher` owns the persistent GPU instance/decal rows and the
  binding methods which write bare GPU Scene handles;
- `RenderingService` already provides the serialized CPU-drain, contribution-
  resolution, shared GPU Scene publication, failure-consumption, and retirement-
  collection boundaries.

The scan also found four real integration gaps:

1. `RenderingService` does not yet own the material resolver/materializer,
   material residency records, native program cache, or the existing
   `PipelineCache`;
2. a logical mesh/decal material change currently clears its GPU binding
   immediately. That is legal for absence, but wrong for replacement because it
   discards the last valid resident before the candidate can succeed;
3. `RenderSceneGpuPublisher` retains only bare indices and has no per-binding
   acceptance receipt. An outside owner therefore cannot prove when it may drop
   the old owning reference;
4. the renderer has per-owner `SealRetirements` APIs but no frame-level queue
   cutover coordinator yet. `FlushPreviousFrameProcessing` is only a CPU join and
   must never be treated as GPU fence evidence.

These gaps require one lifecycle coordinator and one narrow scene-publication
acknowledgment. They do not justify parallel material or scene machinery.

### Accepted ownership and identity

`MaterialResidencyRuntime` is the renderer-owned coordinator. A request accepts
an already loaded `MaterialResourceObject` handle; it never calls
`ResourcePipeline::Request` itself. This preserves the existing rule that world
or resource-streaming ownership decides what is loaded, while renderer residency
decides what becomes GPU-visible.

Each request returns a move-only `MaterialDemandHandle`. Equal demands coalesce
only when resource path and registry generation both match. A residency record
owns:

- the exact strong VMAT root and therefore the complete 3A closure;
- the 3B materialization ticket or accepted `GpuMaterialReference`;
- demand count, last failure, progress state, and bounded diagnostics.

Native-program references are held by outstanding technique requests, not by every
resident material. This keeps data-only residency independent of speculative PSO
creation while the strong VMAT closure still retains every exact shader/pipeline
resource needed by a later request.

The accepted `GpuMaterialReference` remains owned by the residency record.
Consumers receive a generational `MaterialResidencyHandle` and may query its
current bare `GpuMaterialHandle`; they never take the move-only 3B owner. This
lets mesh slots, decals, and future `GpuMaterialSet` construction share one
resident definition without duplicating its references.

`ResourcePipeline` remains the only CPU-load state machine. Consequently the
public residency states are deliberately smaller than the conceptual pipeline:

```text
Invalid
  -> Resolving          exact CPU closure retained; layout/resource roles and 3B image progressing
  -> PublicationPending frozen or staged in the shared GPU Scene batch
  -> Resident           accepted GpuMaterialReference may be exposed
  -> Retiring           accepted owner awaits a complete queue cutover
  -> Invalid            retirement collected and slot generation advanced

Resolving/PublicationPending -> Failed
```

`MaterialMaterializer::Begin` either admits the exact closure synchronously or
the public request fails, and cancellation clears the materializer ticket
synchronously even when its frozen batch completes rollback later. Therefore
`Requested` and `Cancelling` are not public query states; exposing them would be
dead API rather than useful lifecycle information.

CPU-ready and non-Soft dependency-ready are preconditions established by the
published 3A root, not duplicate residency states. `Replacing` is a consumer-
binding relationship containing an active record and a candidate record; it is
not another state of either immutable residency record. This avoids a bloated
cross-product state machine.

### Native program and technique readiness

GPU material residency and native PSO readiness are related but not identical.
A VMAT can become `Resident` once its 3B compound is accepted. Its techniques
may still require a concrete render-target/depth/sample attachment signature,
which does not exist until a future render phase requests that technique.
Therefore 3C must not block `GpuMaterial` publication on every possible PSO and
must not invent an attachment signature.

The runtime owns one bounded native-program cache keyed by exact
`ShaderResourceObject` generation. It creates one `RenderShader` per retained
generation on the renderer-owning thread and shares it across material and
pipeline requests. A technique request identifies a resident material, cooked
technique name, concrete attachment signature when the VPPL defers attachments,
and priority. It resolves only the shader handles already retained by that exact
`PipelineResourceObject`, then calls the existing `RequestRenderPipeline`; the
existing `PipelineCache` remains the sole PSO cache.

The result returned to future draw construction is atomic: requested material
handle plus ready compatible pipeline, an explicit compatible fallback pair, or
`Unavailable`. Pending/failed pipeline creation never mutates the resident
material record. Vanguard does not yet own a universal default material catalog,
so 3C does not synthesize a hidden default. Replacement failure keeps the last
valid pair. Initial failure remains inactive unless the caller supplies a pinned,
domain/technique-compatible fallback; otherwise the future draw is skipped with
a diagnostic. This is safer than binding a layout-incompatible global material.

### Consumer binding and atomic replacement

A small `MaterialSceneBindingBridge` attaches to `RenderSceneManager` and
observes exact material-handle changes for live decal proxies. It owns those
consumer demands; it does not load assets or own GPU definitions. Mesh material
demands remain with the renderable/material-set assembly owner because only that
owner can publish and acknowledge the complete mesh binding atomically.

For an initial binding, the proxy remains unbound/inactive until the candidate
is resident. For replacement, the bridge retains both active and candidate:

1. the logical scene update changes the desired exact CPU handle without
   clearing the accepted GPU binding, and the lifecycle integration starts or
   joins its candidate demand;
2. the current GPU binding is left untouched while the candidate resolves;
3. after the candidate becomes resident, the bridge calls the existing scene
   publisher binding method and receives a generational binding receipt;
4. only after that receipt is accepted by a successful shared GPU Scene
   publication does the candidate become active and the old demand become
   retireable;
5. the old `GpuMaterialReference` is retired only with a later complete
   graphics/compute/copy cutover supplied by the normal renderer submission
   boundary.

Cancellation or failure at steps 1--3 removes only the candidate. A cancelled
GPU Scene publication preserves the dirty binding for retry and cannot advance
the receipt. Proxy destruction cancels its candidate and releases its active
demand, but physical reuse still waits for the queue cutover.

The scene acknowledgment is intentionally narrow: a binding call returns a
`RenderSceneGpuBindingReceipt`, and polling reports pending, accepted, or stale.
The publisher stores a monotonically increasing binding revision in the existing
tracked proxy and copies the frozen revision into its existing publication. No
second mutation queue or shadow scene is added.

Mesh primitive defaults consume ordinary `MaterialDemandHandle`s while a
renderable definition is assembled. Decals use the direct scene binding bridge.
The current single mesh-proxy material field may later feed a
`GpuMaterialSet`, but Phase 3C does not implement per-instance material arrays or
inheritance. Phase 4 owns `GpuMaterialSet` override construction.

### Replacement and hot-reload boundary

Atomic replacement in 3C means changing a consumer from one exact loaded VMAT
root to another while the old resident remains usable. This includes different
paths and any exact generations already supplied by the resource system.

The current `ResourceRegistry` does not publish two generations of the same key
while strong handles retain the first. Because a resident material deliberately
retains that handle, 3C cannot honestly implement overlapping same-path live
recook. Phase 4 owns the resource-generation handoff needed for true hot reload.
The 3C proof therefore exercises last-valid replacement, cancellation, and
failure with distinct exact roots; it does not label that test same-path hot
reload.

### Tick, failure, retirement, and shutdown policy

All lifecycle mutation is main-thread owned. The existing renderer frame order
is extended as follows:

1. flush the previous CPU rendering chain;
2. resolve shared GPU Scene contribution outcomes and consume asynchronous
   command/publication failures;
3. collect completed GPU Scene and residency retirements;
4. progress texture residency, then material residency/materializer polling, so
   a texture made bindless-ready this update may unblock a material;
5. accept ready material references and progress pending scene binding receipts;
6. immediately before `FrameTick`, stage the existing texture contribution;
   material batches already use the same `GpuSceneRuntime` contribution queue;
7. `FrameTick` publishes material, texture, and scene-row changes in the one
   shared batch.

Progress is bounded by configured residency records, demands, state checks,
native programs, technique requests, and the existing 3B batch limits. Stats
separate demands, coalesces, resolving/publication-pending/resident/failed/
retiring records, active/candidate scene bindings, accepted replacements,
fallbacks, skipped techniques, and first asynchronous failure evidence.

Failures are fail-closed and generation-specific. Initial material failure leaves
the consumer inactive. Candidate failure preserves the active resident. A GPU
Scene or native pipeline failure is surfaced through the renderer's existing
asynchronous failure channel after its CPU tail is joined; it is not converted
to success by silently selecting a fallback.

Normal retirement accepts only real renderer graphics/compute/copy fence
evidence. The future frame renderer calls the existing per-owner retirement
seals from one common frame cutover point; 3C adds material residency to that
fan-out but does not fabricate empty command submissions. Normal quiesce requires
zero scenes, material demands, candidates, native-program references, materializer
operations, and unsealed retirements before dependency owners shut down.

Shutdown order after quiesce is:

1. material scene bridge;
2. material residency and materializer;
3. material resource resolver and native program/PSO cache;
4. texture and mesh residency;
5. material layout registry and GPU Scene;
6. descriptor domains and RHI.

Device loss is terminal for the current renderer service. New demands and
bindings stop, no lost fence is waited upon, and no handle is recycled for a
replacement device. A dedicated device-abandon path drops material operations,
scene bindings, provider references, descriptor caches, and GPU definitions in
dependency order while retaining only CPU resource handles until service
destruction. Device recreation, replay, and recovery policy are outside Phase 3;
ordinary cancellation/retirement APIs must not masquerade as device loss.

### Unreal sanity check

The design preserves the useful Unreal boundaries without importing its object
model. Unreal's `FMaterialRenderProxy` is a render resource with cached uniform
expressions and an invalidation serial; its cache records the shader map used to
produce those values. `GetMaterialWithFallback` returns a material with a complete
shader map, and render-resource initialization/release is marshalled across the
render thread. Unreal's PSO cache is separately ticked and may precompile in
batches. These facts support four Vanguard choices: retain render-ready values,
make replacement observable, keep incomplete candidates away from consumers,
and separate material-data residency from concrete PSO readiness.

Vanguard does not copy the render-proxy class hierarchy, per-material uniform
buffers, UObject lifetime, implicit global default, or a second material shader
cache. Its resident value is the existing immutable GPU Scene compound, and its
native PSOs remain in the existing `PipelineCache`.

### Rejected designs

3C explicitly rejects:

- requesting VMAT or dependency resources from inside renderer residency;
- one residency/materialization/native shader/PSO record per scene proxy;
- clearing the last valid GPU binding when a replacement request merely begins;
- overwriting a live `GpuMaterial` compound in place;
- dropping an owning material reference when a bare GPU Scene index is written;
- treating CPU-chain completion as GPU queue retirement evidence;
- compiling every attachment-dependent technique before a material may be
  resident;
- adding another PSO cache beside `PipelineCache`;
- silently using a layout-incompatible universal default material;
- claiming same-path live hot reload while the resource registry forbids
  overlapping strong generations;
- adding material instances, `GpuMaterialSet` overrides, draw submission,
  compiler/editor objects, or legacy cooked-data compatibility.

### 3C implementation slices and completion proofs

1. **3C.1 -- renderer-owned material residency and technique admission**
   - **complete**;
   - implement bounded generational residency/demand records over the existing
     materializer, exact-root coalescing, cancellation/failure, shared native
     program references, and lazy technique requests through the existing pipeline
     factory/cache;
   - prove resident data is independent of attachment-deferred PSO readiness,
     exact shader generations share correctly, incompatible techniques fail
     closed, and all first-load cancellation/capacity paths return to zero;
2. **3C.2 -- scene binding and atomic replacement**
   - **complete**;
   - add the narrow tracked-decal binding receipt and material binding bridge;
   - correct eager clear-on-change behavior, then prove initial decal binding,
     distinct-root replacement, candidate cancellation/failure preserving the
     active material, logical-change preservation of the accepted binding,
     publication retry, proxy destruction, non-starving failure progress, and
     receipt/generation staleness;
3. **3C.3 -- service, cutover, and terminal lifecycle integration**
   - **complete**;
   - **3C.3.1 -- service ownership, progress, cutover, and normal shutdown is complete**;
   - compose resolver, materializer, residency, native program cache, existing
     pipeline cache, and scene bridge into `RenderingService` in dependency
     order; wire bounded frame progress and asynchronous diagnostics;
   - add material retirement to the real renderer cutover fan-out, enforce
     quiesce/shutdown invariants, and prove partial-init rollback, normal drain,
     and missing-fence rejection;
   - **3C.3.2 -- terminal device-abandon is complete**: explicit terminal
     invalidation covers GPU Scene, mesh, texture, material-resource,
     materializer, residency, pipeline, and scene-binding owners. Outstanding
     move-only handles become harmlessly stale and native resources are dropped
     without fabricated fences or a successful `WaitIdle`;
4. **3C.4 -- production vertical and adversarial Phase 3 freeze**
   - **complete**;
   - load real loose and indexed-VPAK closures through `ResourcePipeline`,
     materialize and bind them through the service, request a concrete native
     technique, and read back the exact accepted material/resource/parameter
     data without a draw;
   - cover coalescing, backpressure, distinct-root replacement, cancellation,
     Required failure, Optional fallback, pipeline pending/failure, publication
     retry, real three-queue retirement fences, and clean zero-state shutdown;
   - rerun the lower resource, shader, pipeline, material, RHI, rendering, and
     engine suites. Do not duplicate their generic corruption/fault matrices in
     the vertical test.

Phase 3C ends when a production-loaded material can be retained, published,
bound, technique-resolved, replaced, and retired through the renderer service
without exposing partial state or leaking ownership. It still performs no draw.

### 3C.1 implementation evidence

`MaterialResidencyRuntime` now owns bounded generational material residencies,
move-only coalesced demands, lazy technique requests, and a bounded exact-
generation native-program cache over the existing `MaterialMaterializer`,
`RenderShader`, `RequestRenderPipeline`, and `PipelineCache`. It accepts only an
already-loaded `MaterialResourceObject`; it neither requests resources nor
records/submits rendering work. Resident `GpuMaterialHandle`s remain independent
from attachment-deferred pipeline readiness, and an accepted reference enters
`Retiring` after its final demand/technique interest disappears until a complete
graphics/compute/copy cutover is explicitly supplied.

The D3D12 proof constructs and decodes a real VSHADER/VPPL/VMAT closure, then
proves pre-publication cancellation, exact-root coalescing, demand and residency
capacity rejection, publication through the shared GPU Scene transaction,
attachment-deferred failure without loss of residency, unknown-technique
rejection, exact native-shader sharing, existing-PSO-cache coalescing, clean
technique release, missing-fence rejection, and complete three-queue retirement.
The proof performs no draw. `RenderingService` composition and frame-level fence
fan-out remain 3C.3.

### 3C.1.1 runtime cost correction

The lifecycle contract is unchanged, but its recurring cost is now proportional
to live work rather than slot-array high-water marks. Material residencies and
native shader programs use path-hash heads with collision chains that still
verify exact resource path, generation, and object identity. Resolving and
retiring residencies, resolving materializer operations, and publication-ready
operations live in explicit dense work sets. Per-update residency checks,
materializer operation checks, resource-role polls, publication batches, and
per-seal retirements all have independent configured bounds.

VMAT resource parameters are already sealed as dense reflection-ordered slots,
so materialization admits them in one linear pass instead of searching the full
parameter list for every role. Loaded dependencies retain their canonical
path/type ordering and are found with binary search. The default safety profile
accepts up to 256 resource roles per material; callers may deliberately raise
that read/materialization limit, but doing so is an explicit memory and progress-
budget decision rather than an accidental 16K-role default.

The 3C.1 proof asserts lookup-probe, state-check, and retirement-check counters.
These deterministic counters guard the intended complexity without relying on
machine-specific wall-clock thresholds. No new handle, cache owner, scheduler,
or material execution path was introduced.

### 3C.2 implementation evidence

`RenderSceneGpuPublisher` now returns a narrow generational
`RenderSceneGpuBindingReceipt` for decal binding changes. Each tracked decal owns
one monotonically increasing binding revision and one accepted
revision. A successful shared GPU Scene publication advances acceptance; a
canceled publication leaves the receipt pending for retry; a later binding or a
destroyed proxy makes the old receipt stale. No second scene database, mutation
queue, or shadow publication image was added.

`MaterialSceneBindingBridge` is keyed directly by the existing generational
`RenderProxyHandle`. Each bounded decal record owns at most one active and one
candidate `MaterialDemandHandle`. Candidate equality checks the resource object,
path, and generation rather than accepting a coincidentally equal key from another
registry. Candidates become active only after the matching scene receipt is
accepted. Explicit clearing likewise retains the old demand until the clear is
accepted. Candidate cancellation or failure preserves the active demand, and
proxy destruction drops both interests. Mesh-default demand and commit APIs were
removed: the future renderable/material-set assembly owner must retain and publish
that complete mesh binding instead of splitting ownership across two partial
systems.

The D3D12 vertical proof uses production-decoded VMAT roots, the real materializer,
residency runtime, Render Scene manager, shared GPU Scene publisher, and command
frame transaction. It proves initial decal activation, distinct-root replacement,
pre-publication cancellation, invalid candidate rejection with last-valid
preservation, a logical component material change that leaves the accepted GPU
binding intact, canceled-publication retry, superseded-receipt rejection and
active restoration, continued bounded progress after a record-local failure,
accepted clearing, proxy-destruction cleanup, bounded retirement, and real
three-queue cutover. The proof ends with zero bridge and residency records and
performs no draw.

### 3C.3.1 implementation evidence

`RenderingService` now owns and initializes the material resource resolver,
materializer, shared pipeline cache, residency runtime, and scene-binding bridge
after their descriptor, GPU Scene, layout, mesh, and texture dependencies. Its
bounded RenderUpdate participant collects GPU Scene, mesh, and texture
retirements, progresses texture and material residency, then progresses material
scene bindings. Failures remain visible through the existing frame-participant
failure channel; no scheduler, submission owner, scene database, or PSO cache was
added.

One renderer-owned `SealResidencyRetirements` cutover fans the same real
graphics/compute/copy fence set to material, texture, mesh, and GPU Scene owners.
It rejects incomplete cutovers before mutating any owner. Normal quiesce requires
zero bindings, demands, residencies, techniques, materializer operations,
resource references, and descriptor-cache entries. Shutdown and failed startup
unwind the material chain in reverse dependency order after an idle wait. The
engine proofs cover the device-disabled path, complete ownership startup,
missing-cutover rejection, shared retirement fan-out, zero-state normal shutdown,
and a deliberately failed material initialization that leaves RHI uninitialized.

### 3C.3.2 implementation evidence

Device loss now has one terminal path rather than being represented as ordinary
cancellation or shutdown. `RenderingService` stops new update/tick work after the
RHI reports `Removed` or `ResetRequired`, joins the renderer CPU chain, and then
abandons scene bindings, material residency, native program/pipeline work,
materialization, provider resolution, texture and mesh residency, layout state,
GPU Scene state, descriptor domains, and finally the RHI backend in dependency
order. A failed shutdown idle wait carrying `DeviceLost` enters the same path.

Move-only material, texture, and mesh demand/reference handles keep their compact
owner-plus-generation representation. Their validity checks now consult the
owner's generational record, so terminal abandonment invalidates live handles
without per-handle control blocks or a second registry. Later reset/destruction
is a no-op. Custom material providers have a required terminal `abandon` callback
separate from normal fence-backed release; pending CPU preparation jobs are
joined before captured records are destroyed.

The RHI `IBackend::AbandonDevice` contract is mandatory. The D3D12 implementation
does not call `WaitIdle`, wait for a fence, or process normal retirement; it drops
device-owned state and clears the global RHI binding. Global detachment remains
unconditional even if backend cleanup reports a diagnostic failure, preventing a
dangling backend pointer. Focused tests prove live material, texture, and mesh
handles become stale and harmless, custom provider tokens take their terminal
callback, GPU Scene can be abandoned with live state, and D3D12 destroys an active
query pool without an idle wait. The affected rendering, engine-service,
frame-pipeline, streaming, and D3D12 backend suites pass. No draw, recovery/replay
policy, compatibility path, or parallel lifetime system was added.

### 3C.4 implementation evidence

`materialRuntimeServiceTests` is the production headless closure proof. It builds
current-format VSHADER, VPPL, VMAT, and VTEX artifacts with the production
writers, loads both a loose closure and an indexed VPAK closure through the
managed streaming/resource services, and carries them through resolver,
materializer, residency, decal binding, and the existing pipeline cache. Exact
GPU readback verifies the material row, dense resource range, parameter words,
layout fingerprints, and accepted decal reference. No draw is submitted.

The same proof covers coalesced demand and bounded backpressure, Required-load
failure, exact Optional fallback, concrete technique pending/ready/failure,
last-valid candidate cancellation, canceled-publication retry, distinct-root
replacement, incomplete-cutover rejection, real graphics/compute/copy fence
retirement, and zero normal shutdown state. Lower tests remain authoritative for
generic corruption and injected owner-failure matrices.

The closure pass exposed two contract bugs. `MaterialSceneBindingBridge` now
restores an active binding when a pending clear is superseded, preventing the
clear receipt from accepting a later candidate or clearing an equal active
request. `RenderingService` now declares its Resources dependency and clears
unreferenced native material programs plus registered fallbacks during quiesce,
before `ResourcesService` validates that no dependent strong handles remain.

The final cross-configuration recheck exposed one proof-harness ordering bug,
not a production lifecycle bug. `FramePipelineService::RunFrame` deliberately
returns while the renderer CPU publication chain may still be active. The 3C.4
test then took direct control of `RenderSceneGpuPublisher::Prepare`; Debug timing
happened to finish the earlier publication first, while Shipping correctly
returned `Busy`. The proof now calls `FlushPreviousFrameProcessing` and
`ResolveContributions` before direct cancellation control, matching the normal
next-`RenderUpdate` boundary. The production publisher contract was unchanged.
After this correction, the affected Debug matrix passes 27/27 and the focused
Shipping projects build and pass 8/8. The corrected Shipping production vertical
also passes ten consecutive reruns.

Phase 3C and Phase 3 are complete and frozen. Same-path overlapping hot reload,
instances/overrides, editor integration, and draw execution remain Phase 4 or
later work.
