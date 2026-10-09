#include "../../../tests/support/scratch_directory.hpp"
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
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/texture_resource.hpp>
#include <vanguard/textures/textures.hpp>

#include <cstring>
#include <cstdio>

#if defined(_WIN32)
#include <Windows.h>
#endif

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
    inline constexpr vanguard::u64 RetainedReplacementBudget = 256ull * 1024ull;
    inline constexpr const char* UndeliveredGenerationRetirementMode = "--undelivered-generation-retirement-shutdown";
    inline constexpr vanguard::u32 UndeliveredGenerationRetirementSuccessExit = 42u;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[textureResidencyServiceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    [[nodiscard]] engine::RenderingServiceConfig MakeRenderingConfig() noexcept
    {
        engine::RenderingServiceConfig renderingConfig;
        renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
        renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
        renderingConfig.device.enableValidation = true;
        renderingConfig.resourceDescriptors.capacity = 128;
        renderingConfig.samplerDescriptors.capacity = 16;
        renderingConfig.frameResources.descriptorCacheTarget = 32;
        renderingConfig.frameResources.descriptorCacheMaximum = 64;
        renderingConfig.maximumMaterialProgramLayouts = 8;
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
        renderingConfig.textureResidency.residency.maximumPendingInstallations = 1;
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
        renderingConfig.textureResidency.maximumMipTransitionsPerTick = 4;
        renderingConfig.textureResidency.mipDemotionHysteresisTicks = 3;
        renderingConfig.textureResidency.maximumRetainedReplacementCandidateGpuBytes = RetainedReplacementBudget;
        renderingConfig.materialResources.maximumProviders = 4;
        renderingConfig.materialResources.maximumFallbacks = 4;
        renderingConfig.materialResources.maximumOperations = 8;
        renderingConfig.materialResources.maximumDescriptorCacheEntries = 8;
        renderingConfig.materialMaterializer.maximumOperations = 8;
        renderingConfig.materialMaterializer.maximumRolesPerMaterial = 8;
        renderingConfig.materialMaterializer.maximumMaterialsPerBatch = 4;
        renderingConfig.materialMaterializer.maximumOperationsProgressedPerUpdate = 4;
        renderingConfig.materialMaterializer.maximumResourceRolePollsPerUpdate = 16;
        renderingConfig.materialResidency.maximumResidencies = 4;
        renderingConfig.materialResidency.maximumDemands = 8;
        renderingConfig.materialResidency.maximumTechniqueRequests = 4;
        renderingConfig.materialResidency.maximumNativePrograms = 4;
        renderingConfig.materialResidency.maximumResidencyChecksPerUpdate = 4;
        renderingConfig.materialResidency.maximumRetirementsPerSeal = 4;
        renderingConfig.materialBindings.maximumBindings = 4;
        renderingConfig.materialBindings.maximumChecksPerUpdate = 4;
        return renderingConfig;
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

    [[nodiscard]] bool ResolvePackagePath(const resources::ResourceReference resource, char* const destination, const vanguard::usize capacity, vanguard::usize& written, void* const userData) noexcept
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

    [[nodiscard]] bool BuildAndPublish(assets::BuildSystem& system, assets::DependencyIndex& index, const assets::BuildRequest& request, assets::BuildOutput& output) noexcept
    {
        assets::BuildPlan plan;
        return system.Prepare(request, plan) == assets::Result::Success && system.Execute(request, plan, output) == assets::Result::Success &&
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

    [[nodiscard]] bool WriteStandaloneTextureFixture(const filesystem::AbsolutePath& path, const vanguard::u8 seed) noexcept
    {
        vanguard::u8 pixels[64]{};
        for (vanguard::u32 index = 0; index < 64; ++index)
            pixels[index] = static_cast<vanguard::u8>(seed + index * 13u);
        const textures::SubresourceBuildRecord subresource{0, 0, 0, pixels, sizeof(pixels), 16, 64};
        textures::BuildDescription description;
        description.dimension = textures::TextureDimension::Texture2D;
        description.format = textures::PixelFormat::R8G8B8A8UNorm;
        description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
        description.width = 4;
        description.height = 4;
        description.depth = 1;
        description.arrayLayers = 1;
        description.mipCount = 1;
        description.mipTailFirstLevel = 0;
        const char* const fingerprintText = seed == 17u ? "undelivered-retirement-a" : "undelivered-retirement-b";
        description.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        description.subresources = {&subresource, 1};

        ByteArray bytes(vanguard::memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter memoryWriter(bytes);
        if (textures::WriteTexture(memoryWriter, description) != textures::Result::Success)
            return false;
        auto writer = filesystem::GetManager().CreateFileWriter(path, filesystem::FOF_Buffered);
        if (!writer)
            return false;
        writer->Serialize(bytes.TypedData(), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
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
        return rhi::WaitForGpuFence({rhi::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) && rhi::WaitForGpuFence({rhi::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
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

    [[nodiscard]] bool DriveUntilReady(engine::FramePipelineService& frames, TestClock& clock, rendering::TextureResidencyRuntime& runtime, const rendering::GpuTextureResidencyHandle residency) noexcept
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

    [[nodiscard]] bool DriveUntilQuality(engine::FramePipelineService& frames, TestClock& clock,
                                         rendering::TextureResidencyRuntime& runtime,
                                         const rendering::GpuTextureResidencyHandle residency,
                                         const vanguard::u32 expectedFirstResidentMip) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!RunFrame(frames, clock))
            {
                std::fprintf(stderr, "[textureResidencyServiceTests] quality drive frame failed: device=%u\n",
                             static_cast<unsigned>(rhi::TestDeviceState()));
                return false;
            }
            if (!runtime.GetInfo(residency, info, &failure))
            {
                std::fprintf(stderr, "[textureResidencyServiceTests] quality drive info failed: code=%u message=%s device=%u\n",
                             static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "<none>",
                             static_cast<unsigned>(rhi::TestDeviceState()));
                return false;
            }
            if (info.state == rendering::TextureRuntimeState::BindlessReady &&
                info.currentFirstResidentMip == expectedFirstResidentMip &&
                info.desiredFirstResidentMip == expectedFirstResidentMip &&
                info.qualityState == rendering::TextureQualityState::Stable &&
                info.qualitySatisfied)
                return true;
            if (info.state == rendering::TextureRuntimeState::Failed ||
                info.qualityState == rendering::TextureQualityState::Failed)
            {
                std::fprintf(stderr,
                             "[textureResidencyServiceTests] quality drive failed: state=%u quality=%u current=%u desired=%u target=%u retry=%u failure=%u message=%s\n",
                             static_cast<unsigned>(info.state), static_cast<unsigned>(info.qualityState),
                             info.currentFirstResidentMip, info.desiredFirstResidentMip,
                             info.qualityTargetFirstResidentMip, info.qualityRetryRequired ? 1u : 0u,
                             static_cast<unsigned>(info.qualityFailure.code),
                             info.qualityFailure.message != nullptr ? info.qualityFailure.message : "<none>");
                return false;
            }
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        if (runtime.GetInfo(residency, info, &failure))
            std::fprintf(stderr,
                         "[textureResidencyServiceTests] quality drive timed out: state=%u quality=%u current=%u desired=%u target=%u retry=%u failure=%u message=%s\n",
                         static_cast<unsigned>(info.state), static_cast<unsigned>(info.qualityState),
                         info.currentFirstResidentMip, info.desiredFirstResidentMip,
                         info.qualityTargetFirstResidentMip, info.qualityRetryRequired ? 1u : 0u,
                         static_cast<unsigned>(info.qualityFailure.code),
                         info.qualityFailure.message != nullptr ? info.qualityFailure.message : "<none>");
        return false;
    }

    [[nodiscard]] bool DriveUntilReplacementCurrent(engine::FramePipelineService& frames, TestClock& clock,
                                                    rendering::TextureResidencyRuntime& runtime,
                                                    const rendering::GpuTextureResidencyHandle residency,
                                                    const vanguard::u32 expectedGeneration) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!RunFrame(frames, clock) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.state == rendering::TextureRuntimeState::BindlessReady && info.resourceGeneration == expectedGeneration &&
                info.replacementState == rendering::TextureReplacementState::None && !info.replacementRetryRequired)
                return true;
            if (info.replacementState == rendering::TextureReplacementState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilInstallationPending(rendering::TextureResidencyRuntime& runtime,
                                                     const rendering::GpuTextureResidencyHandle residency) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!runtime.Tick(&failure) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.state == rendering::TextureRuntimeState::InstallationPending)
                return true;
            if (info.state == rendering::TextureRuntimeState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilReplacementFailed(rendering::TextureResidencyRuntime& runtime,
                                                   const rendering::GpuTextureResidencyHandle residency,
                                                   const vanguard::u32 expectedReplacementGeneration) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!runtime.Tick(&failure) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.replacementState == rendering::TextureReplacementState::Failed)
                return info.state == rendering::TextureRuntimeState::BindlessReady && info.replacementRetryRequired &&
                       info.replacementResourceGeneration == expectedReplacementGeneration;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool VerifyPhysicalMip(const textures::TextureResourceObject& resource, const rendering::TextureResidencyInfo& residency, const vanguard::u32 assetMip) noexcept
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
        if (record.depth != 1 || record.rowPitch == 0 || record.slicePitch == 0 || record.slicePitch % record.rowPitch != 0 || record.byteSize != record.slicePitch)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u has unsupported proof layout: %ux%u row=%u slice=%u bytes=%llu depth=%u\n", assetMip, record.width, record.height, record.rowPitch,
                         record.slicePitch, static_cast<unsigned long long>(record.byteSize), record.depth);
            return false;
        }

        textures::TextureSubresourceReadRequest expectedRead;
        if (resource.GetSubresourceSource().ReadSubresourceAsync(metadata, subresource, expectedRead) != textures::Result::Success || !expectedRead.TryWait(10'000) ||
            expectedRead.GetResult() != textures::Result::Success || expectedRead.GetBytes().Count() != record.byteSize)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u cooked-byte read failed\n", assetMip);
            return false;
        }

        rhi::Failure failure;
        rhi::CommandListRef commands = rhi::CreateCommandList(rhi::CommandListType::Default, 0x5458545244424b31ull, &failure);
        if (!commands)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback command-list creation failed: %s\n", assetMip, failure.message != nullptr ? failure.message : "unknown");
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
            readback = rhi::TextureReadback(rhi::AdoptReference, rhi::RequestTextureReadback(residency.texture, {{physicalMip, 0}}, &failure));
        recorded = recorded && readback.IsValid() && rhi::TransitionTexture(residency.texture, rhi::ResourceState::CopySource, shaderRead, range, &failure);
        rhi::UnbindCommandList();
        if (!recorded)
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback recording failed: %s\n", assetMip, failure.message != nullptr ? failure.message : "unknown");
            rhi::DiscardCommandList(commands);
            return false;
        }

        const rhi::CommandListRef submissions[]{commands};
        rhi::GpuFence completion;
        if (!rhi::CloseAndSubmitCommandLists("texture residency physical mip proof", {submissions, 1}, rhi::CommandListSyncType::None, completion, &failure) ||
            !rhi::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || !rhi::RetireResources(&failure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback submission failed: %s\n", assetMip, failure.message != nullptr ? failure.message : "unknown");
            return false;
        }

        rhi::TextureReadbackInfo readbackInfo;
        rhi::TextureReadbackMapping mapping;
        if (!rhi::GetTextureReadbackInfo(readback.GetRef(), readbackInfo, &failure) || readbackInfo.state != rhi::TextureReadbackState::Ready || readbackInfo.extent.width != record.width ||
            readbackInfo.extent.height != record.height || readbackInfo.extent.depth != 1 || !rhi::MapTextureReadback(readback.GetRef(), mapping, &failure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback map failed: extent=%ux%ux%u expected=%ux%u state=%u message=%s\n", assetMip, readbackInfo.extent.width,
                         readbackInfo.extent.height, readbackInfo.extent.depth, record.width, record.height, static_cast<unsigned>(readbackInfo.state), failure.message != nullptr ? failure.message : "unknown");
            return false;
        }

        const auto expected = expectedRead.GetBytes();
        const auto* const actualBytes = static_cast<const vanguard::u8*>(mapping.data);
        const vanguard::u32 rows = record.slicePitch / record.rowPitch;
        bool matches = mapping.rowPitch >= record.rowPitch && mapping.depthPitch >= record.slicePitch && mapping.dataSize >= mapping.depthPitch;
        if (matches)
            for (vanguard::u32 row = 0; row < rows; ++row)
                matches = matches && std::memcmp(actualBytes + static_cast<vanguard::u64>(row) * mapping.rowPitch, expected.Data() + static_cast<vanguard::u64>(row) * record.rowPitch, record.rowPitch) == 0;
        if (!matches)
            std::fprintf(stderr, "[textureResidencyServiceTests] mip %u readback mismatch: rows=%u gpuRow=%llu gpuDepth=%llu gpuBytes=%llu cookedRow=%u cookedSlice=%u\n", assetMip, rows,
                         static_cast<unsigned long long>(mapping.rowPitch), static_cast<unsigned long long>(mapping.depthPitch), static_cast<unsigned long long>(mapping.dataSize), record.rowPitch,
                         record.slicePitch);
        const bool unmapped = rhi::UnmapTextureReadback(readback.GetRef(), &failure);
        return unmapped && matches;
    }

    [[nodiscard]] bool VerifyGpuTextureRecord(const rendering::GpuSceneRuntime& gpuScene, const rendering::TextureResidencyInfo& residency) noexcept
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
                              rhi::CopyBuffer(readback.GetRef(), 0, address.buffer, address.byteOffset, sizeof(rendering::GpuTextureResidency), &failure) &&
                              rhi::TransitionBuffer(address.buffer, rhi::ResourceState::Unknown, shaderRead, &failure);
        rhi::UnbindCommandList();
        if (!recorded)
        {
            rhi::DiscardCommandList(commands);
            return false;
        }

        const rhi::CommandListRef submissions[]{commands};
        rhi::GpuFence completion;
        if (!rhi::CloseAndSubmitCommandLists("texture residency GPU Scene record proof", {submissions, 1}, rhi::CommandListSyncType::None, completion, &failure) ||
            !rhi::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return false;

        const auto* const uploaded = static_cast<const rendering::GpuTextureResidency*>(rhi::LockBuffer(readback.GetRef(), 0, sizeof(rendering::GpuTextureResidency), &failure));
        if (uploaded == nullptr)
            return false;
        const rendering::GpuTextureResidency copy = *uploaded;
        rhi::UnlockBuffer(readback.GetRef());
        const vanguard::u32 generation = copy.generationAndFlags & rendering::GpuTextureResidencyGenerationMask;
        const vanguard::u32 flags = copy.generationAndFlags >> rendering::GpuTextureResidencyFlagsShift;
        return copy.descriptor == residency.descriptor.GpuIndex() && copy.firstResidentMip == residency.firstResidentMip && copy.residentMipCount == residency.residentMipCount &&
               generation == (residency.handle.generation & rendering::GpuTextureResidencyGenerationMask) && flags == static_cast<vanguard::u32>(rendering::GpuTextureResidencyFlags::BindlessReady);
    }

    [[nodiscard]] bool VerifyTextureInstallation(const textures::TextureResourceObject& resource, engine::RenderingService& service, const rendering::GpuTextureResidencyHandle handle,
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
            std::fprintf(stderr, "[textureResidencyServiceTests] proof GPU idle wait failed: %s\n", rhiFailure.message != nullptr ? rhiFailure.message : "unknown");
            return false;
        }

        rendering::TextureResidencyInfo residency;
        rendering::TextureResidencyFailure residencyFailure;
        if (!service.GetTextureResidency().GetResidencyManager().GetInfo(handle, residency, &residencyFailure) || residency.handle != handle ||
            residency.state != rendering::TextureResidencyState::BindlessReady || !residency.texture || !residency.descriptor ||
            !(residency.contentFingerprint == resource.GetMetadata().GetContentFingerprint()) || residency.residentMipCount == 0 ||
            residency.firstResidentMip + residency.residentMipCount > resource.GetMetadata().GetMipCount())
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] physical residency proof failed: state=%u first=%u count=%u total=%u message=%s\n", static_cast<unsigned>(residency.state),
                         residency.firstResidentMip, residency.residentMipCount, resource.GetMetadata().GetMipCount(), residencyFailure.message != nullptr ? residencyFailure.message : "");
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

    [[nodiscard]] bool RetireTexture(engine::FramePipelineService& frames, TestClock& clock, engine::RenderingService& service, rendering::TextureDemandHandle& demand) noexcept
    {
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        demand.Reset();
        if (!RunFrame(frames, clock))
            return false;

        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        rendering::GpuSceneLifetime& lifetime = service.GetGpuScene().GetLifetime();
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !service.SealResidencyRetirements(safeAfter, &retirementFailure) || !WaitRetirementFences(safeAfter, rhiFailure) ||
            !rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
            return false;
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        rendering::TextureResidencyRuntimeFailure textureFailure;
        static_cast<void>(lifetime.Collect(&lifetimeFailure));
        static_cast<void>(runtime.CollectRetirements(&textureFailure));
        return RunFrame(frames, clock) && runtime.GetStats().residencyRecords == 0 && runtime.GetStats().liveDemands == 0;
    }

    [[nodiscard]] bool AcknowledgeCompletedGenerationRetirements(rendering::TextureResidencyRuntime& runtime) noexcept
    {
        rendering::TextureResidencyManager& manager = runtime.GetResidencyManager();
        rendering::TextureResidencyFailure failure;
        rendering::TextureGenerationRetirement evidence[8]{};
        for (;;)
        {
            const vanguard::u32 count = manager.PeekCompletedGenerationRetirements(evidence, &failure);
            if (failure.code != rendering::TextureResidencyFailureCode::None)
                return false;
            if (count == 0)
                return runtime.GetStats().residency.pendingGenerationRetirements == 0;
            for (vanguard::u32 index = 0; index < count; ++index)
                if (!manager.AcknowledgeGenerationRetirement(evidence[index].retirementId, &failure))
                    return false;
        }
    }

    [[nodiscard]] bool VerifyAndAcknowledgeSingleGenerationRetirement(rendering::TextureResidencyRuntime& runtime,
                                                                       const resources::ResourcePath expectedPath,
                                                                       const vanguard::u32 expectedGeneration) noexcept
    {
        rendering::TextureResidencyManager& manager = runtime.GetResidencyManager();
        rendering::TextureResidencyFailure failure;
        rendering::TextureGenerationRetirement evidence[8]{};
        const vanguard::u32 count = manager.PeekCompletedGenerationRetirements(evidence, &failure);
        if (failure.code != rendering::TextureResidencyFailureCode::None || count != 1 ||
            evidence[0].resourcePath != expectedPath || evidence[0].resourceGeneration != expectedGeneration || evidence[0].retirementId == 0)
            return false;
        return manager.AcknowledgeGenerationRetirement(evidence[0].retirementId, &failure) &&
               runtime.GetStats().residency.pendingGenerationRetirements == 0;
    }

    [[nodiscard]] bool WaitForResourceDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = pipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeOperations == 0 && pipelineStats.activeJobs == 0 && pipelineStats.activePreparations == 0 && pipelineStats.externalRequests == 0 && streamingStats.activeLoads == 0 &&
                streamingStats.liveSources == 0 && streamingStats.activeReads == 0 && streamingStats.stagingBytesInUse == 0)
                return true;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool ExerciseTexture(const resources::ResourceReference reference, streaming::ResourceStreamer& streamer, engine::FramePipelineService& frames, TestClock& clock,
                                       engine::RenderingService& service, const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
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
        if (after.submittedBatches != before.submittedBatches + 1 || after.stagedContributions != before.stagedContributions + 1 || after.acceptedContributions != before.acceptedContributions + 1 ||
            after.retriedContributions != before.retriedContributions)
            return false;
        const vanguard::u32 generation = resource.GetGeneration();
        if (!RetireTexture(frames, clock, service, demand) ||
            !VerifyAndAcknowledgeSingleGenerationRetirement(runtime, reference.GetPath(), generation))
            return false;
        resource.Reset();
        request.Reset();
        return true;
    }

    [[nodiscard]] bool ExerciseTextureQualityQos(const resources::ResourceReference reference,
                                                streaming::ResourceStreamer& streamer,
                                                engine::FramePipelineService& frames, TestClock& clock,
                                                engine::RenderingService& service,
                                                const filesystem::AbsolutePath& replacementPath,
                                                const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] texture quality QoS proof failed at %s\n", step);
            return false;
        };

        resources::PipelineRequest request = streamer.Request(reference, resources::LoadPriority::High);
        if (!request.TryWait(10'000) || !request.HasLoaded())
            return FailStep("resource-load");
        resources::ResourceHandle resource = request.Acquire();
        const auto* const texture = static_cast<const textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen())
            return FailStep("resource-acquire");
        const textures::TextureFile& metadata = texture->GetMetadata();
        if (metadata.GetMipCount() < 3u || metadata.GetMipTailFirstLevel() < 2u)
            return FailStep("streamable-mip-shape");

        const vanguard::u32 coarseFirstMip = metadata.GetMipTailFirstLevel() > 2u
                                                 ? metadata.GetMipTailFirstLevel() - 2u
                                                 : metadata.GetMipTailFirstLevel() - 1u;
        constexpr vanguard::u32 DetailedFirstMip = 0u;
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        const rendering::TextureResidencyRuntimeStats before = runtime.GetStats();

        rendering::TextureDemandDesc coarse;
        coarse.firstResidentMip = coarseFirstMip;
        coarse.priority = rendering::TextureDemandPriority::Background;
        rendering::TextureDemandHandle coarseDemand;
        if (!runtime.RequestTexture(resource, coarse, coarseDemand, &failure))
            return FailStep("initial-quality-request");
        const rendering::GpuTextureResidencyHandle residency = coarseDemand.GetResidency();
        if (!residency.IsValid() || !DriveUntilQuality(frames, clock, runtime, residency, coarseFirstMip))
            return FailStep("initial-quality-ready");

        rendering::TextureRuntimeInfo coarseInfo;
        rendering::TextureResidencyInfo coarsePhysical;
        if (!runtime.GetInfo(residency, coarseInfo, &failure) ||
            !runtime.GetResidencyInfo(residency, coarsePhysical, &failure) ||
            coarseInfo.demandCount != 1u ||
            coarseInfo.demandPriority != rendering::TextureDemandPriority::Background ||
            coarseInfo.currentFirstResidentMip != coarseFirstMip ||
            coarsePhysical.firstResidentMip != coarseFirstMip ||
            coarseInfo.committedGpuBytes == 0u ||
            coarseInfo.committedGpuBytes != rhi::GetMemoryRequirements(coarsePhysical.texture).size ||
            runtime.GetStats().qualityTransitionsStarted != before.qualityTransitionsStarted ||
            runtime.GetStats().committedTextureGpuBytes != before.committedTextureGpuBytes + coarseInfo.committedGpuBytes ||
            !VerifyTextureInstallation(*texture, service, residency, expectedSourceFingerprint))
            return FailStep("direct-initial-target");
        const vanguard::u64 coarseBytes = coarseInfo.committedGpuBytes;

        rendering::TextureDemandDesc detailed;
        detailed.firstResidentMip = DetailedFirstMip;
        detailed.priority = rendering::TextureDemandPriority::Focused;
        rendering::TextureDemandHandle detailedDemand;
        if (!runtime.RequestTexture(resource, detailed, detailedDemand, &failure) ||
            detailedDemand.GetResidency() != residency)
            return FailStep("shared-detailed-demand");
        rendering::TextureRuntimeInfo aggregated;
        if (!runtime.GetInfo(residency, aggregated, &failure) ||
            aggregated.demandCount != 2u ||
            aggregated.desiredFirstResidentMip != DetailedFirstMip ||
            aggregated.currentFirstResidentMip != coarseFirstMip ||
            aggregated.demandPriority != rendering::TextureDemandPriority::Focused ||
            aggregated.qualitySatisfied)
            return FailStep("quality-aggregation");

        detailed.priority = rendering::TextureDemandPriority::Interactive;
        if (!runtime.UpdateDemand(detailedDemand, detailed, &failure) ||
            !runtime.GetInfo(residency, aggregated, &failure) ||
            aggregated.desiredFirstResidentMip != DetailedFirstMip ||
            aggregated.demandPriority != rendering::TextureDemandPriority::Interactive)
            return FailStep("priority-independent-from-quality");

        if (!DriveUntilQuality(frames, clock, runtime, residency, DetailedFirstMip))
            return FailStep("promotion");
        rendering::TextureRuntimeInfo detailedInfo;
        rendering::TextureResidencyInfo detailedPhysical;
        const rendering::TextureResidencyRuntimeStats afterPromotion = runtime.GetStats();
        if (!runtime.GetInfo(residency, detailedInfo, &failure) ||
            !runtime.GetResidencyInfo(residency, detailedPhysical, &failure) ||
            detailedInfo.currentFirstResidentMip != DetailedFirstMip ||
            detailedPhysical.firstResidentMip != DetailedFirstMip ||
            detailedInfo.committedGpuBytes <= coarseBytes ||
            detailedInfo.committedGpuBytes != rhi::GetMemoryRequirements(detailedPhysical.texture).size ||
            afterPromotion.committedTextureGpuBytes != before.committedTextureGpuBytes + detailedInfo.committedGpuBytes ||
            afterPromotion.qualityTransitionsStarted != before.qualityTransitionsStarted + 1u)
            return FailStep("promotion-accounting");

        detailedDemand.Reset();
        if (!runtime.GetInfo(residency, aggregated, &failure) ||
            aggregated.demandCount != 1u ||
            aggregated.desiredFirstResidentMip != coarseFirstMip ||
            aggregated.currentFirstResidentMip != DetailedFirstMip ||
            aggregated.qualitySatisfied)
            return FailStep("demotion-demand");
        if (!RunFrame(frames, clock) || !runtime.GetInfo(residency, aggregated, &failure) ||
            aggregated.currentFirstResidentMip != DetailedFirstMip ||
            aggregated.qualityState != rendering::TextureQualityState::Stable)
            return FailStep("demotion-hysteresis");
        if (!DriveUntilQuality(frames, clock, runtime, residency, coarseFirstMip))
            return FailStep("demotion");
        const rendering::TextureResidencyRuntimeStats afterDemotion = runtime.GetStats();
        if (!runtime.GetInfo(residency, coarseInfo, &failure) ||
            coarseInfo.committedGpuBytes != coarseBytes ||
            afterDemotion.committedTextureGpuBytes != before.committedTextureGpuBytes + coarseBytes ||
            afterDemotion.qualityTransitionsStarted != before.qualityTransitionsStarted + 2u)
            return FailStep("demotion-accounting");

        const vanguard::u32 oldGeneration = resource.GetGeneration();
        const vanguard::u64 transitionsBeforeReplacement = afterDemotion.qualityTransitionsStarted;
        if (!streamer.PublishLoose({reference, replacementPath, {}, 0, 0}))
            return FailStep("replacement-publish");
        resources::PipelineRequest replacementRequest = streamer.Replace(reference, resources::LoadPriority::High);
        if (!replacementRequest.IsValid() || !replacementRequest.TryWait(10'000) || !replacementRequest.HasLoaded())
            return FailStep("replacement-load");
        resources::ResourceHandle replacementResource = replacementRequest.Acquire();
        const auto* const replacementTexture =
            static_cast<const textures::TextureResourceObject*>(replacementResource.Get());
        if (replacementTexture == nullptr || !replacementTexture->IsOpen() ||
            replacementResource.GetGeneration() == oldGeneration ||
            replacementTexture->GetMetadata().GetMipCount() != metadata.GetMipCount() ||
            replacementTexture->GetMetadata().GetMipTailFirstLevel() != metadata.GetMipTailFirstLevel())
            return FailStep("replacement-shape");
        if (!runtime.ReplaceTexture(replacementResource, residency, &failure) ||
            !DriveUntilReplacementCurrent(frames, clock, runtime, residency, replacementResource.GetGeneration()))
            return FailStep("replacement-cutover");

        rendering::TextureRuntimeInfo replaced;
        rendering::TextureResidencyInfo replacedPhysical;
        if (!runtime.GetInfo(residency, replaced, &failure) ||
            !runtime.GetResidencyInfo(residency, replacedPhysical, &failure) ||
            replaced.residency != residency ||
            replaced.resourceGeneration != replacementResource.GetGeneration() ||
            replaced.currentFirstResidentMip != coarseFirstMip ||
            replaced.desiredFirstResidentMip != coarseFirstMip ||
            !replaced.qualitySatisfied ||
            replaced.committedGpuBytes != coarseBytes ||
            replacedPhysical.firstResidentMip != coarseFirstMip ||
            replacedPhysical.source.GetGeneration() != replacementResource.GetGeneration() ||
            runtime.GetStats().qualityTransitionsStarted != transitionsBeforeReplacement ||
            runtime.GetStats().committedTextureGpuBytes != before.committedTextureGpuBytes + coarseBytes ||
            !VerifyTextureInstallation(*replacementTexture, service, residency, expectedSourceFingerprint))
            return FailStep("replacement-quality-target");

        request.Reset();
        resource.Reset();
        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) ||
            !service.SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure) ||
            !rhi::RetireResources(&rhiFailure) ||
            !rhi::FlushRetiredResources(&rhiFailure) ||
            !VerifyAndAcknowledgeSingleGenerationRetirement(runtime, reference.GetPath(), oldGeneration))
            return FailStep("replacement-old-generation-retirement");

        const vanguard::u32 currentGeneration = replacementResource.GetGeneration();
        replacementResource.Reset();
        replacementRequest.Reset();
        if (!RetireTexture(frames, clock, service, coarseDemand) ||
            !VerifyAndAcknowledgeSingleGenerationRetirement(runtime, reference.GetPath(), currentGeneration) ||
            runtime.GetStats().committedTextureGpuBytes != before.committedTextureGpuBytes)
            return FailStep("final-retirement");
        return true;
    }

    [[nodiscard]] bool ExerciseTextureQualityRapidRetarget(const resources::ResourceReference reference,
                                                          streaming::ResourceStreamer& streamer,
                                                          engine::FramePipelineService& frames, TestClock& clock,
                                                          engine::RenderingService& service) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] rapid BC7 quality retarget proof failed at %s\n", step);
            return false;
        };
        const auto DeviceOperational = []() noexcept
        {
            if (rhi::TestDeviceState() == rhi::DeviceState::Operational)
                return true;
            rhi::Failure idleFailure;
            const bool idle = rhi::WaitIdle(&idleFailure);
            std::fprintf(stderr,
                         "[textureResidencyServiceTests] D3D12 device loss detail: state=%u waitIdle=%u code=%u backend=0x%016llx message=%s\n",
                         static_cast<unsigned>(rhi::TestDeviceState()), idle ? 1u : 0u,
                         static_cast<unsigned>(idleFailure.code),
                         static_cast<unsigned long long>(idleFailure.backendCode),
                         idleFailure.message[0] != '\0' ? idleFailure.message : "<none>");
            return false;
        };

        resources::PipelineRequest request = streamer.Request(reference, resources::LoadPriority::High);
        if (!request.TryWait(10'000) || !request.HasLoaded())
            return FailStep("resource-load");
        resources::ResourceHandle resource = request.Acquire();
        const auto* const texture = static_cast<const textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen())
            return FailStep("resource-acquire");
        const textures::TextureFile& metadata = texture->GetMetadata();
        if (metadata.Format() != textures::PixelFormat::BC7UNorm || metadata.GetSpace() != textures::ColorSpace::SRgb ||
            metadata.GetMipCount() < 3u || metadata.GetMipTailFirstLevel() < 2u)
            return FailStep("bc7-streamable-shape");

        constexpr vanguard::u32 DetailedFirstMip = 0u;
        constexpr vanguard::u32 CoarseFirstMip = 1u;
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureDemandDesc demandDesc;
        demandDesc.firstResidentMip = CoarseFirstMip;
        demandDesc.priority = rendering::TextureDemandPriority::Background;
        rendering::TextureDemandHandle demand;
        if (!runtime.RequestTexture(resource, demandDesc, demand, &failure))
            return FailStep("coarse-request");
        const rendering::GpuTextureResidencyHandle residency = demand.GetResidency();
        if (!residency.IsValid() || !DriveUntilQuality(frames, clock, runtime, residency, CoarseFirstMip))
            return FailStep("coarse-ready");

        // First prove the exact Project Cardinal slider threshold in isolation:
        // Grid is normally mip 1 around its default size and requests mip 0 when
        // enlarged past the 125px boundary. This one promotion performs the BC7
        // shared-suffix copy without any retarget cancellation in the picture.
        demandDesc.firstResidentMip = DetailedFirstMip;
        if (!runtime.UpdateDemand(demand, demandDesc, &failure) ||
            !DriveUntilQuality(frames, clock, runtime, residency, DetailedFirstMip))
            return FailStep("single-promotion");
        if (!DeviceOperational())
            return FailStep("single-promotion-device-state");

        demandDesc.firstResidentMip = CoarseFirstMip;
        if (!runtime.UpdateDemand(demand, demandDesc, &failure) ||
            !DriveUntilQuality(frames, clock, runtime, residency, CoarseFirstMip))
            return FailStep("single-demotion");
        if (!DeviceOperational())
            return FailStep("single-demotion-device-state");

        // Mirror a live grid-size drag: cross multiple mip thresholds on consecutive
        // frames instead of waiting for each physical transition to settle. The first
        // frame necessarily leaves a newly issued transition in flight because the
        // uploader cannot poll a fence that was submitted later in that same Tick.
        const vanguard::u32 targets[]{DetailedFirstMip, CoarseFirstMip, DetailedFirstMip,
                                     CoarseFirstMip, DetailedFirstMip};
        constexpr vanguard::u32 TargetCount = static_cast<vanguard::u32>(sizeof(targets) / sizeof(targets[0]));
        for (vanguard::u32 index = 0u; index < TargetCount; ++index)
        {
            demandDesc.firstResidentMip = targets[index];
            demandDesc.priority = rendering::TextureDemandPriority::Background;
            if (!runtime.UpdateDemand(demand, demandDesc, &failure))
                return FailStep("retarget-update");
            if (!RunFrame(frames, clock))
                return FailStep("retarget-frame");
            if (!DeviceOperational())
                return FailStep("retarget-device-state");
            rendering::TextureRuntimeInfo info;
            if (!runtime.GetInfo(residency, info, &failure) || info.desiredFirstResidentMip != targets[index])
                return FailStep("retarget-runtime-state");
        }

        if (!DriveUntilQuality(frames, clock, runtime, residency, DetailedFirstMip) ||
            !DeviceOperational())
            return FailStep("final-detailed-quality");

        rendering::TextureResidencyInfo physical;
        if (!runtime.GetResidencyInfo(residency, physical, &failure) ||
            physical.firstResidentMip != DetailedFirstMip || !physical.texture.IsValid())
            return FailStep("final-physical-quality");

        const vanguard::u32 generation = resource.GetGeneration();
        request.Reset();
        resource.Reset();
        if (!RetireTexture(frames, clock, service, demand) ||
            !VerifyAndAcknowledgeSingleGenerationRetirement(runtime, reference.GetPath(), generation))
            return FailStep("retirement");
        return true;
    }

    [[nodiscard]] bool ExerciseTextureReplacement(const resources::ResourceReference reference, streaming::ResourceStreamer& streamer,
                                                  engine::FramePipelineService& frames, TestClock& clock,
                                                  engine::RenderingService& service,
                                                  const filesystem::AbsolutePath& replacementPath,
                                                  const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] replacement retirement proof failed at %s\n", step);
            return false;
        };
        resources::PipelineRequest initialRequest = streamer.Request(reference, resources::LoadPriority::High);
        if (!initialRequest.TryWait(10'000) || !initialRequest.HasLoaded())
            return false;
        resources::ResourceHandle oldResource = initialRequest.Acquire();
        const auto* const oldTexture = static_cast<const textures::TextureResourceObject*>(oldResource.Get());
        if (oldTexture == nullptr || !oldTexture->IsOpen())
            return false;

        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureDemandHandle demand;
        if (!runtime.RequestTexture(oldResource, demand, &failure) || !DriveUntilReady(frames, clock, runtime, demand.GetResidency()) ||
            !VerifyTextureInstallation(*oldTexture, service, demand.GetResidency(), expectedSourceFingerprint))
            return false;

        const rendering::GpuTextureResidencyHandle residency = demand.GetResidency();
        const vanguard::u32 oldGeneration = oldResource.GetGeneration();
        if (!streamer.PublishLoose({reference, replacementPath, {}, 0, 0}))
            return false;

        resources::PipelineRequest replacementRequest = streamer.Replace(reference, resources::LoadPriority::High);
        if (!replacementRequest.IsValid() || !replacementRequest.TryWait(10'000) || !replacementRequest.HasLoaded())
            return false;
        resources::ResourceHandle replacementResource = replacementRequest.Acquire();
        const auto* const replacementTexture = static_cast<const textures::TextureResourceObject*>(replacementResource.Get());
        if (replacementTexture == nullptr || !replacementTexture->IsOpen() || replacementResource.GetGeneration() == oldGeneration || !oldResource.IsValid())
            return false;

        const rendering::TextureResidencyRuntimeStats beforeReplacement = runtime.GetStats();
        if (!runtime.ReplaceTexture(replacementResource, residency, &failure))
            return false;
        rendering::TextureRuntimeInfo pending;
        if (!runtime.GetInfo(residency, pending, &failure) || pending.resourceGeneration != oldGeneration ||
            pending.replacementResourceGeneration != replacementResource.GetGeneration() ||
            pending.replacementState != rendering::TextureReplacementState::Uploading)
            return false;
        if (!runtime.ReplaceTexture(replacementResource, residency, &failure))
            return false;
        const rendering::TextureResidencyRuntimeStats afterIdempotentRequest = runtime.GetStats();
        if (afterIdempotentRequest.uploader.requestsIssued != beforeReplacement.uploader.requestsIssued + 1u)
            return false;

        if (!DriveUntilReplacementCurrent(frames, clock, runtime, residency, replacementResource.GetGeneration()))
            return false;
        rendering::TextureRuntimeInfo replaced;
        if (!runtime.GetInfo(residency, replaced, &failure) || replaced.residency != residency || replaced.resourceGeneration != replacementResource.GetGeneration() ||
            replaced.replacementState != rendering::TextureReplacementState::None || replaced.replacementRetryRequired || !oldResource.IsValid() ||
            !VerifyTextureInstallation(*replacementTexture, service, residency, expectedSourceFingerprint))
            return false;
        const rendering::TextureResidencyRuntimeStats replacementStats = runtime.GetStats();
        if (replacementStats.residency.replacements < beforeReplacement.residency.replacements + 1u)
            return false;

        // The old CPU object may outlive the GPU cutover, while future requests
        // and the runtime's resident-generation report already use the replacement.
        initialRequest.Reset();
        oldResource.Reset();
        resources::PipelineRequest currentRequest = streamer.Request(reference, resources::LoadPriority::Normal);
        resources::ResourceHandle currentResource = currentRequest.Acquire();
        if (!currentRequest.HasLoaded() || !currentResource.IsValid() || currentResource.GetGeneration() != replacementResource.GetGeneration())
            return false;

        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !service.SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure))
            return FailStep("seal-cutover");
        rendering::TextureResidencyManager& manager = runtime.GetResidencyManager();
        rendering::TextureGenerationRetirement retirementEvidence[2]{};
        rendering::TextureResidencyFailure evidenceFailure;
        if (!rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
            return FailStep("native-retire");
        const vanguard::u32 retirementCount = manager.PeekCompletedGenerationRetirements(retirementEvidence, &evidenceFailure);
        if (retirementCount != 1 || retirementEvidence[0].resourcePath != reference.GetPath() || retirementEvidence[0].resourceGeneration != oldGeneration ||
            retirementEvidence[0].retirementId == 0 || runtime.GetStats().residency.pendingGenerationRetirements != 1)
            return FailStep("exact-old-generation");
        rendering::TextureGenerationRetirement repeatedEvidence[2]{};
        if (manager.PeekCompletedGenerationRetirements(repeatedEvidence, &evidenceFailure) != 1 ||
            repeatedEvidence[0].retirementId != retirementEvidence[0].retirementId || repeatedEvidence[0].resourcePath != retirementEvidence[0].resourcePath ||
            repeatedEvidence[0].resourceGeneration != retirementEvidence[0].resourceGeneration)
            return FailStep("repeat-peek");
        if (!manager.AcknowledgeGenerationRetirement(retirementEvidence[0].retirementId, &evidenceFailure) ||
            manager.PeekCompletedGenerationRetirements(retirementEvidence, &evidenceFailure) != 0 || runtime.GetStats().residency.pendingGenerationRetirements != 0)
            return FailStep("ack");
        evidenceFailure = {};
        if (manager.AcknowledgeGenerationRetirement(repeatedEvidence[0].retirementId, &evidenceFailure) ||
            evidenceFailure.code != rendering::TextureResidencyFailureCode::InvalidArgument)
            return FailStep("stale-ack");
        rendering::TextureResidencyRuntimeFailure collectionFailure;
        static_cast<void>(runtime.CollectRetirements(&collectionFailure));
        if (collectionFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
            return false;

        const vanguard::u32 currentGeneration = replacementResource.GetGeneration();
        currentResource.Reset();
        currentRequest.Reset();
        replacementResource.Reset();
        replacementRequest.Reset();
        return RetireTexture(frames, clock, service, demand) &&
               VerifyAndAcknowledgeSingleGenerationRetirement(runtime, reference.GetPath(), currentGeneration);
    }

    [[nodiscard]] bool ExerciseTextureReplacementRetry(const resources::ResourceReference reference,
                                                       const resources::ResourceReference blockerReference,
                                                       streaming::ResourceStreamer& streamer,
                                                       engine::FramePipelineService& frames, TestClock& clock,
                                                       engine::RenderingService& service,
                                                       const filesystem::AbsolutePath& replacementPath,
                                                       const vanguard::crypto::Digest256& expectedSourceFingerprint) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] P3 retry proof failed at %s\n", step);
            return false;
        };
        resources::PipelineRequest initialRequest = streamer.Request(reference, resources::LoadPriority::High);
        resources::PipelineRequest blockerRequest = streamer.Request(blockerReference, resources::LoadPriority::High);
        if (!initialRequest.TryWait(10'000) || !initialRequest.HasLoaded() || !blockerRequest.TryWait(10'000) || !blockerRequest.HasLoaded())
            return FailStep("resource-load");
        resources::ResourceHandle oldResource = initialRequest.Acquire();
        resources::ResourceHandle blockerResource = blockerRequest.Acquire();
        const auto* const oldTexture = static_cast<const textures::TextureResourceObject*>(oldResource.Get());
        if (oldTexture == nullptr || !oldTexture->IsOpen() || !blockerResource.IsValid())
            return FailStep("resource-acquire");

        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureDemandHandle demand;
        rendering::TextureDemandHandle blockerDemand;
        if (!runtime.RequestTexture(oldResource, demand, &failure) || !DriveUntilReady(frames, clock, runtime, demand.GetResidency()) ||
            !runtime.RequestTexture(blockerResource, blockerDemand, &failure) ||
            !DriveUntilInstallationPending(runtime, blockerDemand.GetResidency()))
            return FailStep("blocker-pending");
        if (runtime.GetStats().residency.pendingInstallations != 1u)
            return FailStep("blocker-capacity");

        const rendering::GpuTextureResidencyHandle residency = demand.GetResidency();
        const vanguard::u32 oldGeneration = oldResource.GetGeneration();
        if (!streamer.PublishLoose({reference, replacementPath, {}, 0, 0}))
            return FailStep("publish-replacement");
        resources::PipelineRequest replacementRequest = streamer.Replace(reference, resources::LoadPriority::High);
        if (!replacementRequest.IsValid() || !replacementRequest.TryWait(10'000) || !replacementRequest.HasLoaded())
            return FailStep("replacement-load");
        resources::ResourceHandle replacementResource = replacementRequest.Acquire();
        const auto* const replacementTexture = static_cast<const textures::TextureResourceObject*>(replacementResource.Get());
        if (replacementTexture == nullptr || !replacementTexture->IsOpen() || replacementResource.GetGeneration() == oldGeneration)
            return FailStep("replacement-acquire");

        const rendering::TextureResidencyRuntimeStats beforeReplacement = runtime.GetStats();
        if (!runtime.ReplaceTexture(replacementResource, residency, &failure) ||
            !DriveUntilReplacementFailed(runtime, residency, replacementResource.GetGeneration()))
            return FailStep("replacement-failure-transition");
        const rendering::TextureResidencyRuntimeStats retainedStats = runtime.GetStats();
        if (retainedStats.retainedReplacementCandidateGpuBytes == 0)
            return FailStep("retained-candidate-charge");
        rendering::TextureRuntimeInfo failed;
        if (!runtime.GetInfo(residency, failed, &failure) || failed.state != rendering::TextureRuntimeState::BindlessReady ||
            failed.resourceGeneration != oldGeneration || failed.replacementResourceGeneration != replacementResource.GetGeneration() ||
            failed.replacementState != rendering::TextureReplacementState::Failed || !failed.replacementRetryRequired || !oldResource.IsValid() ||
            !VerifyTextureInstallation(*oldTexture, service, residency, expectedSourceFingerprint))
            return FailStep("failed-state-old-current");

        failure = {};
        if (runtime.ReplaceTexture(replacementResource, residency, &failure) ||
            failure.code != rendering::TextureResidencyRuntimeFailureCode::ReplacementRetryRequired)
            return FailStep("sticky-retry-required");

        // Accept the unrelated pending installation to release the manager's one
        // installation slot. The failed replacement keeps its completed physical
        // candidate, so explicit retry must not issue a second upload request.
        if (!DriveUntilReady(frames, clock, runtime, blockerDemand.GetResidency()))
            return FailStep("blocker-accept");
        const rendering::TextureResidencyRuntimeStats beforeRetry = runtime.GetStats();
        if (!runtime.RetryTextureReplacement(residency, &failure))
            return FailStep("explicit-retry");
        const rendering::TextureResidencyRuntimeStats afterRetry = runtime.GetStats();
        if (afterRetry.uploader.requestsIssued != beforeRetry.uploader.requestsIssued ||
            beforeRetry.uploader.requestsIssued != beforeReplacement.uploader.requestsIssued + 1u ||
            afterRetry.retainedReplacementCandidateGpuBytes != 0)
            return FailStep("retry-no-reupload");
        if (!DriveUntilReplacementCurrent(frames, clock, runtime, residency, replacementResource.GetGeneration()))
            return FailStep("retry-cutover");
        rendering::TextureRuntimeInfo replaced;
        if (!runtime.GetInfo(residency, replaced, &failure) || replaced.residency != residency ||
            replaced.resourceGeneration != replacementResource.GetGeneration() || replaced.replacementState != rendering::TextureReplacementState::None ||
            replaced.replacementRetryRequired || !VerifyTextureInstallation(*replacementTexture, service, residency, expectedSourceFingerprint))
            return FailStep("replacement-current");

        oldResource.Reset();
        initialRequest.Reset();
        replacementResource.Reset();
        replacementRequest.Reset();
        blockerResource.Reset();
        blockerRequest.Reset();
        blockerDemand.Reset();
        demand.Reset();
        if (!RunFrame(frames, clock))
            return FailStep("joint-retire-frame");
        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !service.SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure) || !rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
            return FailStep("joint-retire-fences");
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        rendering::TextureResidencyRuntimeFailure collectionFailure;
        static_cast<void>(service.GetGpuScene().GetLifetime().Collect(&lifetimeFailure));
        static_cast<void>(runtime.CollectRetirements(&collectionFailure));
        if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None ||
            collectionFailure.code != rendering::TextureResidencyRuntimeFailureCode::None || !RunFrame(frames, clock) ||
            runtime.GetStats().residencyRecords != 0 || runtime.GetStats().liveDemands != 0)
            return FailStep("joint-retire-collect");
        if (!AcknowledgeCompletedGenerationRetirements(runtime))
            return FailStep("joint-retire-evidence");
        return true;
    }

    [[nodiscard]] bool ExerciseTextureReplacementRetentionBound(const resources::ResourceReference firstReference,
                                                                const resources::ResourceReference secondReference,
                                                                const resources::ResourceReference blockerReference,
                                                                streaming::ResourceStreamer& streamer,
                                                                engine::FramePipelineService& frames, TestClock& clock,
                                                                engine::RenderingService& service,
                                                                const filesystem::AbsolutePath& replacementPath) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] P4 retained-candidate bound failed at %s\n", step);
            return false;
        };

        resources::PipelineRequest firstInitialRequest = streamer.Request(firstReference, resources::LoadPriority::High);
        resources::PipelineRequest secondInitialRequest = streamer.Request(secondReference, resources::LoadPriority::High);
        resources::PipelineRequest blockerRequest = streamer.Request(blockerReference, resources::LoadPriority::High);
        if (!firstInitialRequest.TryWait(10'000) || !firstInitialRequest.HasLoaded() ||
            !secondInitialRequest.TryWait(10'000) || !secondInitialRequest.HasLoaded() ||
            !blockerRequest.TryWait(10'000) || !blockerRequest.HasLoaded())
            return FailStep("resource-load");

        resources::ResourceHandle firstOldResource = firstInitialRequest.Acquire();
        resources::ResourceHandle secondOldResource = secondInitialRequest.Acquire();
        resources::ResourceHandle blockerResource = blockerRequest.Acquire();
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureDemandHandle firstDemand;
        rendering::TextureDemandHandle secondDemand;
        rendering::TextureDemandHandle blockerDemand;
        if (!runtime.RequestTexture(firstOldResource, firstDemand, &failure) ||
            !DriveUntilReady(frames, clock, runtime, firstDemand.GetResidency()) ||
            !runtime.RequestTexture(secondOldResource, secondDemand, &failure) ||
            !DriveUntilReady(frames, clock, runtime, secondDemand.GetResidency()) ||
            !runtime.RequestTexture(blockerResource, blockerDemand, &failure) ||
            !DriveUntilInstallationPending(runtime, blockerDemand.GetResidency()))
            return FailStep("initial-residency");
        if (runtime.GetStats().residency.pendingInstallations != 1u)
            return FailStep("blocker-capacity");

        if (!streamer.PublishLoose({firstReference, replacementPath, {}, 0, 0}) ||
            !streamer.PublishLoose({secondReference, replacementPath, {}, 0, 0}))
            return FailStep("publish-replacements");
        resources::PipelineRequest firstReplacementRequest = streamer.Replace(firstReference, resources::LoadPriority::High);
        resources::PipelineRequest secondReplacementRequest = streamer.Replace(secondReference, resources::LoadPriority::High);
        if (!firstReplacementRequest.TryWait(10'000) || !firstReplacementRequest.HasLoaded() ||
            !secondReplacementRequest.TryWait(10'000) || !secondReplacementRequest.HasLoaded())
            return FailStep("replacement-load");
        resources::ResourceHandle firstReplacement = firstReplacementRequest.Acquire();
        resources::ResourceHandle secondReplacement = secondReplacementRequest.Acquire();
        if (!firstReplacement.IsValid() || !secondReplacement.IsValid())
            return FailStep("replacement-acquire");

        const rendering::GpuTextureResidencyHandle firstResidency = firstDemand.GetResidency();
        const rendering::GpuTextureResidencyHandle secondResidency = secondDemand.GetResidency();
        if (!runtime.ReplaceTexture(firstReplacement, firstResidency, &failure) ||
            !DriveUntilReplacementFailed(runtime, firstResidency, firstReplacement.GetGeneration()))
            return FailStep("first-retain");
        const rendering::TextureResidencyRuntimeStats oneRetained = runtime.GetStats();
        if (oneRetained.retainedReplacementCandidateGpuBytes == 0 ||
            oneRetained.retainedReplacementCandidateGpuBytes > RetainedReplacementBudget)
            return FailStep("first-retained-charge");
        rendering::TextureResidencyRuntimeFailure shutdownFailure;
        if (runtime.Shutdown(&shutdownFailure) ||
            shutdownFailure.code != rendering::TextureResidencyRuntimeFailureCode::LiveResidencyRemains ||
            runtime.GetStats().retainedReplacementCandidateGpuBytes != oneRetained.retainedReplacementCandidateGpuBytes)
            return FailStep("active-replacement-shutdown-refusal");

        if (!runtime.ReplaceTexture(secondReplacement, secondResidency, &failure) ||
            !DriveUntilReplacementFailed(runtime, secondResidency, secondReplacement.GetGeneration()))
            return FailStep("second-overflow-transition");
        rendering::TextureRuntimeInfo secondFailed;
        const rendering::TextureResidencyRuntimeStats capped = runtime.GetStats();
        if (!runtime.GetInfo(secondResidency, secondFailed, &failure) ||
            secondFailed.replacementFailure.code != rendering::TextureUploadFailureCode::CapacityExceeded ||
            capped.retainedReplacementCandidateGpuBytes != oneRetained.retainedReplacementCandidateGpuBytes)
            return FailStep("bounded-overflow-state");

        // Free the manager installation slot. The first replacement has a retained candidate and therefore
        // must retry without another upload; the overflowed second replacement has no retained candidate and
        // must explicitly re-upload once the retained-byte charge has been released.
        if (!DriveUntilReady(frames, clock, runtime, blockerDemand.GetResidency()))
            return FailStep("blocker-accept");
        const vanguard::u64 beforeFirstRetryRequests = runtime.GetStats().uploader.requestsIssued;
        if (!runtime.RetryTextureReplacement(firstResidency, &failure))
            return FailStep("first-explicit-retry");
        const rendering::TextureResidencyRuntimeStats firstRetryStats = runtime.GetStats();
        if (firstRetryStats.uploader.requestsIssued != beforeFirstRetryRequests || firstRetryStats.retainedReplacementCandidateGpuBytes != 0 ||
            !DriveUntilReplacementCurrent(frames, clock, runtime, firstResidency, firstReplacement.GetGeneration()))
            return FailStep("first-retry-no-reupload");

        const vanguard::u64 beforeSecondRetryRequests = runtime.GetStats().uploader.requestsIssued;
        if (!runtime.RetryTextureReplacement(secondResidency, &failure))
            return FailStep("second-explicit-retry");
        if (runtime.GetStats().uploader.requestsIssued != beforeSecondRetryRequests + 1u ||
            !DriveUntilReplacementCurrent(frames, clock, runtime, secondResidency, secondReplacement.GetGeneration()) ||
            runtime.GetStats().retainedReplacementCandidateGpuBytes != 0)
            return FailStep("second-overflow-recovery");

        // The two completed replacements intentionally retain their old-generation
        // evidence until composition accepts it. Acknowledge that handoff before
        // exercising unrelated terminal residency retirement, otherwise the
        // retained evidence correctly consumes the shared retirement bound.
        rhi::Failure replacementRetirementRhiFailure;
        rhi::ResidencyFenceSet replacementRetirementFences;
        engine::RenderingRetirementFailure replacementRetirementFailure;
        if (!SubmitRetirementFences(replacementRetirementFences, replacementRetirementRhiFailure))
            return FailStep("replacement-retirement-submit");
        if (!service.SealResidencyRetirements(replacementRetirementFences, &replacementRetirementFailure))
            return FailStep("replacement-retirement-seal");
        if (!WaitRetirementFences(replacementRetirementFences, replacementRetirementRhiFailure))
            return FailStep("replacement-retirement-wait");
        if (!rhi::RetireResources(&replacementRetirementRhiFailure) || !rhi::FlushRetiredResources(&replacementRetirementRhiFailure))
            return FailStep("replacement-retirement-native");
        if (!AcknowledgeCompletedGenerationRetirements(runtime))
            return FailStep("replacement-retirement-ack");

        firstOldResource.Reset();
        secondOldResource.Reset();
        firstInitialRequest.Reset();
        secondInitialRequest.Reset();
        firstReplacement.Reset();
        secondReplacement.Reset();
        firstReplacementRequest.Reset();
        secondReplacementRequest.Reset();
        blockerResource.Reset();
        blockerRequest.Reset();
        firstDemand.Reset();
        secondDemand.Reset();
        blockerDemand.Reset();
        if (!RunFrame(frames, clock))
            return FailStep("retire-frame");
        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !service.SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure) || !rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
            return FailStep("retire-fences");
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        rendering::TextureResidencyRuntimeFailure collectionFailure;
        static_cast<void>(service.GetGpuScene().GetLifetime().Collect(&lifetimeFailure));
        static_cast<void>(runtime.CollectRetirements(&collectionFailure));
        if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None ||
            collectionFailure.code != rendering::TextureResidencyRuntimeFailureCode::None || !RunFrame(frames, clock) ||
            runtime.GetStats().retainedReplacementCandidateGpuBytes != 0)
            return FailStep("retire-collect");
        if (!AcknowledgeCompletedGenerationRetirements(runtime))
            return FailStep("retire-evidence");
        return true;
    }

    [[nodiscard]] bool ExerciseTextureLifecycleSessions(const resources::ResourceHandle& resource) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] P4 lifecycle-session proof failed at %s\n", step);
            return false;
        };
        rendering::TextureUploadFailure uploadFailure;
        rendering::TextureUploaderConfig uploaderConfig;
        uploaderConfig.maximumRequests = 1;
        rendering::TextureUploader uploader;
        rendering::TextureUploadRequestId staleRequest;
        rendering::TextureUploadRequestId currentRequest;
        if (!uploader.Initialize(uploaderConfig, &uploadFailure) ||
            !uploader.RequestGenerationReplacement(resource, {0x7fffu, 1u}, staleRequest, vanguard::io::eAsyncPriority_Streaming, &uploadFailure))
            return FailStep("uploader-session-a");
        uploader.AbandonDevice();
        if (!uploader.Initialize(uploaderConfig, &uploadFailure) ||
            !uploader.RequestGenerationReplacement(resource, {0x7fffu, 1u}, currentRequest, vanguard::io::eAsyncPriority_Streaming, &uploadFailure) ||
            uploader.GetState(staleRequest) != rendering::TextureUploadState::Invalid || staleRequest == currentRequest)
            return FailStep("uploader-session-b");
        uploadFailure = {};
        if (uploader.Cancel(staleRequest, &uploadFailure) || uploadFailure.code != rendering::TextureUploadFailureCode::InvalidArgument ||
            uploader.GetState(currentRequest) == rendering::TextureUploadState::Invalid)
            return FailStep("uploader-stale-rejection");
        uploader.AbandonDevice();

        rhi::Failure rhiFailure;
        const vanguard::u32 visibility = (1u << static_cast<vanguard::u32>(rhi::ShaderStage::Count)) - 1u;
        rhi::DescriptorDomain localDescriptors(rhi::AdoptReference,
                                               rhi::CreateDescriptorDomain({rhi::DescriptorDomainKind::Resources, 128, 0, visibility}, &rhiFailure));
        rendering::RenderSceneManager localScenes;
        rendering::RenderSceneFailure sceneFailure;
        rendering::GpuSceneRuntimeConfig gpuSceneConfig;
        gpuSceneConfig.tables.resourceDescriptors = localDescriptors;
        gpuSceneConfig.tables.maximumPagesPerTable = 2;
        gpuSceneConfig.upload.bytesPerSegment = 2u * 1024u * 1024u;
        gpuSceneConfig.upload.maximumUpdatesPerBatch = 64;
        gpuSceneConfig.upload.maximumCopiesPerBatch = 128;
        gpuSceneConfig.definitions.maximumGeometries = 2;
        gpuSceneConfig.definitions.maximumMaterials = 4;
        gpuSceneConfig.definitions.maximumRenderables = 2;
        gpuSceneConfig.definitions.maximumDefinitionsPerBatch = 6;
        gpuSceneConfig.definitions.maximumAllocationsPerBatch = 16;
        gpuSceneConfig.maximumExternalContributions = 2;
        rendering::GpuSceneRuntime localGpuScene;
        rendering::GpuSceneRuntimeFailure gpuSceneFailure;
        if (!localDescriptors.IsValid() || !localScenes.Initialize({}, &sceneFailure) ||
            !localGpuScene.Initialize(localScenes, gpuSceneConfig, &gpuSceneFailure))
            return FailStep("local-gpu-scene-init");

        rendering::TextureResidencyRuntimeConfig runtimeConfig;
        runtimeConfig.residency.maximumTextures = 1;
        runtimeConfig.residency.maximumPendingInstallations = 1;
        runtimeConfig.residency.maximumPendingRetirements = 1;
        runtimeConfig.uploader.maximumRequests = 1;
        runtimeConfig.maximumResidencyRecords = 1;
        runtimeConfig.maximumDemands = 1;
        runtimeConfig.maximumInstallationsPerTick = 1;
        runtimeConfig.maximumTableInstallationsPerFrame = 1;
        runtimeConfig.maximumStateChecksPerTick = 1;
        rendering::TextureResidencyRuntime runtime;
        rendering::TextureResidencyRuntimeFailure runtimeFailure;
        rendering::TextureDemandHandle staleDemand;
        rendering::TextureDemandHandle currentDemand;
        if (!runtime.Initialize(localGpuScene.GetLifetime(), localDescriptors, runtimeConfig, &runtimeFailure) ||
            !runtime.RequestTexture(resource, staleDemand, &runtimeFailure) || !staleDemand.IsValid())
            return FailStep("runtime-session-a");
        if (!runtime.AbandonDevice(&runtimeFailure) || staleDemand.IsValid() || !localGpuScene.AbandonDevice(&gpuSceneFailure))
            return FailStep("runtime-session-a-abandon");
        gpuSceneConfig.tables.resourceDescriptors = {};
        localDescriptors.Reset();
        localDescriptors = rhi::DescriptorDomain(rhi::AdoptReference,
                                                 rhi::CreateDescriptorDomain({rhi::DescriptorDomainKind::Resources, 128, 0, visibility}, &rhiFailure));
        gpuSceneConfig.tables.resourceDescriptors = localDescriptors;
        if (!localDescriptors.IsValid() || !localGpuScene.Initialize(localScenes, gpuSceneConfig, &gpuSceneFailure))
            return FailStep("runtime-session-b-gpu");
        if (!runtime.Initialize(localGpuScene.GetLifetime(), localDescriptors, runtimeConfig, &runtimeFailure) ||
            !runtime.RequestTexture(resource, currentDemand, &runtimeFailure) || !currentDemand.IsValid() || staleDemand.IsValid())
            return FailStep("runtime-session-b");
        staleDemand.Reset();
        if (!currentDemand.IsValid() || !runtime.AbandonDevice(&runtimeFailure) || currentDemand.IsValid() ||
            !localGpuScene.AbandonDevice(&gpuSceneFailure))
            return FailStep("runtime-stale-release");
        currentDemand.Reset();
        const bool scenesStopped = localScenes.Shutdown(&sceneFailure);
        localDescriptors.Reset();
        return scenesStopped ? true : FailStep("local-scene-shutdown");
    }

    [[nodiscard]] bool VerifyMaterialInitializationRollback(engine::RenderingServiceConfig config) noexcept
    {
        config.materialResources.maximumProviders = 0;
        vanguard::application::EngineHost host;
        vanguard::application::HostFailure failure;
        if (!engine::RegisterEngineModule(host, &failure) || !engine::RegisterIoService(host, &failure) || !engine::RegisterFilesystemService(host, &failure) || !engine::RegisterJobsService(host, &failure) ||
            !engine::RegisterFramePipelineService(host, &failure) || !engine::RegisterResourcesService(host, &failure) || !engine::RegisterRenderingService(host, config, &failure) ||
            !host.Compile(vanguard::application::ApplicationProfile::Runtime, &failure))
            return false;
        return !host.Start(&failure) && !rhi::IsInitialized();
    }

    [[nodiscard]] bool VerifyUndeliveredGenerationRetirementShutdownFailure(const engine::RenderingServiceConfig& renderingConfig,
                                                                           const resources::ResourceReference reference,
                                                                           const filesystem::AbsolutePath& loosePath,
                                                                           const filesystem::AbsolutePath& replacementPath,
                                                                           const filesystem::AbsolutePath& cleanupDirectory) noexcept
    {
        const auto FailStep = [](const char* const step) noexcept
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] undelivered shutdown proof failed at %s\n", step);
            return false;
        };
        vanguard::application::EngineHost host;
        vanguard::application::HostFailure failure;
        if (!engine::RegisterEngineModule(host, &failure) || !engine::RegisterIoService(host, &failure) || !engine::RegisterFilesystemService(host, &failure) ||
            !engine::RegisterJobsService(host, &failure) || !engine::RegisterFramePipelineService(host, &failure) || !engine::RegisterReflectionService(host, &failure) ||
            !engine::RegisterResourcesService(host, &failure) || !engine::RegisterResourceStreamingService(host, &failure) ||
            !engine::RegisterRenderingService(host, renderingConfig, &failure) ||
            !host.Compile(vanguard::application::ApplicationProfile::Runtime, &failure) || !host.Start(&failure))
        {
            std::fprintf(stderr, "[textureResidencyServiceTests] undelivered shutdown host failure: code=%u service=%llu message=%s\n",
                         static_cast<unsigned>(failure.code), static_cast<unsigned long long>(failure.service), failure.message != nullptr ? failure.message : "");
            return FailStep("host-start");
        }

        engine::FramePipelineService* const frames = engine::FindFramePipelineService(host);
        engine::ResourcesService* const resourcesService = engine::FindResourcesService(host);
        engine::ResourceStreamingService* const streamingService = engine::FindResourceStreamingService(host);
        engine::RenderingService* const renderingService = engine::FindRenderingService(host);
        if (frames == nullptr || resourcesService == nullptr || streamingService == nullptr || renderingService == nullptr)
            return FailStep("service-find");
        TestClock clock;
        engine::FrameFailure frameFailure;
        engine::FramePipelineConfig frameConfig;
        frameConfig.clock = {&ReadClock, 1'000, &clock};
        frameConfig.pacing = engine::FramePacingMode::Disabled;
        if (!frames->Configure(frameConfig, &frameFailure) || !frames->Compile(&frameFailure))
            return FailStep("frame-configure");

        filesystem::Manager& files = filesystem::GetManager();
        DeleteDirectoryTree(files, cleanupDirectory);
        if (!files.CreatePath(cleanupDirectory) || !WriteStandaloneTextureFixture(loosePath, 17u) || !WriteStandaloneTextureFixture(replacementPath, 53u))
            return FailStep("fixture-create");

        streaming::ResourceStreamer& streamer = streamingService->GetStreamer();
        if (!streamer.RegisterLoose({reference, loosePath, {}, 0, 0}))
            return FailStep("register-loose");
        resources::PipelineRequest request = streamer.Request(reference, resources::LoadPriority::High);
        if (!request.TryWait(10'000) || !request.HasLoaded())
            return FailStep("initial-load");
        resources::ResourceHandle resource = request.Acquire();
        rendering::TextureResidencyRuntime& runtime = renderingService->GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure runtimeFailure;
        rendering::TextureDemandHandle demand;
        if (!resource.IsValid() || !runtime.RequestTexture(resource, demand, &runtimeFailure) ||
            !DriveUntilReady(*frames, clock, runtime, demand.GetResidency()))
            return FailStep("initial-residency");

        const vanguard::u32 oldGeneration = resource.GetGeneration();
        const rendering::GpuTextureResidencyHandle residency = demand.GetResidency();
        if (!streamer.PublishLoose({reference, replacementPath, {}, 0, 0}))
            return FailStep("publish-replacement");
        resources::PipelineRequest replacementRequest = streamer.Replace(reference, resources::LoadPriority::High);
        if (!replacementRequest.IsValid() || !replacementRequest.TryWait(10'000) || !replacementRequest.HasLoaded())
            return FailStep("replacement-load");
        resources::ResourceHandle replacementResource = replacementRequest.Acquire();
        if (!replacementResource.IsValid() || replacementResource.GetGeneration() == oldGeneration ||
            !runtime.ReplaceTexture(replacementResource, residency, &runtimeFailure) ||
            !DriveUntilReplacementCurrent(*frames, clock, runtime, residency, replacementResource.GetGeneration()))
            return FailStep("replacement-current");

        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !renderingService->SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure) || !rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
            return FailStep("replacement-retirement");
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        static_cast<void>(renderingService->GetGpuScene().GetLifetime().Collect(&lifetimeFailure));
        static_cast<void>(runtime.CollectRetirements(&runtimeFailure));
        rendering::TextureGenerationRetirement evidence[2]{};
        rendering::TextureResidencyFailure evidenceFailure;
        if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None ||
            runtimeFailure.code != rendering::TextureResidencyRuntimeFailureCode::None ||
            runtime.GetStats().residency.pendingGenerationRetirements != 1 ||
            runtime.GetResidencyManager().PeekCompletedGenerationRetirements(evidence, &evidenceFailure) != 1 ||
            evidence[0].resourcePath != reference.GetPath() || evidence[0].resourceGeneration != oldGeneration || evidence[0].retirementId == 0)
            return FailStep("replacement-evidence");
        const vanguard::u64 oldRetirementId = evidence[0].retirementId;
        const vanguard::u32 currentGeneration = replacementResource.GetGeneration();

        resource.Reset();
        request.Reset();
        if (!RetireTexture(*frames, clock, *renderingService, demand) ||
            runtime.GetStats().residency.pendingGenerationRetirements != 2)
            return FailStep("current-retirement");
        rendering::TextureGenerationRetirement finalEvidence[3]{};
        const vanguard::u32 finalEvidenceCount = runtime.GetResidencyManager().PeekCompletedGenerationRetirements(finalEvidence, &evidenceFailure);
        vanguard::u64 currentRetirementId = 0;
        bool retainedOldEvidence = false;
        for (vanguard::u32 index = 0; index < finalEvidenceCount; ++index)
        {
            const rendering::TextureGenerationRetirement& item = finalEvidence[index];
            if (item.retirementId == oldRetirementId && item.resourcePath == reference.GetPath() && item.resourceGeneration == oldGeneration)
                retainedOldEvidence = true;
            if (item.resourcePath == reference.GetPath() && item.resourceGeneration == currentGeneration && item.retirementId != oldRetirementId)
                currentRetirementId = item.retirementId;
        }
        if (evidenceFailure.code != rendering::TextureResidencyFailureCode::None || finalEvidenceCount != 2 || !retainedOldEvidence || currentRetirementId == 0 ||
            !runtime.GetResidencyManager().AcknowledgeGenerationRetirement(currentRetirementId, &evidenceFailure))
            return FailStep("current-retirement-evidence");
        rendering::TextureGenerationRetirement remainingEvidence[2]{};
        if (runtime.GetResidencyManager().PeekCompletedGenerationRetirements(remainingEvidence, &evidenceFailure) != 1 ||
            remainingEvidence[0].retirementId != oldRetirementId || remainingEvidence[0].resourcePath != reference.GetPath() ||
            remainingEvidence[0].resourceGeneration != oldGeneration || runtime.GetStats().residency.pendingGenerationRetirements != 1)
            return FailStep("isolated-undelivered-evidence");
        replacementResource.Reset();
        replacementRequest.Reset();
        if (!streamer.UnregisterLoose(reference.GetPath()) || !WaitForResourceDrain(resourcesService->GetPipeline(), streamer))
            return FailStep("streaming-drain");
        DeleteDirectoryTree(filesystem::GetManager(), cleanupDirectory);

        failure = {};
        const bool shutdown = host.Shutdown(&failure);
        bool rendererDrainFailure = false;
        host.VisitLifecycleEvents([](const vanguard::application::LifecycleEvent& event, void* const userData) noexcept
        {
            auto& observed = *static_cast<bool*>(userData);
            if (event.service == engine::RenderingServiceId && event.stage == vanguard::application::LifecycleStage::Drain &&
                event.result == vanguard::application::HostFailureCode::DrainFailure)
                observed = true;
        }, &rendererDrainFailure);
        std::fprintf(stderr,
                     "[textureResidencyServiceTests] undelivered shutdown result: shutdown=%u code=%u service=%llu rendererDrainFailure=%u rhiInitialized=%u message=%s\n",
                     shutdown ? 1u : 0u, static_cast<unsigned>(failure.code), static_cast<unsigned long long>(failure.service),
                     rendererDrainFailure ? 1u : 0u, rhi::IsInitialized() ? 1u : 0u,
                     failure.message != nullptr ? failure.message : "");
        // ResourceRegistry also reports the weak source handle deliberately retained by the
        // unacknowledged texture retirement. The host keeps unwinding after the renderer's
        // lossless Drain failure, so inspect lifecycle evidence rather than assuming the last
        // cleanup failure returned to the caller belongs to RenderingService.
        return !shutdown && rendererDrainFailure && !rhi::IsInitialized();
    }

    [[nodiscard]] bool RunUndeliveredGenerationRetirementShutdownSubprocess(const char* const executable) noexcept
    {
        if (executable == nullptr || executable[0] == '\0')
            return false;
#if defined(_WIN32)
        char command[2048]{};
        const int written = std::snprintf(command, sizeof(command), "\"%s\" %s", executable, UndeliveredGenerationRetirementMode);
        if (written <= 0 || static_cast<vanguard::usize>(written) >= sizeof(command))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == UndeliveredGenerationRetirementSuccessExit;
#else
        return false;
#endif
    }

} // namespace

int main(const int argumentCount, char** const arguments)
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "textureResidencyServiceTests"), "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    const engine::RenderingServiceConfig renderingConfig = MakeRenderingConfig();
    if (argumentCount == 2 && std::strcmp(arguments[1], UndeliveredGenerationRetirementMode) == 0)
    {
        if (g_failures != 0)
            return 44;
        const filesystem::AbsolutePath root = vanguard::tests::ScratchDirectory("texture_residency_service_tests");
        const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_texture_service_tests");
        const resources::ResourceReference runtimeTexture(resources::ResourcePath::FromString("textures/runtime_source.vtex"), textures::TextureResourceType);
        const bool proof = VerifyUndeliveredGenerationRetirementShutdownFailure(renderingConfig, runtimeTexture,
                                                                                directory.AddFilePath("runtime_loose.vtex"),
                                                                                directory.AddFilePath("runtime_loose_replacement.vtex"), directory);
        vanguard::diagnostics::Shutdown();
        return proof ? static_cast<int>(UndeliveredGenerationRetirementSuccessExit) : 43;
    }

    vanguard::application::EngineHost host;
    vanguard::application::HostFailure hostFailure;

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
    Check(frames != nullptr && resourcesService != nullptr && streamingService != nullptr && renderingService != nullptr && renderingService->GetTextureResidency().IsInitialized() &&
              renderingService->GetGpuScene().IsInitialized(),
          "required rendering and resource services are initialized");
    Check(renderingService != nullptr && renderingService->GetMaterialProgramLayouts().IsInitialized() && renderingService->GetMaterialProgramLayouts().GetStats().capacity == 8,
          "RenderingService owns the bounded material program-layout registry beside both descriptor domains");
    Check(renderingService != nullptr && renderingService->GetMaterialResources().IsInitialized() && renderingService->GetMaterialMaterializer().IsInitialized() &&
              renderingService->GetMaterialResidency().IsInitialized() && renderingService->GetMaterialBindings().IsInitialized(),
          "RenderingService owns the complete material runtime chain in dependency order");
    const engine::RenderingResourceAllocatorDiagnostics frameResourceDiagnostics =
        renderingService != nullptr ? renderingService->GetResourceAllocatorDiagnostics() : engine::RenderingResourceAllocatorDiagnostics{};
    Check(frameResourceDiagnostics.initialized && frameResourceDiagnostics.stats.state == rendering::RenderFlowResourceSessionState::Idle && frameResourceDiagnostics.stats.chargedNativeBytes == 0,
          "FrameRenderer owns an initialized idle frame-resource allocator after device startup");
    engine::RenderingRetirementFailure missingCutoverFailure;
    Check(renderingService != nullptr && !renderingService->SealResidencyRetirements({}, &missingCutoverFailure) && missingCutoverFailure.code == engine::RenderingRetirementFailureCode::MissingCutover,
          "RenderingService rejects residency retirement without complete queue cutover evidence");

    TestClock clock;
    engine::FrameFailure frameFailure;
    engine::FramePipelineConfig frameConfig;
    frameConfig.clock = {&ReadClock, 1'000, &clock};
    frameConfig.pacing = engine::FramePacingMode::Disabled;
    Check(frames != nullptr && frames->Configure(frameConfig, &frameFailure) && frames->Compile(&frameFailure), "ordinary engine frame pipeline compilation");

    const filesystem::AbsolutePath root = vanguard::tests::ScratchDirectory("texture_residency_service_tests");
    const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_texture_service_tests");
    const filesystem::AbsolutePath loosePath = directory.AddFilePath("runtime_loose.vtex");
    const filesystem::AbsolutePath looseTemporaryPath = directory.AddFilePath("runtime_loose.vtex.tmp");
    const filesystem::AbsolutePath qualityBc7LoosePath = directory.AddFilePath("runtime_loose_quality_bc7.vtex");
    const filesystem::AbsolutePath qualityBc7LooseTemporaryPath = directory.AddFilePath("runtime_loose_quality_bc7.vtex.tmp");
    const filesystem::AbsolutePath qualityReplacementLoosePath = directory.AddFilePath("runtime_loose_quality_replacement.vtex");
    const filesystem::AbsolutePath qualityReplacementLooseTemporaryPath = directory.AddFilePath("runtime_loose_quality_replacement.vtex.tmp");
    const filesystem::AbsolutePath replacementLoosePath = directory.AddFilePath("runtime_loose_replacement.vtex");
    const filesystem::AbsolutePath replacementLooseTemporaryPath = directory.AddFilePath("runtime_loose_replacement.vtex.tmp");
    const filesystem::AbsolutePath packagePath = directory.AddFilePath("runtime_texture.vpak");
    const filesystem::AbsolutePath packageTemporaryPath = directory.AddFilePath("runtime_texture.vpak.tmp");
    filesystem::Manager& files = filesystem::GetManager();
    DeleteDirectoryTree(files, directory);
    Check(files.CreatePath(directory), "fresh texture cook integration directory");

    Check(textureTools::Initialize(), "texture tools initialization");
    const textureTools::TextureCookingProfile runtimeProofProfile{RuntimeProofProfile,
                                                                  1,
                                                                  textures::PixelFormat::R8G8B8A8UNorm,
                                                                  textures::ColorSpace::SRgb,
                                                                  textureTools::TextureCookingFlags::GenerateFullMipChain | textureTools::TextureCookingFlags::Streamable,
                                                                  4,
                                                                  3,
                                                                  0.5f,
                                                                  0};
    Check(textureTools::RegisterCookingProfile(runtimeProofProfile) == textureTools::ProfileRegistrationResult::Success, "register deterministic uncompressed streamable texture proof profile");
    assets::BuildSystem buildSystem;
    assets::Config buildConfig;
    buildConfig.persistentCacheRoot = directory.AsChar();
    Check(buildSystem.Initialize(buildConfig), "persistent texture build-system initialization");
    textureTools::TextureAssetCompiler textureCompiler;
    Check(textureCompiler.Initialize() && textureCompiler.Register(buildSystem) == assets::Result::Success, "registered VTSR-to-VTEX compiler");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = directory.AsChar();
    indexConfig.settingsFingerprint = vanguard::crypto::Sha256("texture-service-index-v1", 24);
    assets::DependencyIndex dependencyIndex;
    Check(dependencyIndex.Initialize(indexConfig) == assets::IndexResult::Success, "texture dependency index initialization");
    Check(dependencyIndex.BindPersistentProtectionDomain(buildSystem), "bind texture dependency index to persistent protection domain");

    ByteArray jpegBytes(vanguard::memory::pools::Assets::GetInstance());
    const filesystem::AbsolutePath jpegPath = filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("source").AddDirPath("textureTools").AddDirPath("tests").AddDirPath("data").AddFilePath("libjpeg_rgb.jpg");
    Check(filesystem::LoadFileToBuffer(jpegPath, jpegBytes), "load the real encoded JPEG source fixture");
    const resources::ResourceReference jpegSource(resources::ResourcePath::FromString("tests/textures/runtime_source.jpg"), textureTools::TextureSourceResourceType);
    const resources::ResourceReference jpegOutput(resources::ResourcePath::FromString("textures/runtime_source.vtex"), textures::TextureResourceType);
    textureTools::TextureBuildDescription jpegDescription;
    jpegDescription.sourceMode = textureTools::TextureBuildSourceMode::Image2D;
    jpegDescription.colorSpace = textureTools::ImportedColorSpace::Automatic;
    jpegDescription.profile = RuntimeProofProfile;
    ByteArray jpegSettings(vanguard::memory::pools::Assets::GetInstance());
    Check(textureTools::EncodeTextureBuildSettings(jpegDescription, jpegSettings) == textureTools::TextureBuildSettingsResult::Success, "encode canonical JPEG texture settings");
    const assets::BuildRequest jpegRequest{{jpegSource, {jpegBytes.TypedData(), jpegBytes.Size()}, {}}, jpegOutput, assets::TargetPlatform::WindowsD3D12, jpegSettings};
    assets::BuildOutput jpegOutputBuild;
    Check(BuildAndPublish(buildSystem, dependencyIndex, jpegRequest, jpegOutputBuild) && jpegOutputBuild.disposition == assets::BuildDisposition::Built,
          "real JPEG source compiles and publishes through BuildSystem/DDC/index");
    assets::BuildPlan jpegCachedPlan;
    assets::BuildOutput jpegCachedBuild;
    Check(buildSystem.Prepare(jpegRequest, jpegCachedPlan) == assets::Result::Success && buildSystem.Execute(jpegRequest, jpegCachedPlan, jpegCachedBuild) == assets::Result::Success &&
              jpegCachedBuild.disposition == assets::BuildDisposition::CacheHit && jpegCachedBuild.buildFingerprint == jpegOutputBuild.buildFingerprint,
          "identical JPEG request hits the generic texture build cache");
    const vanguard::crypto::Digest256 jpegSourceFingerprint = vanguard::crypto::Sha256(jpegBytes.TypedData(), jpegBytes.Size());

    textureTools::TextureBuildDescription alternateJpegDescription = jpegDescription;
    alternateJpegDescription.profile = textureTools::profiles::Ui;
    ByteArray alternateJpegSettings(vanguard::memory::pools::Assets::GetInstance());
    Check(textureTools::EncodeTextureBuildSettings(alternateJpegDescription, alternateJpegSettings) == textureTools::TextureBuildSettingsResult::Success, "encode alternate valid JPEG profile");
    const assets::BuildRequest alternateJpegRequest{{jpegSource, {jpegBytes.TypedData(), jpegBytes.Size()}, {}}, jpegOutput, assets::TargetPlatform::WindowsD3D12, alternateJpegSettings};
    assets::BuildOutput alternateJpegBuild;
    Check(buildSystem.Build(alternateJpegRequest, alternateJpegBuild) == assets::Result::Success && alternateJpegBuild.disposition == assets::BuildDisposition::Built &&
              !(alternateJpegBuild.buildFingerprint == jpegOutputBuild.buildFingerprint),
          "texture profile change rebuilds under a different build identity");

    const resources::ResourceReference qualityBc7Texture(resources::ResourcePath::FromString("textures/runtime_quality_bc7.vtex"), textures::TextureResourceType);
    textureTools::TextureBuildDescription qualityBc7Description = jpegDescription;
    qualityBc7Description.profile = textureTools::profiles::Color;
    ByteArray qualityBc7Settings(vanguard::memory::pools::Assets::GetInstance());
    Check(textureTools::EncodeTextureBuildSettings(qualityBc7Description, qualityBc7Settings) == textureTools::TextureBuildSettingsResult::Success,
          "encode the production Color-profile BC7 quality settings");
    const assets::BuildRequest qualityBc7Request{{jpegSource, {jpegBytes.TypedData(), jpegBytes.Size()}, {}}, qualityBc7Texture,
                                                 assets::TargetPlatform::WindowsD3D12, qualityBc7Settings};
    assets::BuildOutput qualityBc7Build;
    Check(BuildAndPublish(buildSystem, dependencyIndex, qualityBc7Request, qualityBc7Build) && qualityBc7Build.disposition == assets::BuildDisposition::Built,
          "production Color-profile source compiles to a streamable BC7 quality fixture");

    constexpr const char* RuntimeTexturePath = "textures/runtime_source.vtex";
    const resources::ResourceReference runtimeTexture = jpegOutput;
    const resources::ResourceReference qualityTexture(resources::ResourcePath::FromString("textures/runtime_quality.vtex"), textures::TextureResourceType);
    const resources::ResourceReference retryTexture(resources::ResourcePath::FromString("textures/runtime_retry.vtex"), textures::TextureResourceType);
    const resources::ResourceReference retryBlockerTexture(resources::ResourcePath::FromString("textures/runtime_retry_blocker.vtex"), textures::TextureResourceType);
    const resources::ResourceReference boundedFirstTexture(resources::ResourcePath::FromString("textures/runtime_bound_first.vtex"), textures::TextureResourceType);
    const resources::ResourceReference boundedSecondTexture(resources::ResourcePath::FromString("textures/runtime_bound_second.vtex"), textures::TextureResourceType);
    const resources::ResourceReference boundedBlockerTexture(resources::ResourcePath::FromString("textures/runtime_bound_blocker.vtex"), textures::TextureResourceType);
    Check(!jpegOutputBuild.artifacts.Empty() && dependencyIndex.Save() == assets::IndexResult::Success, "streamable JPEG cook publishes its indexed metadata and mip artifacts");

    assets::DependencyRecord runtimeRecord;
    assets::DependencyRecord qualityBc7Record;
    assets::DerivedDataArtifactSource artifactSource;
    Check(dependencyIndex.Find(runtimeTexture, runtimeRecord) == assets::IndexResult::Success &&
              dependencyIndex.Find(qualityBc7Texture, qualityBc7Record) == assets::IndexResult::Success &&
              artifactSource.Initialize({directory.AsChar(), {}}),
          "open the indexed real-JPEG artifact set through the generic DDC source");
    assets::LooseResourceMaterializer materializer;
    Check(materializer.Materialize(runtimeRecord, runtimeTexture, artifactSource, loosePath, looseTemporaryPath) == assets::LooseMaterializationResult::Success && !files.FileExist(looseTemporaryPath),
          "materialize the indexed JPEG artifacts as one loose VTEX");
    Check(materializer.Materialize(qualityBc7Record, qualityBc7Texture, artifactSource, qualityBc7LoosePath,
                                   qualityBc7LooseTemporaryPath) == assets::LooseMaterializationResult::Success &&
              !files.FileExist(qualityBc7LooseTemporaryPath),
          "materialize the production Color-profile BC7 VTEX for rapid quality retargeting");
    Check(materializer.Materialize(runtimeRecord, runtimeTexture, artifactSource, qualityReplacementLoosePath,
                                   qualityReplacementLooseTemporaryPath) == assets::LooseMaterializationResult::Success &&
              !files.FileExist(qualityReplacementLooseTemporaryPath),
          "materialize a second identical streamable VTEX path for quality-aware generation cutover");

    assets::BuildOutput alternatePublishedBuild;
    assets::DependencyRecord alternateRuntimeRecord;
    Check(BuildAndPublish(buildSystem, dependencyIndex, alternateJpegRequest, alternatePublishedBuild) &&
              alternatePublishedBuild.buildFingerprint == alternateJpegBuild.buildFingerprint &&
              dependencyIndex.Find(runtimeTexture, alternateRuntimeRecord) == assets::IndexResult::Success &&
              materializer.Materialize(alternateRuntimeRecord, runtimeTexture, artifactSource, replacementLoosePath,
                                       replacementLooseTemporaryPath) == assets::LooseMaterializationResult::Success &&
              !files.FileExist(replacementLooseTemporaryPath) && dependencyIndex.Save() == assets::IndexResult::Success,
          "materialize a distinct committed VTEX generation for same-identity resident replacement");

    assets::PackageManifest manifest;
    manifest.packageId = 0x5458545356435003ull;
    Check(manifest.AddRoot({runtimeTexture, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success, "select the same indexed VTEX as the package root");
    assets::PackagePlanner packagePlanner;
    assets::PackageBuildPlan packagePlan;
    Check(packagePlanner.Prepare(manifest, dependencyIndex, packagePlan) == assets::PackagingResult::Success, "prepare VPAK directly from the dependency index");
    PackagePathContext packagePathContext{runtimeTexture, RuntimeTexturePath};
    assets::DerivedDataPackageArtifactReader artifactReader(artifactSource);
    const assets::PackageAssemblyCallbacks packageCallbacks{&ResolvePackagePath, &packagePathContext, &assets::DerivedDataPackageArtifactReader::ReadCallback, &artifactReader};
    assets::PackageAssembler packageAssembler;
    Check(packageAssembler.Publish(packagePlan, packagePath, packageTemporaryPath, packageCallbacks) == assets::PackagingResult::Success && !files.FileExist(packageTemporaryPath),
          "assemble and safely publish VPAK from the same immutable JPEG artifact set");
    artifactReader.Reset();

    streaming::ResourceStreamer& streamer = streamingService->GetStreamer();
    Check(streamer.RegisterLoose({qualityBc7Texture, qualityBc7LoosePath, {}, 0, 0}),
          "rapid quality proof registers the production Color-profile BC7 VTEX");
    Check(ExerciseTextureQualityRapidRetarget(qualityBc7Texture, streamer, *frames, clock, *renderingService),
          "BC7 wanted-mip QoS survives consecutive-frame live retargets while transitions are in flight");
    Check(streamer.UnregisterLoose(qualityBc7Texture.GetPath()), "rapid BC7 quality proof loose identity unregisters cleanly");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "rapid BC7 quality proof resource pipeline drain");

    Check(streamer.RegisterLoose({qualityTexture, loosePath, {}, 0, 0}),
          "quality QoS proof registers the real streamable JPEG VTEX");
    Check(ExerciseTextureQualityQos(qualityTexture, streamer, *frames, clock, *renderingService,
                                   qualityReplacementLoosePath, jpegSourceFingerprint),
          "wanted-mip QoS aggregates shared demands, promotes/demotes with byte accounting, and preserves target quality across generation cutover");
    Check(streamer.UnregisterLoose(qualityTexture.GetPath()), "quality QoS proof loose identity unregisters cleanly");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "quality QoS proof resource pipeline drain");

    Check(streamer.RegisterLoose({runtimeTexture, loosePath, {}, 0, 0}), "materialized loose VTEX generation registration");
    Check(ExerciseTextureReplacement(runtimeTexture, streamer, *frames, clock, *renderingService, replacementLoosePath,
                                     jpegSourceFingerprint),
          "real-JPEG loose VTEX replaces its CPU generation on the same stable GPU residency and retires through real fences");
    Check(streamer.UnregisterLoose(runtimeTexture.GetPath()), "loose VTEX generation removal");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "loose VTEX resource pipeline drain");

    Check(streamer.RegisterLoose({retryTexture, loosePath, {}, 0, 0}) &&
              streamer.RegisterLoose({retryBlockerTexture, loosePath, {}, 0, 0}),
          "P3 retry proof registers two logical VTEX identities over the same immutable initial loose payload");
    resources::PipelineRequest lifecycleRequest = streamer.Request(retryTexture, resources::LoadPriority::High);
    Check(lifecycleRequest.TryWait(10'000) && lifecycleRequest.HasLoaded(), "P4 lifecycle identity proof loads a stable VTEX generation");
    resources::ResourceHandle lifecycleResource = lifecycleRequest.Acquire();
    Check(lifecycleResource.IsValid() && ExerciseTextureLifecycleSessions(lifecycleResource),
          "P4 texture demand and uploader request identities cannot ABA across abandon/reinitialize");
    lifecycleResource.Reset();
    lifecycleRequest.Reset();
    Check(ExerciseTextureReplacementRetry(retryTexture, retryBlockerTexture, streamer, *frames, clock, *renderingService,
                                          replacementLoosePath, jpegSourceFingerprint),
          "P3 failed VTEX replacement preserves the old resident and explicitly retries the retained candidate on the same GPU identity");
    Check(streamer.UnregisterLoose(retryTexture.GetPath()) && streamer.UnregisterLoose(retryBlockerTexture.GetPath()),
          "P3 retry proof loose identities unregister cleanly");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "P3 retry proof resource pipeline drain");

    Check(streamer.RegisterLoose({boundedFirstTexture, loosePath, {}, 0, 0}) &&
              streamer.RegisterLoose({boundedSecondTexture, loosePath, {}, 0, 0}) &&
              streamer.RegisterLoose({boundedBlockerTexture, loosePath, {}, 0, 0}),
          "P4 retained-candidate bound registers three logical VTEX identities over the same immutable initial payload");
    Check(ExerciseTextureReplacementRetentionBound(boundedFirstTexture, boundedSecondTexture, boundedBlockerTexture, streamer, *frames, clock,
                                                   *renderingService, replacementLoosePath),
          "P4 failed replacement retention is GPU-byte bounded, preserves retained no-reupload retry, and recovers overflow explicitly");
    Check(streamer.UnregisterLoose(boundedFirstTexture.GetPath()) && streamer.UnregisterLoose(boundedSecondTexture.GetPath()) && streamer.UnregisterLoose(boundedBlockerTexture.GetPath()),
          "P4 retained-candidate bound loose identities unregister cleanly");
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "P4 retained-candidate bound resource pipeline drain");

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

    Check(renderingService->GetTextureResidency().GetStats().residencyRecords == 0 && renderingService->GetTextureResidency().GetStats().liveDemands == 0,
          "texture runtime has no live residency or demand at shutdown");
    Check(renderingService->GetMaterialBindings().GetStats().bindings == 0 && renderingService->GetMaterialResidency().GetStats().residencyRecords == 0 &&
              renderingService->GetMaterialResidency().GetStats().liveDemands == 0 && renderingService->GetMaterialMaterializer().GetStats().activeOperations == 0 &&
              renderingService->GetMaterialResources().GetStats().activeOperations == 0,
          "material service chain reaches zero state before renderer shutdown");
    Check(artifactSource.Shutdown(), "derived-data artifact source shutdown");
    Check(dependencyIndex.Shutdown(), "texture dependency index shutdown");
    Check(textureCompiler.Shutdown(), "texture compiler unregistration and shutdown");
    Check(buildSystem.Shutdown(), "texture BuildSystem shutdown");
    Check(renderingService->GetTextureResidency().GetStats().residency.pendingGenerationRetirements == 0,
          "all texture generation-retirement evidence is acknowledged before graceful renderer shutdown");
    Check(host.Shutdown(&hostFailure), "engine graph and device shutdown");
    Check(RunUndeliveredGenerationRetirementShutdownSubprocess(arguments[0]),
          "unacknowledged texture generation-retirement evidence fails graceful shutdown explicitly and still abandons backend-scoped observations safely");
    Check(VerifyMaterialInitializationRollback(renderingConfig), "material runtime initialization failure rolls back every earlier renderer/device owner");

    vanguard::diagnostics::Shutdown();

    if (g_failures == 0)
        std::printf("[textureResidencyServiceTests] source/DDC/loose/package/GPU texture integration passed.\n");
    return g_failures == 0 ? 0 : 1;
}
