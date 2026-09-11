# Shader and Pipeline Runtime

## Current scope status

Geometry-rendering continuation: see the source-backed
[RED batcher and phase study](../../../docs/development/geometry-rendering-study.md).
The production surface/pass/vertex shader bridge belongs to 9B; existing material
codegen, GPU Scene material accessors, runtime residency and pipeline caching are
reused. Phase 10 supplies the actual mesh graph passes and scene-on-screen proof.
The clarified plan ports RED's relevant CPU preparation/recording machinery;
geometry uses GPU-driven indirect drawing from the start, with no direct-first
baseline. Existing spatial GPU-candidate production is reused.

The startup-layout gap is closed. `RenderingServiceConfig::Pipeline::bindingLayouts` accepts ordinary RHI layout descriptions; startup creates/retains the layouts through pipeline preparation and passes them through the existing factory. No shader-name-specific behavior or general resource-binding subsystem was added. Empty layouts retain the existing behavior.

The startup integration test now cooks the actual FullscreenCopy source/pipeline pair, writes and registers it in the prerequisite mount service, and configures its push-constant layout as fixture data. Successful `RenderingService` initialization proves real streaming, native shader loading and ready pipeline creation. Cleanup evicts/unregisters the cooked resources, removes fixture files, and shuts down the renderer with feature pipeline ownership intact until its normal release point.

`materialRuntimeServiceTests` built and passed in Debug and Shipping with the actual copy startup catalog and cleanup checks. The actual FullscreenCopy scheduled graph/pixel proof remains in the native test; startup and graph execution are separate tests, not one service-driven copy frame. That is the explicit validation boundary, not an outstanding general graph/allocator or binding-system implementation task. Historical progress notes below describe the route to this state; their earlier pending-startup statements are superseded here.

The runtime boundary is intentionally narrow:

```text
cooked vshader -> RenderShader -> immutable RHI shader handles
cooked vpipeline + resolved RenderShader generations + render-graph attachment signature
    -> RequestRenderPipeline -> asynchronous PipelineCache -> immutable RHI pipeline handle
```

`RenderShader` owns native shader stages and their content identities. Reloading creates a new object generation; outstanding pipeline-cache requests retain the old RHI shader handles until their asynchronous creation work finishes.

`RequestRenderPipeline` does not bind resources or record commands. It validates resource identity, creates or resolves the vertex layout, converts fixed-function state, incorporates concrete render-target formats into the SHA-256 pipeline key, and hands a deep-copied description to the cache. Renderer features prepare pipeline variants before recording; nodes consume ready handles during execution. The graph and flow allocator do not own shader loading or pipeline requests.

The primary binding path is bindless. Global resource and sampler descriptor domains are renderer-owned interface resources supplied during pipeline materialization. Shader reflection remains representation-neutral, so materials and cooked pipeline documents do not encode a per-draw descriptor strategy. Fixed layouts remain an explicit exceptional input rather than an alternative renderer architecture.

Source compilation remains tool-side. Slang must emit backend-native bytecode, normalized reflection, dependency information, and a compiler/options fingerprint into `shaders::BuildDescription`. Shipping runtime code consumes only cooked documents and does not load a compiler.

## Renderer feature shader catalog — Phase 1

`RenderShaderMap` follows the named, renderer-owned static-shader role of RED's `CRenderShaderMap`. It accepts named, already-open cooked `ShaderFile` documents and reuses `RenderShader::Load`; it is not a second asset loader or pipeline cache. Initialization is all-or-nothing, duplicate names fail, hash collisions compare full names, and required missing shaders are fatal. `FindShader` is the optional lookup. Neither lookup loads, allocates, or locks. Source documents are borrowed only while initialization runs; each stable catalog entry owns its native shader stages and copied interface metadata afterward.

`FrameRenderer::InitializeShaders` is startup-only, before resource-allocator initialization and graph construction. Cached nodes may borrow `GetShader(name)` results until the graph cache is cleared. Joined renderer shutdown destroys cached nodes before clearing shaders and before RHI shutdown; startup rollback also clears the catalog if allocator initialization never succeeded. There is deliberately no live mutation or reload API in this phase.

Material residency retains its existing generation-aware, reference-counted and evictable native-program cache. That ownership policy is not replaced by the fixed renderer-feature catalog. Both use the existing native shader loader and existing pipeline machinery; ordinary feature nodes must not manufacture materials to access shaders.

Phase 2 connects this foundation to service startup as described below. No production feature catalog is registered by default. Phase 3 adds one real feature node, its cooked assets, and its end-to-end execution proof.

