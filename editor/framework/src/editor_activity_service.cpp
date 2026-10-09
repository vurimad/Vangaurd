#if defined(_MSC_VER)
#pragma warning(disable : 4389)
#endif

#include <vanguard/editor/editor_activity_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstring>

namespace
{
    namespace app = vanguard::application;
    namespace diagnostics = vanguard::diagnostics;
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;
    namespace engine = vanguard::engine;

    std::atomic<vanguard::u64> g_activityServiceGeneration{0};
    std::atomic<vanguard::u64> g_activitySourceGeneration{0};
    detail::EditorActivityDiagnosticsBridge g_activityDiagnosticsBridge;

    void ClearFailure(editor::EditorActivityFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] vanguard::u64 ReserveGeneration(std::atomic<vanguard::u64>& counter) noexcept
    {
        vanguard::u64 current = counter.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u64{0})
                return 0;
            if (counter.compare_exchange_weak(current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }

    [[nodiscard]] bool CopyMessage(const char* const source, char* const destination, const vanguard::usize capacity) noexcept
    {
        if (capacity == 0)
            return source != nullptr && source[0] != '\0';
        if (source == nullptr)
        {
            destination[0] = '\0';
            return false;
        }

        vanguard::usize index = 0;
        while (index + 1u < capacity && source[index] != '\0')
        {
            destination[index] = source[index];
            ++index;
        }
        destination[index] = '\0';
        return source[index] != '\0';
    }

    [[nodiscard]] bool CopyBoundedTextExact(const char* const source, char* const destination,
                                            const vanguard::usize capacity, const bool required) noexcept
    {
        if (capacity == 0)
            return false;
        if (source == nullptr)
        {
            destination[0] = '\0';
            return !required;
        }

        vanguard::usize index = 0;
        while (index < capacity && source[index] != '\0')
            ++index;
        if (index == capacity || (required && index == 0))
            return false;
        if (index != 0)
            std::memcpy(destination, source, index);
        destination[index] = '\0';
        return true;
    }

    void ActivityDiagnosticsCallback(const diagnostics::MessageView& message, void* const userData) noexcept
    {
        auto& bridge = *static_cast<detail::EditorActivityDiagnosticsBridge*>(userData);
        const vanguard::u64 sequence = ReserveGeneration(bridge.ingressSequence);
        if (sequence == 0)
        {
            bridge.dropped.fetch_add(1u, std::memory_order_relaxed);
            return;
        }

        const vanguard::u64 serviceGeneration = bridge.activeServiceGeneration.load(std::memory_order_acquire);
        if (serviceGeneration == 0)
        {
            bridge.inactiveDiscarded.fetch_add(1u, std::memory_order_relaxed);
            return;
        }

        detail::EditorActivityDiagnosticIngressRecord record;
        record.serviceGeneration = serviceGeneration;
        record.sequence = sequence;
        record.frame = message.frame;
        record.threadId = message.threadId;
        record.threadContextId = message.threadContextId;
        record.networkPeerId = message.networkPeerId;
        record.level = message.level;
        record.category = message.category;
        record.truncated = CopyMessage(message.message, record.message, diagnostics::MaxMessageLength);
        if (!bridge.ingress.Push(record))
            bridge.dropped.fetch_add(1u, std::memory_order_relaxed);
    }

    diagnostics::SinkRegistrationResult RegisterActivitySink(const diagnostics::SinkCallback callback, void* const userData,
                                                              diagnostics::SinkRegistrationToken& token) noexcept
    {
        return diagnostics::RegisterSinkCallback(callback, userData, token);
    }

    diagnostics::SinkRegistrationResult UnregisterActivitySink(const diagnostics::SinkRegistrationToken token) noexcept
    {
        return diagnostics::UnregisterSinkCallback(token);
    }

    bool IsActivitySinkRegistered(const diagnostics::SinkRegistrationToken token) noexcept
    {
        return diagnostics::IsSinkCallbackRegistered(token);
    }

    class ManagedEditorActivityService final : public detail::EditorActivityPrivateService
    {
    public:
        ManagedEditorActivityService() noexcept
            : m_core(detail::GetProductionEditorActivityDiagnosticsBridge(),
                     detail::GetProductionEditorActivityDiagnosticsRegistrationAccess(),
                     detail::GetProductionEditorActivityGenerationSource())
        {
        }

        bool CaptureDiagnostics(const editor::EditorActivityCursor& cursor,
                                vanguard::containers::ArraySpan<editor::EditorActivityDiagnosticRecord> records,
                                editor::EditorActivityDiagnosticCapture& capture,
                                editor::EditorActivityFailure* const failure) const noexcept override
        {
            return m_core.CaptureDiagnostics(cursor, records, capture, failure);
        }

        bool CaptureActivities(const editor::EditorActivityCursor& cursor,
                               vanguard::containers::ArraySpan<editor::EditorActivitySourceSnapshot> sources,
                               vanguard::containers::ArraySpan<editor::EditorActivityItem> items,
                               editor::EditorActivityCapture& capture,
                               editor::EditorActivityFailure* const failure) const noexcept override
        {
            return m_core.CaptureActivities(cursor, sources, items, capture, failure);
        }

        editor::EditorActivityCancelAdmission RequestCancel(const editor::EditorActivityKey key,
                                                            editor::EditorActivityFailure* const failure) noexcept override
        {
            return m_core.RequestCancel(key, failure);
        }

        bool ClearDiagnostics(editor::EditorActivityFailure* const failure) noexcept override
        {
            return m_core.ClearDiagnostics(failure);
        }

        editor::EditorActivityServiceStatus GetStatus() const noexcept override
        {
            return m_core.GetStatus();
        }

        bool RegisterSourceForOwner(const editor::EditorExtensionOwner& owner,
                                    const detail::EditorActivitySourceDescriptor& descriptor,
                                    detail::EditorActivitySourceToken& token,
                                    editor::EditorActivityFailure* const failure) noexcept override
        {
            return m_core.RegisterSourceForOwner(owner, descriptor, token, failure);
        }

        bool UnregisterSourceForOwner(const editor::EditorExtensionOwner& owner,
                                      const detail::EditorActivitySourceToken token,
                                      editor::EditorActivityFailure* const failure) noexcept override
        {
            return m_core.UnregisterSourceForOwner(owner, token, failure);
        }

        bool RevokeSourceOwner(const editor::EditorExtensionOwner& owner,
                               editor::EditorActivityFailure* const failure) noexcept override
        {
            return m_core.RevokeSourceOwner(owner, failure);
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            editor::EditorActivityFailure failure;
            if (!m_core.ReserveServiceGeneration(&failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor activity generation reservation failed");

            m_framePipeline = static_cast<engine::FramePipelineService*>(context.FindCapability(engine::FramePipelineCapabilityId));
            if (m_framePipeline == nullptr)
                return app::LifecycleStatus::Failure("Frame Pipeline capability is unavailable to editor activity");

            if (!m_core.AttachDiagnostics(&failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor activity diagnostics attach failed");

            constexpr engine::FrameParticipantId dependencies[]{engine::InputFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorActivityFrameParticipantId;
            descriptor.name = "editorActivity";
            descriptor.phase = engine::FramePhase::BeginFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = &ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
            {
                static_cast<void>(m_core.DetachDiagnostics(nullptr));
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message :
                                                                                      "editor activity frame participant registration failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            m_core.Activate();
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_core.Quiesce();
            editor::EditorActivityFailure failure;
            if (!m_core.DetachDiagnostics(&failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor activity diagnostics detach failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            m_core.Quiesce();
            editor::EditorActivityFailure failure;
            if (!m_core.DetachDiagnostics(&failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor activity diagnostics detach failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_core.Quiesce();
            editor::EditorActivityFailure failure;
            const bool detached = m_core.DetachDiagnostics(&failure);
            m_core.Reset();
            return detached ? app::LifecycleStatus::Success()
                            : app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message :
                                                                                       "editor activity diagnostics detach failed");
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& frame, void* const userData) noexcept
        {
            auto& service = *static_cast<ManagedEditorActivityService*>(userData);
            if (service.m_core.IsFrameActive())
                service.m_core.Tick(frame);
            return engine::FrameParticipantStatus::Success();
        }

        detail::EditorActivityCore m_core;
        engine::FramePipelineService* m_framePipeline = nullptr;
    };

    app::Service* CreateEditorActivityService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Editor, sizeof(ManagedEditorActivityService), alignof(ManagedEditorActivityService));
        return block ? ::new (block.address) ManagedEditorActivityService() : nullptr;
    }

    void DestroyEditorActivityService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorActivityService*>(service)->~ManagedEditorActivityService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorActivityService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor::detail
{
    EditorActivityDiagnosticsBridge& GetProductionEditorActivityDiagnosticsBridge() noexcept
    {
        return g_activityDiagnosticsBridge;
    }

    EditorActivityDiagnosticsRegistrationAccess GetProductionEditorActivityDiagnosticsRegistrationAccess() noexcept
    {
        return {&RegisterActivitySink, &UnregisterActivitySink, &IsActivitySinkRegistered};
    }

    EditorActivityGenerationSource GetProductionEditorActivityGenerationSource() noexcept
    {
        return {&g_activityServiceGeneration, &g_activitySourceGeneration};
    }

    EditorActivitySourceWriter::EditorActivitySourceWriter(const EditorActivitySourceId source,
                                                           const u64 sourceGeneration,
                                                           const bool cancellationSupported,
                                                           const u32 maximumRows) noexcept
        : m_source(source), m_sourceGeneration(sourceGeneration),
          m_cancellationSupported(cancellationSupported), m_maximumRows(maximumRows)
    {
    }

    bool EditorActivitySourceWriter::Add(const EditorActivityItemDescriptor& descriptor,
                                         EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        auto reject = [&](const EditorActivityFailureCode code, const char* const message) noexcept {
            const EditorActivityFailure current{code, m_source, descriptor.id, message};
            if (m_failure.code == EditorActivityFailureCode::None)
                m_failure = current;
            if (failure != nullptr)
                *failure = current;
            return false;
        };

        if (m_failure.code != EditorActivityFailureCode::None)
        {
            if (failure != nullptr)
                *failure = m_failure;
            return false;
        }
        if (descriptor.id == InvalidEditorActivityItemId || descriptor.generation == 0)
            return reject(EditorActivityFailureCode::InvalidDescriptor,
                          "editor activity source item identity is invalid");
        if (static_cast<u8>(descriptor.kind) > static_cast<u8>(EditorActivityKind::Progress) ||
            static_cast<u8>(descriptor.state) > static_cast<u8>(EditorActivityState::Blocked) ||
            static_cast<u8>(descriptor.attention) > static_cast<u8>(EditorActivityAttention::Error))
            return reject(EditorActivityFailureCode::InvalidDescriptor,
                          "editor activity source item presentation enum is invalid");
        if ((descriptor.kind == EditorActivityKind::Status &&
             (descriptor.determinate || descriptor.completed != 0 || descriptor.total != 0)) ||
            (descriptor.determinate && (descriptor.total == 0 || descriptor.completed > descriptor.total)) ||
            (!descriptor.determinate && (descriptor.completed != 0 || descriptor.total != 0)))
            return reject(EditorActivityFailureCode::InvalidDescriptor,
                          "editor activity source item progress values are invalid");
        if (descriptor.cancellable && !m_cancellationSupported)
            return reject(EditorActivityFailureCode::InvalidDescriptor,
                          "editor activity source item cannot be cancellable without a source cancel callback");
        for (const EditorActivityItem& existing : m_items)
            if (existing.key.item == descriptor.id)
                return reject(EditorActivityFailureCode::DuplicateItem,
                              "editor activity source snapshot contains a duplicate item");
        if (m_items.Size() >= m_maximumRows)
            return reject(EditorActivityFailureCode::CapacityExceeded,
                          "editor activity source or total row capacity is exhausted");

        EditorActivityItem item;
        item.key = {m_source, m_sourceGeneration, descriptor.id, descriptor.generation};
        item.kind = descriptor.kind;
        item.state = descriptor.state;
        item.attention = descriptor.attention;
        item.completed = descriptor.completed;
        item.total = descriptor.total;
        item.determinate = descriptor.determinate;
        item.cancellable = descriptor.cancellable;
        if (!CopyBoundedTextExact(descriptor.label, item.label, MaximumEditorActivityLabelBytes, true) ||
            !CopyBoundedTextExact(descriptor.detail, item.detail, MaximumEditorActivityDetailBytes, false))
            return reject(EditorActivityFailureCode::InvalidDescriptor,
                          "editor activity source item text exceeds the bounded copy capacity");

        m_items.PushBack(item);
        return true;
    }

    EditorActivityCore::EditorActivityCore(EditorActivityDiagnosticsBridge& bridge,
                                           const EditorActivityDiagnosticsRegistrationAccess diagnosticsAccess,
                                           const EditorActivityGenerationSource generations) noexcept
        : m_bridge(&bridge), m_diagnosticsAccess(diagnosticsAccess), m_generations(generations),
          m_drainedDiagnostics(memory::pools::Editor::GetInstance()),
          m_retained(memory::pools::Editor::GetInstance())
    {
        m_drainedDiagnostics.Reserve(MaximumEditorActivityDiagnosticsDrainPerTick);
        m_retained.Reserve(MaximumEditorActivityDiagnosticsRetained);
    }

    bool EditorActivityCore::ReserveServiceGeneration(EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread, "editor activity service generation must be reserved on the main thread");
        if (!m_generations.IsValid() || m_serviceGeneration != 0)
            return FailOperation(failure, EditorActivityFailureCode::InvalidArgument, "editor activity generation source is invalid or already consumed");
        m_serviceGeneration = ReserveGeneration(*m_generations.serviceCounter);
        if (m_serviceGeneration == 0)
            return FailOperation(failure, EditorActivityFailureCode::GenerationExhausted, "editor activity service generation space is exhausted");
        return true;
    }

    bool EditorActivityCore::AttachDiagnostics(EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread, "editor activity diagnostics attach must run on the main thread");
        if (m_serviceGeneration == 0 || m_bridge == nullptr || !m_diagnosticsAccess.IsValid())
            return FailOperation(failure, EditorActivityFailureCode::InvalidArgument, "editor activity diagnostics attachment state is invalid");

        if (m_bridge->hasSinkToken)
        {
            if (m_diagnosticsAccess.isSinkRegistered(m_bridge->sinkToken))
                return true;
            m_bridge->sinkToken = {};
            m_bridge->hasSinkToken = false;
        }

        diagnostics::SinkRegistrationToken token;
        const diagnostics::SinkRegistrationResult result =
            m_diagnosticsAccess.registerSink(&ActivityDiagnosticsCallback, m_bridge, token);
        if (result != diagnostics::SinkRegistrationResult::Success)
            return FailOperation(failure, EditorActivityFailureCode::DiagnosticsSinkFailure, "editor activity diagnostics sink registration failed");
        m_bridge->sinkToken = token;
        m_bridge->hasSinkToken = true;
        return true;
    }

    bool EditorActivityCore::DetachDiagnostics(EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread, "editor activity diagnostics detach must run on the main thread");
        if (m_bridge == nullptr || !m_diagnosticsAccess.IsValid())
            return FailOperation(failure, EditorActivityFailureCode::InvalidArgument, "editor activity diagnostics attachment state is invalid");
        if (!m_bridge->hasSinkToken)
            return true;

        const diagnostics::SinkRegistrationResult result = m_diagnosticsAccess.unregisterSink(m_bridge->sinkToken);
        if (result == diagnostics::SinkRegistrationResult::Success || result == diagnostics::SinkRegistrationResult::StaleToken)
        {
            m_bridge->sinkToken = {};
            m_bridge->hasSinkToken = false;
            return true;
        }
        return FailOperation(failure, EditorActivityFailureCode::DiagnosticsSinkFailure, "editor activity diagnostics sink unregister failed");
    }

    void EditorActivityCore::Activate() noexcept
    {
        if (m_bridge == nullptr || m_serviceGeneration == 0)
            return;
        m_bridge->activeServiceGeneration.store(m_serviceGeneration, std::memory_order_release);
        m_sourceAdmissionOpen = true;
        m_frameActive = true;
        m_state = EditorActivityServiceState::Ready;
    }

    void EditorActivityCore::Quiesce() noexcept
    {
        m_sourceAdmissionOpen = false;
        m_frameActive = false;
        if (m_bridge != nullptr)
            m_bridge->activeServiceGeneration.store(0, std::memory_order_release);
        RevokeAllSourcesForQuiesce();
        m_pendingCancels.Clear();
        if (m_state != EditorActivityServiceState::Offline)
            m_state = EditorActivityServiceState::Quiesced;
    }

    void EditorActivityCore::Reset() noexcept
    {
        Quiesce();
        m_retained.Clear();
        m_retainedStart = 0;
        m_drainedDiagnostics.Clear();
        m_publishedSources.Clear();
        m_publishedItems.Clear();
        m_pendingCancels.Clear();
        for (SourceSlot& slot : m_sourceSlots)
            slot = {};
        m_activityGeneration = 0;
        m_evicted = 0;
        m_coalesced = 0;
        m_truncated = 0;
        m_sourceQueryFailures = 0;
        m_cancellationFailures = 0;
        m_sourceCallbackDepth = 0;
        m_sourceAdmissionOpen = false;
        m_lastFailure = {};
        m_state = EditorActivityServiceState::Offline;
    }

    void EditorActivityCore::Tick(const engine::FrameContext&) noexcept
    {
        if (!m_frameActive || m_state != EditorActivityServiceState::Ready || m_bridge == nullptr)
            return;

        if (m_activityGeneration == ~u64{0})
        {
            m_sourceAdmissionOpen = false;
            m_pendingCancels.Clear();
            static_cast<void>(FailOperation(nullptr, EditorActivityFailureCode::GenerationExhausted,
                                            "editor activity publication generation space is exhausted"));
            return;
        }

        const u64 nextGeneration = m_activityGeneration + 1u;
        containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources> nextSources;
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows> nextItems;
        u64 nextSourceQueryFailures = m_sourceQueryFailures;
        u64 nextCancellationFailures = m_cancellationFailures;
        EditorActivityFailure nextLastFailure = m_lastFailure;
        m_callbackReadStatus = GetStatus();

        m_drainedDiagnostics.Clear();
        EditorActivityDiagnosticIngressRecord record;
        while (m_drainedDiagnostics.Size() < MaximumEditorActivityDiagnosticsDrainPerTick &&
               m_bridge->ingress.Pop(record))
            m_drainedDiagnostics.EmplaceBack(record);

        SampleSources(nextGeneration, nextSources, nextItems, nextSourceQueryFailures, nextLastFailure);
        DispatchCancels(nextSources, nextItems, nextCancellationFailures, nextLastFailure);

        for (const EditorActivityDiagnosticIngressRecord& drained : m_drainedDiagnostics)
            if (drained.serviceGeneration == m_serviceGeneration)
                Retain(drained);
        m_drainedDiagnostics.Clear();

        m_publishedSources = nextSources;
        m_publishedItems = nextItems;
        m_sourceQueryFailures = nextSourceQueryFailures;
        m_cancellationFailures = nextCancellationFailures;
        m_lastFailure = nextLastFailure;
        m_activityGeneration = nextGeneration;
    }

    bool EditorActivityCore::CaptureDiagnostics(const EditorActivityCursor& cursor,
                                                containers::ArraySpan<EditorActivityDiagnosticRecord> records,
                                                EditorActivityDiagnosticCapture& capture,
                                                EditorActivityFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        capture = {};
        if (!concurrency::IsMainThread())
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::WrongThread,
                                         "editor activity diagnostics capture must run on the main thread");
        if (m_state != EditorActivityServiceState::Ready)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::NotReady,
                                         "editor activity service is not ready");
        if (!ValidateCursor(cursor, failure))
            return false;

        capture.cursor.serviceGeneration = m_serviceGeneration;
        capture.cursor.diagnosticSequence = cursor.diagnosticSequence;
        capture.cursor.activityGeneration = m_activityGeneration;
        if (m_retained.Empty())
            return true;

        const EditorActivityDiagnosticRecord& first = m_retained[PhysicalRetainedIndex(0)];
        const EditorActivityDiagnosticRecord& last = m_retained[PhysicalRetainedIndex(m_retained.Size() - 1u)];
        capture.retainedFirstSequence = first.firstSequence;
        capture.retainedLastSequence = last.lastSequence;
        if (cursor.diagnosticSequence == ~u64{0})
        {
            capture.requestedSequence = cursor.diagnosticSequence;
            return true;
        }
        capture.requestedSequence = cursor.diagnosticSequence == 0 ? first.firstSequence : cursor.diagnosticSequence + 1u;
        u64 wanted = capture.requestedSequence;
        if (wanted < first.firstSequence)
        {
            capture.gap = true;
            wanted = first.firstSequence;
        }
        if (wanted > last.lastSequence || records.Size() == 0)
            return true;

        u64 expected = wanted;
        for (u32 logical = 0; logical < m_retained.Size() && capture.copiedCount < records.Size(); ++logical)
        {
            const EditorActivityDiagnosticRecord& retained = m_retained[PhysicalRetainedIndex(logical)];
            if (retained.lastSequence < expected)
                continue;
            if (retained.firstSequence > expected)
                capture.gap = true;
            records[capture.copiedCount++] = retained;
            if (capture.copiedFirstSequence == 0)
                capture.copiedFirstSequence = retained.firstSequence;
            capture.copiedLastSequence = retained.lastSequence;
            expected = retained.lastSequence == ~u64{0} ? retained.lastSequence : retained.lastSequence + 1u;
        }
        if (capture.copiedCount != 0)
            capture.cursor.diagnosticSequence = capture.copiedLastSequence;
        return true;
    }

    bool EditorActivityCore::CaptureActivities(const EditorActivityCursor& cursor,
                                               containers::ArraySpan<EditorActivitySourceSnapshot> sources,
                                               containers::ArraySpan<EditorActivityItem> items,
                                               EditorActivityCapture& capture,
                                               EditorActivityFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::WrongThread,
                                         "editor activity capture must run on the main thread");
        if (m_state != EditorActivityServiceState::Ready)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::NotReady,
                                         "editor activity service is not ready");
        if (!ValidateCursor(cursor, failure))
            return false;
        if (sources.Size() < m_publishedSources.Size() || items.Size() < m_publishedItems.Size())
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::CapacityExceeded,
                                         "editor activity capture storage is smaller than the published snapshot");

        for (u32 index = 0; index < m_publishedSources.Size(); ++index)
            sources[index] = m_publishedSources[index];
        for (u32 index = 0; index < m_publishedItems.Size(); ++index)
            items[index] = m_publishedItems[index];

        EditorActivityCapture next;
        next.cursor = {m_serviceGeneration, cursor.diagnosticSequence, m_activityGeneration};
        next.sourceCount = m_publishedSources.Size();
        next.itemCount = m_publishedItems.Size();
        capture = next;
        return true;
    }

    EditorActivityCancelAdmission EditorActivityCore::RequestCancel(const EditorActivityKey key,
                                                                    EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::WrongThread, "editor activity cancellation admission must run on the main thread", key.source, key.item));
            return EditorActivityCancelAdmission::WrongThread;
        }
        if (m_state != EditorActivityServiceState::Ready)
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::NotReady, "editor activity service is not ready", key.source, key.item));
            return EditorActivityCancelAdmission::InvalidState;
        }
        if (m_sourceCallbackDepth != 0)
        {
            static_cast<void>(ReportReadOnlyFailure(failure, EditorActivityFailureCode::ReentrantMutation,
                                                    "editor activity mutation is unavailable from a source callback", key.source, key.item));
            return EditorActivityCancelAdmission::ReentrantMutation;
        }
        if (!key.IsValid())
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::StaleActivity, "editor activity cancellation key is invalid", key.source, key.item));
            return EditorActivityCancelAdmission::Stale;
        }
        const EditorActivitySourceSnapshot* const source = FindPublishedSource(key.source, key.sourceGeneration);
        const EditorActivityItem* const item = FindPublishedItem(key);
        SourceSlot* const live = FindLiveSource(key.source, key.sourceGeneration);
        if (source == nullptr || source->stale || item == nullptr || live == nullptr)
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::StaleActivity,
                                            "editor activity cancellation key is stale", key.source, key.item));
            return EditorActivityCancelAdmission::Stale;
        }
        if (!item->cancellable || live->requestCancel == nullptr)
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::CancellationUnsupported,
                                            "editor activity item does not expose cancellation", key.source, key.item));
            return EditorActivityCancelAdmission::Unsupported;
        }
        if (item->cancelQueued || IsCancelQueued(key))
            return EditorActivityCancelAdmission::AlreadyQueued;
        if (m_pendingCancels.Size() >= MaximumEditorActivityCancelRequests)
        {
            static_cast<void>(FailOperation(failure, EditorActivityFailureCode::CapacityExceeded,
                                            "editor activity cancellation queue capacity is exhausted", key.source, key.item));
            return EditorActivityCancelAdmission::CapacityExceeded;
        }
        m_pendingCancels.PushBack(key);
        return EditorActivityCancelAdmission::Queued;
    }

    bool EditorActivityCore::ClearDiagnostics(EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread, "editor activity diagnostics clear must run on the main thread");
        if (m_state != EditorActivityServiceState::Ready)
            return FailOperation(failure, EditorActivityFailureCode::NotReady, "editor activity service is not ready");
        if (m_sourceCallbackDepth != 0)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::ReentrantMutation,
                                         "editor activity mutation is unavailable from a source callback");
        m_retained.Clear();
        m_retainedStart = 0;
        m_evicted = 0;
        m_coalesced = 0;
        m_truncated = 0;
        return true;
    }

    EditorActivityServiceStatus EditorActivityCore::GetStatus() const noexcept
    {
        if (m_sourceCallbackDepth != 0)
            return m_callbackReadStatus;
        EditorActivityServiceStatus status;
        status.state = m_state;
        status.serviceGeneration = m_serviceGeneration;
        status.activityGeneration = m_activityGeneration;
        for (const SourceSlot& slot : m_sourceSlots)
            if (slot.state == SourceSlotState::Live)
                ++status.liveSources;
        status.liveActivities = m_publishedItems.Size();
        status.diagnosticsRetained = m_retained.Size();
        status.diagnosticsEvicted = m_evicted;
        status.diagnosticsCoalesced = m_coalesced;
        status.diagnosticsTruncated = m_truncated;
        status.pendingCancellations = m_pendingCancels.Size();
        status.sourceQueryFailures = m_sourceQueryFailures;
        status.cancellationFailures = m_cancellationFailures;
        status.lastFailure = m_lastFailure;
        if (m_bridge != nullptr)
        {
            status.diagnosticsIngress = m_bridge->ingress.Size();
            status.diagnosticsDropped = m_bridge->dropped.load(std::memory_order_relaxed);
            status.diagnosticsInactiveDiscarded = m_bridge->inactiveDiscarded.load(std::memory_order_relaxed);
        }
        return status;
    }

    bool EditorActivityCore::RegisterSourceForOwner(const EditorExtensionOwner& owner,
                                                    const EditorActivitySourceDescriptor& descriptor,
                                                    EditorActivitySourceToken& token,
                                                    EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread,
                                 "editor activity source registration must run on the main thread", descriptor.id);
        if (m_sourceCallbackDepth != 0)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::ReentrantMutation,
                                         "editor activity source registration is unavailable from a source callback",
                                         descriptor.id);
        if (m_state != EditorActivityServiceState::Ready || !m_sourceAdmissionOpen)
            return FailOperation(failure, EditorActivityFailureCode::NotReady,
                                 "editor activity source registration is not open", descriptor.id);
        if (!owner.IsValid() || descriptor.id == InvalidEditorActivitySourceId || descriptor.query == nullptr)
            return FailOperation(failure, EditorActivityFailureCode::InvalidDescriptor,
                                 "editor activity source descriptor is invalid", descriptor.id);

        char displayName[MaximumEditorActivityLabelBytes]{};
        if (!CopyBoundedTextExact(descriptor.displayName, displayName, MaximumEditorActivityLabelBytes, true))
            return FailOperation(failure, EditorActivityFailureCode::InvalidDescriptor,
                                 "editor activity source display name exceeds the bounded copy capacity", descriptor.id);

        SourceSlot* freeSlot = nullptr;
        u32 freeIndex = 0;
        for (u32 index = 0; index < MaximumEditorActivitySources; ++index)
        {
            SourceSlot& slot = m_sourceSlots[index];
            if (slot.state == SourceSlotState::Live && slot.id == descriptor.id)
                return FailOperation(failure, EditorActivityFailureCode::DuplicateSource,
                                     "editor activity source identity is already registered", descriptor.id);
            if (freeSlot == nullptr && slot.state == SourceSlotState::Free)
            {
                freeSlot = &slot;
                freeIndex = index;
            }
        }
        if (freeSlot == nullptr)
            return FailOperation(failure, EditorActivityFailureCode::CapacityExceeded,
                                 "editor activity source capacity is exhausted", descriptor.id);
        if (!m_generations.HasSourceCounter())
            return FailOperation(failure, EditorActivityFailureCode::InvalidArgument,
                                 "editor activity source generation state is unavailable", descriptor.id);

        const u64 generation = ReserveGeneration(*m_generations.sourceCounter);
        if (generation == 0)
            return FailOperation(failure, EditorActivityFailureCode::GenerationExhausted,
                                 "editor activity source generation space is exhausted", descriptor.id);

        SourceSlot next;
        next.token = {freeIndex, generation};
        next.owner = owner;
        next.id = descriptor.id;
        next.query = descriptor.query;
        next.requestCancel = descriptor.requestCancel;
        next.userData = descriptor.userData;
        std::memcpy(next.displayName, displayName, sizeof(next.displayName));
        next.state = SourceSlotState::Live;
        *freeSlot = next;
        token = next.token;
        return true;
    }

    bool EditorActivityCore::UnregisterSourceForOwner(const EditorExtensionOwner& owner,
                                                      const EditorActivitySourceToken token,
                                                      EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread,
                                 "editor activity source removal must run on the main thread");
        if (m_sourceCallbackDepth != 0)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::ReentrantMutation,
                                         "editor activity source removal is unavailable from a source callback");
        if (!token.IsValid() || token.index >= MaximumEditorActivitySources)
            return FailOperation(failure, EditorActivityFailureCode::StaleToken,
                                 "editor activity source token is stale");

        SourceSlot& slot = m_sourceSlots[token.index];
        if (slot.state == SourceSlotState::Free || slot.token.generation != token.generation)
            return FailOperation(failure, EditorActivityFailureCode::StaleToken,
                                 "editor activity source token is stale");
        if (!(slot.owner == owner))
            return FailOperation(failure, EditorActivityFailureCode::OwnerMismatch,
                                 "editor activity source owner does not match the token", slot.id);

        DiscardQueuedCancels(slot.id, slot.token.generation);
        slot = {};
        return true;
    }

    bool EditorActivityCore::RevokeSourceOwner(const EditorExtensionOwner& owner,
                                               EditorActivityFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailOperation(failure, EditorActivityFailureCode::WrongThread,
                                 "editor activity source revoke must run on the main thread");
        if (m_sourceCallbackDepth != 0)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::ReentrantMutation,
                                         "editor activity source revoke is unavailable from a source callback");
        if (!owner.IsValid())
            return FailOperation(failure, EditorActivityFailureCode::InvalidArgument,
                                 "editor activity source owner is invalid");

        for (SourceSlot& slot : m_sourceSlots)
        {
            if (slot.state != SourceSlotState::Live || !(slot.owner == owner))
                continue;
            DiscardQueuedCancels(slot.id, slot.token.generation);
            slot.query = nullptr;
            slot.requestCancel = nullptr;
            slot.userData = nullptr;
            slot.state = SourceSlotState::Revoked;
        }
        return true;
    }

    bool EditorActivityCore::FailOperation(EditorActivityFailure* const failure, const EditorActivityFailureCode code,
                                           const char* const message, const EditorActivitySourceId source,
                                           const EditorActivityItemId item) noexcept
    {
        const EditorActivityFailure current{code, source, item, message};
        if (code != EditorActivityFailureCode::WrongThread)
            m_lastFailure = current;
        if (failure != nullptr)
            *failure = current;
        return false;
    }

    bool EditorActivityCore::ReportReadOnlyFailure(EditorActivityFailure* const failure,
                                                   const EditorActivityFailureCode code,
                                                   const char* const message,
                                                   const EditorActivitySourceId source,
                                                   const EditorActivityItemId item) noexcept
    {
        if (failure != nullptr)
            *failure = {code, source, item, message};
        return false;
    }

    bool EditorActivityCore::ValidateCursor(const EditorActivityCursor& cursor, EditorActivityFailure* const failure) const noexcept
    {
        if (cursor.serviceGeneration == 0 && (cursor.diagnosticSequence != 0 || cursor.activityGeneration != 0))
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::StaleCursor,
                                         "editor activity cursor without a service generation must be completely zero");
        if (cursor.serviceGeneration != 0 && cursor.serviceGeneration != m_serviceGeneration)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::StaleCursor,
                                         "editor activity cursor belongs to another service instance");
        if (cursor.activityGeneration > m_activityGeneration)
            return ReportReadOnlyFailure(failure, EditorActivityFailureCode::StaleCursor,
                                         "editor activity cursor is ahead of the published generation");
        return true;
    }

    EditorActivityCore::SourceSlot* EditorActivityCore::FindLiveSource(const EditorActivitySourceId source,
                                                                      const u64 sourceGeneration) noexcept
    {
        for (SourceSlot& slot : m_sourceSlots)
            if (slot.state == SourceSlotState::Live && slot.id == source &&
                slot.token.generation == sourceGeneration)
                return &slot;
        return nullptr;
    }

    const EditorActivityCore::SourceSlot* EditorActivityCore::FindLiveSource(const EditorActivitySourceId source,
                                                                            const u64 sourceGeneration) const noexcept
    {
        for (const SourceSlot& slot : m_sourceSlots)
            if (slot.state == SourceSlotState::Live && slot.id == source &&
                slot.token.generation == sourceGeneration)
                return &slot;
        return nullptr;
    }

    const EditorActivitySourceSnapshot* EditorActivityCore::FindPublishedSource(const EditorActivitySourceId source,
                                                                                const u64 sourceGeneration) const noexcept
    {
        for (const EditorActivitySourceSnapshot& snapshot : m_publishedSources)
            if (snapshot.source == source && snapshot.sourceGeneration == sourceGeneration)
                return &snapshot;
        return nullptr;
    }

    const EditorActivityItem* EditorActivityCore::FindPublishedItem(const EditorActivityKey key) const noexcept
    {
        for (const EditorActivityItem& item : m_publishedItems)
            if (item.key == key)
                return &item;
        return nullptr;
    }

    EditorActivityItem* EditorActivityCore::FindItem(
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
        const EditorActivityKey key) noexcept
    {
        for (EditorActivityItem& item : items)
            if (item.key == key)
                return &item;
        return nullptr;
    }

    bool EditorActivityCore::AppendStaleSource(
        const SourceSlot& slot, const EditorActivityFailure& failure,
        containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources>& sources,
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items) noexcept
    {
        if (sources.Size() >= MaximumEditorActivitySources)
            return false;

        const EditorActivitySourceSnapshot* const previous =
            FindPublishedSource(slot.id, slot.token.generation);
        const u32 previousCount = previous != nullptr ? previous->itemCount : 0;
        if (items.Size() + previousCount > MaximumEditorActivityRows)
            return false;

        EditorActivitySourceSnapshot snapshot;
        snapshot.source = slot.id;
        snapshot.sourceGeneration = slot.token.generation;
        snapshot.lastSuccessGeneration = previous != nullptr ? previous->lastSuccessGeneration : 0;
        snapshot.firstItem = items.Size();
        snapshot.itemCount = previousCount;
        snapshot.stale = true;
        snapshot.failure = failure;
        std::memcpy(snapshot.displayName, slot.displayName, sizeof(snapshot.displayName));

        if (previous != nullptr)
        {
            if (previous->firstItem + previous->itemCount > m_publishedItems.Size())
                return false;
            for (u32 index = 0; index < previous->itemCount; ++index)
            {
                EditorActivityItem item = m_publishedItems[previous->firstItem + index];
                item.cancellable = false;
                item.cancelQueued = false;
                items.PushBack(item);
            }
        }
        sources.PushBack(snapshot);
        return true;
    }

    void EditorActivityCore::SampleSources(
        const u64 generation,
        containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources>& sources,
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
        u64& sourceQueryFailures, EditorActivityFailure& latestFailure) noexcept
    {
        for (u32 slotIndex = 0; slotIndex < MaximumEditorActivitySources; ++slotIndex)
        {
            SourceSlot& slot = m_sourceSlots[slotIndex];
            if (slot.state != SourceSlotState::Live || slot.query == nullptr)
                continue;

            u32 reservedPreviousRows = 0;
            for (u32 laterIndex = slotIndex + 1u; laterIndex < MaximumEditorActivitySources; ++laterIndex)
            {
                const SourceSlot& later = m_sourceSlots[laterIndex];
                if (later.state != SourceSlotState::Live)
                    continue;
                const EditorActivitySourceSnapshot* const previous =
                    FindPublishedSource(later.id, later.token.generation);
                if (previous != nullptr)
                    reservedPreviousRows += previous->itemCount;
            }
            const u32 remainingRows = items.Size() + reservedPreviousRows < MaximumEditorActivityRows
                ? MaximumEditorActivityRows - items.Size() - reservedPreviousRows
                : 0;
            const u32 writerMaximumRows = remainingRows < MaximumEditorActivityRowsPerSource
                ? remainingRows
                : MaximumEditorActivityRowsPerSource;

            EditorActivitySourceWriter writer(slot.id, slot.token.generation,
                                              slot.requestCancel != nullptr, writerMaximumRows);
            EditorActivityFailure queryFailure;
            ++m_sourceCallbackDepth;
            const bool queried = slot.query(writer, slot.userData, &queryFailure);
            --m_sourceCallbackDepth;

            const bool valid = queried && writer.m_failure.code == EditorActivityFailureCode::None &&
                               queryFailure.code == EditorActivityFailureCode::None;
            EditorActivityFailure sourceFailure;
            if (!valid)
            {
                if (writer.m_failure.code != EditorActivityFailureCode::None)
                    sourceFailure = writer.m_failure;
                else
                {
                    sourceFailure.code = queryFailure.code != EditorActivityFailureCode::None
                        ? queryFailure.code
                        : EditorActivityFailureCode::SourceQueryFailed;
                    sourceFailure.source = slot.id;
                    sourceFailure.item = queryFailure.item;
                    sourceFailure.message = "editor activity source query failed";
                }
            }

            const bool publishFresh = valid;

            if (!publishFresh)
            {
                if (sourceQueryFailures != ~u64{0})
                    ++sourceQueryFailures;
                latestFailure = sourceFailure;
                if (!AppendStaleSource(slot, sourceFailure, sources, items))
                {
                    EditorActivitySourceSnapshot snapshot;
                    snapshot.source = slot.id;
                    snapshot.sourceGeneration = slot.token.generation;
                    const EditorActivitySourceSnapshot* const previous =
                        FindPublishedSource(slot.id, slot.token.generation);
                    snapshot.lastSuccessGeneration = previous != nullptr
                        ? previous->lastSuccessGeneration
                        : 0;
                    snapshot.firstItem = items.Size();
                    snapshot.itemCount = 0;
                    snapshot.stale = true;
                    snapshot.failure = {EditorActivityFailureCode::CapacityExceeded, slot.id,
                                        InvalidEditorActivityItemId,
                                        "editor activity stale snapshot could not retain prior rows within the total bound"};
                    std::memcpy(snapshot.displayName, slot.displayName,
                                sizeof(snapshot.displayName));
                    if (sources.Size() < MaximumEditorActivitySources)
                        sources.PushBack(snapshot);
                    latestFailure = snapshot.failure;
                }
                continue;
            }

            EditorActivitySourceSnapshot snapshot;
            snapshot.source = slot.id;
            snapshot.sourceGeneration = slot.token.generation;
            snapshot.lastSuccessGeneration = generation;
            snapshot.firstItem = items.Size();
            snapshot.itemCount = writer.m_items.Size();
            snapshot.stale = false;
            std::memcpy(snapshot.displayName, slot.displayName, sizeof(snapshot.displayName));

            for (EditorActivityItem item : writer.m_items)
            {
                if (const EditorActivityItem* const previous = FindPublishedItem(item.key);
                    previous != nullptr)
                {
                    item.cancelQueued = previous->cancelQueued;
                    item.cancelResult = previous->cancelResult;
                }
                if (IsCancelQueued(item.key))
                    item.cancelQueued = true;
                items.PushBack(item);
            }
            sources.PushBack(snapshot);
        }
    }

    void EditorActivityCore::DispatchCancels(
        containers::StaticArray<EditorActivitySourceSnapshot, MaximumEditorActivitySources>& sources,
        containers::StaticArray<EditorActivityItem, MaximumEditorActivityRows>& items,
        u64& cancellationFailures, EditorActivityFailure& latestFailure) noexcept
    {
        const auto requests = m_pendingCancels;
        for (const EditorActivityKey key : requests)
        {
            EditorActivitySourceSnapshot* source = nullptr;
            for (EditorActivitySourceSnapshot& candidate : sources)
            {
                if (candidate.source == key.source &&
                    candidate.sourceGeneration == key.sourceGeneration)
                {
                    source = &candidate;
                    break;
                }
            }

            EditorActivityItem* const item = FindItem(items, key);
            SourceSlot* const live = FindLiveSource(key.source, key.sourceGeneration);
            EditorActivityCancelResult result = EditorActivityCancelResult::Stale;
            if (source != nullptr && !source->stale && item != nullptr && live != nullptr)
            {
                if (!item->cancellable || live->requestCancel == nullptr)
                    result = EditorActivityCancelResult::Unsupported;
                else
                {
                    ++m_sourceCallbackDepth;
                    result = live->requestCancel(key, live->userData);
                    --m_sourceCallbackDepth;
                    if (static_cast<u8>(result) >
                        static_cast<u8>(EditorActivityCancelResult::Failed))
                        result = EditorActivityCancelResult::Failed;
                }
            }

            if (item != nullptr)
            {
                item->cancelQueued = result == EditorActivityCancelResult::Accepted ||
                                     result == EditorActivityCancelResult::AlreadyRequested;
                item->cancelResult = result;
            }

            EditorActivityFailure dispatchFailure;
            switch (result)
            {
            case EditorActivityCancelResult::Accepted:
            case EditorActivityCancelResult::AlreadyRequested:
                break;
            case EditorActivityCancelResult::Busy:
                dispatchFailure = {EditorActivityFailureCode::CancellationBusy, key.source,
                                   key.item, "editor activity cancellation owner is busy"};
                break;
            case EditorActivityCancelResult::Unsupported:
                dispatchFailure = {EditorActivityFailureCode::CancellationUnsupported,
                                   key.source, key.item,
                                   "editor activity cancellation is unsupported"};
                break;
            case EditorActivityCancelResult::Stale:
                dispatchFailure = {EditorActivityFailureCode::StaleActivity, key.source,
                                   key.item, "editor activity cancellation key became stale"};
                break;
            case EditorActivityCancelResult::Failed:
            case EditorActivityCancelResult::None:
                dispatchFailure = {EditorActivityFailureCode::CancellationFailed, key.source,
                                   key.item, "editor activity cancellation request failed"};
                break;
            }
            if (dispatchFailure.code != EditorActivityFailureCode::None)
            {
                if (cancellationFailures != ~u64{0})
                    ++cancellationFailures;
                latestFailure = dispatchFailure;
            }
        }
        m_pendingCancels.Clear();
    }

    void EditorActivityCore::RevokeAllSourcesForQuiesce() noexcept
    {
        for (SourceSlot& slot : m_sourceSlots)
        {
            if (slot.state != SourceSlotState::Live)
                continue;
            slot.query = nullptr;
            slot.requestCancel = nullptr;
            slot.userData = nullptr;
            slot.state = SourceSlotState::Revoked;
        }
    }

    void EditorActivityCore::DiscardQueuedCancels(const EditorActivitySourceId source,
                                                  const u64 sourceGeneration) noexcept
    {
        containers::StaticArray<EditorActivityKey, MaximumEditorActivityCancelRequests> kept;
        for (const EditorActivityKey key : m_pendingCancels)
            if (key.source != source || key.sourceGeneration != sourceGeneration)
                kept.PushBack(key);
        m_pendingCancels = kept;
    }

    bool EditorActivityCore::IsCancelQueued(const EditorActivityKey key) const noexcept
    {
        for (const EditorActivityKey queued : m_pendingCancels)
            if (queued == key)
                return true;
        return false;
    }

    void EditorActivityCore::Retain(const EditorActivityDiagnosticIngressRecord& record) noexcept
    {
        if (!m_retained.Empty())
        {
            EditorActivityDiagnosticRecord& last = m_retained[PhysicalRetainedIndex(m_retained.Size() - 1u)];
            const bool same = last.lastSequence != ~u64{0} && record.sequence == last.lastSequence + 1u &&
                              last.threadId == record.threadId && last.threadContextId == record.threadContextId &&
                              last.networkPeerId == record.networkPeerId && last.level == record.level &&
                              last.category == record.category && last.truncated == record.truncated &&
                              std::strcmp(last.message, record.message) == 0;
            if (same)
            {
                last.lastSequence = record.sequence;
                last.lastFrame = record.frame;
                if (last.repeatCount != ~u32{0})
                    ++last.repeatCount;
                ++m_coalesced;
                return;
            }
        }

        EditorActivityDiagnosticRecord retained;
        retained.firstSequence = record.sequence;
        retained.lastSequence = record.sequence;
        retained.firstFrame = record.frame;
        retained.lastFrame = record.frame;
        retained.threadId = record.threadId;
        retained.threadContextId = record.threadContextId;
        retained.networkPeerId = record.networkPeerId;
        retained.level = record.level;
        retained.category = record.category;
        retained.truncated = record.truncated;
        std::memcpy(retained.message, record.message, diagnostics::MaxMessageLength);
        if (retained.truncated)
            ++m_truncated;

        if (m_retained.Size() < MaximumEditorActivityDiagnosticsRetained)
        {
            m_retained.EmplaceBack(retained);
            return;
        }

        m_retained[m_retainedStart] = retained;
        m_retainedStart = (m_retainedStart + 1u) % MaximumEditorActivityDiagnosticsRetained;
        ++m_evicted;
    }

    u32 EditorActivityCore::PhysicalRetainedIndex(const u32 logicalIndex) const noexcept
    {
        return (m_retainedStart + logicalIndex) % MaximumEditorActivityDiagnosticsRetained;
    }
} // namespace vanguard::editor::detail

namespace vanguard::editor
{
    bool RegisterEditorActivityService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorActivityCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorActivityServiceId;
        descriptor.name = "editorActivity";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 1};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorActivityService;
        descriptor.destroy = DestroyEditorActivityService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorActivityService* FindEditorActivityService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorActivityService*>(host.FindCapability(EditorActivityCapabilityId));
    }

    EditorActivityService* FindEditorActivityService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorActivityService*>(context.FindCapability(EditorActivityCapabilityId));
    }
} // namespace vanguard::editor
