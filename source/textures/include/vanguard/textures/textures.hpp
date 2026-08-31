#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::textures
{
    inline constexpr u32 TextureMagic = serialization::MakeFourCC('V', 'T', 'E', 'X');
    inline constexpr resources::ResourceTypeId TextureResourceType = serialization::MakeFourCC('V', 'T', 'E', 'X');
    inline constexpr u32 InvalidSubresourceIndex = 0xffffffffu;

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        InvalidDimension,
        InvalidFormat,
        InvalidMipChain,
        InvalidSubresource,
        DuplicateSubresource,
        MissingSubresource,
        BufferTooSmall,
        Cancelled,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class TextureDimension : u8
    {
        Texture1D,
        Texture2D,
        Texture3D,
        Cube
    };

    enum class ColorSpace : u8
    {
        Linear,
        SRgb
    };

    /// GPU storage format. ColorSpace selects the linear or sRGB sampling interpretation where the format supports it.
    enum class PixelFormat : u8
    {
        R8UNorm,
        R8SNorm,
        R8UInt,
        R8G8UNorm,
        R8G8SNorm,
        R8G8UInt,
        R8G8B8A8UNorm,
        R8G8B8A8SNorm,
        R8G8B8A8UInt,
        B8G8R8A8UNorm,
        R16UNorm,
        R16SNorm,
        R16Float,
        R16G16UNorm,
        R16G16SNorm,
        R16G16Float,
        R16G16B16A16UNorm,
        R16G16B16A16SNorm,
        R16G16B16A16Float,
        R32Float,
        R32G32Float,
        R32G32B32A32Float,
        R10G10B10A2UNorm,
        R11G11B10Float,
        R9G9B9E5SharedExponent,
        BC1UNorm,
        BC2UNorm,
        BC3UNorm,
        BC4UNorm,
        BC4SNorm,
        BC5UNorm,
        BC5SNorm,
        BC6HUFloat,
        BC6HSFloat,
        BC7UNorm,
        Count
    };

    struct FormatInfo
    {
        u8 blockWidth = 1;
        u8 blockHeight = 1;
        u8 bytesPerBlock = 0;
        bool blockCompressed = false;
        bool supportsSRgb = false;
    };

    [[nodiscard]] FormatInfo GetFormatInfo(PixelFormat format) noexcept;
    [[nodiscard]] u32 CalculateMipCount(u32 width, u32 height, u32 depth = 1) noexcept;
    [[nodiscard]] u32 CalculateMipExtent(u32 baseExtent, u8 mipLevel) noexcept;
    [[nodiscard]] u32 CalculateMinimumRowPitch(PixelFormat format, u32 width) noexcept;
    [[nodiscard]] u32 CalculateMinimumSlicePitch(PixelFormat format, u32 width, u32 height) noexcept;

    enum class TextureFlags : u16
    {
        None = 0,
        Streamable = 1u << 0u,
        DirectGpuUpload = 1u << 1u
    };

    [[nodiscard]] constexpr TextureFlags operator|(TextureFlags left, TextureFlags right) noexcept
    {
        return static_cast<TextureFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(TextureFlags value, TextureFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    enum class SubresourceFlags : u16
    {
        None = 0,
        MipTail = 1u << 0u,
        DirectGpuUpload = 1u << 1u
    };

    [[nodiscard]] constexpr SubresourceFlags operator|(SubresourceFlags left, SubresourceFlags right) noexcept
    {
        return static_cast<SubresourceFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(SubresourceFlags value, SubresourceFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    /// One GPU uploadable subresource. A 3D texture has one subresource per mip and its complete depth is represented by
    /// depth and slicePitch. A cube has six faces per array layer in PositiveX, NegativeX, PositiveY, NegativeY,
    /// PositiveZ, NegativeZ order.
    struct SubresourceBuildRecord
    {
        u8 mipLevel = 0;
        u16 arrayLayer = 0;
        u8 face = 0;
        const void* data = nullptr;
        usize byteSize = 0;
        u32 rowPitch = 0;
        u32 slicePitch = 0;
    };

    struct BuildDescription
    {
        TextureDimension dimension = TextureDimension::Texture2D;
        PixelFormat format = PixelFormat::R8G8B8A8UNorm;
        ColorSpace colorSpace = ColorSpace::Linear;
        TextureFlags flags = TextureFlags::DirectGpuUpload;
        u32 width = 1;
        u32 height = 1;
        u32 depth = 1;
        u16 arrayLayers = 1;
        u8 mipCount = 1;
        /// First mip in the always-resident tail. Mip zero is the highest-resolution mip.
        u8 mipTailFirstLevel = 0;
        crypto::Digest256 sourceFingerprint;
        containers::ArraySpan<const SubresourceBuildRecord> subresources;
    };

    struct SubresourceRecord
    {
        u8 mipLevel = 0;
        u16 arrayLayer = 0;
        u8 face = 0;
        SubresourceFlags flags = SubresourceFlags::None;
        u32 width = 0;
        u32 height = 0;
        u32 depth = 0;
        u32 rowPitch = 0;
        u32 slicePitch = 0;
        u64 dataOffset = 0;
        u64 byteSize = 0;
        crypto::Digest256 digest;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 16ull * 1024ull * 1024ull * 1024ull;
        u64 maximumMetadataBytes = 64ull * 1024ull * 1024ull;
        u64 maximumTextureBytes = 15ull * 1024ull * 1024ull * 1024ull;
        u64 maximumSubresourceBytes = 4ull * 1024ull * 1024ull * 1024ull;
        u32 maximumDimension = 131072;
        u32 maximumArrayLayers = 65535;
        u32 maximumSubresources = 1048576;
    };

    enum class StorageSegmentFlags : u8
    {
        None = 0,
        Metadata = 1u << 0u,
        Streamable = 1u << 1u,
        RequiredForMipTail = 1u << 2u
    };

    [[nodiscard]] constexpr StorageSegmentFlags operator|(StorageSegmentFlags left, StorageSegmentFlags right) noexcept
    {
        return static_cast<StorageSegmentFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    struct StorageSegment
    {
        /// Segments are ordered, byte-exact, and together cover the complete vtex document, including alignment padding.
        u64 offset = 0;
        u64 byteSize = 0;
        u8 alignmentLog2 = 0;
        StorageSegmentFlags flags = StorageSegmentFlags::None;
        u32 subresource = InvalidSubresourceIndex;
    };

    class TextureFile final
    {
    public:
        TextureFile() noexcept;
        TextureFile(TextureFile&& other) noexcept;
        TextureFile& operator=(TextureFile&& other) noexcept;
        TextureFile(const TextureFile&) = delete;
        TextureFile& operator=(const TextureFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] TextureDimension GetDimension() const noexcept;
        [[nodiscard]] PixelFormat Format() const noexcept;
        [[nodiscard]] ColorSpace GetSpace() const noexcept;
        [[nodiscard]] TextureFlags GetFlags() const noexcept;
        [[nodiscard]] u32 GetWidth() const noexcept;
        [[nodiscard]] u32 GetHeight() const noexcept;
        [[nodiscard]] u32 GetDepth() const noexcept;
        [[nodiscard]] u16 GetArrayLayers() const noexcept;
        [[nodiscard]] u8 GetMipCount() const noexcept;
        [[nodiscard]] u8 GetMipTailFirstLevel() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetSourceFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SubresourceRecord> GetSubresources() const noexcept;
        [[nodiscard]] u64 GetTextureDataOffset() const noexcept;
        [[nodiscard]] u64 GetTextureDataSize() const noexcept;
        [[nodiscard]] u32 FindSubresource(u8 mipLevel, u16 arrayLayer = 0, u8 face = 0) const noexcept;
        [[nodiscard]] Result ReadSubresource(filesystem::IFile& reader, u32 index, void* destination, usize capacity) const noexcept;

    private:
        bool m_open = false;
        TextureDimension m_dimension = TextureDimension::Texture2D;
        PixelFormat m_format = PixelFormat::R8G8B8A8UNorm;
        ColorSpace m_colorSpace = ColorSpace::Linear;
        TextureFlags m_flags = TextureFlags::None;
        u32 m_width = 0;
        u32 m_height = 0;
        u32 m_depth = 0;
        u16 m_arrayLayers = 0;
        u8 m_mipCount = 0;
        u8 m_mipTailFirstLevel = 0;
        crypto::Digest256 m_sourceFingerprint;
        crypto::Digest256 m_contentFingerprint;
        u64 m_dataOffset = 0;
        u64 m_dataSize = 0;
        containers::DynamicArray<SubresourceRecord> m_subresources;
    };

    [[nodiscard]] Result WriteTexture(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result BuildStorageSegments(const TextureFile& texture, u64 documentSize, containers::DynamicArray<StorageSegment>& segments,
                                              u32 maximumSegments = 1048577) noexcept;
} // namespace vanguard::textures
