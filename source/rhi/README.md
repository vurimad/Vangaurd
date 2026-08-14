# Vanguard RHI

`rhi` is Vanguard's renderer-facing hardware contract. Its vocabulary deliberately stays close to GpuApi so proven renderer code can be adapted mechanically: typed resource references, `CreateTexture`, `CreateBuffer`, bound command lists, explicit barriers, queue submissions, GPU fences, swapchains, backbuffers, presentation and debug names.

The API does not expose NVRHI or native D3D12/Vulkan objects. A backend implements `IBackend`; the NVRHI implementation remains private and may use narrow native extensions where explicit aliasing or queue behavior cannot be represented faithfully by NVRHI alone.

API-independent implementation belongs to the private `CommonBackend`: resource creation, command recording, barriers, submission, ownership and garbage collection operate on `nvrhi::IDevice`. Native backend files create the device and queues, provide presentation, raw timestamp calibration and query-pool operations not exposed by NVRHI, discover native capabilities, and delegate common operations. Adding Vulkan therefore supplies a narrow Vulkan native-device layer around the same common implementation instead of reproducing the complete RHI.

## Ownership and lifetime

Raw references such as `TextureRef` and `BufferRef` are cheap non-owning identities. `Ref<T>` is the intrusive owning wrapper expected by renderer code; `Texture`, `TextureReadback`, `Buffer`, `Heap`, `SamplerState`, `Shader`, `Pipeline`, `BindingLayout`, `AccelerationStructure` and `SwapChain` are its named aliases. Copying retains, moving transfers, and destruction releases. A newly created raw reference is transferred into an owner with `Ref<T>(AdoptReference, raw)` so adoption cannot accidentally increment its count.

`ResourceRef` is the explicitly tagged type-erased identity used by allocators and generic tables. It packs kind, a 28-bit slot index and a 32-bit generation into eight bytes, preserving the size of typed references. Converting it back with `CastResourceRef<T>` checks the resource kind and returns null on mismatch. It is not an ownership object.

The ownership exceptions are deliberate: `VertexLayoutRef` is backend-interned and non-refcounted, `CommandListRef` is a consumable single-owner recording reference, and `QueryPool` is a move-only owner. Submitted command-list references must not be reused.

Binding layouts are immutable, structurally interned objects. A request is canonicalized independently of declaration order, hashed into a collision-safe cache and returned with ordinary intrusive ownership. They describe one bounded register space and may contain CBV, SRV, UAV and sampler namespaces without treating equal numeric registers in different namespaces as collisions.

Binding layouts are low-level pipeline-interface objects, not a material binding policy. Global bindless interfaces are represented by descriptor domains instead of pretending to be immutable binding layouts. Per-material resource packs, command-list binding schemas and a second fixed-binding execution path are intentionally absent.

Pipelines declare their global resource and sampler descriptor domains. Recording `SetPipeline` therefore supplies the native bindless tables automatically; draw code passes compact descriptor indices through push constants or GPU-visible records instead of rebinding material textures. Push-constant-only binding layouts are materialized as immutable native binding sets. Other fixed-binding layouts may describe a pipeline interface, but command execution rejects them explicitly until the optional fixed-binding path is implemented.

## Bindless descriptor domains

A descriptor domain owns one mutable native descriptor table and its matching pipeline layout. Resource and sampler domains are separate because the hardware exposes distinct descriptor heaps and Vulkan uses distinct descriptor classes. `DescriptorHandle` contains a stable GPU-visible index and a CPU-only generation. Allocation and publication are thread-safe; a slot can be written exactly once while live, preventing an in-flight descriptor from being mutated behind GPU work.

`RetireDescriptor` requires the caller to provide the last graphics, compute and copy fence values that may reference the index. The handle becomes invalid immediately, while the descriptor, its strongly retained resource and the slot remain unavailable until every supplied fence completes. Reuse increments the generation, making delayed CPU operations fail rather than target a different resource. `RetireResources` performs ordinary reclamation, and allocation also opportunistically collects completed slots.

Descriptor tables intentionally do not infer resource states or residency. The renderer must stop publishing an index before retiring it and must supply the actual last-use fences from its submission plan. Descriptor domains retain native resources but do not make an unsubmitted command safe, and they do not rewrite a live slot as a convenience operation. Resource replacement therefore allocates and publishes a new handle, updates GPU-visible records, then explicitly retires the old handle.

