#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/memory/memory.hpp>

namespace
{
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace textures = vanguard::textures;
    namespace tools = vanguard::texture_tools;
    using vanguard::u8;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::usize;

    constexpr u32 DdsMagic = 0x20534444u;
    constexpr u32 DdsHeaderSize = 124;
    constexpr u32 DdsPixelFormatSize = 32;
    constexpr u32 DdsMinimumHeaderBytes = 128;
    constexpr u32 DdsDx10HeaderBytes = 148;
    constexpr u32 DdsRequiredFlags = 0x00001007u;
    constexpr u32 DdsDepthFlag = 0x00800000u;
    constexpr u32 DdsPixelFormatFourCc = 0x00000004u;
    constexpr u32 DdsCube = 0x00000200u;
    constexpr u32 DdsCubeAllFaces = 0x0000fc00u;
    constexpr u32 DdsResourceMiscCube = 0x00000004u;

    [[nodiscard]] constexpr u32 FourCc(const char a, const char b, const char c, const char d) noexcept
    {
        return static_cast<u32>(static_cast<u8>(a)) | static_cast<u32>(static_cast<u8>(b)) << 8u |
               static_cast<u32>(static_cast<u8>(c)) << 16u | static_cast<u32>(static_cast<u8>(d)) << 24u;
    }

    [[nodiscard]] u32 ReadU32(const u8* bytes) noexcept
    {
        return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8u |
               static_cast<u32>(bytes[2]) << 16u | static_cast<u32>(bytes[3]) << 24u;
    }

    [[nodiscard]] bool MapDxgiFormat(const u32 dxgi, textures::PixelFormat& format,
                                     textures::ColorSpace& colorSpace) noexcept
    {
        colorSpace = textures::ColorSpace::Linear;
        switch (dxgi)
        {
        case 71: format = textures::PixelFormat::BC1UNorm; return true;
        case 72: format = textures::PixelFormat::BC1UNorm; colorSpace = textures::ColorSpace::SRgb; return true;
        case 74: format = textures::PixelFormat::BC2UNorm; return true;
        case 75: format = textures::PixelFormat::BC2UNorm; colorSpace = textures::ColorSpace::SRgb; return true;
        case 77: format = textures::PixelFormat::BC3UNorm; return true;
        case 78: format = textures::PixelFormat::BC3UNorm; colorSpace = textures::ColorSpace::SRgb; return true;
        case 80: format = textures::PixelFormat::BC4UNorm; return true;
        case 81: format = textures::PixelFormat::BC4SNorm; return true;
        case 83: format = textures::PixelFormat::BC5UNorm; return true;
        case 84: format = textures::PixelFormat::BC5SNorm; return true;
        case 95: format = textures::PixelFormat::BC6HUFloat; return true;
        case 96: format = textures::PixelFormat::BC6HSFloat; return true;
        case 98: format = textures::PixelFormat::BC7UNorm; return true;
        case 99: format = textures::PixelFormat::BC7UNorm; colorSpace = textures::ColorSpace::SRgb; return true;
        default: return false;
        }
    }

    [[nodiscard]] bool MapLegacyFormat(const u32 fourCc, textures::PixelFormat& format) noexcept
    {
        switch (fourCc)
        {
        case FourCc('D', 'X', 'T', '1'): format = textures::PixelFormat::BC1UNorm; return true;
        case FourCc('D', 'X', 'T', '3'): format = textures::PixelFormat::BC2UNorm; return true;
        case FourCc('D', 'X', 'T', '5'): format = textures::PixelFormat::BC3UNorm; return true;
        case FourCc('A', 'T', 'I', '1'):
        case FourCc('B', 'C', '4', 'U'): format = textures::PixelFormat::BC4UNorm; return true;
        case FourCc('B', 'C', '4', 'S'): format = textures::PixelFormat::BC4SNorm; return true;
        case FourCc('A', 'T', 'I', '2'):
        case FourCc('B', 'C', '5', 'U'): format = textures::PixelFormat::BC5UNorm; return true;
        case FourCc('B', 'C', '5', 'S'): format = textures::PixelFormat::BC5SNorm; return true;
        default: return false;
        }
    }

