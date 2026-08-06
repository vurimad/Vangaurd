#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/engine/streaming_observer_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/engine/world_session_service.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/game_input/mapping_resource.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/world/worlds.hpp>

#include <cstdio>
#include <cmath>

namespace
{
    int g_failures = 0;

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

    class EmptyInputBackend final : public vanguard::input::IInputBackend
    {
    public:
        [[nodiscard]] vanguard::input::BackendDrainResult Drain(vanguard::input::RawEvent*, vanguard::u32) noexcept override
        {
            return {};
        }
        [[nodiscard]] bool SetRumble(vanguard::input::DeviceId, vanguard::f32, vanguard::f32,
                                     vanguard::u32) noexcept override { return false; }
        void RequestDeviceRefresh() noexcept override {}
    };

    [[nodiscard]] vanguard::u64 ReadFakeFrameClock(void* const userData) noexcept
    {
        return static_cast<FakeFrameClock*>(userData)->ticks;
    }

    [[nodiscard]] vanguard::engine::FrameParticipantStatus RecordFrameParticipant(
        const vanguard::engine::FrameContext& context, void* const userData) noexcept
    {
        auto& fixture = *static_cast<FrameParticipantFixture*>(userData);
        if (fixture.fail) return vanguard::engine::FrameParticipantStatus::Failure("requested participant failure");
        if (fixture.trace != nullptr && fixture.trace->count < 32)
        {
            fixture.trace->participants[fixture.trace->count] = fixture.id;
            fixture.trace->contexts[fixture.trace->count] = context;
            ++fixture.trace->count;
        }
        return vanguard::engine::FrameParticipantStatus::Success();
    }

    [[nodiscard]] bool BuildWorldSessionPackage(vanguard::containers::DynamicArray<vanguard::u8>& output) noexcept
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
        if (writer.Begin(file, options, packageSet) != vanguard::packages::Result::Success) return false;

        constexpr vanguard::u8 InvalidWorldPayload[16]{};
        const vanguard::packages::BuildSegment segment{
            InvalidWorldPayload, sizeof(InvalidWorldPayload), vanguard::packages::Codec::None, 4,
            vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource world;
        world.path = "world/session_test.vworld";
        world.type = vanguard::world::WorldResourceType;
        world.flags = vanguard::packages::ResourceFlags::Startup;
        world.segments = {&segment, 1};
        vanguard::containers::DynamicArray<vanguard::u8> mappingBytes(
            vanguard::memory::pools::Serialization::GetInstance());
        vanguard::filesystem::MemoryFileWriter mappingFile(mappingBytes);
        const vanguard::game_input::MappingBuildDescription mapping;
        if (vanguard::game_input::CookMapping(mapping, mappingFile) != vanguard::game_input::MappingResult::Success)
            return false;
        const vanguard::packages::BuildSegment inputSegment{
            mappingBytes.Data(), mappingBytes.Size(), vanguard::packages::Codec::None, 4,
            vanguard::packages::SegmentFlags::MemoryResident};
        vanguard::packages::BuildResource input;
        input.path = "input/default.vinput";
        input.type = vanguard::game_input::MappingResourceType;
        input.flags = vanguard::packages::ResourceFlags::Startup;
        input.segments = {&inputSegment, 1};
        return writer.Add(world) == vanguard::packages::Result::Success &&
               writer.Add(input) == vanguard::packages::Result::Success &&
               writer.Finalize() == vanguard::packages::Result::Success;
    }

    [[nodiscard]] bool SaveBytes(const vanguard::filesystem::AbsolutePath& path, const void* const data,
                                 const vanguard::usize size) noexcept
    {
        auto writer = vanguard::filesystem::RawFileWriter::Create(path, false);
        if (!writer) return false;
        if (size != 0) writer->Serialize(const_cast<void*>(data), size);
        writer->Flush();
        return writer->GetSize() == size;
    }

