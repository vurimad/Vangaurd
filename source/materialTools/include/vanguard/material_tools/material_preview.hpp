#pragma once

#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/materials/materials.hpp>

namespace vanguard::material_tools
{
    struct MaterialPreviewBuildSet
    {
        assets::BuildRequest program;
        containers::ArraySpan<const assets::BuildRequest> pipelines;
        assets::BuildRequest material;
    };

    enum class MaterialPreviewState : u8
    {
        Idle,
        Building,
        Valid,
        Failed,
        Cancelled
    };

    struct MaterialPreviewLimits
    {
        u32 maximumPipelines = 256;
        u64 maximumRetainedArtifactBytes = 768ull * 1024ull * 1024ull;
    };

    using ApplyMaterialRevisionFunction = bool (*)(u64 revision, void* userData) noexcept;

    class MaterialPreviewService final
    {
    public:
        struct Impl;

        MaterialPreviewService() noexcept = default;
        ~MaterialPreviewService();

        MaterialPreviewService(const MaterialPreviewService&) = delete;
        MaterialPreviewService& operator=(const MaterialPreviewService&) = delete;

        [[nodiscard]] bool Initialize(assets::BuildSystem& buildSystem, assets::ResolveGeneratedDependencyFunction fallbackResolver = nullptr,
                                      void* fallbackUserData = nullptr, const assets::BuildGraphConfig& graphConfig = {},
                                      const MaterialPreviewLimits& limits = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Copies all requests synchronously through BuildGraph resolution. A newer
        /// submission releases interest in every operation of the previous revision.
        [[nodiscard]] u64 Submit(const MaterialPreviewBuildSet& build) noexcept;
        [[nodiscard]] MaterialPreviewState Poll() noexcept;
        void Wait() noexcept;
        [[nodiscard]] bool Cancel() noexcept;

        [[nodiscard]] u64 CurrentRevision() const noexcept;
        [[nodiscard]] u64 LastValidRevision() const noexcept;
        [[nodiscard]] MaterialPreviewState State() const noexcept;
        [[nodiscard]] bool CopyLastValidMaterial(assets::BuildOutput& output) const noexcept;
        [[nodiscard]] bool CopyLastReport(assets::BuildReport& report) const noexcept;

        /// Apply commits the authored revision through its owner. Preview artifact
        /// bytes are deliberately not exposed to this callback.
        [[nodiscard]] bool Apply(ApplyMaterialRevisionFunction apply, void* userData = nullptr) noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::material_tools