    [[nodiscard]] crypto::Digest256 Fingerprint(const tools::TextureImportRequest& request) noexcept
    {
        return request.sourceFingerprint.IsEmpty()
            ? crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes()) : request.sourceFingerprint;
    }

    [[nodiscard]] u32 ExpectedSubresourceCount(const textures::TextureDimension dimension, const u16 arrayLayers,
                                               const u8 mipCount) noexcept
    {
        if (dimension == textures::TextureDimension::Texture3D) return mipCount;
        const u32 faceCount = dimension == textures::TextureDimension::Cube ? 6u : 1u;
        return static_cast<u32>(arrayLayers) * faceCount * mipCount;
    }
}

namespace vanguard::texture_tools
{
    ImportedGpuTexture::ImportedGpuTexture() noexcept
        : m_payload(memory::pools::Assets::GetInstance()),
          m_ownedSubresources(memory::pools::Assets::GetInstance()),
          m_views(memory::pools::Assets::GetInstance()) {}

    ImportedGpuTexture::ImportedGpuTexture(ImportedGpuTexture&& other) noexcept
        : m_dimension(other.m_dimension), m_format(other.m_format), m_colorSpace(other.m_colorSpace),
          m_width(other.m_width), m_height(other.m_height), m_depth(other.m_depth),
          m_arrayLayers(other.m_arrayLayers), m_mipCount(other.m_mipCount), m_writeOffset(other.m_writeOffset),
          m_sourceFingerprint(other.m_sourceFingerprint), m_importer(other.m_importer),
          m_importerVersion(other.m_importerVersion),
          m_payload(static_cast<containers::DynamicArray<u8>&&>(other.m_payload)),
          m_ownedSubresources(static_cast<containers::DynamicArray<OwnedSubresource>&&>(other.m_ownedSubresources)),
          m_views(memory::pools::Assets::GetInstance())
    {
        other.Reset();
    }

    ImportedGpuTexture& ImportedGpuTexture::operator=(ImportedGpuTexture&& other) noexcept
    {
        if (this != &other)
        {
            m_dimension = other.m_dimension;
            m_format = other.m_format;
            m_colorSpace = other.m_colorSpace;
            m_width = other.m_width;
            m_height = other.m_height;
            m_depth = other.m_depth;
            m_arrayLayers = other.m_arrayLayers;
            m_mipCount = other.m_mipCount;
            m_writeOffset = other.m_writeOffset;
            m_sourceFingerprint = other.m_sourceFingerprint;
            m_importer = other.m_importer;
            m_importerVersion = other.m_importerVersion;
            m_payload = static_cast<containers::DynamicArray<u8>&&>(other.m_payload);
            m_ownedSubresources = static_cast<containers::DynamicArray<OwnedSubresource>&&>(other.m_ownedSubresources);
            m_views.Clear();
            other.Reset();
        }
        return *this;
    }

    void ImportedGpuTexture::Reset() noexcept
    {
        m_dimension = textures::TextureDimension::Texture2D;
        m_format = textures::PixelFormat::BC1UNorm;
        m_colorSpace = textures::ColorSpace::Linear;
        m_width = m_height = m_depth = 0;
        m_arrayLayers = 0;
        m_mipCount = 0;
        m_writeOffset = 0;
        m_sourceFingerprint = {};
        m_importer = InvalidTextureImporterId;
        m_importerVersion = 0;
        m_payload.Clear();
        m_ownedSubresources.Clear();
        m_views.Clear();
    }

    bool ImportedGpuTexture::IsValid() const noexcept
    {
        if (m_width == 0 || m_height == 0 || m_depth == 0 || m_arrayLayers == 0 || m_mipCount == 0 ||
            m_importer == InvalidTextureImporterId || m_importerVersion == 0 || m_writeOffset != m_payload.Size() ||
            m_ownedSubresources.Size() != ExpectedSubresourceCount(m_dimension, m_arrayLayers, m_mipCount))
            return false;
        for (u32 index = 0; index < m_ownedSubresources.Size(); ++index)
            if (!m_ownedSubresources[index].initialized) return false;
        return true;
    }