Validation: native `rhiNvrhiTests` built and passed in Debug and Shipping, including catalog duplicate-name rollback, stable/optional lookup, rejection of reinitialization without pointer invalidation, and clear/reinitialize. `engineServicesTests` built and passed in Debug. These checks do not constitute feature pipeline or graph-node execution coverage.

## Startup loading and ready feature pipelines — Phase 2

`RenderingServiceConfig::rendererShaders` supplies named, typed cooked shader references; `rendererPipelines` supplies named cooked pipeline references and concrete graphics attachment signatures. Catalog backing storage remains valid through service initialization. Empty catalogs preserve existing applications without inventing shader paths or placeholder feature assets.

The service requests these assets through the existing `ResourceStreamer` and decoders. Sources must already be mounted: an application can set `rendererCatalogSourceService` to its mount/bootstrap service to establish that ordering through the existing service dependency mechanism. That service must initialize independently of rendering. A nonempty catalog requires resource streaming and a renderer device; missing assets fail initialization rather than being silently skipped.

Shaders load before allocator/graph initialization. Once the existing pipeline cache and renderer descriptor domains are available, `FrameRenderer::InitializePipelines` resolves the configured shaders and calls `RequestRenderPipeline`. It issues the catalog requests before waiting for their completion, then publishes the named ready handles only if all requests succeeded. This is startup-only preparation. The service-owned cache, still named `materialPipelines`, is shared with the material path; there is no additional native PSO cache.

Feature-node construction may obtain `GetShader(name)` and `GetPipeline(name)` and retain borrowed handles for execution. Pipeline lookup is a small catalog name scan intended for construction, not a per-draw operation; it neither requests nor waits for pipelines. Recording does not load assets, compile shaders, or synchronize on catalog preparation. No graph or allocator API was added for shader ownership.

Joined shutdown and startup rollback clear cached nodes and feature pipeline requests before shutting down the shared cache, then clear native shaders before shutting down RHI. Cooked resource handles are needed only during initialization: shader loading owns its native data, and pipeline requests already deep-copy their asynchronous creation inputs.

Validation: `rhiNvrhiTests` and `engineServicesTests` built and passed in Debug and Shipping. The native fixture covers feature pipeline preparation, duplicate-name rejection, stable ready-handle lookup and request release. The engine service tests cover existing empty-catalog startup. A configured catalog through an application mount service and a real graph feature remain to be exercised end-to-end; they are not implied by the native fixture.

## Node execution handoff — Phase 3, first slice

The native conformance fixture now constructs a `CatalogDrawNode : RenderNodeImpl` from the renderer's prepared pipeline catalog. It borrows the ready pipeline once in its constructor and uses ordinary pipeline/target/vertex binding and draw calls in `Execute()`. The same node is recorded twice into independent command lists, submitted, and verified by exact RGBA texture readback. Native Debug and Shipping checks pass. The fixture's pipeline disables face culling so the diagnostic does not depend on triangle winding.

This follows the constructor-time lookup visible in RED's `CRenderNode_ResolveDistortion` (`renderGraphNodes.cpp`), while preserving Vanguard's cooked pipeline/cache ownership. No production graph topology, allocator contract, lock or cache was added.

Boundary: this is a test-local diagnostic node using the existing cooked-document fixture, not a shipped feature. The harness calls its execution body directly and handles resource transitions/submission/readback; it does not prove graph `Process()` orchestration or allocator declarations. Phase 3 remains open for a selected production feature and cooked assets, configured resource-streaming startup, and graph-driven execution proof. Existing camera/composition feature placeholders remain untouched.

## 3A — Fullscreen copy asset contract

Selected feature: `FullscreenCopy`, a non-camera texture-to-color-target draw. The reference is `CRenderPostProcess::PresentCopy` in RED's `renderPostProcess.cpp`: source/target rectangles, a fullscreen draw when shader processing is required, and renderer-owned shaders. This bounded feature does not replace PresentCopy's HDR/gamma logic or the native-copy fast path. It is not wired into presentation or a camera builder by this phase.

Source: `source/rendering/shaders/fullscreen_copy.vsl`. Vertex entry `CopyVertexMain` generates a fullscreen triangle from `SV_VertexID`; fragment entry `CopyFragmentMain` samples mip zero through the renderer's resource/sampler descriptor domains. No vertex buffer, material, depth buffer or per-draw binding-layout object is required.

Cook recipe using existing tooling:

