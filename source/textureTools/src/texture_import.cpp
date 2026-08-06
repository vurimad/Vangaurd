#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/platform.hpp>

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
    using vanguard::f32;

    constexpr u32 MaximumTextureImporters = 32;
    tools::TextureImporterDescriptor g_importers[MaximumTextureImporters]{};
    u32 g_importerCount = 0;
    bool g_importerRegistrySealed = false;

    [[nodiscard]] u32 BytesPerPixel(const tools::SourcePixelFormat format) noexcept
    {
        switch (format)
        {
        case tools::SourcePixelFormat::R8UNorm: return 1;
        case tools::SourcePixelFormat::R8G8UNorm: return 2;
        case tools::SourcePixelFormat::R8G8B8A8UNorm: return 4;
        case tools::SourcePixelFormat::R16G16B16A16UNorm:
        case tools::SourcePixelFormat::R16G16B16A16Float: return 8;
        case tools::SourcePixelFormat::R32G32B32A32Float: return 16;
        }
        return 0;
    }

    [[nodiscard]] tools::TextureCookingProfileId DefaultProfile(const tools::TextureUsage usage,
                                                                 const tools::SourcePixelFormat format) noexcept
    {
        switch (usage)
        {
        case tools::TextureUsage::Color: return tools::profiles::Color;
        case tools::TextureUsage::ColorAlpha: return tools::profiles::ColorAlpha;
        case tools::TextureUsage::Normal: return tools::profiles::Normal;
        case tools::TextureUsage::Masks: return tools::profiles::Masks;
        case tools::TextureUsage::Data: return tools::profiles::Data;
        case tools::TextureUsage::Ui: return tools::profiles::Ui;
        case tools::TextureUsage::Hdr: return tools::profiles::Hdr;
        case tools::TextureUsage::Automatic:
            return format == tools::SourcePixelFormat::R16G16B16A16Float ||
                   format == tools::SourcePixelFormat::R32G32B32A32Float ? tools::profiles::Hdr : tools::profiles::ColorAlpha;
        }
        return tools::profiles::ColorAlpha;
    }
}

namespace vanguard::texture_tools
{
    [[nodiscard]] const TextureImporterDescriptor& PngImporter() noexcept;
    [[nodiscard]] const TextureImporterDescriptor& JpegImporter() noexcept;
    [[nodiscard]] const TextureImporterDescriptor& TiffImporter() noexcept;
    [[nodiscard]] const TextureImporterDescriptor& OpenExrImporter() noexcept;

    const char* ToString(const TextureImportResult result) noexcept
    {
        switch (result)
        {
        case TextureImportResult::Success: return "Success";
        case TextureImportResult::InvalidArgument: return "InvalidArgument";
        case TextureImportResult::InvalidState: return "InvalidState";
        case TextureImportResult::UnsupportedFormat: return "UnsupportedFormat";
        case TextureImportResult::DecodeFailure: return "DecodeFailure";
        case TextureImportResult::MultipleImagesUnsupported: return "MultipleImagesUnsupported";
        case TextureImportResult::LimitExceeded: return "LimitExceeded";
        case TextureImportResult::OutOfMemory: return "OutOfMemory";
        }
        return "Unknown";
    }

    ImportedTexture::ImportedTexture() noexcept
        : m_pixels(memory::pools::Assets::GetInstance()), m_views(memory::pools::Assets::GetInstance()) {}

    ImportedTexture::ImportedTexture(ImportedTexture&& other) noexcept
        : m_format(other.m_format), m_dimension(other.m_dimension), m_colorSpace(other.m_colorSpace),
          m_width(other.m_width), m_height(other.m_height), m_rowPitch(other.m_rowPitch),
          m_imageByteSize(other.m_imageByteSize), m_imageCount(other.m_imageCount), m_sourceFingerprint(other.m_sourceFingerprint),
          m_recommendedProfile(other.m_recommendedProfile), m_importer(other.m_importer),
          m_importerVersion(other.m_importerVersion), m_pixels(static_cast<containers::DynamicArray<u8>&&>(other.m_pixels)),
          m_views(memory::pools::Assets::GetInstance())
    {
        other.Reset();
    }

