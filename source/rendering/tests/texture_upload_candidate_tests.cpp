#include <vanguard/rendering/texture_upload_candidate.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/pool.hpp>

#include <array>

namespace
{
    namespace rendering = vanguard::rendering;
    namespace textures = vanguard::textures;
    using CheckFunction = void (*)(bool condition, const char* message) noexcept;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    [[nodiscard]] bool OpenCooked(const textures::BuildDescription& description, ByteArray& bytes,
                                  textures::TextureFile& texture) noexcept
    {
        vanguard::filesystem::MemoryFileWriter writer(bytes);
        if (textures::WriteTexture(writer, description) != textures::Result::Success)
            return false;
        vanguard::filesystem::MemoryFileReader reader(bytes, 0);
        return texture.Open(reader) == textures::Result::Success;
    }

    [[nodiscard]] vanguard::rhi::Capabilities TestCapabilities() noexcept
    {
        vanguard::rhi::Capabilities capabilities;
        capabilities.maximumTextureDimension2D = 16'384;
        capabilities.maximumTextureDimension3D = 2'048;
        capabilities.maximumTextureArrayLayers = 2'048;
        return capabilities;
    }
} // namespace

void RunTextureUploadCandidateTests(CheckFunction check) noexcept
{
    namespace rhi = vanguard::rhi;
    using vanguard::u8;
    using vanguard::u16;
    using vanguard::u32;

    std::array<std::array<u8, 32>, 8> bytes{};
    std::array<textures::SubresourceBuildRecord, 8> records{};
    u32 recordIndex = 0;
    for (u8 mip = 0; mip < 4; ++mip)
    {
        const u32 extent = textures::CalculateMipExtent(8, mip);
        const u32 rowPitch = textures::CalculateMinimumRowPitch(textures::PixelFormat::BC1UNorm, extent);
        const u32 slicePitch = textures::CalculateMinimumSlicePitch(textures::PixelFormat::BC1UNorm, extent, extent);
        for (u16 layer = 0; layer < 2; ++layer)
        {
            records[recordIndex] = {mip, layer, 0, bytes[recordIndex].data(), slicePitch, rowPitch, slicePitch};
            ++recordIndex;
        }
    }
    textures::BuildDescription description;
    description.dimension = textures::TextureDimension::Texture2D;
    description.format = textures::PixelFormat::BC1UNorm;
    description.colorSpace = textures::ColorSpace::SRgb;
    description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
    description.width = 8;
    description.height = 8;
    description.depth = 1;
    description.arrayLayers = 2;
    description.mipCount = 4;
    description.mipTailFirstLevel = 2;
    description.subresources = {records.data(), static_cast<u32>(records.size())};

    ByteArray cooked{vanguard::memory::pools::Rendering::GetInstance()};
    textures::TextureFile texture;
    check(OpenCooked(description, cooked, texture), "candidate tests open cooked 2D-array VTEX");

    rendering::TextureUploadCandidatePlan plan;
    rendering::TextureUploadCandidateFailure failure;
    const rhi::Capabilities capabilities = TestCapabilities();
    rendering::TextureMipTransitionPlan noOpTransition;
    rendering::TextureMipTransitionPlan promotionTransition;
    rendering::TextureMipTransitionPlan demotionTransition;
    rendering::TextureMipTransitionPlan clampedTransition;
    rendering::TextureMipTransitionPlan invalidTransition;
    rendering::TextureMipSharedCopy sharedMip;
    check(noOpTransition.Build(10, 7, 7, 7, &failure) && noOpTransition.GetKind() == rendering::TextureMipTransitionKind::NoOp &&
              noOpTransition.GetCandidateMipCount() == 3 && noOpTransition.GetUploadMipCount() == 0 && noOpTransition.GetSharedMipCount() == 0,
          "transition planner recognizes an unchanged compact suffix");
    check(promotionTransition.Build(10, 7, 7, 4, &failure) && promotionTransition.GetKind() == rendering::TextureMipTransitionKind::Promotion &&
              promotionTransition.GetTargetFirstMip() == 4 && promotionTransition.GetCandidateMipCount() == 6 &&
              promotionTransition.GetUploadFirstMip() == 4 && promotionTransition.GetUploadMipCount() == 3 &&
              promotionTransition.GetSharedFirstMip() == 7 && promotionTransition.GetSharedMipCount() == 3 &&
              promotionTransition.GetSharedMipCopy(0, sharedMip) && sharedMip.assetMip == 7 && sharedMip.sourcePhysicalMip == 0 &&
              sharedMip.destinationPhysicalMip == 3 && promotionTransition.GetSharedMipCopy(2, sharedMip) && sharedMip.assetMip == 9 &&
              sharedMip.sourcePhysicalMip == 2 && sharedMip.destinationPhysicalMip == 5,
          "transition planner maps promotion upload and shared-copy intervals");
    check(demotionTransition.Build(10, 7, 4, 7, &failure) && demotionTransition.GetKind() == rendering::TextureMipTransitionKind::Demotion &&
              demotionTransition.GetUploadMipCount() == 0 && demotionTransition.GetSharedFirstMip() == 7 &&
              demotionTransition.GetSharedMipCount() == 3 && demotionTransition.GetSharedMipCopy(0, sharedMip) &&
              sharedMip.assetMip == 7 && sharedMip.sourcePhysicalMip == 3 && sharedMip.destinationPhysicalMip == 0,
          "transition planner maps copy-only demotion");
    check(clampedTransition.Build(10, 7, 4, 9, &failure) && clampedTransition.GetRequestedFirstMip() == 9 &&
              clampedTransition.GetTargetFirstMip() == 7 && clampedTransition.GetKind() == rendering::TextureMipTransitionKind::Demotion,
          "transition planner clamps requests so the guaranteed tail remains resident");
    check(!invalidTransition.Build(10, 7, 8, 4, &failure) && failure.code == rendering::TextureUploadCandidateFailureCode::InvalidArgument,
          "transition planner rejects a current suffix that omitted guaranteed-tail mips");

    check(plan.InitializeMipTail(texture, capabilities, &failure), "candidate plan initializes compact mip tail");
    const rhi::TextureDesc& candidateDesc = plan.GetTextureDesc();
    check(candidateDesc.extent.width == 2 && candidateDesc.extent.height == 2 && candidateDesc.extent.depth == 1 &&
              candidateDesc.dimension == rhi::TextureDimension::Texture2D && candidateDesc.format == rhi::Format::BC1UNormSrgb &&
              candidateDesc.mipCount == 2 && candidateDesc.arraySize == 2 && candidateDesc.sampleCount == 1 &&
              (static_cast<u32>(candidateDesc.usage) & static_cast<u32>(rhi::TextureUsage::ShaderResource)) != 0 &&
              (static_cast<u32>(candidateDesc.usage) & static_cast<u32>(rhi::TextureUsage::CopySource)) != 0 &&
              (static_cast<u32>(candidateDesc.usage) & static_cast<u32>(rhi::TextureUsage::CopyDestination)) != 0 &&
              candidateDesc.initialState == (rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute) &&
              !candidateDesc.virtualResource,
          "candidate plan builds exact compact RHI descriptor");
    check(plan.GetFirstAssetMip() == 2 && plan.GetResidentMipCount() == 2 && plan.GetExpectedSubresourceCount() == 4 &&
              plan.GetExpectedByteCount() == 32 && !plan.IsComplete(),
          "candidate plan records tail interval and coverage");

    rendering::TextureUploadCandidateSubresource firstMapped;
    for (u32 sourceIndex = 4; sourceIndex < 8; ++sourceIndex)
    {
        const textures::SubresourceRecord& sourceRecord = texture.GetSubresources()[sourceIndex];
        const textures::TextureSubresourceView view{sourceIndex, &sourceRecord,
                                                    {bytes[sourceIndex].data(), static_cast<u32>(sourceRecord.byteSize)}};
        rendering::TextureUploadCandidateSubresource mapped;
        check(plan.MapSubresource(view, mapped, &failure), "candidate maps verified 2D-array subresource");
        check(mapped.upload.mipLevel == sourceRecord.mipLevel - 2 && mapped.upload.arraySlice == sourceRecord.arrayLayer &&
                  mapped.upload.rowPitch == sourceRecord.rowPitch && mapped.upload.depthPitch == sourceRecord.slicePitch &&
                  mapped.upload.size == sourceRecord.byteSize,
              "candidate mapping preserves pitches and maps logical mip to physical mip");
        if (sourceIndex == 4)
            firstMapped = mapped;
        check(plan.MarkUploaded(mapped, &failure), "candidate marks successful subresource upload");
    }
    check(plan.IsComplete() && plan.GetUploadedSubresourceCount() == 4, "candidate becomes complete only after exact coverage");
    check(!plan.MarkUploaded(firstMapped, &failure) && failure.code == rendering::TextureUploadCandidateFailureCode::DuplicateSubresource,
          "candidate rejects duplicate upload coverage");

    rendering::TextureUploadCandidatePlan mixedPlan;
    rendering::TextureMipTransitionPlan mixedTransition;
    check(mixedPlan.Initialize(texture, 0, 4, capabilities, &failure) && mixedTransition.Build(4, 2, 2, 0, &failure),
          "candidate initializes a promotion target covering the full compact chain");
    for (u32 sourceIndex = 0; sourceIndex < 4; ++sourceIndex)
    {
        const textures::SubresourceRecord& sourceRecord = texture.GetSubresources()[sourceIndex];
        const textures::TextureSubresourceView view{sourceIndex, &sourceRecord,
                                                    {bytes[sourceIndex].data(), static_cast<u32>(sourceRecord.byteSize)}};
        rendering::TextureUploadCandidateSubresource mapped;
        check(mixedPlan.MapSubresource(view, mapped, &failure) && mixedPlan.MarkUploaded(mapped, &failure),
              "promotion candidate marks newly acquired high-detail subresources");
    }
    rendering::TextureCandidateCopySubresource firstCopied;
    for (u32 sharedIndex = 0; sharedIndex < mixedTransition.GetSharedMipCount(); ++sharedIndex)
        for (u32 arraySlice = 0; arraySlice < 2; ++arraySlice)
        {
            rendering::TextureCandidateCopySubresource mapped;
            check(mixedPlan.MapSharedCopy(mixedTransition, sharedIndex, arraySlice, mapped, &failure),
                  "promotion candidate maps a shared resident subresource");
            check(mapped.copy.source.mipLevel == sharedIndex && mapped.copy.destination.mipLevel == sharedIndex + 2 &&
                      mapped.copy.source.arraySlice == arraySlice && mapped.copy.destination.arraySlice == arraySlice,
                  "shared resident copy maps old and candidate physical subresources");
            if (sharedIndex == 0 && arraySlice == 0)
                firstCopied = mapped;
            check(mixedPlan.MarkCopied(mapped, &failure), "promotion candidate marks successful shared GPU copy");
        }
    check(mixedPlan.IsComplete() && mixedPlan.GetUploadedSubresourceCount() == 4 && mixedPlan.GetCopiedSubresourceCount() == 4 &&
              mixedPlan.GetInitializedSubresourceCount() == 8 &&
              mixedPlan.GetCoverage(firstCopied.coverageIndex) == rendering::TextureCandidateSubresourceCoverage::CopiedFromCurrent,
          "candidate completeness combines uploaded and copied subresources");
    check(!mixedPlan.MarkCopied(firstCopied, &failure) && failure.code == rendering::TextureUploadCandidateFailureCode::DuplicateSubresource,
          "candidate rejects duplicate shared-copy coverage");

    rendering::TextureUploadCandidatePlan malformedPlan;
    check(malformedPlan.InitializeMipTail(texture, capabilities, &failure), "malformed-view candidate initializes");
    const textures::SubresourceRecord& tailRecord = texture.GetSubresources()[4];
    const textures::TextureSubresourceView shortView{4, &tailRecord, {bytes[4].data(), static_cast<u32>(tailRecord.byteSize - 1)}};
    rendering::TextureUploadCandidateSubresource rejectedMapping;
    check(!malformedPlan.MapSubresource(shortView, rejectedMapping, &failure) &&
              failure.code == rendering::TextureUploadCandidateFailureCode::InvalidSubresource,
          "candidate rejects a verified view with the wrong byte extent");
    const textures::SubresourceRecord& outsideRecord = texture.GetSubresources()[0];
    const textures::TextureSubresourceView outsideView{0, &outsideRecord, {bytes[0].data(), static_cast<u32>(outsideRecord.byteSize)}};
    check(!malformedPlan.MapSubresource(outsideView, rejectedMapping, &failure) &&
              failure.code == rendering::TextureUploadCandidateFailureCode::InvalidSubresource,
          "candidate rejects a source mip outside its compact interval");

    rhi::Capabilities constrained = capabilities;
    constrained.maximumTextureDimension2D = 1;
    rendering::TextureUploadCandidatePlan constrainedPlan;
    check(!constrainedPlan.InitializeMipTail(texture, constrained, &failure) &&
              failure.code == rendering::TextureUploadCandidateFailureCode::CapacityExceeded,
          "candidate rejects a compact descriptor outside active RHI limits");

    std::array<std::array<u8, 4>, 12> cubeBytes{};
    std::array<textures::SubresourceBuildRecord, 12> cubeRecords{};
    recordIndex = 0;
    for (u16 layer = 0; layer < 2; ++layer)
        for (u8 face = 0; face < 6; ++face)
        {
            cubeRecords[recordIndex] = {0, layer, face, cubeBytes[recordIndex].data(), 4, 4, 4};
            ++recordIndex;
        }
    textures::BuildDescription cubeDescription;
    cubeDescription.dimension = textures::TextureDimension::Cube;
    cubeDescription.format = textures::PixelFormat::R8G8B8A8UNorm;
    cubeDescription.flags = textures::TextureFlags::DirectGpuUpload;
    cubeDescription.width = cubeDescription.height = 1;
    cubeDescription.depth = 1;
    cubeDescription.arrayLayers = 2;
    cubeDescription.mipCount = 1;
    cubeDescription.mipTailFirstLevel = 0;
    cubeDescription.subresources = {cubeRecords.data(), static_cast<u32>(cubeRecords.size())};
    ByteArray cookedCube{vanguard::memory::pools::Rendering::GetInstance()};
    textures::TextureFile cube;
    rendering::TextureUploadCandidatePlan cubePlan;
    check(OpenCooked(cubeDescription, cookedCube, cube) && cubePlan.InitializeMipTail(cube, capabilities, &failure) &&
              cubePlan.GetTextureDesc().dimension == rhi::TextureDimension::TextureCube && cubePlan.GetTextureDesc().arraySize == 12,
          "candidate builds a cube-array descriptor with total face slices");
    const textures::SubresourceRecord& cubeRecord = cube.GetSubresources()[11];
    const textures::TextureSubresourceView cubeView{11, &cubeRecord, {cubeBytes[11].data(), 4}};
    rendering::TextureUploadCandidateSubresource cubeMapped;
    check(cubePlan.MapSubresource(cubeView, cubeMapped, &failure) && cubeMapped.upload.mipLevel == 0 && cubeMapped.upload.arraySlice == 11,
          "candidate maps cube layer and face to one physical array slice");

    std::array<u8, 8> volumeBytes{};
    const std::array<textures::SubresourceBuildRecord, 1> volumeRecords{{{0, 0, 0, volumeBytes.data(), volumeBytes.size(), 2, 4}}};
    textures::BuildDescription volumeDescription;
    volumeDescription.dimension = textures::TextureDimension::Texture3D;
    volumeDescription.format = textures::PixelFormat::R8UNorm;
    volumeDescription.flags = textures::TextureFlags::DirectGpuUpload;
    volumeDescription.width = volumeDescription.height = volumeDescription.depth = 2;
    volumeDescription.arrayLayers = 1;
    volumeDescription.mipCount = 1;
    volumeDescription.mipTailFirstLevel = 0;
    volumeDescription.subresources = {volumeRecords.data(), 1};
    ByteArray cookedVolume{vanguard::memory::pools::Rendering::GetInstance()};
    textures::TextureFile volume;
    rendering::TextureUploadCandidatePlan volumePlan;
    check(OpenCooked(volumeDescription, cookedVolume, volume) && volumePlan.InitializeMipTail(volume, capabilities, &failure) &&
              volumePlan.GetTextureDesc().dimension == rhi::TextureDimension::Texture3D && volumePlan.GetTextureDesc().arraySize == 1,
          "candidate builds a 3D descriptor");
    const textures::SubresourceRecord& volumeRecord = volume.GetSubresources()[0];
    const textures::TextureSubresourceView volumeView{0, &volumeRecord, {volumeBytes.data(), static_cast<u32>(volumeBytes.size())}};
    rendering::TextureUploadCandidateSubresource volumeMapped;
    check(volumePlan.MapSubresource(volumeView, volumeMapped, &failure) && volumeMapped.upload.arraySlice == 0 &&
              volumeMapped.upload.depthPitch == 4 && volumeMapped.upload.size == 8,
          "candidate preserves one complete 3D mip and its depth pitch");
    constrained = capabilities;
    constrained.maximumTextureDimension3D = 1;
    rendering::TextureUploadCandidatePlan constrainedVolumePlan;
    check(!constrainedVolumePlan.InitializeMipTail(volume, constrained, &failure) &&
              failure.code == rendering::TextureUploadCandidateFailureCode::CapacityExceeded,
          "candidate rejects a 3D descriptor outside active RHI limits");

    std::array<u8, 4> sharedExponentBytes{};
    const std::array<textures::SubresourceBuildRecord, 1> sharedExponentRecords{{{0, 0, 0, sharedExponentBytes.data(), 4, 4, 4}}};
    textures::BuildDescription sharedExponentDescription;
    sharedExponentDescription.format = textures::PixelFormat::R9G9B9E5SharedExponent;
    sharedExponentDescription.flags = textures::TextureFlags::DirectGpuUpload;
    sharedExponentDescription.width = sharedExponentDescription.height = sharedExponentDescription.depth = 1;
    sharedExponentDescription.arrayLayers = 1;
    sharedExponentDescription.mipCount = 1;
    sharedExponentDescription.mipTailFirstLevel = 0;
    sharedExponentDescription.subresources = {sharedExponentRecords.data(), 1};
    ByteArray cookedSharedExponent{vanguard::memory::pools::Rendering::GetInstance()};
    textures::TextureFile sharedExponent;
    rendering::TextureUploadCandidatePlan unsupportedPlan;
    check(OpenCooked(sharedExponentDescription, cookedSharedExponent, sharedExponent) &&
              !unsupportedPlan.InitializeMipTail(sharedExponent, capabilities, &failure) &&
              failure.code == rendering::TextureUploadCandidateFailureCode::UnsupportedFormat,
          "candidate rejects VTEX R9G9B9E5 until the RHI exposes it");

    texture.Close();
    cube.Close();
    volume.Close();
    sharedExponent.Close();
}
