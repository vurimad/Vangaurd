#include "../../../tests/support/scratch_directory.hpp"
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/engine/streaming_observer_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/engine/game_world_service_detail.hpp>
#include <vanguard/engine/rendering_service_detail.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/component_registry.hpp>
#include <vanguard/entities/component_runtime.hpp>
#include <vanguard/entities/materializer.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/entities/scene_components.hpp>
#include <vanguard/entities/transform_runtime.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/game_input/mapping_resource.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/shaders/shaders.hpp>
#include <vanguard/world/worlds.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
    int g_failures = 0;
    inline constexpr const char* WorldSessionObserverChildMode = "--world-session-observer-child";
    inline constexpr const char* WorldSessionFirstFaultChildMode = "--world-session-first-fault-child";
    inline constexpr const char* WorldSessionPartialStartChildMode = "--world-session-partial-start-child";

    struct LifecycleTrace
    {
        vanguard::application::LifecycleEvent events[256]{};
        vanguard::u32 count = 0;
    };

    struct FakeFrameClock
    {
        vanguard::u64 ticks = 1'000;
    };

    struct FrameTrace
    {
        vanguard::engine::FrameParticipantId participants[32]{};
        vanguard::engine::FrameContext contexts[32]{};
        vanguard::u32 count = 0;
    };

    struct FrameParticipantFixture
    {
        FrameTrace* trace = nullptr;
        vanguard::engine::FrameParticipantId id = vanguard::engine::InvalidFrameParticipantId;
        bool fail = false;
    };

    class VisualReleaseProbe final : public vanguard::entities::VisualComponent
    {
    public:
        [[nodiscard]] bool Admit(vanguard::entities::RenderingRuntime& runtime, vanguard::rendering::RenderProxyDesc desc) noexcept
        {
            return BeginProxyAdmission(runtime, desc);
        }

        [[nodiscard]] bool Release(vanguard::rendering::RenderSceneFailure* const failure = nullptr) noexcept
        {
            return ReleaseVisualProxy(true, failure);
        }
    };

    [[nodiscard]] vanguard::resources::Failure DiscoverDistantProxyTestMesh(
        vanguard::resources::ResourceReference, vanguard::resources::DependencyBuilder&, void*) noexcept
    {
        return vanguard::resources::Failure::None;
    }

    [[nodiscard]] vanguard::resources::ResourceObject* ConstructDistantProxyTestMesh(
        const vanguard::resources::LoadContext&, vanguard::resources::Failure& failure, void*) noexcept
    {
        failure = vanguard::resources::Failure::None;
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Resources, sizeof(vanguard::meshes::MeshResourceObject),
                                      alignof(vanguard::meshes::MeshResourceObject));
        if (!block)
        {
            failure = vanguard::resources::Failure::OutOfMemory;
            return nullptr;
        }
        return ::new (block.address) vanguard::meshes::MeshResourceObject();
    }

    void DestroyDistantProxyTestMesh(vanguard::resources::ResourceObject* const resource, void*) noexcept
    {
        auto* const mesh = static_cast<vanguard::meshes::MeshResourceObject*>(resource);
        if (mesh == nullptr)
            return;
        mesh->~MeshResourceObject();
        vanguard::memory::MemoryBlock block{mesh, sizeof(vanguard::meshes::MeshResourceObject), vanguard::memory::PoolId::Resources};
        vanguard::memory::Free(block);
    }

    class EmptyInputBackend final : public vanguard::input::IInputBackend
    {
    public:
        [[nodiscard]] vanguard::input::BackendDrainResult Drain(vanguard::input::RawEvent*, vanguard::u32) noexcept override
        {
            return {};
        }
        [[nodiscard]] bool SetRumble(vanguard::input::DeviceId, vanguard::f32, vanguard::f32, vanguard::u32) noexcept override
        {
            return false;
        }
        void RequestDeviceRefresh() noexcept override {}
    };

    [[nodiscard]] vanguard::u64 ReadFakeFrameClock(void* const userData) noexcept
    {
        return static_cast<FakeFrameClock*>(userData)->ticks;
    }

    [[nodiscard]] vanguard::engine::FrameParticipantStatus RecordFrameParticipant(const vanguard::engine::FrameContext& context, void* const userData) noexcept
    {
        auto& fixture = *static_cast<FrameParticipantFixture*>(userData);
        if (fixture.fail)
            return vanguard::engine::FrameParticipantStatus::Failure("requested participant failure");
        if (fixture.trace != nullptr && fixture.trace->count < 32)
        {
            fixture.trace->participants[fixture.trace->count] = fixture.id;
            fixture.trace->contexts[fixture.trace->count] = context;
            ++fixture.trace->count;
        }
        return vanguard::engine::FrameParticipantStatus::Success();
    }

    struct FrameParticipantSearch
    {
        vanguard::engine::FrameParticipantId id = vanguard::engine::InvalidFrameParticipantId;
        vanguard::engine::FramePhase phase = vanguard::engine::FramePhase::PlatformEvents;
        bool found = false;
    };

    void FindFrameParticipantStats(const vanguard::engine::FrameParticipantStats& stats, void* const userData) noexcept
    {
        auto* const search = static_cast<FrameParticipantSearch*>(userData);
        if (search != nullptr && stats.id == search->id)
        {
            search->found = true;
            search->phase = stats.phase;
        }
    }

    [[nodiscard]] bool BuildWorldSessionPackage(vanguard::containers::DynamicArray<vanguard::u8>& output, const bool validWorld = false,
                                                const bool validInput = true) noexcept
    {
        output.Clear();
        vanguard::filesystem::MemoryFileWriter file(output);
        vanguard::packages::PackageWriter writer;
        vanguard::packages::BuildOptions options;
        options.packageId = 0x5753455353494f4eull;
        options.buildId = 0x202608040001ull;
        options.dataAlignmentLog2 = 4;

        vanguard::packages::PackageSetBuild packageSet;
        packageSet.gameId = 0x56414e4755415244ull;
        packageSet.targetPlatformId = vanguard::serialization::MakeFourCC('W', 'I', 'N', '6');
        packageSet.startupWorld = vanguard::packages::HashResourcePath("world/session_test.vworld");
        packageSet.startupWorldType = vanguard::world::WorldResourceType;
        packageSet.defaultInput = vanguard::packages::HashResourcePath("input/default.vinput");
        packageSet.defaultInputType = vanguard::game_input::MappingResourceType;
        if (writer.Begin(file, options, packageSet) != vanguard::packages::Result::Success)
            return false;

        constexpr vanguard::u8 InvalidWorldPayload[16]{};
        const void* worldPayload = InvalidWorldPayload;
        vanguard::usize worldPayloadSize = sizeof(InvalidWorldPayload);
        vanguard::containers::DynamicArray<vanguard::u8> cookedWorld(vanguard::memory::pools::Serialization::GetInstance());
        if (validWorld)
        {
            vanguard::world::WorldCellBuildRecord cell;
            cell.cellId = 1;
            cell.cell = vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("world/session_test.vcell"),
                                                               vanguard::world::CellResourceType);
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
            {
                cell.bounds.minimum[axis] = -1.0;
                cell.bounds.maximum[axis] = 1.0;
            }
            cell.activationDistance = 1.0f;
            cell.retentionDistance = 2.0f;
            vanguard::world::WorldBuildDescription description;
            description.worldId = 0x5032574f524c4401ull;
            description.bounds = cell.bounds;
            description.cells = {&cell, 1};
            vanguard::filesystem::MemoryFileWriter worldFile(cookedWorld);
            if (vanguard::world::CookWorld(description, worldFile) != vanguard::world::Result::Success)
                return false;
            worldPayload = cookedWorld.Data();
            worldPayloadSize = cookedWorld.Size();
        }
        const vanguard::packages::BuildSegment segment{worldPayload, worldPayloadSize, vanguard::packages::Codec::None, 4,
                                                       vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource world;
        world.path = "world/session_test.vworld";
        world.type = vanguard::world::WorldResourceType;
        world.flags = vanguard::packages::ResourceFlags::Startup;
        world.segments = {&segment, 1};
        constexpr vanguard::u8 InvalidInputPayload[16]{};
        const void* inputPayload = InvalidInputPayload;
        vanguard::usize inputPayloadSize = sizeof(InvalidInputPayload);
        vanguard::containers::DynamicArray<vanguard::u8> mappingBytes(vanguard::memory::pools::Serialization::GetInstance());
        if (validInput)
        {
            vanguard::filesystem::MemoryFileWriter mappingFile(mappingBytes);
            const vanguard::game_input::MappingBuildDescription mapping;
            if (vanguard::game_input::CookMapping(mapping, mappingFile) != vanguard::game_input::MappingResult::Success)
                return false;
            inputPayload = mappingBytes.Data();
            inputPayloadSize = mappingBytes.Size();
        }
        const vanguard::packages::BuildSegment inputSegment{inputPayload, inputPayloadSize, vanguard::packages::Codec::None, 4,
                                                           vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource input;
        input.path = "input/default.vinput";
        input.type = vanguard::game_input::MappingResourceType;
        input.flags = vanguard::packages::ResourceFlags::Startup;
        input.segments = {&inputSegment, 1};
        return writer.Add(world) == vanguard::packages::Result::Success && writer.Add(input) == vanguard::packages::Result::Success && writer.Finalize() == vanguard::packages::Result::Success;
    }

    [[nodiscard]] bool BuildMissingCameraSessionPackage(vanguard::containers::DynamicArray<vanguard::u8>& output) noexcept
    {
        output.Clear();
        constexpr vanguard::u64 CellId = 1;
        constexpr vanguard::u64 AuthoredEntityId = 10;
        constexpr vanguard::u64 MissingCameraEntityId = 999;
        constexpr vanguard::u64 MissingCameraComponentId = 77;

        const vanguard::resources::ResourceReference prefabReference(
            vanguard::resources::ResourcePath::FromString("world/session_camera_failure.vprefab"), vanguard::prefabs::PrefabResourceType);
        const vanguard::prefabs::EntityBuildRecord prefabEntity{1};
        vanguard::prefabs::CookDescription prefabDescription;
        prefabDescription.name = 0x503243414d464149ull;
        prefabDescription.entities = {&prefabEntity, 1};
        vanguard::containers::DynamicArray<vanguard::u8> prefabBytes(vanguard::memory::pools::Serialization::GetInstance());
        vanguard::filesystem::MemoryFileWriter prefabWriter(prefabBytes);
        if (vanguard::prefabs::CookPrefab(prefabDescription, prefabWriter) != vanguard::prefabs::Result::Success)
            return false;

        vanguard::world::PlacementBuildRecord placement;
        placement.entityId = AuthoredEntityId;
        placement.prefab = prefabReference;
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            placement.bounds.minimum[axis] = -1.0f;
            placement.bounds.maximum[axis] = 1.0f;
        }
        vanguard::world::CellBuildDescription cellDescription;
        cellDescription.cellId = CellId;
        cellDescription.worldId = 0x503243414d46414aull;
        cellDescription.bounds = placement.bounds;
        cellDescription.placements = {&placement, 1};
        vanguard::containers::DynamicArray<vanguard::u8> cellBytes(vanguard::memory::pools::World::GetInstance());
        vanguard::filesystem::MemoryFileWriter cellWriter(cellBytes);
        if (vanguard::world::CookCell(cellDescription, cellWriter) != vanguard::world::Result::Success)
            return false;

        const vanguard::resources::ResourceReference cellReference(
            vanguard::resources::ResourcePath::FromString("world/session_camera_failure.vcell"), vanguard::world::CellResourceType);
        vanguard::world::WorldCellBuildRecord worldCell;
        worldCell.cellId = CellId;
        worldCell.cell = cellReference;
        worldCell.bounds.minimum[0] = worldCell.bounds.minimum[1] = worldCell.bounds.minimum[2] = -2.0;
        worldCell.bounds.maximum[0] = worldCell.bounds.maximum[1] = worldCell.bounds.maximum[2] = 2.0;
        worldCell.flags = vanguard::world::WorldCellFlags::AlwaysLoaded;
        vanguard::world::WorldBuildDescription worldDescription;
        worldDescription.worldId = cellDescription.worldId;
        worldDescription.bounds = worldCell.bounds;
        worldDescription.startupCamera = {CellId, MissingCameraEntityId, MissingCameraComponentId};
        worldDescription.cells = {&worldCell, 1};
        vanguard::containers::DynamicArray<vanguard::u8> worldBytes(vanguard::memory::pools::World::GetInstance());
        vanguard::filesystem::MemoryFileWriter worldWriter(worldBytes);
        if (vanguard::world::CookWorld(worldDescription, worldWriter) != vanguard::world::Result::Success)
            return false;

        vanguard::filesystem::MemoryFileWriter packageFile(output);
        vanguard::packages::PackageWriter writer;
        vanguard::packages::BuildOptions options;
        options.packageId = 0x503243414d504143ull;
        options.buildId = 0x202609170002ull;
        options.dataAlignmentLog2 = 4;
        vanguard::packages::PackageSetBuild packageSet;
        packageSet.gameId = 0x56414e4755415244ull;
        packageSet.targetPlatformId = vanguard::serialization::MakeFourCC('W', 'I', 'N', '6');
        packageSet.startupWorld = vanguard::packages::HashResourcePath("world/session_camera_failure.vworld");
        packageSet.startupWorldType = vanguard::world::WorldResourceType;
        packageSet.defaultInput = vanguard::packages::HashResourcePath("input/default.vinput");
        packageSet.defaultInputType = vanguard::game_input::MappingResourceType;
        if (writer.Begin(packageFile, options, packageSet) != vanguard::packages::Result::Success)
            return false;

        const vanguard::packages::BuildSegment worldSegment{worldBytes.Data(), worldBytes.Size(), vanguard::packages::Codec::None, 4,
                                                            vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource worldResource;
        worldResource.path = "world/session_camera_failure.vworld";
        worldResource.type = vanguard::world::WorldResourceType;
        worldResource.flags = vanguard::packages::ResourceFlags::Startup;
        worldResource.segments = {&worldSegment, 1};

        const vanguard::packages::BuildSegment cellSegment{cellBytes.Data(), cellBytes.Size(), vanguard::packages::Codec::None, 4,
                                                           vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource cellResource;
        cellResource.path = "world/session_camera_failure.vcell";
        cellResource.type = vanguard::world::CellResourceType;
        cellResource.flags = vanguard::packages::ResourceFlags::Streamable;
        cellResource.segments = {&cellSegment, 1};
        const vanguard::packages::Dependency prefabDependency{
            vanguard::packages::HashResourcePath("world/session_camera_failure.vprefab"),
            vanguard::prefabs::PrefabResourceType,
            vanguard::resources::DependencyKind::Required};
        cellResource.dependencies = {&prefabDependency, 1};

        const vanguard::packages::BuildSegment prefabSegment{prefabBytes.Data(), prefabBytes.Size(), vanguard::packages::Codec::None, 4,
                                                             vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource prefabResource;
        prefabResource.path = "world/session_camera_failure.vprefab";
        prefabResource.type = vanguard::prefabs::PrefabResourceType;
        prefabResource.flags = vanguard::packages::ResourceFlags::Streamable;
        prefabResource.segments = {&prefabSegment, 1};

        vanguard::containers::DynamicArray<vanguard::u8> mappingBytes(vanguard::memory::pools::Serialization::GetInstance());
        vanguard::filesystem::MemoryFileWriter mappingWriter(mappingBytes);
        const vanguard::game_input::MappingBuildDescription mappingDescription;
        if (vanguard::game_input::CookMapping(mappingDescription, mappingWriter) != vanguard::game_input::MappingResult::Success)
            return false;
        const vanguard::packages::BuildSegment inputSegment{mappingBytes.Data(), mappingBytes.Size(), vanguard::packages::Codec::None, 4,
                                                           vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource inputResource;
        inputResource.path = "input/default.vinput";
        inputResource.type = vanguard::game_input::MappingResourceType;
        inputResource.flags = vanguard::packages::ResourceFlags::Startup;
        inputResource.segments = {&inputSegment, 1};

        std::array<const vanguard::packages::BuildResource*, 4> resources{
            &worldResource, &cellResource, &prefabResource, &inputResource};
        std::sort(resources.begin(), resources.end(), [](const auto* const left, const auto* const right) noexcept
                  { return vanguard::packages::HashResourcePath(left->path) < vanguard::packages::HashResourcePath(right->path); });
        for (const vanguard::packages::BuildResource* const resource : resources)
            if (writer.Add(*resource) != vanguard::packages::Result::Success)
                return false;
        return writer.Finalize() == vanguard::packages::Result::Success;
    }

    [[nodiscard]] bool SaveBytes(const vanguard::filesystem::AbsolutePath& path, const void* const data, const vanguard::usize size) noexcept
    {
        auto writer = vanguard::filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        if (size != 0)
            writer->Serialize(const_cast<void*>(data), size);
        writer->Flush();
        return writer->GetSize() == size;
    }

    [[nodiscard]] vanguard::engine::WorldSessionStatus PollSessionUntil(vanguard::engine::WorldSessionService& session, const vanguard::engine::WorldSessionStatus target) noexcept
    {
        vanguard::engine::WorldSessionFailure failure;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const vanguard::engine::WorldSessionStatus status = session.Poll(&failure);
            if (status == target || status == vanguard::engine::WorldSessionStatus::Failed)
                return status;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return session.GetStatus();
    }

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[engineServicesTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct WrongCameraData
    {
        vanguard::u32 value = 7;
    };

    class WrongCameraComponent final : public vanguard::entities::Component
    {
    public:
        explicit WrongCameraComponent(const WrongCameraData& data) noexcept : m_data(data) {}
    private:
        WrongCameraData m_data;
    };

    inline constexpr vanguard::reflection::SchemaTypeId WrongCameraType =
        vanguard::reflection::HashSchemaName("vanguard.engine_tests.wrong_camera");
    const std::array<vanguard::reflection::SchemaField, 1> WrongCameraFields{{
        vanguard::reflection::MakeField("value", vanguard::reflection::builtin::U32, vanguard::reflection::ValueKind::U32,
                                       static_cast<vanguard::u32>(offsetof(WrongCameraData, value)), sizeof(vanguard::u32),
                                       alignof(vanguard::u32), 1, 0, vanguard::reflection::FieldFlags::Required)}};
    const vanguard::reflection::Schema WrongCameraSchema{
        WrongCameraType, "vanguard.engine_tests.wrong_camera", sizeof(WrongCameraData), alignof(WrongCameraData), 1, 1,
        WrongCameraFields.data(), static_cast<vanguard::u32>(WrongCameraFields.size())};

    struct CameraPrefabResolver
    {
        vanguard::resources::ResourceReference reference;
        const vanguard::prefabs::PrefabFile* file = nullptr;
    };

    const vanguard::prefabs::PrefabFile* ResolveCameraPrefab(const vanguard::resources::ResourceReference reference, void* const userData) noexcept
    {
        const auto* const resolver = static_cast<const CameraPrefabResolver*>(userData);
        return resolver != nullptr && resolver->file != nullptr && resolver->reference == reference ? resolver->file : nullptr;
    }

    vanguard::rendering::RenderFrameExecutionStatus ExecuteCameraResolverTick(vanguard::rendering::RenderFrameTickContext&, void*) noexcept
    {
        return vanguard::rendering::RenderFrameExecutionStatus::Success();
    }

    vanguard::rendering::RenderFrameExecutionStatus ExecuteCameraResolverFrame(vanguard::rendering::RenderFrameContext&, void*) noexcept
    {
        return vanguard::rendering::RenderFrameExecutionStatus::Success();
    }

    void RunWorldCameraResolverTests() noexcept
    {
        namespace entities = vanguard::entities;
        namespace rendering = vanguard::rendering;
        namespace world = vanguard::world;

        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        Check(scenes.Initialize({}, &sceneFailure), "P2 resolver initializes CPU RenderSceneManager");
        rendering::RenderCameraStorage cameras;
        rendering::RenderCameraFailure cameraFailure;
        Check(cameras.Initialize(scenes, {}, &cameraFailure), "P2 resolver initializes CPU RenderCameraStorage");
        rendering::RenderCommandSystem commands;
        rendering::RenderCommandSystemConfig commandConfig;
        commandConfig.executeFrameTick = &ExecuteCameraResolverTick;
        commandConfig.executeFrame = &ExecuteCameraResolverFrame;
        rendering::RenderCommandFailure commandFailure;
        Check(commands.Initialize(scenes, cameras, commandConfig, &commandFailure), "P2 resolver initializes CPU RenderCommandSystem");
        rendering::RenderPhaseRegistry phases;
        rendering::RenderPhaseFailure phaseFailure;
        Check(phases.Initialize({}, &phaseFailure) && rendering::RegisterStandardRenderPhases(phases, &phaseFailure) && phases.Seal(&phaseFailure),
              "P2 resolver seals the real standard render phases");
        rendering::RenderPhaseSet cameraPhases;
        Check(cameraPhases.Add(phases.Find(rendering::standardRenderPhases::DepthPrepass)) &&
                  cameraPhases.Add(phases.Find(rendering::standardRenderPhases::Opaque)),
              "P2 resolver builds the production default camera phase subset");

        vanguard::game::GameWorld fixtureWorld;
        entities::ComponentDirectoryConfig componentConfig;
        componentConfig.maximumComponents = 128;
        entities::ComponentRuntime componentRuntime(componentConfig);
        entities::TransformRuntimeConfig transformConfig;
        transformConfig.maximumTrackedEntities = 64;
        transformConfig.maximumPlacedComponents = 64;
        transformConfig.maximumChangesPerSynchronize = 256;
        transformConfig.transformSystem.maximumComponents = 128;
        transformConfig.transformSystem.maximumAttachments = 127;
        transformConfig.transformSystem.maximumScheduledRoots = 64;
        entities::TransformRuntime transformRuntime(transformConfig);
        entities::RenderingRuntimeConfig renderingConfig;
        renderingConfig.scene.maximumProxies = 128;
        renderingConfig.scene.maximumPendingProxyMutations = 128;
        renderingConfig.scene.maximumViews = 4;
        renderingConfig.maximumPendingProxyAdmissions = 128;
        renderingConfig.maximumProxyAdmissionsPerFrame = 32;
        renderingConfig.maximumPendingProxyRetirements = 128;
        renderingConfig.maximumPendingMeshPreparations = 128;
        renderingConfig.maximumMeshPreparationsPerFrame = 32;
        renderingConfig.maximumPendingCameraTransforms = 32;
        entities::RenderingRuntime renderingRuntime(scenes, renderingConfig);
        Check(renderingRuntime.BindCommands(commands, phases, cameraPhases), "P2 resolver binds CPU renderer commands");
        vanguard::game::GameWorldConfig fixtureWorldConfig;
        fixtureWorldConfig.entities.initialEntityCapacity = 128;
        fixtureWorldConfig.entities.initialActionCapacity = 128;
        fixtureWorldConfig.entities.committedChangeCapacity = 512;
        Check(fixtureWorld.RegisterSystem(componentRuntime) && fixtureWorld.RegisterSystem(transformRuntime) &&
                  fixtureWorld.RegisterSystem(renderingRuntime) && fixtureWorld.Initialize(fixtureWorldConfig),
              "P2 resolver initializes real component/transform/rendering runtime systems");

        entities::ComponentRegistry registry;
        const vanguard::reflection::Schema* const existingWrongCameraSchema = vanguard::reflection::FindSchema(WrongCameraType);
        const bool wrongCameraSchemaReady =
            existingWrongCameraSchema == &WrongCameraSchema ||
            (existingWrongCameraSchema == nullptr && vanguard::reflection::RegisterSchema(WrongCameraSchema));
        Check(registry.Initialize(fixtureWorld.GetEntities()) && entities::RegisterCameraComponent(registry) && wrongCameraSchemaReady &&
                  registry.RegisterObject<WrongCameraData, WrongCameraComponent>(WrongCameraSchema) && registry.Seal(),
              "P2 resolver seals camera and wrong-type component schemas");

        constexpr vanguard::u64 PrefabRootStableId = 10;
        constexpr vanguard::u64 CameraStableId = 20;
        constexpr vanguard::u64 WrongStableId = 21;
        constexpr vanguard::u64 EntityId = 0x43414d4552411001ull;
        constexpr vanguard::u64 CellId = 0x43414d4552412001ull;
        constexpr vanguard::u64 OtherEntityId = 0x43414d4552411002ull;
        constexpr vanguard::u64 OtherCellId = 0x43414d4552412002ull;
        constexpr vanguard::u64 PartitionOnlyEntityId = 0x43414d4552411003ull;
        constexpr vanguard::u64 NonBasePartitionId = 0x504152544954494full;
        const vanguard::resources::ResourceReference prefabReference(
            vanguard::resources::ResourcePath::FromString("world/p2/camera_fixture.vprefab"), vanguard::prefabs::PrefabResourceType);

        const vanguard::prefabs::EntityBuildRecord prefabEntity{PrefabRootStableId};
        entities::CameraComponentData cameraData;
        WrongCameraData wrongData;
        const std::array<vanguard::prefabs::ComponentBuildRecord, 2> prefabComponents{{
            {CameraStableId, PrefabRootStableId, &entities::GetCameraComponentSchema(), &cameraData},
            {WrongStableId, PrefabRootStableId, &WrongCameraSchema, &wrongData}}};
        vanguard::prefabs::CookDescription prefabDescription;
        prefabDescription.name = 0x503243414d455241ull;
        prefabDescription.entities = {&prefabEntity, 1};
        prefabDescription.components = {prefabComponents.data(), static_cast<vanguard::u32>(prefabComponents.size())};
        vanguard::containers::DynamicArray<vanguard::u8> prefabBytes(vanguard::memory::pools::Serialization::GetInstance());
        vanguard::filesystem::MemoryFileWriter prefabWriter(prefabBytes);
        Check(vanguard::prefabs::CookPrefab(prefabDescription, prefabWriter) == vanguard::prefabs::Result::Success,
              "P2 resolver cooks camera fixture prefab");
        vanguard::filesystem::MemoryFileReader prefabReader(prefabBytes, 0);
        vanguard::prefabs::PrefabFile prefabFile;
        Check(prefabFile.Open(prefabReader) == vanguard::prefabs::Result::Success, "P2 resolver opens camera fixture prefab");

        world::PlacementBuildRecord placement;
        placement.entityId = EntityId;
        placement.prefab = prefabReference;
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            placement.bounds.minimum[axis] = -1.0f;
            placement.bounds.maximum[axis] = 1.0f;
        }
        world::CellBuildDescription cellDescription;
        cellDescription.cellId = CellId;
        cellDescription.worldId = 0x503243414d574f52ull;
        cellDescription.bounds = placement.bounds;
        cellDescription.placements = {&placement, 1};
        vanguard::containers::DynamicArray<vanguard::u8> cellBytes(vanguard::memory::pools::World::GetInstance());
        vanguard::filesystem::MemoryFileWriter cellWriter(cellBytes);
        Check(world::CookCell(cellDescription, cellWriter) == world::Result::Success, "P2 resolver cooks camera fixture cell");
        vanguard::filesystem::MemoryFileReader cellReader(cellBytes, 0);
        world::CellFile cellFile;
        Check(cellFile.Open(cellReader) == world::Result::Success, "P2 resolver opens camera fixture cell");

        world::PlacementBuildRecord otherPlacement = placement;
        otherPlacement.entityId = OtherEntityId;
        world::CellBuildDescription otherCellDescription = cellDescription;
        otherCellDescription.cellId = OtherCellId;
        otherCellDescription.placements = {&otherPlacement, 1};
        vanguard::containers::DynamicArray<vanguard::u8> otherCellBytes(vanguard::memory::pools::World::GetInstance());
        vanguard::filesystem::MemoryFileWriter otherCellWriter(otherCellBytes);
        Check(world::CookCell(otherCellDescription, otherCellWriter) == world::Result::Success,
              "P2 resolver cooks second camera fixture cell");
        vanguard::filesystem::MemoryFileReader otherCellReader(otherCellBytes, 0);
        world::CellFile otherCellFile;
        Check(otherCellFile.Open(otherCellReader) == world::Result::Success,
              "P2 resolver opens second camera fixture cell");

        entities::EntityReferenceRegistry references;
        entities::CellMaterializer materializer;
        Check(references.Initialize(fixtureWorld.GetEntities()) &&
                  materializer.Initialize(registry, componentRuntime.GetDirectory(), references),
              "P2 resolver initializes real materializer/reference owners");
        CameraPrefabResolver prefabResolver{prefabReference, &prefabFile};
        Check(materializer.QueueCell(cellFile, 1, &ResolveCameraPrefab, &prefabResolver) == entities::Result::Success,
              "P2 resolver queues the authored camera cell");
        entities::TransformRuntimeFailure transformFailure;
        const bool actionsFlushed = fixtureWorld.GetEntities().FlushActions();
        const bool componentsFlushed = actionsFlushed && fixtureWorld.GetEntities().FlushComponentActions();
        const bool transformsSynchronized = componentsFlushed && transformRuntime.Synchronize(&transformFailure);
        const bool transformRootReady = transformsSynchronized && transformRuntime.GetRoot(EntityId) != nullptr;
        Check(transformRootReady,
              "P2 resolver commits camera cell ECS state and mirrors its real transform root");
        entities::Result activation = entities::Result::NotReady;
        for (vanguard::u32 attempt = 0; attempt < 1000 && activation == entities::Result::NotReady; ++attempt)
        {
            activation = materializer.PrepareActivation(CellId, 1);
            if (activation == entities::Result::NotReady)
                vanguard::concurrency::SleepOnCurrentThread(1);
        }
        const entities::Result publication =
            activation == entities::Result::Success ? materializer.PublishActivation(CellId, 1) : activation;
        Check(activation == entities::Result::Success && publication == entities::Result::Success &&
                  materializer.GetState(CellId) == entities::CellState::Active && references.HasEntity(EntityId),
              "P2 resolver publishes identity only after camera attachment completes");
        Check(references.HasEntityInPartition(EntityId, CellId, 1, entities::EntityReferenceRegistry::BasePartition),
              "P2 resolver provenance query identifies the base publication owner");

        Check(fixtureWorld.GetEntities().QueueCreate(PartitionOnlyEntityId) && fixtureWorld.Tick(0.0f),
              "P2 resolver creates a partition-only identity for provenance proof");
        const vanguard::ecs::EntityId partitionOnlyEntities[]{PartitionOnlyEntityId};
        Check(references.RegisterPartition(CellId, 1, NonBasePartitionId, {partitionOnlyEntities, 1}, {}) ==
                  entities::ReferenceResult::Success &&
                  references.HasEntity(PartitionOnlyEntityId) &&
                  !references.HasEntityInPartition(PartitionOnlyEntityId, CellId, 1, entities::EntityReferenceRegistry::BasePartition) &&
                  references.HasEntityInPartition(PartitionOnlyEntityId, CellId, 1, NonBasePartitionId),
              "P2 resolver provenance query rejects identities published only by a non-base activation partition");
        Check(references.UnregisterPartition(CellId, 1, NonBasePartitionId) == entities::ReferenceResult::Success &&
                  fixtureWorld.GetEntities().QueueDestroy(PartitionOnlyEntityId) && fixtureWorld.Tick(0.0f),
              "P2 resolver removes the partition-only provenance fixture");

        Check(materializer.QueueCell(otherCellFile, 1, &ResolveCameraPrefab, &prefabResolver) == entities::Result::Success,
              "P2 resolver queues the second active camera cell");
        entities::TransformRuntimeFailure otherTransformFailure;
        Check(fixtureWorld.GetEntities().FlushActions() && fixtureWorld.GetEntities().FlushComponentActions() &&
                  transformRuntime.Synchronize(&otherTransformFailure) && transformRuntime.GetRoot(OtherEntityId) != nullptr,
              "P2 resolver mirrors the second camera-cell transform root");
        entities::Result otherActivation = entities::Result::NotReady;
        for (vanguard::u32 attempt = 0; attempt < 1000 && otherActivation == entities::Result::NotReady; ++attempt)
        {
            otherActivation = materializer.PrepareActivation(OtherCellId, 1);
            if (otherActivation == entities::Result::NotReady)
                vanguard::concurrency::SleepOnCurrentThread(1);
        }
        const entities::Result otherPublication =
            otherActivation == entities::Result::Success ? materializer.PublishActivation(OtherCellId, 1) : otherActivation;
        Check(otherActivation == entities::Result::Success && otherPublication == entities::Result::Success &&
                  materializer.GetState(OtherCellId) == entities::CellState::Active &&
                  references.HasEntityInPartition(OtherEntityId, OtherCellId, 1, entities::EntityReferenceRegistry::BasePartition),
              "P2 resolver publishes the second cell with exact base provenance");

        std::array<world::WorldCellBuildRecord, 2> worldCells{};
        worldCells[0].cellId = CellId;
        worldCells[0].cell = vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("world/p2/camera_fixture.vcell"),
                                                                   world::CellResourceType);
        worldCells[1].cellId = OtherCellId;
        worldCells[1].cell =
            vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("world/p2/camera_fixture_other.vcell"),
                                                   world::CellResourceType);
        for (world::WorldCellBuildRecord& worldCell : worldCells)
        {
            worldCell.bounds.minimum[0] = worldCell.bounds.minimum[1] = worldCell.bounds.minimum[2] = -2.0;
            worldCell.bounds.maximum[0] = worldCell.bounds.maximum[1] = worldCell.bounds.maximum[2] = 2.0;
            worldCell.flags = world::WorldCellFlags::AlwaysLoaded;
        }
        world::WorldBuildDescription worldDescription;
        worldDescription.worldId = cellDescription.worldId;
        worldDescription.bounds = worldCells[0].bounds;
        worldDescription.cells = {worldCells.data(), static_cast<vanguard::u32>(worldCells.size())};
        vanguard::containers::DynamicArray<vanguard::u8> worldBytes(vanguard::memory::pools::World::GetInstance());
        vanguard::filesystem::MemoryFileWriter worldWriter(worldBytes);
        Check(world::CookWorld(worldDescription, worldWriter) == world::Result::Success, "P2 resolver cooks selector world");
        vanguard::filesystem::MemoryFileReader worldReader(worldBytes, 0);
        world::WorldFile worldFile;
        Check(worldFile.Open(worldReader) == world::Result::Success, "P2 resolver opens selector world");
        world::WorldStreamingGrid grid;
        Check(grid.Initialize(worldFile), "P2 resolver initializes selector grid");

        vanguard::engine::WorldCameraResolver noCamera;
        Check(noCamera.Begin({}) && noCamera.GetResolution().status == vanguard::engine::WorldCameraResolutionStatus::None &&
                  !noCamera.GetResolution().component.IsValid() && !noCamera.GetResolution().camera.IsValid(),
              "P2 resolver treats the all-zero authored locator as no world camera");
        vanguard::engine::WorldCameraResolver partialCamera;
        Check(!partialCamera.Begin({CellId, 0, CameraStableId}),
              "P2 resolver rejects partial authored camera locators");

        vanguard::engine::WorldCameraResolver resolver;
        Check(resolver.Begin({CellId, EntityId, CameraStableId}) &&
                  resolver.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras),
              "P2 resolver processes the exact named camera");
        const vanguard::engine::WorldCameraResolution ready = resolver.GetResolution();
        Check(ready.status == vanguard::engine::WorldCameraResolutionStatus::Ready && ready.failure == vanguard::engine::WorldCameraResolutionFailure::None &&
                  ready.component.IsValid() && ready.camera.IsValid() && cameras.IsAlive(ready.camera) &&
                  componentRuntime.GetDirectory().Get(ready.component) != nullptr,
              "P2 resolver retains only live generational component/camera handles");

        vanguard::engine::WorldCameraResolver missingEntity;
        Check(missingEntity.Begin({CellId, EntityId + 99u, CameraStableId}) &&
                  !missingEntity.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras) &&
                  missingEntity.GetResolution().failure == vanguard::engine::WorldCameraResolutionFailure::MissingEntity,
              "P2 resolver fails an active startup cell with a missing exact entity");
        vanguard::engine::WorldCameraResolver missingComponent;
        Check(missingComponent.Begin({CellId, EntityId, CameraStableId + 99u}) &&
                  !missingComponent.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras) &&
                  missingComponent.GetResolution().failure == vanguard::engine::WorldCameraResolutionFailure::MissingComponent,
              "P2 resolver fails an active entity with a missing exact component");
        vanguard::engine::WorldCameraResolver wrongType;
        Check(wrongType.Begin({CellId, EntityId, WrongStableId}) &&
                  !wrongType.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras) &&
                  wrongType.GetResolution().failure == vanguard::engine::WorldCameraResolutionFailure::WrongComponentType,
              "P2 resolver rejects a named non-camera component without scanning");
        vanguard::engine::WorldCameraResolver wrongCell;
        Check(wrongCell.Begin({OtherCellId, EntityId, CameraStableId}) &&
                  !wrongCell.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras) &&
                  wrongCell.GetResolution().failure == vanguard::engine::WorldCameraResolutionFailure::MissingEntity,
              "P2 resolver rejects a same-ID camera published by a different active cell");
        Check(materializer.QueueRelease(OtherCellId, 1) == entities::Result::Success && fixtureWorld.Tick(0.0f) &&
                  materializer.CompleteRelease(OtherCellId, 1) == entities::Result::Success,
              "P2 resolver releases the second provenance fixture cell");

        vanguard::engine::WorldCameraResolver revokeProbe;
        Check(revokeProbe.Begin({CellId, EntityId, CameraStableId}) &&
                  revokeProbe.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras),
              "P2 revoke probe first resolves the real camera");
        revokeProbe.Revoke();
        Check(revokeProbe.GetResolution().status == vanguard::engine::WorldCameraResolutionStatus::Pending &&
                  revokeProbe.GetResolution().locator.cellId == CellId && revokeProbe.GetResolution().locator.entityId == EntityId &&
                  revokeProbe.GetResolution().locator.componentStableId == CameraStableId &&
                  !revokeProbe.GetResolution().component.IsValid() && !revokeProbe.GetResolution().camera.IsValid(),
              "P2 explicit revoke drops runtime authority while preserving the locator");

        Check(materializer.QueueRelease(CellId, 1) == entities::Result::Success && fixtureWorld.Tick(0.0f) &&
                  materializer.CompleteRelease(CellId, 1) == entities::Result::Success,
              "P2 resolver releases camera materialization generation one");
        Check(!cameras.IsAlive(ready.camera) && componentRuntime.GetDirectory().Get(ready.component) == nullptr,
              "P2 camera/component generations become stale after detach");
        Check(resolver.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras) &&
                  resolver.GetResolution().status == vanguard::engine::WorldCameraResolutionStatus::Pending &&
                  !resolver.GetResolution().component.IsValid() && !resolver.GetResolution().camera.IsValid(),
              "P2 resolver revokes stale generations and returns to pending without scanning");

        Check(materializer.QueueCell(cellFile, 2, &ResolveCameraPrefab, &prefabResolver) == entities::Result::Success,
              "P2 resolver queues replacement materialization generation two");
        entities::TransformRuntimeFailure replacementTransformFailure;
        Check(fixtureWorld.GetEntities().FlushActions() && fixtureWorld.GetEntities().FlushComponentActions() &&
                  transformRuntime.Synchronize(&replacementTransformFailure) && transformRuntime.GetRoot(EntityId) != nullptr,
              "P2 resolver mirrors the replacement transform root");
        entities::Result replacementActivation = entities::Result::NotReady;
        for (vanguard::u32 attempt = 0; attempt < 1000 && replacementActivation == entities::Result::NotReady; ++attempt)
        {
            replacementActivation = materializer.PrepareActivation(CellId, 2);
            if (replacementActivation == entities::Result::NotReady)
                vanguard::concurrency::SleepOnCurrentThread(1);
        }
        const entities::Result replacementPublication =
            replacementActivation == entities::Result::Success ? materializer.PublishActivation(CellId, 2) : replacementActivation;
        Check(replacementActivation == entities::Result::Success && replacementPublication == entities::Result::Success &&
                  resolver.Update(grid, materializer, references, componentRuntime.GetDirectory(), cameras),
              "P2 resolver re-resolves the same authored camera identity after replacement");
        const vanguard::engine::WorldCameraResolution rebound = resolver.GetResolution();
        Check(rebound.status == vanguard::engine::WorldCameraResolutionStatus::Ready &&
                  rebound.component.IsValid() && rebound.camera.IsValid() && cameras.IsAlive(rebound.camera) &&
                  rebound.component.generation != ready.component.generation && rebound.camera.generation != ready.camera.generation &&
                  componentRuntime.GetDirectory().Get(ready.component) == nullptr && !cameras.IsAlive(ready.camera),
              "P2 resolver binds only the new component/camera generations after replacement");

        resolver.Revoke();
        Check(materializer.QueueRelease(CellId, 2) == entities::Result::Success && fixtureWorld.Tick(0.0f) &&
                  materializer.CompleteRelease(CellId, 2) == entities::Result::Success,
              "P2 resolver releases replacement materialization after final revoke");

        Check(grid.RequestShutdown(), "P2 resolver requests selector-grid shutdown");
        vanguard::containers::DynamicArray<world::StreamingCommand> shutdownCommands(vanguard::memory::pools::World::GetInstance());
        world::StreamingObserver shutdownObserver;
        world::StreamingProcessInput shutdownInput;
        shutdownInput.observers = {&shutdownObserver, 1};
        Check(grid.Process(shutdownInput, shutdownCommands), "P2 resolver drains selector grid");
        for (const world::StreamingCommand& command : shutdownCommands)
            Check(grid.NotifyStreamOutComplete(command.key), "P2 resolver completes selector-grid release");
        Check(grid.Shutdown(), "P2 resolver shuts down selector grid");
        Check(materializer.Shutdown() && references.Shutdown(), "P2 resolver shuts down materialization owners");
        Check(renderingRuntime.ReleaseScene(&sceneFailure), "P2 resolver explicitly releases its fixture RenderScene before world shutdown");
        Check(fixtureWorld.Shutdown(), "P2 resolver shuts down fixture world");
        Check(phases.Shutdown(&phaseFailure), "P2 resolver shuts down phase registry");
        Check(commands.Shutdown(&commandFailure), "P2 resolver shuts down command system");
        Check(cameras.Shutdown(&cameraFailure), "P2 resolver shuts down camera storage");
        Check(scenes.Shutdown(&sceneFailure), "P2 resolver shuts down scene manager");
        worldFile.Close();
        otherCellFile.Close();
        cellFile.Close();
        prefabFile.Close();
    }

    void RecordLifecycle(const vanguard::application::LifecycleEvent& event, void* const userData) noexcept
    {
        auto& trace = *static_cast<LifecycleTrace*>(userData);
        if (trace.count < 256)
            trace.events[trace.count++] = event;
    }

    [[nodiscard]] vanguard::u32 FindLifecycleEvent(const LifecycleTrace& trace, const vanguard::application::ServiceId service, const vanguard::application::LifecycleStage stage) noexcept
    {
        for (vanguard::u32 index = 0; index < trace.count; ++index)
            if (trace.events[index].service == service && trace.events[index].stage == stage)
                return index;
        return 128;
    }

    /// P5 deterministic proof of the session-owned camera binding and its single
    /// streaming observer. It drives one real EngineHost with the real owners: a
    /// scratch-backed cooked world, the production GameWorldService world tick, the
    /// sealed render phase registry, the real RenderCommandSystem camera storage and
    /// the real FramePipelineService. Registered render cameras are inert roots; no
    /// view, viewport, output region or render graph is constructed, and only the
    /// session-owned observer is exercised.
    void RunWorldSessionObserverLifecycleTests() noexcept
    {
        namespace engine = vanguard::engine;
        namespace rendering = vanguard::rendering;

        vanguard::application::EngineHost host;
        EmptyInputBackend inputBackend;
        vanguard::application::HostFailure hostFailure;
        const bool registered = engine::RegisterEngineModule(host, &hostFailure) && engine::RegisterIoService(host, &hostFailure) &&
                                engine::RegisterFilesystemService(host, &hostFailure) && engine::RegisterJobsService(host, &hostFailure) &&
                                engine::RegisterFramePipelineService(host, &hostFailure) && engine::RegisterReflectionService(host, &hostFailure) &&
                                engine::RegisterInputService(host, &inputBackend, &hostFailure) && engine::RegisterResourcesService(host, &hostFailure) &&
                                engine::RegisterResourceStreamingService(host, &hostFailure) && engine::RegisterGameInputService(host, &hostFailure) &&
                                engine::RegisterWorldService(host, &hostFailure) && engine::RegisterGameWorldService(host, &hostFailure) &&
                                engine::RegisterStreamingObserverService(host, &hostFailure) && engine::RegisterWorldSessionService(host, &hostFailure) &&
                                engine::RegisterRenderingService(host, &hostFailure);
        const bool compiled = registered && host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure);
        const bool started = compiled && host.Start(&hostFailure);
        if (!started)
            std::fprintf(stderr, "[engineServicesTests] P5 host failure: code=%u service=%llu related=%llu message=%s\n",
                         static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                         static_cast<unsigned long long>(hostFailure.relatedService), hostFailure.message != nullptr ? hostFailure.message : "<none>");
        Check(started, "P5 observer host starts the production service graph");

        engine::ResourceStreamingService* const streaming = engine::FindResourceStreamingService(host);
        engine::WorldService* const worldService = engine::FindWorldService(host);
        engine::GameWorldService* const gameWorld = engine::FindGameWorldService(host);
        engine::StreamingObserverService* const observers = engine::FindStreamingObserverService(host);
        engine::WorldSessionService* const session = engine::FindWorldSessionService(host);
        engine::RenderingService* const renderingService = engine::FindRenderingService(host);
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(host);
        Check(streaming != nullptr && worldService != nullptr && gameWorld != nullptr && observers != nullptr && session != nullptr && renderingService != nullptr &&
                  framePipeline != nullptr,
              "P5 observer host publishes the typed production owners");
        if (streaming == nullptr || worldService == nullptr || gameWorld == nullptr || observers == nullptr || session == nullptr || renderingService == nullptr ||
            framePipeline == nullptr)
            return;

        engine::GameWorldServiceConfig gameWorldConfig;
        gameWorldConfig.components.maximumComponents = 128;
        gameWorldConfig.transforms.maximumTrackedEntities = 64;
        gameWorldConfig.transforms.maximumPlacedComponents = 64;
        gameWorldConfig.transforms.maximumChangesPerSynchronize = 256;
        gameWorldConfig.transforms.transformSystem.maximumComponents = 128;
        gameWorldConfig.transforms.transformSystem.maximumAttachments = 127;
        gameWorldConfig.transforms.transformSystem.maximumScheduledRoots = 64;
        gameWorldConfig.materialization.maximumEntitiesPerCell = 64;
        gameWorldConfig.materialization.maximumComponentsPerCell = 128;
        gameWorldConfig.rendering.scene.maximumProxies = 128;
        gameWorldConfig.rendering.scene.maximumPendingProxyMutations = 128;
        gameWorldConfig.rendering.scene.maximumViews = 4;
        gameWorldConfig.rendering.maximumPendingProxyAdmissions = 128;
        gameWorldConfig.rendering.maximumProxyAdmissionsPerFrame = 32;
        gameWorldConfig.rendering.maximumPendingProxyRetirements = 128;
        gameWorldConfig.rendering.maximumPendingMeshPreparations = 128;
        gameWorldConfig.rendering.maximumMeshPreparationsPerFrame = 32;
        gameWorldConfig.rendering.maximumPendingCameraTransforms = 32;
        gameWorldConfig.world.entities.initialEntityCapacity = 128;
        gameWorldConfig.world.entities.initialActionCapacity = 128;
        gameWorldConfig.world.entities.committedChangeCapacity = 512;
        Check(gameWorld->Configure(gameWorldConfig), "P5 observer host configures the bounded Game World fixture");

        FakeFrameClock frameClock;
        engine::FrameFailure frameFailure;
        engine::FramePipelineConfig pipelineConfig;
        pipelineConfig.clock = {&ReadFakeFrameClock, 1'000, &frameClock};
        pipelineConfig.maximumDeltaSeconds = 0.1f;
        pipelineConfig.fixedDeltaSeconds = 0.02f;
        pipelineConfig.maximumFixedStepsPerFrame = 2;
        pipelineConfig.pacing = engine::FramePacingMode::Disabled;
        Check(framePipeline->Configure(pipelineConfig, &frameFailure) && framePipeline->Compile(&frameFailure),
              "P5 observer host compiles the frame pipeline");
        FrameParticipantSearch sessionObserverSearch;
        sessionObserverSearch.id = engine::WorldSessionCameraObserverParticipantId;
        framePipeline->VisitParticipantStats(&FindFrameParticipantStats, &sessionObserverSearch);
        Check(sessionObserverSearch.found && sessionObserverSearch.phase == engine::FramePhase::BeginFrame,
              "P5 observer host schedules the session observer in BeginFrame");

        vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
        const vanguard::filesystem::AbsolutePath directory = vanguard::tests::ScratchDirectory("engine_services_tests").AddDirPath("vanguard_p5_observer");
        const vanguard::filesystem::AbsolutePath package = directory.AddFilePath("DATA000.vpak");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        Check(files.CreatePath(directory), "P5 observer fixture directory creation");
        vanguard::containers::DynamicArray<vanguard::u8> packageBytes(vanguard::memory::pools::Resources::GetInstance());

        engine::WorldSessionFailure sessionFailure;
        engine::WorldSessionStartRequest start;
        start.inputMode = engine::WorldSessionInputMode::None;

        // Pending and Failed WorldDefault bindings must never own an observer.
        Check(BuildWorldSessionPackage(packageBytes, true) && SaveBytes(package, packageBytes.Data(), packageBytes.Size()),
              "P5 publishes the camera-less world fixture");
        Check(streaming->GetPackageSet().Mount(streaming->GetStreamer(), directory) == vanguard::streaming::PackageSetMountResult::Success,
              "P5 mounts the camera-less world fixture");
        start.world = streaming->GetPackageSet().StartupWorld();
        start.cameraSource = engine::WorldSessionCameraSource::WorldDefault;
        Check(session->Begin(start, &sessionFailure) && session->GetStatus() == engine::WorldSessionStatus::LoadingWorld,
              "P5 WorldDefault session begins loading");
        const engine::WorldSessionCameraBinding pendingBinding = session->GetCameraBinding();
        Check(pendingBinding.source == engine::WorldSessionCameraBindingSource::WorldDefault &&
                  pendingBinding.status == engine::WorldSessionCameraBindingStatus::Pending,
              "P5 WorldDefault binding is Pending while the world resource loads");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 0,
              "P5 Pending WorldDefault binding owns no observer");
        Check(session->RequestStop(&sessionFailure) && PollSessionUntil(*session, engine::WorldSessionStatus::Idle) == engine::WorldSessionStatus::Idle,
              "P5 Pending WorldDefault session can be withdrawn cleanly before terminal startup failure");
        Check(observers->GetStats().registeredObservers == 0 && session->GetCameraBinding().source == engine::WorldSessionCameraBindingSource::None,
              "P5 withdrawn Pending WorldDefault session leaves no observer authority");
        Check(streaming->GetPackageSet().Unmount() == vanguard::streaming::PackageSetMountResult::Success, "P5 releases the camera-less world fixture");

        // Live-selection observer lifecycle on a running None session.
        static_cast<void>(files.DeleteFile(package));
        Check(BuildMissingCameraSessionPackage(packageBytes) && SaveBytes(package, packageBytes.Data(), packageBytes.Size()),
              "P5 publishes the authored-locator world fixture");
        Check(streaming->GetPackageSet().Mount(streaming->GetStreamer(), directory) == vanguard::streaming::PackageSetMountResult::Success,
              "P5 mounts the authored-locator world fixture");
        start.world = streaming->GetPackageSet().StartupWorld();
        start.cameraSource = engine::WorldSessionCameraSource::None;
        const bool liveSessionBegun = session->Begin(start, &sessionFailure);
        const engine::WorldSessionStatus liveSessionStatus =
            liveSessionBegun ? PollSessionUntil(*session, engine::WorldSessionStatus::Running) : session->GetStatus();
        Check(liveSessionBegun && liveSessionStatus == engine::WorldSessionStatus::Running &&
                  session->GetCameraBinding().source == engine::WorldSessionCameraBindingSource::None,
              "P5 None session reaches Running without any camera binding");

        // World-origin fallback remains the loading fallback while no observer exists.
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 0 && observers->GetStats().usingWorldOriginFallback,
              "P5 running session with no observer publishes the world-origin streaming fallback");

        const rendering::RenderSceneHandle worldScene =
            gameWorld->GetRendering() != nullptr ? gameWorld->GetRendering()->GetScene() : rendering::RenderSceneHandle{};
        // The device-disabled RenderingService deliberately leaves its own render
        // phase registry uninitialized, so the inert live-camera fixture uses a local
        // sealed standard phase set exactly like the CPU-only resolver fixture. No
        // view family is ever prepared from these cameras; they only need to be valid
        // and alive in the world scene.
        rendering::RenderPhaseRegistry liveCameraPhases;
        rendering::RenderPhaseFailure liveCameraPhaseFailure;
        rendering::RenderPhaseSet cameraPhases;
        Check(worldScene.IsValid() && liveCameraPhases.Initialize({}, &liveCameraPhaseFailure) &&
                  rendering::RegisterStandardRenderPhases(liveCameraPhases, &liveCameraPhaseFailure) && liveCameraPhases.Seal(&liveCameraPhaseFailure) &&
                  cameraPhases.Add(liveCameraPhases.Find(rendering::standardRenderPhases::DepthPrepass)) &&
                  cameraPhases.Add(liveCameraPhases.Find(rendering::standardRenderPhases::Opaque)),
              "P5 world scene and local sealed live-camera phase set are available");

        auto RegisterLiveCamera = [&](const char* const name, rendering::RenderCameraHandle& camera) noexcept
        {
            rendering::RenderCameraDesc desc;
            desc.scene = worldScene;
            desc.name = name;
            desc.enabled = true;
            desc.state.phases = cameraPhases;
            rendering::RenderCameraFailure cameraFailure;
            return renderingService->GetCommands().RegisterCamera(desc, camera, &cameraFailure) && camera.IsValid();
        };
        auto MakePose = [](const vanguard::f32 x, const vanguard::f32 y, const vanguard::f32 z) noexcept
        {
            engine::WorldCameraPose pose;
            pose.position = math::WorldPosition(x, y, z);
            return pose;
        };

        rendering::RenderCameraHandle liveCamera;
        Check(RegisterLiveCamera("p5.liveCamera", liveCamera), "P5 registers the caller-owned Live camera");
        engine::WorldSessionCameraSelectionToken liveToken;
        Check(session->SelectLiveCamera(liveCamera, MakePose(10.0f, 0.0f, 0.0f), &liveToken, &sessionFailure) && liveToken.IsValid() &&
                  session->GetCameraBinding().status == engine::WorldSessionCameraBindingStatus::Ready,
              "P5 selects the explicit Live camera and its binding is Ready");

        // Explicit primary policy: an unrelated primary observer must not own the
        // streaming camera position once the session observer is ready.
        engine::StreamingObserverDescriptor externalDescriptor;
        externalDescriptor.name = "p5.externalPrimary";
        engine::StreamingObserverHandle externalPrimary;
        engine::StreamingObserverUpdate externalUpdate;
        externalUpdate.position[0] = 100.0;
        Check(observers->RegisterObserver(externalDescriptor, externalPrimary) && observers->UpdateObserver(externalPrimary, externalUpdate) &&
                  observers->SetPrimaryObserver(externalPrimary),
              "P5 registers an unrelated primary observer before the session observer");

        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure), "P5 first Live observer frame succeeds");
        engine::StreamingObserverSnapshot snapshot;
        Check(observers->GetStats().registeredObservers == 2 && observers->GetSnapshot(snapshot) && snapshot.observerCount == 2 &&
                  !snapshot.usingWorldOriginFallback && std::fabs(snapshot.cameraPosition[0] - 10.0) < 0.01,
              "P5 session observer takes primary ownership from an unrelated primary observer");
        Check(observers->UnregisterObserver(externalPrimary), "P5 releases the unrelated primary observer");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 1 && observers->GetSnapshot(snapshot) &&
                  snapshot.observerCount == 1 && std::fabs(snapshot.observers[0].predictedPosition[0] - 10.0) < 0.01,
              "P5 session observer publishes the precise pose with zero first-sample velocity");
        const engine::StreamingObserverServiceStats firstFrameStats = observers->GetStats();

        // Variable delta: the correct velocity divides the displacement by the previous
        // completed-pose delta (0.01), not the current frame delta (0.04).
        Check(session->UpdateLiveCameraPose(liveToken, MakePose(10.03125f, 0.0f, 0.0f), &sessionFailure), "P5 updates the Live pose");
        frameClock.ticks += 40;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetSnapshot(snapshot) &&
                  std::fabs(snapshot.observers[0].predictedPosition[0] - 13.15625) < 0.02,
              "P5 velocity uses the previous completed-pose simulation time under variable deltas");

        // A paused frame with an unchanged Live pose publishes zero velocity.
        Check(framePipeline->SetPaused(true), "P5 requests a paused frame");
        frameClock.ticks += 20;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetSnapshot(snapshot) &&
                  std::fabs(snapshot.observers[0].predictedPosition[0] - 10.03125) < 0.02,
              "P5 paused frame publishes zero velocity for an unchanged Live pose");
        Check(framePipeline->SetPaused(false), "P5 resumes the frame pipeline");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure), "P5 resume frame succeeds");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure), "P5 post-pause frame succeeds");

        // Teleport/cut resets velocity to zero.
        Check(session->UpdateLiveCameraPose(liveToken, MakePose(5010.0f, 0.0f, 0.0f), &sessionFailure), "P5 teleports the Live pose");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetSnapshot(snapshot) &&
                  std::fabs(snapshot.observers[0].predictedPosition[0] - 5010.0) < 0.02,
              "P5 teleport/cut publishes zero observer velocity");

        // Selection replacement resets velocity history.
        Check(session->SelectLiveCamera(liveCamera, MakePose(20.0f, 0.0f, 0.0f), &liveToken, &sessionFailure), "P5 replaces the Live selection");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetSnapshot(snapshot) &&
                  std::fabs(snapshot.observers[0].predictedPosition[0] - 20.0) < 0.02,
              "P5 selection replacement resets observer velocity");

        // A second render-only camera never becomes a streaming observer.
        rendering::RenderCameraHandle secondaryCamera;
        Check(RegisterLiveCamera("p5.secondaryCamera", secondaryCamera), "P5 registers a render-only secondary camera");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 1 && observers->GetSnapshot(snapshot) &&
                  snapshot.observerCount == 1 && std::fabs(snapshot.cameraPosition[0] - 20.0) < 0.02,
              "P5 render-only secondary camera does not become a streaming observer");

        // Live -> WorldDefault revokes the Live observer before the authored binding.
        Check(session->SelectWorldDefaultCamera(&liveToken, &sessionFailure), "P5 switches to the authored WorldDefault camera");
        Check(observers->GetStats().registeredObservers == 0, "P5 Live -> WorldDefault revokes the Live observer immediately");
        const engine::WorldSessionCameraBinding authoredBinding = session->GetCameraBinding();
        Check(authoredBinding.source == engine::WorldSessionCameraBindingSource::WorldDefault &&
                  authoredBinding.status == engine::WorldSessionCameraBindingStatus::Pending,
              "P5 authored WorldDefault binding is Pending after the Live switch");

        // WorldDefault -> Live restarts explicit Live authority and re-registers one observer.
        rendering::RenderCameraHandle replacementLiveCamera;
        Check(RegisterLiveCamera("p5.replacementLiveCamera", replacementLiveCamera), "P5 registers a replacement Live camera");
        Check(session->SelectLiveCamera(replacementLiveCamera, MakePose(30.0f, 0.0f, 0.0f), &liveToken, &sessionFailure),
              "P5 switches back to explicit Live authority");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 1,
              "P5 WorldDefault -> Live re-registers exactly one observer");

        // A destroyed Live camera is binding-local and removes the observer without failing the world.
        Check(renderingService->GetCommands().UnregisterCamera(replacementLiveCamera), "P5 releases the selected Live camera");
        const engine::WorldSessionCameraBinding staleLiveBinding = session->GetCameraBinding();
        Check(staleLiveBinding.status == engine::WorldSessionCameraBindingStatus::Failed &&
                  staleLiveBinding.failure == engine::WorldSessionCameraBindingFailure::LiveCameraUnavailable,
              "P5 destroyed Live camera reports a binding-local failure");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 0 &&
                  session->GetStatus() == engine::WorldSessionStatus::Running && session->Poll() == engine::WorldSessionStatus::Running,
              "P5 destroyed Live camera removes the observer without failing the world session");

        // Optional observer failure at fixed capacity stays binding-local and self-heals.
        rendering::RenderCameraHandle capacityCamera;
        Check(RegisterLiveCamera("p5.capacityCamera", capacityCamera), "P5 registers the capacity Live camera");
        Check(session->SelectLiveCamera(capacityCamera, MakePose(40.0f, 0.0f, 0.0f), &liveToken, &sessionFailure), "P5 selects the capacity Live camera");
        std::array<engine::StreamingObserverHandle, vanguard::world::MaximumStreamingObservers> externalObservers{};
        bool capacityFilled = true;
        for (vanguard::u32 index = 0; index < vanguard::world::MaximumStreamingObservers; ++index)
        {
            engine::StreamingObserverDescriptor descriptor;
            descriptor.name = "p5.externalObserver";
            capacityFilled = capacityFilled && observers->RegisterObserver(descriptor, externalObservers[index]);
        }
        Check(capacityFilled && observers->GetStats().registeredObservers == vanguard::world::MaximumStreamingObservers,
              "P5 external producers fill the fixed observer capacity");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && session->GetStatus() == engine::WorldSessionStatus::Running &&
                  observers->GetStats().registeredObservers == vanguard::world::MaximumStreamingObservers &&
                  session->GetLastFailure().code == engine::WorldSessionFailureCode::CameraObserverFailure,
              "P5 optional Live observer failure stays binding-local and leaks no registration");
        for (const engine::StreamingObserverHandle handle : externalObservers)
            Check(observers->UnregisterObserver(handle), "P5 external observer release");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 1,
              "P5 optional Live observer self-heals once capacity is free");

        // Explicit withdrawal removes observer authority immediately.
        const engine::WorldSessionCameraSelectionToken staleToken = liveToken;
        Check(session->ClearCameraSelection(liveToken, &sessionFailure), "P5 withdraws the Live selection");
        Check(observers->GetStats().registeredObservers == 0 && session->GetCameraBinding().source == engine::WorldSessionCameraBindingSource::None,
              "P5 ClearCameraSelection removes observer authority immediately");

        // A stale generation token cannot mutate or revoke the replacement selection.
        Check(session->SelectLiveCamera(capacityCamera, MakePose(50.0f, 0.0f, 0.0f), &liveToken, &sessionFailure), "P5 selects a replacement Live camera");
        Check(!session->UpdateLiveCameraPose(staleToken, MakePose(1.0f, 0.0f, 0.0f), &sessionFailure) &&
                  sessionFailure.code == engine::WorldSessionFailureCode::InvalidState,
              "P5 stale selection token cannot update a replacement selection");
        Check(!session->ClearCameraSelection(staleToken, &sessionFailure) && sessionFailure.code == engine::WorldSessionFailureCode::InvalidState,
              "P5 stale selection token cannot revoke a replacement selection");
        frameClock.ticks += 10;
        Check(framePipeline->RunFrame(&frameFailure) && observers->GetStats().registeredObservers == 1 && observers->GetSnapshot(snapshot) &&
                  std::fabs(snapshot.observers[0].predictedPosition[0] - 50.0) < 0.02,
              "P5 stale-token rejection preserves the live replacement observer");
        Check(observers->GetStats().submittedSnapshots > firstFrameStats.submittedSnapshots && observers->GetStats().validObservers <= 1,
              "P5 observer publication stays at most one update per active session frame");

        // Caller-owned Live cameras must be released before world teardown.
        Check(renderingService->GetCommands().UnregisterCamera(liveCamera), "P5 releases the first Live camera");
        Check(renderingService->GetCommands().UnregisterCamera(secondaryCamera), "P5 releases the secondary camera");
        Check(renderingService->GetCommands().UnregisterCamera(capacityCamera), "P5 releases the capacity Live camera");

        // Stop leaves zero observer registrations and no binding.
        Check(session->RequestStop(&sessionFailure) && PollSessionUntil(*session, engine::WorldSessionStatus::Idle) == engine::WorldSessionStatus::Idle,
              "P5 observer session stops cleanly");
        Check(observers->GetStats().registeredObservers == 0 && session->GetCameraBinding().source == engine::WorldSessionCameraBindingSource::None &&
                  session->GetCameraBinding().status == engine::WorldSessionCameraBindingStatus::None,
              "P5 stop leaves zero observer registrations and no camera binding");

        // Restart inherits no stale observer or binding state.
        const bool restarted = session->Begin(start, &sessionFailure);
        const engine::WorldSessionStatus restartedStatus = restarted ? PollSessionUntil(*session, engine::WorldSessionStatus::Running) : session->GetStatus();
        Check(restarted && restartedStatus == engine::WorldSessionStatus::Running && observers->GetStats().registeredObservers == 0 &&
                  session->GetCameraBinding().status == engine::WorldSessionCameraBindingStatus::None,
              "P5 restart inherits no stale observer or camera binding");
        Check(session->RequestStop(&sessionFailure) && PollSessionUntil(*session, engine::WorldSessionStatus::Idle) == engine::WorldSessionStatus::Idle,
              "P5 restarted session stops cleanly");

        // End this isolated host with a deliberately failed stop transaction. This
        // proves the P2 typed GameWorld detail survives WorldSession cleanup and that
        // ownership-empty Failed is a valid host-shutdown terminal state.
        constexpr const char* TerminalSessionRendererFailure = "P3 terminal renderer lifecycle failure";
        const bool terminalSessionBegun = session->Begin(start, &sessionFailure);
        const engine::WorldSessionStatus terminalSessionRunning =
            terminalSessionBegun ? PollSessionUntil(*session, engine::WorldSessionStatus::Running) : session->GetStatus();
        vanguard::entities::RenderingRuntime* const terminalRendering = gameWorld->GetRendering();
        Check(terminalSessionBegun && terminalSessionRunning == engine::WorldSessionStatus::Running && terminalRendering != nullptr,
              "P3 terminal-detail fixture reaches Running with the real world rendering owner");
        if (terminalRendering != nullptr)
            terminalRendering->ReportComponentFailure(TerminalSessionRendererFailure);
        Check(session->RequestStop(&sessionFailure), "P3 terminal-detail fixture admits one stop transaction");
        const engine::WorldSessionStatus terminalSessionStatus = PollSessionUntil(*session, engine::WorldSessionStatus::Failed);
        const engine::WorldSessionFailure terminalSessionFailure = session->GetLastFailure();
        Check(terminalSessionStatus == engine::WorldSessionStatus::Failed &&
                  terminalSessionFailure.code == engine::WorldSessionFailureCode::GameWorldStopFailure &&
                  terminalSessionFailure.gameWorldStopFailure.code == engine::GameWorldStopFailureCode::RendererProgress &&
                  terminalSessionFailure.gameWorldStopFailure.sceneFailure.code == rendering::RenderSceneFailureCode::InvalidState &&
                  terminalSessionFailure.gameWorldStopFailure.sceneFailure.message != nullptr &&
                  std::strcmp(terminalSessionFailure.gameWorldStopFailure.sceneFailure.message, TerminalSessionRendererFailure) == 0 &&
                  gameWorld->GetWorld() == nullptr,
              "P3 WorldSession preserves exact nested GameWorld renderer failure through ownership-empty final Failed");
        Check(session->RequestStop(&sessionFailure) && session->Poll(&sessionFailure) == engine::WorldSessionStatus::Failed &&
                  session->GetLastFailure().gameWorldStopFailure.code == engine::GameWorldStopFailureCode::RendererProgress,
              "P3 final typed Failed stop request is idempotent and does not reopen or erase nested detail");

        Check(streaming->GetPackageSet().Unmount() == vanguard::streaming::PackageSetMountResult::Success, "P5 releases the authored-locator world fixture");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        rendering::RenderCommandFailure renderingDrainFailure;
        Check(renderingService->GetCommands().FlushPreviousFrameProcessing(&renderingDrainFailure),
              "P5 drains rendering tail work before host shutdown");
        Check(liveCameraPhases.Shutdown(&liveCameraPhaseFailure), "P5 shuts down the local live-camera phase registry");
        Check(host.Shutdown(&hostFailure), "P5 observer host shuts down with zero observers and no world camera binding");
    }

    void RunWorldSessionFirstFaultLifecycleTests() noexcept
    {
        namespace engine = vanguard::engine;
        namespace rendering = vanguard::rendering;

        vanguard::application::EngineHost host;
        EmptyInputBackend inputBackend;
        vanguard::application::HostFailure hostFailure;
        const bool registered = engine::RegisterEngineModule(host, &hostFailure) && engine::RegisterIoService(host, &hostFailure) &&
                                engine::RegisterFilesystemService(host, &hostFailure) && engine::RegisterJobsService(host, &hostFailure) &&
                                engine::RegisterFramePipelineService(host, &hostFailure) && engine::RegisterReflectionService(host, &hostFailure) &&
                                engine::RegisterInputService(host, &inputBackend, &hostFailure) && engine::RegisterResourcesService(host, &hostFailure) &&
                                engine::RegisterResourceStreamingService(host, &hostFailure) && engine::RegisterGameInputService(host, &hostFailure) &&
                                engine::RegisterWorldService(host, &hostFailure) && engine::RegisterGameWorldService(host, &hostFailure) &&
                                engine::RegisterStreamingObserverService(host, &hostFailure) && engine::RegisterWorldSessionService(host, &hostFailure) &&
                                engine::RegisterRenderingService(host, &hostFailure);
        const bool compiled = registered && host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure);
        const bool started = compiled && host.Start(&hostFailure);
        Check(started, "P3 first-fault host starts the production service graph");

        engine::ResourceStreamingService* const streaming = engine::FindResourceStreamingService(host);
        engine::GameWorldService* const gameWorld = engine::FindGameWorldService(host);
        engine::WorldSessionService* const session = engine::FindWorldSessionService(host);
        Check(streaming != nullptr && gameWorld != nullptr && session != nullptr,
              "P3 first-fault host publishes the session/world owners");
        if (streaming == nullptr || gameWorld == nullptr || session == nullptr)
            return;

        engine::GameWorldServiceConfig gameWorldConfig;
        gameWorldConfig.components.maximumComponents = 128;
        gameWorldConfig.transforms.maximumTrackedEntities = 64;
        gameWorldConfig.transforms.maximumPlacedComponents = 64;
        gameWorldConfig.transforms.maximumChangesPerSynchronize = 256;
        gameWorldConfig.transforms.transformSystem.maximumComponents = 128;
        gameWorldConfig.transforms.transformSystem.maximumAttachments = 127;
        gameWorldConfig.transforms.transformSystem.maximumScheduledRoots = 64;
        gameWorldConfig.materialization.maximumEntitiesPerCell = 64;
        gameWorldConfig.materialization.maximumComponentsPerCell = 128;
        gameWorldConfig.rendering.scene.maximumProxies = 128;
        gameWorldConfig.rendering.scene.maximumPendingProxyMutations = 128;
        gameWorldConfig.rendering.scene.maximumViews = 4;
        gameWorldConfig.rendering.maximumPendingProxyAdmissions = 128;
        gameWorldConfig.rendering.maximumProxyAdmissionsPerFrame = 32;
        gameWorldConfig.rendering.maximumPendingProxyRetirements = 128;
        gameWorldConfig.rendering.maximumPendingMeshPreparations = 128;
        gameWorldConfig.rendering.maximumMeshPreparationsPerFrame = 32;
        gameWorldConfig.rendering.maximumPendingCameraTransforms = 32;
        gameWorldConfig.world.entities.initialEntityCapacity = 128;
        gameWorldConfig.world.entities.initialActionCapacity = 128;
        gameWorldConfig.world.entities.committedChangeCapacity = 512;
        Check(gameWorld->Configure(gameWorldConfig), "P3 first-fault host configures the bounded Game World fixture");

        vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
        const vanguard::filesystem::AbsolutePath directory =
            vanguard::tests::ScratchDirectory("engine_services_tests").AddDirPath("vanguard_p3_first_fault");
        const vanguard::filesystem::AbsolutePath package = directory.AddFilePath("DATA000.vpak");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        Check(files.CreatePath(directory), "P3 first-fault fixture directory creation");
        vanguard::containers::DynamicArray<vanguard::u8> packageBytes(vanguard::memory::pools::Resources::GetInstance());
        Check(BuildMissingCameraSessionPackage(packageBytes) && SaveBytes(package, packageBytes.Data(), packageBytes.Size()),
              "P3 first-fault publishes the missing-camera world fixture");
        Check(streaming->GetPackageSet().Mount(streaming->GetStreamer(), directory) == vanguard::streaming::PackageSetMountResult::Success,
              "P3 first-fault mounts the missing-camera world fixture");

        engine::WorldSessionFailure sessionFailure;
        engine::WorldSessionStartRequest start;
        start.world = streaming->GetPackageSet().StartupWorld();
        start.inputMode = engine::WorldSessionInputMode::None;
        start.cameraSource = engine::WorldSessionCameraSource::WorldDefault;
        const bool sessionBegun = session->Begin(start, &sessionFailure);
        const engine::WorldSessionStatus runningStatus =
            sessionBegun ? PollSessionUntil(*session, engine::WorldSessionStatus::Running) : session->GetStatus();
        Check(sessionBegun && runningStatus == engine::WorldSessionStatus::Running &&
                  gameWorld->GetWorldCameraResolution().status == engine::WorldCameraResolutionStatus::Pending,
              "P3 first-fault fixture reaches Running with required camera resolution pending");

        bool cameraFrameFailed = false;
        for (vanguard::u32 attempt = 0; attempt < 10'000 && gameWorld->GetStatus() == engine::GameWorldStatus::Running; ++attempt)
        {
            if (!gameWorld->Tick(0.0f))
            {
                cameraFrameFailed = true;
                break;
            }
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        const engine::WorldCameraResolution cameraResolution = gameWorld->GetWorldCameraResolution();
        Check(cameraFrameFailed && gameWorld->GetStatus() == engine::GameWorldStatus::Failed &&
                  cameraResolution.status == engine::WorldCameraResolutionStatus::Failed &&
                  cameraResolution.failure == engine::WorldCameraResolutionFailure::MissingEntity,
              "P3 first-fault fixture produces the required-camera terminal cause before stop cleanup");

        engine::WorldSessionFailure firstFailure;
        Check(session->Poll(&firstFailure) == engine::WorldSessionStatus::Stopping &&
                  firstFailure.code == engine::WorldSessionFailureCode::RequiredCameraFailure &&
                  session->GetLastFailure().code == engine::WorldSessionFailureCode::RequiredCameraFailure,
              "P3 first-fault latches RequiredCamera before a competing cleanup failure exists");
        engine::WorldSessionFailure replacementDuringFailure;
        Check(!session->Begin(start, &replacementDuringFailure) &&
                  replacementDuringFailure.code == engine::WorldSessionFailureCode::InvalidState &&
                  session->GetStatus() == engine::WorldSessionStatus::Stopping &&
                  session->GetLastFailure().code == engine::WorldSessionFailureCode::RequiredCameraFailure,
              "replacement remains rejected while terminal cleanup is Stopping without replacing the first failure");

        constexpr const char* SecondaryRendererFailure = "P3 later cleanup renderer failure";
        vanguard::entities::RenderingRuntime* const renderingRuntime = gameWorld->GetRendering();
        Check(renderingRuntime != nullptr, "P3 first-fault keeps the renderer runtime owned until stop cleanup");
        if (renderingRuntime != nullptr)
            renderingRuntime->ReportComponentFailure(SecondaryRendererFailure);

        const engine::WorldSessionStatus terminalStatus = PollSessionUntil(*session, engine::WorldSessionStatus::Failed);
        const engine::WorldSessionFailure terminalFailure = session->GetLastFailure();
        engine::GameWorldStopFailure secondaryFailure;
        const engine::GameWorldStopResult finalGameWorldStop = gameWorld->StopWorld(&secondaryFailure);
        Check(terminalStatus == engine::WorldSessionStatus::Failed &&
                  terminalFailure.code == engine::WorldSessionFailureCode::RequiredCameraFailure &&
                  terminalFailure.gameWorldStopFailure.code == engine::GameWorldStopFailureCode::None &&
                  finalGameWorldStop == engine::GameWorldStopResult::Failure &&
                  secondaryFailure.code == engine::GameWorldStopFailureCode::RendererProgress &&
                  secondaryFailure.sceneFailure.code == rendering::RenderSceneFailureCode::InvalidState &&
                  secondaryFailure.sceneFailure.message != nullptr &&
                  std::strcmp(secondaryFailure.sceneFailure.message, SecondaryRendererFailure) == 0 &&
                  gameWorld->GetWorld() == nullptr,
              "P3 first terminal session cause wins while the later exact GameWorld renderer cause still drains and remains inspectable");
        engine::WorldSessionFailure replacementAfterFailure;
        Check(!session->Begin(start, &replacementAfterFailure) &&
                  replacementAfterFailure.code == engine::WorldSessionFailureCode::InvalidState &&
                  session->GetStatus() == engine::WorldSessionStatus::Failed &&
                  session->GetLastFailure().code == engine::WorldSessionFailureCode::RequiredCameraFailure,
              "terminal Failed never admits replacement and preserves the original first fault");

        Check(streaming->GetPackageSet().Unmount() == vanguard::streaming::PackageSetMountResult::Success,
              "P3 first-fault releases the missing-camera source after terminal cleanup");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        Check(host.Shutdown(&hostFailure),
              "P3 first-fault host accepts ownership-empty WorldSession Failed and GameWorld FailedComplete");
    }

    void RunWorldSessionPartialStartLifecycleTests() noexcept
    {
        namespace engine = vanguard::engine;

        vanguard::application::EngineHost host;
        EmptyInputBackend inputBackend;
        vanguard::application::HostFailure hostFailure;
        const bool registered = engine::RegisterEngineModule(host, &hostFailure) && engine::RegisterIoService(host, &hostFailure) &&
                                engine::RegisterFilesystemService(host, &hostFailure) && engine::RegisterJobsService(host, &hostFailure) &&
                                engine::RegisterFramePipelineService(host, &hostFailure) && engine::RegisterReflectionService(host, &hostFailure) &&
                                engine::RegisterInputService(host, &inputBackend, &hostFailure) && engine::RegisterResourcesService(host, &hostFailure) &&
                                engine::RegisterResourceStreamingService(host, &hostFailure) && engine::RegisterGameInputService(host, &hostFailure) &&
                                engine::RegisterWorldService(host, &hostFailure) && engine::RegisterGameWorldService(host, &hostFailure) &&
                                engine::RegisterStreamingObserverService(host, &hostFailure) && engine::RegisterWorldSessionService(host, &hostFailure) &&
                                engine::RegisterRenderingService(host, &hostFailure);
        const bool compiled = registered && host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure);
        const bool started = compiled && host.Start(&hostFailure);
        Check(started, "P4 partial-start host starts the production service graph");

        engine::ResourceStreamingService* const streaming = engine::FindResourceStreamingService(host);
        engine::WorldService* const world = engine::FindWorldService(host);
        engine::GameWorldService* const gameWorld = engine::FindGameWorldService(host);
        engine::GameInputService* const gameInput = engine::FindGameInputService(host);
        engine::WorldSessionService* const session = engine::FindWorldSessionService(host);
        Check(streaming != nullptr && world != nullptr && gameWorld != nullptr && gameInput != nullptr && session != nullptr,
              "P4 partial-start host publishes the session startup owners");
        if (streaming == nullptr || world == nullptr || gameWorld == nullptr || gameInput == nullptr || session == nullptr)
            return;

        vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
        const vanguard::filesystem::AbsolutePath directory =
            vanguard::tests::ScratchDirectory("engine_services_tests").AddDirPath("vanguard_p4_partial_start");
        const vanguard::filesystem::AbsolutePath package = directory.AddFilePath("DATA000.vpak");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        Check(files.CreatePath(directory), "P4 partial-start fixture directory creation");
        vanguard::containers::DynamicArray<vanguard::u8> packageBytes(vanguard::memory::pools::Resources::GetInstance());
        Check(BuildWorldSessionPackage(packageBytes, true, false) && SaveBytes(package, packageBytes.Data(), packageBytes.Size()),
              "P4 partial-start publishes a valid world with a terminal input payload");
        Check(streaming->GetPackageSet().Mount(streaming->GetStreamer(), directory) == vanguard::streaming::PackageSetMountResult::Success,
              "P4 partial-start source mounts before startup admission");

        engine::WorldSessionStartRequest request;
        request.world = streaming->GetPackageSet().StartupWorld();
        request.input = streaming->GetPackageSet().GetDefaultInput();
        request.inputMode = engine::WorldSessionInputMode::Mapping;
        request.cameraSource = engine::WorldSessionCameraSource::None;
        engine::WorldSessionFailure failure;
        Check(session->Begin(request, &failure) && session->GetStatus() == engine::WorldSessionStatus::LoadingWorld,
              "P4 partial-start admits both world and input ownership before either terminal result is observed");

        engine::WorldSessionStatus observed = session->GetStatus();
        for (vanguard::u32 attempt = 0; attempt < 10'000 && observed == engine::WorldSessionStatus::LoadingWorld; ++attempt)
        {
            observed = session->Poll(&failure);
            if (observed == engine::WorldSessionStatus::LoadingWorld)
                vanguard::concurrency::SleepOnCurrentThread(1);
        }
        const engine::WorldSessionFailure firstFailure = session->GetLastFailure();
        Check(observed == engine::WorldSessionStatus::Stopping && firstFailure.code == engine::WorldSessionFailureCode::InputLoadFailure &&
                  firstFailure.resourceFailure != vanguard::resources::Failure::None,
              "P4 partial-start terminal input result enters the one Stopping rollback transaction with the concrete first cause");
        engine::WorldSessionFailure repeatedFailure;
        Check(session->RequestStop(&repeatedFailure) && repeatedFailure.code == firstFailure.code &&
                  repeatedFailure.resourceFailure == firstFailure.resourceFailure && session->GetStatus() == engine::WorldSessionStatus::Stopping,
              "P4 partial-start repeated stop is idempotent and cannot replace the first terminal load cause");

        const engine::WorldSessionStatus terminal = PollSessionUntil(*session, engine::WorldSessionStatus::Failed);
        const engine::WorldSessionFailure terminalFailure = session->GetLastFailure();
        Check(terminal == engine::WorldSessionStatus::Failed && terminalFailure.code == firstFailure.code &&
                  terminalFailure.resourceFailure == firstFailure.resourceFailure && world->GetStatus() == engine::WorldResourceStatus::Idle &&
                  gameWorld->GetStatus() == engine::GameWorldStatus::Idle && gameWorld->GetWorld() == nullptr &&
                  gameInput->GetMappings().GetStats().contexts == 0,
              "P4 partial-start rollback reaches ownership-empty Failed without erasing the admitted input failure");

        Check(streaming->GetPackageSet().Unmount() == vanguard::streaming::PackageSetMountResult::Success,
              "P4 partial-start source owner releases only after session ownership is empty");
        static_cast<void>(files.DeleteFile(package));
        static_cast<void>(files.DeletePath(directory));
        Check(host.Shutdown(&hostFailure), "P4 partial-start host accepts ownership-empty terminal WorldSession Failed");
    }

    [[nodiscard]] bool RunWorldSessionObserverLifecycleSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;

        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" %s", executable, WorldSessionObserverChildMode);
        if (commandLength <= 0 || static_cast<vanguard::usize>(commandLength) >= sizeof(commandLine))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
        DWORD exitCode = 1;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0;
