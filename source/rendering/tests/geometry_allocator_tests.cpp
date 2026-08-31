#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rendering/geometry_allocator.hpp>
#include <vanguard/rendering/geometry_upload.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/mesh_geometry_upload.hpp>
#include <vanguard/rendering/mesh_lod_geometry_uploader.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>
#include <vanguard/rendering/texture_uploader.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    namespace gpu = vanguard::rhi;
    namespace meshes = vanguard::meshes;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace textures = vanguard::textures;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    inline constexpr resources::ResourceTypeId TestMaterialType = vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T');

    class TestMaterialResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return TestMaterialType;
        }
    };

    template <typename T> [[nodiscard]] T* AllocateResource() noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(vanguard::memory::PoolId::Resources, sizeof(T), alignof(T));
        return block ? ::new (block.address) T() : nullptr;
    }

    template <typename T> void DeleteResource(T* const resource) noexcept
    {
        if (resource == nullptr)
            return;
        resource->~T();
        vanguard::memory::MemoryBlock block{resource, sizeof(T), vanguard::memory::PoolId::Resources};
        vanguard::memory::Free(block);
    }

    void BeginMaterialLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        if (!registry.BeginLoading(request))
            return;
        TestMaterialResource* const material = AllocateResource<TestMaterialResource>();
        if (material != nullptr && registry.Publish(request, material))
            return;
        DeleteResource(material);
        static_cast<void>(registry.Fail(request, resources::Failure::OutOfMemory));
    }

    void BeginDeferredMeshLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        static_cast<void>(registry.BeginLoading(request));
    }

    void BeginDeferredTextureLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        static_cast<void>(registry.BeginLoading(request));
    }

    void DestroyMaterial(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<TestMaterialResource*>(resource));
    }

    void DestroyMesh(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<meshes::MeshResourceObject*>(resource));
    }

    void DestroyTexture(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<textures::TextureResourceObject*>(resource));
    }

    [[nodiscard]] bool WriteFile(const vanguard::filesystem::AbsolutePath& path, const ByteArray& bytes) noexcept
    {
        auto writer = vanguard::filesystem::GetManager().CreateFileWriter(path, vanguard::filesystem::FOF_Buffered);
        if (!writer)
            return false;
        writer->Serialize(const_cast<vanguard::u8*>(bytes.TypedData()), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }

    [[nodiscard]] bool BuildMeshUploadFixture(ByteArray& document, meshes::MeshFile& mesh) noexcept
    {
        std::array<vanguard::u8, 24> positions0{};
        std::array<vanguard::u8, 24> positions1{};
        std::array<vanguard::u8, 16> texcoords0{};
        std::array<vanguard::u8, 16> texcoords1{};
        std::array<vanguard::u8, 12> indices{};
        for (vanguard::u32 index = 0; index < positions0.size(); ++index)
        {
            positions0[index] = static_cast<vanguard::u8>(1u + index);
            positions1[index] = static_cast<vanguard::u8>(41u + index);
        }
        for (vanguard::u32 index = 0; index < texcoords0.size(); ++index)
        {
            texcoords0[index] = static_cast<vanguard::u8>(81u + index);
            texcoords1[index] = static_cast<vanguard::u8>(111u + index);
        }
        for (vanguard::u32 index = 0; index < indices.size(); ++index)
            indices[index] = static_cast<vanguard::u8>(151u + index);

        const std::array<meshes::BufferBuildRecord, 3> buffers{{{10, meshes::BufferKind::Vertex, 12, 48}, {11, meshes::BufferKind::Vertex, 8, 32}, {20, meshes::BufferKind::Index, 2, 12}}};
        const meshes::PageFlags pageFlags = meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload;
        const std::array<meshes::PageBuildRecord, 5> pages{{{10, 0, positions0.data(), positions0.size(), 4, pageFlags},
                                                            {10, 24, positions1.data(), positions1.size(), 4, pageFlags},
                                                            {11, 0, texcoords0.data(), texcoords0.size(), 4, pageFlags},
                                                            {11, 16, texcoords1.data(), texcoords1.size(), 4, pageFlags},
                                                            {20, 0, indices.data(), indices.size(), 4, pageFlags}}};
        const std::array<meshes::VertexLayoutBuildRecord, 1> layouts{{{1}}};
        const std::array<meshes::VertexStreamBuildRecord, 2> streams{{{1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12},
                                                                      {1, meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, 1, 11, 0, 8}}};
        const std::array<meshes::MaterialSlotBuildRecord, 1> materials{{{1, 0x4d4154455249414cull,
                                                                         vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("materials/geometry_test.vmat"),
                                                                                                                vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T'))}}};
        const std::array<meshes::LodBuildRecord, 1> lods{{{0, 1.0f}}};
        meshes::Bounds bounds;
        bounds.minimum[0] = bounds.minimum[1] = bounds.minimum[2] = -1.0f;
        bounds.maximum[0] = bounds.maximum[1] = bounds.maximum[2] = 1.0f;
        bounds.sphereRadius = 1.75f;
        const std::array<meshes::SubmeshBuildRecord, 1> submeshes{
            {{0x1000, 0x2000, 0, 1, 1, 20, meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::CastsShadow, 1, 2, 1, 3, bounds}}};

        meshes::BuildDescription description;
        description.kind = meshes::MeshKind::Static;
        description.name = 0x47454f4d45545259ull;
        description.bounds = bounds;
        description.sourceFingerprint = vanguard::crypto::Sha256("geometry-upload-fixture", 23);
        description.buffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
        description.pages = {pages.data(), static_cast<vanguard::u32>(pages.size())};
        description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
        description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
        description.materialSlots = {materials.data(), static_cast<vanguard::u32>(materials.size())};
        description.lods = {lods.data(), static_cast<vanguard::u32>(lods.size())};
        description.submeshes = {submeshes.data(), static_cast<vanguard::u32>(submeshes.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        if (meshes::WriteMesh(writer, description) != meshes::Result::Success)
            return false;
        vanguard::filesystem::MemoryFileReader reader(document, 0);
        return mesh.Open(reader) == meshes::Result::Success;
    }

    [[nodiscard]] bool BuildTextureUploadFixture(ByteArray& document) noexcept
    {
        std::array<vanguard::u8, 64> pixels{};
        for (vanguard::u32 index = 0; index < pixels.size(); ++index)
            pixels[index] = static_cast<vanguard::u8>(index * 17u + 3u);

        const std::array<textures::SubresourceBuildRecord, 1> subresources{{
            {0, 0, 0, pixels.data(), pixels.size(), 16, 64},
        }};
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
        description.sourceFingerprint = vanguard::crypto::Sha256("texture-uploader-fixture", 24);
        description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        return textures::WriteTexture(writer, description) == textures::Result::Success;
    }

    [[nodiscard]] bool BuildTextureTransitionFixture(ByteArray& document) noexcept
    {
        std::array<vanguard::u8, 256> mip0{};
        std::array<vanguard::u8, 64> mip1{};
        std::array<vanguard::u8, 16> mip2{};
        std::array<vanguard::u8, 4> mip3{};
        for (vanguard::u32 index = 0; index < mip0.size(); ++index)
            mip0[index] = static_cast<vanguard::u8>(index * 13u + 1u);
        for (vanguard::u32 index = 0; index < mip1.size(); ++index)
            mip1[index] = static_cast<vanguard::u8>(index * 11u + 2u);
        for (vanguard::u32 index = 0; index < mip2.size(); ++index)
            mip2[index] = static_cast<vanguard::u8>(index * 7u + 3u);
        for (vanguard::u32 index = 0; index < mip3.size(); ++index)
            mip3[index] = static_cast<vanguard::u8>(index * 5u + 4u);
        const std::array<textures::SubresourceBuildRecord, 4> subresources{{
            {0, 0, 0, mip0.data(), mip0.size(), 32, 256},
            {1, 0, 0, mip1.data(), mip1.size(), 16, 64},
            {2, 0, 0, mip2.data(), mip2.size(), 8, 16},
            {3, 0, 0, mip3.data(), mip3.size(), 4, 4},
        }};
        textures::BuildDescription description;
        description.dimension = textures::TextureDimension::Texture2D;
        description.format = textures::PixelFormat::R8G8B8A8UNorm;
        description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
        description.width = 8;
        description.height = 8;
        description.depth = 1;
        description.arrayLayers = 1;
        description.mipCount = 4;
        description.mipTailFirstLevel = 2;
        description.sourceFingerprint = vanguard::crypto::Sha256("texture-transition-fixture", 26);
        description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        return textures::WriteTexture(writer, description) == textures::Result::Success;
    }

    [[nodiscard]] bool PumpTextureUpload(rendering::TextureUploader& uploader, const rendering::TextureUploadRequestId request, rendering::TextureUploadFailure& failure) noexcept
    {
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            const rendering::TextureUploadState state = uploader.GetState(request);
            if (state == rendering::TextureUploadState::Submitted || state == rendering::TextureUploadState::Failed)
                return true;
            if (!uploader.Tick(&failure))
                return false;
            std::this_thread::yield();
        }
        return false;
    }

    [[nodiscard]] bool RunTextureUploaderRegressionTests(const resources::ResourceHandle& texture,
                                                          const resources::ResourceHandle& multiWindowTexture,
                                                          gpu::Failure& rhiFailure) noexcept
    {
        const auto VerifyPersistentShaderRead = [&rhiFailure](const gpu::TextureRef candidate) noexcept
        {
            const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
            for (vanguard::u32 pass = 0; pass < 2; ++pass)
            {
                const gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::CopySync, 0x5458535441544500ull + pass, &rhiFailure);
                if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !gpu::TransitionTexture(candidate, shaderRead, gpu::ResourceState::CopySource, {}, &rhiFailure))
                    return false;
                gpu::UnbindCommandList();
                const gpu::CommandListRef submissions[] = {commands};
                gpu::GpuFence completion;
                if (!gpu::CloseAndSubmitCommandLists("texture persistent shader-read state", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure) ||
                    !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
                    return false;
            }
            return true;
        };

        rendering::TextureUploadFailure failure;
        rendering::TextureUploaderConfig invalidConfig;
        invalidConfig.maximumAcquisitionStartsPerTick = 0;
        rendering::TextureUploader invalidUploader;
        if (invalidUploader.Initialize(invalidConfig, &failure) || failure.code != rendering::TextureUploadFailureCode::InvalidConfiguration)
            return false;

        rendering::TextureUploaderConfig throttledConfig;
        throttledConfig.maximumRequests = 2;
        throttledConfig.maximumAcquisitionStartsPerTick = 1;
        throttledConfig.maximumCandidatesPerBatch = 2;
        throttledConfig.maximumWritesPerBatch = 2;
        throttledConfig.maximumBytesPerBatch = 128;
        throttledConfig.maximumPendingCandidateBytes = 128;
        throttledConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader throttledUploader;
        rendering::TextureUploadRequestId firstThrottleRequest;
        rendering::TextureUploadRequestId secondThrottleRequest;
        if (!throttledUploader.Initialize(throttledConfig, &failure) ||
            !throttledUploader.RequestMipTail(texture, {11, 1}, firstThrottleRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !throttledUploader.RequestMipTail(texture, {12, 1}, secondThrottleRequest, vanguard::io::eAsyncPriority_Streaming, &failure) || !throttledUploader.Tick(&failure) ||
            throttledUploader.GetStats().acquisitionWindowsStarted != 1 || !throttledUploader.Tick(&failure) || throttledUploader.GetStats().acquisitionWindowsStarted != 2)
            return false;
        if (!PumpTextureUpload(throttledUploader, firstThrottleRequest, failure) || !PumpTextureUpload(throttledUploader, secondThrottleRequest, failure) ||
            throttledUploader.GetState(firstThrottleRequest) != rendering::TextureUploadState::Submitted ||
            throttledUploader.GetState(secondThrottleRequest) != rendering::TextureUploadState::Submitted)
            return false;
        rendering::SubmittedTextureCandidate firstThrottleCandidate;
        rendering::SubmittedTextureCandidate secondThrottleCandidate;
        if (!throttledUploader.TakeSubmitted(firstThrottleRequest, firstThrottleCandidate, &failure) ||
            !throttledUploader.TakeSubmitted(secondThrottleRequest, secondThrottleCandidate, &failure) ||
            !gpu::WaitForGpuFence(firstThrottleCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure) ||
            !gpu::WaitForGpuFence(secondThrottleCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return false;
        firstThrottleCandidate.Reset();
        secondThrottleCandidate.Reset();
        if (!throttledUploader.Shutdown(&failure))
            return false;

        rendering::TextureUploaderConfig normalConfig;
        normalConfig.maximumRequests = 1;
        normalConfig.maximumAcquisitionStartsPerTick = 1;
        normalConfig.maximumCandidatesPerBatch = 1;
        normalConfig.maximumWritesPerBatch = 1;
        normalConfig.maximumBytesPerBatch = 64;
        normalConfig.maximumPendingCandidateBytes = 64;
        normalConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader normalUploader;
        rendering::TextureUploadRequestId request;
        if (!normalUploader.Initialize(normalConfig, &failure) || !normalUploader.RequestMipTail(texture, {21, 1}, request, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(normalUploader, request, failure) || normalUploader.GetState(request) != rendering::TextureUploadState::Submitted)
            return false;

        rendering::TextureUploadRequestId coalesced;
        if (!normalUploader.RequestMipTail(texture, {21, 1}, coalesced, vanguard::io::eAsyncPriority_Streaming, &failure) || coalesced != request ||
            normalUploader.GetStats().requestsCoalesced != 1 || normalUploader.GetStats().liveRequests != 1 || normalUploader.GetStats().batchesSubmitted != 1)
            return false;
        rendering::SubmittedTextureCandidate submitted;
        if (!normalUploader.TakeSubmitted(request, submitted, &failure) || !submitted.IsValid() || !gpu::WaitForGpuFence(submitted.copyCompletion, 5'000'000'000ull, &rhiFailure) ||
            !VerifyPersistentShaderRead(submitted.texture.GetRef()))
            return false;
        submitted.Reset();
        if (!normalUploader.Shutdown(&failure))
            return false;

        rendering::TextureUploaderConfig multiWindowConfig = normalConfig;
        multiWindowConfig.maximumAcquisitionWindowSubresources = 1;
        rendering::TextureUploader multiWindowUploader;
        rendering::TextureUploadRequestId multiWindowRequest;
        if (!multiWindowUploader.Initialize(multiWindowConfig, &failure) ||
            !multiWindowUploader.RequestMipTail(multiWindowTexture, {23, 1}, multiWindowRequest,
                                                vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(multiWindowUploader, multiWindowRequest, failure) ||
            multiWindowUploader.GetState(multiWindowRequest) != rendering::TextureUploadState::Submitted)
            return false;
        const rendering::TextureUploaderStats multiWindowStats = multiWindowUploader.GetStats();
        rendering::SubmittedTextureCandidate multiWindowCandidate;
        if (multiWindowStats.acquisitionWindowsStarted != 2 || multiWindowStats.batchesSubmitted != 2 ||
            multiWindowStats.writesSubmitted != 2 || multiWindowStats.bytesSubmitted != 20 ||
            !multiWindowUploader.TakeSubmitted(multiWindowRequest, multiWindowCandidate, &failure) ||
            !multiWindowCandidate.IsValid() || multiWindowCandidate.firstResidentMip != 2 ||
            multiWindowCandidate.residentMipCount != 2 || multiWindowCandidate.subresourceCount != 2 ||
            multiWindowCandidate.sourceBytesUploaded != 20 ||
            !gpu::WaitForGpuFence(multiWindowCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return false;
        multiWindowCandidate.Reset();
        if (!multiWindowUploader.Shutdown(&failure))
            return false;

        // Source-byte admission and retained physical-memory admission are separate. Two multi-window
        // requests may reserve their small cooked tails together, but an exact one-texture GPU budget must
        // retain only one candidate until ownership of that candidate leaves the uploader.
        gpu::TextureDesc budgetProbeDesc;
        budgetProbeDesc.extent = {2, 2, 1};
        budgetProbeDesc.format = gpu::Format::R8G8B8A8UNorm;
        budgetProbeDesc.mipCount = 2;
        budgetProbeDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        budgetProbeDesc.initialState = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        gpu::Texture budgetProbe(gpu::AdoptReference, gpu::CreateTexture(budgetProbeDesc, {}, &rhiFailure));
        if (!budgetProbe.IsValid())
            return false;
        const vanguard::u64 probedCandidateBytes = gpu::GetMemoryRequirements(budgetProbe.GetRef()).size;
        if (probedCandidateBytes <= 1)
            return false;
        const vanguard::u64 exactCandidateBytes = probedCandidateBytes;
        budgetProbe.Reset();

        rendering::TextureUploaderConfig physicalBudgetConfig = multiWindowConfig;
        physicalBudgetConfig.maximumRequests = 2;
        physicalBudgetConfig.maximumAcquisitionStartsPerTick = 2;
        physicalBudgetConfig.maximumCandidatesPerBatch = 2;
        physicalBudgetConfig.maximumWritesPerBatch = 2;
        physicalBudgetConfig.maximumCandidateGpuBytesPerBatch = exactCandidateBytes * 2;
        physicalBudgetConfig.maximumPendingCandidateGpuBytes = exactCandidateBytes;
        rendering::TextureUploader physicalBudgetUploader;
        rendering::TextureUploadRequestId firstPhysicalRequest;
        rendering::TextureUploadRequestId secondPhysicalRequest;
        if (!physicalBudgetUploader.Initialize(physicalBudgetConfig, &failure) ||
            !physicalBudgetUploader.RequestMipTail(multiWindowTexture, {41, 1}, firstPhysicalRequest,
                                                   vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !physicalBudgetUploader.RequestMipTail(multiWindowTexture, {42, 1}, secondPhysicalRequest,
                                                   vanguard::io::eAsyncPriority_Streaming, &failure))
            return false;
        for (vanguard::u32 poll = 0; poll < 100'000 && physicalBudgetUploader.GetState(firstPhysicalRequest) == rendering::TextureUploadState::Acquiring; ++poll)
        {
            if (!physicalBudgetUploader.Tick(&failure))
                return false;
            std::this_thread::yield();
        }
        const rendering::TextureUploaderStats oneRetainedStats = physicalBudgetUploader.GetStats();
        if ((physicalBudgetUploader.GetState(firstPhysicalRequest) != rendering::TextureUploadState::Submitted &&
             physicalBudgetUploader.GetState(firstPhysicalRequest) != rendering::TextureUploadState::ReadyToInstall) ||
            physicalBudgetUploader.GetState(secondPhysicalRequest) != rendering::TextureUploadState::Acquiring ||
            oneRetainedStats.pendingCandidateBytes != 40 || oneRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes ||
            oneRetainedStats.candidateGpuBytesSubmitted != exactCandidateBytes)
            return false;
        for (vanguard::u32 tick = 0; tick < 4; ++tick)
            if (!physicalBudgetUploader.Tick(&failure))
                return false;
        const rendering::TextureUploaderStats stillOneRetainedStats = physicalBudgetUploader.GetStats();
        if (physicalBudgetUploader.GetState(secondPhysicalRequest) != rendering::TextureUploadState::Acquiring ||
            stillOneRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes ||
            stillOneRetainedStats.candidateGpuBytesSubmitted != exactCandidateBytes)
            return false;

        rendering::SubmittedTextureCandidate firstPhysicalCandidate;
        if (!physicalBudgetUploader.TakeSubmitted(firstPhysicalRequest, firstPhysicalCandidate, &failure) ||
            physicalBudgetUploader.GetStats().pendingCandidateBytes != 20 || physicalBudgetUploader.GetStats().pendingCandidateGpuBytes != 0 ||
            !PumpTextureUpload(physicalBudgetUploader, secondPhysicalRequest, failure) ||
            physicalBudgetUploader.GetState(secondPhysicalRequest) != rendering::TextureUploadState::Submitted)
            return false;
        const rendering::TextureUploaderStats secondRetainedStats = physicalBudgetUploader.GetStats();
        rendering::SubmittedTextureCandidate secondPhysicalCandidate;
        if (secondRetainedStats.pendingCandidateBytes != 20 || secondRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes ||
            !physicalBudgetUploader.TakeSubmitted(secondPhysicalRequest, secondPhysicalCandidate, &failure) ||
            !gpu::WaitForGpuFence(firstPhysicalCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure) ||
            !gpu::WaitForGpuFence(secondPhysicalCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return false;
        firstPhysicalCandidate.Reset();
        secondPhysicalCandidate.Reset();
        if (physicalBudgetUploader.GetStats().pendingCandidateBytes != 0 || physicalBudgetUploader.GetStats().pendingCandidateGpuBytes != 0 ||
            !physicalBudgetUploader.Shutdown(&failure))
            return false;

        rendering::TextureUploaderConfig impossiblePhysicalBudgetConfig = multiWindowConfig;
        impossiblePhysicalBudgetConfig.maximumPendingCandidateGpuBytes = exactCandidateBytes - 1;
        rendering::TextureUploader impossiblePhysicalBudgetUploader;
        rendering::TextureUploadRequestId impossiblePhysicalRequest;
        if (!impossiblePhysicalBudgetUploader.Initialize(impossiblePhysicalBudgetConfig, &failure) ||
            !impossiblePhysicalBudgetUploader.RequestMipTail(multiWindowTexture, {43, 1}, impossiblePhysicalRequest,
                                                             vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(impossiblePhysicalBudgetUploader, impossiblePhysicalRequest, failure) ||
            impossiblePhysicalBudgetUploader.GetState(impossiblePhysicalRequest) != rendering::TextureUploadState::Failed)
            return false;
        rendering::TextureUploadFailure impossiblePhysicalFailure;
        if (!impossiblePhysicalBudgetUploader.GetRequestFailure(impossiblePhysicalRequest, impossiblePhysicalFailure) ||
            impossiblePhysicalFailure.code != rendering::TextureUploadFailureCode::CapacityExceeded ||
            impossiblePhysicalBudgetUploader.GetStats().pendingCandidateGpuBytes != 0 ||
            !impossiblePhysicalBudgetUploader.Cancel(impossiblePhysicalRequest, &failure) ||
            !impossiblePhysicalBudgetUploader.Shutdown(&failure))
            return false;

        rendering::TextureUploaderConfig cancelledConfig = normalConfig;
        cancelledConfig.maximumReadyCandidates = 1;
        rendering::TextureUploader cancelledUploader;
        rendering::TextureUploadRequestId cancelledRequest;
        if (!cancelledUploader.Initialize(cancelledConfig, &failure) ||
            !cancelledUploader.RequestMipTail(texture, {22, 1}, cancelledRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(cancelledUploader, cancelledRequest, failure) || cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Submitted ||
            !cancelledUploader.Cancel(cancelledRequest, &failure) || cancelledUploader.GetStats().liveRequests != 1)
            return false;
        for (vanguard::u32 poll = 0; poll < 100'000 && cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Invalid; ++poll)
        {
            if (!cancelledUploader.Tick(&failure))
                return false;
            std::this_thread::yield();
        }
        if (cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Invalid || !cancelledUploader.Shutdown(&failure))
            return false;

        rendering::TextureUploaderConfig hardCapConfig = normalConfig;
        hardCapConfig.maximumBytesPerBatch = 63;
        rendering::TextureUploader hardCapUploader;
        rendering::TextureUploadRequestId hardCapRequest;
        if (!hardCapUploader.Initialize(hardCapConfig, &failure) || !hardCapUploader.RequestMipTail(texture, {31, 1}, hardCapRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(hardCapUploader, hardCapRequest, failure) || hardCapUploader.GetState(hardCapRequest) != rendering::TextureUploadState::Failed)
            return false;
        rendering::TextureUploadFailure requestFailure;
        const rendering::TextureUploaderStats hardCapStats = hardCapUploader.GetStats();
        if (!hardCapUploader.GetRequestFailure(hardCapRequest, requestFailure) || requestFailure.code != rendering::TextureUploadFailureCode::CapacityExceeded ||
            hardCapStats.batchesSubmitted != 0 || hardCapStats.bytesSubmitted != 0 || !hardCapUploader.Cancel(hardCapRequest, &failure) || !hardCapUploader.Shutdown(&failure))
            return false;
        return true;
    }

    [[nodiscard]] bool SubmitFences(const vanguard::u64 name, gpu::ResidencyFenceSet& fences, gpu::Failure& failure) noexcept
    {
        fences = {};
        const gpu::CommandListType types[] = {gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            gpu::CommandListRef list = gpu::CreateCommandList(types[index], name + index, &failure);
            if (!list || !gpu::BindCommandList(list, &failure))
                return false;
            gpu::UnbindCommandList();
            const gpu::CommandListRef lists[] = {list};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("geometry allocator retirement", {lists, 1}, gpu::CommandListSyncType::None, completion, &failure))
                return false;
            fences.Include(completion);
        }
        return true;
    }

    [[nodiscard]] bool WaitFences(const gpu::ResidencyFenceSet& fences, gpu::Failure& failure) noexcept
    {
        return gpu::WaitForGpuFence({gpu::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) &&
               gpu::WaitForGpuFence({gpu::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
               gpu::WaitForGpuFence({gpu::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
    }

    struct TextureRuntimeGpuSceneExecution
    {
        rendering::GpuSceneRuntime* runtime = nullptr;
    };

    rendering::RenderFrameExecutionStatus ExecuteTextureRuntimeGpuSceneTick(rendering::RenderFrameTickContext& context,
                                                                             void* const userData) noexcept
    {
        auto* const execution = static_cast<TextureRuntimeGpuSceneExecution*>(userData);
        rendering::GpuSceneRuntimeFailure failure;
        return execution != nullptr && execution->runtime != nullptr && execution->runtime->Publish(context, &failure)
                   ? rendering::RenderFrameExecutionStatus::Success()
                   : rendering::RenderFrameExecutionStatus::Failure(
                         failure.message != nullptr ? failure.message : "texture runtime GPU Scene contribution failed");
    }

    rendering::RenderFrameExecutionStatus ExecuteTextureRuntimeGpuSceneFrame(rendering::RenderFrameContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    [[nodiscard]] bool RunTests(gpu::Failure& failure) noexcept
    {
        rendering::GeometryAllocator allocator;
        rendering::GeometryAllocatorFailure allocatorFailure;
        rendering::GeometryAllocatorConfig config;
        config.verticesPerArena = 64;
        config.indexBytesPerArena = 128;
        config.maximumIndexArenaBytes = 4096;
        config.maximumVertexArenas = 8;
        config.maximumIndexArenas = 8;
        config.maximumAllocations = 32;
        config.retirementEpochCount = 4;
        config.initialRetirementsPerEpoch = 8;
        if (!allocator.Initialize(config, &allocatorFailure))
            return false;
        rendering::GeometryUploader uploader;
        rendering::GeometryUploadFailure uploadFailure;
        rendering::GeometryUploadConfig uploadConfig;
        uploadConfig.bytesPerSegment = 1024;
        uploadConfig.maximumOverflowBytes = 4096;
        uploadConfig.maximumGeometriesPerBatch = 8;
        uploadConfig.maximumCopiesPerBatch = 32;
        if (!uploader.Initialize(allocator, uploadConfig, &uploadFailure))
            return false;

        const gpu::VertexBindingDesc bindings[] = {{0, 12, gpu::VertexInputRate::PerVertex, 1}, {1, 20, gpu::VertexInputRate::PerVertex, 1}};
        vanguard::crypto::Digest256 fingerprint;
        fingerprint.bytes[0] = 1;
        rendering::GeometryAllocationRequest request;
        request.vertexLayout = {fingerprint, {bindings, 2}};
        request.vertexCount = 10;
        request.indexCount = 6;

        rendering::GeometryReservation first;
        rendering::GeometryReservation second;
        if (!allocator.Reserve(request, first, &allocatorFailure))
            return false;
        request.vertexCount = 8;
        request.indexCount = 8;
        if (!allocator.Reserve(request, second, &allocatorFailure) || first.vertex.arena != second.vertex.arena || first.index.arena != second.index.arena ||
            second.vertex.firstVertex != 12 || second.index.firstIndex != 8)
            return false;

        rendering::GeometryVertexArenaView vertexArena;
        rendering::GeometryIndexArenaView indexArena;
        if (!allocator.GetVertexArena(first.vertex.arena, vertexArena) || vertexArena.bindingCount != 2 || vertexArena.vertexCapacity != 64 || !vertexArena.buffers[0] ||
            !vertexArena.buffers[1] || vertexArena.dedicated || !allocator.GetIndexArena(first.index.arena, indexArena) || indexArena.format != gpu::IndexFormat::UInt16 ||
            indexArena.indexCapacity != 64 || !indexArena.buffer || indexArena.dedicated)
            return false;

        rendering::GeometryReservation reused;
        if (!allocator.Cancel(second, &allocatorFailure) || !allocator.Reserve(request, reused, &allocatorFailure) || reused.vertex.firstVertex != second.vertex.firstVertex ||
            reused.index.firstIndex != second.index.firstIndex || !allocator.Cancel(reused, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest otherLayout = request;
        otherLayout.vertexLayout.fingerprint.bytes[0] = 2;
        rendering::GeometryReservation otherLayoutReservation;
        if (!allocator.Reserve(otherLayout, otherLayoutReservation, &allocatorFailure) || otherLayoutReservation.vertex.arena == first.vertex.arena ||
            !allocator.Cancel(otherLayoutReservation, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest uint32Request = request;
        uint32Request.indexFormat = gpu::IndexFormat::UInt32;
        rendering::GeometryReservation uint32Reservation;
        if (!allocator.Reserve(uint32Request, uint32Reservation, &allocatorFailure) || uint32Reservation.index.arena == first.index.arena ||
            !allocator.Cancel(uint32Reservation, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest batchRequests[] = {request, request};
        batchRequests[1].vertexLayout.fingerprint = {};
        rendering::GeometryReservation batchReservations[2];
        const vanguard::u32 reservedBeforeBatch = allocator.GetStats().reservedAllocations;
        if (allocator.ReserveBatch({batchRequests, 2}, {batchReservations, 2}, &allocatorFailure) || batchReservations[0].IsValid() || batchReservations[1].IsValid() ||
            allocator.GetStats().reservedAllocations != reservedBeforeBatch)
            return false;

        rendering::GeometryAllocationRequest dedicatedRequest = request;
        dedicatedRequest.vertexCount = 65;
        dedicatedRequest.indexCount = 65;
        rendering::GeometryReservation dedicated;
        if (!allocator.Reserve(dedicatedRequest, dedicated, &allocatorFailure) || !allocator.GetVertexArena(dedicated.vertex.arena, vertexArena) || !vertexArena.dedicated ||
            !allocator.GetIndexArena(dedicated.index.arena, indexArena) || !indexArena.dedicated)
            return false;
        const rendering::GeometryUploadRequest oversizedUpload[] = {{dedicated}};
        rendering::GeometryUploadReservation oversizedStaging[1];
        if (!uploader.Begin({oversizedUpload, 1}, {oversizedStaging, 1}, &uploadFailure) || !oversizedStaging[0].IsValid() || uploader.GetStats().overflowBatches != 1 ||
            !uploader.Cancel(&uploadFailure))
            return false;
        const rendering::VertexArenaSetId oldVertexArena = dedicated.vertex.arena;
        const rendering::IndexArenaId oldIndexArena = dedicated.index.arena;
        if (!allocator.Cancel(dedicated, &allocatorFailure) || allocator.GetVertexArena(oldVertexArena, vertexArena) || allocator.GetIndexArena(oldIndexArena, indexArena))
            return false;

        rendering::GeometryReservation recycledDedicated;
        if (!allocator.Reserve(dedicatedRequest, recycledDedicated, &allocatorFailure) || recycledDedicated.vertex.arena.index != oldVertexArena.index ||
            recycledDedicated.vertex.arena.generation == oldVertexArena.generation || recycledDedicated.index.arena.index != oldIndexArena.index ||
            recycledDedicated.index.arena.generation == oldIndexArena.generation || !allocator.Cancel(recycledDedicated, &allocatorFailure))
            return false;

        rendering::GeometryReservation forged = first;
        ++forged.vertex.vertexCount;
        const rendering::GeometryUploadRequest forgedRequest[] = {{forged}};
        rendering::GeometryUploadReservation staging[1];
        if (uploader.Begin({forgedRequest, 1}, {staging, 1}, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::InvalidAllocationState)
            return false;

        const rendering::GeometryUploadRequest uploadRequest[] = {{first}};
        if (!uploader.Begin({uploadRequest, 1}, {staging, 1}, &uploadFailure) || staging[0].bindingCount != 2 || staging[0].vertexStreams[0].size != 120 ||
            staging[0].vertexStreams[1].size != 200 || staging[0].indices.size != 12)
            return false;
        const rendering::GeometryUploadReservation cancelledStaging = staging[0];
        if (!uploader.Cancel(&uploadFailure) || uploader.Complete(cancelledStaging, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::NoOpenBatch ||
            !allocator.ValidateReservation(first) || !uploader.Begin({uploadRequest, 1}, {staging, 1}, &uploadFailure))
            return false;
        for (vanguard::u32 binding = 0; binding < staging[0].bindingCount; ++binding)
            for (vanguard::u64 byte = 0; byte < staging[0].vertexStreams[binding].size; ++byte)
                static_cast<vanguard::u8*>(staging[0].vertexStreams[binding].destination)[byte] = static_cast<vanguard::u8>(17u + binding * 53u + byte);
        for (vanguard::u64 byte = 0; byte < staging[0].indices.size; ++byte)
            static_cast<vanguard::u8*>(staging[0].indices.destination)[byte] = static_cast<vanguard::u8>(201u + byte);

        rendering::GeometryPlacement placements[1];
        rendering::GeometryUploadResult uploadResult;
        if (uploader.Submit({placements, 1}, uploadResult, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::BatchNotReady)
            return false;
        rendering::GeometryUploadReservation forgedStaging = staging[0];
        ++forgedStaging.vertexStreams[0].size;
        if (uploader.Complete(forgedStaging, &uploadFailure) || !uploader.Complete(staging[0], &uploadFailure) || !uploader.Submit({placements, 1}, uploadResult, &uploadFailure) ||
            !uploadResult.completion.IsValid() || uploadResult.geometryCount != 1 || uploadResult.copyCount != 3 || uploadResult.destinationBufferCount != 3 ||
            uploadResult.uploadedBytes != 332 || !gpu::WaitForGpuFence(uploadResult.completion, 5'000'000'000ull, &failure) ||
            allocator.GetState(first.allocation) != rendering::GeometryAllocationState::Active)
            return false;
        const rendering::GeometryUploadStats uploadStats = uploader.GetStats();
        if (uploadStats.batchesSubmitted != 1 || uploadStats.geometriesUploaded != 1 || uploadStats.copiesRecorded != 3 || uploadStats.bytesUploaded != 332 ||
            uploadStats.stagingBytesCommitted != 3072)
            return false;
        const rendering::GeometryPlacement placement = placements[0];
        if (!allocator.Retire(placement, &allocatorFailure))
            return false;

        gpu::ResidencyFenceSet missingQueues;
        missingQueues.graphics = 1;
        if (allocator.SealRetirements(missingQueues, &allocatorFailure) || allocatorFailure.code != rendering::GeometryAllocatorFailureCode::MissingRetirementFence)
            return false;

        gpu::ResidencyFenceSet submitted;
        if (!SubmitFences(0x47454f4d45545200ull, submitted, failure) || !WaitFences(submitted, failure))
            return false;
        const gpu::ResidencyFenceSet future{submitted.graphics + 1u, submitted.compute + 1u, submitted.copy + 1u};
        if (!allocator.SealRetirements(future, &allocatorFailure) || allocator.Collect(&allocatorFailure) != 0)
            return false;
        gpu::ResidencyFenceSet completed;
        if (!SubmitFences(0x47454f4d45545300ull, completed, failure) || completed.graphics < future.graphics || completed.compute < future.compute || completed.copy < future.copy ||
            !WaitFences(completed, failure) || allocator.Collect(&allocatorFailure) != 1 || allocator.GetState(first.allocation) != rendering::GeometryAllocationState::Invalid)
            return false;

        rendering::GeometryReservation reclaimed;
        if (!allocator.Reserve(request, reclaimed, &allocatorFailure) || reclaimed.vertex.firstVertex != first.vertex.firstVertex || reclaimed.index.firstIndex != first.index.firstIndex ||
            !allocator.Cancel(reclaimed, &allocatorFailure))
            return false;

        ByteArray meshDocument{vanguard::memory::pools::Rendering::GetInstance()};
        ByteArray textureDocument{vanguard::memory::pools::Rendering::GetInstance()};
        ByteArray transitionTextureDocument{vanguard::memory::pools::Rendering::GetInstance()};
        meshes::MeshFile mesh;
        if (!BuildMeshUploadFixture(meshDocument, mesh) || !BuildTextureUploadFixture(textureDocument) || !BuildTextureTransitionFixture(transitionTextureDocument))
            return false;
        std::array<ByteArray, 5> pageBytes{{ByteArray(vanguard::memory::pools::Rendering::GetInstance()), ByteArray(vanguard::memory::pools::Rendering::GetInstance()),
                                            ByteArray(vanguard::memory::pools::Rendering::GetInstance()), ByteArray(vanguard::memory::pools::Rendering::GetInstance()),
                                            ByteArray(vanguard::memory::pools::Rendering::GetInstance())}};
        rendering::VerifiedMeshPagePayload pagePayloads[5];
        vanguard::filesystem::MemoryFileReader pageReader(meshDocument, 0);
        for (vanguard::u32 page = 0; page < 5; ++page)
        {
            pageBytes[page].Resize(static_cast<vanguard::u32>(mesh.GetPages()[page].byteSize));
            if (mesh.ReadPage(pageReader, page, pageBytes[page].TypedData(), pageBytes[page].Size()) != meshes::Result::Success)
                return false;
            pagePayloads[page] = {page, {pageBytes[page].TypedData(), pageBytes[page].Size()}};
        }

        rendering::PreparedMeshLodUpload prepared;
        rendering::MeshGeometryUploadFailure meshUploadFailure;
        rendering::VerifiedMeshPagePayload duplicatePages[5] = {pagePayloads[0], pagePayloads[0], pagePayloads[2], pagePayloads[3], pagePayloads[4]};
        const vanguard::u32 reservedBeforeInvalidPages = allocator.GetStats().reservedAllocations;
        if (rendering::PrepareMeshLodGeometryUpload(mesh, 0, {duplicatePages, 5}, allocator, uploader, prepared, &meshUploadFailure) ||
            meshUploadFailure.code != rendering::MeshGeometryUploadFailureCode::InvalidPageSet || prepared.IsValid() ||
            allocator.GetStats().reservedAllocations != reservedBeforeInvalidPages)
            return false;

        rendering::GeometryReservation blockingReservation;
        if (!allocator.Reserve(request, blockingReservation, &allocatorFailure))
            return false;
        const rendering::GeometryUploadRequest blockingRequest[] = {{blockingReservation}};
        rendering::GeometryUploadReservation blockingUpload[1];
        if (!uploader.Begin({blockingRequest, 1}, {blockingUpload, 1}, &uploadFailure) ||
            rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure) ||
            meshUploadFailure.code != rendering::MeshGeometryUploadFailureCode::UploadFailure || prepared.IsValid() || allocator.GetStats().reservedAllocations != 1 ||
            !uploader.Cancel(&uploadFailure) || !allocator.Cancel(blockingReservation, &allocatorFailure))
            return false;

        if (!rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure) || !prepared.IsValid() || prepared.pages.Size() != 5 ||
            prepared.geometries.Size() != 1 || prepared.submeshes.Size() != 1 || prepared.vertexBytes != 40 || prepared.indexBytes != 6 ||
            !rendering::CancelPreparedMeshLodUpload(allocator, uploader, prepared, &meshUploadFailure) || prepared.IsValid() || allocator.GetStats().reservedAllocations != 0)
            return false;

        if (!rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure))
            return false;
        if (!rendering::FillPreparedMeshLodUpload(uploader, prepared, &meshUploadFailure))
            return false;
        rendering::GeometryPlacement meshPlacement[1];
        rendering::GeometryUploadResult meshUploadResult;
        if (!uploader.Submit({meshPlacement, 1}, meshUploadResult, &uploadFailure) || !meshPlacement[0].IsValid() || meshUploadResult.geometryCount != 1 || meshUploadResult.copyCount != 3 ||
            meshUploadResult.uploadedBytes != 46 || !gpu::WaitForGpuFence(meshUploadResult.completion, 5'000'000'000ull, &failure))
            return false;
        prepared.Reset();
        if (!allocator.Retire(meshPlacement[0], &allocatorFailure))
            return false;
        gpu::ResidencyFenceSet meshRetirementFences;
        if (!SubmitFences(0x4d45534847454f00ull, meshRetirementFences, failure) || !WaitFences(meshRetirementFences, failure) ||
            !allocator.SealRetirements(meshRetirementFences, &allocatorFailure) || allocator.Collect(&allocatorFailure) != 1)
            return false;

        const vanguard::filesystem::AbsolutePath testDirectory = vanguard::filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("vanguard_mesh_lod_geometryUploader_tests");
        const vanguard::filesystem::AbsolutePath meshPath = testDirectory.AddFilePath("geometry.vmesh");
        const vanguard::filesystem::AbsolutePath texturePath = testDirectory.AddFilePath("upload.vtex");
        const vanguard::filesystem::AbsolutePath transitionTexturePath = testDirectory.AddFilePath("transition.vtex");
        vanguard::filesystem::Manager& fileManager = vanguard::filesystem::GetManager();
        static_cast<void>(fileManager.DeleteFile(meshPath));
        static_cast<void>(fileManager.DeleteFile(texturePath));
        static_cast<void>(fileManager.DeleteFile(transitionTexturePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
        if (!fileManager.CreatePath(testDirectory) || !WriteFile(meshPath, meshDocument) || !WriteFile(texturePath, textureDocument) ||
            !WriteFile(transitionTexturePath, transitionTextureDocument))
            return false;

        resources::ResourceRegistry registry;
        if (!registry.Initialize() || !registry.RegisterLoader({TestMaterialType, "geometry geometryUploader material", BeginMaterialLoad, DestroyMaterial, nullptr}) ||
            !registry.RegisterLoader({meshes::MeshResourceType, "geometry geometryUploader mesh", BeginDeferredMeshLoad, DestroyMesh, nullptr}) ||
            !registry.RegisterLoader({textures::TextureResourceType, "texture uploader regression texture", BeginDeferredTextureLoad, DestroyTexture, nullptr}))
            return false;
        const resources::ResourceReference materialReference(resources::ResourcePath::FromString("materials/geometry_test.vmat"), TestMaterialType);
        resources::ResourceRequest materialRequest = registry.Request(materialReference);
        materialRequest.Wait();
        resources::ResourceHandle materialHandle = materialRequest.Acquire();
        if (!materialHandle.IsValid())
            return false;

        const resources::ResourceReference meshReference(resources::ResourcePath::FromString("meshes/geometry_geometryUploader.vmesh"), meshes::MeshResourceType);
        resources::ResourceRequest meshRequest = registry.Request(meshReference);
        meshes::MeshPageSource pageSource;
        meshes::MeshResourceObject* meshResource = AllocateResource<meshes::MeshResourceObject>();
        if (meshResource == nullptr || pageSource.OpenLoose(meshPath) != meshes::Result::Success ||
            meshResource->Open(std::move(pageSource), {&materialHandle, 1}) != meshes::Result::Success || !registry.Publish(meshRequest, meshResource))
        {
            DeleteResource(meshResource);
            return false;
        }
        meshRequest.Wait();
        resources::ResourceHandle meshHandle = meshRequest.Acquire();
        if (!meshHandle.IsValid())
            return false;

        const resources::ResourceReference textureReference(resources::ResourcePath::FromString("textures/texture_uploader_test.vtex"), textures::TextureResourceType);
        resources::ResourceRequest textureRequest = registry.Request(textureReference);
        textures::TextureSubresourceSource textureSource;
        textures::TextureFile textureMetadata;
        vanguard::filesystem::MemoryFileReader textureReader(textureDocument, 0);
        textures::TextureResourceObject* textureResource = AllocateResource<textures::TextureResourceObject>();
        if (textureResource == nullptr || textureSource.OpenLoose(texturePath) != textures::Result::Success || textureMetadata.Open(textureReader) != textures::Result::Success ||
            textureResource->OpenPrepared(std::move(textureSource), std::move(textureMetadata)) != textures::Result::Success || !registry.Publish(textureRequest, textureResource))
        {
            DeleteResource(textureResource);
            return false;
        }
        textureRequest.Wait();
        resources::ResourceHandle textureHandle = textureRequest.Acquire();
        if (!textureHandle.IsValid())
            return false;
        const resources::ResourceReference transitionTextureReference(resources::ResourcePath::FromString("textures/texture_transition_test.vtex"), textures::TextureResourceType);
        resources::ResourceRequest transitionTextureRequest = registry.Request(transitionTextureReference);
        textures::TextureSubresourceSource transitionTextureSource;
        textures::TextureFile transitionTextureMetadata;
        vanguard::filesystem::MemoryFileReader transitionTextureReader(transitionTextureDocument, 0);
        textures::TextureResourceObject* transitionTextureResource = AllocateResource<textures::TextureResourceObject>();
        if (transitionTextureResource == nullptr || transitionTextureSource.OpenLoose(transitionTexturePath) != textures::Result::Success ||
            transitionTextureMetadata.Open(transitionTextureReader) != textures::Result::Success ||
            transitionTextureResource->OpenPrepared(std::move(transitionTextureSource), std::move(transitionTextureMetadata)) != textures::Result::Success ||
            !registry.Publish(transitionTextureRequest, transitionTextureResource))
        {
            DeleteResource(transitionTextureResource);
            return false;
        }
        transitionTextureRequest.Wait();
        resources::ResourceHandle transitionTextureHandle = transitionTextureRequest.Acquire();
        if (!transitionTextureHandle.IsValid())
            return false;
        if (!RunTextureUploaderRegressionTests(textureHandle, transitionTextureHandle, failure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture uploader regression failed: rhi=%u message=%s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain residencyDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        rendering::GpuSceneTables residencyTables;
        rendering::GpuSceneTablesFailure residencyTableFailure;
        rendering::GpuSceneLifetime residencyLifetime;
        rendering::GpuSceneLifetimeFailure residencyLifetimeFailure;
        rendering::GpuSceneUploader residencySceneUploader;
        rendering::GpuSceneUploadFailure residencySceneUploadFailure;
        rendering::GpuSceneDefinitions residencyDefinitions;
        rendering::GpuSceneDefinitionFailure residencyDefinitionFailure;
        rendering::GpuSceneUploadConfig residencySceneUploadConfig;
        residencySceneUploadConfig.bytesPerSegment = 2u * 1024u * 1024u;
        residencySceneUploadConfig.maximumUpdatesPerBatch = 64;
        residencySceneUploadConfig.maximumCopiesPerBatch = 128;
        rendering::GpuSceneDefinitionsConfig residencyDefinitionConfig;
        residencyDefinitionConfig.maximumGeometries = 16;
        residencyDefinitionConfig.maximumMaterials = 16;
        residencyDefinitionConfig.maximumRenderables = 16;
        residencyDefinitionConfig.maximumDefinitionsPerBatch = 16;
        residencyDefinitionConfig.maximumAllocationsPerBatch = 64;
        if (!residencyDescriptors || !residencyTables.Initialize({residencyDescriptors, 2}, &residencyTableFailure) ||
            !residencyLifetime.Initialize(residencyTables, {}, &residencyLifetimeFailure) ||
            !residencySceneUploader.Initialize(residencyTables, residencyLifetime, residencySceneUploadConfig, &residencySceneUploadFailure) ||
            !residencyDefinitions.Initialize(residencyLifetime, residencySceneUploader, residencyDefinitionConfig, &residencyDefinitionFailure))
        {
            std::printf("[geometryAllocatorTests] residency GPU Scene setup failed: table=%u lifetime=%u upload=%u definitions=%u\n", static_cast<unsigned>(residencyTableFailure.code),
                        static_cast<unsigned>(residencyLifetimeFailure.code), static_cast<unsigned>(residencySceneUploadFailure.code),
                        static_cast<unsigned>(residencyDefinitionFailure.code));
            return false;
        }

        gpu::DescriptorDomain textureDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 3, 0, visibility}, &failure));
        rendering::TextureResidencyManager textureResidency;
        rendering::TextureResidencyFailure textureResidencyFailure;
        rendering::TextureResidencyConfig textureResidencyConfig;
        textureResidencyConfig.maximumTextures = 3;
        textureResidencyConfig.maximumPendingInstallations = 2;
        textureResidencyConfig.maximumPendingRetirements = 4;
        rendering::GpuTextureResidencyHandle firstTextureHandle;
        rendering::GpuTextureResidencyHandle secondTextureHandle;
        const auto TextureTestFailure = [](const unsigned line) noexcept
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture residency check failed at line %u\n", line);
            return false;
        };
        const rendering::GpuSceneUploadStats uploadStatsBeforeTextureAllocation = residencySceneUploader.GetStats();
        if (!textureDescriptors || !textureResidency.Initialize(residencyLifetime, textureDescriptors, textureResidencyConfig, &textureResidencyFailure) ||
            !textureResidency.Allocate(firstTextureHandle, &textureResidencyFailure) || !textureResidency.Allocate(secondTextureHandle, &textureResidencyFailure) ||
            !firstTextureHandle.IsValid() || !secondTextureHandle.IsValid() || residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureAllocation.batchesSubmitted)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle capacityHandle;
        rendering::GpuTextureResidencyHandle overflowHandle;
        if (!textureResidency.Allocate(capacityHandle, &textureResidencyFailure) || textureResidency.Allocate(overflowHandle, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::CapacityExceeded)
            return TextureTestFailure(__LINE__);

        rendering::GpuSceneAllocation mixedProducerAllocation;
        if (!residencyLifetime.Allocate<rendering::GpuInstance>(1, mixedProducerAllocation, &residencyLifetimeFailure))
            return TextureTestFailure(__LINE__);

        auto SubmitTextureBatch = [&](gpu::GpuFence& completion, const vanguard::u32 maximumTextureInstallations, const rendering::GpuSceneAllocation extraAllocation = {}) noexcept
        {
            completion = {};
            rendering::TextureResidencyBatch batch;
            if (!textureResidency.PrepareBatch(maximumTextureInstallations, batch, &textureResidencyFailure) || !batch.IsValid() || batch.installationCount == 0)
                return TextureTestFailure(__LINE__);
            vanguard::containers::DynamicArray<rendering::GpuSceneUploadRequest> requests(vanguard::memory::pools::Rendering::GetInstance());
            vanguard::containers::DynamicArray<rendering::GpuSceneUploadReservation> reservations(vanguard::memory::pools::Rendering::GetInstance());
            const vanguard::u32 requestCount = batch.installationCount + (extraAllocation.IsValid() ? 1u : 0u);
            requests.Resize(requestCount);
            reservations.Resize(requestCount);
            if (!textureResidency.BuildUploadRequests(batch, {requests.TypedData(), batch.installationCount}, &textureResidencyFailure))
            {
                static_cast<void>(textureResidency.DiscardBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (extraAllocation.IsValid())
                requests[batch.installationCount] = {extraAllocation, 0, 1};
            if (!residencySceneUploader.Begin({requests.TypedData(), requests.Size()}, {reservations.TypedData(), reservations.Size()}, &residencySceneUploadFailure))
            {
                static_cast<void>(textureResidency.RetryBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (!textureResidency.WriteBatch(batch, {reservations.TypedData(), batch.installationCount}, &textureResidencyFailure))
            {
                static_cast<void>(residencySceneUploader.Cancel());
                static_cast<void>(textureResidency.DiscardBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (extraAllocation.IsValid())
            {
                const rendering::GpuSceneUploadReservation& reservation = reservations[batch.installationCount];
                if (!reservation.IsValid() || reservation.size != sizeof(rendering::GpuInstance))
                {
                    static_cast<void>(residencySceneUploader.Cancel());
                    static_cast<void>(textureResidency.DiscardBatch(batch));
                    return TextureTestFailure(__LINE__);
                }
                const rendering::GpuInstance value{};
                std::memcpy(reservation.destination, &value, sizeof(value));
            }
            for (const rendering::GpuSceneUploadReservation reservation : reservations)
                if (!residencySceneUploader.Complete(reservation, &residencySceneUploadFailure))
                {
                    static_cast<void>(residencySceneUploader.Cancel());
                    static_cast<void>(textureResidency.DiscardBatch(batch));
                    return TextureTestFailure(__LINE__);
                }
            rendering::GpuSceneUploadResult result;
            if (!residencySceneUploader.Submit(result, &residencySceneUploadFailure))
            {
                static_cast<void>(textureResidency.RetryBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            textureResidency.AcceptSubmittedBatch(batch, result.completion);
            completion = result.completion;
            return true;
        };

        gpu::TextureDesc residentTextureDesc;
        residentTextureDesc.extent = {4, 4, 1};
        residentTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        residentTextureDesc.mipCount = 1;
        residentTextureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        gpu::Texture firstTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture secondTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture thirdTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture fourthTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        rendering::TextureInstallationTicket firstTextureTicket;
        rendering::TextureInstallationTicket secondTextureTicket;
        rendering::TextureResidencyInfo firstTextureInfo;
        rendering::TextureResidencyInfo secondTextureInfo;
        const rendering::GpuSceneUploadStats uploadStatsBeforeTextureInstall = residencySceneUploader.GetStats();
        if (!firstTexture || !secondTexture || !thirdTexture || !fourthTexture ||
            !textureResidency.Install(firstTextureHandle, {firstTexture, 4, 1}, firstTextureTicket, &textureResidencyFailure) ||
            !textureResidency.Install(secondTextureHandle, {secondTexture, 2, 1}, secondTextureTicket, &textureResidencyFailure) || firstTextureTicket.revision != 1 ||
            secondTextureTicket.revision != 1 || residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted ||
            !textureResidency.GetInfo(firstTextureHandle, firstTextureInfo, &textureResidencyFailure) || firstTextureInfo.state != rendering::TextureResidencyState::Allocated ||
            firstTextureInfo.texture.IsValid())
            return TextureTestFailure(__LINE__);

        rendering::TextureResidencyBatch retriedTextureBatch;
        if (!textureResidency.PrepareBatch(1, retriedTextureBatch, &textureResidencyFailure) || retriedTextureBatch.installationCount != 1 ||
            !textureResidency.RetryBatch(retriedTextureBatch, &textureResidencyFailure) || textureResidency.GetStats().pendingInstallations != 2 ||
            textureResidency.GetStats().frozenInstallations != 0)
            return TextureTestFailure(__LINE__);

        gpu::GpuFence firstTextureBatchCompletion;
        if (!SubmitTextureBatch(firstTextureBatchCompletion, 1, mixedProducerAllocation) || !firstTextureBatchCompletion.IsValid() ||
            residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted + 1 ||
            residencyLifetime.GetState(mixedProducerAllocation) != rendering::GpuSceneAllocationState::Active ||
            !textureResidency.GetInfo(firstTextureHandle, firstTextureInfo, &textureResidencyFailure) || firstTextureInfo.texture != firstTexture.GetRef() ||
            !textureResidency.GetInfo(secondTextureHandle, secondTextureInfo, &textureResidencyFailure) || secondTextureInfo.state != rendering::TextureResidencyState::Allocated ||
            secondTextureInfo.texture.IsValid() || textureResidency.GetStats().pendingInstallations != 1)
            return TextureTestFailure(__LINE__);
        gpu::GpuFence secondTextureBatchCompletion;
        if (!SubmitTextureBatch(secondTextureBatchCompletion, textureResidencyConfig.maximumPendingInstallations) || !secondTextureBatchCompletion.IsValid() ||
            residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted + 2 ||
            !textureResidency.GetInfo(secondTextureHandle, secondTextureInfo, &textureResidencyFailure) || secondTextureInfo.texture != secondTexture.GetRef() ||
            firstTextureInfo.descriptor.GpuIndex() == secondTextureInfo.descriptor.GpuIndex() || textureResidency.GetStats().pendingInstallations != 0)
            return TextureTestFailure(__LINE__);

        rendering::TextureResidencyInfo textureInfo;
        rendering::TextureInstallationTicket thirdTextureTicket;
        rendering::TextureInstallationTicket failedTextureTicket;
        if (!textureResidency.Install(firstTextureHandle, {thirdTexture, 0, 1}, thirdTextureTicket, &textureResidencyFailure) || thirdTextureTicket.revision != 2 ||
            textureResidency.Install(capacityHandle, {fourthTexture, 0, 1}, failedTextureTicket, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::DescriptorFailure ||
            !textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) || textureInfo.texture != firstTexture.GetRef() || textureInfo.firstResidentMip != 4 ||
            textureInfo.installationRevision != 1 || textureResidency.GetStats().pendingInstallations != 1 ||
            !textureResidency.GetInfo(capacityHandle, textureInfo, &textureResidencyFailure) || textureInfo.state != rendering::TextureResidencyState::Allocated ||
            textureInfo.texture.IsValid())
            return TextureTestFailure(__LINE__);

        gpu::GpuFence replacementCompletion;
        if (!SubmitTextureBatch(replacementCompletion, textureResidencyConfig.maximumPendingInstallations) || !replacementCompletion.IsValid() ||
            !textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) || textureInfo.texture != thirdTexture.GetRef() || textureInfo.firstResidentMip != 0 ||
            textureInfo.installationRevision != 2 || textureResidency.GetStats().pendingDescriptorRetirements != 1)
            return TextureTestFailure(__LINE__);

        if (textureResidency.SealRetirements({}, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::MissingRetirementFence)
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet textureReplacementFences;
        if (!SubmitFences(0x5445585245504c00ull, textureReplacementFences, failure) || !textureResidency.SealRetirements(textureReplacementFences, &textureResidencyFailure) ||
            !WaitFences(textureReplacementFences, failure) || !gpu::RetireResources(&failure))
            return TextureTestFailure(__LINE__);

        rendering::TextureInstallationTicket fourthTextureTicket;
        rendering::TextureResidencyBatch cancelledTextureBatch;
        if (!textureResidency.Install(secondTextureHandle, {fourthTexture, 0, 1}, fourthTextureTicket, &textureResidencyFailure) || fourthTextureTicket.revision != 2 ||
            !textureResidency.PrepareBatch(1, cancelledTextureBatch, &textureResidencyFailure) || cancelledTextureBatch.installationCount != 1 ||
            !textureResidency.DiscardBatch(cancelledTextureBatch, &textureResidencyFailure) || !textureResidency.GetInfo(secondTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.texture != secondTexture.GetRef() || textureInfo.installationRevision != 1)
            return TextureTestFailure(__LINE__);

        if (!textureResidency.Install(secondTextureHandle, {fourthTexture, 0, 1}, fourthTextureTicket, &textureResidencyFailure) ||
            !SubmitTextureBatch(replacementCompletion, textureResidencyConfig.maximumPendingInstallations) ||
            !textureResidency.GetInfo(secondTextureHandle, textureInfo, &textureResidencyFailure) || textureInfo.texture != fourthTexture.GetRef() || textureInfo.installationRevision != 2 ||
            !textureResidency.Retire(capacityHandle, &textureResidencyFailure) || textureResidency.GetInfo(capacityHandle, textureInfo, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::StaleHandle || !textureResidency.Retire(firstTextureHandle, &textureResidencyFailure) ||
            !textureResidency.Retire(secondTextureHandle, &textureResidencyFailure) || !residencyLifetime.Retire(mixedProducerAllocation, &residencyLifetimeFailure))
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet textureRetirementFences;
        if (!SubmitFences(0x5445585245545200ull, textureRetirementFences, failure) || !textureResidency.SealRetirements(textureRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(textureRetirementFences, &residencyLifetimeFailure) || !WaitFences(textureRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 3 || textureResidency.CollectRetirements(&textureResidencyFailure) != 2 ||
            textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::StaleHandle)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle streamedTextureHandle;
        rendering::TextureUploaderConfig streamedUploaderConfig;
        streamedUploaderConfig.maximumRequests = 1;
        streamedUploaderConfig.maximumAcquisitionStartsPerTick = 1;
        streamedUploaderConfig.maximumCandidatesPerBatch = 1;
        streamedUploaderConfig.maximumCompletionPollsPerTick = 1;
        streamedUploaderConfig.maximumReadyCandidates = 1;
        streamedUploaderConfig.maximumWritesPerBatch = 1;
        streamedUploaderConfig.maximumBytesPerBatch = 64;
        streamedUploaderConfig.maximumPendingCandidateBytes = 64;
        streamedUploaderConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader streamedUploader;
        rendering::TextureUploadFailure streamedUploadFailure;
        rendering::TextureUploadRequestId streamedRequest;
        if (!textureResidency.Allocate(streamedTextureHandle, &textureResidencyFailure) || !streamedUploader.Initialize(streamedUploaderConfig, &streamedUploadFailure) ||
            !streamedUploader.RequestMipTail(textureHandle, textureResidency, streamedTextureHandle, streamedRequest, vanguard::io::eAsyncPriority_Streaming, &streamedUploadFailure))
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            const rendering::TextureUploadState state = streamedUploader.GetState(streamedRequest);
            if (state == rendering::TextureUploadState::ReadyToInstall || state == rendering::TextureUploadState::Failed)
                break;
            if (!streamedUploader.Tick(&streamedUploadFailure))
                return TextureTestFailure(__LINE__);
            std::this_thread::yield();
        }
        vanguard::u32 installedCandidates = 0;
        if (streamedUploader.GetState(streamedRequest) != rendering::TextureUploadState::ReadyToInstall ||
            !streamedUploader.InstallReadyCandidates(textureResidency, 1, installedCandidates, &streamedUploadFailure) || installedCandidates != 1 ||
            streamedUploader.GetState(streamedRequest) != rendering::TextureUploadState::Invalid || !textureResidency.GetInfo(streamedTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.state != rendering::TextureResidencyState::Allocated || textureInfo.texture.IsValid() || !streamedUploader.Shutdown(&streamedUploadFailure))
            return TextureTestFailure(__LINE__);

        gpu::GpuFence streamedTableCompletion;
        if (!SubmitTextureBatch(streamedTableCompletion, 1) || !streamedTableCompletion.IsValid() ||
            !textureResidency.GetInfo(streamedTextureHandle, textureInfo, &textureResidencyFailure) || textureInfo.state != rendering::TextureResidencyState::BindlessReady ||
            !textureInfo.texture.IsValid() || textureInfo.firstResidentMip != 0 || textureInfo.residentMipCount != 1 || !textureInfo.source.IsValid() ||
            textureInfo.source.GetGeneration() != textureHandle.GetGeneration() ||
            !(textureInfo.contentFingerprint == static_cast<textures::TextureResourceObject*>(textureHandle.Get())->GetMetadata().GetContentFingerprint()) ||
            !textureResidency.Retire(streamedTextureHandle, &textureResidencyFailure))
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet streamedRetirementFences;
        if (!SubmitFences(0x545853545245414dull, streamedRetirementFences, failure) || !textureResidency.SealRetirements(streamedRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(streamedRetirementFences, &residencyLifetimeFailure) || !WaitFences(streamedRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle transitionHandle;
        rendering::TextureUploaderConfig transitionUploaderConfig;
        transitionUploaderConfig.maximumRequests = 1;
        transitionUploaderConfig.maximumAcquisitionStartsPerTick = 1;
        transitionUploaderConfig.maximumCandidatesPerBatch = 1;
        transitionUploaderConfig.maximumCompletionPollsPerTick = 1;
        transitionUploaderConfig.maximumReadyCandidates = 1;
        transitionUploaderConfig.maximumWritesPerBatch = 4;
        transitionUploaderConfig.maximumCopiesPerBatch = 4;
        transitionUploaderConfig.maximumAcquisitionWindowSubresources = 4;
        transitionUploaderConfig.maximumBytesPerBatch = 512;
        transitionUploaderConfig.maximumCopyBytesPerBatch = 512;
        transitionUploaderConfig.maximumCandidateGpuBytesPerBatch = 1024ull * 1024ull;
        transitionUploaderConfig.maximumPendingCandidateBytes = 512;
        transitionUploaderConfig.maximumAcquisitionWindowBytes = 512;
        rendering::TextureUploader transitionUploader;
        rendering::TextureUploadFailure transitionFailure;
        const auto PumpTransition = [&](const rendering::TextureUploadRequestId transitionRequest, const gpu::ResidencyFenceSet& prerequisites) noexcept
        {
            for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
            {
                const rendering::TextureUploadState state = transitionUploader.GetState(transitionRequest);
                if (state == rendering::TextureUploadState::ReadyToInstall || state == rendering::TextureUploadState::Failed)
                    return true;
                if (!transitionUploader.Tick(prerequisites, &transitionFailure))
                    return false;
                std::this_thread::yield();
            }
            return false;
        };
        const auto InstallTransition = [&](gpu::GpuFence& tableCompletion) noexcept
        {
            vanguard::u32 installed = 0;
            if (!transitionUploader.InstallReadyCandidates(textureResidency, 1, installed, &transitionFailure) || installed != 1)
                return false;
            // Installation admission transfers transition ownership to residency. The identity must remain
            // immutable until the shared GPU Scene batch either accepts or discards the candidate.
            if (textureResidency.Retire(transitionHandle, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::Busy)
                return false;
            return SubmitTextureBatch(tableCompletion, 1);
        };

        rendering::TextureUploadRequestId transitionRequest;
        if (!textureResidency.Allocate(transitionHandle, &textureResidencyFailure) || !transitionUploader.Initialize(transitionUploaderConfig, &transitionFailure) ||
            !transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 2, transitionRequest, vanguard::io::eAsyncPriority_Streaming,
                                                     &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, {}))
            return TextureTestFailure(__LINE__);
        rendering::TextureInstallationTicket forgedTransitionTicket;
        rendering::TextureInstallationDesc forgedTransitionInstallation;
        forgedTransitionInstallation.texture = fourthTexture.GetRef();
        forgedTransitionInstallation.firstResidentMip = 2;
        forgedTransitionInstallation.residentMipCount = 1;
        forgedTransitionInstallation.source = transitionTextureHandle.ToWeak();
        forgedTransitionInstallation.contentFingerprint = static_cast<textures::TextureResourceObject*>(transitionTextureHandle.Get())->GetMetadata().GetContentFingerprint();
        if (textureResidency.Install(transitionHandle, forgedTransitionInstallation, forgedTransitionTicket, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::Busy)
            return TextureTestFailure(__LINE__);
        gpu::GpuFence tailTableCompletion;
        if (!InstallTransition(tailTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 2 ||
            textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);

        const rendering::TextureUploaderStats tailStats = transitionUploader.GetStats();
        gpu::ResidencyFenceSet promotionPrerequisites;
        promotionPrerequisites.Include(tailTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming,
                                                     &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, promotionPrerequisites))
            return TextureTestFailure(__LINE__);
        gpu::GpuFence promotionTableCompletion;
        if (!InstallTransition(promotionTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 0 ||
            textureInfo.residentMipCount != 4)
            return TextureTestFailure(__LINE__);
        const rendering::TextureUploaderStats promotionStats = transitionUploader.GetStats();
        if (promotionStats.bytesSubmitted - tailStats.bytesSubmitted != 320 || promotionStats.copyBytesSubmitted - tailStats.copyBytesSubmitted != 20 ||
            promotionStats.copiesSubmitted - tailStats.copiesSubmitted != 2)
            return TextureTestFailure(__LINE__);

        gpu::ResidencyFenceSet demotionPrerequisites;
        demotionPrerequisites.Include(promotionTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 2, transitionRequest, vanguard::io::eAsyncPriority_Streaming,
                                                     &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, demotionPrerequisites))
            return TextureTestFailure(__LINE__);
        gpu::GpuFence demotionTableCompletion;
        if (!InstallTransition(demotionTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 2 ||
            textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);
        const rendering::TextureUploaderStats demotionStats = transitionUploader.GetStats();
        if (demotionStats.bytesSubmitted != promotionStats.bytesSubmitted || demotionStats.copyBytesSubmitted - promotionStats.copyBytesSubmitted != 20 ||
            demotionStats.copiesSubmitted - promotionStats.copiesSubmitted != 2)
            return TextureTestFailure(__LINE__);

        gpu::ResidencyFenceSet completedTransitionCutover;
        if (!SubmitFences(0x5458544355544f56ull, completedTransitionCutover, failure) || !textureResidency.SealRetirements(completedTransitionCutover, &textureResidencyFailure) ||
            !WaitFences(completedTransitionCutover, failure) || !gpu::RetireResources(&failure))
            return TextureTestFailure(__LINE__);

        // A candidate discarded before shared table submission must leave the old installation authoritative,
        // release the manager-owned transition, and allow a later request to start normally.
        gpu::ResidencyFenceSet discardPrerequisites;
        discardPrerequisites.Include(demotionTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming,
                                                     &transitionFailure) ||
            !transitionRequest.IsValid())
        {
            std::fprintf(stderr, "[geometryAllocatorTests] discard transition request failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!PumpTransition(transitionRequest, discardPrerequisites))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] discard transition pump failed: %s (state %u)\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message",
                         static_cast<unsigned>(transitionUploader.GetState(transitionRequest)));
            return TextureTestFailure(__LINE__);
        }
        vanguard::u32 discardedInstalled = 0;
        rendering::TextureResidencyBatch discardedTransitionBatch;
        if (!transitionUploader.InstallReadyCandidates(textureResidency, 1, discardedInstalled, &transitionFailure) || discardedInstalled != 1 ||
            !textureResidency.PrepareBatch(1, discardedTransitionBatch, &textureResidencyFailure) || discardedTransitionBatch.installationCount != 1 ||
            !textureResidency.DiscardBatch(discardedTransitionBatch, &textureResidencyFailure) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.firstResidentMip != 2 || textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming,
                                                     &transitionFailure) ||
            !transitionRequest.IsValid())
            return TextureTestFailure(__LINE__);
        if (!transitionUploader.Cancel(transitionRequest, &transitionFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] cancelled replacement failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!transitionUploader.Shutdown(&transitionFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] transition uploader shutdown failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!textureResidency.Retire(transitionHandle, &textureResidencyFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] transition residency retirement failed: %s\n",
                         textureResidencyFailure.message != nullptr ? textureResidencyFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }

        gpu::ResidencyFenceSet transitionRetirementFences;
        if (!SubmitFences(0x5458545245544952ull, transitionRetirementFences, failure) || !textureResidency.SealRetirements(transitionRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(transitionRetirementFences, &residencyLifetimeFailure) || !WaitFences(transitionRetirementFences, failure) ||
            !gpu::RetireResources(&failure) || residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1 ||
            !textureResidency.Shutdown(&textureResidencyFailure))
            return TextureTestFailure(__LINE__);
        firstTexture.Reset();
        secondTexture.Reset();
        thirdTexture.Reset();
        fourthTexture.Reset();
        textureDescriptors.Reset();
        std::printf("Vanguard D3D12 texture upload and residency tests passed.\n");

        gpu::DescriptorDomain runtimeTextureDescriptors(
            gpu::AdoptReference,
            gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        rendering::RenderSceneManager runtimeScenes;
        rendering::RenderSceneFailure runtimeSceneFailure;
        rendering::GpuSceneRuntimeConfig gpuSceneRuntimeConfig;
        gpuSceneRuntimeConfig.tables.resourceDescriptors = runtimeTextureDescriptors;
        gpuSceneRuntimeConfig.tables.maximumPagesPerTable = 2;
        gpuSceneRuntimeConfig.upload.bytesPerSegment = 2u * 1024u * 1024u;
        gpuSceneRuntimeConfig.upload.maximumUpdatesPerBatch = 64;
        gpuSceneRuntimeConfig.upload.maximumCopiesPerBatch = 128;
        gpuSceneRuntimeConfig.definitions.maximumGeometries = 2;
        gpuSceneRuntimeConfig.definitions.maximumMaterials = 2;
        gpuSceneRuntimeConfig.definitions.maximumRenderables = 2;
        gpuSceneRuntimeConfig.definitions.maximumDefinitionsPerBatch = 2;
        gpuSceneRuntimeConfig.definitions.maximumAllocationsPerBatch = 8;
        gpuSceneRuntimeConfig.maximumExternalContributions = 2;
        rendering::GpuSceneRuntime gpuSceneRuntime;
        rendering::GpuSceneRuntimeFailure gpuSceneRuntimeFailure;
        rendering::RenderCameraStorage runtimeCameras;
        rendering::RenderCameraFailure runtimeCameraFailure;
        TextureRuntimeGpuSceneExecution gpuSceneExecution{&gpuSceneRuntime};
        rendering::RenderCommandSystem runtimeCommands;
        rendering::RenderCommandSystemConfig runtimeCommandConfig;
        runtimeCommandConfig.executeFrameTick = &ExecuteTextureRuntimeGpuSceneTick;
        runtimeCommandConfig.executeFrame = &ExecuteTextureRuntimeGpuSceneFrame;
        runtimeCommandConfig.userData = &gpuSceneExecution;
        rendering::RenderCommandFailure runtimeCommandFailure;
        rendering::TextureResidencyRuntimeConfig textureRuntimeConfig;
        textureRuntimeConfig.residency.maximumTextures = 2;
        textureRuntimeConfig.residency.maximumPendingInstallations = 2;
        textureRuntimeConfig.residency.maximumPendingRetirements = 2;
        textureRuntimeConfig.uploader.maximumRequests = 2;
        textureRuntimeConfig.uploader.maximumAcquisitionStartsPerTick = 2;
        textureRuntimeConfig.uploader.maximumCandidatesPerBatch = 2;
        textureRuntimeConfig.uploader.maximumCompletionPollsPerTick = 2;
        textureRuntimeConfig.uploader.maximumReadyCandidates = 2;
        textureRuntimeConfig.uploader.maximumWritesPerBatch = 8;
        textureRuntimeConfig.uploader.maximumCopiesPerBatch = 8;
        textureRuntimeConfig.maximumResidencyRecords = 2;
        textureRuntimeConfig.maximumDemands = 4;
        textureRuntimeConfig.maximumInstallationsPerTick = 2;
        textureRuntimeConfig.maximumTableInstallationsPerFrame = 2;
        textureRuntimeConfig.maximumStateChecksPerTick = 2;
        rendering::TextureResidencyRuntime textureRuntime;
        rendering::TextureResidencyRuntimeFailure textureRuntimeFailure;
        rendering::TextureDemandHandle firstTextureDemand;
        rendering::TextureDemandHandle secondTextureDemand;
        if (!runtimeTextureDescriptors || !runtimeScenes.Initialize({}, &runtimeSceneFailure) ||
            !gpuSceneRuntime.Initialize(runtimeScenes, gpuSceneRuntimeConfig, &gpuSceneRuntimeFailure) ||
            !runtimeCameras.Initialize(runtimeScenes, {}, &runtimeCameraFailure) ||
            !runtimeCommands.Initialize(runtimeScenes, runtimeCameras, runtimeCommandConfig, &runtimeCommandFailure) ||
            !textureRuntime.Initialize(gpuSceneRuntime.GetLifetime(), runtimeTextureDescriptors, textureRuntimeConfig,
                                       &textureRuntimeFailure) ||
            !textureRuntime.RequestTexture(textureHandle, firstTextureDemand, &textureRuntimeFailure) ||
            !textureRuntime.RequestTexture(textureHandle, secondTextureDemand, &textureRuntimeFailure) ||
            firstTextureDemand.GetResidency() != secondTextureDemand.GetResidency())
            return TextureTestFailure(__LINE__);
        const rendering::GpuTextureResidencyHandle runtimeTexture = firstTextureDemand.GetResidency();
        rendering::TextureRuntimeInfo runtimeTextureInfo;
        if (!textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) ||
            runtimeTextureInfo.state != rendering::TextureRuntimeState::MipTailLoading ||
            runtimeTextureInfo.demandCount != 2 || textureRuntime.GetStats().demandsCoalesced != 1)
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            if (!textureRuntime.Tick(&textureRuntimeFailure) ||
                !textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure))
                return TextureTestFailure(__LINE__);
            if (runtimeTextureInfo.state == rendering::TextureRuntimeState::InstallationPending ||
                runtimeTextureInfo.state == rendering::TextureRuntimeState::Failed)
                break;
            std::this_thread::yield();
        }
        if (runtimeTextureInfo.state != rendering::TextureRuntimeState::InstallationPending ||
            textureRuntime.GetStats().residency.pendingInstallations != 1)
            return TextureTestFailure(__LINE__);
        rendering::RenderCommandFrameTickResult runtimeTickResult;
        if (!textureRuntime.StageGpuSceneContribution(gpuSceneRuntime, &textureRuntimeFailure) ||
            !runtimeCommands.FrameTick(runtimeScenes.GetFramePipelineScenes(), 1, runtimeTickResult,
                                       &runtimeCommandFailure) ||
            !runtimeCommands.FlushPreviousFrameProcessing(&runtimeCommandFailure) ||
            !gpuSceneRuntime.ResolveContributions(&gpuSceneRuntimeFailure) ||
            runtimeCommands.ConsumeExecutionFailure(runtimeCommandFailure) ||
            gpuSceneRuntime.ConsumePublicationFailure(gpuSceneRuntimeFailure) ||
            !textureRuntime.Tick(&textureRuntimeFailure) ||
            !textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) ||
            runtimeTextureInfo.state != rendering::TextureRuntimeState::BindlessReady)
            return TextureTestFailure(__LINE__);
        const rendering::GpuSceneRuntimeStats gpuSceneRuntimeStats = gpuSceneRuntime.GetStats();
        if (gpuSceneRuntimeStats.publicationTicks != 1 || gpuSceneRuntimeStats.submittedBatches != 1 ||
            gpuSceneRuntimeStats.stagedContributions != 1 || gpuSceneRuntimeStats.acceptedContributions != 1 ||
            gpuSceneRuntimeStats.retriedContributions != 0)
            return TextureTestFailure(__LINE__);
        firstTextureDemand.Reset();
        if (!textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) ||
            runtimeTextureInfo.demandCount != 1)
            return TextureTestFailure(__LINE__);
        secondTextureDemand.Reset();
        if (!textureRuntime.Tick(&textureRuntimeFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture runtime retirement tick failed: code=%u message=%s lower=%u upload=%u\n",
                         static_cast<unsigned>(textureRuntimeFailure.code),
                         textureRuntimeFailure.message != nullptr ? textureRuntimeFailure.message : "",
                         static_cast<unsigned>(textureRuntimeFailure.residencyFailure.code),
                         static_cast<unsigned>(textureRuntimeFailure.uploadFailure.code));
            return TextureTestFailure(__LINE__);
        }
        gpu::ResidencyFenceSet runtimeRetirementFences;
        rendering::GpuSceneLifetimeFailure runtimeLifetimeFailure;
        if (!SubmitFences(0x54585452554e3542ull, runtimeRetirementFences, failure) ||
            !textureRuntime.SealRetirements(runtimeRetirementFences, &textureRuntimeFailure) ||
            !gpuSceneRuntime.GetLifetime().SealRetirements(runtimeRetirementFences, &runtimeLifetimeFailure) ||
            !WaitFences(runtimeRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            gpuSceneRuntime.GetLifetime().Collect(&runtimeLifetimeFailure) != 1 ||
            textureRuntime.CollectRetirements(&textureRuntimeFailure) != 1 ||
            !textureRuntime.Tick(&textureRuntimeFailure))
            return TextureTestFailure(__LINE__);
        const rendering::TextureResidencyRuntimeStats textureRuntimeStats = textureRuntime.GetStats();
        if (textureRuntimeStats.residencyRecords != 0 || textureRuntimeStats.liveDemands != 0 ||
            !textureRuntime.Shutdown(&textureRuntimeFailure) ||
            !runtimeCommands.Shutdown(&runtimeCommandFailure) ||
            !gpuSceneRuntime.Shutdown({}, &gpuSceneRuntimeFailure) ||
            !runtimeCameras.Shutdown(&runtimeCameraFailure) ||
            !runtimeScenes.Shutdown(&runtimeSceneFailure))
        {
            std::fprintf(stderr,
                         "[geometryAllocatorTests] texture runtime cleanup failed: records=%u demands=%u manager=%u requests=%u code=%u message=%s\n",
                         textureRuntimeStats.residencyRecords, textureRuntimeStats.liveDemands,
                         textureRuntimeStats.residency.liveResidencies, textureRuntimeStats.uploader.liveRequests,
                         static_cast<unsigned>(textureRuntimeFailure.code),
                         textureRuntimeFailure.message != nullptr ? textureRuntimeFailure.message : "");
            return TextureTestFailure(__LINE__);
        }
        runtimeTextureDescriptors.Reset();
        std::printf("Vanguard texture runtime ownership, demand, and GPU Scene installation tests passed.\n");

        rendering::MeshResidencyConfig residencyConfig;
        residencyConfig.geometryAllocator.verticesPerArena = 64;
        residencyConfig.geometryAllocator.indexBytesPerArena = 1024;
        residencyConfig.geometryAllocator.maximumCommittedVertexBytes = 1024 * 1024;
        residencyConfig.geometryAllocator.maximumCommittedIndexBytes = 1024 * 1024;
        residencyConfig.geometryAllocator.maximumVertexArenas = 4;
        residencyConfig.geometryAllocator.maximumIndexArenas = 4;
        residencyConfig.geometryAllocator.maximumAllocations = 16;
        residencyConfig.geometryUpload.bytesPerSegment = 1024;
        residencyConfig.geometryUpload.maximumOverflowBytes = 4096;
        residencyConfig.geometryUpload.segmentCount = 3;
        residencyConfig.geometryUpload.maximumGeometriesPerBatch = 8;
        residencyConfig.geometryUpload.maximumCopiesPerBatch = 32;
        residencyConfig.lodUpload.maximumRequests = 4;
        residencyConfig.lodUpload.maximumLodsPerTick = 1;
        residencyConfig.lodUpload.maximumUploadBytesPerTick = 4096;
        residencyConfig.lodUpload.maximumPendingUploadBytes = 4096;
        residencyConfig.lodUpload.maximumPreparationDeferrals = 4;
        residencyConfig.maximumResidencyRecords = 2;
        residencyConfig.maximumDemands = 8;
        rendering::MeshResidencyManager residency;
        rendering::MeshResidencyFailure residencyFailure;
        rendering::MeshDemandHandle firstDemand;
        rendering::MeshDemandHandle secondDemand;
        if (!residency.Initialize(residencyDefinitions, residencyConfig, &residencyFailure) || !residency.RequestMesh(meshHandle, firstDemand, &residencyFailure) ||
            !residency.RequestMesh(meshHandle, secondDemand, &residencyFailure) || firstDemand.GetResidency() != secondDemand.GetResidency())
        {
            std::printf("[geometryAllocatorTests] residency demand setup failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        rendering::MeshResidencyInfo residencyInfo;
        const rendering::MeshResidencyHandle residencyHandle = firstDemand.GetResidency();
        if (!residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MeshResidencyState::MetadataRetained ||
            residencyInfo.resourceGeneration != meshHandle.GetGeneration() || residencyInfo.anchorLod != 0 || residencyInfo.demandCount != 2 || residency.GetStats().demandsCoalesced != 1 ||
            residency.Shutdown(&residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::LiveResidencyRemains ||
            !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure))
        {
            std::printf("[geometryAllocatorTests] residency coalescing/lifetime checks failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }

        std::array<rendering::MeshDemandHandle, 4> concurrentDemands;
        bool concurrentResults[4]{};
        std::thread demandThreads[4];
        for (vanguard::u32 index = 0; index < 4; ++index)
            demandThreads[index] = std::thread([&residency, &meshHandle, &concurrentDemands, &concurrentResults, index]() noexcept
                                               { concurrentResults[index] = residency.RequestMesh(meshHandle, concurrentDemands[index]); });
        for (std::thread& thread : demandThreads)
            thread.join();
        if (!concurrentResults[0] || !concurrentResults[1] || !concurrentResults[2] || !concurrentResults[3] || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) ||
            residencyInfo.demandCount != 6 || residency.GetStats().demandsCoalesced != 5)
            return false;
        for (rendering::MeshDemandHandle& concurrentDemand : concurrentDemands)
            concurrentDemand.Reset();
        if (!residency.CancelDemand(firstDemand, &residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.demandCount != 1)
            return false;

        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            if (!residency.Tick(&residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure))
                return false;
            if (residencyInfo.state == rendering::MeshResidencyState::AnchorLodDefinitionsSubmitted || residencyInfo.state == rendering::MeshResidencyState::Failed)
                break;
            std::this_thread::yield();
        }
        if (residencyInfo.state != rendering::MeshResidencyState::AnchorLodDefinitionsSubmitted || residencyInfo.geometryCount != 1 || residencyInfo.vertexBytes != 40 ||
            residencyInfo.indexBytes != 6)
            return false;
        const rendering::GpuSceneDefinitionsStats residencyDefinitionStats = residencyDefinitions.GetStats();
        if (residencyDefinitionStats.geometries != 1 || residencyDefinitionStats.references != 1)
            return false;

        secondDemand.Reset();
        if (!residency.Tick(&residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MeshResidencyState::Retiring)
            return false;
        gpu::ResidencyFenceSet residencyRetirementFences;
        if (!SubmitFences(0x4d45534852455300ull, residencyRetirementFences, failure) || !residency.SealRetirements(residencyRetirementFences, &residencyFailure) ||
            !residencyLifetime.SealRetirements(residencyRetirementFences, &residencyLifetimeFailure) || !WaitFences(residencyRetirementFences, failure) ||
            residency.CollectRetirements(&residencyFailure) != 1 || residencyLifetime.Collect(&residencyLifetimeFailure) != 2)
            return false;
        if (residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::InvalidArgument ||
            residency.GetStats().residencyRecords != 0 || residency.GetStats().liveDemands != 0 || !residency.Shutdown(&residencyFailure) ||
            !residencyDefinitions.Shutdown(&residencyDefinitionFailure) || !residencySceneUploader.Shutdown(&residencySceneUploadFailure) ||
            !residencyLifetime.Shutdown(&residencyLifetimeFailure) || !residencyTables.Shutdown({}, &residencyTableFailure))
        {
            std::printf("[geometryAllocatorTests] residency teardown checks failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        residencyDescriptors.Reset();

        rendering::MeshLodGeometryUploader geometryUploader;
        rendering::MeshLodGeometryUploadFailure geometryUploadFailure;
        rendering::MeshLodGeometryUploaderConfig uploaderConfig;
        uploaderConfig.maximumRequests = 4;
        uploaderConfig.maximumLodsPerTick = 1;
        uploaderConfig.maximumUploadBytesPerTick = 1024;
        uploaderConfig.maximumPendingUploadBytes = 4096;
        uploaderConfig.maximumPreparationDeferrals = 8;
        rendering::MeshLodGeometryUploadRequestId geometryUploadRequest;
        rendering::MeshLodGeometryUploadRequestId coalescedRequest;
        if (!geometryUploader.Initialize(allocator, uploader, uploaderConfig, &geometryUploadFailure) ||
            !geometryUploader.Request(meshHandle, 0, geometryUploadRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure) ||
            !geometryUploader.Request(meshHandle, 0, coalescedRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure) || geometryUploadRequest != coalescedRequest ||
            geometryUploader.GetStats().requestsCoalesced != 1)
            return false;
        if (!geometryUploader.Cancel(coalescedRequest, &geometryUploadFailure) || geometryUploader.GetState(geometryUploadRequest) == rendering::MeshLodGeometryUploadState::Invalid)
            return false;
        for (vanguard::u32 poll = 0; poll < 100'000 && geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Complete &&
                                     geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Failed;
             ++poll)
        {
            if (!geometryUploader.Tick(&geometryUploadFailure))
                return false;
            std::this_thread::yield();
        }
        if (geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Complete)
            return false;

        rendering::PendingMeshLodGeometry pendingGeometry;
        if (!geometryUploader.TakeCompleted(geometryUploadRequest, pendingGeometry, &geometryUploadFailure) || !pendingGeometry.IsValid() || pendingGeometry.pages.Size() != 5 ||
            pendingGeometry.geometries.Size() != 1 || pendingGeometry.submeshes.Size() != 1 || pendingGeometry.vertexBytes != 40 || pendingGeometry.indexBytes != 6 ||
            geometryUploader.GetStats().liveRequests != 0 || !gpu::WaitForGpuFence(pendingGeometry.copyCompletion, 5'000'000'000ull, &failure))
            return false;
        if (!rendering::AbortPendingMeshLodGeometry(pendingGeometry, allocator, &allocatorFailure))
            return false;
        gpu::ResidencyFenceSet uploadRetirementFences;
        if (!SubmitFences(0x4d455348494e5300ull, uploadRetirementFences, failure) || !WaitFences(uploadRetirementFences, failure) ||
            !allocator.SealRetirements(uploadRetirementFences, &allocatorFailure) || allocator.Collect(&allocatorFailure) != 1)
            return false;

        rendering::MeshLodGeometryUploadRequestId staleRequest;
        if (!geometryUploader.Request(meshHandle, 0, staleRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure))
            return false;
        meshHandle.Reset();
        if (!registry.Evict(meshReference.GetPath()))
            return false;
        for (vanguard::u32 poll = 0; poll < 100'000 && geometryUploader.GetState(staleRequest) != rendering::MeshLodGeometryUploadState::Failed; ++poll)
        {
            if (!geometryUploader.Tick(&geometryUploadFailure))
                return false;
            std::this_thread::yield();
        }
        rendering::MeshLodGeometryUploadFailure staleFailure;
        if (geometryUploader.GetState(staleRequest) != rendering::MeshLodGeometryUploadState::Failed || !geometryUploader.GetRequestFailure(staleRequest, staleFailure) ||
            staleFailure.code != rendering::MeshLodGeometryUploadFailureCode::StaleResourceGeneration || !geometryUploader.Cancel(staleRequest, &geometryUploadFailure))
            return false;
        if (!geometryUploader.Shutdown(&geometryUploadFailure))
            return false;

        materialHandle.Reset();
        textureHandle.Reset();
        meshRequest.Reset();
        textureRequest.Reset();
        materialRequest.Reset();
        if (!registry.Evict(textureReference.GetPath()) || !registry.Evict(materialReference.GetPath()) || !registry.Shutdown())
            return false;
        static_cast<void>(fileManager.DeleteFile(meshPath));
        static_cast<void>(fileManager.DeleteFile(texturePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
        mesh.Close();

        const rendering::GeometryAllocatorStats stats = allocator.GetStats();
        if (stats.reservedAllocations != 0 || stats.activeAllocations != 0 || stats.retiringAllocations != 0 || stats.retirements != 3 || stats.reclaimed != 3 ||
            !uploader.Shutdown(&uploadFailure) || !allocator.Shutdown(&allocatorFailure))
            return false;
        return gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
    }
} // namespace

int main()
{
    if (!vanguard::memory::Initialize() || !vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "geometryAllocatorTests") || !vanguard::containers::Initialize() ||
        !vanguard::io::Initialize())
        return 1;
    const vanguard::filesystem::AbsolutePath root = vanguard::filesystem::paths::GetCurrentWorkingDirectory();
    if (!vanguard::filesystem::Initialize({root, root, root}) || !vanguard::jobs::Initialize(vanguard::jobs::ToolConfig()))
        return 1;

    vanguard::rhi::d3d12::Backend backend;
    vanguard::rhi::Failure failure;
    vanguard::rhi::DeviceParams params;
#if VG_ENABLE_ASSERTS
    params.enableValidation = true;
#endif
    if (!vanguard::rhi::Initialize(backend, params, &failure))
    {
        const bool unsupported = failure.code == vanguard::rhi::FailureCode::Unsupported;
        static_cast<void>(vanguard::jobs::Shutdown());
        vanguard::filesystem::Shutdown();
        vanguard::io::Shutdown();
        vanguard::diagnostics::Shutdown();
        return unsupported ? 0 : 1;
    }

    const bool passed = RunTests(failure);
    if (!passed)
        std::printf("[geometryAllocatorTests] failed: %s\n", failure.message);
    static_cast<void>(vanguard::rhi::WaitIdle());
    static_cast<void>(vanguard::rhi::RetireResources());
    const bool shutdown = vanguard::rhi::Shutdown(&failure) && vanguard::jobs::Shutdown();
    vanguard::filesystem::Shutdown();
    vanguard::io::Shutdown();
    vanguard::diagnostics::Shutdown();
    return passed && shutdown ? 0 : 1;
}
