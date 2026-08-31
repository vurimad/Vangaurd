#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resources_service.hpp>

#include <vanguard/assets/asset_index.hpp>
#include <vanguard/assets/derived_data_package_artifact_reader.hpp>
#include <vanguard/assets/loose_resource_materializer.hpp>
#include <vanguard/assets/package_planner.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/texture_resource.hpp>
#include <vanguard/textures/textures.hpp>

#include <cstring>
#include <cstdio>

namespace
{
    namespace assets = vanguard::assets;
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace packages = vanguard::packages;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace rhi = vanguard::rhi;
    namespace streaming = vanguard::streaming;
    namespace textureTools = vanguard::texture_tools;
    namespace textures = vanguard::textures;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    inline constexpr textureTools::TextureCookingProfileId RuntimeProofProfile = 0x74787470726f6f66ull;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[textureResidencyServiceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct TestClock
    {
        vanguard::u64 ticks = 1'000;
    };

    [[nodiscard]] vanguard::u64 ReadClock(void* const userData) noexcept
    {
        return static_cast<TestClock*>(userData)->ticks;
    }

    struct PackagePathContext
    {
        resources::ResourceReference resource;
        const char* path = nullptr;
    };

    [[nodiscard]] bool ResolvePackagePath(const resources::ResourceReference resource, char* const destination,
                                          const vanguard::usize capacity, vanguard::usize& written, void* const userData) noexcept
    {
        const PackagePathContext& context = *static_cast<const PackagePathContext*>(userData);
        if (resource != context.resource || context.path == nullptr)
            return false;
        written = 0;
        while (context.path[written] != '\0')
        {
            if (written == capacity)
                return false;
            destination[written] = context.path[written];
            ++written;
        }
        return true;
    }

    [[nodiscard]] bool BuildAndPublish(assets::BuildSystem& system, assets::DependencyIndex& index,
                                       const assets::BuildRequest& request, assets::BuildOutput& output) noexcept
    {
        assets::BuildPlan plan;
        return system.Prepare(request, plan) == assets::Result::Success &&
               system.Execute(request, plan, output) == assets::Result::Success &&
               index.Publish(request, plan, output) == assets::IndexResult::Success;
    }

    void DeleteDirectoryTree(filesystem::Manager& files, const filesystem::AbsolutePath& directory) noexcept
    {
        vanguard::containers::DynamicArray<filesystem::AbsolutePath> children(vanguard::memory::pools::Assets::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            DeleteDirectoryTree(files, child);

        vanguard::containers::DynamicArray<filesystem::AbsolutePath> storedFiles(vanguard::memory::pools::Assets::GetInstance());
        files.FindFiles(directory, vanguard::containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            static_cast<void>(files.DeleteFile(storedFile));
        static_cast<void>(files.DeletePath(directory));
    }

    [[nodiscard]] bool SubmitRetirementFences(rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        fences = {};
        const rhi::CommandListType types[]{rhi::CommandListType::Default, rhi::CommandListType::Compute, rhi::CommandListType::CopyAsync};
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            const rhi::CommandListRef list = rhi::CreateCommandList(types[index], 0x5458545356433500ull + index, &failure);
            if (!list || !rhi::BindCommandList(list, &failure))
                return false;
            rhi::UnbindCommandList();
            const rhi::CommandListRef lists[]{list};
            rhi::GpuFence completion;
            if (!rhi::CloseAndSubmitCommandLists("texture service retirement", {lists, 1}, rhi::CommandListSyncType::None, completion, &failure))
                return false;
            fences.Include(completion);
        }
        return true;
    }

    [[nodiscard]] bool WaitRetirementFences(const rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        return rhi::WaitForGpuFence({rhi::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) &&
               rhi::WaitForGpuFence({rhi::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
               rhi::WaitForGpuFence({rhi::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
    }

    [[nodiscard]] bool RunFrame(engine::FramePipelineService& frames, TestClock& clock) noexcept
    {
        clock.ticks += 16;
        engine::FrameFailure failure;
        if (frames.RunFrame(&failure))
            return true;
        std::fprintf(stderr, "[textureResidencyServiceTests] frame failure: %s\n", failure.message != nullptr ? failure.message : "unknown");
        return false;
    }

    [[nodiscard]] bool DriveUntilReady(engine::FramePipelineService& frames, TestClock& clock,
                                       rendering::TextureResidencyRuntime& runtime, const rendering::GpuTextureResidencyHandle residency) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!RunFrame(frames, clock) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.state == rendering::TextureRuntimeState::BindlessReady)
                return true;
            if (info.state == rendering::TextureRuntimeState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool VerifyPhysicalMip(const textures::TextureResourceObject& resource, const rendering::TextureResidencyInfo& residency,
                                         const vanguard::u32 assetMip) noexcept
    {
        const textures::TextureFile& metadata = resource.GetMetadata();
        const vanguard::u32 subresource = metadata.FindSubresource(static_cast<vanguard::u8>(assetMip));
        const auto subresources = metadata.GetSubresources();
        if (subresource == textures::InvalidSubresourceIndex || subresource >= subresources.Count())
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u has no cooked subresource\n", assetMip);
            return false;
        }
        const textures::SubresourceRecord& record = subresources[subresource];
        if (record.depth != 1 || record.rowPitch == 0 || record.slicePitch == 0 || record.slicePitch % record.rowPitch != 0 ||
            record.byteSize != record.slicePitch)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u has unsupported proof layout: %ux%u row=%u slice=%u bytes=%llu depth=%u\n",
                         assetMip, record.width, record.height, record.rowPitch, record.slicePitch,
                         static_cast<unsigned long long>(record.byteSize), record.depth);
            return false;
        }

        textures::TextureSubresourceReadRequest expectedRead;
        if (resource.GetSubresourceSource().ReadSubresourceAsync(metadata, subresource, expectedRead) != textures::Result::Success ||
            !expectedRead.TryWait(10'000) || expectedRead.GetResult() != textures::Result::Success ||
            expectedRead.GetBytes().Count() != record.byteSize)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u cooked-byte read failed\n", assetMip);
            return false;
        }

        rhi::Failure failure;
        rhi::CommandListRef commands = rhi::CreateCommandList(rhi::CommandListType::Default, 0x5458545244424b31ull, &failure);
        if (!commands)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback command-list creation failed: %s\n", assetMip,
                         failure.message != nullptr ? failure.message : "unknown");
            return false;
        }
        if (!rhi::BindCommandList(commands, &failure))
        {
            rhi::DiscardCommandList(commands);
            return false;
        }

        const vanguard::u16 physicalMip = static_cast<vanguard::u16>(assetMip - residency.firstResidentMip);
        const rhi::ResourceState shaderRead = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
        const rhi::SubresourceRange range{physicalMip, 1, 0, 1};
        bool recorded = rhi::TransitionTexture(residency.texture, shaderRead, rhi::ResourceState::CopySource, range, &failure);
        rhi::TextureReadback readback;
        if (recorded)
            readback = rhi::TextureReadback(rhi::AdoptReference,
                                            rhi::RequestTextureReadback(residency.texture, {{physicalMip, 0}}, &failure));
        recorded = recorded && readback.IsValid() &&
                   rhi::TransitionTexture(residency.texture, rhi::ResourceState::CopySource, shaderRead, range, &failure);
        rhi::UnbindCommandList();
        if (!recorded)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback recording failed: %s\n", assetMip,
                         failure.message != nullptr ? failure.message : "unknown");
            rhi::DiscardCommandList(commands);
            return false;
        }

        const rhi::CommandListRef submissions[]{commands};
        rhi::GpuFence completion;
        if (!rhi::CloseAndSubmitCommandLists("texture residency physical mip proof", {submissions, 1}, rhi::CommandListSyncType::None, completion,
                                             &failure) ||
            !rhi::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || !rhi::RetireResources(&failure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback submission failed: %s\n", assetMip,
                         failure.message != nullptr ? failure.message : "unknown");
            return false;
        }

        rhi::TextureReadbackInfo readbackInfo;
        rhi::TextureReadbackMapping mapping;
        if (!rhi::GetTextureReadbackInfo(readback.GetRef(), readbackInfo, &failure) ||
            readbackInfo.state != rhi::TextureReadbackState::Ready || readbackInfo.extent.width != record.width ||
            readbackInfo.extent.height != record.height || readbackInfo.extent.depth != 1 ||
            !rhi::MapTextureReadback(readback.GetRef(), mapping, &failure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback map failed: extent=%ux%ux%u expected=%ux%u state=%u message=%s\n",
                         assetMip, readbackInfo.extent.width, readbackInfo.extent.height, readbackInfo.extent.depth,
                         record.width, record.height, static_cast<unsigned>(readbackInfo.state),
                         failure.message != nullptr ? failure.message : "unknown");
            return false;
        }

        const auto expected = expectedRead.GetBytes();
        const auto* const actualBytes = static_cast<const vanguard::u8*>(mapping.data);
        const vanguard::u32 rows = record.slicePitch / record.rowPitch;
        bool matches = mapping.rowPitch >= record.rowPitch && mapping.depthPitch >= record.slicePitch &&
                       mapping.dataSize >= mapping.depthPitch;
        if (matches)
            for (vanguard::u32 row = 0; row < rows; ++row)
                matches = matches && std::memcmp(actualBytes + static_cast<vanguard::u64>(row) * mapping.rowPitch,
                                                 expected.Data() + static_cast<vanguard::u64>(row) * record.rowPitch,
                                                 record.rowPitch) == 0;
        if (!matches)
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback mismatch: rows=%u gpuRow=%llu gpuDepth=%llu gpuBytes=%llu cookedRow=%u cookedSlice=%u\n",
                         assetMip, rows, static_cast<unsigned long long>(mapping.rowPitch),
                         static_cast<unsigned long long>(mapping.depthPitch), static_cast<unsigned long long>(mapping.dataSize),
                         record.rowPitch, record.slicePitch);
        const bool unmapped = rhi::UnmapTextureReadback(readback.GetRef(), &failure);
        return unmapped && matches;
    }

    [[nodiscard]] bool VerifyGpuTextureRecord(const rendering::GpuSceneRuntime& gpuScene,
                                              const rendering::TextureResidencyInfo& residency) noexcept
    {
        rendering::GpuSceneElementAddress address;
        if (!gpuScene.GetTables().Resolve<rendering::GpuTextureResidency>(residency.handle.index, address))
            return false;

        rhi::Failure failure;
        rhi::BufferDesc readbackDesc;
        readbackDesc.size = sizeof(rendering::GpuTextureResidency);
        readbackDesc.usage = rhi::BufferUsage::CopyDestination;
        readbackDesc.initialState = rhi::ResourceState::CopyDestination;
        readbackDesc.memoryType = rhi::MemoryType::Readback;
        rhi::Buffer readback(rhi::AdoptReference, rhi::CreateBuffer(readbackDesc, {}, &failure));
        rhi::CommandListRef commands = rhi::CreateCommandList(rhi::CommandListType::CopySync, 0x5458545244424b32ull, &failure);
        if (!readback || !commands)
            return false;
        if (!rhi::BindCommandList(commands, &failure))
        {
            rhi::DiscardCommandList(commands);
            return false;
        }

        const rhi::ResourceState shaderRead = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
        const bool recorded = rhi::TransitionBuffer(address.buffer, rhi::ResourceState::Unknown, rhi::ResourceState::CopySource, &failure) &&
                              rhi::CopyBuffer(readback.GetRef(), 0, address.buffer, address.byteOffset,
                                              sizeof(rendering::GpuTextureResidency), &failure) &&
                              rhi::TransitionBuffer(address.buffer, rhi::ResourceState::Unknown, shaderRead, &failure);
        rhi::UnbindCommandList();
        if (!recorded)
        {
            rhi::DiscardCommandList(commands);
            return false;
        }

        const rhi::CommandListRef submissions[]{commands};
        rhi::GpuFence completion;
        if (!rhi::CloseAndSubmitCommandLists("texture residency GPU Scene record proof", {submissions, 1}, rhi::CommandListSyncType::None,
                                             completion, &failure) ||
            !rhi::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return false;

        const auto* const uploaded = static_cast<const rendering::GpuTextureResidency*>(
            rhi::LockBuffer(readback.GetRef(), 0, sizeof(rendering::GpuTextureResidency), &failure));
        if (uploaded == nullptr)
            return false;
        const rendering::GpuTextureResidency copy = *uploaded;
        rhi::UnlockBuffer(readback.GetRef());
        const vanguard::u32 generation = copy.generationAndFlags & rendering::GpuTextureResidencyGenerationMask;
        const vanguard::u32 flags = copy.generationAndFlags >> rendering::GpuTextureResidencyFlagsShift;
        return copy.descriptor == residency.descriptor.GpuIndex() && copy.firstResidentMip == residency.firstResidentMip &&
               copy.residentMipCount == residency.residentMipCount &&
               generation == (residency.handle.generation & rendering::GpuTextureResidencyGenerationMask) &&
               flags == static_cast<vanguard::u32>(rendering::GpuTextureResidencyFlags::BindlessReady);
    }

    [[nodiscard]] bool VerifyTextureInstallation(const textures::TextureResourceObject& resource,
                                                 engine::RenderingService& service,
                                                 const rendering::GpuTextureResidencyHandle handle,
                                                 const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
    {
        if (!(resource.GetMetadata().GetSourceFingerprint() == expectedSourceFingerprint))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] source fingerprint proof failed\n");
            return false;
        }
        rhi::Failure rhiFailure;
        if (!rhi::WaitIdle(&rhiFailure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] proof GPU idle wait failed: %s\n",
                         rhiFailure.message != nullptr ? rhiFailure.message : "unknown");
            return false;
        }

        rendering::TextureResidencyInfo residency;
        rendering::TextureResidencyFailure residencyFailure;
        if (!service.GetTextureResidency().GetResidencyManager().GetInfo(handle, residency, &residencyFailure) ||
            residency.handle != handle || residency.state != rendering::TextureResidencyState::BindlessReady || !residency.texture ||
            !residency.descriptor || !(residency.contentFingerprint == resource.GetMetadata().GetContentFingerprint()) ||
            residency.residentMipCount == 0 || residency.firstResidentMip + residency.residentMipCount > resource.GetMetadata().GetMipCount())
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] physical residency proof failed: state=%u first=%u count=%u total=%u message=%s\n",
                         static_cast<unsigned>(residency.state), residency.firstResidentMip, residency.residentMipCount,
                         resource.GetMetadata().GetMipCount(), residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        for (vanguard::u32 mip = residency.firstResidentMip; mip < residency.firstResidentMip + residency.residentMipCount; ++mip)
            if (!VerifyPhysicalMip(resource, residency, mip))
            {
                std::fprintf(stderr, "[textureResidencyServiceTests] physical mip %u byte proof failed\n", mip);
                return false;
            }
        if (!VerifyGpuTextureRecord(service.GetGpuScene(), residency))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] GPU Scene texture record proof failed\n");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool RetireTexture(engine::FramePipelineService& frames, TestClock& clock,
                                     engine::RenderingService& service, rendering::TextureDemandHandle& demand) noexcept
    {
        demand.Reset();
        if (!RunFrame(frames, clock))
            return false;

        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        rendering::TextureResidencyRuntimeFailure textureFailure;
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::GpuSceneLifetime& lifetime = service.GetGpuScene().GetLifetime();
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !runtime.SealRetirements(safeAfter, &textureFailure) ||
            !lifetime.SealRetirements(safeAfter, &lifetimeFailure) || !WaitRetirementFences(safeAfter, rhiFailure) ||
            !rhi::RetireResources(&rhiFailure))
            return false;
        static_cast<void>(lifetime.Collect(&lifetimeFailure));
        static_cast<void>(runtime.CollectRetirements(&textureFailure));
        return RunFrame(frames, clock) && runtime.GetStats().residencyRecords == 0 && runtime.GetStats().liveDemands == 0;
    }

    [[nodiscard]] bool WaitForResourceDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = pipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeOperations == 0 && pipelineStats.activeJobs == 0 && pipelineStats.activePreparations == 0 &&
                pipelineStats.externalRequests == 0 && streamingStats.activeLoads == 0 && streamingStats.activeReads == 0 &&
                streamingStats.stagingBytesInUse == 0)
                return true;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool ExerciseTexture(const resources::ResourceReference reference, streaming::ResourceStreamer& streamer,
                                       engine::FramePipelineService& frames, TestClock& clock, engine::RenderingService& service,
                                       const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
    {
        resources::PipelineRequest request = streamer.Request(reference, resources::LoadPriority::High);
        if (!request.TryWait(10'000) || !request.HasLoaded())
            return false;
        resources::ResourceHandle resource = request.Acquire();
        const auto* const texture = static_cast<const textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen())
            return false;

        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureDemandHandle demand;
        const rendering::GpuSceneRuntimeStats before = service.GetGpuScene().GetStats();
        if (!runtime.RequestTexture(resource, demand, &failure) || !DriveUntilReady(frames, clock, runtime, demand.GetResidency()) ||
            !VerifyTextureInstallation(*texture, service, demand.GetResidency(), expectedSourceFingerprint))
            return false;
        const rendering::GpuSceneRuntimeStats after = service.GetGpuScene().GetStats();
        if (after.submittedBatches != before.submittedBatches + 1 || after.stagedContributions != before.stagedContributions + 1 ||
            after.acceptedContributions != before.acceptedContributions + 1 || after.retriedContributions != before.retriedContributions)
            return false;
        if (!RetireTexture(frames, clock, service, demand))
            return false;
        resource.Reset();
        request.Reset();
        return true;
    }
} // namespace

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "textureResidencyServiceTests"),
          "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    vanguard::application::EngineHost host;
    vanguard::application::HostFailure hostFailure;
    engine::RenderingServiceConfig renderingConfig;
    renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
    renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
    renderingConfig.resourceDescriptors.capacity = 128;
    renderingConfig.gpuScene.tables.maximumPagesPerTable = 2;
    renderingConfig.gpuScene.lifetime.retirementEpochCount = 4;
    renderingConfig.gpuScene.lifetime.initialRetirementsPerEpoch = 8;
    renderingConfig.gpuScene.upload.bytesPerSegment = 2u * 1024u * 1024u;
    renderingConfig.gpuScene.upload.maximumUpdatesPerBatch = 64;
    renderingConfig.gpuScene.upload.maximumCopiesPerBatch = 128;
    renderingConfig.gpuScene.definitions.maximumGeometries = 4;
    renderingConfig.gpuScene.definitions.maximumMaterials = 4;
    renderingConfig.gpuScene.definitions.maximumRenderables = 4;
    renderingConfig.gpuScene.definitions.maximumDefinitionsPerBatch = 4;
    renderingConfig.gpuScene.definitions.maximumAllocationsPerBatch = 16;
    renderingConfig.gpuScene.maximumExternalContributions = 2;
    renderingConfig.textureResidency.residency.maximumTextures = 4;
    renderingConfig.textureResidency.residency.maximumPendingInstallations = 4;
    renderingConfig.textureResidency.residency.maximumPendingRetirements = 4;
    renderingConfig.textureResidency.uploader.maximumRequests = 4;
    renderingConfig.textureResidency.uploader.maximumAcquisitionStartsPerTick = 4;
    renderingConfig.textureResidency.uploader.maximumCandidatesPerBatch = 4;
    renderingConfig.textureResidency.uploader.maximumCompletionPollsPerTick = 4;
    renderingConfig.textureResidency.uploader.maximumReadyCandidates = 4;
    renderingConfig.textureResidency.uploader.maximumWritesPerBatch = 8;
    renderingConfig.textureResidency.uploader.maximumCopiesPerBatch = 8;
    renderingConfig.textureResidency.maximumResidencyRecords = 4;
    renderingConfig.textureResidency.maximumDemands = 8;
    renderingConfig.textureResidency.maximumInstallationsPerTick = 4;
    renderingConfig.textureResidency.maximumTableInstallationsPerFrame = 4;
    renderingConfig.textureResidency.maximumStateChecksPerTick = 4;

    Check(engine::RegisterEngineModule(host, &hostFailure), "engine module registration");
    Check(engine::RegisterIoService(host, &hostFailure), "I/O service registration");
    Check(engine::RegisterFilesystemService(host, &hostFailure), "filesystem service registration");
    Check(engine::RegisterJobsService(host, &hostFailure), "jobs service registration");
    Check(engine::RegisterFramePipelineService(host, &hostFailure), "frame pipeline service registration");
    Check(engine::RegisterReflectionService(host, &hostFailure), "reflection service registration");
    Check(engine::RegisterResourcesService(host, &hostFailure), "resources service registration");
    Check(engine::RegisterResourceStreamingService(host, &hostFailure), "resource streaming service registration");
    Check(engine::RegisterRenderingService(host, renderingConfig, &hostFailure), "rendering service registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure), "engine service graph compilation");
    Check(host.Start(&hostFailure), "engine service graph startup");

    engine::FramePipelineService* const frames = engine::FindFramePipelineService(host);
    engine::ResourcesService* const resourcesService = engine::FindResourcesService(host);
    engine::ResourceStreamingService* const streamingService = engine::FindResourceStreamingService(host);
    engine::RenderingService* const renderingService = engine::FindRenderingService(host);
    Check(frames != nullptr && resourcesService != nullptr && streamingService != nullptr && renderingService != nullptr &&
              renderingService->GetTextureResidency().IsInitialized() && renderingService->GetGpuScene().IsInitialized(),
          "required rendering and resource services are initialized");

    TestClock clock;
    engine::FrameFailure frameFailure;
    engine::FramePipelineConfig frameConfig;
    frameConfig.clock = {&ReadClock, 1'000, &clock};
    frameConfig.pacing = engine::FramePacingMode::Disabled;
    Check(frames != nullptr && frames->Configure(frameConfig, &frameFailure) && frames->Compile(&frameFailure),
          "ordinary engine frame pipeline compilation");

    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_texture_service_tests");
    const filesystem::AbsolutePath loosePath = directory.AddFilePath("runtime_loose.vtex");
    const filesystem::AbsolutePath looseTemporaryPath = directory.AddFilePath("runtime_loose.vtex.tmp");
    const filesystem::AbsolutePath packagePath = directory.AddFilePath("runtime_texture.vpak");
    const filesystem::AbsolutePath packageTemporaryPath = directory.AddFilePath("runtime_texture.vpak.tmp");
    filesystem::Manager& files = filesystem::GetManager();
    DeleteDirectoryTree(files, directory);
    Check(files.CreatePath(directory), "fresh texture cook integration directory");

    Check(textureTools::Initialize(), "texture tools initialization");
    const textureTools::TextureCookingProfile runtimeProofProfile{
        RuntimeProofProfile,
        1,
        textures::PixelFormat::R8G8B8A8UNorm,
        textures::ColorSpace::SRgb,
        textureTools::TextureCookingFlags::GenerateFullMipChain | textureTools::TextureCookingFlags::Streamable,
        4,
        3,
        0.5f,
        0};
    Check(textureTools::RegisterCookingProfile(runtimeProofProfile) == textureTools::ProfileRegistrationResult::Success,
          "register deterministic uncompressed streamable texture proof profile");
    assets::BuildSystem buildSystem;
    assets::Config buildConfig;
    buildConfig.persistentCacheRoot = directory.AsChar();
    Check(buildSystem.Initialize(buildConfig), "persistent texture build-system initialization");
    textureTools::TextureAssetCompiler textureCompiler;
    Check(textureCompiler.Initialize() && textureCompiler.Register(buildSystem) == assets::Result::Success,
          "registered VTSR-to-VTEX compiler");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = directory.AsChar();
    indexConfig.settingsFingerprint = vanguard::crypto::Sha256("texture-service-index-v1", 24);
    assets::DependencyIndex dependencyIndex;
    Check(dependencyIndex.Initialize(indexConfig) == assets::IndexResult::Success, "texture dependency index initialization");

    ByteArray jpegBytes(vanguard::memory::pools::Assets::GetInstance());
    const filesystem::AbsolutePath jpegPath =
        root.AddDirPath("source").AddDirPath("textureTools").AddDirPath("tests").AddDirPath("data").AddFilePath("libjpeg_rgb.jpg");
    Check(filesystem::LoadFileToBuffer(jpegPath, jpegBytes), "load the real encoded JPEG source fixture");
    const resources::ResourceReference jpegSource(resources::ResourcePath::FromString("tests/textures/runtime_source.jpg"),
                                                    textureTools::TextureSourceResourceType);
    const resources::ResourceReference jpegOutput(resources::ResourcePath::FromString("textures/runtime_source.vtex"),
                                                    textures::TextureResourceType);
    textureTools::TextureBuildDescription jpegDescription;
    jpegDescription.sourceMode = textureTools::TextureBuildSourceMode::Image2D;
    jpegDescription.colorSpace = textureTools::ImportedColorSpace::Automatic;
    jpegDescription.profile = RuntimeProofProfile;
    ByteArray jpegSettings(vanguard::memory::pools::Assets::GetInstance());
    Check(textureTools::EncodeTextureBuildSettings(jpegDescription, jpegSettings) == textureTools::TextureBuildSettingsResult::Success,
          "encode canonical JPEG texture settings");
    const assets::BuildRequest jpegRequest{{jpegSource, {jpegBytes.TypedData(), jpegBytes.Size()}, {}}, jpegOutput,
                                            assets::TargetPlatform::WindowsD3D12, jpegSettings};
    assets::BuildOutput jpegOutputBuild;
    Check(BuildAndPublish(buildSystem, dependencyIndex, jpegRequest, jpegOutputBuild) &&
              jpegOutputBuild.disposition == assets::BuildDisposition::Built,
          "real JPEG source compiles and publishes through BuildSystem/DDC/index");
    assets::BuildPlan jpegCachedPlan;
    assets::BuildOutput jpegCachedBuild;
    Check(buildSystem.Prepare(jpegRequest, jpegCachedPlan) == assets::Result::Success &&
              buildSystem.Execute(jpegRequest, jpegCachedPlan, jpegCachedBuild) == assets::Result::Success &&
              jpegCachedBuild.disposition == assets::BuildDisposition::CacheHit &&
              jpegCachedBuild.buildFingerprint == jpegOutputBuild.buildFingerprint,
          "identical JPEG request hits the generic texture build cache");
    const vanguard::crypto::Digest256 jpegSourceFingerprint =
        vanguard::crypto::Sha256(jpegBytes.TypedData(), jpegBytes.Size());

    textureTools::TextureBuildDescription alternateJpegDescription = jpegDescription;
    alternateJpegDescription.profile = textureTools::profiles::Ui;
    ByteArray alternateJpegSettings(vanguard::memory::pools::Assets::GetInstance());
    Check(textureTools::EncodeTextureBuildSettings(alternateJpegDescription, alternateJpegSettings) ==
              textureTools::TextureBuildSettingsResult::Success,
          "encode alternate valid JPEG profile");
    const assets::BuildRequest alternateJpegRequest{{jpegSource, {jpegBytes.TypedData(), jpegBytes.Size()}, {}}, jpegOutput,
                                                     assets::TargetPlatform::WindowsD3D12, alternateJpegSettings};
    assets::BuildOutput alternateJpegBuild;
    Check(buildSystem.Build(alternateJpegRequest, alternateJpegBuild) == assets::Result::Success &&
              alternateJpegBuild.disposition == assets::BuildDisposition::Built &&
              !(alternateJpegBuild.buildFingerprint == jpegOutputBuild.buildFingerprint),
          "texture profile change rebuilds under a different build identity");

    constexpr const char* RuntimeTexturePath = "textures/runtime_source.vtex";
    const resources::ResourceReference runtimeTexture = jpegOutput;
    Check(!jpegOutputBuild.artifacts.Empty() && dependencyIndex.Save() == assets::IndexResult::Success,
          "streamable JPEG cook publishes its indexed metadata and mip artifacts");

    assets::DependencyRecord runtimeRecord;
    assets::DerivedDataArtifactSource artifactSource;
    Check(dependencyIndex.Find(runtimeTexture, runtimeRecord) == assets::IndexResult::Success &&
              artifactSource.Initialize({directory.AsChar(), {}}),
          "open the indexed real-JPEG artifact set through the generic DDC source");
    assets::LooseResourceMaterializer materializer;
    Check(materializer.Materialize(runtimeRecord, runtimeTexture, artifactSource, loosePath, looseTemporaryPath) ==
                  assets::LooseMaterializationResult::Success &&
              !files.FileExist(looseTemporaryPath),
          "materialize the indexed JPEG artifacts as one loose VTEX");

    assets::PackageManifest manifest;
    manifest.packageId = 0x5458545356435003ull;
    Check(manifest.AddRoot({runtimeTexture, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success,
          "select the same indexed VTEX as the package root");
    assets::PackagePlanner packagePlanner;
    assets::PackageBuildPlan packagePlan;
    Check(packagePlanner.Prepare(manifest, dependencyIndex, packagePlan) == assets::PackagingResult::Success,
          "prepare VPAK directly from the dependency index");
    PackagePathContext packagePathContext{runtimeTexture, RuntimeTexturePath};
    assets::DerivedDataPackageArtifactReader artifactReader(artifactSource);
    const assets::PackageAssemblyCallbacks packageCallbacks{&ResolvePackagePath, &packagePathContext,
                                                             &assets::DerivedDataPackageArtifactReader::ReadCallback, &artifactReader};
    assets::PackageAssembler packageAssembler;
    Check(packageAssembler.Publish(packagePlan, packagePath, packageTemporaryPath, packageCallbacks) == assets::PackagingResult::Success &&
              !files.FileExist(packageTemporaryPath),
          "assemble and safely publish VPAK from the same immutable JPEG artifact set");
    artifactReader.Reset();

    streaming::ResourceStreamer& streamer = streamingService->GetStreamer();
    Check(streamer.RegisterLoose({runtimeTexture, loosePath, {}, 0, 0}), "materialized loose VTEX generation registration");
    Check(ExerciseTexture(runtimeTexture, streamer, *frames, clock, *renderingService, jpegSourceFingerprint),
          "real-JPEG loose VTEX reaches bindless-ready state with exact physical mips and GPU Scene record, then retires");
    Check(streamer.UnregisterLoose(runtimeTexture.GetPath()), "loose VTEX generation removal");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "loose VTEX resource pipeline drain");

    auto packageFile = filesystem::RawFileReader::Create(packagePath);
    packages::PackageReader mountedPackage;
    Check(packageFile && mountedPackage.Open(*packageFile) == packages::Result::Success, "packaged VTEX index open");
    packageFile.Reset();
    Check(streamer.MountPackage(mountedPackage, packagePath, 0), "packaged VTEX mount");
    Check(ExerciseTexture(runtimeTexture, streamer, *frames, clock, *renderingService, jpegSourceFingerprint),
          "the same real-JPEG VTEX reaches bindless-ready state from VPAK with exact GPU proof, then retires");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "packaged VTEX resource pipeline drain");
    Check(streamer.UnmountPackage(mountedPackage), "packaged VTEX unmount");
    mountedPackage.Close();

    Check(renderingService->GetTextureResidency().GetStats().residencyRecords == 0 &&
              renderingService->GetTextureResidency().GetStats().liveDemands == 0,
          "texture runtime has no live residency or demand at shutdown");
    Check(artifactSource.Shutdown(), "derived-data artifact source shutdown");
    Check(dependencyIndex.Shutdown(), "texture dependency index shutdown");
    Check(textureCompiler.Shutdown(), "texture compiler unregistration and shutdown");
    Check(buildSystem.Shutdown(), "texture BuildSystem shutdown");
    DeleteDirectoryTree(files, directory);
    Check(host.Shutdown(&hostFailure), "engine graph and device shutdown");

    vanguard::diagnostics::Shutdown();

    if (g_failures == 0)
        std::printf("[textureResidencyServiceTests] source/DDC/loose/package/GPU texture integration passed.\n");
    return g_failures == 0 ? 0 : 1;
}
