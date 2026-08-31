#pragma once

#include <vanguard/texture_tools/texture_tools.hpp>

namespace vanguard::texture_tools
{
    using TextureImporterId = u64;
    inline constexpr TextureImporterId InvalidTextureImporterId = 0;

    namespace importers
    {
        inline constexpr TextureImporterId Png = 0x706e670000000001ull;
        inline constexpr TextureImporterId Jpeg = 0x6a70656700000001ull;
        inline constexpr TextureImporterId Tiff = 0x7469666600000001ull;
        inline constexpr TextureImporterId OpenExr = 0x6578720000000001ull;
        inline constexpr TextureImporterId Dds = 0x6464730000000001ull;
        inline constexpr TextureImporterId CubeAssembler = 0x6375626500000001ull;
    } // namespace importers

    enum class TextureUsage : u8
    {
        Automatic,
        Color,
        ColorAlpha,
        Normal,
        Masks,
        Data,
        Ui,
        Hdr
    };

    enum class ImportedColorSpace : u8
    {
        Automatic,
        Linear,
        SRgb
    };

    enum class ImportedChannel : u8
    {
        Red,
        Green,
        Blue,
        Alpha,
        Zero,
        One
    };

    struct TextureChannelMapping
    {
        ImportedChannel red = ImportedChannel::Red;
        ImportedChannel green = ImportedChannel::Green;
        ImportedChannel blue = ImportedChannel::Blue;
        ImportedChannel alpha = ImportedChannel::Alpha;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return red <= ImportedChannel::One && green <= ImportedChannel::One && blue <= ImportedChannel::One && alpha <= ImportedChannel::One;
        }
    };

    enum class TextureImportResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        UnsupportedFormat,
        DecodeFailure,
        MultipleImagesUnsupported,
        LimitExceeded,
        OutOfMemory,
        Cancelled
    };

    [[nodiscard]] const char* ToString(TextureImportResult result) noexcept;

    struct TextureImportLimits
    {
        u32 maximumDimension = 131072;
        u32 maximumImages = 4096;
        u64 maximumDecodedBytes = 16ull * 1024ull * 1024ull * 1024ull;
    };

    /// Encoded source data is caller-owned and may represent any registered image format. typeHint is an optional
    /// extension without a leading dot; it improves diagnostics and probing but never overrides file signature checks.
    struct TextureImportRequest
    {
        containers::ArraySpan<const u8> encoded;
        containers::StringView typeHint;
        TextureUsage usage = TextureUsage::Automatic;
        ImportedColorSpace colorSpace = ImportedColorSpace::Automatic;
        TextureChannelMapping channels;
        crypto::Digest256 sourceFingerprint;
        TextureImportLimits limits;
        system::CancellationView cancellation;
    };

    enum class TextureSourceKind : u8
    {
        Png,
        Jpeg,
        Tiff,
        OpenExr,
        Dds
    };

    struct TextureSourceInspection
    {
        TextureSourceKind kind = TextureSourceKind::Png;
        u32 width = 0;
        u32 height = 0;
        u32 depth = 1;
        u16 arrayLayers = 1;
        u8 faceCount = 1;
        u8 mipCount = 1;
        u64 decodedBytes = 0;
        u64 payloadBytes = 0;
    };

    /// Reads only container/header metadata. It never allocates decoded pixels or performs compression.
    [[nodiscard]] TextureImportResult InspectTextureSource(containers::ArraySpan<const u8> encoded, TextureSourceInspection& inspection) noexcept;

    class ImportedTexture final
    {
    public:
        ImportedTexture() noexcept;
        ImportedTexture(ImportedTexture&& other) noexcept;
        ImportedTexture& operator=(ImportedTexture&& other) noexcept;
        ~ImportedTexture() = default;

        ImportedTexture(const ImportedTexture&) = delete;
        ImportedTexture& operator=(const ImportedTexture&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] SourceTexture GetSource() const noexcept;
        [[nodiscard]] TextureCookingProfileId GetRecommendedProfile() const noexcept;
        [[nodiscard]] TextureImporterId Importer() const noexcept;
        [[nodiscard]] u32 ImporterVersion() const noexcept;

        /// Decoder callback boundary. Allocates one tightly packed 2D image and returns its writable storage.
        [[nodiscard]] TextureImportResult Initialize2D(SourcePixelFormat format, textures::ColorSpace colorSpace, u32 width, u32 height, u32 rowPitch,
                                                       const crypto::Digest256& sourceFingerprint, TextureCookingProfileId recommendedProfile,
                                                       TextureImporterId importer, u32 importerVersion, const TextureImportLimits& limits) noexcept;
        [[nodiscard]] u8* GetMutableImageData() noexcept;
        [[nodiscard]] u8* GetMutableImageData(u32 imageIndex) noexcept;
        [[nodiscard]] const u8* GetImageData(u32 imageIndex = 0) const noexcept;
        [[nodiscard]] u32 GetImageCount() const noexcept;
        [[nodiscard]] usize GetImageByteSize() const noexcept;
        [[nodiscard]] TextureImportResult ApplyChannelMapping(const TextureChannelMapping& mapping) noexcept;
        [[nodiscard]] TextureImportResult InitializeCube(SourcePixelFormat format, textures::ColorSpace colorSpace, u32 extent, u32 rowPitch,
                                                         const crypto::Digest256& sourceFingerprint, TextureCookingProfileId recommendedProfile,
                                                         TextureImporterId importer, u32 importerVersion, const TextureImportLimits& limits) noexcept;

    private:
        SourcePixelFormat m_format = SourcePixelFormat::R8G8B8A8UNorm;
        textures::TextureDimension m_dimension = textures::TextureDimension::Texture2D;
        textures::ColorSpace m_colorSpace = textures::ColorSpace::Linear;
        u32 m_width = 0;
        u32 m_height = 0;
        u32 m_rowPitch = 0;
        u32 m_imageByteSize = 0;
        u32 m_imageCount = 0;
        crypto::Digest256 m_sourceFingerprint;
        TextureCookingProfileId m_recommendedProfile = 0;
        TextureImporterId m_importer = InvalidTextureImporterId;
        u32 m_importerVersion = 0;
        containers::DynamicArray<u8> m_pixels;
        mutable containers::DynamicArray<SourceImage> m_views;
    };

    enum class TextureProbeResult : u8
    {
        NoMatch,
        Possible,
        Exact
    };

    using ProbeTextureFunction = TextureProbeResult (*)(const TextureImportRequest& request, void* userData) noexcept;
    using DecodeTextureFunction = TextureImportResult (*)(const TextureImportRequest& request, ImportedTexture& output, void* userData) noexcept;

    struct TextureImporterDescriptor
    {
        TextureImporterId id = InvalidTextureImporterId;
        const char* name = nullptr;
        u32 version = 0;
        ProbeTextureFunction probe = nullptr;
        DecodeTextureFunction decode = nullptr;
        void* userData = nullptr;
        crypto::Digest256 configurationFingerprint;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return id != InvalidTextureImporterId && name != nullptr && name[0] != '\0' && version != 0 && probe != nullptr && decode != nullptr;
        }
    };

    enum class TextureImporterRegistrationResult : u8
    {
        Success,
        InvalidArgument,
        DuplicateIdentifier,
        CapacityExceeded,
        RegistrySealed
    };

    struct TextureImportReport
    {
        TextureImporterId importer = InvalidTextureImporterId;
        u32 importerVersion = 0;
        SourcePixelFormat decodedFormat = SourcePixelFormat::R8G8B8A8UNorm;
        textures::ColorSpace colorSpace = textures::ColorSpace::Linear;
        TextureCookingProfileId recommendedProfile = 0;
        u32 width = 0;
        u32 height = 0;
        u64 decodedBytes = 0;
    };

    [[nodiscard]] TextureImporterRegistrationResult RegisterTextureImporter(const TextureImporterDescriptor& importer) noexcept;
    [[nodiscard]] TextureImportResult ImportTexture(const TextureImportRequest& request, ImportedTexture& output,
                                                    TextureImportReport* report = nullptr) noexcept;
    [[nodiscard]] TextureImportResult AssembleCubeFaces(containers::ArraySpan<const ImportedTexture> faces, TextureUsage usage, ImportedTexture& output,
                                                        const TextureImportLimits& limits = {}, system::CancellationView cancellation = {}) noexcept;

    enum class CubeCrossLayout : u8
    {
        /// Four columns by three rows: +Y above, -X/+Z/+X/-Z across the center, -Y below.
        Horizontal,
        /// Three columns by four rows: +Y above, -X/+Z/+X across the second row, -Y then -Z below.
        Vertical
    };

    /// Extracts a canonical Direct3D/NVRHI cube without rotating individual cells. Every occupied cross cell must
    /// already have the same texel orientation as its corresponding +X, -X, +Y, -Y, +Z, or -Z destination face.
    [[nodiscard]] TextureImportResult ExtractCubeCross(const ImportedTexture& cross, CubeCrossLayout layout, TextureUsage usage, ImportedTexture& output,
                                                       const TextureImportLimits& limits = {}, system::CancellationView cancellation = {}) noexcept;

    /// A byte-exact GPU subresource imported from a container such as DDS. Unlike SourceTexture, this payload is already
    /// in its final GPU storage format and must never pass through pixel filtering or lossy recompression.
    struct GpuSubresourceSource
    {
        u8 mipLevel = 0;
        u16 arrayLayer = 0;
        u8 face = 0;
        const void* data = nullptr;
        usize byteSize = 0;
        u32 rowPitch = 0;
        u32 slicePitch = 0;
    };

    struct GpuTextureSource
    {
        textures::TextureDimension dimension = textures::TextureDimension::Texture2D;
        textures::PixelFormat format = textures::PixelFormat::BC1UNorm;
        textures::ColorSpace colorSpace = textures::ColorSpace::Linear;
        u32 width = 1;
        u32 height = 1;
        u32 depth = 1;
        u16 arrayLayers = 1;
        u8 mipCount = 1;
        crypto::Digest256 sourceFingerprint;
        containers::ArraySpan<const GpuSubresourceSource> subresources;
    };

    class ImportedGpuTexture final
    {
    public:
        ImportedGpuTexture() noexcept;
        ImportedGpuTexture(ImportedGpuTexture&& other) noexcept;
        ImportedGpuTexture& operator=(ImportedGpuTexture&& other) noexcept;
        ImportedGpuTexture(const ImportedGpuTexture&) = delete;
        ImportedGpuTexture& operator=(const ImportedGpuTexture&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] GpuTextureSource GetSource() const noexcept;
        [[nodiscard]] TextureImporterId Importer() const noexcept;
        [[nodiscard]] u32 ImporterVersion() const noexcept;

        /// Container-decoder boundary. Storage is allocated once; SetSubresource then copies every validated payload.
        [[nodiscard]] TextureImportResult Initialize(textures::TextureDimension dimension, textures::PixelFormat format, textures::ColorSpace colorSpace,
                                                     u32 width, u32 height, u32 depth, u16 arrayLayers, u8 mipCount, u32 subresourceCount, u64 totalBytes,
                                                     const crypto::Digest256& sourceFingerprint, TextureImporterId importer, u32 importerVersion,
                                                     const TextureImportLimits& limits) noexcept;
        [[nodiscard]] TextureImportResult SetSubresource(u32 index, u8 mipLevel, u16 arrayLayer, u8 face, const void* data, usize byteSize, u32 rowPitch,
                                                         u32 slicePitch) noexcept;

    private:
        struct OwnedSubresource
        {
            u8 mipLevel = 0;
            u16 arrayLayer = 0;
            u8 face = 0;
            u32 byteOffset = 0;
            u32 byteSize = 0;
            u32 rowPitch = 0;
            u32 slicePitch = 0;
            bool initialized = false;
        };

        textures::TextureDimension m_dimension = textures::TextureDimension::Texture2D;
        textures::PixelFormat m_format = textures::PixelFormat::BC1UNorm;
        textures::ColorSpace m_colorSpace = textures::ColorSpace::Linear;
        u32 m_width = 0;
        u32 m_height = 0;
        u32 m_depth = 0;
        u16 m_arrayLayers = 0;
        u8 m_mipCount = 0;
        u32 m_writeOffset = 0;
        crypto::Digest256 m_sourceFingerprint;
        TextureImporterId m_importer = InvalidTextureImporterId;
        u32 m_importerVersion = 0;
        containers::DynamicArray<u8> m_payload;
        containers::DynamicArray<OwnedSubresource> m_ownedSubresources;
        mutable containers::DynamicArray<GpuSubresourceSource> m_views;
    };

    struct GpuTextureImportReport
    {
        TextureImporterId importer = InvalidTextureImporterId;
        u32 importerVersion = 0;
        textures::TextureDimension dimension = textures::TextureDimension::Texture2D;
        textures::PixelFormat format = textures::PixelFormat::BC1UNorm;
        textures::ColorSpace colorSpace = textures::ColorSpace::Linear;
        u32 width = 0;
        u32 height = 0;
        u32 depth = 0;
        u16 arrayLayers = 0;
        u8 mipCount = 0;
        u32 subresourceCount = 0;
        u64 payloadBytes = 0;
    };

    [[nodiscard]] TextureImportResult ImportDdsTexture(const TextureImportRequest& request, ImportedGpuTexture& output,
                                                       GpuTextureImportReport* report = nullptr) noexcept;

    struct GpuTextureCookSettings
    {
        bool streamable = true;
        u8 mipTailCount = 4;
        u64 maximumOutputBytes = 16ull * 1024ull * 1024ull * 1024ull;
        system::CancellationView cancellation;
    };

    [[nodiscard]] Result CookGpuTexture(const GpuTextureSource& source, filesystem::IFile& output, const GpuTextureCookSettings& settings = {},
                                        CookReport* report = nullptr) noexcept;
} // namespace vanguard::texture_tools