The NVRHI backend follows a fence-epoch retirement model. Each resource kind owns a fixed-capacity, generation-safe slot table; final release enters a bounded lock-free multi-producer queue without allocating. Queue insertion and epoch accounting are published under a short lock taken only when the reference count reaches zero, preventing submission sealing from observing a queue entry without its matching bucket count. Retirements are grouped into rotating fence buckets, then a latent Vanguard job performs physical destruction only after every queue that used each resource has completed its recorded fence. Exact per-resource graphics, compute and copy fence values remain the final safety check, so a coarse bucket can delay destruction but cannot release an in-flight object early. Queue saturation is recoverable by scanning zero-reference slots; bucket saturation applies conservative backpressure until completed epochs are collected. Telemetry reports live owners, pending/destroying resources, stale-reference attempts, peak backlog, recovery events and bucket pressure.

Normal shutdown waits for reclamation, rejects leaked owners and destroys all zero-reference payloads after the GPU is idle. The backend destructor also has an explicit emergency device-idle path that invalidates leaked generations and releases native payloads, preventing an application teardown mistake from leaking the D3D12 device graph; that path is diagnostic recovery, not normal ownership policy.

`FlushRetiredResources` is a blocking maintenance barrier for shutdown and native ownership replacement. It waits for GPU
idle and physical deferred destruction; it is deliberately separate from per-frame `RetireResources` and must not be used
for normal frame pacing.

Swap chains own every native back buffer through ordinary generation-safe `TextureRef` entries, so command recording, submission tracking, debug naming and renderer access use the same resource contract as non-presentable textures. `AcquireBackBuffer` returns an `AcquiredBackBuffer` token identifying one physical image and one acquisition serial. That exact token must be transitioned on a graphics command list and submitted before `Present` accepts it; an unused token must be returned through `AbandonBackBuffer`. A second acquisition, resize, or presentation-state mutation is rejected while a token is outstanding.

Presentation has a dedicated native timeline fence with one completion value per back buffer. Acquiring the current back buffer first observes the swap chain's bounded frame-latency policy and then waits only when that physical buffer is being recycled before its previous presentation completes; resize and destruction wait the latest presentation value before releasing native buffers. Command submission, presentation, and the presentation-fence signal share one backend queue-ordering authority, preserving the order observed by the graphics queue without a per-frame GPU-idle wait. A pacing timeout is an explicit failure and never silently disables the latency contract.

Swap-chain color configuration is explicit. SDR clears native HDR metadata, HDR10 supplies mastering primaries, luminance, MaxCLL and MaxFALL, and scRGB uses its linear extended-range color space without HDR10 static metadata. The backend records the active output primaries, white point, luminance range, bit depth and desktop HDR mode. `SwapChainStats` exposes these values alongside pacing time, timeout, acquisition, resize, presentation, fence and tearing telemetry for diagnostics and editor inspection.

Virtual textures and buffers have no backing allocation until `BindMemory` binds them to a compatible heap range. Memory requirements include size, alignment and an opaque compatibility class; the future transient allocator may alias only non-overlapping lifetimes with compatible requirements and must emit an explicit aliasing barrier.

GPU memory control is explicit and backend-agnostic. Local and non-local `MemoryBudgetSnapshot` values expose the operating-system budget, current process usage and reservation state. Textures, buffers and placed heaps accept residency priorities and bounded batched make-resident/evict operations. Eviction receives the last graphics, compute and copy fences that may use the allocation; the backend rechecks every fence while holding the same serialization authority used by queue submission and returns `Busy` instead of waiting or evicting in-flight memory. Residency telemetry separates requests, affected native allocations, rejected in-flight evictions and backend failures. Callers must retain every supplied resource for the duration of an operation.

Command lists gather residency allocations while recording. Direct texture, buffer and heap operands are tracked automatically;
placed textures and buffers coalesce to their owning heap. Resources reached indirectly through bindless indices are declared by
graph or resource-table infrastructure through `AddToResidencyWorkingSet`; individual render nodes do not manage residency.
Submission checks the deduplicated working set under the queue-ordering authority and makes explicitly evicted allocations
resident before closing or executing any command list. Failure leaves every list open and retryable. Explicit residency calls
remain reserved for streaming and allocator infrastructure; ordinary render code must not manually evict resources.

The backend owns a bounded budget-driven residency policy for device-local committed allocations and placed heaps. Each
successful submission stamps its deduplicated working set with the graphics, compute or copy completion fence and a stable
use ordinal. Maintenance begins only when local usage crosses the configured pressure threshold and evicts toward the lower
recovery threshold. Candidates are ordered by residency priority, then least-recent submission use, and are never selected
while pinned or protected by an incomplete queue fence. Selection and native eviction are serialized with submission, are
bounded per maintenance call, and never wait for GPU work. Repeated frame maintenance continues pressure recovery without a
large one-frame eviction spike. Allocation destruction removes its policy record at the actual deferred-reclamation boundary.