    ImportedTexture& ImportedTexture::operator=(ImportedTexture&& other) noexcept
    {
        if (this != &other)
        {
            m_format = other.m_format;
            m_dimension = other.m_dimension;
            m_colorSpace = other.m_colorSpace;
            m_width = other.m_width;
            m_height = other.m_height;
            m_rowPitch = other.m_rowPitch;
            m_imageByteSize = other.m_imageByteSize;
            m_imageCount = other.m_imageCount;
            m_sourceFingerprint = other.m_sourceFingerprint;
            m_recommendedProfile = other.m_recommendedProfile;
            m_importer = other.m_importer;
            m_importerVersion = other.m_importerVersion;
            m_pixels = static_cast<containers::DynamicArray<u8>&&>(other.m_pixels);
            other.Reset();
        }
        return *this;
    }

    void ImportedTexture::Reset() noexcept
    {
        m_format = SourcePixelFormat::R8G8B8A8UNorm;
        m_dimension = textures::TextureDimension::Texture2D;
        m_colorSpace = textures::ColorSpace::Linear;
        m_width = 0;
        m_height = 0;
        m_rowPitch = 0;
        m_imageByteSize = 0;
        m_imageCount = 0;
        m_sourceFingerprint = {};
        m_recommendedProfile = 0;
        m_importer = InvalidTextureImporterId;
        m_importerVersion = 0;
        m_pixels.Clear();
        m_views.Clear();
    }

    bool ImportedTexture::IsValid() const noexcept
    {
        const u32 expectedImages = m_dimension == textures::TextureDimension::Cube ? 6u : 1u;
        return m_width != 0 && m_height != 0 && m_rowPitch != 0 && m_imageCount == expectedImages &&
               m_importer != InvalidTextureImporterId &&
               m_importerVersion != 0 && m_recommendedProfile != 0 &&
               m_imageByteSize == static_cast<u64>(m_rowPitch) * m_height &&
               m_pixels.Size() == static_cast<u64>(m_imageByteSize) * m_imageCount;
    }

    SourceTexture ImportedTexture::Source() const noexcept
    {
        if (!IsValid()) return {};
        m_views.Resize(m_imageCount);
        if (m_views.Size() != m_imageCount) return {};
        for (u32 image = 0; image < m_imageCount; ++image)
            m_views[image] = {m_pixels.TypedData() + static_cast<usize>(image) * m_imageByteSize,
                              m_imageByteSize, m_rowPitch, m_imageByteSize};
        return {m_dimension, m_format, m_colorSpace, m_width, m_height, 1, 1,
                m_sourceFingerprint, {m_views.TypedData(), m_views.Size()}};
    }

    TextureCookingProfileId ImportedTexture::RecommendedProfile() const noexcept { return m_recommendedProfile; }
    TextureImporterId ImportedTexture::Importer() const noexcept { return m_importer; }
    u32 ImportedTexture::ImporterVersion() const noexcept { return m_importerVersion; }

