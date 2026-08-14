#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/entities/entities.hpp>
#include <vanguard/entities/world_render_bridge.hpp>
#include <vanguard/ecs/native.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <new>

namespace
{
    namespace entities = vanguard::entities;
    namespace prefab = vanguard::prefabs;
    namespace reflection = vanguard::reflection;
    namespace resources = vanguard::resources;
    namespace world = vanguard::world;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[entitiesTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    struct TransformData
    {
        vanguard::f32 translation[3]{};
    };

    struct RenderData
    {
        resources::ResourceReference mesh;
    };

    struct LightData
    {
        vanguard::f32 intensity = 1.0f;
    };

    struct BridgeLight
    {
        vanguard::f32 intensity = 1.0f;
    };

    struct BridgeTransform
    {
        vanguard::f32 x = 0.0f;
    };

    struct BridgeComponentContext
    {
        vanguard::ecs::ComponentType<BridgeLight> light;
        vanguard::ecs::ComponentType<BridgeTransform> transform;
    };

    bool BuildBridgeLight(const vanguard::ecs::World& world, const vanguard::ecs::EntityId entityId,
                          entities::WorldRenderContributorBuild& build, void* const userData) noexcept
    {
        const auto* const context = static_cast<const BridgeComponentContext*>(userData);
        const vanguard::ecs::Entity entity = world.Resolve(entityId);
        const auto* const light = context != nullptr && entity
                                      ? static_cast<const BridgeLight*>(ecs_get_id(
                                            world.Native(), entity.value, context->light.id))
                                      : nullptr;
        if (light == nullptr) return false;
        build.light.proxy.bounds.minimum[0] = -1.0f;
        build.light.proxy.bounds.minimum[1] = -1.0f;
        build.light.proxy.bounds.minimum[2] = -1.0f;
        build.light.proxy.bounds.maximum[0] = 1.0f;
        build.light.proxy.bounds.maximum[1] = 1.0f;
        build.light.proxy.bounds.maximum[2] = 1.0f;
        build.light.proxy.debugName = "bridgeLight";
        build.light.intensity = light->intensity;
        build.light.range = 10.0f;
        return true;
    }

    bool ReadBridgeTransform(const vanguard::ecs::World& world, const vanguard::ecs::EntityId entityId,
                             const vanguard::ecs::CommittedChangeKind, entities::WorldRenderState& state,
                             void* const userData) noexcept
    {
        const auto* const context = static_cast<const BridgeComponentContext*>(userData);
        const vanguard::ecs::Entity entity = world.Resolve(entityId);
        const auto* const transform = context != nullptr && entity
                                          ? static_cast<const BridgeTransform*>(ecs_get_id(
                                                world.Native(), entity.value, context->transform.id))
                                          : nullptr;
        if (transform == nullptr) return false;
        state.fields = entities::WorldRenderStateFields::TransformAndBounds;
        state.transform.row0[3] = transform->x;
        state.bounds.minimum[0] = transform->x - 1.0f;
        state.bounds.minimum[1] = -1.0f;
        state.bounds.minimum[2] = -1.0f;
        state.bounds.maximum[0] = transform->x + 1.0f;
        state.bounds.maximum[1] = 1.0f;
        state.bounds.maximum[2] = 1.0f;
        return true;
    }

    constexpr reflection::SchemaTypeId TransformType = reflection::HashSchemaName("vanguard.test.transform");
    constexpr reflection::SchemaTypeId RenderType = reflection::HashSchemaName("vanguard.test.render");
    constexpr reflection::SchemaTypeId LightType = reflection::HashSchemaName("vanguard.test.light");

    const std::array<reflection::SchemaField, 1> TransformFields{{
        reflection::MakeField("translation", reflection::builtin::Blob, reflection::ValueKind::Blob,
                              static_cast<vanguard::u32>(offsetof(TransformData, translation)),
                              static_cast<vanguard::u32>(sizeof(TransformData::translation)), alignof(vanguard::f32),
                              1, 0, reflection::FieldFlags::Required)}};
    const std::array<reflection::SchemaField, 1> RenderFields{{
        reflection::MakeField("mesh", reflection::builtin::ResourceReference,
                              reflection::ValueKind::ResourceReference,
                              static_cast<vanguard::u32>(offsetof(RenderData, mesh)), sizeof(resources::ResourceReference),
                              alignof(resources::ResourceReference), 1, 0, reflection::FieldFlags::Required)}};
    const std::array<reflection::SchemaField, 1> LightFields{{
        reflection::MakeField("intensity", reflection::builtin::F32, reflection::ValueKind::F32,
                              static_cast<vanguard::u32>(offsetof(LightData, intensity)), sizeof(vanguard::f32),
                              alignof(vanguard::f32), 1, 0, reflection::FieldFlags::Required)}};

    const reflection::Schema TransformSchema{TransformType, "vanguard.test.transform", sizeof(TransformData),
                                             alignof(TransformData), 1, 1, TransformFields.data(), 1};
    const reflection::Schema RenderSchema{RenderType, "vanguard.test.render", sizeof(RenderData),
                                          alignof(RenderData), 1, 1, RenderFields.data(), 1};
    const reflection::Schema LightSchema{LightType, "vanguard.test.light", sizeof(LightData),
                                         alignof(LightData), 1, 1, LightFields.data(), 1};

    struct PrefabResolverContext
    {
        resources::ResourceReference reference;
        const prefab::PrefabFile* file = nullptr;
    };

    const prefab::PrefabFile* ResolvePrefab(const resources::ResourceReference reference, void* const userData) noexcept
    {
        const auto* const context = static_cast<const PrefabResolverContext*>(userData);
        return context != nullptr && context->file != nullptr && reference == context->reference ? context->file : nullptr;
    }

    bool RegisterStreamingComponents(entities::ComponentRegistry& registry, void*) noexcept
    {
        return registry.Register<TransformData>(TransformSchema) && registry.Register<RenderData>(RenderSchema) &&
               registry.Register<LightData>(LightSchema);
    }

    struct CellLoaderHarness
    {
        const ByteArray* bytes = nullptr;
        const ByteArray* prefabBytes = nullptr;
        resources::ResourceReference prefab;
    };

    resources::Failure DiscoverCellDependencies(resources::ResourceReference, resources::DependencyBuilder& dependencies,
                                                 void* const userData) noexcept
    {
        const auto* const harness = static_cast<const CellLoaderHarness*>(userData);
        return harness != nullptr && harness->prefab.IsValid() &&
               dependencies.Add(harness->prefab, resources::DependencyRequirement::Required)
            ? resources::Failure::None : resources::Failure::InternalError;
    }

    resources::ResourceObject* ConstructCellResource(const resources::LoadContext& context, resources::Failure& failure,
                                                      void* const userData) noexcept
    {
        const auto* const harness = static_cast<const CellLoaderHarness*>(userData);
        if (harness == nullptr || harness->bytes == nullptr)
        {
            failure = resources::Failure::InternalError;
            return nullptr;
        }
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(world::CellResource), alignof(world::CellResource));
        if (!block)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* const resource = ::new (block.address) world::CellResource();
        const world::Result result = resource->Open(harness->bytes->Data(), harness->bytes->Size());
        if (result == world::Result::Success && resource->BindDependencies(context))
        {
            failure = resources::Failure::None;
            return resource;
        }
        resource->~CellResource();
        vanguard::memory::Free(block);
        failure = resources::Failure::DeserializationFailure;
        return nullptr;
    }