    GpuTextureSource ImportedGpuTexture::Source() const noexcept
    {
        if (!IsValid()) return {};
        m_views.Resize(m_ownedSubresources.Size());
        if (m_views.Size() != m_ownedSubresources.Size()) return {};
        for (u32 index = 0; index < m_ownedSubresources.Size(); ++index)
        {
            const OwnedSubresource& owned = m_ownedSubresources[index];
            m_views[index] = {owned.mipLevel, owned.arrayLayer, owned.face, m_payload.TypedData() + owned.byteOffset,
                              owned.byteSize, owned.rowPitch, owned.slicePitch};
        }
        return {m_dimension, m_format, m_colorSpace, m_width, m_height, m_depth, m_arrayLayers, m_mipCount,
                m_sourceFingerprint, {m_views.TypedData(), m_views.Size()}};
    }

    TextureImporterId ImportedGpuTexture::Importer() const noexcept { return m_importer; }
    u32 ImportedGpuTexture::ImporterVersion() const noexcept { return m_importerVersion; }

    TextureImportResult ImportedGpuTexture::Initialize(
        const textures::TextureDimension dimension, const textures::PixelFormat format,
        const textures::ColorSpace colorSpace, const u32 width, const u32 height, const u32 depth,
        const u16 arrayLayers, const u8 mipCount, const u32 subresourceCount, const u64 totalBytes,
        const crypto::Digest256& sourceFingerprint, const TextureImporterId importer, const u32 importerVersion,
        const TextureImportLimits& limits) noexcept
    {
        Reset();
        const textures::FormatInfo info = textures::GetFormatInfo(format);
        const u32 expectedCount = ExpectedSubresourceCount(dimension, arrayLayers, mipCount);
        if (dimension > textures::TextureDimension::Cube || format >= textures::PixelFormat::Count ||
            colorSpace > textures::ColorSpace::SRgb || info.bytesPerBlock == 0 || width == 0 || height == 0 || depth == 0 ||
            width > limits.maximumDimension || height > limits.maximumDimension || depth > limits.maximumDimension ||
            arrayLayers == 0 || mipCount == 0 || subresourceCount != expectedCount ||
            subresourceCount > limits.maximumImages || totalBytes == 0 || totalBytes > limits.maximumDecodedBytes ||
            totalBytes > 0xffffffffull || importer == InvalidTextureImporterId || importerVersion == 0 ||
            (colorSpace == textures::ColorSpace::SRgb && !info.supportsSRgb))
            return totalBytes > limits.maximumDecodedBytes || totalBytes > 0xffffffffull ||
                           width > limits.maximumDimension || height > limits.maximumDimension ||
                           depth > limits.maximumDimension || subresourceCount > limits.maximumImages
                       ? TextureImportResult::LimitExceeded : TextureImportResult::InvalidArgument;
        m_payload.Resize(static_cast<u32>(totalBytes));
        m_ownedSubresources.Resize(subresourceCount);
        if (m_payload.Size() != totalBytes || m_ownedSubresources.Size() != subresourceCount)
        {
            Reset();
            return TextureImportResult::OutOfMemory;
        }
        m_dimension = dimension;
        m_format = format;
        m_colorSpace = colorSpace;
        m_width = width;
        m_height = height;
        m_depth = depth;
        m_arrayLayers = arrayLayers;
        m_mipCount = mipCount;
        m_sourceFingerprint = sourceFingerprint;
        m_importer = importer;
        m_importerVersion = importerVersion;
        return TextureImportResult::Success;
    }

    TextureImportResult ImportedGpuTexture::SetSubresource(
        const u32 index, const u8 mipLevel, const u16 arrayLayer, const u8 face, const void* data,
        const usize byteSize, const u32 rowPitch, const u32 slicePitch) noexcept
    {
        if (index >= m_ownedSubresources.Size() || data == nullptr || byteSize == 0 || byteSize > 0xffffffffull ||
            m_ownedSubresources[index].initialized || mipLevel >= m_mipCount || arrayLayer >= m_arrayLayers ||
            face >= (m_dimension == textures::TextureDimension::Cube ? 6u : 1u) || rowPitch == 0 || slicePitch == 0 ||
            byteSize > m_payload.Size() - m_writeOffset)
            return TextureImportResult::InvalidArgument;
        const auto* const source = static_cast<const u8*>(data);
        const u32 copyByteSize = static_cast<u32>(byteSize);
        for (u32 byte = 0; byte < copyByteSize; ++byte) m_payload[m_writeOffset + byte] = source[byte];
        m_ownedSubresources[index] = {mipLevel, arrayLayer, face, m_writeOffset, static_cast<u32>(byteSize),
                                      rowPitch, slicePitch, true};
        m_writeOffset += static_cast<u32>(byteSize);
        return TextureImportResult::Success;
    }

