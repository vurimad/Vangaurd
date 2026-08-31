#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/texture_tools/texture_import.hpp>

namespace vanguard::texture_tools
{
    inline constexpr resources::ResourceTypeId TextureSourceResourceType = serialization::MakeFourCC('V', 'T', 'S', 'R');
    inline constexpr resources::ResourceTypeId TextureCompilerToolResourceType = serialization::MakeFourCC('V', 'T', 'T', 'L');
    inline constexpr u32 TextureAssetCompilerVersion = 1;

    enum class TextureBuildSourceMode : u8
    {
        Image2D,
        CubeCrossHorizontal,
        CubeCrossVertical,
        PreservedDds
    };

    enum class TextureBuildRouteFlags : u8
    {
        None = 0,
        Streamable = 1u << 0u
    };

    enum class TextureBuildSettingsResult : u8
    {
        Success,
        InvalidArgument,
        LimitExceeded,
        UnsupportedVersion
    };

    struct TextureBuildDescription
    {
        TextureBuildSourceMode sourceMode = TextureBuildSourceMode::Image2D;
        ImportedColorSpace colorSpace = ImportedColorSpace::Automatic;
        TextureChannelMapping channels;
        TextureBuildRouteFlags routeFlags = TextureBuildRouteFlags::None;
        u8 ddsMipTailCount = 0;
        TextureCookingProfileId profile = profiles::Color;
    };

    /// Produces the canonical fixed-size V1 recipe placed in assets::BuildRequest::settings.
    [[nodiscard]] TextureBuildSettingsResult EncodeTextureBuildSettings(const TextureBuildDescription& description,
                                                                        containers::DynamicArray<u8>& output) noexcept;

    class TextureAssetCompiler final
    {
    public:
        struct Impl;

        TextureAssetCompiler() noexcept = default;
        ~TextureAssetCompiler();

        TextureAssetCompiler(const TextureAssetCompiler&) = delete;
        TextureAssetCompiler& operator=(const TextureAssetCompiler&) = delete;

        /// Freezes the texture-tools configuration. Custom profiles/importers must be registered first.
        [[nodiscard]] bool Initialize() noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] assets::CompilerDescriptor GetDescriptor() noexcept;
        [[nodiscard]] assets::Result Register(assets::BuildSystem& buildSystem) noexcept;
        [[nodiscard]] assets::Result Unregister() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::texture_tools
