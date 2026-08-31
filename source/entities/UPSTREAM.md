# Upstream design record

The entity materialization lifecycle follows the RED architecture studied in these sources:

- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entity.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entityScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/include/runtimeSystemEntityTransactor.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/runtimeSystemEntityTransactor.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/core/include/worldGlobalNodeID.h`
- `D:/root/R6.Root/Mainline/dev/src/common/core/src/worldGlobalNodeIDUtils.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldGlobalNodeRuntimeResolver.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRuntimeResolverImpl.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRuntimeResolverImpl.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldGlobalNodeRef.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRef.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/runtimeSystemNodeStreaming.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/runtimeSystemNodeStreaming.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldStreamingListener.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldPrefabNodeInstance.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldPrefabNodeInstance.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldNodeGroup.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldPrefab.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/runtimeSystemRendering.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/runtimeSystemRendering.cpp`

The retained mechanics are staged construction before world visibility, stable hierarchical entity identities, complete validation before publication, a world-scoped hash registry with read-heavy synchronization, weak/generational runtime resolution, registration in the attach epilogue, unregistration before streamed-out notification and destruction, explicit resolver/activation barriers, queued structural transactions, generation-aware cancellation, reverse teardown, separate parallel/serial operation stages, an explicit synchronization fence, final attach/detach epilogues, group-owned instance lists that are created and unregistered independently, and a per-world rendering runtime that creates or adopts one render scene and serves as the proxy creation boundary for visual components. Vanguard adds indexed unresolved waiters, required-reference dependency closure, and multiple explicit owners because `.vcell` records those policies directly and editor/gameplay systems may overlap. Vanguard replaces RED RTTI, packages, entity classes and runtime-system component ownership with reflection schemas, `.vprefab`/`.vcell`, Flecs components and Vanguard world transactions. Proxy admission budgeting and visual dissolve retirement remain deferred until Vanguard exposes a real constructed-versus-admitted proxy state. No RED file format or public namespace crosses this module boundary.

`CellStreamingSystem` preserves RED's publication ordering explicitly: component attachment is prepared for every available base cell or activation group first, and only a later coordinator-wide attach epilogue registers stable identities. Reference publication is therefore never hidden inside per-cell component attachment, and teardown retains the inverse rule by unregistering identities before object or ECS destruction.

## Stable runtime component ownership

- `common/worldEntities/include/entityComponent.h`
- `common/worldEntities/include/entityComponentsStorage.h`
- `common/worldEntities/src/entityComponentsStorage.cpp`
- `common/worldEntities/include/entityAssembler.h`
- `common/worldEntities/src/entityAssembler.cpp`
- `common/worldEntities/include/entityBuilder.h`

Vanguard's `Component` and `ComponentDirectory` preserve RED's important ownership boundary: components are stable, individually allocated objects carrying an authored identity, while the owning entity has a small component directory. Vanguard uses generational runtime handles because Flecs entity values may relocate and because stale external references must fail deterministically. Concrete typed systems retain their own dense runtime data; the directory is identity and lifetime machinery, not a frame-time typed store.

`ComponentRegistry` classifies each cooked schema as either a relocatable Flecs value or a stable object factory. `CellMaterializer` decodes both through the same prefab transaction, but keeps object data staged until the owning ECS entity and all value components are observable. It then creates the complete stable sibling set before initializing any sibling. Initialization is dispatched as one parallel component batch; every callback receives an immutable sibling resolver, retained-resource access, the streaming I/O priority and the current Jobs continuation context. Jobs spawned from that continuation extend the retained materialization counter, so serialized attachment cannot begin until the complete initialization tree finishes. As in RED's `Entity::Attach`, attachment is one complete component pass followed by a distinct complete `PostAttach` pass; stable-identity publication remains after both. Disabled components still pass through both attachment stages, matching RED's separation between lifecycle and `IComponent::Enable`. Vanguard additionally combines the local component flag with its ECS-owned entity-disabled state, updates the whole sibling set before entity transition callbacks, and retains RED's rule that `OnEnabled` is called only for attached components. Failure and release wait for that same ownership boundary before reversing the created set. Release unregisters entity references first, then destroys stable objects in reverse order while their ECS owners are still live, and only then allows queued ECS destruction to complete. This establishes RED's entity-assembly, asynchronous initialization, attachment and enablement barriers without importing RED RTTI or ownership types.

Placed stable objects follow RED's `CreateRootTransformComponent` / `ConnectFloatingComponentsToRootTransform` ordering. Vanguard's `TransformRuntime` already owns a stable placeholder root per placed entity, so materialization queries the explicit placed capability, creates one floating hard binding before parallel initialization, and retains the binding beside the component instance for exact reverse teardown. Vanguard deliberately does not reproduce RED's RTTI cast or animation-specific attachment exceptions here; authored specialized bindings remain a later extension of the same transform runtime.