    TextureImportResult ImportedTexture::Initialize2D(const SourcePixelFormat format, const textures::ColorSpace colorSpace,
                                                      const u32 width, const u32 height, const u32 rowPitch,
                                                      const crypto::Digest256& sourceFingerprint,
                                                      const TextureCookingProfileId recommendedProfile,
                                                      const TextureImporterId importer, const u32 importerVersion,
                                                      const TextureImportLimits& limits) noexcept
    {
        Reset();
        const u32 bytesPerPixel = BytesPerPixel(format);
        const u64 byteCount = static_cast<u64>(rowPitch) * height;
        if (bytesPerPixel == 0 || colorSpace > textures::ColorSpace::SRgb || width == 0 || height == 0 ||
            width > limits.maximumDimension || height > limits.maximumDimension ||
            rowPitch < static_cast<u64>(width) * bytesPerPixel || byteCount > limits.maximumDecodedBytes ||
            byteCount > 0xffffffffull || recommendedProfile == 0 || importer == InvalidTextureImporterId || importerVersion == 0)
        {
            return byteCount > limits.maximumDecodedBytes || width > limits.maximumDimension ||
                   height > limits.maximumDimension || byteCount > 0xffffffffull
                       ? TextureImportResult::LimitExceeded
                       : TextureImportResult::InvalidArgument;
        }
        m_pixels.Resize(static_cast<u32>(byteCount));
        if (m_pixels.Size() != byteCount) return TextureImportResult::OutOfMemory;
        m_format = format;
        m_dimension = textures::TextureDimension::Texture2D;
        m_colorSpace = colorSpace;
        m_width = width;
        m_height = height;
        m_rowPitch = rowPitch;
        m_imageByteSize = static_cast<u32>(byteCount);
        m_imageCount = 1;
        m_sourceFingerprint = sourceFingerprint;
        m_recommendedProfile = recommendedProfile;
        m_importer = importer;
        m_importerVersion = importerVersion;
        return TextureImportResult::Success;
    }

    TextureImportResult ImportedTexture::InitializeCube(const SourcePixelFormat format, const textures::ColorSpace colorSpace,
                                                        const u32 extent, const u32 rowPitch,
                                                        const crypto::Digest256& sourceFingerprint,
                                                        const TextureCookingProfileId recommendedProfile,
                                                        const TextureImporterId importer, const u32 importerVersion,
                                                        const TextureImportLimits& limits) noexcept
    {
        const TextureImportResult result = Initialize2D(format, colorSpace, extent, extent, rowPitch, sourceFingerprint,
                                                        recommendedProfile, importer, importerVersion, limits);
        if (result != TextureImportResult::Success) return result;
        const u64 totalBytes = static_cast<u64>(m_imageByteSize) * 6u;
        if (limits.maximumImages < 6 || totalBytes > limits.maximumDecodedBytes || totalBytes > 0xffffffffull)
        {
            Reset();
            return TextureImportResult::LimitExceeded;
        }
        m_pixels.Resize(static_cast<u32>(totalBytes));
        if (m_pixels.Size() != totalBytes)
        {
            Reset();
            return TextureImportResult::OutOfMemory;
        }
        m_dimension = textures::TextureDimension::Cube;
        m_imageCount = 6;
        return TextureImportResult::Success;
    }

    u8* ImportedTexture::MutableImageData() noexcept { return MutableImageData(0); }
    u8* ImportedTexture::MutableImageData(const u32 imageIndex) noexcept
    {
        return imageIndex < m_imageCount ? m_pixels.TypedData() + static_cast<usize>(imageIndex) * m_imageByteSize : nullptr;
    }
    const u8* ImportedTexture::ImageData(const u32 imageIndex) const noexcept
    {
        return imageIndex < m_imageCount ? m_pixels.TypedData() + static_cast<usize>(imageIndex) * m_imageByteSize : nullptr;
    }
    u32 ImportedTexture::ImageCount() const noexcept { return m_imageCount; }
    usize ImportedTexture::ImageByteSize() const noexcept { return m_imageByteSize; }

