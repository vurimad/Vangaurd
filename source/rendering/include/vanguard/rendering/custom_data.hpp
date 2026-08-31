#pragma once

#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumCameraCustomDataTypes = 16;
    inline constexpr u32 MaximumSceneCustomDataTypes = 16;

    enum class CustomDataKind : u8
    {
        Camera,
        Scene
    };

    enum class CustomDataPriority : u8
    {
        Low,
        Normal,
        High
    };

    /// Immutable frame data supplied while renderer-owned custom data is prepared. It deliberately
    /// provides no dispatch, synchronization, or preparation operations.
    struct CustomDataPrepareInfo
    {
        u64 frameSerial = 0;
        RenderSceneHandle scene;
        const RenderViewFamily* family = nullptr;
        const RenderView* view = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return frameSerial != 0 && scene.IsValid() && family != nullptr;
        }
    };

    class RenderCameraStorage;

    /// Common persistent state shared by camera and scene custom data. Instances are prepared on the
    /// serialized renderer CPU chain and remain alive until that chain no longer references their owner.
    class CustomData
    {
    public:
        CustomData() noexcept = default;
        virtual ~CustomData() = default;

        CustomData(const CustomData&) = delete;
        CustomData& operator=(const CustomData&) = delete;
        CustomData(CustomData&&) = delete;
        CustomData& operator=(CustomData&&) = delete;

        [[nodiscard]] virtual u32 GetTypeIndex() const noexcept = 0;

        [[nodiscard]] virtual CustomDataPriority GetPriority() const noexcept
        {
            return CustomDataPriority::Normal;
        }

        /// Returns nullptr when rendering may proceed, otherwise a stable diagnostic reason.
        [[nodiscard]] virtual const char* GetRenderingBlockReason() const noexcept
        {
            return nullptr;
        }

        virtual void TickWhileLoading(bool) noexcept {}

        [[nodiscard]] bool IsPreparedFor(const u64 frameSerial) const noexcept
        {
            return frameSerial != 0 && m_preparedFrameSerial == frameSerial;
        }

        [[nodiscard]] bool HasBeenPrepared() const noexcept
        {
            return m_preparedFrameSerial != 0;
        }

    private:
        friend class RenderCameraStorage;

        void MarkPrepared(const u64 frameSerial) noexcept
        {
            m_preparedFrameSerial = frameSerial;
        }

        void ClearPrepared() noexcept
        {
            m_preparedFrameSerial = 0;
        }

        u64 m_preparedFrameSerial = 0;
    };

    class CameraCustomData : public CustomData
    {
    public:
        ~CameraCustomData() override = default;

        virtual void Initialize() noexcept = 0;
        virtual void Prepare(const CustomDataPrepareInfo& info) noexcept = 0;
        virtual void Evict() noexcept = 0;
    };

    class SceneCustomData : public CustomData
    {
    public:
        ~SceneCustomData() override = default;

        virtual void Initialize() noexcept = 0;
        virtual void Prepare(const CustomDataPrepareInfo& info) noexcept = 0;
        virtual void Evict() noexcept = 0;
    };

    template <u32 Index> class CameraCustomDataType : public CameraCustomData
    {
    public:
        static_assert(Index < MaximumCameraCustomDataTypes);
        static constexpr u32 TypeIndex = Index;

        [[nodiscard]] u32 GetTypeIndex() const noexcept final
        {
            return TypeIndex;
        }
    };

    template <u32 Index> class SceneCustomDataType : public SceneCustomData
    {
    public:
        static_assert(Index < MaximumSceneCustomDataTypes);
        static constexpr u32 TypeIndex = Index;

        [[nodiscard]] u32 GetTypeIndex() const noexcept final
        {
            return TypeIndex;
        }
    };

    using CreateCameraCustomData = CameraCustomData* (*)() noexcept;
    using DestroyCameraCustomData = void (*)(CameraCustomData*) noexcept;
    using CreateSceneCustomData = SceneCustomData* (*)() noexcept;
    using DestroySceneCustomData = void (*)(SceneCustomData*) noexcept;

    struct CameraCustomDataDescriptor
    {
        const char* name = nullptr;
        u32 typeIndex = ~u32{0};
        CreateCameraCustomData create = nullptr;
        DestroyCameraCustomData destroy = nullptr;
    };

    struct SceneCustomDataDescriptor
    {
        const char* name = nullptr;
        u32 typeIndex = ~u32{0};
        CreateSceneCustomData create = nullptr;
        DestroySceneCustomData destroy = nullptr;
    };

    struct CustomDataCatalog
    {
        containers::ArraySpan<const CameraCustomDataDescriptor> cameraTypes;
        containers::ArraySpan<const SceneCustomDataDescriptor> sceneTypes;
    };

    enum class CustomDataCatalogFailureCode : u8
    {
        None,
        InvalidDescriptor,
        TypeIndexOutOfRange,
        DuplicateTypeIndex
    };

    struct CustomDataCatalogFailure
    {
        CustomDataCatalogFailureCode code = CustomDataCatalogFailureCode::None;
        CustomDataKind kind = CustomDataKind::Camera;
        u32 typeIndex = ~u32{0};
        const char* message = nullptr;
    };

    /// Validates the fixed type directories without allocating or constructing custom-data objects.
    [[nodiscard]] bool ValidateCustomDataCatalog(const CustomDataCatalog& catalog, CustomDataCatalogFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
