#pragma once

#include <vanguard/rendering/material_residency_runtime.hpp>
#include <vanguard/rendering/render_scene_gpu.hpp>

namespace vanguard::rendering
{
    struct MaterialSceneBindingConfig
    {
        u32 maximumBindings = 16'384;
        u32 maximumChecksPerUpdate = 256;
    };

    enum class MaterialSceneBindingFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidArgument,
        InvalidProxy,
        CapacityExceeded,
        ResidencyFailure,
        ScenePublicationFailure,
        LiveBindingsRemain
    };

    struct MaterialSceneBindingFailure
    {
        MaterialSceneBindingFailureCode code = MaterialSceneBindingFailureCode::None;
        const char* message = nullptr;
        RenderProxyHandle proxy;
        MaterialResidencyRuntimeFailure residencyFailure;
        RenderSceneGpuFailure sceneFailure;
    };

    struct MaterialSceneBindingInfo
    {
        RenderProxyHandle proxy;
        MaterialResidencyHandle activeResidency;
        MaterialResidencyHandle candidateResidency;
        GpuMaterialHandle activeMaterial;
        MaterialResidencyState candidateState = MaterialResidencyState::Invalid;
        bool awaitingScenePublication = false;
        bool clearing = false;
    };

    struct MaterialSceneBindingStats
    {
        u32 bindings = 0;
        u32 activeBindings = 0;
        u32 candidateBindings = 0;
        u64 requests = 0;
        u64 replacements = 0;
        u64 candidateCancellations = 0;
        u64 candidateFailures = 0;
        u64 destroyedProxies = 0;
        u64 stateChecks = 0;
    };

    /// Main-thread ownership bridge between exact material residency demands and
    /// tracked decal consumers. Replacement commits only after the scene publisher
    /// accepts the matching binding receipt. Mesh material sets are assembled and
    /// retained by the renderable-definition owner, not by this bridge.
    class MaterialSceneBindingBridge final
    {
    public:
        struct Impl;

        MaterialSceneBindingBridge() noexcept = default;
        ~MaterialSceneBindingBridge();

        MaterialSceneBindingBridge(const MaterialSceneBindingBridge&) = delete;
        MaterialSceneBindingBridge& operator=(const MaterialSceneBindingBridge&) = delete;

        [[nodiscard]] bool Initialize(MaterialResidencyRuntime& residency, RenderSceneGpuPublisher& publisher, const MaterialSceneBindingConfig& config = {},
                                      MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool SetDecalMaterial(RenderProxyHandle proxy, const resources::ResourceHandle& material, MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelCandidate(RenderProxyHandle proxy, MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Remove(RenderProxyHandle proxy, MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Update(MaterialSceneBindingFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetInfo(RenderProxyHandle proxy, MaterialSceneBindingInfo& info, MaterialSceneBindingFailure* failure = nullptr) const noexcept;
        [[nodiscard]] MaterialSceneBindingStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
