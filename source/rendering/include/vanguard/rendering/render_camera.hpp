#pragma once

#include <vanguard/rendering/custom_data.hpp>

#include <type_traits>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumRenderCameraNameBytes = 96;
    inline constexpr u32 MaximumRenderCameraDependencies = 8;

    struct RenderCameraHandle
    {
        RenderSceneHandle scene;
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && index < MaximumRenderViews && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderCameraHandle&, const RenderCameraHandle&) noexcept = default;
    };

    inline constexpr RenderCameraHandle InvalidRenderCameraHandle{};

    enum class RenderCameraRenderPolicy : u8
    {
        OnDemand,
        Always
    };

    enum class RenderCameraProjectionKind : u8
    {
        Perspective,
        Orthographic
    };

    enum class RenderCameraResolutionMode : u8
    {
        InheritFrame,
        ScaleFrame,
        Fixed
    };

    struct RenderCameraExtent
    {
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return width != 0 && height != 0;
        }
    };

    struct RenderCameraPose
    {
        RenderViewOrigin origin;
        f32 orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct RenderCameraProjection
    {
        RenderCameraProjectionKind kind = RenderCameraProjectionKind::Perspective;
        f32 verticalFieldOfViewRadians = 1.0471975512f;
        f32 orthographicHeight = 10.0f;
        f32 aspectRatio = 0.0f;
        f32 nearPlane = 0.1f;
        f32 farPlane = 1000.0f;
        f32 offset[2]{};
        bool reverseDepth = true;
        bool infiniteFarPlane = false;
    };

    struct RenderCameraResolution
    {
        RenderCameraResolutionMode mode = RenderCameraResolutionMode::InheritFrame;
        RenderCameraExtent fixedExtent;
        f32 scale = 1.0f;
    };

    struct RenderCameraState
    {
        RenderCameraPose pose;
        RenderCameraProjection projection;
        RenderCameraResolution resolution;
        RenderViewPurpose purpose = RenderViewPurpose::Main;
        RenderViewFlags flags = RenderViewFlags::Primary | RenderViewFlags::OcclusionCulling;
        RenderPhaseSet phases;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        f32 lodBias = 0.0f;
        bool temporalHistory = true;
        bool temporalJitter = true;
    };

    enum class RenderCameraDependencyOutputs : u8
    {
        None = 0,
        Color = 1u << 0u,
        Final = 1u << 1u
    };

    [[nodiscard]] constexpr RenderCameraDependencyOutputs operator|(const RenderCameraDependencyOutputs left,
                                                                    const RenderCameraDependencyOutputs right) noexcept
    {
        return static_cast<RenderCameraDependencyOutputs>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasOutput(const RenderCameraDependencyOutputs value, const RenderCameraDependencyOutputs output) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(output)) != 0;
    }

    struct RenderCameraDesc
    {
        RenderSceneHandle scene;
        const char* name = nullptr;
        RenderCameraRenderPolicy renderPolicy = RenderCameraRenderPolicy::OnDemand;
        RenderCameraState state;
        bool enabled = true;
    };

    struct RenderCameraDependency
    {
        /// The parent consumes one or more outputs produced by the child. Dependency planning
        /// therefore emits the child before the parent. Camera-pose derivation is intentionally
        /// separate and belongs to typed camera custom data.
        RenderCameraHandle parent;
        RenderCameraHandle child;
        RenderCameraDependencyOutputs outputs = RenderCameraDependencyOutputs::None;
    };

    struct RenderCameraSnapshot
    {
        RenderCameraHandle handle;
        RenderCameraRenderPolicy renderPolicy = RenderCameraRenderPolicy::OnDemand;
        RenderCameraState state;
        u32 dependencyCount = 0;
        u32 dependentCount = 0;
        bool enabled = false;
        char name[MaximumRenderCameraNameBytes]{};
    };

    struct RenderCameraDependencyPlan
    {
        RenderSceneHandle scene;
        containers::ArraySpan<const RenderCameraHandle> cameras;
        containers::ArraySpan<const RenderCameraDependency> dependencies;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return scene.IsValid() && !cameras.Empty();
        }
    };

    struct RenderViewFamilyPrepareRequest
    {
        RenderSceneHandle scene;
        containers::ArraySpan<const RenderCameraHandle> roots;
        RenderCameraExtent frameExtent;
        u64 frameSerial = 0;
        u32 jitterIndex = 0;
        bool enableTemporalJitter = true;
        bool forceCameraCut = false;
    };

    class RenderCameraStorage;
    class RenderCommandSystem;
    class FrameRenderer;
    class FrameCustomData;

    /// Retained, immutable, dependency-ordered view family prepared from persistent cameras for one
    /// frame. The caller owns one reference until it is transferred into a RenderFrameInfo or released.
    class PreparedRenderViewFamily final
    {
    public:
        PreparedRenderViewFamily() noexcept = default;
        PreparedRenderViewFamily(const PreparedRenderViewFamily& other) noexcept;
        PreparedRenderViewFamily(PreparedRenderViewFamily&& other) noexcept;
        PreparedRenderViewFamily& operator=(const PreparedRenderViewFamily& other) noexcept;
        PreparedRenderViewFamily& operator=(PreparedRenderViewFamily&& other) noexcept;
        ~PreparedRenderViewFamily();

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderSceneHandle GetScene() const noexcept;
        [[nodiscard]] u64 GetFrameSerial() const noexcept;
        [[nodiscard]] const RenderViewFamily& GetFamily() const noexcept;
        [[nodiscard]] containers::ArraySpan<const RenderView> GetViews() const noexcept;
        [[nodiscard]] containers::ArraySpan<const RenderCameraDependency> GetDependencies() const noexcept;
        void Commit() const noexcept;
        void Release() noexcept;

    private:
        friend class RenderCameraStorage;
        friend class FrameCustomData;

        RenderCameraStorage* m_owner = nullptr;
        RenderSceneHandle m_scene;
        u32 m_slot = ~u32{0};
        u32 m_generation = 0;
        u64 m_frameSerial = 0;
    };

    /// Retained, read-only access to custom data prepared for one exact view-family frame. Copies retain
    /// the prepared family so render jobs cannot outlive the camera and scene data they read.
    class FrameCustomData final
    {
    public:
        FrameCustomData() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderSceneHandle GetScene() const noexcept;
        [[nodiscard]] u64 GetFrameSerial() const noexcept;
        [[nodiscard]] const SceneCustomData* GetSceneData(u32 typeIndex) const noexcept;
        [[nodiscard]] const CameraCustomData* GetCameraData(RenderViewId view, u32 typeIndex) const noexcept;

        template <typename Data> [[nodiscard]] const Data* GetSceneData() const noexcept
        {
            static_assert(std::is_base_of_v<SceneCustomData, Data>);
            return static_cast<const Data*>(GetSceneData(Data::TypeIndex));
        }

        template <typename Data> [[nodiscard]] const Data* GetCameraData(const RenderViewId view) const noexcept
        {
            static_assert(std::is_base_of_v<CameraCustomData, Data>);
            return static_cast<const Data*>(GetCameraData(view, Data::TypeIndex));
        }

    private:
        friend class FrameRenderer;

        explicit FrameCustomData(const PreparedRenderViewFamily& family) noexcept : m_family(family) {}

        PreparedRenderViewFamily m_family;
    };

    enum class RenderCameraFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidDescriptor,
        InvalidHandle,
        WrongScene,
        CapacityExceeded,
        DependencyCapacityExceeded,
        DependencyCycle,
        DependencyDepthExceeded,
        CameraStillReferenced,
        CamerasRemainAlive,
        Busy,
        InvalidCameraState,
        PreparedFamilyUnavailable,
        CustomDataNotReady
    };

    struct RenderCameraFailure
    {
        RenderCameraFailureCode code = RenderCameraFailureCode::None;
        RenderSceneHandle scene;
        RenderCameraHandle camera;
        RenderCameraHandle relatedCamera;
        CustomDataCatalogFailure customDataFailure;
        CustomDataKind customDataKind = CustomDataKind::Camera;
        u32 customDataTypeIndex = ~u32{0};
        const char* message = nullptr;
    };

    struct RenderCameraStorageConfig
    {
        u32 maximumDependencyDepth = 4;
        u32 maximumFramesInFlight = 3;
        CustomDataCatalog customData;
    };

    struct RenderCameraStorageStats
    {
        u32 attachedScenes = 0;
        u32 activeCameras = 0;
        u32 alwaysRenderCameras = 0;
        u32 dependencies = 0;
        u64 registeredCameras = 0;
        u64 unregisteredCameras = 0;
        u64 preparedPlans = 0;
        u64 preparedFrames = 0;
        u64 committedFrames = 0;
        u64 abandonedFrames = 0;
        u64 rejectedOperations = 0;
    };

    /// Per-RenderScene persistent camera registry and dependency graph. Mutation is a renderer-main-thread
    /// commit operation. BuildDependencyPlan is allocation-free and emits dependency children before parents.
    class RenderCameraStorage final
    {
    public:
        struct Impl;

        RenderCameraStorage() noexcept = default;
        ~RenderCameraStorage();

        RenderCameraStorage(const RenderCameraStorage&) = delete;
        RenderCameraStorage& operator=(const RenderCameraStorage&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, const RenderCameraStorageConfig& config = {},
                                      RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool HasDependency(RenderCameraHandle parent, RenderCameraHandle child, RenderCameraDependencyOutputs* outputs = nullptr) const noexcept;

        [[nodiscard]] bool IsAlive(RenderCameraHandle camera) const noexcept;
        [[nodiscard]] bool GetSnapshot(RenderCameraHandle camera, RenderCameraSnapshot& snapshot) const noexcept;
        [[nodiscard]] RenderCameraStorageStats GetStats() const noexcept;

    private:
        friend class RenderSceneManager;
        friend class PreparedRenderViewFamily;
        friend class FrameCustomData;
        friend class RenderCommandSystem;
        friend class FrameRenderer;

        [[nodiscard]] bool RegisterCamera(const RenderCameraDesc& desc, RenderCameraHandle& camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterCamera(RenderCameraHandle camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateCamera(RenderCameraHandle camera, const RenderCameraState& state, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestCameraCut(RenderCameraHandle camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetEnabled(RenderCameraHandle camera, bool enabled, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetRenderPolicy(RenderCameraHandle camera, RenderCameraRenderPolicy policy, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AddDependency(RenderCameraHandle parent, RenderCameraHandle child, RenderCameraDependencyOutputs outputs,
                                         RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RemoveDependency(RenderCameraHandle parent, RenderCameraHandle child, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BuildDependencyPlan(RenderSceneHandle scene, containers::ArraySpan<const RenderCameraHandle> roots,
                                               containers::ArraySpan<RenderCameraHandle> cameraStorage,
                                               containers::ArraySpan<RenderCameraDependency> dependencyStorage, RenderCameraDependencyPlan& plan,
                                               RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PrepareViewFamily(const RenderViewFamilyPrepareRequest& request, PreparedRenderViewFamily& family,
                                             RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PrepareCustomData(const PreparedRenderViewFamily& family, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CheckCustomDataReadiness(const PreparedRenderViewFamily& family,
                                                    RenderCameraFailure* failure = nullptr) const noexcept;
        [[nodiscard]] const char* GetRenderingBlockReason(RenderSceneHandle scene) const noexcept;
        void TickWhileLoading(RenderSceneHandle scene, bool isFirstFrame) noexcept;
        [[nodiscard]] const SceneCustomData* GetFrameSceneCustomData(const PreparedRenderViewFamily& family, u32 typeIndex) const noexcept;
        [[nodiscard]] const CameraCustomData* GetFrameCameraCustomData(const PreparedRenderViewFamily& family, RenderViewId view,
                                                                       u32 typeIndex) const noexcept;

        [[nodiscard]] bool IsFrameValid(const PreparedRenderViewFamily& family) const noexcept;
        [[nodiscard]] const RenderViewFamily& GetFrameFamily(const PreparedRenderViewFamily& family) const noexcept;
        [[nodiscard]] containers::ArraySpan<const RenderView> GetFrameViews(const PreparedRenderViewFamily& family) const noexcept;
        [[nodiscard]] containers::ArraySpan<const RenderCameraDependency> GetFrameDependencies(const PreparedRenderViewFamily& family) const noexcept;
        [[nodiscard]] bool RetainFrame(const PreparedRenderViewFamily& family) noexcept;
        void CommitFrame(const PreparedRenderViewFamily& family) noexcept;
        void ReleaseFrame(PreparedRenderViewFamily& family) noexcept;

        [[nodiscard]] bool AttachScene(RenderSceneHandle scene, u32 maximumCameras) noexcept;
        [[nodiscard]] bool CanDetachScene(RenderSceneHandle scene) const noexcept;
        [[nodiscard]] bool DetachScene(RenderSceneHandle scene) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
