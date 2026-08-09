# RED study record

## Sources studied

- `dev/src/common/gpuApi/include/gpuApiInterface.h`
- `dev/src/common/gpuApi/include/gpuApiLimits.h`
- The resource-reference implementation included by `gpuResourceRef.inl`

## Retained behavior

- Small typed references rather than exposing backend pointers.
- Raw typed references plus intrusive `Ref<T>` ownership, safe assignment, adoption and release helpers.
- A type-erased `ResourceRef` for resource-flow allocator storage.
- Shared texture, buffer, resource-pack, sampler, shader, swap-chain and state-object ownership.
- Non-refcounted interned vertex-layout references, consumable command-list references and uniquely owned query pools.
- A thread-bound command-list recording facade.
- Distinct default, synchronous-copy, asynchronous-copy and compute command-list roles.
- Explicit command-list submission, GPU fences, swapchains, backbuffers, resource transitions, UAV barriers, aliasing barriers and debug names.
- Submission rejects the currently bound command list and submitted lists become consumed objects.

## Modernized behavior

- References carry generations, not only numeric slots.
- Type-erased references carry a resource-kind tag; conversion back to typed references is checked.
- New raw references use explicit adoption rather than the initializer-list ownership trick used by `GpuApi::Ref<T>`.
- Queue identity is explicit in timeline fences.
- Placed heaps and virtual resources are first-class contracts for transient aliasing.
- Capabilities describe bindless resources, descriptor indexing, async compute, ray tracing and modern GPU-driven features.
- Failures retain a stable Vanguard code, native backend code and bounded message.
- Native platform objects are restricted to presentation creation and never become engine window identity.
- Backends must retire final-release native objects behind their last queue fences and expose bounded lifetime statistics.

## Rejected assumptions

- Platform preprocessor branches in the public renderer contract.
- Implicit resource-state repair.
- Backend objects, NVRHI types or native graphics types crossing the public boundary.
- Resource-flow or render-graph policy inside the hardware interface.