- Shader catalog name `FullscreenCopy`; cooked resource `rendering/shaders/fullscreen_copy.vshader`; program identity `0x4653434f5059`. Use `ShaderAssetCompiler` with the source above, the two named entries, shader model 6.6 and the normal target/settings/source-derived permutation and DDC identity. Do not hand-author fingerprints.
- Pipeline catalog name `FullscreenCopy`; cooked resource `rendering/pipelines/fullscreen_copy.vpipeline`. Use `pipelines::WritePipeline` with graphics kind, the cooked shader's actual resource/permutation/interface fingerprints, no vertex streams/attributes, triangle topology, solid fill, no culling, no depth/stencil, no blending, and RGBA writes to one color attachment. Supply the concrete destination format through the existing startup attachment signature; use a single-sample UNorm target for the first integration proof.
- Push constants, in byte order: `u32 sourceDescriptor` (0), `u32 samplerDescriptor` (4), `float2 uvScale` (8), `float2 uvOffset` (16); total 24 bytes. Full-image copy uses scale `(1,1)` and offset `(0,0)`. Source cropping uses normalized scale/offset; destination placement uses viewport/scissor. The caller supplies a clamp sampler and retains descriptor validity through GPU completion.
- Source and destination must be distinct, nonoverlapping resources; source is a sampled 2D color texture and destination a render target. No implicit HDR conversion, tone mapping, alpha compositing or multisample resolve. Standard texture/attachment format conversion still applies.

The tooling test reads this actual source, compiles DXIL, checks zero vertex inputs and one fragment output, emits a cooked shader and reopens it. Debug and Shipping validation pass. Generated target-specific bytes are not checked into source control. Pipeline document generation and source mounting will use the existing build/resource APIs during the end-to-end fixture; no new manifest parser or cooking subsystem is introduced.

Next, 3B implements the node with separate resource declaration and execution, borrowing the prepared pipeline at construction. 3C proves catalog streaming, graph execution, pixel output and joined shutdown. Neither is complete from this source/cook-contract slice.

## 3B — Fullscreen copy node

`RenderNodeFullscreenCopy` is implemented in `render_graph_nodes.hpp/.cpp`. Construction borrows `FrameRenderer::GetPipeline("FullscreenCopy")` and copies resource names and the 24-byte constants. `DeclareResources()` uses the existing named-use API: mip-zero sampled read of the source and mip-zero discard/write of the destination, with balanced scopes. `Execute()` obtains the resolved textures, sets the prepared pipeline, target, viewport/scissor and constants, and draws three vertices. It does not request a shader, create a pipeline, allocate a descriptor or add synchronization.

This first implementation explicitly accepts a stable imported source: `FullscreenCopyDesc::source` and `sourceDescriptor` must identify the same whole 2D texture, with a mip-zero/default-format SRV. The caller supplies a clamp sampler descriptor in the descriptor domains used to prepare the pipeline. The source and descriptors remain valid through GPU retirement, and the cached node must be rebuilt if the source or descriptor assignment changes. Execution checks resolved source identity; it cannot introspect a descriptor's payload. Transient graph-source descriptor allocation is not implemented or implicitly promised. The target can be allocator-resolved, must not overlap the source, and both textures must be single-sample non-array 2D resources. Only target mip zero is written.

No camera, composition or presentation builder is changed. These are source-level node integration and compilation results; existing native regressions do not exercise this new node. 3C still needs the cooked pipeline, configured streaming and actual graph-driven readback/lifetime proof.

## 3C — Asset-pair fixture established; integration remains open

`shaderToolsTests` now cooks both FullscreenCopy documents together: compiled shader output and a no-vertex-stream graphics pipeline referencing that shader's actual permutation, binding-layout and pipeline-interface fingerprints. The pipeline uses deferred attachment format, disabled culling and one unblended color output. Both documents reopen successfully in Debug and Shipping.

This exposed and corrected a fixture omission: the earlier standalone shader round-trip supplied the default empty permutation, which is legal for that shader document but cannot be referenced by a valid pipeline. The single-variant test now supplies a source-derived permutation; production remains on `ShaderAssetCompiler`'s build-settings/target identity path. No pipeline validation was weakened.

This is the asset prerequisite, not end-to-end closure. The documents are currently generated in memory by the tooling test. Still pending: publish/mount the pair through the configured startup catalog, create the source texture and matching descriptor, execute `RenderNodeFullscreenCopy` through graph scheduling and allocator declarations, verify copied pixels and joined retirement. Do not count the existing direct diagnostic draw as that proof. No production frame builder is enabled by this slice.