    resources::Failure DiscoverPrefabDependencies(resources::ResourceReference, resources::DependencyBuilder&, void*) noexcept
    {
        return resources::Failure::None;
    }

    resources::ResourceObject* ConstructPrefabResource(const resources::LoadContext&, resources::Failure& failure,
                                                        void* const userData) noexcept
    {
        const auto* const harness = static_cast<const CellLoaderHarness*>(userData);
        if (harness == nullptr || harness->prefabBytes == nullptr)
        {
            failure = resources::Failure::InternalError;
            return nullptr;
        }
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(prefab::PrefabResource), alignof(prefab::PrefabResource));
        if (!block)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* const resource = ::new (block.address) prefab::PrefabResource();
        if (resource->Open(harness->prefabBytes->Data(), harness->prefabBytes->Size()) == prefab::Result::Success)
        {
            failure = resources::Failure::None;
            return resource;
        }
        resource->~PrefabResource();
        vanguard::memory::Free(block);
        failure = resources::Failure::DeserializationFailure;
        return nullptr;
    }

    void DestroyPrefabResource(resources::ResourceObject* const object, void*) noexcept
    {
        auto* const resource = static_cast<prefab::PrefabResource*>(object);
        resource->~PrefabResource();
        vanguard::memory::MemoryBlock block{resource, sizeof(prefab::PrefabResource), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }

    void DestroyCellResource(resources::ResourceObject* const object, void*) noexcept
    {
        auto* const resource = static_cast<world::CellResource*>(object);
        resource->~CellResource();
        vanguard::memory::MemoryBlock block{resource, sizeof(world::CellResource), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }
}

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace ecs = vanguard::ecs;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "entitiesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");
    Check(reflection::Initialize(), "reflection initialization");
    Check(reflection::RegisterSchema(TransformSchema) && reflection::RegisterSchema(RenderSchema) &&
              reflection::RegisterSchema(LightSchema),
          "register stable component schemas");

    const resources::ResourceReference prefabReference(
        resources::ResourcePath::FromString("prefabs/materializer_test.vprefab"), prefab::PrefabResourceType);
    const resources::ResourceReference originalMesh(
        resources::ResourcePath::FromString("meshes/original.vmesh"),
        vanguard::serialization::MakeFourCC('V', 'M', 'S', 'H'));
    const resources::ResourceReference replacementMesh(
        resources::ResourcePath::FromString("meshes/replacement.vmesh"),
        vanguard::serialization::MakeFourCC('V', 'M', 'S', 'H'));
    const TransformData rootTransform{{1.0f, 2.0f, 3.0f}};
    const TransformData childTransform{{4.0f, 5.0f, 6.0f}};
    const RenderData originalRender{originalMesh};
    const RenderData replacementRender{replacementMesh};
    const LightData addedLight{25.0f};

    const std::array<prefab::EntityBuildRecord, 4> prefabEntities{{
        {10, prefab::InvalidStableId, 0x10, prefab::EntityFlags::None},
        {20, 10, 0x20, prefab::EntityFlags::DisabledByDefault},
        {30, 10, 0x30, prefab::EntityFlags::EditorOnly},
        {40, 30, 0x40, prefab::EntityFlags::None}}};
    const std::array<prefab::ComponentBuildRecord, 3> prefabComponents{{
        {100, 10, &TransformSchema, &rootTransform, prefab::ComponentFlags::None},
        {101, 10, &RenderSchema, &originalRender, prefab::ComponentFlags::DisabledByDefault},
        {200, 20, &TransformSchema, &childTransform, prefab::ComponentFlags::None}}};
    prefab::CookDescription prefabDescription;
    prefabDescription.name = 0x1234;
    prefabDescription.entities = {prefabEntities.data(), static_cast<vanguard::u32>(prefabEntities.size())};
    prefabDescription.components = {prefabComponents.data(), static_cast<vanguard::u32>(prefabComponents.size())};
    prefabDescription.sourceFingerprint = vanguard::crypto::Sha256("entity prefab", 13);
    prefabDescription.includeEditorData = true;
    ByteArray prefabBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter prefabWriter(prefabBytes);
    Check(prefab::CookPrefab(prefabDescription, prefabWriter) == prefab::Result::Success, "cook materializer prefab");
    filesystem::MemoryFileReader prefabReader(prefabBytes, 0);
    prefab::PrefabFile prefabFile;
    Check(prefabFile.Open(prefabReader) == prefab::Result::Success, "open materializer prefab");

    world::PlacementBuildRecord placement;
    placement.entityId = 1000;
    placement.prefab = prefabReference;
    placement.transform.translation[0] = 5.0f;
    placement.transform.translation[1] = 6.0f;
    placement.transform.translation[2] = 7.0f;
    world::PlacementBuildRecord inactivePlacement = placement;
    inactivePlacement.entityId = 2000;
    inactivePlacement.activationGroup = 77;
    world::PlacementBuildRecord dependencyPlacement = placement;
    dependencyPlacement.entityId = 3000;
    dependencyPlacement.activationGroup = 88;
    const std::array<world::PlacementBuildRecord, 3> placements{{placement, inactivePlacement, dependencyPlacement}};
    const std::array<world::ActivationGroupBuildRecord, 2> activationGroups{{
        {77, 0x77, world::ActivationGroupFlags::None}, {88, 0x88, world::ActivationGroupFlags::None}}};
    const std::array<world::ComponentOverrideBuildRecord, 3> overrides{{
        {1000, 101, world::OverrideMode::Replace, &RenderSchema, &replacementRender, world::OverrideFlags::None},
        {1000, 200, world::OverrideMode::Remove, nullptr, nullptr, world::OverrideFlags::None},
        {1000, 300, world::OverrideMode::Add, &LightSchema, &addedLight, world::OverrideFlags::None}}};
    constexpr vanguard::u64 RequiredWorldSlot = 0x7265717569726564ull;
    constexpr vanguard::u64 OptionalWorldSlot = 0x6f7074696f6e616cull;
    const std::array<world::EntityReferenceBuildRecord, 3> entityReferences{{
        {1000, RequiredWorldSlot, 9000, world::EntityReferenceKind::RequiredWorld},
        {1000, OptionalWorldSlot, 9001, world::EntityReferenceKind::OptionalWorld},
        {2000, 0x67726f7570646570ull, 3000, world::EntityReferenceKind::RequiredLocal}}};
    world::CellBuildDescription cellDescription;
    cellDescription.cellId = 500;
    cellDescription.worldId = 50;
    cellDescription.origin[0] = 100000.0;
    cellDescription.origin[1] = 200000.0;
    cellDescription.origin[2] = 300000.0;
    cellDescription.activationGroups = {activationGroups.data(), static_cast<vanguard::u32>(activationGroups.size())};
    cellDescription.placements = {placements.data(), static_cast<vanguard::u32>(placements.size())};
    cellDescription.overrides = {overrides.data(), static_cast<vanguard::u32>(overrides.size())};
    cellDescription.entityReferences = {entityReferences.data(), static_cast<vanguard::u32>(entityReferences.size())};
    cellDescription.sourceFingerprint = vanguard::crypto::Sha256("entity cell", 11);
    ByteArray cellBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter cellWriter(cellBytes);
    Check(world::CookCell(cellDescription, cellWriter) == world::Result::Success, "cook materializer cell");
    filesystem::MemoryFileReader cellReader(cellBytes, 0);
    world::CellFile cellFile;
    Check(cellFile.Open(cellReader) == world::Result::Success, "open materializer cell");

    world::CellBuildDescription collidingCellDescription = cellDescription;
    collidingCellDescription.cellId = 501;
    collidingCellDescription.sourceFingerprint = vanguard::crypto::Sha256("colliding entity cell", 21);
    ByteArray collidingCellBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter collidingCellWriter(collidingCellBytes);
    Check(world::CookCell(collidingCellDescription, collidingCellWriter) == world::Result::Success,
          "cook cell with a colliding placement identity");
    filesystem::MemoryFileReader collidingCellReader(collidingCellBytes, 0);
    world::CellFile collidingCellFile;
    Check(collidingCellFile.Open(collidingCellReader) == world::Result::Success,
          "open cell with a colliding placement identity");

    ecs::World emptyRegistryWorld;
    entities::ComponentRegistry emptyRegistry;
    Check(emptyRegistryWorld.Initialize() && emptyRegistry.Initialize(emptyRegistryWorld) && emptyRegistry.Seal() &&
              emptyRegistry.IsSealed() && emptyRegistry.Shutdown() && emptyRegistryWorld.Shutdown(),
          "seal an empty component registry for component-free bootstrap worlds");

    ecs::World ecsWorld;
    Check(ecsWorld.Initialize(), "initialize materialization ECS world");
    entities::ComponentRegistry componentRegistry;
    Check(componentRegistry.Initialize(ecsWorld), "initialize world-scoped component registry");
    Check(!componentRegistry.Register<TransformData>(LightSchema),
          "reject a reflection schema whose size and alignment do not match the C++ component ABI");
    Check(componentRegistry.Register<TransformData>(TransformSchema) &&
              componentRegistry.Register<RenderData>(RenderSchema) && componentRegistry.Register<LightData>(LightSchema),
          "map stable schemas to runtime Flecs components");
    Check(!componentRegistry.Register<TransformData>(TransformSchema), "reject duplicate component schema mapping");
    Check(componentRegistry.Seal() && componentRegistry.IsSealed() &&
              !componentRegistry.Register<TransformData>(TransformSchema),
          "freeze schema-to-component registration before concurrent decoding begins");

    entities::DecodedComponent decoded;
    Check(componentRegistry.Decode(TransformType, 2, prefabFile.ComponentData(prefabFile.Components()[0]), decoded) ==
              entities::Result::UnsupportedSchemaVersion,
          "reject unsupported cooked component versions before queueing");

    entities::CellMaterializer materializer;
    entities::EntityReferenceRegistry referenceRegistry;
    Check(referenceRegistry.Initialize(ecsWorld), "initialize world-scoped entity reference registry");
    Check(materializer.Initialize(componentRegistry, referenceRegistry),
          "initialize cell materializer and built-in runtime components");
    PrefabResolverContext resolver{prefabReference, &prefabFile};
    entities::MaterializationReport report;
    Check(ecsWorld.QueueCreate(1000) &&
              materializer.QueueCell(cellFile, 6, &ResolvePrefab, &resolver) == entities::Result::DuplicateEntity &&
              ecsWorld.GetStats().pendingActions == 1 && ecsWorld.GetStats().pendingComponentActions == 0,
          "materializer collision rollback preserves another producer's pending entity command");
    Check(ecsWorld.FlushActions() && ecsWorld.Resolve(1000) && ecsWorld.QueueDestroy(1000) && ecsWorld.FlushActions(),
          "release the competing producer entity before normal materialization");
    Check(materializer.QueueCell(cellFile, 7, &ResolvePrefab, &resolver, &report) == entities::Result::Success &&
              report.placements == 1 && report.placementsSkippedInactive == 2 && report.entities == 2 &&
              report.entitiesSkippedEditorOnly == 2 && report.components == 3 && report.overridesApplied == 2 &&
              report.overridesRemoved == 1 && !ecsWorld.Resolve(2000),
          "decode prefab defaults and sparse cell overrides into one pending materialization");
    Check(materializer.QueueCell(collidingCellFile, 1, &ResolvePrefab, &resolver) == entities::Result::DuplicateEntity,
          "reject an identity reserved by another cell before either cell is committed");
    Check(materializer.CompleteActivation(500, 7) == entities::Result::NotReady,
          "cell readiness cannot publish before structural commit");
    Check(ecsWorld.FlushActions(), "commit materialized entity batch");
    Check(materializer.CompleteActivation(500, 7) == entities::Result::NotReady &&
              materializer.State(500) == entities::CellState::PendingActivation,
          "entity visibility alone cannot prematurely fail or activate a component-incomplete cell");
    Check(ecsWorld.FlushComponentActions(), "commit materialized component batch");
    Check(materializer.CompleteActivation(500, 6) == entities::Result::StaleGeneration,
          "stale resource generation cannot publish cell readiness");
    Check(materializer.CompleteActivation(500, 7) == entities::Result::NotReady &&
              materializer.State(500) == entities::CellState::PendingReferences &&
              referenceRegistry.Resolve(1000, RequiredWorldSlot).state == entities::ReferenceState::Unresolved &&
              referenceRegistry.Resolve(1000, OptionalWorldSlot).state == entities::ReferenceState::Unresolved,
          "publish materialized entities while required world references wait for another streamed cell");
    Check(ecsWorld.QueueCreate(9000) && ecsWorld.FlushActions(), "materialize a later world-reference target");
    const std::array<ecs::EntityId, 1> referenceTargetEntities{{9000}};
    Check(referenceRegistry.RegisterCell(600, 1, {referenceTargetEntities.data(), 1}, {}) ==
              entities::ReferenceResult::Success &&
              referenceRegistry.Resolve(1000, RequiredWorldSlot) &&
              !referenceRegistry.Resolve(1000, OptionalWorldSlot) &&
              materializer.CompleteActivation(500, 7) == entities::Result::Success &&
              materializer.State(500) == entities::CellState::Active,
          "relink a required world reference when its target publishes without making optional absence block readiness");
    const ecs::Entity firstResolvedTarget = referenceRegistry.Resolve(1000, RequiredWorldSlot).entity;
    const vanguard::u64 firstResolutionRevision = referenceRegistry.Resolve(1000, RequiredWorldSlot).revision;
    Check(referenceRegistry.UnregisterCell(600, 1) == entities::ReferenceResult::Success &&
              referenceRegistry.Resolve(1000, RequiredWorldSlot).state == entities::ReferenceState::Unresolved &&
              materializer.State(500) == entities::CellState::PendingReferences &&
              materializer.CompleteActivation(500, 7) == entities::Result::NotReady &&
              ecsWorld.QueueDestroy(9000) && ecsWorld.FlushActions() && ecsWorld.QueueCreate(9000) &&
              ecsWorld.FlushActions(),
          "invalidate incoming references before the target entity is destroyed");
    Check(referenceRegistry.RegisterCell(600, 2, {referenceTargetEntities.data(), 1}, {}) ==
              entities::ReferenceResult::Success &&
              referenceRegistry.Resolve(1000, RequiredWorldSlot).revision > firstResolutionRevision &&
              referenceRegistry.Resolve(1000, RequiredWorldSlot).entity.value != firstResolvedTarget.value &&
              materializer.State(500) == entities::CellState::Active,
          "automatically relink waiters to the new Flecs generation when the stable target returns");

    constexpr entities::ActivationOwnerId GameplayGroupOwner = 0x1001;
    constexpr entities::ActivationOwnerId EditorGroupOwner = 0x1002;
    Check(materializer.AcquireActivationGroup(cellFile, 7, 77, GameplayGroupOwner, &ResolvePrefab, &resolver) ==
                  entities::Result::Success &&
              materializer.AcquireActivationGroup(cellFile, 7, 77, EditorGroupOwner, &ResolvePrefab, &resolver) ==
                  entities::Result::Success &&
              materializer.ActivationOwnerCount(500, 77) == 2 &&
              materializer.ActivationOwnerCount(500, 88) == 2 && !ecsWorld.Resolve(2000) && !ecsWorld.Resolve(3000),
          "coalesce overlapping activation-group owners onto one pending entity transaction");
    Check(ecsWorld.FlushActions() &&
              materializer.SynchronizeActivationGroups(500, 7) == entities::Result::NotReady &&
              ecsWorld.FlushComponentActions() &&
              materializer.SynchronizeActivationGroups(500, 7) == entities::Result::Success &&
              materializer.ActivationState(500, 77) == entities::ActivationGroupState::Active &&
              ecsWorld.Resolve(2000) && ecsWorld.Resolve(3000) &&
              referenceRegistry.Resolve(2000, 0x67726f7570646570ull),
          "publish a group and its required-local dependency closure only after both Flecs batches are observable");
    Check(materializer.ReleaseActivationGroup(500, 7, 77, GameplayGroupOwner) == entities::Result::Success &&
              materializer.ActivationOwnerCount(500, 77) == 1 &&
              materializer.ActivationOwnerCount(500, 88) == 1 && ecsWorld.Resolve(2000) && ecsWorld.Resolve(3000),
          "retain a shared activation group until its final owner releases it");
    Check(materializer.ReleaseActivationGroup(500, 7, 77, EditorGroupOwner) == entities::Result::Success &&
              materializer.ActivationState(500, 77) == entities::ActivationGroupState::PendingRelease &&
              ecsWorld.FlushActions() && ecsWorld.FlushComponentActions() &&
              materializer.SynchronizeActivationGroups(500, 7) == entities::Result::Success &&
              materializer.ActivationState(500, 77) == entities::ActivationGroupState::Inactive &&
              !ecsWorld.Resolve(2000) && !ecsWorld.Resolve(3000),
          "remove only the final owner's group entities through an exact release transaction");
    Check(materializer.AcquireActivationGroup(cellFile, 7, 77, GameplayGroupOwner, &ResolvePrefab, &resolver) ==
                  entities::Result::Success &&
              materializer.ReleaseActivationGroup(500, 7, 77, GameplayGroupOwner) == entities::Result::Success &&
              materializer.ActivationState(500, 77) == entities::ActivationGroupState::Inactive &&
              !ecsWorld.Resolve(2000) && !ecsWorld.Resolve(3000),
          "cancel an uncommitted group transaction without touching the base cell or another command batch");

    const ecs::Entity rootEntity = ecsWorld.Resolve(1000);
    const ecs::EntityId childId = entities::DeriveEntityId(1000, 20, 10);
    const ecs::Entity childEntity = ecsWorld.Resolve(childId);
    Check(rootEntity && childEntity && childId != 1000, "materialize stable root and deterministic prefab-child identities");
    {
        flecs::world native = ecs::Native(ecsWorld);
        const TransformData* const transform = native.entity(rootEntity.value).try_get<TransformData>();
        const RenderData* const render = native.entity(rootEntity.value).try_get<RenderData>();
        const LightData* const light = native.entity(rootEntity.value).try_get<LightData>();
        const entities::WorldPlacement* const worldPlacement =
            native.entity(rootEntity.value).try_get<entities::WorldPlacement>();
        const entities::EntityParent* const parent = native.entity(childEntity.value).try_get<entities::EntityParent>();
        Check(transform != nullptr && transform->translation[0] == 1.0f,
              "install schema-decoded prefab component on root entity");
        Check(render != nullptr && render->mesh == replacementMesh && light != nullptr && light->intensity == 25.0f,
              "apply Replace and Add overrides to the initial component set");
        Check(!ecs_is_enabled_id(ecsWorld.Native(), rootEntity.value, componentRegistry.RuntimeComponent(RenderType)),
              "retain DisabledByDefault component data while keeping the component inactive");
        Check(native.entity(childEntity.value).try_get<TransformData>() == nullptr,
              "apply Remove override before the child becomes live");
        Check(worldPlacement != nullptr && worldPlacement->translation[0] == 100005.0 &&
                  worldPlacement->translation[1] == 200006.0 && worldPlacement->translation[2] == 300007.0,
              "combine double-precision cell origin with compact placement translation");
        Check(parent != nullptr && parent->stableParent == 1000 &&
                  native.entity(childEntity.value).has<entities::DisabledEntity>(),
              "materialize hierarchy identity and authored disabled state");
    }

    Check(materializer.QueueRelease(500, 7) == entities::Result::Success,
          "queue cell release transaction");
    Check(ecsWorld.FlushActions(), "flush first release entities");
    Check(ecsWorld.FlushComponentActions() && materializer.CompleteRelease(500, 7) == entities::Result::Success,
          "release a cell through reverse entity destruction and explicit completion");
    Check(!ecsWorld.Resolve(1000) && !ecsWorld.Resolve(childId), "cell release invalidates every runtime generation");
    Check(referenceRegistry.Resolve(1000, RequiredWorldSlot).state == entities::ReferenceState::Undefined &&
              referenceRegistry.UnregisterCell(600, 2) == entities::ReferenceResult::Success &&
              ecsWorld.QueueDestroy(9000) && ecsWorld.FlushActions(),
          "remove outgoing slots on source unload and retire the independently streamed target");

    Check(materializer.QueueCell(cellFile, 8, &ResolvePrefab, &resolver) == entities::Result::Success &&
              materializer.Cancel(500, 8) == entities::Result::Success &&
              ecsWorld.GetStats().pendingActions == 0 && ecsWorld.GetStats().pendingComponentActions == 0,
          "cancel uncommitted materialization without exposing partial entities");
    Check(materializer.QueueCell(cellFile, 9, &ResolvePrefab, &resolver) == entities::Result::Success &&
              ecsWorld.FlushActions() && ecsWorld.FlushComponentActions() &&
              materializer.Cancel(500, 9) == entities::Result::Success && ecsWorld.FlushActions() &&
              ecsWorld.FlushComponentActions() && materializer.CompleteRelease(500, 9) == entities::Result::Success,
          "cancellation after commit becomes an explicit release transaction");

    Check(materializer.GetStats().queuedEntities == 14 && materializer.GetStats().releasedEntities == 8 &&
              materializer.GetStats().cancelledCells == 2,
          "materializer telemetry distinguishes activation, release and cancellation");

    world::PlacementBuildRecord localSourcePlacement = placement;
    localSourcePlacement.entityId = 3000;
    world::PlacementBuildRecord localTargetPlacement = inactivePlacement;
    localTargetPlacement.entityId = 4000;
    const std::array<world::PlacementBuildRecord, 2> localPlacements{{localSourcePlacement, localTargetPlacement}};
    const std::array<world::EntityReferenceBuildRecord, 1> localReferences{{
        {3000, 0x6c6f63616cull, 4000, world::EntityReferenceKind::RequiredLocal}}};
    world::CellBuildDescription localCellDescription = cellDescription;
    localCellDescription.cellId = 502;
    localCellDescription.placements = {localPlacements.data(), static_cast<vanguard::u32>(localPlacements.size())};
    localCellDescription.overrides = {};
    localCellDescription.entityReferences = {localReferences.data(), static_cast<vanguard::u32>(localReferences.size())};
    localCellDescription.sourceFingerprint = vanguard::crypto::Sha256("required local cell", 19);
    ByteArray localCellBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter localCellWriter(localCellBytes);
    Check(world::CookCell(localCellDescription, localCellWriter) == world::Result::Success,
          "cook a required-local activation-closure cell");
    filesystem::MemoryFileReader localCellReader(localCellBytes, 0);
    world::CellFile localCellFile;
    Check(localCellFile.Open(localCellReader) == world::Result::Success, "open required-local activation-closure cell");
    entities::MaterializationReport localReport;
    Check(materializer.QueueCell(localCellFile, 1, &ResolvePrefab, &resolver, &localReport) == entities::Result::Success &&
              localReport.placements == 2 && localReport.placementsSkippedInactive == 0 &&
              ecsWorld.FlushActions() && ecsWorld.FlushComponentActions() &&
              materializer.CompleteActivation(502, 1) == entities::Result::Success &&
              ecsWorld.Resolve(3000) && ecsWorld.Resolve(4000) && referenceRegistry.Resolve(3000, 0x6c6f63616cull),
          "pull a required local target into the materialization transaction even when its group is initially inactive");
    Check(materializer.QueueRelease(502, 1) == entities::Result::Success && ecsWorld.FlushActions() &&
              ecsWorld.FlushComponentActions() && materializer.CompleteRelease(502, 1) == entities::Result::Success,
          "release a required-local reference cell without retaining registry state");

    world::CellBuildDescription soakCellDescription = cellDescription;
    soakCellDescription.entityReferences = {};
    soakCellDescription.sourceFingerprint = vanguard::crypto::Sha256("materializer soak cell", 22);
    ByteArray soakCellBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter soakCellWriter(soakCellBytes);
    Check(world::CookCell(soakCellDescription, soakCellWriter) == world::Result::Success,
          "cook reference-independent materializer soak cell");
    filesystem::MemoryFileReader soakCellReader(soakCellBytes, 0);
    world::CellFile soakCellFile;
    Check(soakCellFile.Open(soakCellReader) == world::Result::Success,
          "open reference-independent materializer soak cell");

    memory::PoolMetrics worldPoolBeforeSoak;
    Check(memory::GetPoolMetrics(memory::PoolId::World, worldPoolBeforeSoak),
          "capture World pool baseline before repeated streaming");
    bool soakSucceeded = true;
    for (vanguard::u32 cycle = 0; cycle < 64; ++cycle)
    {
        const vanguard::u32 generation = 10 + cycle;
        soakSucceeded = materializer.QueueCell(soakCellFile, generation, &ResolvePrefab, &resolver) == entities::Result::Success &&
                        ecsWorld.FlushActions() && ecsWorld.FlushComponentActions() &&
                        materializer.CompleteActivation(500, generation) == entities::Result::Success &&
                        materializer.QueueRelease(500, generation) == entities::Result::Success &&
                        ecsWorld.FlushActions() && ecsWorld.FlushComponentActions() &&
                        materializer.CompleteRelease(500, generation) == entities::Result::Success && soakSucceeded;
    }
    memory::PoolMetrics worldPoolAfterSoak;
    Check(soakSucceeded && memory::GetPoolMetrics(memory::PoolId::World, worldPoolAfterSoak) &&
              materializer.GetStats().trackedCells == 0 && ecsWorld.GetStats().commandBatches == 0 &&
              worldPoolAfterSoak.allocationCount == worldPoolBeforeSoak.allocationCount &&
              worldPoolAfterSoak.allocatedBytes == worldPoolBeforeSoak.allocatedBytes,
          "repeated cell activation and release leaves no records, command batches, or retained World allocations");

    const resources::ResourceReference streamedCellReference(
        resources::ResourcePath::FromString("world/runtime/materialized.vcell"), world::CellResourceType);
    entities::CellDecoderContext cellDecoderContext;
    const vanguard::streaming::DecoderDescriptor cellDecoder = entities::MakeCellDecoder(cellDecoderContext);
    Check(cellDecoder.IsValid() && cellDecoder.type == world::CellResourceType,
          "provide a production ResourceStreamer decoder for validated vcell resources");
    world::WorldCellBuildRecord streamedCell;
    streamedCell.cellId = 500;
    streamedCell.cell = streamedCellReference;
    streamedCell.bounds.minimum[0] = -100.0;
    streamedCell.bounds.minimum[1] = -100.0;
    streamedCell.bounds.minimum[2] = -100.0;
    streamedCell.bounds.maximum[0] = 100.0;
    streamedCell.bounds.maximum[1] = 100.0;
    streamedCell.bounds.maximum[2] = 100.0;
    streamedCell.activationDistance = 100.0f;
    streamedCell.retentionDistance = 150.0f;
    world::WorldCellBuildRecord mismatchedStreamedCell = streamedCell;
    mismatchedStreamedCell.cellId = 501;
    mismatchedStreamedCell.streamingReferencePoint[0] = 2000.0;
    mismatchedStreamedCell.bounds.minimum[0] = 1900.0;
    mismatchedStreamedCell.bounds.maximum[0] = 2100.0;
    const std::array<world::WorldCellBuildRecord, 2> streamedCells{{streamedCell, mismatchedStreamedCell}};
    world::WorldBuildDescription streamedWorldDescription;
    streamedWorldDescription.worldId = 0x53545245414d4544ull;
    streamedWorldDescription.bounds = streamedCell.bounds;
    streamedWorldDescription.bounds.maximum[0] = 2100.0;
    streamedWorldDescription.cells = {streamedCells.data(), static_cast<vanguard::u32>(streamedCells.size())};
    streamedWorldDescription.sourceFingerprint = vanguard::crypto::Sha256("entity streaming world", 22);
    ByteArray streamedWorldBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter streamedWorldWriter(streamedWorldBytes);
    Check(world::CookWorld(streamedWorldDescription, streamedWorldWriter) == world::Result::Success,
          "cook entity-streaming integration world");
    filesystem::MemoryFileReader streamedWorldReader(streamedWorldBytes, 0);
    world::WorldFile streamedWorld;
    Check(streamedWorld.Open(streamedWorldReader) == world::Result::Success,
          "open entity-streaming integration world");

    resources::ResourceRegistry streamedRegistry;
    resources::ResourcePipeline streamedPipeline;
    CellLoaderHarness cellLoaderHarness{&soakCellBytes, &prefabBytes, prefabReference};
    const resources::AsyncLoaderDescriptor streamedCellLoader{
        world::CellResourceType, "entity streaming cell loader", &DiscoverCellDependencies,
        &ConstructCellResource, &DestroyCellResource, &cellLoaderHarness};
    const resources::AsyncLoaderDescriptor streamedPrefabLoader{
        prefab::PrefabResourceType, "entity streaming prefab loader", &DiscoverPrefabDependencies,
        &ConstructPrefabResource, &DestroyPrefabResource, &cellLoaderHarness};
    Check(streamedRegistry.Initialize() && streamedPipeline.Initialize(streamedRegistry) &&
              streamedPipeline.RegisterLoader(streamedCellLoader) && streamedPipeline.RegisterLoader(streamedPrefabLoader),
          "initialize asynchronous cell and retained-prefab resource pipeline");
    world::WorldStreamingGrid streamedGrid;
    world::WorldStreamingExecutor streamedExecutor;
    Check(streamedGrid.Initialize(streamedWorld) && streamedExecutor.Initialize(streamedGrid, streamedPipeline),
          "initialize world selector and resource executor for entity streaming");

    entities::CellStreamingSystemConfig streamingSystemConfig;
    streamingSystemConfig.executor = &streamedExecutor;
    streamingSystemConfig.registerComponents = &RegisterStreamingComponents;
    streamingSystemConfig.resolvePrefab = nullptr;
    entities::CellStreamingSystem streamingSystem(streamingSystemConfig);
    vanguard::game::GameWorld streamedGameWorld;
    Check(streamedGameWorld.RegisterSystem(streamingSystem) && streamedGameWorld.Initialize(),
          "initialize cell streaming as a GameWorld runtime system");
    world::StreamingObserver streamedObserver;
    world::StreamingProcessInput streamedInput;
    streamedInput.observers = {&streamedObserver, 1};
    Check(streamingSystem.SetProcessInput(streamedInput), "publish the initial streaming observer snapshot");
    bool streamedIn = false;
    for (vanguard::u32 attempt = 0; attempt < 500 && !streamedIn; ++attempt)
    {
        Check(streamedGameWorld.Tick(1.0f / 60.0f), "tick asynchronous cell materialization");
        streamedIn = static_cast<bool>(streamedGameWorld.Entities().Resolve(1000));
        if (!streamedIn) vanguard::concurrency::SleepOnCurrentThread(1);
    }
    Check(streamedIn && streamedGrid.State({500, world::StreamingNodeKind::Cell}) == world::StreamingNodeState::Streamed &&
              streamedGrid.IsRenderReady({500, world::StreamingNodeKind::Cell}) &&
              streamingSystem.GetStats().activatedCells == 1 &&
              streamingSystem.References()->ContainsEntity(1000),
          "load vcell, commit Flecs entities, publish stable references and acknowledge readiness in one epilogue");

    streamedObserver.predictedPosition[0] = 1000.0;
    streamedInput.cameraPosition[0] = 1000.0;
    streamedInput.observers = {&streamedObserver, 1};
    Check(streamingSystem.SetProcessInput(streamedInput), "move the streaming observer outside retention range");
    bool streamedOut = false;
    for (vanguard::u32 attempt = 0; attempt < 100 && !streamedOut; ++attempt)
    {
        Check(streamedGameWorld.Tick(1.0f / 60.0f), "tick ordered cell detachment");
        streamedOut = !streamedGameWorld.Entities().Resolve(1000) &&
                      streamedGrid.State({500, world::StreamingNodeKind::Cell}) == world::StreamingNodeState::Unloaded;
    }
    Check(streamedOut && streamingSystem.GetStats().releasedCells == 1 &&
              streamingSystem.GetStats().trackedCells == 0,
          "unregister references, destroy Flecs entities and release the resource through the detach epilogue");

    streamedObserver.predictedPosition[0] = 2000.0;
    streamedInput.cameraPosition[0] = 2000.0;
    streamedInput.observers = {&streamedObserver, 1};
    Check(streamingSystem.SetProcessInput(streamedInput), "request a world node with mismatched cell contents");
    bool downstreamRejected = false;
    for (vanguard::u32 attempt = 0; attempt < 500 && !downstreamRejected; ++attempt)
    {
        Check(streamedGameWorld.Tick(1.0f / 60.0f), "tick downstream cell validation failure");
        downstreamRejected = streamedGrid.State({501, world::StreamingNodeKind::Cell}) ==
                             world::StreamingNodeState::Failed;
        if (!downstreamRejected) vanguard::concurrency::SleepOnCurrentThread(1);
    }
    Check(downstreamRejected && !streamedGameWorld.Entities().Resolve(1000) &&
              streamedExecutor.LastFailure({501, world::StreamingNodeKind::Cell}) ==
                  resources::Failure::DeserializationFailure &&
              streamingSystem.GetStats().downstreamFailures == 1,
          "reject mismatched loaded cell identity without publishing any Flecs state");
    streamedObserver.predictedPosition[0] = 1000.0;
    streamedInput.cameraPosition[0] = 1000.0;
    streamedInput.observers = {&streamedObserver, 1};
    Check(streamingSystem.SetProcessInput(streamedInput) && streamedGameWorld.Tick(0.0f) &&
              streamedGrid.State({501, world::StreamingNodeKind::Cell}) == world::StreamingNodeState::Unloaded,
          "retire downstream-failed residency after it leaves the desired set");
    Check(streamedGameWorld.Shutdown() && streamedExecutor.Shutdown() && streamedGrid.Shutdown() &&
              streamedPipeline.Shutdown() && streamedRegistry.Shutdown(),
          "shutdown a fully drained entity-streaming world");
    Check(vanguard::jobs::Shutdown(), "shutdown Jobs after entity streaming drains");
    streamedWorld.Close();

    {
        vanguard::ecs::World bridgeWorld;
        Check(bridgeWorld.Initialize(), "initialize world-render bridge ECS world");
        BridgeComponentContext bridgeComponents;
        bridgeComponents.light = vanguard::ecs::RegisterComponent<BridgeLight>(bridgeWorld, true);
        bridgeComponents.transform = vanguard::ecs::RegisterComponent<BridgeTransform>(bridgeWorld, true);
        Check(bridgeComponents.light && bridgeComponents.transform,
              "register project-defined world-render bridge components");

        vanguard::rendering::RenderSceneManager scenes;
        vanguard::rendering::RenderSceneFailure sceneFailure;
        vanguard::rendering::RenderSceneHandle scene;
        vanguard::rendering::RenderSceneDesc sceneDesc;
        sceneDesc.name = "entityBridgeTest";
        sceneDesc.maximumProxies = 64;
        sceneDesc.maximumPendingProxyMutations = 2;
        Check(scenes.Initialize({}, &sceneFailure) && scenes.CreateScene(sceneDesc, scene, &sceneFailure),
              "create the world-render bridge target scene");

        entities::WorldRenderBridge bridge;
        entities::WorldRenderBridgeFailure bridgeFailure;
        Check(bridge.Initialize(bridgeWorld, scenes, scene, 7, {}, &bridgeFailure) &&
                  bridge.RegisterContributor({bridgeComponents.light.id, bridgeComponents.light.world,
                                              entities::WorldRenderContributorKind::Light,
                                              &BuildBridgeLight, &bridgeComponents}, &bridgeFailure) &&
                  bridge.RegisterState({bridgeComponents.transform.id, bridgeComponents.transform.world,
                                        entities::WorldRenderStateFields::TransformAndBounds,
                                        &ReadBridgeTransform, &bridgeComponents}, &bridgeFailure),
              "initialize component-neutral world-render translation descriptors");

        const vanguard::ecs::CommandBatch activation = bridgeWorld.BeginCommandBatch();
        Check(activation && bridgeWorld.QueueCreate(activation, 9001) &&
                  bridgeWorld.QueueAddComponent(activation, 9001, bridgeComponents.light) &&
                  bridgeWorld.QueueSetComponent(activation, 9001, bridgeComponents.light, BridgeLight{4.0f}) &&
                  bridgeWorld.QueueAddComponent(activation, 9001, bridgeComponents.transform) &&
                  bridgeWorld.QueueSetComponent(activation, 9001, bridgeComponents.transform, BridgeTransform{2.0f}) &&
                  bridgeWorld.QueueSetComponent(activation, 9001, bridgeComponents.transform, BridgeTransform{3.0f}) &&
                  bridgeWorld.SealCommandBatch(activation) && bridgeWorld.FlushActions() &&
                  bridgeWorld.FlushComponentActions(),
              "commit one activation-group component transaction");
        entities::WorldRenderBridgeFlushResult bridgeFlush;
        vanguard::rendering::RenderProxyHandle bridgeProxy;
        Check(bridge.Flush(bridgeFlush, &bridgeFailure) && bridgeFlush.sceneCommitAllowed &&
                  bridgeFlush.createdProxies == 1 && bridgeFlush.updatedProxies == 1 &&
                  bridge.FindProxy(9001, bridgeComponents.light.id, bridgeProxy),
              "coalesce repeated component writes and admit one matching proxy transaction");
        vanguard::rendering::RenderProxySnapshot proxySnapshot;
        Check(scenes.SnapshotProxy(bridgeProxy, proxySnapshot) && proxySnapshot.transform.row0[3] == 3.0f &&
                  proxySnapshot.producerId == 9001 && proxySnapshot.producerGeneration == 7,
              "publish stable entity identity, bridge generation and the final transform value");
        entities::WorldRenderBridgeValidationReport bridgeValidation;
        Check(bridge.ValidateFullRebuild(bridgeValidation, &bridgeFailure) && bridgeValidation.valid &&
                  bridgeValidation.scannedWorldContributors == 1 && bridgeValidation.trackedContributors == 1,
              "validate the dirty-stream result against a cold full-world contributor scan");

        vanguard::rendering::RenderSceneFramePrepareResult prepare;
        vanguard::rendering::RenderSceneCommitResult firstCommit;
        Check(scenes.PrepareSceneFrame(scene, prepare, &sceneFailure) &&
                  scenes.CommitScene(scene, firstCommit, &sceneFailure),
              "publish the activated proxy scene version");
        vanguard::rendering::SceneReadLease retainedLease;
        Check(scenes.AcquireLatestReadLease(scene, retainedLease, &sceneFailure),
              "retain the proxy-bearing scene version across cell release");

        Check(bridgeWorld.RetireCommandBatch(activation),
              "retire the completed activation transaction before release");
        Check(bridgeWorld.QueueDestroy(9001) && bridgeWorld.FlushActions() &&
                  bridgeWorld.FlushComponentActions() && bridge.Flush(bridgeFlush, &bridgeFailure) &&
                  bridgeFlush.destroyedProxies == 1 && !bridge.FindProxy(9001, bridgeComponents.light.id, bridgeProxy),
              "translate exact entity destruction into proxy detachment without a table scan");
        vanguard::rendering::RenderSceneCommitResult emptyCommit;
        Check(scenes.PrepareSceneFrame(scene, prepare, &sceneFailure) &&
                  scenes.CommitScene(scene, emptyCommit, &sceneFailure),
              "publish the scene version with the released entity removed");
        vanguard::containers::DynamicArray<entities::WorldRenderDetachedProxy> detached(
            vanguard::memory::pools::Rendering::GetInstance());
        Check(bridge.RetireDetachedProxies(detached, &bridgeFailure) && detached.Empty(),
              "withhold downstream release while a reader retains the proxy-bearing version");
        vanguard::rendering::RenderSceneVersionRetirementResult retirement;
        Check(scenes.ReleaseReadLease(retainedLease, &sceneFailure) &&
                  scenes.RetirePublishedVersions(retirement, &sceneFailure) &&
                  bridge.RetireDetachedProxies(detached, &bridgeFailure) && detached.Size() == 1 &&
                  detached[0].entity == 9001 && detached[0].bridgeGeneration == 7,
              "acknowledge proxy release only after published-version retirement");
        Check(bridge.Shutdown(&bridgeFailure) && scenes.DestroyScene(scene, &sceneFailure) &&
                  scenes.Shutdown(&sceneFailure) && bridgeWorld.Shutdown(),
              "shutdown the drained world-render bridge path");
    }

    Check(materializer.Shutdown(), "shutdown empty materializer");
    Check(referenceRegistry.Shutdown(), "shutdown empty entity reference registry");
    Check(componentRegistry.Shutdown(), "shutdown component registry");
    Check(ecsWorld.Shutdown(), "shutdown empty ECS world");

    collidingCellFile.Close();
    localCellFile.Close();
    soakCellFile.Close();
    cellFile.Close();
    prefabFile.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    std::printf("entitiesTests: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
