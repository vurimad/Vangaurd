#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>

namespace vanguard::shader_tools
{
    inline constexpr resources::ResourceTypeId ShaderSourceResourceType = serialization::MakeFourCC('V', 'S', 'S', 'R');
    inline constexpr resources::ResourceTypeId ShaderCompilerToolResourceType = serialization::MakeFourCC('V', 'S', 'T', 'L');
    inline constexpr u32 ShaderAssetCompilerVersion = 1;

    enum class BuildSettingsResult : u8
    {
        Success,
        InvalidArgument,
        LimitExceeded,
        InvalidEncoding,
        UnsupportedVersion
    };

    struct ShaderBuildDescription
    {
        const char* sourceName = nullptr;
        u64 program = 0;
        crypto::Digest256 permutation;
        containers::ArraySpan<const EntryPoint> entryPoints;
        containers::ArraySpan<const Define> defines;
        Optimization optimization = Optimization::High;
        DebugInformation debugInformation = DebugInformation::None;
        const char* profile = "sm_6_6";
        bool warningsAsErrors = true;
        bool preciseFloatingPoint = false;
    };

    /// Produces the canonical byte sequence placed in assets::BuildRequest::settings.
    [[nodiscard]] BuildSettingsResult EncodeShaderBuildSettings(const ShaderBuildDescription& description, containers::DynamicArray<u8>& output) noexcept;

    struct ShaderSource
    {
        const char* canonicalPath = nullptr;
        resources::ResourceReference identity;
        containers::ArraySpan<const u8> content;
    };

    /// May be called concurrently by dependency planning and compilation. Returned path and content
    /// storage must remain valid until at least the next provider invocation on the same thread.
    using LoadShaderSourceFunction = bool (*)(const char* canonicalPath, ShaderSource& source, void* userData) noexcept;

    struct ShaderAssetCompilerConfig
    {
        LoadShaderSourceFunction loadSource = nullptr;
        void* loadSourceUserData = nullptr;
        u32 maximumIncludeFiles = 4096;
        u32 maximumIncludeDepth = 64;
    };

    class ShaderAssetCompiler final
    {
    public:
        struct Impl;

        ShaderAssetCompiler() noexcept = default;
        ~ShaderAssetCompiler();

        ShaderAssetCompiler(const ShaderAssetCompiler&) = delete;
        ShaderAssetCompiler& operator=(const ShaderAssetCompiler&) = delete;

        [[nodiscard]] bool Initialize(const ShaderAssetCompilerConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] assets::CompilerDescriptor GetDescriptor() noexcept;
        [[nodiscard]] assets::Result Register(assets::BuildSystem& buildSystem) noexcept;
        [[nodiscard]] assets::Result Unregister() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::shader_tools