    [[nodiscard]] vanguard::engine::WorldSessionStatus PollSessionUntil(
        vanguard::engine::WorldSessionService& session, const vanguard::engine::WorldSessionStatus target) noexcept
    {
        vanguard::engine::WorldSessionFailure failure;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const vanguard::engine::WorldSessionStatus status = session.Poll(&failure);
            if (status == target || status == vanguard::engine::WorldSessionStatus::Failed) return status;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return session.Status();
    }

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        std::fprintf(stderr, "[engineServicesTests] FAILED: %s\n", message);
        ++g_failures;
    }

    void RecordLifecycle(const vanguard::application::LifecycleEvent& event, void* const userData) noexcept
    {
        auto& trace = *static_cast<LifecycleTrace*>(userData);
        if (trace.count < 256) trace.events[trace.count++] = event;
    }

    [[nodiscard]] vanguard::u32 FindLifecycleEvent(const LifecycleTrace& trace,
                                                   const vanguard::application::ServiceId service,
                                                   const vanguard::application::LifecycleStage stage) noexcept
    {
        for (vanguard::u32 index = 0; index < trace.count; ++index)
            if (trace.events[index].service == service && trace.events[index].stage == stage) return index;
        return 128;
    }
}

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "engineServicesTests"),
          "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete engine module registration");
        Check(vanguard::engine::RegisterJobsService(incompleteHost, &incompleteFailure),
              "incomplete Jobs service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::JobsServiceId &&
                  incompleteFailure.relatedService == vanguard::engine::IoServiceId,
              "Jobs graph rejects a missing I/O dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete filesystem engine module registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure),
              "incomplete Filesystem service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::FilesystemServiceId &&
                  incompleteFailure.relatedService == vanguard::engine::IoServiceId,
              "Filesystem graph rejects a missing I/O dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete resources engine module registration");
        Check(vanguard::engine::RegisterIoService(incompleteHost, &incompleteFailure),
              "incomplete resources I/O service registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure),
              "incomplete resources Filesystem service registration");
        Check(vanguard::engine::RegisterResourcesService(incompleteHost, &incompleteFailure),
              "incomplete Resources service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::ResourcesServiceId &&
                  incompleteFailure.relatedService == vanguard::engine::JobsServiceId,
              "Resources graph rejects a missing Jobs dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete Resource Streaming engine module registration");
        Check(vanguard::engine::RegisterIoService(incompleteHost, &incompleteFailure),
              "incomplete Resource Streaming I/O service registration");
        Check(vanguard::engine::RegisterFilesystemService(incompleteHost, &incompleteFailure),
              "incomplete Resource Streaming Filesystem service registration");
        Check(vanguard::engine::RegisterJobsService(incompleteHost, &incompleteFailure),
              "incomplete Resource Streaming Jobs service registration");
        Check(vanguard::engine::RegisterResourceStreamingService(incompleteHost, &incompleteFailure),
              "incomplete Resource Streaming service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::ResourceStreamingServiceId &&
                  incompleteFailure.relatedService == vanguard::engine::ResourcesServiceId,
              "Resource Streaming graph rejects a missing Resources dependency");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete World engine module registration");
        Check(vanguard::engine::RegisterWorldService(incompleteHost, &incompleteFailure),
              "incomplete World service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::WorldServiceId &&
                  (incompleteFailure.relatedService == vanguard::engine::ResourceStreamingServiceId ||
                   incompleteFailure.relatedService == vanguard::engine::ResourcesServiceId),
              "World graph rejects missing resource dependencies");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete Game World engine module registration");
        Check(vanguard::engine::RegisterGameWorldService(incompleteHost, &incompleteFailure),
              "incomplete Game World service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::GameWorldServiceId,
              "Game World graph rejects missing World dependencies");
    }

    {
        vanguard::application::EngineHost incompleteHost;
        vanguard::application::HostFailure incompleteFailure;
        Check(vanguard::engine::RegisterEngineModule(incompleteHost, &incompleteFailure),
              "incomplete World Session engine module registration");
        Check(vanguard::engine::RegisterWorldSessionService(incompleteHost, &incompleteFailure),
              "incomplete World Session service registration");
        Check(!incompleteHost.Compile(vanguard::application::ApplicationProfile::Runtime, &incompleteFailure) &&
                  incompleteFailure.code == vanguard::application::HostFailureCode::MissingDependency &&
                  incompleteFailure.service == vanguard::engine::WorldSessionServiceId,
              "World Session graph rejects missing runtime dependencies");
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
    Check(vanguard::engine::RegisterInputService(host, &inputBackend, &failure), "Input service registration");
    Check(vanguard::engine::RegisterResourcesService(host, &failure), "Resources service registration");
    Check(vanguard::engine::RegisterResourceStreamingService(host, &failure),
          "Resource Streaming service registration");
    Check(vanguard::engine::RegisterGameInputService(host, &failure), "Game Input service registration");
    Check(vanguard::engine::RegisterWorldService(host, &failure), "World service registration");
    Check(vanguard::engine::RegisterGameWorldService(host, &failure), "Game World service registration");
    Check(vanguard::engine::RegisterStreamingObserverService(host, &failure),
          "Streaming Observer service registration");
    Check(vanguard::engine::RegisterWorldSessionService(host, &failure), "World Session service registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Runtime, &failure), "engine graph compilation");
    Check(host.Start(&failure), "engine graph startup");
    Check(host.StateOf(vanguard::engine::JobsServiceId) == vanguard::application::ServiceState::Running,
          "Jobs service running state");
    Check(host.StateOf(vanguard::engine::FramePipelineServiceId) == vanguard::application::ServiceState::Running,
          "Frame Pipeline service running state");
    Check(host.StateOf(vanguard::engine::IoServiceId) == vanguard::application::ServiceState::Running,
          "I/O service running state");
    Check(host.StateOf(vanguard::engine::FilesystemServiceId) == vanguard::application::ServiceState::Running,
          "Filesystem service running state");
    Check(host.StateOf(vanguard::engine::ResourcesServiceId) == vanguard::application::ServiceState::Running,
          "Resources service running state");
    Check(host.StateOf(vanguard::engine::ResourceStreamingServiceId) == vanguard::application::ServiceState::Running,
          "Resource Streaming service running state");
    Check(host.StateOf(vanguard::engine::WorldServiceId) == vanguard::application::ServiceState::Running,
          "World service running state");
    Check(host.StateOf(vanguard::engine::GameWorldServiceId) == vanguard::application::ServiceState::Running,
          "Game World service running state");
    Check(host.StateOf(vanguard::engine::StreamingObserverServiceId) ==
              vanguard::application::ServiceState::Running,
          "Streaming Observer service running state");
    Check(host.StateOf(vanguard::engine::WorldSessionServiceId) == vanguard::application::ServiceState::Running,
          "World Session service running state");
    Check(host.FindCapability(vanguard::engine::IoCapabilityId) != nullptr, "I/O capability publication");
    Check(host.FindCapability(vanguard::engine::FilesystemCapabilityId) != nullptr,
          "Filesystem capability publication");
    Check(host.FindCapability(vanguard::engine::JobSchedulerCapabilityId) != nullptr,
          "Jobs scheduler capability publication");
    Check(host.FindCapability(vanguard::engine::FramePipelineCapabilityId) != nullptr,
          "Frame Pipeline capability publication");
    Check(host.FindCapability(vanguard::engine::StreamingObserverCapabilityId) != nullptr,
          "Streaming Observer capability publication");
    Check(host.FindCapability(vanguard::engine::WorldSessionCapabilityId) != nullptr,
          "World Session capability publication");
    Check(host.FindCapability(vanguard::engine::ResourceRegistryCapabilityId) != nullptr &&
              host.FindCapability(vanguard::engine::ResourceRegistryCapabilityId) ==
                  host.FindCapability(vanguard::engine::ResourcePipelineCapabilityId),
          "Resources registry and pipeline capability publication");
    vanguard::engine::ResourcesService* const resourcesService = vanguard::engine::FindResourcesService(host);
    Check(resourcesService != nullptr && resourcesService->Registry().IsInitialized() &&
              resourcesService->Pipeline().IsInitialized(),
          "typed Resources service access");
    vanguard::engine::ResourceStreamingService* const resourceStreamingService =
        vanguard::engine::FindResourceStreamingService(host);
    const vanguard::streaming::Stats resourceStreamingStats =
        resourceStreamingService != nullptr ? resourceStreamingService->Streamer().GetStats() : vanguard::streaming::Stats{};
    Check(resourceStreamingService != nullptr && resourceStreamingService->Streamer().IsInitialized() &&
              !resourceStreamingService->PackageSet().IsMounted() &&
              resourceStreamingStats.stagingBudgetBytes == 512ull * 1024ull * 1024ull &&
              resourceStreamingStats.activeLoads == 0 && resourceStreamingStats.activeReads == 0,
          "typed Resource Streaming service access and default budget");
    vanguard::engine::WorldService* const worldService = vanguard::engine::FindWorldService(host);
    Check(worldService != nullptr && worldService->Status() == vanguard::engine::WorldResourceStatus::Idle &&
              worldService->Resource() == nullptr && worldService->Grid() == nullptr && worldService->Executor() == nullptr,
          "typed World service access and empty startup state");
    vanguard::engine::GameWorldService* const gameWorldService = vanguard::engine::FindGameWorldService(host);
    Check(gameWorldService != nullptr && gameWorldService->Status() == vanguard::engine::GameWorldStatus::Idle &&
              gameWorldService->World() == nullptr && gameWorldService->CellStreaming() == nullptr,
          "typed Game World service access and empty session state");
    vanguard::engine::StreamingObserverService* const streamingObservers =
        vanguard::engine::FindStreamingObserverService(host);
    Check(streamingObservers != nullptr && streamingObservers->GetStats().registeredObservers == 0,
          "typed Streaming Observer service access and empty startup state");

    vanguard::engine::StreamingObserverDescriptor cameraDescriptor;
    cameraDescriptor.name = "gameCamera";
    cameraDescriptor.velocityClass = vanguard::engine::StreamingObserverVelocityClass::OnFoot;
    vanguard::engine::StreamingObserverHandle cameraObserver;
    vanguard::engine::StreamingObserverDescriptor vehicleDescriptor;
    vehicleDescriptor.name = "playerVehicle";
    vehicleDescriptor.velocityClass = vanguard::engine::StreamingObserverVelocityClass::GroundVehicle;
    vanguard::engine::StreamingObserverHandle vehicleObserver;
    Check(streamingObservers != nullptr && streamingObservers->RegisterObserver(cameraDescriptor, cameraObserver) &&
              streamingObservers->RegisterObserver(vehicleDescriptor, vehicleObserver),
          "Streaming Observer fixed-capacity registrations");
    vanguard::engine::StreamingObserverUpdate cameraUpdate;
    cameraUpdate.position[0] = 100.0;
    cameraUpdate.position[1] = 200.0;
    cameraUpdate.position[2] = 300.0;
    cameraUpdate.velocity[0] = 10.0;
    vanguard::engine::StreamingObserverUpdate vehicleUpdate;
    vehicleUpdate.velocity[0] = 30.0;
    vehicleUpdate.velocity[1] = 40.0;
    Check(streamingObservers != nullptr && streamingObservers->UpdateObserver(cameraObserver, cameraUpdate) &&
              streamingObservers->UpdateObserver(vehicleObserver, vehicleUpdate) &&
              streamingObservers->SetPrimaryObserver(cameraObserver) &&
              streamingObservers->SetGlobalDistanceScale(1.5f),
          "Streaming Observer producer updates and primary-camera selection");
    vanguard::engine::WorldSessionService* const worldSessionService =
        vanguard::engine::FindWorldSessionService(host);
    Check(worldSessionService != nullptr && worldSessionService->Status() == vanguard::engine::WorldSessionStatus::Idle &&
              worldSessionService->RequestStop(vanguard::engine::WorldSessionStopMode::ReleaseEverything) &&
              worldSessionService->Poll() == vanguard::engine::WorldSessionStatus::Idle,
          "typed World Session access and idempotent empty-session stop");
    vanguard::engine::WorldSessionStartRequest invalidRetainedStart;
    invalidRetainedStart.mountPackages = false;
    vanguard::engine::WorldSessionFailure worldSessionFailure;
    const bool invalidBeginRejected = worldSessionService != nullptr &&
        !worldSessionService->Begin(invalidRetainedStart, &worldSessionFailure);
    const bool invalidFailureReported = worldSessionFailure.code == vanguard::engine::WorldSessionFailureCode::InvalidState &&
        worldSessionService->Status() == vanguard::engine::WorldSessionStatus::Failed;
    const bool invalidStopAccepted = worldSessionService->RequestStop(
        vanguard::engine::WorldSessionStopMode::ReleaseEverything, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus invalidStopStatus = worldSessionService->Poll(&worldSessionFailure);
    Check(invalidBeginRejected && invalidFailureReported && invalidStopAccepted &&
              invalidStopStatus == vanguard::engine::WorldSessionStatus::Idle,
          "World Session reports invalid retained starts and recovers through explicit cleanup");

    vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
    const vanguard::filesystem::AbsolutePath sessionDirectory =
        vanguard::filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("vanguard_world_session_conformance");
    const vanguard::filesystem::AbsolutePath sessionPackage = sessionDirectory.AddFilePath("DATA000.vpak");
    static_cast<void>(files.DeleteFile(sessionPackage));
    static_cast<void>(files.DeletePath(sessionDirectory));
    Check(files.CreatePath(sessionDirectory), "World Session fixture directory creation");
    vanguard::containers::DynamicArray<vanguard::u8> sessionPackageBytes(
        vanguard::memory::pools::Resources::GetInstance());
    Check(BuildWorldSessionPackage(sessionPackageBytes) &&
              SaveBytes(sessionPackage, sessionPackageBytes.Data(), sessionPackageBytes.Size()),
          "World Session DATA000 fixture publication");

    vanguard::engine::WorldSessionStartRequest initialSessionStart;
    initialSessionStart.gameDirectory = sessionDirectory;
    Check(worldSessionService->Begin(initialSessionStart, &worldSessionFailure) &&
              worldSessionService->Status() == vanguard::engine::WorldSessionStatus::LoadingWorld &&
              resourceStreamingService->PackageSet().IsMounted(),
          "World Session mounts packages and begins the catalog startup world");
    Check(worldSessionService->RequestStop(vanguard::engine::WorldSessionStopMode::ReleaseWorld,
                                           &worldSessionFailure) &&
              PollSessionUntil(*worldSessionService, vanguard::engine::WorldSessionStatus::Mounted) ==
                  vanguard::engine::WorldSessionStatus::Mounted &&
              resourceStreamingService->PackageSet().IsMounted(),
          "World Session world-only release retains the package set");

    vanguard::engine::WorldSessionStartRequest retainedSessionStart;
    retainedSessionStart.mountPackages = false;
    retainedSessionStart.world = resourceStreamingService->PackageSet().StartupWorld();
    Check(worldSessionService->Begin(retainedSessionStart, &worldSessionFailure) &&
              worldSessionService->Status() == vanguard::engine::WorldSessionStatus::LoadingWorld,
          "World Session begins another world from retained packages");
    const bool fullStopAccepted = worldSessionService->RequestStop(
        vanguard::engine::WorldSessionStopMode::ReleaseEverything, &worldSessionFailure);
    const vanguard::engine::WorldSessionStatus fullStopStatus = PollSessionUntil(
        *worldSessionService, vanguard::engine::WorldSessionStatus::Idle);
    Check(fullStopAccepted && fullStopStatus == vanguard::engine::WorldSessionStatus::Idle &&
              !resourceStreamingService->PackageSet().IsMounted(),
          "World Session full release unmounts retained packages");
    static_cast<void>(files.DeleteFile(sessionPackage));
    static_cast<void>(files.DeletePath(sessionDirectory));
    vanguard::engine::FramePipelineService* const framePipeline = vanguard::engine::FindFramePipelineService(host);
    vanguard::engine::FrameFailure frameFailure;
    FakeFrameClock frameClock;
    vanguard::engine::FramePipelineConfig frameConfig;
    frameConfig.clock = {&ReadFakeFrameClock, 1'000, &frameClock};
    frameConfig.maximumDeltaSeconds = 0.1f;
    frameConfig.fixedDeltaSeconds = 0.02f;
    frameConfig.maximumFixedStepsPerFrame = 2;
    frameConfig.pacing = vanguard::engine::FramePacingMode::Disabled;
    Check(framePipeline != nullptr && framePipeline->Configure(frameConfig, &frameFailure),
          "deterministic Frame Pipeline configuration");

    constexpr vanguard::engine::FrameParticipantId InputParticipant = 100;
    constexpr vanguard::engine::FrameParticipantId FixedParticipant = 200;
    constexpr vanguard::engine::FrameParticipantId SimulationBeforeParticipant = 20;
    constexpr vanguard::engine::FrameParticipantId SimulationAfterParticipant = 10;
    constexpr vanguard::engine::FrameParticipantId FailureParticipant = 300;
    FrameTrace frameTrace;
    FrameParticipantFixture inputFixture{&frameTrace, InputParticipant};
    FrameParticipantFixture fixedFixture{&frameTrace, FixedParticipant};
    FrameParticipantFixture simulationBeforeFixture{&frameTrace, SimulationBeforeParticipant};
    FrameParticipantFixture simulationAfterFixture{&frameTrace, SimulationAfterParticipant};
    FrameParticipantFixture failureFixture{&frameTrace, FailureParticipant};

    auto RegisterFrameFixture = [&](FrameParticipantFixture& fixture, const vanguard::engine::FramePhase phase,
                                    const vanguard::containers::ArraySpan<const vanguard::engine::FrameParticipantId> after = {})
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

    Check(RegisterFrameFixture(inputFixture, vanguard::engine::FramePhase::Input),
          "Input phase participant registration");
    Check(RegisterFrameFixture(fixedFixture, vanguard::engine::FramePhase::FixedSimulation),
          "fixed phase participant registration");
    Check(RegisterFrameFixture(simulationBeforeFixture, vanguard::engine::FramePhase::Simulation),
          "first Simulation participant registration");
    const vanguard::engine::FrameParticipantId simulationDependencies[]{SimulationBeforeParticipant};
    Check(RegisterFrameFixture(simulationAfterFixture, vanguard::engine::FramePhase::Simulation,
                               {simulationDependencies, 1}),
          "dependent Simulation participant registration");
    Check(RegisterFrameFixture(failureFixture, vanguard::engine::FramePhase::EndFrame),
          "End Frame participant registration");

    vanguard::engine::FrameParticipantDescriptor unsupportedParticipant;
    unsupportedParticipant.id = 999;
    unsupportedParticipant.name = "unsupportedWorkerParticipant";
    unsupportedParticipant.affinity = vanguard::application::ThreadAffinity::AnyWorker;
    unsupportedParticipant.execute = &RecordFrameParticipant;
    Check(!framePipeline->RegisterParticipant(unsupportedParticipant, &frameFailure) &&
              frameFailure.code == vanguard::engine::FrameFailureCode::UnsupportedAffinity,
          "unsupported worker affinity is rejected explicitly");
    Check(framePipeline->Compile(&frameFailure) &&
              framePipeline->State() == vanguard::engine::FramePipelineState::Compiled &&
              framePipeline->GetStats().scheduledParticipants == 9,
          "compiled Frame Pipeline contains deterministic service and test participants");

    frameClock.ticks += 90;
    Check(framePipeline->RunFrame(&frameFailure), "deterministic frame execution");
    vanguard::engine::FramePipelineStats frameStats = framePipeline->GetStats();
    Check(frameTrace.count == 6 && frameTrace.participants[0] == InputParticipant &&
              frameTrace.participants[1] == FixedParticipant && frameTrace.participants[2] == FixedParticipant &&
              frameTrace.participants[3] == SimulationBeforeParticipant &&
              frameTrace.participants[4] == SimulationAfterParticipant &&
              frameTrace.participants[5] == FailureParticipant,
          "global phases, repeated fixed steps, and same-phase dependencies execute deterministically");
    Check(frameStats.frames == 1 && frameStats.lastFixedSteps == 2 && frameStats.fixedSteps == 2 &&
              std::fabs(frameStats.fixedTimeSeconds - 0.04) < 0.0001 &&
              std::fabs(frameStats.lastRawDeltaSeconds - 0.09f) < 0.0001f &&
              std::fabs(frameStats.lastRealDeltaSeconds - 0.09f) < 0.0001f &&
              frameStats.droppedSimulationSeconds > 0.039 && frameStats.droppedSimulationSeconds < 0.041,
          "fixed-step catch-up is bounded and dropped time is reported");
    vanguard::engine::StreamingObserverSnapshot observerSnapshot;
    const vanguard::engine::StreamingObserverServiceStats observerStats =
        streamingObservers != nullptr ? streamingObservers->GetStats()
                                      : vanguard::engine::StreamingObserverServiceStats{};
    Check(streamingObservers != nullptr && streamingObservers->Snapshot(observerSnapshot) &&
              observerSnapshot.observerCount == 2 && observerSnapshot.sequence == 1 &&
              !observerSnapshot.usingWorldOriginFallback &&
              std::fabs(observerSnapshot.cameraPosition[0] - 100.0) < 0.0001 &&
              std::fabs(observerSnapshot.cameraPosition[1] - 200.0) < 0.0001 &&
              std::fabs(observerSnapshot.observers[0].predictedPosition[0] - 105.0) < 0.0001 &&
              std::fabs(observerSnapshot.observers[1].predictedPosition[0] - 12.0) < 0.0001 &&
              std::fabs(observerSnapshot.observers[1].predictedPosition[1] - 16.0) < 0.0001 &&
              std::fabs(observerSnapshot.globalDistanceScale - 1.5f) < 0.0001f &&
              observerStats.registeredObservers == 2 && observerStats.validObservers == 2 &&
              observerStats.submittedSnapshots == 0,
          "Streaming Observer snapshot applies velocity-class prediction caps without mutating source positions");
    Check(streamingObservers != nullptr && streamingObservers->UnregisterObserver(cameraObserver) &&
              !streamingObservers->UpdateObserver(cameraObserver, cameraUpdate) &&
              streamingObservers->UnregisterObserver(vehicleObserver) &&
              streamingObservers->GetStats().registeredObservers == 0 &&
              streamingObservers->GetStats().rejectedUpdates == 1,
          "Streaming Observer generational handles reject stale producer updates");
    Check(frameTrace.contexts[1].fixedStep == 0 && frameTrace.contexts[2].fixedStep == 1 &&
              frameTrace.contexts[1].fixedStepCount == 2 &&
              std::fabs(frameTrace.contexts[1].simulationDeltaSeconds - 0.02f) < 0.0001f &&
              std::fabs(frameTrace.contexts[1].fixedTimeSeconds - 0.02) < 0.0001 &&
              std::fabs(frameTrace.contexts[2].fixedTimeSeconds - 0.04) < 0.0001,
          "fixed-step contexts expose stable step identity, delta, and absolute time");

    Check(framePipeline->SetPaused(true), "pause request at frame boundary");
    frameTrace.count = 0;
    frameClock.ticks += 20;
    Check(framePipeline->RunFrame(&frameFailure) && frameTrace.count == 4 &&
              frameTrace.contexts[0].paused && frameTrace.contexts[0].simulationDeltaSeconds == 0.0f &&
              framePipeline->GetStats().lastFixedSteps == 0,
          "paused frames continue non-simulation phases without fixed advancement");
    Check(framePipeline->SetPaused(false) && !framePipeline->SetTimeScale(-1.0f) &&
              framePipeline->SetTimeScale(0.5f),
          "time controls accept only valid boundary changes");
    frameTrace.count = 0;
    frameClock.ticks += 40;
    Check(framePipeline->RunFrame(&frameFailure) && framePipeline->GetStats().lastFixedSteps == 1 &&
              std::fabs(framePipeline->GetStats().lastSimulationDeltaSeconds - 0.02f) < 0.0001f,
          "scaled time feeds both variable and fixed simulation domains");

    failureFixture.fail = true;
    frameTrace.count = 0;
    frameClock.ticks += 20;
    Check(!framePipeline->RunFrame(&frameFailure) &&
              frameFailure.code == vanguard::engine::FrameFailureCode::ParticipantFailure &&
              frameFailure.phase == vanguard::engine::FramePhase::EndFrame &&
              frameFailure.participant == FailureParticipant &&
              framePipeline->State() == vanguard::engine::FramePipelineState::Failed,
          "participant failures preserve phase and participant identity");
    Check(vanguard::jobs::IsInitialized() && vanguard::jobs::WorkerCount() != 0,
          "Jobs scheduler initialization");
    Check(vanguard::io::IsInitialized(), "I/O initialization");
    Check(vanguard::filesystem::IsInitialized(), "Filesystem initialization");
    const vanguard::filesystem::AbsolutePath root = vanguard::filesystem::paths::GetRootDirectory();
    Check(vanguard::filesystem::GetManager().GetEngineRoot() == root &&
              vanguard::filesystem::GetManager().GetGameRoot() == root.AddDirPath("data") &&
              vanguard::filesystem::GetManager().GetCacheDirectory() == root.AddDirPath("cache"),
          "Filesystem launch roots");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::IoServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "I/O initializes before Filesystem");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Filesystem initializes before Jobs");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FramePipelineServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Jobs initializes before Frame Pipeline");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FramePipelineServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Frame Pipeline initializes before Game World registration");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Jobs initializes before Resources");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Resources initializes before Resource Streaming");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Resource Streaming initializes before World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "World initializes before Game World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId,
                             vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId,
                                 vanguard::application::LifecycleStage::Initialize) &&
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId,
                                 vanguard::application::LifecycleStage::Initialize) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldSessionServiceId,
                                 vanguard::application::LifecycleStage::Initialize),
          "Game World and Streaming Observer initialize before World Session");
    Check(host.Shutdown(&failure), "engine graph shutdown");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "Resource Streaming shuts down before Resources");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourceStreamingServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "World shuts down before Resource Streaming");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "Game World shuts down before World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::WorldSessionServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId,
                                 vanguard::application::LifecycleStage::Shutdown) &&
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::StreamingObserverServiceId,
                                 vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::GameWorldServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "World Session and Streaming Observer shut down before Game World");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::ResourcesServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "Resources shuts down before Jobs");
    Check(!vanguard::jobs::IsInitialized(), "Jobs scheduler shutdown");
    Check(!vanguard::filesystem::IsInitialized(), "Filesystem shutdown");
    Check(!vanguard::io::IsInitialized(), "I/O shutdown");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::JobsServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "Jobs shuts down before Filesystem");
    Check(FindLifecycleEvent(lifecycleTrace, vanguard::engine::FilesystemServiceId,
                             vanguard::application::LifecycleStage::Shutdown) <
              FindLifecycleEvent(lifecycleTrace, vanguard::engine::IoServiceId,
                                 vanguard::application::LifecycleStage::Shutdown),
          "Filesystem shuts down before I/O");

    vanguard::diagnostics::Shutdown();

    if (g_failures == 0) std::printf("[engineServicesTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