    TextureImportResult ImportedTexture::ApplyChannelMapping(const TextureChannelMapping& mapping) noexcept
    {
        if (!IsValid() || !mapping.IsValid()) return TextureImportResult::InvalidArgument;
        const ImportedChannel channels[4] = {mapping.red, mapping.green, mapping.blue, mapping.alpha};
        const u32 texelCount = m_width * m_height * m_imageCount;
        if (m_format == SourcePixelFormat::R8G8B8A8UNorm)
        {
            for (u32 texel = 0; texel < texelCount; ++texel)
            {
                u8* const pixel = m_pixels.TypedData() + texel * 4u;
                const u8 source[4] = {pixel[0], pixel[1], pixel[2], pixel[3]};
                for (u32 channel = 0; channel < 4; ++channel)
                    pixel[channel] = channels[channel] <= ImportedChannel::Alpha
                        ? source[static_cast<u32>(channels[channel])]
                        : (channels[channel] == ImportedChannel::One ? 255u : 0u);
            }
            return TextureImportResult::Success;
        }
        if (m_format == SourcePixelFormat::R16G16B16A16UNorm ||
            m_format == SourcePixelFormat::R16G16B16A16Float)
        {
            const u16 one = m_format == SourcePixelFormat::R16G16B16A16Float ? 0x3c00u : 0xffffu;
            auto* const pixels = reinterpret_cast<u16*>(m_pixels.TypedData());
            for (u32 texel = 0; texel < texelCount; ++texel)
            {
                u16* const pixel = pixels + texel * 4u;
                const u16 source[4] = {pixel[0], pixel[1], pixel[2], pixel[3]};
                for (u32 channel = 0; channel < 4; ++channel)
                    pixel[channel] = channels[channel] <= ImportedChannel::Alpha
                        ? source[static_cast<u32>(channels[channel])]
                        : (channels[channel] == ImportedChannel::One ? one : 0u);
            }
            return TextureImportResult::Success;
        }
        if (m_format == SourcePixelFormat::R32G32B32A32Float)
        {
            auto* const pixels = reinterpret_cast<f32*>(m_pixels.TypedData());
            for (u32 texel = 0; texel < texelCount; ++texel)
            {
                f32* const pixel = pixels + texel * 4u;
                const f32 source[4] = {pixel[0], pixel[1], pixel[2], pixel[3]};
                for (u32 channel = 0; channel < 4; ++channel)
                    pixel[channel] = channels[channel] <= ImportedChannel::Alpha
                        ? source[static_cast<u32>(channels[channel])]
                        : (channels[channel] == ImportedChannel::One ? 1.0f : 0.0f);
            }
            return TextureImportResult::Success;
        }
        return mapping.red == ImportedChannel::Red && mapping.green == ImportedChannel::Green &&
               mapping.blue == ImportedChannel::Blue && mapping.alpha == ImportedChannel::Alpha
                   ? TextureImportResult::Success : TextureImportResult::InvalidArgument;
    }

    TextureImporterRegistrationResult RegisterTextureImporter(const TextureImporterDescriptor& importer) noexcept
    {
        if (g_importerRegistrySealed) return TextureImporterRegistrationResult::RegistrySealed;
        if (!importer.IsValid()) return TextureImporterRegistrationResult::InvalidArgument;
        for (u32 index = 0; index < g_importerCount; ++index)
            if (g_importers[index].id == importer.id) return TextureImporterRegistrationResult::DuplicateIdentifier;
        if (g_importerCount == MaximumTextureImporters) return TextureImporterRegistrationResult::CapacityExceeded;
        g_importers[g_importerCount++] = importer;
        return TextureImporterRegistrationResult::Success;
    }

    bool RegisterBuiltInTextureImporters() noexcept
    {
        return RegisterTextureImporter(PngImporter()) == TextureImporterRegistrationResult::Success &&
               RegisterTextureImporter(JpegImporter()) == TextureImporterRegistrationResult::Success &&
               RegisterTextureImporter(TiffImporter()) == TextureImporterRegistrationResult::Success &&
               RegisterTextureImporter(OpenExrImporter()) == TextureImporterRegistrationResult::Success;
    }