#else
        return false;
#endif
    }

    [[nodiscard]] bool RunWorldSessionFirstFaultLifecycleSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;

        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" %s", executable, WorldSessionFirstFaultChildMode);
        if (commandLength <= 0 || static_cast<vanguard::usize>(commandLength) >= sizeof(commandLine))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
        DWORD exitCode = 1;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0;
#else
        return false;
#endif
    }

    [[nodiscard]] bool RunWorldSessionPartialStartLifecycleSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;

        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" %s", executable, WorldSessionPartialStartChildMode);
        if (commandLength <= 0 || static_cast<vanguard::usize>(commandLength) >= sizeof(commandLine))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
        DWORD exitCode = 1;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0;
#else
        return false;
#endif
    }
} // namespace

int main(const int argumentCount, char** const arguments)
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "engineServicesTests"), "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    if (argumentCount == 2 && std::strcmp(arguments[1], WorldSessionObserverChildMode) == 0)
    {
        RunWorldSessionObserverLifecycleTests();
        vanguard::diagnostics::Shutdown();
        return g_failures == 0 ? 0 : 1;
    }
    if (argumentCount == 2 && std::strcmp(arguments[1], WorldSessionFirstFaultChildMode) == 0)
    {
        RunWorldSessionFirstFaultLifecycleTests();
        vanguard::diagnostics::Shutdown();
        return g_failures == 0 ? 0 : 1;
    }
    if (argumentCount == 2 && std::strcmp(arguments[1], WorldSessionPartialStartChildMode) == 0)
    {
        RunWorldSessionPartialStartLifecycleTests();
        vanguard::diagnostics::Shutdown();
        return g_failures == 0 ? 0 : 1;
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete engine module registration");
        Check(vanguard::engine::RegisterJobsService(incompleteHost, &incompleteFailure), "incomplete Jobs service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::JobsServiceId && incompleteFailure.relatedService == vanguard::engine::IoServiceId,
              "Jobs graph rejects a missing I/O dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete filesystem engine module registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure), "incomplete Filesystem service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::FilesystemServiceId && incompleteFailure.relatedService == vanguard::engine::IoServiceId,
              "Filesystem graph rejects a missing I/O dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete resources engine module registration");
        Check(vanguard::engine::RegisterIoService(incompleteHost, &incompleteFailure), "incomplete resources I/O service registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure), "incomplete resources Filesystem service registration");
        Check(vanguard::engine::RegisterResourcesService(incompleteHost, &incompleteFailure), "incomplete Resources service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::ResourcesServiceId && incompleteFailure.relatedService == vanguard::engine::JobsServiceId,
              "Resources graph rejects a missing Jobs dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete Resource Streaming engine module registration");
        Check(vanguard::engine::RegisterIoService(incompleteHost, &incompleteFailure), "incomplete Resource Streaming I/O service registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure), "incomplete Resource Streaming Filesystem service registration");
        Check(vanguard::engine::RegisterJobsService(incompleteHost, &incompleteFailure), "incomplete Resource Streaming Jobs service registration");
        Check(vanguard::engine::RegisterResourceStreamingService(incompleteHost, &incompleteFailure), "incomplete Resource Streaming service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::ResourceStreamingServiceId && incompleteFailure.relatedService == vanguard::engine::ResourcesServiceId,
              "Resource Streaming graph rejects a missing Resources dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete World engine module registration");
        Check(vanguard::engine::RegisterWorldService(incompleteHost, &incompleteFailure), "incomplete World service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::WorldServiceId &&
                  (incompleteFailure.relatedService == vanguard::engine::ResourceStreamingServiceId || incompleteFailure.relatedService == vanguard::engine::ResourcesServiceId),
              "World graph rejects missing resource dependencies");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete Game World engine module registration");
        Check(vanguard::engine::RegisterGameWorldService(incompleteHost, &incompleteFailure), "incomplete Game World service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::GameWorldServiceId,
              "Game World graph rejects missing World dependencies");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete World Session engine module registration");
        Check(vanguard::engine::RegisterWorldSessionService(incompleteHost, &incompleteFailure), "incomplete World Session service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::WorldSessionServiceId,
              "World Session graph rejects missing runtime dependencies");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure), "incomplete Rendering engine module registration");
        Check(vanguard::engine::RegisterRenderingService(incompleteHost, &incompleteFailure), "incomplete Rendering service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) && incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::RenderingServiceId,
              "Rendering graph rejects missing Jobs, Frame Pipeline, and Render Scene dependencies");
    }

    vanguard::application::EngineHost host;
    EmptyInputBackend inputBackend;
    vanguard::application::HostFailure failure;
    LifecycleTrace lifecycleTrace;
    host.SetLifecycleSink(RecordLifecycle, &lifecycleTrace);
    Check(vanguard::engine::RegisterEngineModule(host, &failure), "engine module registration");
    Check(vanguard::engine::RegisterIoService(host, &failure), "I/O service registration");
    Check(vanguard::engine::RegisterFilesystemService(host, &failure), "Filesystem service registration");
    Check(vanguard::engine::RegisterJobsService(host, &failure), "Jobs service registration");
    Check(vanguard::engine::RegisterFramePipelineService(host, &failure), "Frame Pipeline service registration");
    Check(vanguard::engine::RegisterReflectionService(host, &failure), "Reflection service registration");
    Check(vanguard::engine::RegisterInputService(host, &inputBackend, &failure), "Input service registration");
    Check(vanguard::engine::RegisterResourcesService(host, &failure), "Resources service registration");
    Check(vanguard::engine::RegisterResourceStreamingService(host, &failure), "Resource Streaming service registration");
    Check(vanguard::engine::RegisterGameInputService(host, &failure), "Game Input service registration");
    Check(vanguard::engine::RegisterWorldService(host, &failure), "World service registration");
    Check(vanguard::engine::RegisterGameWorldService(host, &failure), "Game World service registration");
    Check(vanguard::engine::RegisterStreamingObserverService(host, &failure), "Streaming Observer service registration");
    Check(vanguard::engine::RegisterWorldSessionService(host, &failure), "World Session service registration");
    Check(vanguard::engine::RegisterRenderingService(host, &failure), "Rendering service registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Runtime, &failure), "engine graph compilation");
    const bool hostStarted = host.Start(&failure);
    if (!hostStarted)
        std::fprintf(stderr, "[engineServicesTests] startup failure: code=%u service=%llu message=%s\n", static_cast<vanguard::u32>(failure.code), static_cast<unsigned long long>(failure.service),
                     failure.message != nullptr ? failure.message : "unspecified");
    Check(hostStarted, "engine graph startup");
    Check(host.GetStateOf(vanguard::engine::JobsServiceId) == vanguard::application::ServiceState::Running, "Jobs service running state");
    Check(host.GetStateOf(vanguard::engine::FramePipelineServiceId) == vanguard::application::ServiceState::Running, "Frame Pipeline service running state");
    Check(host.GetStateOf(vanguard::engine::IoServiceId) == vanguard::application::ServiceState::Running, "I/O service running state");
    Check(host.GetStateOf(vanguard::engine::FilesystemServiceId) == vanguard::application::ServiceState::Running, "Filesystem service running state");
    Check(host.GetStateOf(vanguard::engine::ResourcesServiceId) == vanguard::application::ServiceState::Running, "Resources service running state");
    Check(host.GetStateOf(vanguard::engine::ResourceStreamingServiceId) == vanguard::application::ServiceState::Running, "Resource Streaming service running state");
    Check(host.GetStateOf(vanguard::engine::WorldServiceId) == vanguard::application::ServiceState::Running, "World service running state");
    Check(host.GetStateOf(vanguard::engine::GameWorldServiceId) == vanguard::application::ServiceState::Running, "Game World service running state");
    Check(host.GetStateOf(vanguard::engine::StreamingObserverServiceId) == vanguard::application::ServiceState::Running, "Streaming Observer service running state");
    Check(host.GetStateOf(vanguard::engine::WorldSessionServiceId) == vanguard::application::ServiceState::Running, "World Session service running state");
    Check(host.GetStateOf(vanguard::engine::RenderingServiceId) == vanguard::application::ServiceState::Running, "Rendering service running state");
    Check(host.FindCapability(vanguard::engine::IoCapabilityId) != nullptr, "I/O capability publication");
    Check(host.FindCapability(vanguard::engine::FilesystemCapabilityId) != nullptr, "Filesystem capability publication");
    Check(host.FindCapability(vanguard::engine::JobSchedulerCapabilityId) != nullptr, "Jobs scheduler capability publication");
    Check(host.FindCapability(vanguard::engine::FramePipelineCapabilityId) != nullptr, "Frame Pipeline capability publication");
    Check(host.FindCapability(vanguard::engine::StreamingObserverCapabilityId) != nullptr, "Streaming Observer capability publication");
    Check(host.FindCapability(vanguard::engine::WorldSessionCapabilityId) != nullptr, "World Session capability publication");
    Check(host.FindCapability(vanguard::engine::RenderingCapabilityId) != nullptr, "Rendering capability publication");
    Check(host.FindCapability(vanguard::engine::ResourceRegistryCapabilityId) != nullptr &&
              host.FindCapability(vanguard::engine::ResourceRegistryCapabilityId) == host.FindCapability(vanguard::engine::ResourcePipelineCapabilityId),
          "Resources registry and pipeline capability publication");
    vanguard::engine::ResourcesService* const resourcesService = vanguard::engine::FindResourcesService(host);
    Check(resourcesService != nullptr && resourcesService->GetRegistry().IsInitialized() && resourcesService->GetPipeline().IsInitialized(), "typed Resources service access");
    vanguard::engine::ResourceStreamingService* const resourceStreamingService = vanguard::engine::FindResourceStreamingService(host);
    const vanguard::streaming::Stats resourceStreamingStats = resourceStreamingService != nullptr ? resourceStreamingService->GetStreamer().GetStats() : vanguard::streaming::Stats{};
    Check(resourceStreamingService != nullptr && resourceStreamingService->GetStreamer().IsInitialized() && !resourceStreamingService->GetPackageSet().IsMounted() &&
              resourceStreamingStats.registeredDecoders >= 3 && resourcesService->GetPipeline().GetStats().registeredLoaders >= 5 &&
              resourcesService->GetPipeline().HasLoader(vanguard::shaders::ShaderResourceType) && resourcesService->GetPipeline().HasLoader(vanguard::pipelines::PipelineResourceType) &&
              resourcesService->GetPipeline().HasLoader(vanguard::materials::MaterialResourceType) && resourceStreamingStats.stagingBudgetBytes == 512ull * 1024ull * 1024ull &&
              resourceStreamingStats.activeLoads == 0 && resourceStreamingStats.activeReads == 0,
          "Resource Streaming owns VSHADER, VPPL, and VMAT decoders plus lower metadata loaders");
    vanguard::engine::RenderingService* const renderingService = vanguard::engine::FindRenderingService(host);
    const vanguard::engine::RenderingResourceAllocatorDiagnostics frameResourceDiagnostics =
        renderingService != nullptr ? renderingService->GetResourceAllocatorDiagnostics() : vanguard::engine::RenderingResourceAllocatorDiagnostics{};
    Check(renderingService != nullptr && renderingService->GetScenes().IsInitialized() && renderingService->GetScenes().GetStats().activeScenes == 0 && renderingService->GetCommands().IsInitialized() &&
              renderingService->GetViewports().IsInitialized() && !renderingService->GetMaterialProgramLayouts().IsInitialized() && !renderingService->GetMeshResidency().IsInitialized() &&
              !renderingService->GetTextureResidency().IsInitialized() && !renderingService->GetMaterialResources().IsInitialized() && !renderingService->GetMaterialMaterializer().IsInitialized() &&
              !renderingService->GetMaterialResidency().IsInitialized() && !renderingService->GetMaterialBindings().IsInitialized() && !frameResourceDiagnostics.initialized,
          "typed Rendering service access and device-disabled residency lifecycle");
    Check(renderingService != nullptr && renderingService->GetDefaultCameraPhases().Empty(),
          "device-disabled RenderingService publishes no default camera phases");

    // P4 first-cause/terminal-device policy oracle. These are the exact
    // production-used value transitions around the FrameParticipantStatus boundary
    // and renderer-owner abandonment branches; no fake RHI device or product test
    // mode is required to prove the policy values themselves.
    vanguard::engine::detail::RenderingProgressFailureLatch participantFailureLatch;
    vanguard::engine::RenderingProgressFailure participantFailure;
    participantFailure.code = vanguard::engine::RenderingProgressFailureCode::GpuSceneFailure;
    participantFailure.message = "P4 participant publication failure";
    participantFailure.ownersAbandoned = true;
    participantFailure.gpuSceneFailure.code = vanguard::rendering::GpuSceneRuntimeFailureCode::ContributionFailure;
    participantFailure.gpuSceneFailure.contributionFailure = "P4 nested contribution failure";
    participantFailureLatch.Latch(participantFailure);
    vanguard::engine::RenderingProgressFailure suppressedParticipantFailure;
    suppressedParticipantFailure.code = vanguard::engine::RenderingProgressFailureCode::CommandFailure;
    suppressedParticipantFailure.message = "later participant wrapper failure";
    participantFailureLatch.Latch(suppressedParticipantFailure);
    vanguard::engine::RenderingProgressFailure transferredParticipantFailure;
    const bool participantFailureTransferred = participantFailureLatch.Consume(transferredParticipantFailure);
    vanguard::engine::RenderingProgressFailure replayedParticipantFailure;
    Check(participantFailureTransferred &&
              transferredParticipantFailure.code == vanguard::engine::RenderingProgressFailureCode::GpuSceneFailure &&
              transferredParticipantFailure.ownersAbandoned &&
              transferredParticipantFailure.gpuSceneFailure.code == vanguard::rendering::GpuSceneRuntimeFailureCode::ContributionFailure &&
              transferredParticipantFailure.gpuSceneFailure.contributionFailure != nullptr &&
              std::strcmp(transferredParticipantFailure.gpuSceneFailure.contributionFailure, "P4 nested contribution failure") == 0 &&
              !participantFailureLatch.Consume(replayedParticipantFailure),
          "P4 participant first failure transfers exact nested abandonment evidence once and cannot replay on the next drain");

    vanguard::engine::RenderingProgressFailure removedFailure;
    vanguard::engine::RenderingProgressFailure resetFailure;
    vanguard::engine::RenderingProgressFailure operationalFailure;
    vanguard::engine::RenderingProgressFailure lostReceiptFailure;
    vanguard::engine::detail::BuildSubmittedReceiptCoverageFailure(lostReceiptFailure);
    Check(vanguard::engine::detail::BuildDeviceUnavailableFailure(vanguard::rhi::DeviceState::Removed, removedFailure) &&
              vanguard::engine::detail::BuildDeviceUnavailableFailure(vanguard::rhi::DeviceState::ResetRequired, resetFailure) &&
              !vanguard::engine::detail::BuildDeviceUnavailableFailure(vanguard::rhi::DeviceState::Operational, operationalFailure) &&
              removedFailure.code == vanguard::engine::RenderingProgressFailureCode::DeviceUnavailable && removedFailure.ownersAbandoned &&
              resetFailure.code == vanguard::engine::RenderingProgressFailureCode::DeviceUnavailable && resetFailure.ownersAbandoned &&
              lostReceiptFailure.code == vanguard::engine::RenderingProgressFailureCode::NativeResourceFailure && lostReceiptFailure.ownersAbandoned,
          "P4 Removed, ResetRequired, and lost submitted-receipt coverage carry explicit renderer-owner abandonment evidence");

    RunWorldCameraResolverTests();
    vanguard::rendering::RenderSceneDesc coordinatedSceneDesc;
    coordinatedSceneDesc.name = "Engine frame coordinated scene";
    coordinatedSceneDesc.maximumProxies = 8;
    coordinatedSceneDesc.maximumPendingProxyMutations = 8;
    coordinatedSceneDesc.maximumViews = 1;
    vanguard::rendering::RenderSceneHandle coordinatedScene;
    vanguard::rendering::RenderSceneFailure coordinatedSceneFailure;
    Check(renderingService != nullptr && renderingService->GetScenes().CreateScene(coordinatedSceneDesc, coordinatedScene, &coordinatedSceneFailure), "engine frame coordinated RenderScene creation");
    vanguard::engine::WorldService* const worldService = vanguard::engine::FindWorldService(host);
    Check(worldService != nullptr && worldService->GetStatus() == vanguard::engine::WorldResourceStatus::Idle && worldService->GetResource() == nullptr && worldService->GetGrid() == nullptr &&
              worldService->GetExecutor() == nullptr,
          "typed World service access and empty startup state");
    vanguard::engine::GameWorldService* const gameWorldService = vanguard::engine::FindGameWorldService(host);
    Check(gameWorldService != nullptr && gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Idle && gameWorldService->GetWorld() == nullptr && gameWorldService->GetCellStreaming() == nullptr,
          "typed Game World service access and empty session state");
    vanguard::engine::StreamingObserverService* const streamingObservers = vanguard::engine::FindStreamingObserverService(host);
    Check(streamingObservers != nullptr && streamingObservers->GetStats().registeredObservers == 0, "typed Streaming Observer service access and empty startup state");

    vanguard::engine::StreamingObserverDescriptor cameraDescriptor;
    cameraDescriptor.name = "gameCamera";
    cameraDescriptor.velocityClass = vanguard::engine::StreamingObserverVelocityClass::OnFoot;
    vanguard::engine::StreamingObserverHandle cameraObserver;
    vanguard::engine::StreamingObserverDescriptor vehicleDescriptor;
    vehicleDescriptor.name = "playerVehicle";
    vehicleDescriptor.velocityClass = vanguard::engine::StreamingObserverVelocityClass::GroundVehicle;
    vanguard::engine::StreamingObserverHandle vehicleObserver;
    Check(streamingObservers != nullptr && streamingObservers->RegisterObserver(cameraDescriptor, cameraObserver) && streamingObservers->RegisterObserver(vehicleDescriptor, vehicleObserver),
          "Streaming Observer fixed-capacity registrations");
    vanguard::engine::StreamingObserverUpdate cameraUpdate;
    cameraUpdate.position[0] = 100.0;
    cameraUpdate.position[1] = 200.0;
    cameraUpdate.position[2] = 300.0;
    cameraUpdate.velocity[0] = 10.0;
    vanguard::engine::StreamingObserverUpdate vehicleUpdate;
    vehicleUpdate.velocity[0] = 30.0;
    vehicleUpdate.velocity[1] = 40.0;
    Check(streamingObservers != nullptr && streamingObservers->UpdateObserver(cameraObserver, cameraUpdate) && streamingObservers->UpdateObserver(vehicleObserver, vehicleUpdate) &&
              streamingObservers->SetPrimaryObserver(cameraObserver) && streamingObservers->SetGlobalDistanceScale(1.5f),
          "Streaming Observer producer updates and primary-camera selection");
    vanguard::engine::WorldSessionService* const worldSessionService = vanguard::engine::FindWorldSessionService(host);
    vanguard::engine::GameInputService* const gameInputService = vanguard::engine::FindGameInputService(host);
    Check(worldSessionService != nullptr && worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Idle &&
              worldSessionService->RequestStop() && worldSessionService->Poll() == vanguard::engine::WorldSessionStatus::Idle,
          "typed World Session access and idempotent empty-session stop");
    vanguard::engine::WorldSessionStartRequest invalidStart;
    vanguard::engine::WorldSessionFailure worldSessionFailure;
    const bool invalidBeginAccepted = worldSessionService->Begin(invalidStart, &worldSessionFailure);
    const bool invalidFailureReported = worldSessionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidRequest &&
                                        worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Idle;
    Check(!invalidBeginAccepted && invalidFailureReported && worldSessionService->Poll(&worldSessionFailure) == vanguard::engine::WorldSessionStatus::Idle,
          "P3 pre-admission World Session rejection preserves clean Idle without manufacturing a stop transaction");

    vanguard::engine::WorldSessionStartRequest unspecifiedCameraStart;
    unspecifiedCameraStart.world = vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("world/policy_test.vworld"),
                                                                          vanguard::world::WorldResourceType);
    unspecifiedCameraStart.inputMode = vanguard::engine::WorldSessionInputMode::None;
    const bool unspecifiedCameraAccepted = worldSessionService->Begin(unspecifiedCameraStart, &worldSessionFailure);
    const bool unspecifiedCameraRejected = worldSessionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidRequest &&
                                           worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Idle;
    Check(!unspecifiedCameraAccepted && unspecifiedCameraRejected,
          "P3 explicit camera-source policy rejection leaves replacement admission in clean Idle");

    vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
    const vanguard::filesystem::AbsolutePath sessionDirectory = vanguard::tests::ScratchDirectory("engine_services_tests").AddDirPath("vanguard_world_session_conformance");
    const vanguard::filesystem::AbsolutePath sessionPackage = sessionDirectory.AddFilePath("DATA000.vpak");
    static_cast<void>(files.DeleteFile(sessionPackage));
    static_cast<void>(files.DeletePath(sessionDirectory));
    Check(files.CreatePath(sessionDirectory), "World Session fixture directory creation");
    vanguard::containers::DynamicArray<vanguard::u8> sessionPackageBytes(vanguard::memory::pools::Resources::GetInstance());
    Check(BuildWorldSessionPackage(sessionPackageBytes) && SaveBytes(sessionPackage, sessionPackageBytes.Data(), sessionPackageBytes.Size()), "World Session DATA000 fixture publication");

    vanguard::engine::WorldSessionStartRequest initialSessionStart;
    const auto packageMounted = resourceStreamingService->GetPackageSet().Mount(resourceStreamingService->GetStreamer(), sessionDirectory);
    Check(packageMounted == vanguard::streaming::PackageSetMountResult::Success, "source owner mounts the session fixture");
    initialSessionStart.world = resourceStreamingService->GetPackageSet().StartupWorld();
    initialSessionStart.input = resourceStreamingService->GetPackageSet().GetDefaultInput();
    initialSessionStart.inputMode = vanguard::engine::WorldSessionInputMode::Mapping;
    initialSessionStart.cameraSource = vanguard::engine::WorldSessionCameraSource::WorldDefault;
    const bool initialSessionStarted = worldSessionService->Begin(initialSessionStart, &worldSessionFailure);
    Check(initialSessionStarted && worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::LoadingWorld &&
              resourceStreamingService->GetPackageSet().IsMounted(),
          "World Session begins from explicit resources in an existing source");
    const bool firstStopAccepted = worldSessionService->RequestStop(&worldSessionFailure);
    const bool replacementWhileStoppingAccepted = worldSessionService->Begin(initialSessionStart, &worldSessionFailure);
    Check(!replacementWhileStoppingAccepted && worldSessionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidState &&
              worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Stopping,
          "P3 replacement is rejected while the prior session stop transaction still owns state");
    const auto firstStopStatus = PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Idle);
    Check(firstStopAccepted && firstStopStatus == vanguard::engine::WorldSessionStatus::Idle &&
              worldService->GetStatus() == vanguard::engine::WorldResourceStatus::Idle &&
              gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Idle &&
              gameInputService->GetMappings().GetStats().contexts == 0 &&
              resourceStreamingService->GetPackageSet().IsMounted(),
          "P4 stop-while-loading cancels partial world/input ownership exactly once and preserves the process source");

    vanguard::engine::WorldSessionStartRequest retainedSessionStart;
    retainedSessionStart.world = resourceStreamingService->GetPackageSet().StartupWorld();
    retainedSessionStart.inputMode = vanguard::engine::WorldSessionInputMode::None;
    retainedSessionStart.cameraSource = vanguard::engine::WorldSessionCameraSource::None;
    const bool retainedSessionStarted = worldSessionService->Begin(retainedSessionStart, &worldSessionFailure);
    Check(retainedSessionStarted && worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::LoadingWorld &&
              gameInputService->GetMappings().GetStats().contexts == 0,
          "edit-only session begins without inherited input mappings");
    const bool fullStopAccepted = worldSessionService->RequestStop(&worldSessionFailure);
    const vanguard::engine::WorldSessionStatus fullStopStatus = PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Idle);
    Check(fullStopAccepted && fullStopStatus == vanguard::engine::WorldSessionStatus::Idle &&
              worldService->GetStatus() == vanguard::engine::WorldResourceStatus::Idle &&
              gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Idle &&
              resourceStreamingService->GetPackageSet().IsMounted(),
          "P4 no-input/no-view loading stop reaches ownership-empty Idle without an output lifecycle");
    const auto packageUnmounted = resourceStreamingService->GetPackageSet().Unmount();
    Check(packageUnmounted == vanguard::streaming::PackageSetMountResult::Success, "source owner releases the fixture after sessions stop");

    static_cast<void>(files.DeleteFile(sessionPackage));
    sessionPackageBytes.Clear();
    Check(BuildWorldSessionPackage(sessionPackageBytes, true) && SaveBytes(sessionPackage, sessionPackageBytes.Data(), sessionPackageBytes.Size()),
          "P2 valid World Session fixture publication");
    vanguard::engine::GameWorldServiceConfig p2GameWorldConfig;
    p2GameWorldConfig.components.maximumComponents = 128;
    p2GameWorldConfig.transforms.maximumTrackedEntities = 64;
    p2GameWorldConfig.transforms.maximumPlacedComponents = 64;
    p2GameWorldConfig.transforms.maximumChangesPerSynchronize = 256;
    p2GameWorldConfig.transforms.transformSystem.maximumComponents = 128;
    p2GameWorldConfig.transforms.transformSystem.maximumAttachments = 127;
    p2GameWorldConfig.transforms.transformSystem.maximumScheduledRoots = 64;
    p2GameWorldConfig.materialization.maximumEntitiesPerCell = 64;
    p2GameWorldConfig.materialization.maximumComponentsPerCell = 128;
    p2GameWorldConfig.rendering.scene.maximumProxies = 128;
    p2GameWorldConfig.rendering.scene.maximumPendingProxyMutations = 128;
    p2GameWorldConfig.rendering.scene.maximumViews = 4;
    p2GameWorldConfig.rendering.maximumPendingProxyAdmissions = 128;
    p2GameWorldConfig.rendering.maximumProxyAdmissionsPerFrame = 32;
    p2GameWorldConfig.rendering.maximumPendingProxyRetirements = 128;
    p2GameWorldConfig.rendering.proxyRetirementDelayFrames = 4;
    p2GameWorldConfig.rendering.maximumPendingMeshPreparations = 128;
    p2GameWorldConfig.rendering.maximumMeshPreparationsPerFrame = 32;
    p2GameWorldConfig.rendering.maximumPendingCameraTransforms = 32;
    p2GameWorldConfig.world.entities.initialEntityCapacity = 128;
    p2GameWorldConfig.world.entities.initialActionCapacity = 128;
    p2GameWorldConfig.world.entities.committedChangeCapacity = 512;
    Check(gameWorldService->Configure(p2GameWorldConfig), "P2 bounded Game World fixture configuration");
    const auto p2PackageMounted = resourceStreamingService->GetPackageSet().Mount(resourceStreamingService->GetStreamer(), sessionDirectory);
    Check(p2PackageMounted == vanguard::streaming::PackageSetMountResult::Success, "P2 source owner mounts the valid world fixture");
    vanguard::engine::WorldSessionStartRequest p2SessionStart;
    p2SessionStart.world = resourceStreamingService->GetPackageSet().StartupWorld();
    p2SessionStart.inputMode = vanguard::engine::WorldSessionInputMode::None;
    p2SessionStart.cameraSource = vanguard::engine::WorldSessionCameraSource::None;
    const bool p2SessionStarted = worldSessionService->Begin(p2SessionStart, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus p2SessionStatus =
        p2SessionStarted ? PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Running) : worldSessionService->GetStatus();
    vanguard::entities::RenderingRuntime* const p2Rendering = gameWorldService->GetRendering();
    vanguard::game::GameWorld* const p2World = gameWorldService->GetWorld();
    Check(p2SessionStarted && p2SessionStatus == vanguard::engine::WorldSessionStatus::Running &&
              gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Running && p2World != nullptr && p2Rendering != nullptr &&
              p2World->GetSystem(vanguard::entities::RenderingRuntimeSystemId) == p2Rendering,
          "P2 world reaches Running only after its single RenderingRuntime is installed");
    Check(p2Rendering != nullptr && p2Rendering->GetCommands() == &renderingService->GetCommands() &&
              p2Rendering->GetMeshResidency() == &renderingService->GetMeshResidency() &&
              p2Rendering->GetDefaultCameraPhases() == renderingService->GetDefaultCameraPhases(),
          "P2 RenderingRuntime binds the shared renderer owners and value camera phase set before activation");
    const vanguard::entities::RenderingRuntimeStats p2RenderingStats =
        p2Rendering != nullptr ? p2Rendering->GetStats() : vanguard::entities::RenderingRuntimeStats{};
    Check(p2Rendering != nullptr && p2RenderingStats.initialized && p2RenderingStats.ownsScene && p2Rendering->GetScene().IsValid() &&
              !p2Rendering->GetMeshResidency()->IsInitialized() && p2Rendering->GetDefaultCameraPhases().Empty(),
          "P2 device-disabled world remains valid with an owned scene while rendering capabilities stay unavailable until component use");

    // Integrated deterministic replacement matrix: keep an accepted retirement alive long
    // enough to observe repeated Stopping polls. Only the renderer owner may advance
    // its delay; no outer frame, viewport submission/presentation or source-owner
    // rebuild is allowed while replacement remains rejected.
    const auto* const replacementRenderingOwner = renderingService;
    const auto* const replacementScenesOwner = &renderingService->GetScenes();
    const auto* const replacementCommandsOwner = &renderingService->GetCommands();
    const auto* const replacementStreamingOwner = resourceStreamingService;
    const auto* const replacementStreamerOwner = &resourceStreamingService->GetStreamer();
    const auto* const replacementPackageOwner = &resourceStreamingService->GetPackageSet();
    const auto* const replacementRootPackageBefore = resourceStreamingService->GetPackageSet().FindPackage(0);
    const auto* const replacementRootPackageReader =
        replacementRootPackageBefore != nullptr ? replacementRootPackageBefore->reader : nullptr;
    const vanguard::resources::ResourceReference replacementMountedWorld = p2SessionStart.world;
    const vanguard::streaming::Stats replacementStreamingStatsBefore = resourceStreamingService->GetStreamer().GetStats();
    vanguard::engine::FramePipelineService* const replacementFramePipeline = vanguard::engine::FindFramePipelineService(host);
    const vanguard::u64 replacementOuterFramesBeforeStop =
        replacementFramePipeline != nullptr ? replacementFramePipeline->GetStats().frames : ~vanguard::u64{0};
    const vanguard::u64 replacementGameWorldFramesBeforeStop = p2World->GetStats().frames;
    const vanguard::rendering::ViewportManagerStats replacementViewportStatsBeforeStop = renderingService->GetViewports().GetStats();
    const vanguard::rendering::RenderCommandSystemStats replacementCommandStatsBeforeStop = renderingService->GetCommands().GetStats();
    const vanguard::rendering::RenderSceneHandle replacementOldWorldScene = p2Rendering->GetScene();
    vanguard::rendering::RenderProxyDesc replacementDelayedProxyDesc;
    replacementDelayedProxyDesc.scene = replacementOldWorldScene;
    replacementDelayedProxyDesc.typeId = 0x50355245u;
    replacementDelayedProxyDesc.producerId = 0x50355245u;
    replacementDelayedProxyDesc.producerGeneration = 1u;
    replacementDelayedProxyDesc.debugName = "replacement retirement";
    for (vanguard::u32 axis = 0; axis < 3; ++axis)
    {
        replacementDelayedProxyDesc.bounds.minimum[axis] = -1.0f;
        replacementDelayedProxyDesc.bounds.maximum[axis] = 1.0f;
    }
    vanguard::rendering::RenderProxyHandle replacementDelayedProxy;
    vanguard::rendering::RenderSceneFailure replacementDelayedProxyFailure;
    Check(replacementFramePipeline != nullptr &&
              renderingService->GetScenes().CreateProxy(replacementDelayedProxyDesc, replacementDelayedProxy,
                                                        &replacementDelayedProxyFailure) &&
              p2Rendering->RetireProxy(replacementDelayedProxy, &replacementDelayedProxyFailure),
          "replacement proof admits one real delayed renderer retirement before session cutover");

    const bool p2StopAccepted = worldSessionService->RequestStop(&worldSessionFailure);
    vanguard::engine::WorldSessionFailure replacementAdmissionFailure;
    const bool replacementBeforeProgress = worldSessionService->Begin(p2SessionStart, &replacementAdmissionFailure);
    Check(p2StopAccepted && !replacementBeforeProgress &&
              replacementAdmissionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidState &&
              worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Stopping,
          "replacement is rejected immediately after stop cutover while old ownership is still live");

    vanguard::u32 replacementStoppingPolls = 0;
    vanguard::engine::WorldSessionStatus p2StopStatus = worldSessionService->GetStatus();
    for (vanguard::u32 attempt = 0; attempt < 64 && p2StopStatus == vanguard::engine::WorldSessionStatus::Stopping; ++attempt)
    {
        p2StopStatus = worldSessionService->Poll(&worldSessionFailure);
        if (p2StopStatus != vanguard::engine::WorldSessionStatus::Stopping)
            break;
        ++replacementStoppingPolls;
        replacementAdmissionFailure = {};
        vanguard::game::GameWorld* const stoppingWorld = gameWorldService->GetWorld();
        Check(stoppingWorld != nullptr && stoppingWorld->GetStats().frames == replacementGameWorldFramesBeforeStop &&
                  !worldSessionService->Begin(p2SessionStart, &replacementAdmissionFailure) &&
                  replacementAdmissionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidState &&
                  worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Stopping,
              "repeated stop progress never re-enters GameWorld::Tick and rejects replacement across each Pending renderer epoch");
    }
    const vanguard::rendering::ViewportManagerStats replacementViewportStatsAfterStop = renderingService->GetViewports().GetStats();
    const vanguard::rendering::RenderCommandSystemStats replacementCommandStatsAfterStop = renderingService->GetCommands().GetStats();
    const vanguard::streaming::Stats replacementStreamingStatsAfterStop = resourceStreamingService->GetStreamer().GetStats();
    const auto* const replacementRootPackageAfter = resourceStreamingService->GetPackageSet().FindPackage(0);
    vanguard::rendering::RenderSceneSnapshot retiredWorldSceneSnapshot;
    Check(p2StopStatus == vanguard::engine::WorldSessionStatus::Idle && replacementStoppingPolls >= 2u &&
              replacementRootPackageReader != nullptr &&
              gameWorldService->GetWorld() == nullptr && gameWorldService->GetRendering() == nullptr &&
              resourceStreamingService->GetPackageSet().IsMounted() &&
              resourceStreamingService->GetPackageSet().StartupWorld() == replacementMountedWorld &&
              replacementRootPackageAfter != nullptr && replacementRootPackageAfter->reader == replacementRootPackageReader &&
              vanguard::engine::FindRenderingService(host) == replacementRenderingOwner &&
              vanguard::engine::FindResourceStreamingService(host) == replacementStreamingOwner &&
              &renderingService->GetScenes() == replacementScenesOwner &&
              &renderingService->GetCommands() == replacementCommandsOwner &&
              &resourceStreamingService->GetStreamer() == replacementStreamerOwner &&
              &resourceStreamingService->GetPackageSet() == replacementPackageOwner &&
              replacementStreamingStatsAfterStop.registeredDecoders == replacementStreamingStatsBefore.registeredDecoders &&
              replacementStreamingStatsAfterStop.mountedPackages == replacementStreamingStatsBefore.mountedPackages &&
              replacementStreamingStatsAfterStop.liveSources == replacementStreamingStatsBefore.liveSources &&
              replacementFramePipeline->GetStats().frames == replacementOuterFramesBeforeStop &&
              replacementViewportStatsAfterStop.submittedFrames == replacementViewportStatsBeforeStop.submittedFrames &&
              replacementViewportStatsAfterStop.presentedFrames == replacementViewportStatsBeforeStop.presentedFrames &&
              replacementCommandStatsAfterStop.frameTicks > replacementCommandStatsBeforeStop.frameTicks &&
              !renderingService->GetScenes().GetSnapshot(replacementOldWorldScene, retiredWorldSceneSnapshot),
          "repeated Pending stop progress reaches clean Idle using renderer-only epochs while engine-lifetime renderer/source owners and the mounted source survive");

    const bool replacementStarted = worldSessionService->Begin(p2SessionStart, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus replacementStatus =
        replacementStarted ? PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Running)
                             : worldSessionService->GetStatus();
    vanguard::entities::RenderingRuntime* const replacementRendering = gameWorldService->GetRendering();
    const vanguard::rendering::RenderSceneHandle replacementScene =
        replacementRendering != nullptr ? replacementRendering->GetScene() : vanguard::rendering::RenderSceneHandle{};
    const auto* const replacementRootPackageRunning = resourceStreamingService->GetPackageSet().FindPackage(0);
    Check(replacementStarted && replacementStatus == vanguard::engine::WorldSessionStatus::Running &&
              replacementRendering != nullptr && replacementScene.IsValid() && replacementScene != replacementOldWorldScene &&
              replacementRendering->GetCommands() == replacementCommandsOwner &&
              replacementRendering->GetMeshResidency() == &replacementRenderingOwner->GetMeshResidency() &&
              vanguard::engine::FindRenderingService(host) == replacementRenderingOwner &&
              vanguard::engine::FindResourceStreamingService(host) == replacementStreamingOwner &&
              &resourceStreamingService->GetStreamer() == replacementStreamerOwner &&
              resourceStreamingService->GetPackageSet().IsMounted() &&
              resourceStreamingService->GetPackageSet().StartupWorld() == replacementMountedWorld &&
              replacementRootPackageRunning != nullptr && replacementRootPackageRunning->reader == replacementRootPackageReader,
          "replacement is admitted only from clean Idle and rebuilds per-world scene ownership while reusing engine-lifetime renderer/source owners");
    Check(worldSessionService->RequestStop(&worldSessionFailure) &&
              PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Idle) ==
                  vanguard::engine::WorldSessionStatus::Idle &&
              resourceStreamingService->GetPackageSet().IsMounted(),
          "replacement session stops cleanly without releasing its process source owner");

    vanguard::rendering::RenderSceneDesc p2AdoptedSceneDesc{"p2.adopted"};
    p2AdoptedSceneDesc.ownership = vanguard::rendering::RenderSceneOwnership::External;
    p2AdoptedSceneDesc.maximumProxies = 128;
    p2AdoptedSceneDesc.maximumPendingProxyMutations = 128;
    p2AdoptedSceneDesc.maximumViews = 4;
    vanguard::rendering::RenderSceneHandle p2AdoptedScene;
    vanguard::rendering::RenderSceneFailure p2SceneFailure;
    Check(renderingService->GetScenes().CreateScene(p2AdoptedSceneDesc, p2AdoptedScene, &p2SceneFailure),
          "P2 external owner creates the scene that the next world will adopt");
    p2GameWorldConfig.rendering.existingScene = p2AdoptedScene;
    Check(gameWorldService->Configure(p2GameWorldConfig), "P2 adopted-scene Game World fixture configuration");
    const bool p2AdoptedSessionStarted = worldSessionService->Begin(p2SessionStart, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus p2AdoptedSessionStatus =
        p2AdoptedSessionStarted ? PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Running) : worldSessionService->GetStatus();
    vanguard::entities::RenderingRuntime* const p2AdoptedRendering = gameWorldService->GetRendering();
    const vanguard::entities::RenderingRuntimeStats p2AdoptedStats =
        p2AdoptedRendering != nullptr ? p2AdoptedRendering->GetStats() : vanguard::entities::RenderingRuntimeStats{};
    Check(p2AdoptedSessionStarted && p2AdoptedSessionStatus == vanguard::engine::WorldSessionStatus::Running && p2AdoptedRendering != nullptr &&
              p2AdoptedRendering->GetScene() == p2AdoptedScene && p2AdoptedStats.initialized && !p2AdoptedStats.ownsScene,
          "P2 RenderingRuntime adopts one existing render scene without taking renderer-global ownership");
    const bool p2AdoptedStopAccepted = worldSessionService->RequestStop(&worldSessionFailure);
    const vanguard::engine::WorldSessionStatus p2AdoptedStopStatus = PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Idle);
    vanguard::rendering::RenderSceneSnapshot p2AdoptedSnapshot;
    Check(p2AdoptedStopAccepted && p2AdoptedStopStatus == vanguard::engine::WorldSessionStatus::Idle &&
              renderingService->GetScenes().GetSnapshot(p2AdoptedScene, p2AdoptedSnapshot),
          "P2 adopted render scene survives world teardown for its external owner");
    Check(renderingService->GetScenes().DestroyScene(p2AdoptedScene, &p2SceneFailure), "P2 external owner destroys the adopted scene after world teardown");

    const auto p2PackageUnmounted = resourceStreamingService->GetPackageSet().Unmount();
    Check(p2PackageUnmounted == vanguard::streaming::PackageSetMountResult::Success, "P2 source owner releases the valid world fixture");
    static_cast<void>(files.DeleteFile(sessionPackage));
    sessionPackageBytes.Clear();
    Check(BuildMissingCameraSessionPackage(sessionPackageBytes) &&
              SaveBytes(sessionPackage, sessionPackageBytes.Data(), sessionPackageBytes.Size()),
          "P2 missing-camera session fixture publication");
    p2GameWorldConfig.rendering.existingScene = {};
    Check(gameWorldService->Configure(p2GameWorldConfig), "P2 missing-camera Game World fixture configuration");
    const auto missingCameraPackageMounted =
        resourceStreamingService->GetPackageSet().Mount(resourceStreamingService->GetStreamer(), sessionDirectory);
    Check(missingCameraPackageMounted == vanguard::streaming::PackageSetMountResult::Success,
          "P2 source owner mounts the missing-camera world fixture");

    vanguard::engine::WorldSessionStartRequest missingCameraSessionStart;
    missingCameraSessionStart.world = resourceStreamingService->GetPackageSet().StartupWorld();
    missingCameraSessionStart.inputMode = vanguard::engine::WorldSessionInputMode::None;
    missingCameraSessionStart.cameraSource = vanguard::engine::WorldSessionCameraSource::WorldDefault;
    const bool missingCameraSessionStarted = worldSessionService->Begin(missingCameraSessionStart, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus missingCameraRunningStatus =
        missingCameraSessionStarted ? PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Running)
                                    : worldSessionService->GetStatus();
    Check(missingCameraSessionStarted && missingCameraRunningStatus == vanguard::engine::WorldSessionStatus::Running &&
              gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Running &&
              gameWorldService->GetWorldCameraResolution().status == vanguard::engine::WorldCameraResolutionStatus::Pending &&
              gameWorldService->GetWorldCameraResolution().locator.cellId == 1 &&
              gameWorldService->GetWorldCameraResolution().locator.entityId == 999 &&
              gameWorldService->GetWorldCameraResolution().locator.componentStableId == 77,
          "P2 WorldDefault copies the authored locator into one pending Game World resolution transaction");

    bool missingCameraFrameFailed = false;
    for (vanguard::u32 attempt = 0; attempt < 10'000 && gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Running; ++attempt)
    {
        if (!gameWorldService->Tick(0.0f))
        {
            missingCameraFrameFailed = true;
            break;
        }
        vanguard::concurrency::SleepOnCurrentThread(1);
    }
    const vanguard::engine::WorldCameraResolution missingCameraResolution = gameWorldService->GetWorldCameraResolution();
    Check(missingCameraFrameFailed && gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Failed &&
              missingCameraResolution.status == vanguard::engine::WorldCameraResolutionStatus::Failed &&
              missingCameraResolution.failure == vanguard::engine::WorldCameraResolutionFailure::MissingEntity &&
              !missingCameraResolution.component.IsValid() && !missingCameraResolution.camera.IsValid(),
          "P2 required camera terminal resolution failure fails the real Game World frame path");

    vanguard::engine::WorldSessionFailure mirroredCameraFailure;
    const vanguard::engine::WorldSessionStatus mirroredCameraStatus = worldSessionService->Poll(&mirroredCameraFailure);
    Check(mirroredCameraStatus == vanguard::engine::WorldSessionStatus::Stopping &&
              mirroredCameraFailure.code == vanguard::engine::WorldSessionFailureCode::RequiredCameraFailure &&
              worldSessionService->GetLastFailure().code == vanguard::engine::WorldSessionFailureCode::RequiredCameraFailure,
          "P3 admitted required-camera failure latches its first cause while cleanup remains Stopping");
    const vanguard::engine::WorldSessionStatus missingCameraStopStatus =
        PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Failed);
    Check(missingCameraStopStatus == vanguard::engine::WorldSessionStatus::Failed &&
              worldSessionService->GetLastFailure().code == vanguard::engine::WorldSessionFailureCode::RequiredCameraFailure &&
              worldSessionService->GetLastFailure().gameWorldStopFailure.code == vanguard::engine::GameWorldStopFailureCode::None &&
              gameWorldService->GetStatus() == vanguard::engine::GameWorldStatus::Idle &&
              gameWorldService->GetWorld() == nullptr &&
              gameWorldService->GetWorldCameraResolution().status == vanguard::engine::WorldCameraResolutionStatus::None &&
              !gameWorldService->GetWorldCameraResolution().component.IsValid() &&
              !gameWorldService->GetWorldCameraResolution().camera.IsValid(),
          "P3 terminal cleanup preserves the original required-camera cause and finalizes ownership-empty Failed");
    const bool failedStopAccepted = worldSessionService->RequestStop(&worldSessionFailure);
    const vanguard::engine::WorldSessionStatus failedStopStatus = worldSessionService->Poll(&worldSessionFailure);
    const bool failedReplacementAccepted = worldSessionService->Begin(missingCameraSessionStart, &worldSessionFailure);
    Check(failedStopAccepted && failedStopStatus == vanguard::engine::WorldSessionStatus::Failed && !failedReplacementAccepted &&
              worldSessionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidState &&
              worldSessionService->GetStatus() == vanguard::engine::WorldSessionStatus::Failed &&
              worldSessionService->GetLastFailure().code == vanguard::engine::WorldSessionFailureCode::RequiredCameraFailure,
          "P3 final Failed is non-reopenable and blocks replacement without erasing the original terminal cause");
    const auto missingCameraPackageUnmounted = resourceStreamingService->GetPackageSet().Unmount();
    Check(missingCameraPackageUnmounted == vanguard::streaming::PackageSetMountResult::Success,
          "P2 source owner releases the missing-camera world fixture");
    static_cast<void>(files.DeleteFile(sessionPackage));
    static_cast<void>(files.DeletePath(sessionDirectory));
    const vanguard::rendering::RenderCommandSystemStats renderingStatsBeforeFramePipeline = renderingService->GetCommands().GetStats();
    const vanguard::rendering::RenderSceneManagerStats coordinatedSceneStatsBeforeFramePipeline = renderingService->GetScenes().GetStats();
    vanguard::engine::FramePipelineService* const framePipeline = vanguard::engine::FindFramePipelineService(host);
    vanguard::engine::FrameFailure frameFailure;
    FakeFrameClock frameClock;
    vanguard::engine::FramePipelineConfig frameConfig;
    frameConfig.clock = {&ReadFakeFrameClock, 1'000, &frameClock};
    frameConfig.maximumDeltaSeconds = 0.1f;
    frameConfig.fixedDeltaSeconds = 0.02f;
    frameConfig.maximumFixedStepsPerFrame = 2;
    frameConfig.pacing = vanguard::engine::FramePacingMode::Disabled;
    Check(framePipeline != nullptr && framePipeline->Configure(frameConfig, &frameFailure), "deterministic Frame Pipeline configuration");

    constexpr vanguard::engine::FrameParticipantId InputParticipant = 100;
    constexpr vanguard::engine::FrameParticipantId FixedParticipant = 200;
    constexpr vanguard::engine::FrameParticipantId SimulationBeforeParticipant = 20;
    constexpr vanguard::engine::FrameParticipantId SimulationAfterParticipant = 10;
    constexpr vanguard::engine::FrameParticipantId FailureParticipant = 300;
    constexpr vanguard::engine::FrameParticipantId RenderUpdateProducerParticipant = 400;
    constexpr vanguard::engine::FrameParticipantId RenderFrameSourceParticipant = 500;
    FrameTrace frameTrace;
    FrameParticipantFixture inputFixture{&frameTrace, InputParticipant};
    FrameParticipantFixture fixedFixture{&frameTrace, FixedParticipant};
    FrameParticipantFixture simulationBeforeFixture{&frameTrace, SimulationBeforeParticipant};
    FrameParticipantFixture simulationAfterFixture{&frameTrace, SimulationAfterParticipant};
    FrameParticipantFixture failureFixture{&frameTrace, FailureParticipant};
    FrameParticipantFixture renderUpdateProducerFixture{&frameTrace, RenderUpdateProducerParticipant};
    FrameParticipantFixture renderFrameSourceFixture{&frameTrace, RenderFrameSourceParticipant};

    auto RegisterFrameFixture = [&](FrameParticipantFixture& fixture, const vanguard::engine::FramePhase phase, const vanguard::containers::ArraySpan<const vanguard::engine::FrameParticipantId> after = {})
    {
        vanguard::engine::FrameParticipantDescriptor descriptor;
        descriptor.id = fixture.id;
        descriptor.name = "framePipelineTest";
        descriptor.phase = phase;
        descriptor.affinity = vanguard::application::ThreadAffinity::MainThread;
        descriptor.after = after;
        descriptor.execute = &RecordFrameParticipant;
        descriptor.userData = &fixture;
        return framePipeline->RegisterParticipant(descriptor, &frameFailure);
    };

    Check(RegisterFrameFixture(inputFixture, vanguard::engine::FramePhase::Input), "Input phase participant registration");
    Check(RegisterFrameFixture(fixedFixture, vanguard::engine::FramePhase::FixedSimulation), "fixed phase participant registration");
    Check(RegisterFrameFixture(simulationBeforeFixture, vanguard::engine::FramePhase::Simulation), "first Simulation participant registration");
    const vanguard::engine::FrameParticipantId simulationDependencies[]{SimulationBeforeParticipant};
    Check(RegisterFrameFixture(simulationAfterFixture, vanguard::engine::FramePhase::Simulation, {simulationDependencies, 1}), "dependent Simulation participant registration");
    const vanguard::engine::FrameParticipantId renderUpdateDependencies[]{vanguard::engine::RenderingUpdateFrameParticipantId};
    Check(RegisterFrameFixture(renderUpdateProducerFixture, vanguard::engine::FramePhase::RenderUpdate, {renderUpdateDependencies, 1}), "render producer registration behind the Rendering update boundary");
    const vanguard::engine::FrameParticipantId renderFrameDependencies[]{vanguard::engine::RenderingFrameTickParticipantId};
    Check(RegisterFrameFixture(renderFrameSourceFixture, vanguard::engine::FramePhase::Render, {renderFrameDependencies, 1}), "render frame source registration behind Rendering FrameTick");
    Check(RegisterFrameFixture(failureFixture, vanguard::engine::FramePhase::EndFrame), "End Frame participant registration");

    vanguard::engine::FrameParticipantDescriptor unsupportedParticipant;
    unsupportedParticipant.id = 999;
    unsupportedParticipant.name = "unsupportedWorkerParticipant";
    unsupportedParticipant.affinity = vanguard::application::ThreadAffinity::AnyWorker;
    unsupportedParticipant.execute = &RecordFrameParticipant;
    Check(!framePipeline->RegisterParticipant(unsupportedParticipant, &frameFailure) && frameFailure.code == vanguard::engine::FrameFailureCode::UnsupportedAffinity,
          "unsupported worker affinity is rejected explicitly");
    Check(framePipeline->Compile(&frameFailure) && framePipeline->GetState() == vanguard::engine::FramePipelineState::Compiled && framePipeline->GetStats().scheduledParticipants == 15,
          "compiled Frame Pipeline contains deterministic service and test participants");
    FrameParticipantSearch observerParticipantSearch;
    observerParticipantSearch.id = vanguard::engine::WorldSessionCameraObserverParticipantId;
    framePipeline->VisitParticipantStats(&FindFrameParticipantStats, &observerParticipantSearch);
    Check(observerParticipantSearch.found && observerParticipantSearch.phase == vanguard::engine::FramePhase::BeginFrame,
          "World Session camera observer is scheduled in BeginFrame before the streaming snapshot");

    frameClock.ticks += 90;
    Check(framePipeline->RunFrame(&frameFailure), "deterministic frame execution");
    vanguard::engine::FramePipelineStats frameStats = framePipeline->GetStats();
    Check(frameTrace.count == 8 && frameTrace.participants[0] == InputParticipant && frameTrace.participants[1] == FixedParticipant && frameTrace.participants[2] == FixedParticipant &&
              frameTrace.participants[3] == SimulationBeforeParticipant && frameTrace.participants[4] == SimulationAfterParticipant && frameTrace.participants[5] == RenderUpdateProducerParticipant &&
              frameTrace.participants[6] == RenderFrameSourceParticipant && frameTrace.participants[7] == FailureParticipant,
          "global phases, rendering extension points, repeated fixed steps, and same-phase dependencies execute deterministically");
    Check(frameStats.frames == 1 && frameStats.lastFixedSteps == 2 && frameStats.fixedSteps == 2 && std::fabs(frameStats.fixedTimeSeconds - 0.04) < 0.0001 &&
              std::fabs(frameStats.lastRawDeltaSeconds - 0.09f) < 0.0001f && std::fabs(frameStats.lastRealDeltaSeconds - 0.09f) < 0.0001f && frameStats.droppedSimulationSeconds > 0.039 &&
              frameStats.droppedSimulationSeconds < 0.041,
          "fixed-step catch-up is bounded and dropped time is reported");
    vanguard::engine::StreamingObserverSnapshot observerSnapshot;
    const vanguard::engine::StreamingObserverServiceStats observerStats = streamingObservers != nullptr ? streamingObservers->GetStats() : vanguard::engine::StreamingObserverServiceStats{};
    Check(streamingObservers != nullptr && streamingObservers->GetSnapshot(observerSnapshot) && observerSnapshot.observerCount == 2 && observerSnapshot.sequence == 1 &&
              !observerSnapshot.usingWorldOriginFallback && std::fabs(observerSnapshot.cameraPosition[0] - 100.0) < 0.0001 && std::fabs(observerSnapshot.cameraPosition[1] - 200.0) < 0.0001 &&
              std::fabs(observerSnapshot.observers[0].predictedPosition[0] - 105.0) < 0.0001 && std::fabs(observerSnapshot.observers[1].predictedPosition[0] - 12.0) < 0.0001 &&
              std::fabs(observerSnapshot.observers[1].predictedPosition[1] - 16.0) < 0.0001 && std::fabs(observerSnapshot.globalDistanceScale - 1.5f) < 0.0001f && observerStats.registeredObservers == 2 &&
              observerStats.validObservers == 2 && observerStats.submittedSnapshots == 0,
          "Streaming Observer snapshot applies velocity-class prediction caps without mutating source positions");
    const vanguard::engine::WorldSessionCameraBinding terminalBinding = worldSessionService->GetCameraBinding();
    Check(terminalBinding.source == vanguard::engine::WorldSessionCameraBindingSource::None &&
              terminalBinding.status == vanguard::engine::WorldSessionCameraBindingStatus::None,
          "P3 terminal World Session owns no camera binding and therefore no streaming observer");
    Check(streamingObservers != nullptr && streamingObservers->UnregisterObserver(cameraObserver) && !streamingObservers->UpdateObserver(cameraObserver, cameraUpdate) &&
              streamingObservers->UnregisterObserver(vehicleObserver) && streamingObservers->GetStats().registeredObservers == 0 && streamingObservers->GetStats().rejectedUpdates == 1,
          "Streaming Observer generational handles reject stale producer updates");
    Check(frameTrace.contexts[1].fixedStep == 0 && frameTrace.contexts[2].fixedStep == 1 && frameTrace.contexts[1].fixedStepCount == 2 &&
              std::fabs(frameTrace.contexts[1].simulationDeltaSeconds - 0.02f) < 0.0001f && std::fabs(frameTrace.contexts[1].fixedTimeSeconds - 0.02) < 0.0001 &&
              std::fabs(frameTrace.contexts[2].fixedTimeSeconds - 0.04) < 0.0001,
          "fixed-step contexts expose stable step identity, delta, and absolute time");

    Check(framePipeline->SetPaused(true), "pause request at frame boundary");
    frameTrace.count = 0;
    frameClock.ticks += 20;
    Check(framePipeline->RunFrame(&frameFailure) && frameTrace.count == 6 && frameTrace.contexts[0].paused && frameTrace.contexts[0].simulationDeltaSeconds == 0.0f &&
              framePipeline->GetStats().lastFixedSteps == 0,
          "paused frames continue non-simulation phases without fixed advancement");
    const vanguard::rendering::RenderCommandSystemStats renderingCommandStats = renderingService->GetCommands().GetStats();
    const vanguard::u64 rendererFrameTicksAfterOuterFrames = renderingStatsBeforeFramePipeline.frameTicks + 2u;
    Check(renderingCommandStats.frameTicks == rendererFrameTicksAfterOuterFrames &&
              renderingCommandStats.completedFrameTicks >= renderingStatsBeforeFramePipeline.completedFrameTicks + 1u &&
              renderingCommandStats.explicitFlushes >= renderingStatsBeforeFramePipeline.explicitFlushes + 1u,
          "RenderingService automatically flushes the previous CPU tail and dispatches one renderer FrameTick per outer frame");
    const vanguard::u64 rendererDrainOuterFramesBefore = framePipeline->GetStats().frames;
    const vanguard::rendering::ViewportManagerStats rendererDrainViewportStatsBefore = renderingService->GetViewports().GetStats();
    const vanguard::u64 rendererDrainFrameTicksBefore = renderingCommandStats.frameTicks;
    vanguard::engine::RenderingProgressFailure renderingProgressFailure;
    const vanguard::engine::RenderingSessionDrainResult steadyRenderingDrain =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    Check(steadyRenderingDrain.status == vanguard::engine::RenderingSessionDrainStatus::Complete &&
              steadyRenderingDrain.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames && !steadyRenderingDrain.frameTickDispatched &&
              renderingService->GetCommands().GetStats().frameTicks == rendererFrameTicksAfterOuterFrames,
          "renderer drain observes the same epoch advanced by ordinary frames without inventing terminal work");

    vanguard::rendering::RenderProxyDesc terminalProxyDesc;
    terminalProxyDesc.scene = coordinatedScene;
    terminalProxyDesc.typeId = 1;
    terminalProxyDesc.producerId = 1;
    terminalProxyDesc.producerGeneration = 1;
    terminalProxyDesc.bounds.minimum[0] = terminalProxyDesc.bounds.minimum[1] = terminalProxyDesc.bounds.minimum[2] = -1.0f;
    terminalProxyDesc.bounds.maximum[0] = terminalProxyDesc.bounds.maximum[1] = terminalProxyDesc.bounds.maximum[2] = 1.0f;
    terminalProxyDesc.debugName = "Terminal drain proxy";
    vanguard::rendering::RenderProxyHandle terminalProxy;
    Check(renderingService->GetScenes().CreateProxy(terminalProxyDesc, terminalProxy, &coordinatedSceneFailure),
          "renderer drain fixture admits a pending scene mutation");
    const vanguard::engine::RenderingSessionDrainResult mutationDrain =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    const vanguard::engine::RenderingSessionDrainResult mutationSettled =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    Check(mutationDrain.status == vanguard::engine::RenderingSessionDrainStatus::Pending && mutationDrain.frameTickDispatched &&
              mutationDrain.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 1u &&
              mutationSettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete && !mutationSettled.frameTickDispatched &&
              mutationSettled.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 1u,
          "renderer drain uses the shared renderer epoch to settle scene mutation without a viewport frame");

    Check(renderingService->GetScenes().DestroyProxy(terminalProxy, &coordinatedSceneFailure),
          "renderer drain fixture transfers proxy retirement to scene publication");
    const vanguard::engine::RenderingSessionDrainResult retirementDrain =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    const vanguard::engine::RenderingSessionDrainResult retirementSettled =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    const vanguard::rendering::RenderCommandSystemStats terminalRenderingStats = renderingService->GetCommands().GetStats();
    Check(retirementDrain.status == vanguard::engine::RenderingSessionDrainStatus::Pending && retirementDrain.frameTickDispatched &&
              retirementDrain.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 2u &&
              retirementSettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete && !retirementSettled.frameTickDispatched &&
              retirementSettled.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 2u &&
              terminalRenderingStats.frameTicks == rendererFrameTicksAfterOuterFrames + 2u &&
              terminalRenderingStats.completedFrameTicks >= rendererFrameTicksAfterOuterFrames + 2u,
          "renderer drain closes accepted proxy retirement through FrameTick and CPU-tail join without Present");
    vanguard::rendering::RenderSceneHandle staleDrainScene = coordinatedScene;
    ++staleDrainScene.generation;
    if (staleDrainScene.generation == 0)
        ++staleDrainScene.generation;
    const vanguard::engine::RenderingSessionDrainResult staleDrain =
        renderingService->ProgressSessionDrain(staleDrainScene, &renderingProgressFailure);
    Check(staleDrain.status == vanguard::engine::RenderingSessionDrainStatus::Failed &&
              renderingProgressFailure.code == vanguard::engine::RenderingProgressFailureCode::InvalidScene &&
              staleDrain.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 2u && !staleDrain.frameTickDispatched &&
              renderingService->GetCommands().GetStats().frameTicks == rendererFrameTicksAfterOuterFrames + 2u,
          "renderer drain reports stale scene ownership as a typed terminal failure without inventing renderer work");
    const vanguard::engine::RenderingSessionDrainResult forcedRetirementEpoch =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure, true);
    const vanguard::engine::RenderingSessionDrainResult forcedRetirementSettled =
        renderingService->ProgressSessionDrain(coordinatedScene, &renderingProgressFailure);
    Check(forcedRetirementEpoch.status == vanguard::engine::RenderingSessionDrainStatus::Pending &&
              forcedRetirementEpoch.frameTickDispatched && forcedRetirementEpoch.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 3u &&
              forcedRetirementSettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete &&
              !forcedRetirementSettled.frameTickDispatched && forcedRetirementSettled.rendererProgressEpoch == rendererFrameTicksAfterOuterFrames + 3u &&
              renderingService->GetCommands().GetStats().frameTicks == rendererFrameTicksAfterOuterFrames + 3u,
          "renderer drain can advance one shared retirement epoch without a viewport frame when an accepted owner delay requires it");
    const vanguard::rendering::ViewportManagerStats rendererDrainViewportStatsAfter = renderingService->GetViewports().GetStats();
    Check(framePipeline->GetStats().frames == rendererDrainOuterFramesBefore &&
              renderingService->GetCommands().GetStats().frameTicks == rendererDrainFrameTicksBefore + 3u &&
              rendererDrainViewportStatsAfter.begunFrames == rendererDrainViewportStatsBefore.begunFrames &&
              rendererDrainViewportStatsAfter.submittedFrames == rendererDrainViewportStatsBefore.submittedFrames &&
              rendererDrainViewportStatsAfter.presentedFrames == rendererDrainViewportStatsBefore.presentedFrames,
          "renderer-only drain advances the renderer-owned epoch without recursively running an application/game frame, viewport frame or Present");
    Check(framePipeline->SetPaused(false) && !framePipeline->SetTimeScale(-1.0f) && framePipeline->SetTimeScale(0.5f), "time controls accept only valid boundary changes");
    frameTrace.count = 0;
    frameClock.ticks += 40;
    Check(framePipeline->RunFrame(&frameFailure) && framePipeline->GetStats().lastFixedSteps == 1 && std::fabs(framePipeline->GetStats().lastSimulationDeltaSeconds - 0.02f) < 0.0001f,
          "scaled time feeds both variable and fixed simulation domains");

    failureFixture.fail = true;
    frameTrace.count = 0;
    frameClock.ticks += 20;
    Check(!framePipeline->RunFrame(&frameFailure) && frameFailure.code == vanguard::engine::FrameFailureCode::ParticipantFailure && frameFailure.phase == vanguard::engine::FramePhase::EndFrame &&
              frameFailure.participant == FailureParticipant && framePipeline->GetState() == vanguard::engine::FramePipelineState::Failed,
          "participant failures preserve phase and participant identity");
    Check(vanguard::jobs::IsInitialized() && vanguard::jobs::GetWorkerCount() != 0, "Jobs scheduler initialization");
    Check(vanguard::io::IsInitialized(), "I/O initialization");
    Check(vanguard::filesystem::IsInitialized(), "Filesystem initialization");
    const vanguard::filesystem::AbsolutePath root = vanguard::filesystem::paths::GetRootDirectory();
    Check(vanguard::filesystem::GetManager().GetEngineRoot() == root && vanguard::filesystem::GetManager().GetGameRoot() == root.AddDirPath("data") &&
              vanguard::filesystem::GetManager().GetCacheDirectory() == vanguard::filesystem::paths::GetUserCacheDirectory(),
          "Filesystem launch roots");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::IoServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId, vanguard::application::LifecycleStage::Initialize),
          "I/O initializes before Filesystem");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId, vanguard::application::LifecycleStage::Initialize),
          "Filesystem initializes before Jobs");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FramePipelineServiceId, vanguard::application::LifecycleStage::Initialize),
          "Jobs initializes before Frame Pipeline");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FramePipelineServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Initialize),
          "Frame Pipeline initializes before Game World registration");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId, vanguard::application::LifecycleStage::Initialize),
          "Jobs initializes before Resources");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId, vanguard::application::LifecycleStage::Initialize),
          "Resources initializes before Resource Streaming");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId, vanguard::application::LifecycleStage::Initialize),
          "Resource Streaming initializes before World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Initialize),
          "World initializes before Game World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::RenderingServiceId, vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Initialize),
          "P2 Rendering service initializes before Game World can bind renderer owners");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Initialize) <
                  FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId, vanguard::application::LifecycleStage::Initialize) &&
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId, vanguard::application::LifecycleStage::Initialize) <
                  FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldSessionServiceId, vanguard::application::LifecycleStage::Initialize),
          "Game World and Streaming Observer initialize before World Session");
    vanguard::rendering::RenderCommandFailure renderingDrainFailure;
    Check(renderingService != nullptr && renderingService->GetCommands().FlushPreviousFrameProcessing(&renderingDrainFailure), "final engine frame rendering tail drain");
    const vanguard::rendering::RenderSceneManagerStats coordinatedSceneStats = renderingService->GetScenes().GetStats();
    const vanguard::containers::ArraySpan<const vanguard::rendering::RenderSceneHandle> coordinatedScenes = renderingService->GetScenes().GetFramePipelineScenes();
    Check(coordinatedSceneStats.preparedFrames == coordinatedSceneStatsBeforeFramePipeline.preparedFrames + 7u &&
              coordinatedSceneStats.framePipelineScenes == 1 && coordinatedScenes.Count() == 1 && coordinatedScenes[0] == coordinatedScene,
          "RenderingService feeds ordinary and terminal FrameTicks through the same RenderScene preparation path");

    vanguard::rendering::RenderSceneDesc releaseOnlySceneDesc{"release-only.external"};
    releaseOnlySceneDesc.ownership = vanguard::rendering::RenderSceneOwnership::External;
    releaseOnlySceneDesc.maximumProxies = 8;
    releaseOnlySceneDesc.maximumPendingProxyMutations = 8;
    releaseOnlySceneDesc.maximumViews = 1;
    vanguard::rendering::RenderSceneHandle releaseOnlyScene;
    vanguard::rendering::RenderSceneFailure releaseOnlySceneFailure;
    vanguard::entities::RenderingReleaseFailure releaseOnlyReleaseFailure;
    Check(renderingService->GetScenes().CreateScene(releaseOnlySceneDesc, releaseOnlyScene, &releaseOnlySceneFailure),
          "external scene owner creates a release-only retirement fixture");

    const vanguard::resources::ResourceReference distantProxyMesh(
        vanguard::resources::ResourcePath::FromString("world/p2/release_only_proxy.vmesh"), vanguard::meshes::MeshResourceType);
    vanguard::world::WorldCellBuildRecord releaseOnlyOwnerCell;
    releaseOnlyOwnerCell.cellId = 7'000u;
    releaseOnlyOwnerCell.cell = vanguard::resources::ResourceReference(
        vanguard::resources::ResourcePath::FromString("world/p2/release_only_owner.vcell"), vanguard::world::CellResourceType);
    for (vanguard::u32 axis = 0; axis < 3; ++axis)
    {
        releaseOnlyOwnerCell.bounds.minimum[axis] = -100.0;
        releaseOnlyOwnerCell.bounds.maximum[axis] = 100.0;
        // The authored owner exists solely to satisfy the durable proxy ownership
        // relation. Keep its streaming point outside this distant-proxy-only fixture
        // so the executor never needs a cell loader.
        releaseOnlyOwnerCell.streamingReferencePoint[axis] = 10'000.0;
    }
    std::array<vanguard::world::DistantProxyBuildRecord, 3> releaseOnlyDistantProxies{};
    for (vanguard::u32 index = 0; index < releaseOnlyDistantProxies.size(); ++index)
    {
        auto& proxy = releaseOnlyDistantProxies[index];
        proxy.proxyId = 7'001u + index;
        proxy.ownerCellId = releaseOnlyOwnerCell.cellId;
        proxy.mesh = distantProxyMesh;
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            proxy.bounds.minimum[axis] = -25.0;
            proxy.bounds.maximum[axis] = 25.0;
        }
        proxy.streamingDistance = 2'000.0f;
        proxy.secondaryReferencePointDistance = 1'500.0f;
        proxy.nearHideDistance = 250.0f;
        proxy.streamingPriority = vanguard::world::StreamingPriority::Critical;
        proxy.flags = vanguard::world::DistantProxyFlags::ProxyOnly;
    }
    vanguard::world::WorldBuildDescription releaseOnlyStreamingDescription;
    releaseOnlyStreamingDescription.worldId = 0x703272656c656173ull;
    for (vanguard::u32 axis = 0; axis < 3; ++axis)
    {
        releaseOnlyStreamingDescription.bounds.minimum[axis] = -100.0;
        releaseOnlyStreamingDescription.bounds.maximum[axis] = 100.0;
    }
    releaseOnlyStreamingDescription.cells = {&releaseOnlyOwnerCell, 1};
    releaseOnlyStreamingDescription.distantProxies = {releaseOnlyDistantProxies.data(),
                                                       static_cast<vanguard::u32>(releaseOnlyDistantProxies.size())};
    vanguard::containers::DynamicArray<vanguard::u8> releaseOnlyStreamingBytes(vanguard::memory::pools::Serialization::GetInstance());
    vanguard::filesystem::MemoryFileWriter releaseOnlyStreamingWriter(releaseOnlyStreamingBytes);
    Check(vanguard::world::CookWorld(releaseOnlyStreamingDescription, releaseOnlyStreamingWriter) == vanguard::world::Result::Success,
          "cook release-only distant proxy ownership fixture");
    vanguard::filesystem::MemoryFileReader releaseOnlyStreamingReader(releaseOnlyStreamingBytes, 0);
    vanguard::world::WorldFile releaseOnlyStreamingWorld;
    Check(releaseOnlyStreamingWorld.Open(releaseOnlyStreamingReader) == vanguard::world::Result::Success,
          "open release-only distant proxy ownership fixture");

    vanguard::resources::ResourceRegistry releaseOnlyStreamingRegistry;
    vanguard::resources::ResourcePipeline releaseOnlyStreamingPipeline;
    const vanguard::resources::AsyncLoaderDescriptor releaseOnlyMeshLoader{
        vanguard::meshes::MeshResourceType, "release-only distant proxy mesh", &DiscoverDistantProxyTestMesh,
        &ConstructDistantProxyTestMesh, &DestroyDistantProxyTestMesh, nullptr};
    Check(releaseOnlyStreamingRegistry.Initialize() &&
              releaseOnlyStreamingPipeline.Initialize(releaseOnlyStreamingRegistry) &&
              releaseOnlyStreamingPipeline.RegisterLoader(releaseOnlyMeshLoader),
          "initialize release-only distant proxy resource pipeline");
    vanguard::world::WorldStreamingGrid releaseOnlyStreamingGrid;
    vanguard::world::WorldStreamingExecutor releaseOnlyStreamingExecutor;
    Check(releaseOnlyStreamingGrid.Initialize(releaseOnlyStreamingWorld) &&
              releaseOnlyStreamingExecutor.Initialize(releaseOnlyStreamingGrid, releaseOnlyStreamingPipeline),
          "initialize release-only distant proxy selector and executor");

    vanguard::entities::ComponentDirectoryConfig releaseOnlyComponentConfig;
    releaseOnlyComponentConfig.maximumComponents = 16;
    vanguard::entities::ComponentRuntime releaseOnlyComponents(releaseOnlyComponentConfig);
    vanguard::entities::TransformRuntimeConfig releaseOnlyTransformConfig;
    releaseOnlyTransformConfig.maximumTrackedEntities = 16;
    releaseOnlyTransformConfig.maximumPlacedComponents = 16;
    releaseOnlyTransformConfig.maximumChangesPerSynchronize = 16;
    releaseOnlyTransformConfig.transformSystem.maximumComponents = 32;
    releaseOnlyTransformConfig.transformSystem.maximumAttachments = 31;
    releaseOnlyTransformConfig.transformSystem.maximumScheduledRoots = 16;
    vanguard::entities::TransformRuntime releaseOnlyTransforms(releaseOnlyTransformConfig);
    vanguard::entities::RenderingRuntimeConfig releaseOnlyRenderingConfig;
    releaseOnlyRenderingConfig.existingScene = releaseOnlyScene;
    releaseOnlyRenderingConfig.maximumPendingProxyAdmissions = 8;
    releaseOnlyRenderingConfig.maximumProxyAdmissionsPerFrame = 8;
    releaseOnlyRenderingConfig.maximumPendingProxyRetirements = 8;
    releaseOnlyRenderingConfig.proxyRetirementDelayFrames = 2;
    releaseOnlyRenderingConfig.maximumPendingMeshPreparations = 8;
    releaseOnlyRenderingConfig.maximumMeshPreparationsPerFrame = 8;
    releaseOnlyRenderingConfig.maximumPendingCameraTransforms = 8;
    vanguard::entities::RenderingRuntime releaseOnlyRendering(renderingService->GetScenes(), releaseOnlyRenderingConfig);
    Check(releaseOnlyRendering.BindDistantProxyStreaming(releaseOnlyStreamingWorld, releaseOnlyStreamingExecutor),
          "bind release-only distant proxy ownership to its real executor before runtime initialization");
    vanguard::game::GameWorld releaseOnlyWorld;
    vanguard::game::GameWorldConfig releaseOnlyWorldConfig;
    releaseOnlyWorldConfig.entities.initialEntityCapacity = 16;
    releaseOnlyWorldConfig.entities.initialActionCapacity = 16;
    releaseOnlyWorldConfig.entities.committedChangeCapacity = 32;
    Check(releaseOnlyWorld.RegisterSystem(releaseOnlyComponents) && releaseOnlyWorld.RegisterSystem(releaseOnlyTransforms) &&
              releaseOnlyWorld.RegisterSystem(releaseOnlyRendering) && releaseOnlyWorld.Initialize(releaseOnlyWorldConfig),
          "initialize an external-scene runtime without executing a gameplay frame");

    vanguard::world::StreamingObserver releaseOnlyDistantObserver;
    releaseOnlyDistantObserver.predictedPosition[0] = 500.0;
    vanguard::world::StreamingProcessInput releaseOnlyDistantInput;
    releaseOnlyDistantInput.observers = {&releaseOnlyDistantObserver, 1};
    releaseOnlyDistantInput.cameraPosition[0] = 500.0;
    vanguard::containers::DynamicArray<vanguard::world::StreamingResourceEvent> releaseOnlyDistantEvents(
        vanguard::memory::pools::World::GetInstance());
    std::array<vanguard::world::StreamingResourceEvent, 3> releaseOnlyAvailableEvents{};
    std::array<bool, 3> releaseOnlyAvailable{};
    bool releaseOnlyDistantProcessOk = releaseOnlyStreamingExecutor.Process(releaseOnlyDistantInput, releaseOnlyDistantEvents);
    for (vanguard::u32 attempt = 0; attempt < 500 && releaseOnlyDistantProcessOk &&
                                     (!releaseOnlyAvailable[0] || !releaseOnlyAvailable[1] || !releaseOnlyAvailable[2]);
         ++attempt)
    {
        for (const auto& event : releaseOnlyDistantEvents)
        {
            if (event.type != vanguard::world::StreamingResourceEventType::ResourceAvailable ||
                event.key.kind != vanguard::world::StreamingNodeKind::DistantProxy ||
                event.key.id < 7'001u || event.key.id > 7'003u)
                continue;
            const vanguard::u32 index = static_cast<vanguard::u32>(event.key.id - 7'001u);
            releaseOnlyAvailableEvents[index] = event;
            releaseOnlyAvailable[index] = true;
        }
        if (releaseOnlyAvailable[0] && releaseOnlyAvailable[1] && releaseOnlyAvailable[2])
            break;
        vanguard::concurrency::SleepOnCurrentThread(1);
        releaseOnlyDistantProcessOk = releaseOnlyStreamingExecutor.Process(releaseOnlyDistantInput, releaseOnlyDistantEvents);
    }
    Check(releaseOnlyDistantProcessOk && releaseOnlyAvailable[0] && releaseOnlyAvailable[1] && releaseOnlyAvailable[2],
          "retain three executor-owned distant proxy availability events for distinct release-only races");

    VisualReleaseProbe busyVisual;
    vanguard::rendering::RenderProxyDesc busyVisualDesc;
    busyVisualDesc.scene = releaseOnlyScene;
    busyVisualDesc.typeId = 0x42555359u;
    busyVisualDesc.producerId = 0x42555359u;
    busyVisualDesc.producerGeneration = 1u;
    busyVisualDesc.debugName = "busy-release-probe";
    busyVisualDesc.spatialMode = vanguard::rendering::RenderProxySpatialMode::Bounds;
    for (vanguard::u32 axis = 0; axis < 3; ++axis)
    {
        busyVisualDesc.bounds.minimum[axis] = -1.0f;
        busyVisualDesc.bounds.maximum[axis] = 1.0f;
    }
    vanguard::entities::DistantProxyStreamingFailure distantStreamingFailure;
    Check(busyVisual.Admit(releaseOnlyRendering, busyVisualDesc) &&
              releaseOnlyRendering.HandleStreamingEvent(releaseOnlyAvailableEvents[2], &distantStreamingFailure) &&
              releaseOnlyWorld.Tick(0.0f) && busyVisual.IsProxyBound() &&
              releaseOnlyRendering.GetStats().residentDistantProxies == 1u,
          "admit visual and distant proxy ownership before release cutover");

    const vanguard::rendering::RenderProxyHandle busyVisualProxy = busyVisual.GetProxyBinding().proxy;
    vanguard::rendering::RenderSceneFramePrepareResult busyPreparedScene;
    vanguard::rendering::RenderSceneFailure busyVisualFailure;
    Check(renderingService->GetScenes().PrepareSceneUpdate(releaseOnlyScene, 0x42555359u, busyPreparedScene, &busyVisualFailure) &&
              !busyVisual.Release(&busyVisualFailure) && busyVisualFailure.code == vanguard::rendering::RenderSceneFailureCode::Busy &&
              busyVisual.IsProxyBound() && busyVisual.GetProxyBinding().proxy == busyVisualProxy,
          "visual retirement Busy restores the exact binding while logical ownership remains attached");
    {
        vanguard::jobs::Builder busyReleaseBuilder(
            {vanguard::jobs::Priority::RenderPath, vanguard::jobs::Affinity::AnyWorker}, nullptr);
        vanguard::rendering::RenderSceneUpdateResult busyReleaseUpdate;
        const bool busyReleaseUpdateCompleted =
            busyReleaseBuilder.IsValid() &&
            renderingService->GetScenes().ExecuteSceneUpdate(releaseOnlyScene, busyReleaseBuilder, busyReleaseUpdate, &busyVisualFailure) &&
            (!busyReleaseUpdate.dispatched || busyReleaseBuilder.WaitForCompletion());
        Check(busyReleaseUpdateCompleted && busyVisual.Release(&busyVisualFailure) && !busyVisual.IsProxyBound() &&
                  releaseOnlyWorld.Tick(0.0f) && releaseOnlyWorld.Tick(0.0f),
              "visual retirement retries after Busy without replaying ownership transfer or requiring stop-time transform scheduling");
    }
    const auto busyVisualRetirementDrain = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    const auto busyVisualRetirementSettled = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    Check(busyVisualRetirementDrain.status != vanguard::engine::RenderingSessionDrainStatus::Failed &&
              busyVisualRetirementSettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete &&
              releaseOnlyRendering.GetStats().pendingProxyRetirements == 0u,
          "settle the pre-cutover visual Busy retry fixture through the ordinary renderer owner");

    vanguard::rendering::RenderProxyDesc delayedProxyDesc;
    delayedProxyDesc.scene = releaseOnlyScene;
    delayedProxyDesc.typeId = 2;
    delayedProxyDesc.producerId = 2;
    delayedProxyDesc.producerGeneration = 1;
    delayedProxyDesc.bounds.minimum[0] = delayedProxyDesc.bounds.minimum[1] = delayedProxyDesc.bounds.minimum[2] = -1.0f;
    delayedProxyDesc.bounds.maximum[0] = delayedProxyDesc.bounds.maximum[1] = delayedProxyDesc.bounds.maximum[2] = 1.0f;
    delayedProxyDesc.debugName = "Release-only delayed proxy";
    vanguard::rendering::RenderProxyHandle delayedProxy;
    Check(renderingService->GetScenes().CreateProxy(delayedProxyDesc, delayedProxy, &releaseOnlySceneFailure),
          "admit a proxy before release-only cutover");
    const auto releaseOnlyAdmissionDrain = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    const auto releaseOnlyAdmissionSettled = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    Check(releaseOnlyAdmissionDrain.status == vanguard::engine::RenderingSessionDrainStatus::Pending &&
              releaseOnlyAdmissionSettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete,
          "settle the pre-cutover proxy publication before terminal release begins");

    Check(releaseOnlyRendering.HandleStreamingEvent(releaseOnlyAvailableEvents[1], &distantStreamingFailure) &&
              releaseOnlyRendering.GetStats().pendingProxyAdmissions == 1u,
          "leave one distant proxy admission pending at release cutover");

    Check(releaseOnlyRendering.BeginReleaseOnly(&releaseOnlyReleaseFailure) &&
              releaseOnlyRendering.GetStats().pendingProxyAdmissions == 0u &&
              releaseOnlyRendering.HandleStreamingEvent(releaseOnlyAvailableEvents[0], &distantStreamingFailure) &&
              releaseOnlyRendering.GetStats().pendingProxyAdmissions == 0u,
          "release cutover cancels the owned pending admission and ignores later availability without admitting new work");
    constexpr const char* TerminalComponentLifecycleFailure = "terminal component lifecycle failure probe";
    vanguard::rendering::RenderSceneFailure terminalComponentLifecycleFailure;
    releaseOnlyRendering.ReportComponentFailure(TerminalComponentLifecycleFailure);
    Check(releaseOnlyRendering.ConsumeProxyLifecycleFailure(terminalComponentLifecycleFailure) &&
              terminalComponentLifecycleFailure.code == vanguard::rendering::RenderSceneFailureCode::InvalidState &&
              terminalComponentLifecycleFailure.scene == releaseOnlyScene && terminalComponentLifecycleFailure.message != nullptr &&
              std::strcmp(terminalComponentLifecycleFailure.message, TerminalComponentLifecycleFailure) == 0,
          "release-only component lifecycle failure remains available with exact scene detail for the terminal Game World consumer");
    Check(releaseOnlyStreamingGrid.RequestShutdown(), "request distant proxy grid shutdown after renderer release cutover");

    std::array<bool, 3> releaseOnlyRequested{};
    for (vanguard::u32 attempt = 0; attempt < 32 && releaseOnlyDistantProcessOk &&
                                     (!releaseOnlyRequested[0] || !releaseOnlyRequested[1] || !releaseOnlyRequested[2]);
         ++attempt)
    {
        releaseOnlyDistantProcessOk = releaseOnlyStreamingExecutor.Process(releaseOnlyDistantInput, releaseOnlyDistantEvents);
        for (const auto& event : releaseOnlyDistantEvents)
        {
            if (event.type != vanguard::world::StreamingResourceEventType::ReleaseRequested ||
                event.key.kind != vanguard::world::StreamingNodeKind::DistantProxy ||
                event.key.id < 7'001u || event.key.id > 7'003u)
                continue;
            releaseOnlyRequested[static_cast<vanguard::u32>(event.key.id - 7'001u)] = true;
            releaseOnlyDistantProcessOk =
                releaseOnlyRendering.HandleStreamingEvent(event, &distantStreamingFailure) && releaseOnlyDistantProcessOk;
        }
    }
    const vanguard::world::StreamingExecutorStats distantBeforeTransfer = releaseOnlyStreamingExecutor.GetStats();
    Check(releaseOnlyDistantProcessOk && releaseOnlyRequested[0] && releaseOnlyRequested[1] && releaseOnlyRequested[2] &&
              distantBeforeTransfer.releasePendingNodes == 1u &&
              releaseOnlyRendering.GetStats().pendingDistantProxies == 1u &&
              releaseOnlyRendering.GetStats().pendingProxyAdmissions == 0u,
          "inactive shutdown races acknowledge directly while resident distant ownership remains release-pending");

    Check(releaseOnlyRendering.ProgressReleaseOnly(0, false, &releaseOnlyReleaseFailure) ==
                  vanguard::entities::RenderingReleaseStatus::Pending &&
              releaseOnlyStreamingExecutor.GetStats().releasePendingNodes == 0u &&
              releaseOnlyRendering.GetStats().pendingDistantProxies == 0u &&
              releaseOnlyRendering.GetStats().pendingProxyRetirements == 1u,
          "resident distant proxy acknowledges executor release only after renderer retirement accepts ownership");

    for (vanguard::u32 epoch = 0; epoch < releaseOnlyRenderingConfig.proxyRetirementDelayFrames; ++epoch)
    {
        const auto distantEpoch = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure,
                                                                         releaseOnlyRendering.RequiresReleaseFrameTick());
        Check(distantEpoch.frameTickDispatched &&
                  releaseOnlyRendering.ProgressReleaseOnly(distantEpoch.rendererProgressEpoch, distantEpoch.frameTickDispatched,
                                                           &releaseOnlyReleaseFailure) == vanguard::entities::RenderingReleaseStatus::Pending,
              "distant proxy physical retirement delay advances only on the shared renderer epoch");
    }
    const auto distantDestroySettled = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    Check(distantDestroySettled.status != vanguard::engine::RenderingSessionDrainStatus::Failed &&
              releaseOnlyRendering.ProgressReleaseOnly(distantDestroySettled.rendererProgressEpoch, false, &releaseOnlyReleaseFailure) ==
                  vanguard::entities::RenderingReleaseStatus::Complete &&
              releaseOnlyStreamingExecutor.GetStats().releasedResources == 3u,
          "distant proxy executor ownership is empty while physical renderer retirement settles independently");

    const auto preRetirementEpoch = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure, true);
    Check(preRetirementEpoch.frameTickDispatched &&
              releaseOnlyRendering.ProgressReleaseOnly(preRetirementEpoch.rendererProgressEpoch, preRetirementEpoch.frameTickDispatched,
                                                       &releaseOnlyReleaseFailure) == vanguard::entities::RenderingReleaseStatus::Complete &&
              releaseOnlyRendering.RetireProxy(delayedProxy, &releaseOnlySceneFailure) &&
              releaseOnlyRendering.RequiresReleaseFrameTick(),
          "retirement accepted after a renderer epoch does not count that earlier epoch toward its delay");
    const vanguard::u64 cpuTailFrameTicksBeforeJoin = renderingService->GetCommands().GetStats().frameTicks;
    vanguard::rendering::RenderCommandFailure cpuTailFailure;
    const bool cpuTailJoined = renderingService->GetCommands().FlushPreviousFrameProcessing(&cpuTailFailure);
    const vanguard::entities::RenderingReleaseStatus cpuTailOnlyProgress =
        releaseOnlyRendering.ProgressReleaseOnly(preRetirementEpoch.rendererProgressEpoch, false, &releaseOnlyReleaseFailure);
    Check(cpuTailJoined && renderingService->GetCommands().GetStats().frameTicks == cpuTailFrameTicksBeforeJoin &&
              cpuTailOnlyProgress == vanguard::entities::RenderingReleaseStatus::Pending &&
              releaseOnlyRendering.RequiresReleaseFrameTick(),
          "CPU-tail join alone does not advance renderer policy epochs or masquerade as GPU/retirement completion");

    const auto delayedEpochOne = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure,
                                                                        releaseOnlyRendering.RequiresReleaseFrameTick());
    const auto delayedProgressOne =
        releaseOnlyRendering.ProgressReleaseOnly(delayedEpochOne.rendererProgressEpoch, delayedEpochOne.frameTickDispatched, &releaseOnlyReleaseFailure);
    const auto delayedEpochTwo = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure,
                                                                        releaseOnlyRendering.RequiresReleaseFrameTick());
    const auto delayedProgressTwo =
        releaseOnlyRendering.ProgressReleaseOnly(delayedEpochTwo.rendererProgressEpoch, delayedEpochTwo.frameTickDispatched, &releaseOnlyReleaseFailure);
    Check(delayedEpochOne.frameTickDispatched && delayedEpochTwo.frameTickDispatched &&
              delayedEpochTwo.rendererProgressEpoch == delayedEpochOne.rendererProgressEpoch + 1u &&
              delayedProgressOne == vanguard::entities::RenderingReleaseStatus::Pending &&
              delayedProgressTwo == vanguard::entities::RenderingReleaseStatus::Pending &&
              !releaseOnlyRendering.RequiresReleaseFrameTick(),
          "deferred proxy retirement counts only real renderer progress epochs and reports its staged destruction as pending");
    const auto delayedDestroyDrain = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    const auto delayedDestroySettled = renderingService->ProgressSessionDrain(releaseOnlyScene, &renderingProgressFailure);
    Check(delayedDestroyDrain.status == vanguard::engine::RenderingSessionDrainStatus::Pending &&
              delayedDestroySettled.status == vanguard::engine::RenderingSessionDrainStatus::Complete &&
              releaseOnlyRendering.ProgressReleaseOnly(delayedDestroySettled.rendererProgressEpoch, false, &releaseOnlyReleaseFailure) ==
                  vanguard::entities::RenderingReleaseStatus::Complete &&
              releaseOnlyRendering.ReleaseScene(&releaseOnlySceneFailure),
          "renderer publication settles after delayed physical destruction before the runtime detaches its scene");
    vanguard::rendering::RenderSceneSnapshot externalReleaseSnapshot;
    Check(renderingService->GetScenes().GetSnapshot(releaseOnlyScene, externalReleaseSnapshot) && externalReleaseSnapshot.activeProxies == 0 &&
              releaseOnlyWorld.Shutdown() && renderingService->GetScenes().GetSnapshot(releaseOnlyScene, externalReleaseSnapshot) &&
              renderingService->GetScenes().DestroyScene(releaseOnlyScene, &releaseOnlySceneFailure),
          "release-only runtime leaves an adopted external scene alive and empty for its real owner");
    Check(releaseOnlyStreamingExecutor.Shutdown() && releaseOnlyStreamingGrid.Shutdown() &&
              releaseOnlyStreamingPipeline.Shutdown() && releaseOnlyStreamingRegistry.Shutdown(),
          "shutdown fully released distant proxy resource ownership");
    releaseOnlyStreamingWorld.Close();

    // P4 device-abandonment CPU oracle: after the renderer owner has already
    // terminalized GPU publication, a world-scoped runtime must not wait for its
    // configured retirement-delay epochs. A standalone scene manager represents the
    // post-abandon state (no GPU publisher attached) without injecting product test
    // mode or fabricating an RHI completion.
    vanguard::rendering::RenderSceneManager abandonedSceneManager;
    vanguard::rendering::RenderSceneFailure abandonedSceneFailure;
    Check(abandonedSceneManager.Initialize({}, &abandonedSceneFailure),
          "P4 device-abandonment CPU fixture initializes a publisher-free RenderSceneManager");
    vanguard::entities::ComponentDirectoryConfig abandonedComponentConfig;
    abandonedComponentConfig.maximumComponents = 8;
    vanguard::entities::ComponentRuntime abandonedComponents(abandonedComponentConfig);
    vanguard::entities::TransformRuntimeConfig abandonedTransformConfig;
    abandonedTransformConfig.maximumTrackedEntities = 8;
    abandonedTransformConfig.maximumPlacedComponents = 8;
    abandonedTransformConfig.maximumChangesPerSynchronize = 8;
    abandonedTransformConfig.transformSystem.maximumComponents = 16;
    abandonedTransformConfig.transformSystem.maximumAttachments = 15;
    abandonedTransformConfig.transformSystem.maximumScheduledRoots = 8;
    vanguard::entities::TransformRuntime abandonedTransforms(abandonedTransformConfig);
    vanguard::entities::RenderingRuntimeConfig abandonedRenderingConfig;
    abandonedRenderingConfig.scene.maximumProxies = 8;
    abandonedRenderingConfig.scene.maximumPendingProxyMutations = 8;
    abandonedRenderingConfig.scene.maximumViews = 2;
    abandonedRenderingConfig.maximumPendingProxyAdmissions = 8;
    abandonedRenderingConfig.maximumProxyAdmissionsPerFrame = 8;
    abandonedRenderingConfig.maximumPendingProxyRetirements = 8;
    abandonedRenderingConfig.proxyRetirementDelayFrames = 128;
    abandonedRenderingConfig.maximumPendingMeshPreparations = 8;
    abandonedRenderingConfig.maximumMeshPreparationsPerFrame = 8;
    abandonedRenderingConfig.maximumPendingCameraTransforms = 8;
    vanguard::entities::RenderingRuntime abandonedRendering(abandonedSceneManager, abandonedRenderingConfig);
    vanguard::game::GameWorld abandonedWorld;
    vanguard::game::GameWorldConfig abandonedWorldConfig;
    abandonedWorldConfig.entities.initialEntityCapacity = 8;
    abandonedWorldConfig.entities.initialActionCapacity = 8;
    abandonedWorldConfig.entities.committedChangeCapacity = 16;
    Check(abandonedWorld.RegisterSystem(abandonedComponents) && abandonedWorld.RegisterSystem(abandonedTransforms) &&
              abandonedWorld.RegisterSystem(abandonedRendering) && abandonedWorld.Initialize(abandonedWorldConfig),
          "P4 device-abandonment CPU fixture initializes world-scoped renderer ownership");
    const vanguard::rendering::RenderSceneHandle abandonedScene = abandonedRendering.GetScene();
    vanguard::rendering::RenderProxyDesc abandonedProxyDesc;
    abandonedProxyDesc.scene = abandonedScene;
    abandonedProxyDesc.typeId = 0x50344142u;
    abandonedProxyDesc.producerId = 0x50344142u;
    abandonedProxyDesc.producerGeneration = 1;
    abandonedProxyDesc.bounds.minimum[0] = abandonedProxyDesc.bounds.minimum[1] = abandonedProxyDesc.bounds.minimum[2] = -1.0f;
    abandonedProxyDesc.bounds.maximum[0] = abandonedProxyDesc.bounds.maximum[1] = abandonedProxyDesc.bounds.maximum[2] = 1.0f;
    abandonedProxyDesc.debugName = "P4 device-abandoned retirement";
    vanguard::rendering::RenderProxyHandle abandonedProxy;
    vanguard::entities::RenderingReleaseFailure abandonedReleaseFailure;
    Check(abandonedSceneManager.CreateProxy(abandonedProxyDesc, abandonedProxy, &abandonedSceneFailure) &&
              abandonedRendering.BeginReleaseOnly(&abandonedReleaseFailure) &&
              abandonedRendering.RetireProxy(abandonedProxy, &abandonedSceneFailure) &&
              abandonedRendering.RequiresReleaseFrameTick(),
          "P4 device-abandonment fixture owns a delayed retirement before terminal cutover");

    const vanguard::entities::RenderingReleaseStatus preAbandonRelease =
        abandonedRendering.ProgressReleaseOnly(0, false, &abandonedReleaseFailure);
    vanguard::engine::RenderingProgressFailure acceptedWorkRendererFailure;
    acceptedWorkRendererFailure.code = vanguard::engine::RenderingProgressFailureCode::GpuSceneFailure;
    acceptedWorkRendererFailure.message = "P4 accepted-work renderer abandonment";
    acceptedWorkRendererFailure.ownersAbandoned = true;
    acceptedWorkRendererFailure.gpuSceneFailure.code = vanguard::rendering::GpuSceneRuntimeFailureCode::ContributionFailure;
    acceptedWorkRendererFailure.gpuSceneFailure.contributionFailure = "P4 accepted-work first cause";
    bool acceptedWorkOwnersAbandoned = false;
    vanguard::engine::GameWorldStopFailure acceptedWorkFirstFailure;
    vanguard::engine::detail::ApplyRendererProgressFailure(acceptedWorkRendererFailure,
                                                           "renderer progression failed while draining accepted component work",
                                                           acceptedWorkOwnersAbandoned, acceptedWorkFirstFailure);
    vanguard::engine::RenderingProgressFailure laterAcceptedWorkFailure;
    laterAcceptedWorkFailure.code = vanguard::engine::RenderingProgressFailureCode::CommandFailure;
    laterAcceptedWorkFailure.message = "P4 later cleanup wrapper failure";
    vanguard::engine::detail::ApplyRendererProgressFailure(laterAcceptedWorkFailure, "later renderer cleanup failure",
                                                           acceptedWorkOwnersAbandoned, acceptedWorkFirstFailure);
    const vanguard::entities::RenderingReleaseStatus abandonedRelease =
        abandonedRendering.AbandonDeviceReleaseOnly(&abandonedReleaseFailure);
    const vanguard::entities::RenderingRuntimeStats abandonedStats = abandonedRendering.GetStats();
    Check(preAbandonRelease == vanguard::entities::RenderingReleaseStatus::Pending && acceptedWorkOwnersAbandoned &&
              acceptedWorkFirstFailure.code == vanguard::engine::GameWorldStopFailureCode::RendererProgress &&
              acceptedWorkFirstFailure.renderingFailure.code == vanguard::engine::RenderingProgressFailureCode::GpuSceneFailure &&
              acceptedWorkFirstFailure.renderingFailure.ownersAbandoned &&
              acceptedWorkFirstFailure.renderingFailure.gpuSceneFailure.contributionFailure != nullptr &&
              std::strcmp(acceptedWorkFirstFailure.renderingFailure.gpuSceneFailure.contributionFailure, "P4 accepted-work first cause") == 0 &&
              abandonedRelease == vanguard::entities::RenderingReleaseStatus::Complete &&
              abandonedStats.pendingProxyRetirements == 0 && abandonedStats.activeProxies == 0 &&
              !abandonedRendering.RequiresReleaseFrameTick() && abandonedRendering.ReleaseScene(&abandonedSceneFailure) &&
              abandonedWorld.Shutdown() && abandonedSceneManager.Shutdown(&abandonedSceneFailure),
          "P4 accepted-work renderer abandonment preserves the first typed cause while CPU-only retry terminalizes proxy/scene ownership without a fake epoch");

    Check(renderingService->GetScenes().DestroyScene(coordinatedScene, &coordinatedSceneFailure), "engine frame coordinated RenderScene retirement after the CPU tail drain");
    const bool hostShutdown = host.Shutdown(&failure);
    if (!hostShutdown)
        std::fprintf(stderr, "[engineServicesTests] shutdown failure: code=%u service=%llu message=%s\n", static_cast<vanguard::u32>(failure.code),
                     static_cast<unsigned long long>(failure.service), failure.message != nullptr ? failure.message : "unspecified");
    Check(hostShutdown, "engine graph shutdown");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId, vanguard::application::LifecycleStage::Shutdown),
          "Resource Streaming shuts down before Resources");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId, vanguard::application::LifecycleStage::Shutdown),
          "World shuts down before Resource Streaming");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId, vanguard::application::LifecycleStage::Shutdown),
          "Game World shuts down before World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::RenderingServiceId, vanguard::application::LifecycleStage::Shutdown),
          "P2 Game World releases world rendering state before Rendering service shutdown");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldSessionServiceId, vanguard::application::LifecycleStage::Shutdown) <
                  FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId, vanguard::application::LifecycleStage::Shutdown) &&
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId, vanguard::application::LifecycleStage::Shutdown) <
                  FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId, vanguard::application::LifecycleStage::Shutdown),
          "World Session and Streaming Observer shut down before Game World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId, vanguard::application::LifecycleStage::Shutdown),
          "Resources shuts down before Jobs");
    Check(!vanguard::jobs::IsInitialized(), "Jobs scheduler shutdown");
    Check(!vanguard::filesystem::IsInitialized(), "Filesystem shutdown");
    Check(!vanguard::io::IsInitialized(), "I/O shutdown");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId, vanguard::application::LifecycleStage::Shutdown),
          "Jobs shuts down before Filesystem");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId, vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::IoServiceId, vanguard::application::LifecycleStage::Shutdown),
          "Filesystem shuts down before I/O");

    Check(RunWorldSessionObserverLifecycleSubprocess(),
          "P5 observer lifecycle executes in a fresh process because the managed I/O backend has one lifetime per process");
    Check(RunWorldSessionFirstFaultLifecycleSubprocess(),
          "P3 first-fault competition executes in a fresh process and preserves the original session cause through terminal cleanup");
    Check(RunWorldSessionPartialStartLifecycleSubprocess(),
          "P4 admitted partial-start terminal failure executes in a fresh process and reaches ownership-empty Failed through one rollback transaction");

    vanguard::diagnostics::Shutdown();

    if (g_failures == 0)
        std::printf("[engineServicesTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
