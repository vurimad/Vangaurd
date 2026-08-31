#include <vanguard/rendering/texture_upload_candidate.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        struct ExpectedSubresource
        {
            const textures::SubresourceRecord* record = nullptr;
            u32 sourceSubresource = textures::InvalidSubresourceIndex;
            u16 physicalMip = 0;
            u16 arraySlice = 0;
        };

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(TextureUploadCandidateFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TextureUploadCandidateFailure* const failure, const TextureUploadCandidateFailureCode code,
                                const char* const message, const u32 sourceSubresource = textures::InvalidSubresourceIndex,
                                const u32 mip = 0xffffffffu, const u32 layer = 0xffffffffu, const u32 face = 0xffffffffu) noexcept
        {
            if (failure != nullptr)
                *failure = {code, message, sourceSubresource, mip, layer, face};
            return false;
        }

        [[nodiscard]] bool MultiplyChecked(const u64 left, const u64 right, u64& result) noexcept
        {
            if (left != 0 && right > ~u64{0} / left)
                return false;
            result = left * right;
            return true;
        }

        [[nodiscard]] bool AddChecked(const u64 left, const u64 right, u64& result) noexcept
        {
            if (left > ~u64{0} - right)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool MapFormat(const textures::PixelFormat source, const textures::ColorSpace colorSpace,
                                     rhi::Format& destination) noexcept
        {
            const bool srgb = colorSpace == textures::ColorSpace::SRgb;
            switch (source)
            {
            case textures::PixelFormat::R8UNorm:
                destination = rhi::Format::R8UNorm;
                return !srgb;
            case textures::PixelFormat::R8SNorm:
                destination = rhi::Format::R8SNorm;
                return !srgb;
            case textures::PixelFormat::R8UInt:
                destination = rhi::Format::R8UInt;
                return !srgb;
            case textures::PixelFormat::R8G8UNorm:
                destination = rhi::Format::R8G8UNorm;
                return !srgb;
            case textures::PixelFormat::R8G8SNorm:
                destination = rhi::Format::R8G8SNorm;
                return !srgb;
            case textures::PixelFormat::R8G8UInt:
                destination = rhi::Format::R8G8UInt;
                return !srgb;
            case textures::PixelFormat::R8G8B8A8UNorm:
                destination = srgb ? rhi::Format::R8G8B8A8UNormSrgb : rhi::Format::R8G8B8A8UNorm;
                return true;
            case textures::PixelFormat::R8G8B8A8SNorm:
                destination = rhi::Format::R8G8B8A8SNorm;
                return !srgb;
            case textures::PixelFormat::R8G8B8A8UInt:
                destination = rhi::Format::R8G8B8A8UInt;
                return !srgb;
            case textures::PixelFormat::B8G8R8A8UNorm:
                destination = srgb ? rhi::Format::B8G8R8A8UNormSrgb : rhi::Format::B8G8R8A8UNorm;
                return true;
            case textures::PixelFormat::R16UNorm:
                destination = rhi::Format::R16UNorm;
                return !srgb;
            case textures::PixelFormat::R16SNorm:
                destination = rhi::Format::R16SNorm;
                return !srgb;
            case textures::PixelFormat::R16Float:
                destination = rhi::Format::R16Float;
                return !srgb;
            case textures::PixelFormat::R16G16UNorm:
                destination = rhi::Format::R16G16UNorm;
                return !srgb;
            case textures::PixelFormat::R16G16SNorm:
                destination = rhi::Format::R16G16SNorm;
                return !srgb;
            case textures::PixelFormat::R16G16Float:
                destination = rhi::Format::R16G16Float;
                return !srgb;
            case textures::PixelFormat::R16G16B16A16UNorm:
                destination = rhi::Format::R16G16B16A16UNorm;
                return !srgb;
            case textures::PixelFormat::R16G16B16A16SNorm:
                destination = rhi::Format::R16G16B16A16SNorm;
                return !srgb;
            case textures::PixelFormat::R16G16B16A16Float:
                destination = rhi::Format::R16G16B16A16Float;
                return !srgb;
            case textures::PixelFormat::R32Float:
                destination = rhi::Format::R32Float;
                return !srgb;
            case textures::PixelFormat::R32G32Float:
                destination = rhi::Format::R32G32Float;
                return !srgb;
            case textures::PixelFormat::R32G32B32A32Float:
                destination = rhi::Format::R32G32B32A32Float;
                return !srgb;
            case textures::PixelFormat::R10G10B10A2UNorm:
                destination = rhi::Format::R10G10B10A2UNorm;
                return !srgb;
            case textures::PixelFormat::R11G11B10Float:
                destination = rhi::Format::R11G11B10Float;
                return !srgb;
            case textures::PixelFormat::BC1UNorm:
                destination = srgb ? rhi::Format::BC1UNormSrgb : rhi::Format::BC1UNorm;
                return true;
            case textures::PixelFormat::BC2UNorm:
                destination = srgb ? rhi::Format::BC2UNormSrgb : rhi::Format::BC2UNorm;
                return true;
            case textures::PixelFormat::BC3UNorm:
                destination = srgb ? rhi::Format::BC3UNormSrgb : rhi::Format::BC3UNorm;
                return true;
            case textures::PixelFormat::BC4UNorm:
                destination = rhi::Format::BC4UNorm;
                return !srgb;
            case textures::PixelFormat::BC4SNorm:
                destination = rhi::Format::BC4SNorm;
                return !srgb;
            case textures::PixelFormat::BC5UNorm:
                destination = rhi::Format::BC5UNorm;
                return !srgb;
            case textures::PixelFormat::BC5SNorm:
                destination = rhi::Format::BC5SNorm;
                return !srgb;
            case textures::PixelFormat::BC6HUFloat:
                destination = rhi::Format::BC6HUFloat;
                return !srgb;
            case textures::PixelFormat::BC6HSFloat:
                destination = rhi::Format::BC6HSFloat;
                return !srgb;
            case textures::PixelFormat::BC7UNorm:
                destination = srgb ? rhi::Format::BC7UNormSrgb : rhi::Format::BC7UNorm;
                return true;
            case textures::PixelFormat::R9G9B9E5SharedExponent:
            case textures::PixelFormat::Count:
            default:
                destination = rhi::Format::Unknown;
                return false;
            }
        }

        [[nodiscard]] bool SameExpectedRecord(const textures::SubresourceRecord& left,
                                              const textures::SubresourceRecord& right) noexcept
        {
            return left.mipLevel == right.mipLevel && left.arrayLayer == right.arrayLayer && left.face == right.face &&
                   left.width == right.width && left.height == right.height && left.depth == right.depth &&
                   left.rowPitch == right.rowPitch && left.slicePitch == right.slicePitch && left.byteSize == right.byteSize;
        }
    } // namespace

    bool TextureMipTransitionPlan::Build(const u32 totalMipCount, const u32 guaranteedTailFirstMip,
                                         const u32 currentFirstResidentMip, const u32 requestedFirstResidentMip,
                                         TextureUploadCandidateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Reset();
        if (totalMipCount == 0 || guaranteedTailFirstMip >= totalMipCount || currentFirstResidentMip > guaranteedTailFirstMip)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidArgument,
                        "texture mip transition requires a valid compact suffix containing the guaranteed tail");

        m_totalMipCount = totalMipCount;
        m_guaranteedTailFirstMip = guaranteedTailFirstMip;
        m_currentFirstMip = currentFirstResidentMip;
        m_requestedFirstMip = requestedFirstResidentMip;
        m_targetFirstMip = requestedFirstResidentMip < guaranteedTailFirstMip ? requestedFirstResidentMip : guaranteedTailFirstMip;

        if (m_targetFirstMip == m_currentFirstMip)
        {
            m_kind = TextureMipTransitionKind::NoOp;
            return true;
        }

        if (m_targetFirstMip < m_currentFirstMip)
        {
            m_kind = TextureMipTransitionKind::Promotion;
            m_uploadFirstMip = m_targetFirstMip;
            m_uploadMipCount = m_currentFirstMip - m_targetFirstMip;
            m_sharedFirstMip = m_currentFirstMip;
        }
        else
        {
            m_kind = TextureMipTransitionKind::Demotion;
            m_sharedFirstMip = m_targetFirstMip;
        }
        m_sharedMipCount = m_totalMipCount - m_sharedFirstMip;
        return true;
    }

    void TextureMipTransitionPlan::Reset() noexcept
    {
        *this = {};
    }

    bool TextureMipTransitionPlan::GetSharedMipCopy(const u32 sharedMipIndex,
                                                    TextureMipSharedCopy& copy) const noexcept
    {
        copy = {};
        if (!IsValid() || m_kind == TextureMipTransitionKind::NoOp || sharedMipIndex >= m_sharedMipCount)
            return false;
        copy.assetMip = m_sharedFirstMip + sharedMipIndex;
        copy.sourcePhysicalMip = copy.assetMip - m_currentFirstMip;
        copy.destinationPhysicalMip = copy.assetMip - m_targetFirstMip;
        return true;
    }

    struct TextureUploadCandidatePlan::Impl
    {
        Impl() noexcept
            : expected(memory::pools::Rendering::GetInstance()), coverage(memory::pools::Rendering::GetInstance())
        {
        }

        rhi::TextureDesc desc;
        crypto::Digest256 contentFingerprint;
        containers::DynamicArray<ExpectedSubresource> expected;
        containers::DynamicArray<TextureCandidateSubresourceCoverage> coverage;
        u32 firstAssetMip = 0;
        u32 residentMipCount = 0;
        u32 uploadedCount = 0;
        u32 copiedCount = 0;
        u64 expectedBytes = 0;
    };

    TextureUploadCandidatePlan::~TextureUploadCandidatePlan()
    {
        Reset();
    }

    TextureUploadCandidatePlan::TextureUploadCandidatePlan(TextureUploadCandidatePlan&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    TextureUploadCandidatePlan& TextureUploadCandidatePlan::operator=(TextureUploadCandidatePlan&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    bool TextureUploadCandidatePlan::Initialize(const textures::TextureFile& texture, const u32 firstAssetMip,
                                                const u32 residentMipCount, const rhi::Capabilities& capabilities,
                                                TextureUploadCandidateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidState, "texture upload candidate plan is already initialized");
        if (!texture.IsOpen() || residentMipCount == 0 || firstAssetMip >= texture.GetMipCount() ||
            residentMipCount > static_cast<u32>(texture.GetMipCount()) - firstAssetMip)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidArgument, "texture candidate mip interval is invalid");
        if (!textures::HasFlag(texture.GetFlags(), textures::TextureFlags::DirectGpuUpload))
            return Fail(failure, TextureUploadCandidateFailureCode::DirectUploadRequired, "texture candidate requires cooker-authored direct GPU upload data");

        rhi::Format format = rhi::Format::Unknown;
        if (!MapFormat(texture.Format(), texture.GetSpace(), format))
            return Fail(failure, TextureUploadCandidateFailureCode::UnsupportedFormat, "VTEX format or color-space combination has no supported RHI upload format");

        rhi::TextureDimension dimension = rhi::TextureDimension::Texture2D;
        u32 faceCount = 1;
        switch (texture.GetDimension())
        {
        case textures::TextureDimension::Texture1D:
            dimension = rhi::TextureDimension::Texture1D;
            break;
        case textures::TextureDimension::Texture2D:
            dimension = rhi::TextureDimension::Texture2D;
            break;
        case textures::TextureDimension::Texture3D:
            dimension = rhi::TextureDimension::Texture3D;
            break;
        case textures::TextureDimension::Cube:
            dimension = rhi::TextureDimension::TextureCube;
            faceCount = 6;
            break;
        default:
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidArgument, "VTEX dimension is invalid");
        }

        u64 arraySize64 = 0;
        if (!MultiplyChecked(texture.GetArrayLayers(), faceCount, arraySize64) || arraySize64 == 0 || arraySize64 > 0xffffu)
            return Fail(failure, TextureUploadCandidateFailureCode::ArithmeticOverflow, "texture candidate array-slice count overflowed the RHI descriptor");
        const u32 arraySize = texture.GetDimension() == textures::TextureDimension::Texture3D ? 1u : static_cast<u32>(arraySize64);
        const u32 width = textures::CalculateMipExtent(texture.GetWidth(), static_cast<u8>(firstAssetMip));
        const u32 height = textures::CalculateMipExtent(texture.GetHeight(), static_cast<u8>(firstAssetMip));
        const u32 depth = textures::CalculateMipExtent(texture.GetDepth(), static_cast<u8>(firstAssetMip));
        const bool exceeds3D = dimension == rhi::TextureDimension::Texture3D &&
                               (capabilities.maximumTextureDimension3D == 0 || width > capabilities.maximumTextureDimension3D ||
                                height > capabilities.maximumTextureDimension3D || depth > capabilities.maximumTextureDimension3D);
        const bool exceedsArrayTexture = dimension != rhi::TextureDimension::Texture3D &&
                                         (capabilities.maximumTextureDimension2D == 0 || capabilities.maximumTextureArrayLayers == 0 ||
                                          width > capabilities.maximumTextureDimension2D || height > capabilities.maximumTextureDimension2D ||
                                          arraySize > capabilities.maximumTextureArrayLayers);
        if (exceeds3D || exceedsArrayTexture)
            return Fail(failure, TextureUploadCandidateFailureCode::CapacityExceeded, "compact texture candidate exceeds active RHI texture limits");

        u64 expectedCount64 = 0;
        if (!MultiplyChecked(residentMipCount, arraySize, expectedCount64) || expectedCount64 == 0 || expectedCount64 > 0xffffffffu)
            return Fail(failure, TextureUploadCandidateFailureCode::ArithmeticOverflow, "texture candidate subresource count overflowed");

        Impl* const impl = AllocateObject<Impl>();
        if (impl == nullptr)
            return Fail(failure, TextureUploadCandidateFailureCode::CapacityExceeded, "texture candidate plan allocation failed");
        impl->desc.extent = {width, height, depth};
        impl->desc.dimension = dimension;
        impl->desc.format = format;
        impl->desc.mipCount = static_cast<u16>(residentMipCount);
        impl->desc.arraySize = static_cast<u16>(arraySize);
        impl->desc.sampleCount = 1;
        impl->desc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopySource | rhi::TextureUsage::CopyDestination;
        impl->desc.initialState = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
        impl->desc.virtualResource = false;
        impl->contentFingerprint = texture.GetContentFingerprint();
        impl->firstAssetMip = firstAssetMip;
        impl->residentMipCount = residentMipCount;
        impl->expected.Reserve(static_cast<u32>(expectedCount64));
        impl->coverage.Resize(static_cast<u32>(expectedCount64));
        if (impl->coverage.Size() != static_cast<u32>(expectedCount64))
        {
            DeleteObject(impl);
            return Fail(failure, TextureUploadCandidateFailureCode::CapacityExceeded, "texture candidate coverage allocation failed");
        }
        for (TextureCandidateSubresourceCoverage& value : impl->coverage)
            value = TextureCandidateSubresourceCoverage::Missing;

        for (u32 localMip = 0; localMip < residentMipCount; ++localMip)
        {
            const u32 assetMip = firstAssetMip + localMip;
            for (u32 layer = 0; layer < texture.GetArrayLayers(); ++layer)
            {
                for (u32 face = 0; face < faceCount; ++face)
                {
                    const u32 sourceSubresource = texture.FindSubresource(static_cast<u8>(assetMip), static_cast<u16>(layer), static_cast<u8>(face));
                    if (sourceSubresource == textures::InvalidSubresourceIndex || sourceSubresource >= texture.GetSubresources().Size())
                    {
                        DeleteObject(impl);
                        return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                                    "texture candidate is missing a required source subresource", sourceSubresource, assetMip, layer, face);
                    }
                    const textures::SubresourceRecord& record = texture.GetSubresources()[sourceSubresource];
                    if (!textures::HasFlag(record.flags, textures::SubresourceFlags::DirectGpuUpload))
                    {
                        DeleteObject(impl);
                        return Fail(failure, TextureUploadCandidateFailureCode::DirectUploadRequired,
                                    "texture candidate source subresource is not direct-upload data", sourceSubresource, assetMip, layer, face);
                    }
                    const u32 arraySlice = dimension == rhi::TextureDimension::Texture3D ? 0u :
                                               dimension == rhi::TextureDimension::TextureCube ? layer * 6u + face : layer;
                    impl->expected.PushBack({&record, sourceSubresource, static_cast<u16>(localMip), static_cast<u16>(arraySlice)});
                    u64 newExpectedBytes = 0;
                    if (!AddChecked(impl->expectedBytes, record.byteSize, newExpectedBytes))
                    {
                        DeleteObject(impl);
                        return Fail(failure, TextureUploadCandidateFailureCode::ArithmeticOverflow,
                                    "texture candidate byte count overflowed", sourceSubresource, assetMip, layer, face);
                    }
                    impl->expectedBytes = newExpectedBytes;
                }
            }
        }
        if (impl->expected.Size() != static_cast<u32>(expectedCount64))
        {
            DeleteObject(impl);
            return Fail(failure, TextureUploadCandidateFailureCode::CapacityExceeded, "texture candidate expected-subresource allocation failed");
        }
        m_impl = impl;
        return true;
    }

    bool TextureUploadCandidatePlan::InitializeMipTail(const textures::TextureFile& texture,
                                                       const rhi::Capabilities& capabilities,
                                                       TextureUploadCandidateFailure* const failure) noexcept
    {
        if (!texture.IsOpen())
            return Initialize(texture, 0, 0, capabilities, failure);
        const u32 first = texture.GetMipTailFirstLevel();
        return Initialize(texture, first, static_cast<u32>(texture.GetMipCount()) - first, capabilities, failure);
    }

    void TextureUploadCandidatePlan::Reset() noexcept
    {
        DeleteObject(m_impl);
        m_impl = nullptr;
    }

    bool TextureUploadCandidatePlan::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    const rhi::TextureDesc& TextureUploadCandidatePlan::GetTextureDesc() const noexcept
    {
        static const rhi::TextureDesc invalid{};
        return m_impl != nullptr ? m_impl->desc : invalid;
    }

    const crypto::Digest256& TextureUploadCandidatePlan::GetContentFingerprint() const noexcept
    {
        static const crypto::Digest256 invalid{};
        return m_impl != nullptr ? m_impl->contentFingerprint : invalid;
    }

    u32 TextureUploadCandidatePlan::GetFirstAssetMip() const noexcept
    {
        return m_impl != nullptr ? m_impl->firstAssetMip : 0;
    }

    u32 TextureUploadCandidatePlan::GetResidentMipCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->residentMipCount : 0;
    }

    u32 TextureUploadCandidatePlan::GetExpectedSubresourceCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->expected.Size() : 0;
    }

    u32 TextureUploadCandidatePlan::GetUploadedSubresourceCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->uploadedCount : 0;
    }

    u32 TextureUploadCandidatePlan::GetCopiedSubresourceCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->copiedCount : 0;
    }

    u32 TextureUploadCandidatePlan::GetInitializedSubresourceCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->uploadedCount + m_impl->copiedCount : 0;
    }

    TextureCandidateSubresourceCoverage TextureUploadCandidatePlan::GetCoverage(const u32 coverageIndex) const noexcept
    {
        return m_impl != nullptr && coverageIndex < m_impl->coverage.Size()
                   ? m_impl->coverage[coverageIndex]
                   : TextureCandidateSubresourceCoverage::Missing;
    }

    u64 TextureUploadCandidatePlan::GetExpectedByteCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->expectedBytes : 0;
    }

    bool TextureUploadCandidatePlan::MapSubresource(const textures::TextureSubresourceView& source,
                                                    TextureUploadCandidateSubresource& mapped,
                                                    TextureUploadCandidateFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        mapped = {};
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidState, "texture candidate plan is not initialized");
        if (source.record == nullptr || source.subresource == textures::InvalidSubresourceIndex || source.bytes.Data() == nullptr || source.bytes.Empty())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "texture upload source view is invalid", source.subresource);
        const textures::SubresourceRecord& record = *source.record;
        if (record.mipLevel < m_impl->firstAssetMip || record.mipLevel >= m_impl->firstAssetMip + m_impl->residentMipCount)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "texture upload source mip is outside the candidate interval",
                        source.subresource, record.mipLevel, record.arrayLayer, record.face);
        const u32 physicalMip = record.mipLevel - m_impl->firstAssetMip;
        u32 arraySlice = 0;
        if (m_impl->desc.dimension == rhi::TextureDimension::Texture3D)
        {
            if (record.arrayLayer != 0 || record.face != 0)
                return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "3D texture upload source has an array layer or cube face",
                            source.subresource, record.mipLevel, record.arrayLayer, record.face);
        }
        else if (m_impl->desc.dimension == rhi::TextureDimension::TextureCube)
        {
            if (record.face >= 6)
                return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "cube texture upload source has an invalid face",
                            source.subresource, record.mipLevel, record.arrayLayer, record.face);
            arraySlice = static_cast<u32>(record.arrayLayer) * 6u + record.face;
        }
        else
        {
            if (record.face != 0)
                return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "non-cube texture upload source has a cube face",
                            source.subresource, record.mipLevel, record.arrayLayer, record.face);
            arraySlice = record.arrayLayer;
        }
        if (arraySlice >= m_impl->desc.arraySize)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "texture upload source array slice exceeds the candidate",
                        source.subresource, record.mipLevel, record.arrayLayer, record.face);
        const u64 coverage64 = static_cast<u64>(physicalMip) * m_impl->desc.arraySize + arraySlice;
        if (coverage64 >= m_impl->expected.Size())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "texture upload source maps outside candidate coverage",
                        source.subresource, record.mipLevel, record.arrayLayer, record.face);
        const u32 coverage = static_cast<u32>(coverage64);
        const ExpectedSubresource& expected = m_impl->expected[coverage];
        if (expected.record != source.record || expected.sourceSubresource != source.subresource || expected.physicalMip != physicalMip ||
            expected.arraySlice != arraySlice || !SameExpectedRecord(*expected.record, record) || source.bytes.Size() != record.byteSize)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "texture upload source does not match the expected verified subresource",
                        source.subresource, record.mipLevel, record.arrayLayer, record.face);
        mapped.upload = {source.bytes.Data(), source.bytes.Size(), record.rowPitch, record.slicePitch,
                         static_cast<u16>(physicalMip), static_cast<u16>(arraySlice)};
        mapped.sourceSubresource = source.subresource;
        mapped.coverageIndex = coverage;
        return true;
    }

    bool TextureUploadCandidatePlan::MarkUploaded(const TextureUploadCandidateSubresource& mapped,
                                                  TextureUploadCandidateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidState, "texture candidate plan is not initialized");
        if (!mapped.IsValid() || mapped.coverageIndex >= m_impl->expected.Size())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "mapped texture upload subresource is invalid", mapped.sourceSubresource);
        const ExpectedSubresource& expected = m_impl->expected[mapped.coverageIndex];
        if (expected.sourceSubresource != mapped.sourceSubresource || expected.physicalMip != mapped.upload.mipLevel ||
            expected.arraySlice != mapped.upload.arraySlice)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource, "mapped texture upload subresource does not belong to this candidate",
                        mapped.sourceSubresource);
        if (m_impl->coverage[mapped.coverageIndex] != TextureCandidateSubresourceCoverage::Missing)
            return Fail(failure, TextureUploadCandidateFailureCode::DuplicateSubresource, "texture candidate subresource was initialized more than once",
                        mapped.sourceSubresource, expected.record->mipLevel, expected.record->arrayLayer, expected.record->face);
        m_impl->coverage[mapped.coverageIndex] = TextureCandidateSubresourceCoverage::UploadedFromSource;
        ++m_impl->uploadedCount;
        return true;
    }

    bool TextureUploadCandidatePlan::MapSharedCopy(const TextureMipTransitionPlan& transition, const u32 sharedMipIndex,
                                                   const u32 arraySlice, TextureCandidateCopySubresource& mapped,
                                                   TextureUploadCandidateFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        mapped = {};
        if (m_impl == nullptr || !transition.IsValid())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidState,
                        "shared texture copy requires initialized candidate and transition plans");
        if (transition.GetKind() == TextureMipTransitionKind::NoOp ||
            transition.GetTargetFirstMip() != m_impl->firstAssetMip ||
            transition.GetCandidateMipCount() != m_impl->residentMipCount || arraySlice >= m_impl->desc.arraySize)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidArgument,
                        "shared texture copy does not match the compact candidate interval");

        TextureMipSharedCopy shared;
        if (!transition.GetSharedMipCopy(sharedMipIndex, shared))
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                        "shared texture copy index is outside the transition interval");
        const u64 coverage64 = static_cast<u64>(shared.destinationPhysicalMip) * m_impl->desc.arraySize + arraySlice;
        if (coverage64 >= m_impl->expected.Size())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                        "shared texture copy maps outside candidate coverage", textures::InvalidSubresourceIndex,
                        shared.assetMip, arraySlice);
        const u32 coverageIndex = static_cast<u32>(coverage64);
        const ExpectedSubresource& expected = m_impl->expected[coverageIndex];
        if (expected.record == nullptr || expected.record->mipLevel != shared.assetMip ||
            expected.physicalMip != shared.destinationPhysicalMip || expected.arraySlice != arraySlice)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                        "shared texture copy does not match the expected candidate subresource",
                        expected.sourceSubresource, shared.assetMip, expected.record != nullptr ? expected.record->arrayLayer : 0xffffffffu,
                        expected.record != nullptr ? expected.record->face : 0xffffffffu);

        mapped.copy.source = {static_cast<u16>(shared.sourcePhysicalMip), static_cast<u16>(arraySlice)};
        mapped.copy.destination = {static_cast<u16>(shared.destinationPhysicalMip), static_cast<u16>(arraySlice)};
        mapped.assetMip = shared.assetMip;
        mapped.coverageIndex = coverageIndex;
        mapped.byteSize = expected.record->byteSize;
        return true;
    }

    bool TextureUploadCandidatePlan::MarkCopied(const TextureCandidateCopySubresource& mapped,
                                                TextureUploadCandidateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidState, "texture candidate plan is not initialized");
        if (!mapped.IsValid() || mapped.coverageIndex >= m_impl->expected.Size())
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                        "mapped shared texture copy is invalid", textures::InvalidSubresourceIndex, mapped.assetMip);
        const ExpectedSubresource& expected = m_impl->expected[mapped.coverageIndex];
        if (expected.record == nullptr || expected.record->mipLevel != mapped.assetMip ||
            expected.physicalMip != mapped.copy.destination.mipLevel || expected.arraySlice != mapped.copy.destination.arraySlice)
            return Fail(failure, TextureUploadCandidateFailureCode::InvalidSubresource,
                        "mapped shared texture copy does not belong to this candidate", expected.sourceSubresource, mapped.assetMip);
        if (m_impl->coverage[mapped.coverageIndex] != TextureCandidateSubresourceCoverage::Missing)
            return Fail(failure, TextureUploadCandidateFailureCode::DuplicateSubresource,
                        "texture candidate subresource was initialized more than once", expected.sourceSubresource,
                        expected.record->mipLevel, expected.record->arrayLayer, expected.record->face);
        m_impl->coverage[mapped.coverageIndex] = TextureCandidateSubresourceCoverage::CopiedFromCurrent;
        ++m_impl->copiedCount;
        return true;
    }

    bool TextureUploadCandidatePlan::IsComplete() const noexcept
    {
        return m_impl != nullptr && m_impl->uploadedCount + m_impl->copiedCount == m_impl->expected.Size();
    }
} // namespace vanguard::rendering