    TextureImportResult ImportTexture(const TextureImportRequest& request, ImportedTexture& output,
                                      TextureImportReport* const report) noexcept
    {
        output.Reset();
        if (!IsInitialized()) return TextureImportResult::InvalidState;
        if (request.encoded.Empty() || request.encoded.Data() == nullptr || request.limits.maximumDimension == 0 ||
            request.limits.maximumImages == 0 || request.limits.maximumDecodedBytes == 0 ||
            request.usage > TextureUsage::Hdr || request.colorSpace > ImportedColorSpace::SRgb || !request.channels.IsValid())
            return TextureImportResult::InvalidArgument;

        g_importerRegistrySealed = true;
        const TextureImporterDescriptor* selected = nullptr;
        TextureProbeResult selectedProbe = TextureProbeResult::NoMatch;
        for (u32 index = 0; index < g_importerCount; ++index)
        {
            const TextureProbeResult probe = g_importers[index].probe(request, g_importers[index].userData);
            if (probe > selectedProbe)
            {
                selectedProbe = probe;
                selected = &g_importers[index];
            }
        }
        if (selected == nullptr) return TextureImportResult::UnsupportedFormat;
        TextureImportResult result = selected->decode(request, output, selected->userData);
        if (result != TextureImportResult::Success)
        {
            output.Reset();
            return result;
        }
        const crypto::Digest256 expectedFingerprint = request.sourceFingerprint.IsEmpty()
            ? crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes()) : request.sourceFingerprint;
        if (!output.IsValid() || output.Importer() != selected->id || output.ImporterVersion() != selected->version ||
            output.Source().sourceFingerprint != expectedFingerprint)
        {
            output.Reset();
            return TextureImportResult::DecodeFailure;
        }
        result = output.ApplyChannelMapping(request.channels);
        if (result != TextureImportResult::Success)
        {
            output.Reset();
            return result;
        }
        const SourceTexture source = output.Source();
        if (report != nullptr)
        {
            *report = {output.Importer(), output.ImporterVersion(), source.format, source.colorSpace,
                       output.RecommendedProfile(), source.width, source.height, output.ImageByteSize()};
        }
        return TextureImportResult::Success;
    }

    TextureImportResult AssembleCubeFaces(const containers::ArraySpan<const ImportedTexture> faces,
                                           const TextureUsage usage, ImportedTexture& output,
                                           const TextureImportLimits& limits) noexcept
    {
        if (faces.Count() != 6 || faces.Data() == nullptr || usage > TextureUsage::Hdr)
            return TextureImportResult::InvalidArgument;
        const SourceTexture first = faces[0].Source();
        if (!faces[0].IsValid() || first.dimension != textures::TextureDimension::Texture2D ||
            first.width != first.height || first.images.Count() != 1)
            return TextureImportResult::InvalidArgument;

        constexpr char Domain[] = "vanguard.texture-cube-faces.v1";
        crypto::Sha256Builder fingerprintBuilder;
        static_cast<void>(fingerprintBuilder.Update(Domain, sizeof(Domain) - 1u));
        for (u32 face = 0; face < 6; ++face)
        {
            if (&faces[face] == &output) return TextureImportResult::InvalidArgument;
            const SourceTexture source = faces[face].Source();
            if (!faces[face].IsValid() || source.dimension != textures::TextureDimension::Texture2D ||
                source.format != first.format || source.colorSpace != first.colorSpace ||
                source.width != first.width || source.height != first.height || source.images.Count() != 1 ||
                source.images[0].rowPitch != first.images[0].rowPitch ||
                source.images[0].byteSize != first.images[0].byteSize)
                return TextureImportResult::InvalidArgument;
            static_cast<void>(fingerprintBuilder.Update(source.sourceFingerprint.bytes, crypto::Digest256::ByteCount));
        }
        crypto::Digest256 fingerprint;
        if (!fingerprintBuilder.Finalize(fingerprint)) return TextureImportResult::InvalidState;
        const TextureCookingProfileId profile = usage == TextureUsage::Automatic
            ? faces[0].RecommendedProfile() : DefaultProfile(usage, first.format);
        TextureImportResult result = output.InitializeCube(first.format, first.colorSpace, first.width,
                                                           first.images[0].rowPitch, fingerprint, profile,
                                                           importers::CubeAssembler, 1, limits);
        if (result != TextureImportResult::Success) return result;
        for (u32 face = 0; face < 6; ++face)
        {
            const u8* const source = faces[face].ImageData();
            u8* const destination = output.MutableImageData(face);
            for (usize byte = 0; byte < output.ImageByteSize(); ++byte) destination[byte] = source[byte];
        }
        return TextureImportResult::Success;
    }

    TextureImportResult ExtractCubeCross(const ImportedTexture& cross, const CubeCrossLayout layout,
                                         const TextureUsage usage, ImportedTexture& output,
                                         const TextureImportLimits& limits) noexcept
    {
        if (&cross == &output || !cross.IsValid() || layout > CubeCrossLayout::Vertical || usage > TextureUsage::Hdr)
            return TextureImportResult::InvalidArgument;
        const SourceTexture source = cross.Source();
        if (source.dimension != textures::TextureDimension::Texture2D || source.images.Count() != 1)
            return TextureImportResult::InvalidArgument;
        const u32 columns = layout == CubeCrossLayout::Horizontal ? 4u : 3u;
        const u32 rows = layout == CubeCrossLayout::Horizontal ? 3u : 4u;
        if (source.width % columns != 0 || source.height % rows != 0 ||
            source.width / columns != source.height / rows)
            return TextureImportResult::InvalidArgument;
        const u32 extent = source.width / columns;
        const u32 bytesPerPixel = BytesPerPixel(source.format);
        if (bytesPerPixel == 0 || static_cast<u64>(extent) * bytesPerPixel > 0xffffffffull)
            return TextureImportResult::LimitExceeded;

        constexpr char Domain[] = "vanguard.texture-cube-cross.v1";
        crypto::Sha256Builder fingerprintBuilder;
        static_cast<void>(fingerprintBuilder.Update(Domain, sizeof(Domain) - 1u));
        static_cast<void>(fingerprintBuilder.Update(source.sourceFingerprint.bytes, crypto::Digest256::ByteCount));
        const u8 layoutValue = static_cast<u8>(layout);
        static_cast<void>(fingerprintBuilder.Update(&layoutValue, sizeof(layoutValue)));
        crypto::Digest256 fingerprint;
        if (!fingerprintBuilder.Finalize(fingerprint)) return TextureImportResult::InvalidState;
        const TextureCookingProfileId profile = usage == TextureUsage::Automatic
            ? cross.RecommendedProfile() : DefaultProfile(usage, source.format);
        TextureImportResult result = output.InitializeCube(source.format, source.colorSpace, extent,
                                                           extent * bytesPerPixel, fingerprint, profile,
                                                           importers::CubeAssembler, 1, limits);
        if (result != TextureImportResult::Success) return result;

        const u8 cellX[6] = {2, 0, 1, 1, 1, static_cast<u8>(layout == CubeCrossLayout::Horizontal ? 3 : 1)};
        const u8 cellY[6] = {1, 1, 0, 2, 1, static_cast<u8>(layout == CubeCrossLayout::Horizontal ? 1 : 3)};
        const u8* const sourcePixels = cross.ImageData();
        const u32 copiedRowBytes = extent * bytesPerPixel;
        for (u32 face = 0; face < 6; ++face)
        {
            u8* const destination = output.MutableImageData(face);
            for (u32 y = 0; y < extent; ++y)
            {
                const u8* const sourceRow = sourcePixels +
                    static_cast<usize>(cellY[face] * extent + y) * source.images[0].rowPitch +
                    static_cast<usize>(cellX[face] * extent) * bytesPerPixel;
                u8* const destinationRow = destination + static_cast<usize>(y) * copiedRowBytes;
                for (u32 byte = 0; byte < copiedRowBytes; ++byte) destinationRow[byte] = sourceRow[byte];
            }
        }
        return TextureImportResult::Success;
    }

    TextureCookingProfileId ResolveImportedProfile(const TextureUsage usage, const SourcePixelFormat format) noexcept
    {
        return DefaultProfile(usage, format);
    }
} // namespace vanguard::texture_tools
