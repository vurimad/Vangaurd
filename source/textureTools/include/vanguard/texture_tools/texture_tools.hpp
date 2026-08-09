#pragma once

#include <vanguard/textures/textures.hpp>

namespace vanguard::texture_tools
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        UnknownCookingProfile,
        UnsupportedSourceFormat,
        UnsupportedTargetFormat,
        InvalidSourceLayout,
        InvalidSourceData,
        LimitExceeded,
        OutOfMemory,
        CodecFailure,
        TextureWriteFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class SourcePixelFormat : u8
    {
        R8UNorm,
        R8G8UNorm,
        R8G8B8A8UNorm,
        R16G16B16A16UNorm,
        R16G16B16A16Float,
        R32G32B32A32Float
    };

    struct SourceImage
    {
        const void* data = nullptr;
        usize byteSize = 0;
        u32 rowPitch = 0;
        u32 slicePitch = 0;
    };

    /// Importer-neutral base-level texture source. Images are ordered by array layer and then cube face. Cube faces use
    /// the Direct3D/NVRHI order PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ. Generated cube mips
    /// are filtered jointly in direction space; callers must not pre-rotate faces into an atlas convention.
    /// A 3D texture has one image whose slicePitch advances through depth slices. Multi-byte components are little-endian,
    /// rows may be unaligned, and source memory remains caller-owned. NaN and infinity are rejected.
    struct SourceTexture
    {
        textures::TextureDimension dimension = textures::TextureDimension::Texture2D;
        SourcePixelFormat format = SourcePixelFormat::R8G8B8A8UNorm;
        textures::ColorSpace colorSpace = textures::ColorSpace::Linear;
        u32 width = 1;
        u32 height = 1;
        u32 depth = 1;
        u16 arrayLayers = 1;
        crypto::Digest256 sourceFingerprint;
        containers::ArraySpan<const SourceImage> images;
    };

    using TextureCookingProfileId = u64;

    namespace profiles
    {
        inline constexpr TextureCookingProfileId Color = 0x636f6c6f72000001ull;
        inline constexpr TextureCookingProfileId ColorAlpha = 0x636f6c6f72616c70ull;
        inline constexpr TextureCookingProfileId Normal = 0x6e6f726d616c0001ull;
        inline constexpr TextureCookingProfileId Masks = 0x6d61736b73000001ull;
        inline constexpr TextureCookingProfileId Data = 0x6461746100000001ull;
        inline constexpr TextureCookingProfileId Ui = 0x7569000000000001ull;
        inline constexpr TextureCookingProfileId Hdr = 0x6864720000000001ull;
    }

    enum class TextureCookingFlags : u16
    {
        None = 0,
        GenerateFullMipChain = 1u << 0u,
        Streamable = 1u << 1u,
        RenormalizeNormals = 1u << 2u,
        PreserveAlphaCoverage = 1u << 3u
    };

    [[nodiscard]] constexpr TextureCookingFlags operator|(TextureCookingFlags left, TextureCookingFlags right) noexcept
    {
        return static_cast<TextureCookingFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(TextureCookingFlags value, TextureCookingFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    struct TextureCookingProfile
    {
        TextureCookingProfileId id = 0;
        u32 version = 1;
        textures::PixelFormat targetFormat = textures::PixelFormat::R8G8B8A8UNorm;
        textures::ColorSpace targetColorSpace = textures::ColorSpace::Linear;
        TextureCookingFlags flags = TextureCookingFlags::GenerateFullMipChain | TextureCookingFlags::Streamable;
        u8 mipTailCount = 4;
        u8 alphaCoverageChannel = 3;
        f32 alphaCoverageThreshold = 0.5f;
        u8 compressionQuality = 10;
    };

    enum class ProfileRegistrationResult : u8
    {
        Success,
        InvalidArgument,
        DuplicateIdentifier,
        CapacityExceeded,
        RegistrySealed
    };

    struct CookSettings
    {
        enum class Execution : u8
        {
            /// Use Vanguard Jobs when the scheduler is initialized; otherwise execute serially.
            Automatic,
            /// Never dispatch work. Useful for deterministic reference measurements and constrained tools.
            Serial,
            /// Require Vanguard Jobs to be initialized. CookTexture returns InvalidState when it is unavailable.
            Jobs
        };

        TextureCookingProfileId profile = profiles::Color;
        Execution execution = Execution::Automatic;
        /// Upper bound passed directly to parallel dispatch. Zero lets the scheduler select its batch size.
        u32 maximumBlocksPerJobBatch = 256;
        u32 maximumDimension = 131072;
        u32 maximumSubresources = 1048576;
        u64 maximumOutputBytes = 16ull * 1024ull * 1024ull * 1024ull;
    };

    struct CookReport
    {
        TextureCookingProfileId profile = 0;
        u32 profileVersion = 0;
        textures::PixelFormat targetFormat = textures::PixelFormat::R8G8B8A8UNorm;
        u8 mipCount = 0;
        u32 subresourceCount = 0;
        u64 sourceBytes = 0;
        u64 cookedBytes = 0;
        u64 encodedBlockCount = 0;
        u64 generatedMipTexelCount = 0;
        f32 maximumAlphaCoverageError = 0.0f;
        bool usedJobs = false;
    };

    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] ProfileRegistrationResult RegisterCookingProfile(const TextureCookingProfile& profile) noexcept;
    [[nodiscard]] const TextureCookingProfile* FindCookingProfile(TextureCookingProfileId id) noexcept;
    [[nodiscard]] Result CookTexture(const SourceTexture& source, filesystem::IFile& output,
                                     const CookSettings& settings = {}, CookReport* report = nullptr) noexcept;
} // namespace vanguard::texture_tools
