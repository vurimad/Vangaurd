#pragma once

#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_extension.hpp>

#include <vanguard/containers/containers.hpp>

#include <atomic>

namespace vanguard::editor::detail
{
    inline constexpr u32 InvalidEditorActivitySourceTokenIndex = ~u32{0};

    struct EditorActivitySourceToken final
    {
        u32 index = InvalidEditorActivitySourceTokenIndex;
        u64 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidEditorActivitySourceTokenIndex && generation != 0;
        }
    };

    struct EditorActivityItemDescriptor final
    {
        EditorActivityItemId id = InvalidEditorActivityItemId;
        u64 generation = 0;
        EditorActivityKind kind = EditorActivityKind::Status;
        EditorActivityState state = EditorActivityState::Idle;
        EditorActivityAttention attention = EditorActivityAttention::None;
        u64 completed = 0;
        u64 total = 0;
        bool determinate = false;
        bool cancellable = false;
        const char* label = nullptr;
        const char* detail = nullptr;
    };

    class EditorActivitySourceWriter final
    {
    public:
        [[nodiscard]] bool Add(const EditorActivityItemDescriptor& descriptor,
                               EditorActivityFailure* failure = nullptr) noexcept;

    private:
        EditorActivitySourceWriter(EditorActivitySourceId source, u64 sourceGeneration,
                                   bool cancellationSupported, u32 maximumRows) noexcept;

        EditorActivitySourceId m_source = InvalidEditorActivitySourceId;
        u64 m_sourceGeneration = 0;
        bool m_cancellationSupported = false;
        u32 m_maximumRows = MaximumEditorActivityRowsPerSource;
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRowsPerSource> m_items;
        EditorActivityFailure m_failure;

        friend class EditorActivityCore;
    };

    using EditorActivitySourceQuery = bool (*)(EditorActivitySourceWriter& writer, void* userData,
                                               EditorActivityFailure* failure) noexcept;
    using EditorActivitySourceCancel = EditorActivityCancelResult (*)(EditorActivityKey key,
                                                                      void* userData) noexcept;

    struct EditorActivitySourceDescriptor final
    {
        EditorActivitySourceId id = InvalidEditorActivitySourceId;
        const char* displayName = nullptr;
        EditorActivitySourceQuery query = nullptr;
        EditorActivitySourceCancel requestCancel = nullptr;
        void* userData = nullptr;
    };

    class EditorActivityPrivateService : public EditorActivityService
    {
    public:
        [[nodiscard]] virtual bool RegisterSourceForOwner(const EditorExtensionOwner& owner,
                                                          const EditorActivitySourceDescriptor& descriptor,
                                                          EditorActivitySourceToken& token,
                                                          EditorActivityFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterSourceForOwner(const EditorExtensionOwner& owner,
                                                            EditorActivitySourceToken token,
                                                            EditorActivityFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RevokeSourceOwner(const EditorExtensionOwner& owner,
                                                     EditorActivityFailure* failure = nullptr) noexcept = 0;

    protected:
        EditorActivityPrivateService() noexcept = default;
    };

    struct EditorActivityDiagnosticIngressRecord final
    {
        u64 serviceGeneration = 0;
        u64 sequence = 0;
        u64 frame = 0;
        u32 threadId = 0;
        u32 threadContextId = 0;
        u32 networkPeerId = 0;
        diagnostics::Level level = diagnostics::Level::Info;
        diagnostics::Category category = diagnostics::Category::Default;
        bool truncated = false;
        char message[diagnostics::MaxMessageLength]{};
    };

    struct EditorActivityDiagnosticsBridge final
    {
        containers::MPSCLockFreeQueue<EditorActivityDiagnosticIngressRecord, MaximumEditorActivityDiagnosticsIngress> ingress;
        std::atomic<u64> activeServiceGeneration{0};
        std::atomic<u64> ingressSequence{0};
        std::atomic<u64> dropped{0};
        std::atomic<u64> inactiveDiscarded{0};
        diagnostics::SinkRegistrationToken sinkToken;
        bool hasSinkToken = false;
    };

    struct EditorActivityDiagnosticsRegistrationAccess final
    {
        diagnostics::SinkRegistrationResult (*registerSink)(diagnostics::SinkCallback callback, void* userData,
                                                            diagnostics::SinkRegistrationToken& token) noexcept = nullptr;
        diagnostics::SinkRegistrationResult (*unregisterSink)(diagnostics::SinkRegistrationToken token) noexcept = nullptr;
        bool (*isSinkRegistered)(diagnostics::SinkRegistrationToken token) noexcept = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return registerSink != nullptr && unregisterSink != nullptr && isSinkRegistered != nullptr;
        }
    };

    struct EditorActivityGenerationSource final
    {
        std::atomic<u64>* serviceCounter = nullptr;
        std::atomic<u64>* sourceCounter = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return serviceCounter != nullptr;
        }

        [[nodiscard]] constexpr bool HasSourceCounter() const noexcept { return sourceCounter != nullptr; }
    };

    [[nodiscard]] EditorActivityDiagnosticsBridge& GetProductionEditorActivityDiagnosticsBridge() noexcept;
    [[nodiscard]] EditorActivityDiagnosticsRegistrationAccess GetProductionEditorActivityDiagnosticsRegistrationAccess() noexcept;
    [[nodiscard]] EditorActivityGenerationSource GetProductionEditorActivityGenerationSource() noexcept;

    class EditorActivityCore final
    {
    public:
        EditorActivityCore(EditorActivityDiagnosticsBridge& bridge, EditorActivityDiagnosticsRegistrationAccess diagnosticsAccess,
                           EditorActivityGenerationSource generations) noexcept;

        [[nodiscard]] bool ReserveServiceGeneration(EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AttachDiagnostics(EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DetachDiagnostics(EditorActivityFailure* failure = nullptr) noexcept;
        void Activate() noexcept;
        void Quiesce() noexcept;
        void Reset() noexcept;
        void Tick(const engine::FrameContext& frame) noexcept;

        [[nodiscard]] bool CaptureDiagnostics(const EditorActivityCursor& cursor,
                                              containers::ArraySpan<EditorActivityDiagnosticRecord> records,
                                              EditorActivityDiagnosticCapture& capture,
                                              EditorActivityFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CaptureActivities(const EditorActivityCursor& cursor,
                                             containers::ArraySpan<EditorActivitySourceSnapshot> sources,
                                             containers::ArraySpan<EditorActivityItem> items,
                                             EditorActivityCapture& capture,
                                             EditorActivityFailure* failure = nullptr) const noexcept;
        [[nodiscard]] EditorActivityCancelAdmission RequestCancel(EditorActivityKey key,
                                                                 EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearDiagnostics(EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] EditorActivityServiceStatus GetStatus() const noexcept;
        [[nodiscard]] bool IsFrameActive() const noexcept { return m_frameActive; }
        [[nodiscard]] bool RegisterSourceForOwner(const EditorExtensionOwner& owner,
                                                  const EditorActivitySourceDescriptor& descriptor,
                                                  EditorActivitySourceToken& token,
                                                  EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterSourceForOwner(const EditorExtensionOwner& owner,
                                                    EditorActivitySourceToken token,
                                                    EditorActivityFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RevokeSourceOwner(const EditorExtensionOwner& owner,
                                             EditorActivityFailure* failure = nullptr) noexcept;

    private:
        enum class SourceSlotState : u8
        {
            Free,
            Live,
            Revoked
        };

        struct SourceSlot final
        {
            EditorActivitySourceToken token;
            EditorExtensionOwner owner;
            EditorActivitySourceId id = InvalidEditorActivitySourceId;
            EditorActivitySourceQuery query = nullptr;
            EditorActivitySourceCancel requestCancel = nullptr;
            void* userData = nullptr;
            char displayName[MaximumEditorActivityLabelBytes]{};
            SourceSlotState state = SourceSlotState::Free;
        };

        [[nodiscard]] bool FailOperation(EditorActivityFailure* failure, EditorActivityFailureCode code,
                                         const char* message, EditorActivitySourceId source = InvalidEditorActivitySourceId,
                                         EditorActivityItemId item = InvalidEditorActivityItemId) noexcept;
        [[nodiscard]] static bool ReportReadOnlyFailure(EditorActivityFailure* failure, EditorActivityFailureCode code,
                                                        const char* message,
                                                        EditorActivitySourceId source = InvalidEditorActivitySourceId,
                                                        EditorActivityItemId item = InvalidEditorActivityItemId) noexcept;
        [[nodiscard]] bool ValidateCursor(const EditorActivityCursor& cursor, EditorActivityFailure* failure) const noexcept;
        [[nodiscard]] SourceSlot* FindLiveSource(EditorActivitySourceId source, u64 sourceGeneration) noexcept;
        [[nodiscard]] const SourceSlot* FindLiveSource(EditorActivitySourceId source, u64 sourceGeneration) const noexcept;
        [[nodiscard]] const EditorActivitySourceSnapshot* FindPublishedSource(EditorActivitySourceId source,
                                                                              u64 sourceGeneration) const noexcept;
        [[nodiscard]] const EditorActivityItem* FindPublishedItem(EditorActivityKey key) const noexcept;
        [[nodiscard]] EditorActivityItem* FindItem(containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
                                                   EditorActivityKey key) noexcept;
        void SampleSources(u64 generation,
                           containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources>& sources,
                           containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
                           u64& sourceQueryFailures, EditorActivityFailure& latestFailure) noexcept;
        void DispatchCancels(containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources>& sources,
                             containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
                             u64& cancellationFailures, EditorActivityFailure& latestFailure) noexcept;
        void RevokeAllSourcesForQuiesce() noexcept;
        void DiscardQueuedCancels(EditorActivitySourceId source, u64 sourceGeneration) noexcept;
        [[nodiscard]] bool IsCancelQueued(EditorActivityKey key) const noexcept;
        [[nodiscard]] bool AppendStaleSource(const SourceSlot& slot, const EditorActivityFailure& failure,
                                             containers::StaticArray<EditorActivitySourceSnapshot,
                                                                     MaximumEditorActivitySources>& sources,
                                             containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items) noexcept;
        void Retain(const EditorActivityDiagnosticIngressRecord& record) noexcept;
        [[nodiscard]] u32 PhysicalRetainedIndex(u32 logicalIndex) const noexcept;

        EditorActivityDiagnosticsBridge* m_bridge = nullptr;
        EditorActivityDiagnosticsRegistrationAccess m_diagnosticsAccess;
        EditorActivityGenerationSource m_generations;
        containers::DynamicArray<EditorActivityDiagnosticIngressRecord> m_drainedDiagnostics;
        containers::DynamicArray<EditorActivityDiagnosticRecord> m_retained;
        SourceSlot m_sourceSlots[MaximumEditorActivitySources]{};
        containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources> m_publishedSources;
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows> m_publishedItems;
        containers::StaticArray<EditorActivityKey, MaximumEditorActivityCancelRequests> m_pendingCancels;
        u64 m_serviceGeneration = 0;
        u64 m_activityGeneration = 0;
        u64 m_evicted = 0;
        u64 m_coalesced = 0;
        u64 m_truncated = 0;
        u64 m_sourceQueryFailures = 0;
        u64 m_cancellationFailures = 0;
        u32 m_retainedStart = 0;
        u32 m_sourceCallbackDepth = 0;
        bool m_frameActive = false;
        bool m_sourceAdmissionOpen = false;
        EditorActivityServiceState m_state = EditorActivityServiceState::Offline;
        EditorActivityFailure m_lastFailure;
        EditorActivityServiceStatus m_callbackReadStatus;
    };
} // namespace vanguard::editor::detail