`SetResidencyPinned` is reserved for allocations that must survive budget pressure, such as bootstrap data or a renderer's
critical fallback resources. Pinning does not imply command-use synchronization and does not make an otherwise invalid manual
eviction safe. `ResidencyStats` exposes pressure events, selected objects and bytes, in-flight skips, no-candidate incidents,
failures, tracked resident/evicted bytes, pinned allocations, and the last observed local budget for diagnostics and editor
inspection.

## Threading

Initialization and shutdown are composition-root operations and must run with all RHI callers quiesced. The bound command list is thread-local. Backend resource creation, reference counting and command-list creation are required to support concurrent renderer jobs. A command list may be recorded by only one thread at a time. Queue submission order and synchronization are explicit.

Submission and native presentation operations are serialized inside the backend while recording remains fully parallel. This gives graphics, compute and copy queues one deterministic timeline authority without forcing worker jobs to contend while generating commands. `ForkAsyncCompute` inserts a graphics-to-compute queue wait and returns a compute completion fence; `JoinAsyncCompute` inserts the reverse wait and returns a graphics completion fence. A multi-queue submission without one of those explicit synchronization modes is rejected.

Command lists retain every resource they record. Submission stamps those resources with the queue's native fence before dropping the recording references, so releasing a resource from another CPU thread cannot destroy it while recorded or executing. An unsubmitted command list must be passed to `DiscardCommandList`; this closes it and releases its recording references without silently submitting work.

Graphics and compute setters update command-list-local shadow state. Draw and dispatch validate the complete state, flush pending barriers once, publish one coarse NVRHI graphics or compute state object, restore the pipeline's push constants and emit the native command. Pipeline changes invalidate the shadow push-constant block. Direct draw, indexed draw, multi-draw indirect, counted indexed indirect, direct dispatch and indirect dispatch use portable argument layouts declared in `rhi_types.hpp`.

UAV barriers remain portable NVRHI state operations. Aliasing barriers are a narrow backend extension because NVRHI has no public portable aliasing primitive: the common recorder validates and retains placed resources, then the D3D12 backend emits the native barrier. A Vulkan backend will implement the same callback with its corresponding memory barrier without changing renderer code.

Texture initialization is described per mip and array slice with explicit row and depth pitches. The façade validates format block geometry, including BC-compressed 4x4 blocks, before the backend reads caller-owned bytes. Buffers perform equivalent range and overflow validation for initialization, uploads and copies.

Texture copies address one explicit source and destination subresource with independent origins and a shared extent. An all-zero extent means the complete source mip; partially implicit extents are rejected. The backend validates mip and slice identity, dimensional compatibility, bounds, compressed 4x4 block geometry, multisample whole-subresource restrictions, overlapping self-copies, declared copy usage and command-list tracked states before recording work. Color MSAA resolves similarly require matching formats and extents, an explicitly multisampled source, a single-sampled destination, declared resolve usage and `ResolveSource`/`ResolveDestination` states. Depth-stencil copies and resolves remain rejected until a portable plane-aware contract exists.

Texture readback is an owned asynchronous transfer rather than a synchronous conversion helper. `RequestTextureReadback` records one native-format mip/slice region into dedicated staging storage and retains both the source and request until submission. The request is stamped by the actual queue-submission callback and exposes `PendingSubmission`, `PendingGpu`, `Ready`, `Mapped` and `Failed` states. Mapping before completion returns `Busy` without waiting; successful mapping reports backend-padded row/depth pitches and the exact format and extent. Discarding the recording command list immediately fails its requests. Multisampled sources require an explicit resolve first, and depth-stencil/3D readback remains rejected until portable plane and volume-layout contracts exist.

GPU events are explicitly nested command-list ranges. Submission rejects an unterminated event stack instead of silently repairing it, and ending an empty stack is an error. Point markers use the same portable marker machinery without exposing PIX, Vulkan or NVRHI types.

Color, depth and stencil clears accept explicit mip/slice ranges. Full-subresource clears use the portable command path, while rectangular clears pass through a narrow native callback because NVRHI does not expose clear rectangles. A rectangular clear targets exactly one mip and may cover one or more array slices. Texture UAV clears support floating-point and unsigned-integer formats; partial array-slice UAV clears are rejected because the portable clear view covers a complete mip across its array. A floating-point UAV clear also rejects a texture that simultaneously declares render-target usage because NVRHI otherwise selects the RTV path, while integer clears unambiguously prefer the UAV path. Buffer UAV clears accept an explicit 32-bit value.

