#pragma once

#include <vanguard/editor/editor_service_ids.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>

namespace vanguard::editor
{
    inline constexpr engine::FrameParticipantId EditorActivityFrameParticipantId = 0x6564616374667201ull;

    inline constexpr u32 MaximumEditorActivityDiagnosticsIngress = 512;
    inline constexpr u32 MaximumEditorActivityDiagnosticsRetained = 2048;
    inline constexpr u32 MaximumEditorActivityDiagnosticsDrainPerTick = 256;
    inline constexpr u32 MaximumEditorActivitySources = 64;
    inline constexpr u32 MaximumEditorActivityRowsPerSource = 32;
    inline constexpr u32 MaximumEditorActivityRows = 256;
    inline constexpr u32 MaximumEditorActivityCancelRequests = 32;
    inline constexpr u32 MaximumEditorActivityLabelBytes = 128;
    inline constexpr u32 MaximumEditorActivityDetailBytes = 512;

    using EditorActivitySourceId = u64;
    using EditorActivityItemId = u64;

    inline constexpr EditorActivitySourceId InvalidEditorActivitySourceId = 0;
    inline constexpr EditorActivityItemId InvalidEditorActivityItemId = 0;

    struct EditorActivityKey final
    {
        EditorActivitySourceId source = InvalidEditorActivitySourceId;
        u64 sourceGeneration = 0;
        EditorActivityItemId item = InvalidEditorActivityItemId;
        u64 itemGeneration = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return source != InvalidEditorActivitySourceId && sourceGeneration != 0 &&
                   item != InvalidEditorActivityItemId && itemGeneration != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EditorActivityKey&, const EditorActivityKey&) noexcept = default;
    };

    enum class EditorActivityKind : u8
    {
        Status,
        Progress
    };

    enum class EditorActivityState : u8
    {
        Idle,
        Pending,
        Running,
        Cancelling,
        Succeeded,
        Failed,
        Cancelled,
        Blocked
    };

    enum class EditorActivityAttention : u8
    {
        None,
        Info,
        Warning,
        Error
    };

    enum class EditorActivityCancelAdmission : u8
    {
        Queued,
        AlreadyQueued,
        InvalidState,
        WrongThread,
        ReentrantMutation,
        Stale,
        Unsupported,
        CapacityExceeded
    };

    enum class EditorActivityCancelResult : u8
    {
        None,
        Accepted,
        AlreadyRequested,
        Busy,
        Stale,
        Unsupported,
        Failed
    };

    enum class EditorActivityFailureCode : u8
    {
        None,
        NotReady,
        InvalidArgument,
        InvalidDescriptor,
        WrongThread,
        DuplicateSource,
        DuplicateItem,
        CapacityExceeded,
        GenerationExhausted,
        StaleToken,
        StaleCursor,
        StaleActivity,
        OwnerMismatch,
        ReentrantMutation,
        SourceQueryFailed,
        CancellationUnsupported,
        CancellationRejected,
        CancellationBusy,
        CancellationFailed,
        DiagnosticsSinkFailure
    };

    struct EditorActivityFailure final
    {
        EditorActivityFailureCode code = EditorActivityFailureCode::None;
        EditorActivitySourceId source = InvalidEditorActivitySourceId;
        EditorActivityItemId item = InvalidEditorActivityItemId;
        const char* message = nullptr;
    };

    struct EditorActivityCursor final
    {
        u64 serviceGeneration = 0;
        u64 diagnosticSequence = 0;
        u64 activityGeneration = 0;
    };

    struct EditorActivityDiagnosticRecord final
    {
        u64 firstSequence = 0;
        u64 lastSequence = 0;
        u64 firstFrame = 0;
        u64 lastFrame = 0;
        u32 threadId = 0;
        u32 threadContextId = 0;
        u32 networkPeerId = 0;
        diagnostics::Level level = diagnostics::Level::Info;
        diagnostics::Category category = diagnostics::Category::Default;
        u32 repeatCount = 1;
        bool truncated = false;
        char message[diagnostics::MaxMessageLength]{};
    };

    struct EditorActivityDiagnosticCapture final
    {
        EditorActivityCursor cursor;
        u64 requestedSequence = 0;
        u64 retainedFirstSequence = 0;
        u64 retainedLastSequence = 0;
        u64 copiedFirstSequence = 0;
        u64 copiedLastSequence = 0;
        u32 copiedCount = 0;
        bool gap = false;
    };

    struct EditorActivitySourceSnapshot final
    {
        EditorActivitySourceId source = InvalidEditorActivitySourceId;
        u64 sourceGeneration = 0;
        u64 lastSuccessGeneration = 0;
        u32 firstItem = 0;
        u32 itemCount = 0;
        bool stale = false;
        EditorActivityFailure failure;
        char displayName[MaximumEditorActivityLabelBytes]{};
    };

    struct EditorActivityItem final
    {
        EditorActivityKey key;
        EditorActivityKind kind = EditorActivityKind::Status;
        EditorActivityState state = EditorActivityState::Idle;
        EditorActivityAttention attention = EditorActivityAttention::None;
        u64 completed = 0;
        u64 total = 0;
        bool determinate = false;
        bool cancellable = false;
        bool cancelQueued = false;
        EditorActivityCancelResult cancelResult = EditorActivityCancelResult::None;
        char label[MaximumEditorActivityLabelBytes]{};
        char detail[MaximumEditorActivityDetailBytes]{};
    };

    struct EditorActivityCapture final
    {
        EditorActivityCursor cursor;
        u32 sourceCount = 0;
        u32 itemCount = 0;
    };

    enum class EditorActivityServiceState : u8
    {
        Offline,
        Ready,
        Quiesced
    };

    struct EditorActivityServiceStatus final
    {
        EditorActivityServiceState state = EditorActivityServiceState::Offline;
        u64 serviceGeneration = 0;
        u64 activityGeneration = 0;
        u32 liveSources = 0;
        u32 liveActivities = 0;
        u32 diagnosticsIngress = 0;
        u32 diagnosticsRetained = 0;
        u64 diagnosticsDropped = 0;
        u64 diagnosticsInactiveDiscarded = 0;
        u64 diagnosticsEvicted = 0;
        u64 diagnosticsCoalesced = 0;
        u64 diagnosticsTruncated = 0;
        u32 pendingCancellations = 0;
        u64 sourceQueryFailures = 0;
        u64 cancellationFailures = 0;
        EditorActivityFailure lastFailure;
    };

    class EditorActivityService : public application::Service
    {
    public:
        ~EditorActivityService() override = default;

        [[nodiscard]] virtual bool CaptureDiagnostics(const EditorActivityCursor& cursor,
                                                      containers::ArraySpan<EditorActivityDiagnosticRecord> records,
                                                      EditorActivityDiagnosticCapture& capture,
                                                      EditorActivityFailure* failure = nullptr) const noexcept = 0;
        [[nodiscard]] virtual bool CaptureActivities(const EditorActivityCursor& cursor,
                                                     containers::ArraySpan<EditorActivitySourceSnapshot> sources,
                                                     containers::ArraySpan<EditorActivityItem> items,
                                                     EditorActivityCapture& capture,
                                                     EditorActivityFailure* failure = nullptr) const noexcept = 0;
        [[nodiscard]] virtual EditorActivityCancelAdmission RequestCancel(EditorActivityKey key,
                                                                         EditorActivityFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ClearDiagnostics(EditorActivityFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual EditorActivityServiceStatus GetStatus() const noexcept = 0;

    protected:
        EditorActivityService() noexcept = default;
    };

    [[nodiscard]] bool RegisterEditorActivityService(application::EngineHost& host,
                                                     application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorActivityService* FindEditorActivityService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorActivityService* FindEditorActivityService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