    TextureImportResult ImportDdsTexture(const TextureImportRequest& request, ImportedGpuTexture& output,
                                         GpuTextureImportReport* const report) noexcept
    {
        output.Reset();
        if (!IsInitialized()) return TextureImportResult::InvalidState;
        if (request.encoded.Data() == nullptr || request.encoded.Count() < DdsMinimumHeaderBytes ||
            request.colorSpace > ImportedColorSpace::SRgb || request.limits.maximumDimension == 0 ||
            request.limits.maximumImages == 0 || request.limits.maximumDecodedBytes == 0 ||
            request.channels.red != ImportedChannel::Red || request.channels.green != ImportedChannel::Green ||
            request.channels.blue != ImportedChannel::Blue || request.channels.alpha != ImportedChannel::Alpha)
            return TextureImportResult::InvalidArgument;

        const u8* const bytes = request.encoded.Data();
        const usize size = request.encoded.SizeInBytes();
        if (ReadU32(bytes) != DdsMagic || ReadU32(bytes + 4) != DdsHeaderSize ||
            ReadU32(bytes + 76) != DdsPixelFormatSize || (ReadU32(bytes + 8) & DdsRequiredFlags) != DdsRequiredFlags)
            return TextureImportResult::DecodeFailure;

        const u32 flags = ReadU32(bytes + 8);
        u32 height = ReadU32(bytes + 12);
        const u32 width = ReadU32(bytes + 16);
        u32 depth = ReadU32(bytes + 24);
        const u32 declaredMipCount = ReadU32(bytes + 28);
        const u32 pixelFormatFlags = ReadU32(bytes + 80);
        const u32 fourCc = ReadU32(bytes + 84);
        const u32 caps2 = ReadU32(bytes + 112);
        const bool hasDx10Header = (pixelFormatFlags & DdsPixelFormatFourCc) != 0 && fourCc == FourCc('D', 'X', '1', '0');
        if (width == 0 || height == 0 || declaredMipCount > 255 || (hasDx10Header && size < DdsDx10HeaderBytes))
            return TextureImportResult::DecodeFailure;

        textures::TextureDimension dimension = textures::TextureDimension::Texture2D;
        textures::PixelFormat format = textures::PixelFormat::BC1UNorm;
        textures::ColorSpace embeddedColorSpace = textures::ColorSpace::Linear;
        u32 arrayLayers32 = 1;
        usize payloadOffset = DdsMinimumHeaderBytes;
        if (hasDx10Header)
        {
            const u32 dxgiFormat = ReadU32(bytes + 128);
            const u32 resourceDimension = ReadU32(bytes + 132);
            const u32 miscFlag = ReadU32(bytes + 136);
            arrayLayers32 = ReadU32(bytes + 140);
            const u32 alphaMode = ReadU32(bytes + 144) & 0x7u;
            payloadOffset = DdsDx10HeaderBytes;
            if (arrayLayers32 == 0 || alphaMode > 4 || alphaMode == 2 ||
                !MapDxgiFormat(dxgiFormat, format, embeddedColorSpace))
                return TextureImportResult::UnsupportedFormat;
            if (resourceDimension == 2)
            {
                if (height != 1) return TextureImportResult::DecodeFailure;
                dimension = textures::TextureDimension::Texture1D;
                height = depth = 1;
            }
            else if (resourceDimension == 3)
            {
                depth = 1;
                if ((miscFlag & DdsResourceMiscCube) != 0)
                {
                    dimension = textures::TextureDimension::Cube;
                    if (width != height) return TextureImportResult::DecodeFailure;
                }
            }
            else if (resourceDimension == 4)
            {
                if ((flags & DdsDepthFlag) == 0 || arrayLayers32 != 1 || depth == 0 ||
                    (miscFlag & DdsResourceMiscCube) != 0)
                    return TextureImportResult::DecodeFailure;
                dimension = textures::TextureDimension::Texture3D;
            }
            else return TextureImportResult::DecodeFailure;
        }
        else
        {
            if ((pixelFormatFlags & DdsPixelFormatFourCc) == 0 || fourCc == FourCc('D', 'X', 'T', '2') ||
                fourCc == FourCc('D', 'X', 'T', '4') || !MapLegacyFormat(fourCc, format))
                return TextureImportResult::UnsupportedFormat;
            if ((flags & DdsDepthFlag) != 0)
            {
                if (depth == 0 || (caps2 & DdsCube) != 0) return TextureImportResult::DecodeFailure;
                dimension = textures::TextureDimension::Texture3D;
            }
            else
            {
                depth = 1;
                if ((caps2 & DdsCube) != 0)
                {
                    if ((caps2 & DdsCubeAllFaces) != DdsCubeAllFaces || width != height)
                        return TextureImportResult::DecodeFailure;
                    dimension = textures::TextureDimension::Cube;
                }
            }
        }

        const u8 mipCount = static_cast<u8>(declaredMipCount == 0 ? 1 : declaredMipCount);
        if (mipCount > textures::CalculateMipCount(width, height, depth) || arrayLayers32 > 65535 ||
            width > request.limits.maximumDimension || height > request.limits.maximumDimension ||
            depth > request.limits.maximumDimension)
            return TextureImportResult::LimitExceeded;
        const u16 arrayLayers = static_cast<u16>(arrayLayers32);
        const u32 subresourceCount = ExpectedSubresourceCount(dimension, arrayLayers, mipCount);
        if (subresourceCount == 0 || subresourceCount > request.limits.maximumImages)
            return TextureImportResult::LimitExceeded;

        const textures::FormatInfo formatInfo = textures::GetFormatInfo(format);
        u64 requiredPayloadBytes = 0;
        for (u32 layer = 0; layer < arrayLayers; ++layer)
        {
            const u32 faceCount = dimension == textures::TextureDimension::Cube ? 6u : 1u;
            for (u32 face = 0; face < faceCount; ++face)
            {
                for (u8 mip = 0; mip < mipCount; ++mip)
                {
                    const u32 mipWidth = textures::CalculateMipExtent(width, mip);
                    const u32 mipHeight = textures::CalculateMipExtent(height, mip);
                    const u32 mipDepth = dimension == textures::TextureDimension::Texture3D
                        ? textures::CalculateMipExtent(depth, mip) : 1u;
                    const u64 rowPitch = textures::CalculateMinimumRowPitch(format, mipWidth);
                    const u64 slicePitch = textures::CalculateMinimumSlicePitch(format, mipWidth, mipHeight);
                    const u64 byteSize = slicePitch * mipDepth;
                    if (!formatInfo.blockCompressed || rowPitch == 0 || slicePitch == 0 ||
                        requiredPayloadBytes > 0xffffffffffffffffull - byteSize)
                        return TextureImportResult::UnsupportedFormat;
                    requiredPayloadBytes += byteSize;
                }
            }
            if (dimension == textures::TextureDimension::Texture3D) break;
        }
        if (requiredPayloadBytes > request.limits.maximumDecodedBytes || payloadOffset > size ||
            requiredPayloadBytes != size - payloadOffset)
            return requiredPayloadBytes > request.limits.maximumDecodedBytes
                ? TextureImportResult::LimitExceeded : TextureImportResult::DecodeFailure;

        const textures::ColorSpace colorSpace = request.colorSpace == ImportedColorSpace::Linear
            ? textures::ColorSpace::Linear : request.colorSpace == ImportedColorSpace::SRgb
                ? textures::ColorSpace::SRgb : embeddedColorSpace;
        if (colorSpace == textures::ColorSpace::SRgb && !formatInfo.supportsSRgb)
            return TextureImportResult::InvalidArgument;
        TextureImportResult result = output.Initialize(dimension, format, colorSpace, width, height, depth, arrayLayers,
                                                       mipCount, subresourceCount, requiredPayloadBytes, Fingerprint(request),
                                                       importers::Dds, 1, request.limits);
        usize cursor = payloadOffset;
        u32 subresource = 0;
        for (u16 layer = 0; result == TextureImportResult::Success && layer < arrayLayers; ++layer)
        {
            const u8 faceCount = dimension == textures::TextureDimension::Cube ? 6u : 1u;
            for (u8 face = 0; result == TextureImportResult::Success && face < faceCount; ++face)
            {
                for (u8 mip = 0; result == TextureImportResult::Success && mip < mipCount; ++mip)
                {
                    const u32 mipWidth = textures::CalculateMipExtent(width, mip);
                    const u32 mipHeight = textures::CalculateMipExtent(height, mip);
                    const u32 mipDepth = dimension == textures::TextureDimension::Texture3D
                        ? textures::CalculateMipExtent(depth, mip) : 1u;
                    const u32 rowPitch = textures::CalculateMinimumRowPitch(format, mipWidth);
                    const u32 slicePitch = textures::CalculateMinimumSlicePitch(format, mipWidth, mipHeight);
                    const usize byteSize = static_cast<usize>(slicePitch) * mipDepth;
                    result = output.SetSubresource(subresource++, mip, layer, face, bytes + cursor, byteSize,
                                                   rowPitch, slicePitch);
                    cursor += byteSize;
                }
            }
            if (dimension == textures::TextureDimension::Texture3D) break;
        }
        if (result != TextureImportResult::Success || cursor != size || !output.IsValid())
        {
            output.Reset();
            return result == TextureImportResult::Success ? TextureImportResult::DecodeFailure : result;
        }
        if (report != nullptr)
            *report = {importers::Dds, 1, dimension, format, colorSpace, width, height, depth, arrayLayers,
                       mipCount, subresourceCount, requiredPayloadBytes};
        return TextureImportResult::Success;
    }