Discard commands select a legal state from the resource's declared usage and current queue role before issuing the native discard. Graphics prefers render-target or depth-write state and otherwise uses unordered access; compute requires unordered access. Command lists retain discarded resources through submission exactly like draw, copy and barrier operands. Dynamic stencil reference and blend factor are command-list shadow state and are published with the next graphics draw rather than causing isolated native state calls.

Query pools follow an explicit issue/resolve/acquire/release contract. Occlusion and pipeline-statistics queries are ranged with matching begin/end operations; timestamps are point queries. D3D12 stores every result in a stable readback slot and refuses acquisition until the command list containing the resolve has been submitted and its queue fence has completed, preventing accidental CPU stalls. Pool destruction invalidates the public handle immediately while native heaps and readback memory remain deferred behind their last submission fence. Raw timestamp frequency and CPU/GPU clock calibration are exposed per queue so profiler code can convert ticks without assuming a device frequency.

`GpuCounterSystem` builds the renderer-facing profiling utility over those primitives. It owns a fixed-capacity ring of frame regions, two timestamp slots per named scope, frame-boundary timestamps and an optional pipeline-statistics slot per scope. Scope identities are stable hashes registered against collision-checked names; recording performs no allocation and capacity exhaustion is explicit. `EndFrame` records query resolves but does not invent a completion value: `CommitFrame` requires the actual fence returned by the submission containing those resolves. `Collect` only polls completed fences, never waits for the GPU, and leaves immutable results available until the consumer explicitly consumes or discards the generational frame handle. A slot cannot be reused while recording, awaiting submission, pending GPU completion or awaiting consumption.

## Current boundary

The implemented native path now covers textures, buffers, placed heaps, samplers, shaders, canonical vertex layouts, graphics/compute pipelines, automatic bindless-domain binding, push constants, render-target setup, viewport and scissor state, dynamic stencil/blend values, vertex/index/indirect buffers, direct and indirect graphics/compute commands, color/depth/stencil and UAV clears, resource discard, immutable descriptor-layout creation, global mutable resource and sampler descriptor domains, generational descriptor allocation, explicit multi-queue descriptor retirement, pitched initial uploads, explicit buffer/texture uploads, buffer and texture-region copies, color MSAA resolves, asynchronous native-format texture readback, upload/readback buffer locking, consumable command lists, transitions, UAV and aliasing barriers, barrier flushing, graphics/compute/copy submission, fork/join queue synchronization, native timeline fences, fence-safe resource retirement, GPU events and markers, occlusion/timestamp/pipeline-statistics query pools, buffered GPU counters, timestamp calibration, flip-model D3D12 swap chains, NVRHI-wrapped back buffers, per-buffer presentation pacing, resize, tearing, SDR/HDR color spaces and device-loss reporting.

Ray tracing has a dormant, backend-agnostic public contract so later renderer work does not require an API redesign. It defines
typed BLAS/TLAS and shader-table ownership, triangle and AABB geometry, build/update/clone/compaction operations, CPU and
GPU-authored instance paths, a portable 64-byte GPU instance record, bindless acceleration-structure descriptors, compacted-size
queries, portable device addresses, and ray dispatch dimensions. The NVRHI backend intentionally advertises both ray-tracing
capabilities as false; every façade entry point returns `Unsupported` before native work until acceleration-structure lifetime,
residency, barriers, shader tables and dispatch are implemented and validated together. Internal pipeline construction alone is
not treated as feature support.

Variable-rate shading also has a dormant portable contract. Device capabilities distinguish per-draw and shading-rate-image
tiers, enumerate supported rates and combiners, expose per-primitive support and report the image tile extent. Dynamic
`VariableRateShadingState` selects a base rate, primitive/image combiners and an optional `R8UInt` shading-rate image without
creating pipeline variants. Shading-rate images use explicit `TextureUsage::ShadingRate` and `ResourceState::ShadingRate`, so a
future Render Graph can own their lifetime and transitions like any other texture. The backend advertises VRS as unavailable and
the command returns `Unsupported`; no native VRS commands are issued yet.

Mesh-shader dispatch remains optional RHI work. Render passes, render nodes, resource-flow allocation policy and Jobs-based
render-graph scheduling remain above the low-level RHI. Asynchronous pipeline coalescing and warmup are owned by the rendering
module rather than duplicated here.
