# Vanguard RHI

`rhi` is Vanguard's renderer-facing hardware contract. Its vocabulary deliberately stays close to GpuApi so proven renderer code can be adapted mechanically: typed resource references, `CreateTexture`, `CreateBuffer`, bound command lists, explicit barriers, queue submissions, GPU fences, swapchains, backbuffers, presentation and debug names.

The API does not expose NVRHI or native D3D12/Vulkan objects. A backend implements `IBackend`; the NVRHI implementation remains private and may use narrow native extensions where explicit aliasing or queue behavior cannot be represented faithfully by NVRHI alone.

API-independent implementation belongs to the private `CommonBackend`: resource creation, command recording, barriers, submission, queries, ownership, garbage collection and debug naming all operate on `nvrhi::IDevice`. Native backend files create the device and queues, provide presentation and device-loss integration, discover native capabilities, and delegate common operations. Adding Vulkan therefore supplies a Vulkan native-device layer to the same common implementation instead of reproducing the complete RHI.

## Ownership and lifetime

Raw references such as `TextureRef` and `BufferRef` are cheap non-owning identities. `Ref<T>` is the intrusive owning wrapper expected by renderer code; `Texture`, `Buffer`, `Heap`, `SamplerState`, `Shader`, `Pipeline`, `BindingLayout`, `AccelerationStructure` and `SwapChain` are its named aliases. Copying retains, moving transfers, and destruction releases. A newly created raw reference is transferred into an owner with `Ref<T>(AdoptReference, raw)` so adoption cannot accidentally increment its count.

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

Virtual textures and buffers have no backing allocation until `BindMemory` binds them to a compatible heap range. Memory requirements include size, alignment and an opaque compatibility class; the future transient allocator may alias only non-overlapping lifetimes with compatible requirements and must emit an explicit aliasing barrier.

## Threading

Initialization and shutdown are composition-root operations and must run with all RHI callers quiesced. The bound command list is thread-local. Backend resource creation, reference counting and command-list creation are required to support concurrent renderer jobs. A command list may be recorded by only one thread at a time. Queue submission order and synchronization are explicit.

Submission is serialized inside the backend while recording remains fully parallel. This gives graphics, compute and copy queues one deterministic timeline authority without forcing worker jobs to contend while generating commands. `ForkAsyncCompute` inserts a graphics-to-compute queue wait and returns a compute completion fence; `JoinAsyncCompute` inserts the reverse wait and returns a graphics completion fence. A multi-queue submission without one of those explicit synchronization modes is rejected.

Command lists retain every resource they record. Submission stamps those resources with the queue's native fence before dropping the recording references, so releasing a resource from another CPU thread cannot destroy it while recorded or executing. An unsubmitted command list must be passed to `DiscardCommandList`; this closes it and releases its recording references without silently submitting work.

Graphics and compute setters update command-list-local shadow state. Draw and dispatch validate the complete state, flush pending barriers once, publish one coarse NVRHI graphics or compute state object, restore the pipeline's push constants and emit the native command. Pipeline changes invalidate the shadow push-constant block. Direct draw, indexed draw, multi-draw indirect, counted indexed indirect, direct dispatch and indirect dispatch use portable argument layouts declared in `rhi_types.hpp`.

UAV barriers remain portable NVRHI state operations. Aliasing barriers are a narrow backend extension because NVRHI has no public portable aliasing primitive: the common recorder validates and retains placed resources, then the D3D12 backend emits the native barrier. A Vulkan backend will implement the same callback with its corresponding memory barrier without changing renderer code.

Texture initialization is described per mip and array slice with explicit row and depth pitches. The façade validates format block geometry, including BC-compressed 4x4 blocks, before the backend reads caller-owned bytes. Buffers perform equivalent range and overflow validation for initialization, uploads and copies.

## Current boundary

The implemented native path now covers textures, buffers, placed heaps, samplers, shaders, canonical vertex layouts, graphics/compute/ray-tracing pipelines, automatic bindless-domain binding, push constants, render-target setup, viewport and scissor state, vertex/index/indirect buffers, direct and indirect graphics/compute commands, immutable descriptor-layout creation, global mutable resource and sampler descriptor domains, generational descriptor allocation, explicit multi-queue descriptor retirement, pitched initial uploads, explicit buffer/texture uploads, buffer copies, upload/readback buffer locking, consumable command lists, transitions, UAV and aliasing barriers, barrier flushing, graphics/compute/copy submission, fork/join queue synchronization, native timeline fences and fence-safe resource retirement.

Clears, resolves, texture copies, query recording, ray dispatch/shader tables, acceleration-structure commands and mesh-shader dispatch remain RHI work. Render passes, render nodes, resource-flow allocation policy and Jobs-based render-graph scheduling remain above the low-level RHI. Asynchronous pipeline coalescing and warmup are owned by the rendering module rather than duplicated here.