    Result CookGpuTexture(const GpuTextureSource& source, filesystem::IFile& output,
                          const GpuTextureCookSettings& settings, CookReport* const report) noexcept
    {
        if (!IsInitialized()) return Result::InvalidState;
        const textures::FormatInfo formatInfo = textures::GetFormatInfo(source.format);
        const u32 expectedCount = ExpectedSubresourceCount(source.dimension, source.arrayLayers, source.mipCount);
        if (source.dimension > textures::TextureDimension::Cube || source.format >= textures::PixelFormat::Count ||
            formatInfo.bytesPerBlock == 0 || source.width == 0 || source.height == 0 || source.depth == 0 ||
            source.arrayLayers == 0 || source.mipCount == 0 || source.subresources.Data() == nullptr ||
            source.subresources.Count() != expectedCount || settings.mipTailCount == 0)
            return Result::InvalidArgument;

        containers::DynamicArray<textures::SubresourceBuildRecord> records(memory::pools::Assets::GetInstance());
        records.Resize(expectedCount);
        if (records.Size() != expectedCount) return Result::OutOfMemory;
        u64 sourceBytes = 0;
        for (u32 index = 0; index < expectedCount; ++index)
        {
            const GpuSubresourceSource& subresource = source.subresources[index];
            if (subresource.data == nullptr || subresource.byteSize == 0 ||
                sourceBytes > settings.maximumOutputBytes || subresource.byteSize > settings.maximumOutputBytes - sourceBytes)
                return subresource.byteSize == 0 || subresource.data == nullptr ? Result::InvalidSourceLayout : Result::LimitExceeded;
            sourceBytes += subresource.byteSize;
            records[index] = {subresource.mipLevel, subresource.arrayLayer, subresource.face, subresource.data,
                              subresource.byteSize, subresource.rowPitch, subresource.slicePitch};
        }
        textures::BuildDescription description;
        description.dimension = source.dimension;
        description.format = source.format;
        description.colorSpace = source.colorSpace;
        description.flags = textures::TextureFlags::DirectGpuUpload;
        if (settings.streamable) description.flags = description.flags | textures::TextureFlags::Streamable;
        description.width = source.width;
        description.height = source.height;
        description.depth = source.depth;
        description.arrayLayers = source.arrayLayers;
        description.mipCount = source.mipCount;
        description.mipTailFirstLevel = settings.streamable && source.mipCount > settings.mipTailCount
            ? static_cast<u8>(source.mipCount - settings.mipTailCount) : 0;
        description.sourceFingerprint = source.sourceFingerprint;
        description.subresources = {records.TypedData(), records.Size()};
        const textures::Result writeResult = textures::WriteTexture(output, description);
        if (writeResult != textures::Result::Success)
            return writeResult == textures::Result::LimitExceeded ? Result::LimitExceeded : Result::TextureWriteFailure;
        if (static_cast<u64>(output.GetSize()) > settings.maximumOutputBytes) return Result::LimitExceeded;
        if (report != nullptr)
            *report = {0, 0, source.format, source.mipCount, expectedCount, sourceBytes,
                       static_cast<u64>(output.GetSize()), 0, 0, 0.0f, false};
        return Result::Success;
    }
} // namespace vanguard::texture_tools
