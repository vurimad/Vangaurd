#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/mesh_tools/mesh_import.hpp>

namespace vanguard::mesh_tools
{
    inline constexpr resources::ResourceTypeId MeshSourceResourceType = serialization::MakeFourCC('V', 'M', 'S', 'R');
    inline constexpr resources::ResourceTypeId MeshCompilerToolResourceType = serialization::MakeFourCC('V', 'M', 'T', 'L');
    inline constexpr u32 MeshAssetCompilerVersion = 1;

    enum class MeshBuildSettingsResult : u8
    {
        Success,
        InvalidArgument,
        LimitExceeded,
        UnsupportedVersion
    };

    struct MeshBuildDescription
    {
        const char* sourceName = nullptr;
        const char* formatHint = nullptr;
        MeshImportSettings import;
        CookSettings cook;
    };

    /// Produces the canonical byte sequence placed in assets::BuildRequest::settings.
    [[nodiscard]] MeshBuildSettingsResult EncodeMeshBuildSettings(const MeshBuildDescription& description, containers::DynamicArray<u8>& output) noexcept;

    /// Project/asset-database boundary. The returned path must identify the exact source bytes
    /// supplied in BuildRequest::source.content. The compiler verifies that identity before use.
    using ResolveMeshSourceFileFunction = bool (*)(resources::ResourceReference source, filesystem::AbsolutePath& file, void* userData) noexcept;

    struct MeshDependencySource
    {
        resources::ResourceReference identity;
        crypto::Digest256 content;
    };

    /// Maps an absolute file opened by Assimp to a source-asset identity and current digest.
    /// The main source may be returned; it is recognized and is not added twice.
    using ResolveMeshDependencyFunction = bool (*)(const char* absolutePath, MeshDependencySource& dependency, void* userData) noexcept;

    struct MeshAssetCompilerConfig
    {
        ResolveMeshSourceFileFunction resolveSourceFile = nullptr;
        void* resolveSourceFileUserData = nullptr;
        // Required because even a self-contained import reports its main opened file; this
        // callback maps that file back to BuildRequest::source and maps auxiliaries separately.
        ResolveMeshDependencyFunction resolveDependency = nullptr;
        void* resolveDependencyUserData = nullptr;
        u32 maximumDependencies = 4096;
    };

    class MeshAssetCompiler final
    {
    public:
        struct Impl;

        MeshAssetCompiler() noexcept = default;
        ~MeshAssetCompiler();

        MeshAssetCompiler(const MeshAssetCompiler&) = delete;
        MeshAssetCompiler& operator=(const MeshAssetCompiler&) = delete;

        [[nodiscard]] bool Initialize(const MeshAssetCompilerConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] assets::CompilerDescriptor GetDescriptor() noexcept;
        [[nodiscard]] assets::Result Register(assets::BuildSystem& buildSystem) noexcept;
        [[nodiscard]] assets::Result Unregister() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::mesh_tools