### Configured startup lifecycle proof

`materialRuntimeServiceTests` now registers a test mount service dependent on `ResourceStreamingService`, and makes rendering depend on it through `rendererCatalogSourceService`. It writes/registers the existing cooked shader/pipeline fixture before renderer initialization and configures nonempty shader and pipeline catalogs. Successful host startup therefore includes real streaming, shader loading and pipeline preparation, not just empty-catalog startup. It prepares an FP16 attachment variant to leave the later material test's UNorm pipeline cold; the existing pending-state assertion is preserved.

Debug and Shipping builds/runs pass, including the existing material runtime lifecycle and clean host shutdown with live feature pipeline requests released in renderer shutdown order. Test asset files are removed after resource closure unregisters. No runtime loading API, synchronization or ownership mechanism was added for this test.

This establishes the general configured catalog/service lifecycle using existing fixture assets, not the FullscreenCopy asset pair. Remaining 3C work is still the integrated FullscreenCopy path: mount its cooked pair, import a source with its matching descriptor, execute the actual node via graph/allocator orchestration, and check GPU pixels and joined retirement. The generic startup proof and standalone asset tests must not be reported as that combined proof.

### Re-evaluation: ordinary graph import connection

The graph context previously exposed only presentation-specific import even though `ResourcePlanningWriter::ImportTexture` already supported ordinary registered textures. That left the FullscreenCopy imported-source contract without a node-facing declaration connection.

`RenderNodeImplContext::RTInject(name, importedId)` now forwards to that existing writer operation in the current flow space. This follows RED's `SRenderNodeImplContext::RTInject` role; the Vanguard-specific argument is the frame's registered `ImportedResourceId`, preserving retained ownership and readiness validation. A declaration/setup node injects the source (and an imported test destination), then the ordered copy node refers to those names. Register imports before declarations and do not retain their frame-generation IDs in a reusable graph node across frames without updating its frame data. No new allocator state, locking, descriptor allocation or execution-mode branch was introduced.

A graph preparation test covers invalid/unregistered import failure propagation and cleanup. This closes the declaration API gap only. FullscreenCopy's successful scheduled draw/readback and the combined asset-streaming proof remain open; do not mark 3C complete on the strength of the individual checks above.

### Scheduled catalog-node fixture

The native test now also runs a catalog draw through actual graph declaration, allocator resolve, `RunRenderNodeJobs`/`Process()`, recording completion, native submission and terminal allocator receipts. It injects an ordinary retained target and verifies the node recorded and the allocator accepted successful completion. The test's standalone node owns its command list; production child nodes such as FullscreenCopy continue to require their enclosing command-list owner.

The runner is test-only, parameterized by the graph and allocator, and limited to one Graphics command scope. It does not inspect shader names or copy constants, and does not pretend to implement arbitrary multi-scope scheduling. Existing runtime orchestration, API visibility and synchronization are unchanged. Feature-specific identities remain in feature nodes and test fixtures.

This advances the scheduled execution proof beyond the earlier direct `Execute()` call, but uses the existing catalog triangle fixture. It still does not prove the actual FullscreenCopy shader/node or the combined streaming-to-copy pixel path. Those remain the explicit 3C exit gate.

### Actual FullscreenCopy graph/readback proof

The native fixture now compiles the actual source with tooling-only Slang, cooks/reopens both documents, initializes the renderer catalog and pipeline, and constructs a normal command-list group containing an import setup node and the production `RenderNodeFullscreenCopy`. The existing runner drives declaration, resolve, scheduled `Process()`, submission and allocator completion. A four-color 2x2 source is copied and every RGBA byte is checked through native GPU readback. Node storage is destroyed before catalog release; descriptors retire after joined GPU use. This supersedes the earlier outstanding native-copy proof, not the combined service integration gate.

The test exposed a missing fixture input: the shader needs a 24-byte, pixel-stage push-constant binding layout at slot/space zero in addition to its bindless domains. The fixture supplies that through `PipelineInterfaceResources::bindingLayouts`; there is no feature-name branch or automatic FullscreenCopy rule in the factory. The Slang dependency belongs only to `rhiNvrhiTests`, not to the renderer runtime.

Remaining combined-service seam: `RenderingService` currently supplies descriptor domains but has no configured feature binding-layout creation path. The eventual startup integration must supply the shader-required layout through a general interface contract, not hardcode FullscreenCopy's 24 bytes into the service. Until that is connected and exercised with the same cooked pair, 3C remains open even though the actual native graph copy is proven separately.
