#pragma once

#include <vanguard/jobs/jobs.hpp>

namespace vanguard::entities
{
    class IPlacedComponent;
    class ITransformAttachment;

    struct TransformSystemConfig
    {
        u32 maximumComponents = 1u << 21u;
        u32 maximumAttachments = (1u << 21u) - 1u;
        u32 maximumScheduledRoots = 64u * 1024u;
        u32 maximumHierarchyDepth = 256u;
    };

    enum class TransformFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidComponent,
        InvalidAttachment,
        AttachmentCycle,
        CapacityExceeded,
        Busy,
        DispatchFailure
    };

    struct TransformFailure
    {
        TransformFailureCode code = TransformFailureCode::None;
        IPlacedComponent* component = nullptr;
        IPlacedComponent* source = nullptr;
        IPlacedComponent* destination = nullptr;
        const char* message = nullptr;
    };

    struct TransformSystemStats
    {
        u32 components = 0;
        u32 attachments = 0;
        u32 scheduledRoots = 0;
        u64 dispatchedRoots = 0;
        u64 updatedComponents = 0;
        u64 redundantSchedules = 0;
        bool processing = false;
        bool initialized = false;
    };

    struct TransformUpdateDispatch
    {
        u32 roots = 0;
        bool dispatched = false;
    };

    /// Runtime transform scheduler. Components own transform state and
    /// attachments own transform policy; this system only owns registration,
    /// root scheduling, and parallel update dispatch.
    class TransformSystem final
    {
    public:
        struct Impl;

        TransformSystem() noexcept = default;
        ~TransformSystem();

        TransformSystem(const TransformSystem&) = delete;
        TransformSystem& operator=(const TransformSystem&) = delete;

        [[nodiscard]] bool Initialize(const TransformSystemConfig& config = {}, TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsProcessing() const noexcept;

        [[nodiscard]] bool RegisterComponent(IPlacedComponent& component, TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterComponent(IPlacedComponent& component, TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Attach(ITransformAttachment& attachment, TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Detach(ITransformAttachment& attachment, TransformFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool DispatchUpdates(jobs::Builder& builder, TransformUpdateDispatch& dispatch,
                                           TransformFailure* failure = nullptr) noexcept;
        [[nodiscard]] TransformSystemStats GetStats() const noexcept;

    private:
        friend class IPlacedComponent;

        [[nodiscard]] bool ScheduleTransformUpdate(IPlacedComponent& component) noexcept;
        void CancelTransformUpdate(IPlacedComponent& component) noexcept;
        void RecordTransformUpdated() noexcept;
        [[nodiscard]] u32 GetMaximumHierarchyDepth() const noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities
