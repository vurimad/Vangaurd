// TEST SUPPORT ONLY: dedicated native UI integration-test application wrapper.

#include <native_ui_integration_test_application.hpp>
#include <native_ui_integration_test_extension.hpp>
#include <native_ui_integration_test_harness.hpp>

#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_activity_preferences.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <new>
#include <cstring>
#include <thread>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;

    inline constexpr app::ServiceId NativeUiIntegrationTestServiceId = 0x6564756974657301ull;
    inline constexpr engine::FrameParticipantId NativeUiIntegrationTestFrameParticipantId = 0x6564756974667201ull;
    constexpr const char* NativeUiWorkerDiagnostic = "Editor native UI worker diagnostic";
    constexpr vanguard::u64 PersistenceActivityLevels = 0x15u;
    constexpr vanguard::i64 PersistenceActivityCategory = 4;
    constexpr const char* PersistenceActivityText = "persisted";
    constexpr vanguard::u64 DefaultActivityLevels = 0x3fu;
    constexpr vanguard::i64 DefaultActivityCategory = -1;
    constexpr const char* DefaultActivityText = "";
    constexpr vanguard::u32 UiStateSection =
        vanguard::serialization::MakeFourCC('U', 'I', 'S', 'T');

    [[nodiscard]] vanguard::u32 LoadU32(const vanguard::u8* const bytes) noexcept
    {
        return static_cast<vanguard::u32>(bytes[0]) |
               (static_cast<vanguard::u32>(bytes[1]) << 8u) |
               (static_cast<vanguard::u32>(bytes[2]) << 16u) |
               (static_cast<vanguard::u32>(bytes[3]) << 24u);
    }

    [[nodiscard]] vanguard::u64 LoadU64(const vanguard::u8* const bytes) noexcept
    {
        return static_cast<vanguard::u64>(LoadU32(bytes)) |
               (static_cast<vanguard::u64>(LoadU32(bytes + 4u)) << 32u);
    }

    void StoreU16(vanguard::u8* const bytes, const vanguard::u16 value) noexcept
    {
        bytes[0] = static_cast<vanguard::u8>(value);
        bytes[1] = static_cast<vanguard::u8>(value >> 8u);
    }

    void StoreU32(vanguard::u8* const bytes, const vanguard::u32 value) noexcept
    {
        bytes[0] = static_cast<vanguard::u8>(value);
        bytes[1] = static_cast<vanguard::u8>(value >> 8u);
        bytes[2] = static_cast<vanguard::u8>(value >> 16u);
        bytes[3] = static_cast<vanguard::u8>(value >> 24u);
    }

    void StoreU64(vanguard::u8* const bytes, const vanguard::u64 value) noexcept
    {
        StoreU32(bytes, static_cast<vanguard::u32>(value));
        StoreU32(bytes + 4u, static_cast<vanguard::u32>(value >> 32u));
    }

    enum class PersistenceCanonicalMutation : vanguard::u8
    {
        Topology,
        Corrupt,
        Truncated,
        Future
    };

    void LogPersistenceLifecycleEvent(const app::LifecycleEvent& event, void*) noexcept
    {
        if (event.service != editor::EditorUserStateServiceId &&
            event.service != editor::EditorShellServiceId &&
            event.service != editor::EditorUiServiceId &&
            event.service != NativeUiIntegrationTestServiceId)
            return;
        if (event.stage != app::LifecycleStage::Start &&
            event.stage != app::LifecycleStage::Quiesce &&
            event.stage != app::LifecycleStage::Shutdown)
            return;
        VG_LOG_INFO(
            vanguard::diagnostics::Category::Engine,
            "Editor persistence lifecycle event: sequence=%llu service=%llu stage=%u state=%u result=%u",
            static_cast<unsigned long long>(event.sequence),
            static_cast<unsigned long long>(event.service), static_cast<vanguard::u32>(event.stage),
            static_cast<vanguard::u32>(event.state), static_cast<vanguard::u32>(event.result));
        vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
    }

    [[nodiscard]] bool CapturePersistenceCanonicalDigest(
        app::ServiceContext& context, vanguard::crypto::Digest256& digest) noexcept
    {
        editor::ProjectWorkspaceService* const workspace =
            editor::FindProjectWorkspaceService(context);
        if (workspace == nullptr)
            return false;
        const vanguard::filesystem::AbsolutePath canonical =
            workspace->GetSavedRoot().AddDirPath("Editor").AddFilePath("user-state.vstate");
        vanguard::memory::MemoryBlock image =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       editor::MaximumEditorUserStateFileBytes, 16);
        if (!image)
            return false;
        vanguard::usize byteCount = 0;
        const bool success =
            vanguard::filesystem::ReadCompleteFile(canonical, image.address, image.size,
                                                   byteCount) ==
                vanguard::filesystem::CompleteFileReadResult::Success &&
            byteCount != 0u;
        if (success)
            digest = vanguard::crypto::Sha256(image.address, byteCount);
        vanguard::memory::Free(image);
        return success;
    }

    [[nodiscard]] bool RewritePersistenceCanonical(
        app::ServiceContext& context, const PersistenceCanonicalMutation mutation,
        vanguard::crypto::Digest256* const resultingDigest = nullptr) noexcept
    {
        editor::ProjectWorkspaceService* const workspace =
            editor::FindProjectWorkspaceService(context);
        if (workspace == nullptr)
            return false;
        const vanguard::filesystem::AbsolutePath canonical =
            workspace->GetSavedRoot().AddDirPath("Editor").AddFilePath("user-state.vstate");
        vanguard::memory::MemoryBlock image =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       editor::MaximumEditorUserStateFileBytes, 16);
        if (!image)
            return false;

        vanguard::usize byteCount = 0;
        bool success =
            vanguard::filesystem::ReadCompleteFile(canonical, image.address, image.size,
                                                   byteCount) ==
                vanguard::filesystem::CompleteFileReadResult::Success &&
            byteCount >= vanguard::serialization::DocumentHeader::WireSize +
                             sizeof(vanguard::crypto::Digest256);
        auto* const bytes = static_cast<vanguard::u8*>(image.address);
        if (success)
        {
            editor::EditorUserStateEnvelopeInfo info;
            editor::EditorUserStateFailure failure;
            success = editor::ValidateEditorUserStateImage(
                          bytes, byteCount, workspace->GetProject().id, info, &failure) ==
                      editor::EditorUserStateLoadResult::Restored;
        }

        if (success && mutation == PersistenceCanonicalMutation::Topology)
        {
            const vanguard::u64 tableOffset = LoadU64(bytes + 24u);
            const vanguard::u32 sectionCount = LoadU32(bytes + 32u);
            vanguard::u8* uiDescriptor = nullptr;
            if (sectionCount > 16u ||
                tableOffset + static_cast<vanguard::u64>(sectionCount) *
                                  vanguard::serialization::SectionDescriptor::WireSize >
                    byteCount - sizeof(vanguard::crypto::Digest256))
                success = false;
            for (vanguard::u32 index = 0; success && index < sectionCount; ++index)
            {
                vanguard::u8* const descriptor =
                    bytes + tableOffset +
                    static_cast<vanguard::u64>(index) *
                        vanguard::serialization::SectionDescriptor::WireSize;
                if (LoadU32(descriptor) == UiStateSection)
                    uiDescriptor = descriptor;
            }
            if (success && uiDescriptor == nullptr)
                success = false;
            if (success)
            {
                const vanguard::u64 sectionOffset = LoadU64(uiDescriptor + 16u);
                const vanguard::u64 sectionSize = LoadU64(uiDescriptor + 24u);
                if (sectionOffset + sectionSize > byteCount - sizeof(vanguard::crypto::Digest256) ||
                    sectionSize < 16u)
                    success = false;
                if (success)
                {
                    vanguard::u8* const section = bytes + sectionOffset;
                    const vanguard::u32 displayCount = LoadU32(section);
                    if (displayCount == 0u ||
                        16u + static_cast<vanguard::u64>(displayCount) * 48u > sectionSize)
                        success = false;
                    for (vanguard::u32 index = 0; success && index < displayCount; ++index)
                    {
                        vanguard::u8* const display = section + 16u + index * 48u;
                        const vanguard::i32 x =
                            100000 + static_cast<vanguard::i32>(index) * 10000;
                        const vanguard::i32 y = 50000;
                        StoreU32(display + 0u, static_cast<vanguard::u32>(x));
                        StoreU32(display + 4u, static_cast<vanguard::u32>(y));
                        StoreU32(display + 16u, static_cast<vanguard::u32>(x));
                        StoreU32(display + 20u, static_cast<vanguard::u32>(y));
                    }
                    if (success)
                        StoreU64(uiDescriptor + 40u,
                                 vanguard::serialization::Crc64(section,
                                                                static_cast<vanguard::usize>(
                                                                    sectionSize)));
                }
            }
        }
        else if (success && mutation == PersistenceCanonicalMutation::Future)
        {
            StoreU16(bytes + 8u, 2u);
            StoreU16(bytes + 10u, 0u);
            StoreU32(bytes + 36u, vanguard::serialization::Crc32(bytes, 36u));
        }

        if (success && mutation == PersistenceCanonicalMutation::Corrupt)
        {
            bytes[byteCount - 1u] ^= 0x5au;
        }
        else if (success && mutation == PersistenceCanonicalMutation::Truncated)
        {
            byteCount -= sizeof(vanguard::crypto::Digest256);
        }
        else if (success)
        {
            const vanguard::usize digestOffset =
                byteCount - sizeof(vanguard::crypto::Digest256);
            const vanguard::crypto::Digest256 digest =
                vanguard::crypto::Sha256(bytes, digestOffset);
            std::memcpy(bytes + digestOffset, digest.bytes, sizeof(digest.bytes));

            editor::EditorUserStateEnvelopeInfo info;
            editor::EditorUserStateFailure failure;
            const editor::EditorUserStateLoadResult expected =
                mutation == PersistenceCanonicalMutation::Topology
                    ? editor::EditorUserStateLoadResult::Restored
                    : editor::EditorUserStateLoadResult::RejectedFuture;
            success = editor::ValidateEditorUserStateImage(
                          bytes, byteCount, workspace->GetProject().id, info, &failure) ==
                      expected;
        }
        if (success && (mutation == PersistenceCanonicalMutation::Corrupt ||
                        mutation == PersistenceCanonicalMutation::Truncated))
        {
            editor::EditorUserStateEnvelopeInfo info;
            editor::EditorUserStateFailure failure;
            success = editor::ValidateEditorUserStateImage(
                          bytes, byteCount, workspace->GetProject().id, info, &failure) ==
                      editor::EditorUserStateLoadResult::RejectedCorrupt;
        }
        if (success && resultingDigest != nullptr)
            *resultingDigest = vanguard::crypto::Sha256(bytes, byteCount);

        if (success)
        {
            auto writer = vanguard::filesystem::RawFileWriter::Create(canonical, false);
            success = static_cast<bool>(writer);
            if (writer)
            {
                writer->Serialize(bytes, byteCount);
                writer->Flush();
                success = writer->GetSize() == byteCount;
            }
        }
        vanguard::memory::Free(image);
        return success;
    }

    struct PersistencePanelObservation final
    {
        bool found = false;
        bool open = false;
        bool placeholder = false;
    };

    void ObservePersistencePanel(const editor::EditorPanelInfo& panel, void* const userData) noexcept
    {
        auto* const observation = static_cast<PersistencePanelObservation*>(userData);
        if (observation == nullptr ||
            panel.type != editor::testing::NativeUiCommandPanelTypeId ||
            panel.id != editor::testing::NativeUiCommandPanelInstanceId)
            return;
        observation->found = true;
        observation->open = panel.open;
        observation->placeholder = panel.placeholder;
    }

    class NativeUiIntegrationTestService final : public app::Service
    {
    public:
        explicit NativeUiIntegrationTestService(const editor::testing::NativeUiIntegrationTestServiceConfig& config) noexcept
            : m_config(config)
        {
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_ui = editor::FindEditorUiService(context);
            m_activity = editor::FindEditorActivityService(context);
            m_commands = editor::FindEditorCommandService(context);
            m_rendering = engine::FindRenderingService(context);
            m_shellPersistence = editor::detail::FindEditorShellPersistencePrivateService(context);
            engine::FramePipelineService* const frames = engine::FindFramePipelineService(context);
            if (m_config.services == nullptr || m_config.states == nullptr || m_ui == nullptr ||
                m_activity == nullptr || m_commands == nullptr || m_rendering == nullptr ||
                m_shellPersistence == nullptr || frames == nullptr)
                return app::LifecycleStatus::Failure("native UI integration-test dependencies are unavailable");

            constexpr engine::FrameParticipantId dependencies[]{
                editor::EditorUiRenderParticipantId, editor::EditorUserStateFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = NativeUiIntegrationTestFrameParticipantId;
            descriptor.name = "nativeUiIntegrationTest";
            descriptor.phase = engine::FramePhase::EndFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 2};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!frames->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                                : "native UI integration-test frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext& context) noexcept override
        {
            if (m_config.persistenceScenario != editor::testing::NativeUiPersistenceScenario::None)
            {
                if ((m_config.persistenceScenario ==
                         editor::testing::NativeUiPersistenceScenario::Topology &&
                     !RewritePersistenceCanonical(context, PersistenceCanonicalMutation::Topology)) ||
                    (m_config.persistenceScenario ==
                         editor::testing::NativeUiPersistenceScenario::RejectedCorrupt &&
                     !RewritePersistenceCanonical(context, PersistenceCanonicalMutation::Corrupt,
                                                  &m_rejectionCanonicalDigest)) ||
                    (m_config.persistenceScenario ==
                         editor::testing::NativeUiPersistenceScenario::RejectedTruncated &&
                     !RewritePersistenceCanonical(context, PersistenceCanonicalMutation::Truncated,
                                                  &m_rejectionCanonicalDigest)) ||
                    (m_config.persistenceScenario ==
                         editor::testing::NativeUiPersistenceScenario::Future &&
                     !RewritePersistenceCanonical(context, PersistenceCanonicalMutation::Future,
                                                  &m_rejectionCanonicalDigest)) ||
                    (m_config.persistenceScenario ==
                          editor::testing::NativeUiPersistenceScenario::ProjectMismatch) &&
                     !CapturePersistenceCanonicalDigest(context, m_rejectionCanonicalDigest))
                    return app::LifecycleStatus::Failure(
                        "native UI persistence pre-restore stimulus failed");
                m_rejectionCanonicalDigestValid =
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::RejectedCorrupt ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::RejectedTruncated ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::Future ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::ProjectMismatch;
                m_started = true;
                return app::LifecycleStatus::Success();
            }

            if (m_config.visualPreview)
            {
                if (m_ui == nullptr || m_activity == nullptr || m_rendering == nullptr ||
                    !m_harness.Start(*m_ui, *m_activity, *m_rendering, true, true))
                {
                    m_harness.Stop();
                    return app::LifecycleStatus::Failure("editor visual-preview harness startup failed");
                }
                m_started = true;
                return app::LifecycleStatus::Success();
            }

            std::thread worker([]() noexcept
            {
                VG_LOG_INFO(vanguard::diagnostics::Category::Engine, "%s", NativeUiWorkerDiagnostic);
                vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
            });
            worker.join();
            m_workerDiagnosticEmitted = true;

            if (m_ui == nullptr || m_activity == nullptr || m_rendering == nullptr ||
                !m_harness.Start(*m_ui, *m_activity, *m_rendering, m_config.interactive, false))
            {
                m_harness.Stop();
                return app::LifecycleStatus::Failure("native UI integration-test harness startup failed");
            }
            m_started = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext& context) noexcept override
        {
            if (m_config.persistenceScenario != editor::testing::NativeUiPersistenceScenario::None)
            {
                if (m_userState == nullptr && m_config.services != nullptr)
                    m_userState = editor::FindEditorUserStateService(*m_config.services);
                if (m_userState == nullptr)
                    return app::LifecycleStatus::Failure(
                        "native UI persistence fixture never observed UserState after host startup");
                const editor::EditorUserStateStatus status = m_userState->GetStatus();
                const bool rejected =
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::RejectedCorrupt ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::RejectedTruncated ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::Future ||
                    m_config.persistenceScenario ==
                        editor::testing::NativeUiPersistenceScenario::ProjectMismatch;
                if (status.state != editor::EditorUserStateServiceState::Quiesced ||
                    (rejected && (status.save != editor::EditorUserStateSaveResult::WriteInhibited ||
                                  !status.writesInhibited)) ||
                    (!rejected && status.save != editor::EditorUserStateSaveResult::Published))
                    return app::LifecycleStatus::Failure(
                        "native UI persistence final publication did not precede test-service quiesce");
                if (rejected)
                {
                    vanguard::crypto::Digest256 currentDigest;
                    if (!m_rejectionCanonicalDigestValid ||
                        !CapturePersistenceCanonicalDigest(context, currentDigest) ||
                        !(currentDigest == m_rejectionCanonicalDigest))
                        return app::LifecycleStatus::Failure(
                            "native UI persistence rejected canonical changed during write-inhibited session");
                }
                editor::testing::RecordNativeUiPersistenceAfterFinalCapture();
                LogPersistenceTelemetry(status, "after-final");
                m_started = false;
                return app::LifecycleStatus::Success();
            }
            StopHarness();
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            StopHarness();
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            StopHarness();
            m_ui = nullptr;
            m_activity = nullptr;
            m_commands = nullptr;
            m_rendering = nullptr;
            m_userState = nullptr;
            m_shellPersistence = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<NativeUiIntegrationTestService*>(userData);
            if (service == nullptr || !service->m_started)
                return engine::FrameParticipantStatus::Failure("native UI integration-test service is not running");

            if (service->m_config.persistenceScenario !=
                editor::testing::NativeUiPersistenceScenario::None)
                return service->TickPersistenceScenario();

            if (service->m_config.visualPreview)
            {
                bool finished = false;
                return service->m_harness.Tick(finished)
                    ? engine::FrameParticipantStatus::Success()
                    : engine::FrameParticipantStatus::Failure("editor visual-preview harness failed");
            }

            if (!editor::testing::TickNativeUiActivityProducer())
                return engine::FrameParticipantStatus::Failure("native UI integration-test Activity producer is unavailable");
            if (!service->ObserveWorkerDiagnostic())
                return engine::FrameParticipantStatus::Failure("native UI integration-test diagnostics capture failed");
            if (!service->ApplyShortcutOverrideRequest())
                return engine::FrameParticipantStatus::Failure("native UI integration-test shortcut override failed");
            bool finished = false;
            if (!service->m_harness.Tick(finished))
                return engine::FrameParticipantStatus::Failure("native UI integration test failed");
            if (finished)
            {
                if (!service->m_workerDiagnosticObserved)
                    return engine::FrameParticipantStatus::Failure(
                        "native UI integration-test worker diagnostic was not observed through Activity");
                if (!service->m_config.states->RequestExit())
                    return engine::FrameParticipantStatus::Failure("native UI integration test could not request application exit");
            }
            return engine::FrameParticipantStatus::Success();
        }

        [[nodiscard]] bool ObserveWorkerDiagnostic() noexcept
        {
            if (m_workerDiagnosticObserved)
                return true;
            if (!m_workerDiagnosticEmitted || m_activity == nullptr)
                return false;

            editor::EditorActivityDiagnosticRecord records[32]{};
            editor::EditorActivityDiagnosticCapture capture;
            editor::EditorActivityFailure failure;
            if (!m_activity->CaptureDiagnostics(m_diagnosticCursor, {records, 32}, capture, &failure))
                return false;
            for (vanguard::u32 index = 0; index < capture.copiedCount; ++index)
            {
                if (std::strstr(records[index].message, NativeUiWorkerDiagnostic) == nullptr)
                    continue;
                m_workerDiagnosticObserved = true;
                VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                            "Editor native UI Activity diagnostic copy: worker=1 sequence=%llu",
                            static_cast<unsigned long long>(records[index].lastSequence));
                vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
                break;
            }
            m_diagnosticCursor = capture.cursor;
            return true;
        }

        void StopHarness() noexcept
        {
            if (!m_started)
                return;
            m_harness.Stop();
            m_started = false;
        }

        [[nodiscard]] bool ApplyShortcutOverrideRequest() noexcept
        {
            bool custom = false;
            if (!editor::testing::ConsumeNativeUiShortcutOverrideRequest(custom))
                return true;
            if (m_commands == nullptr)
                return false;

            editor::EditorShortcutOverride shortcutOverride;
            shortcutOverride.mode = custom ? editor::EditorShortcutOverrideMode::Custom : editor::EditorShortcutOverrideMode::Default;
            if (custom)
                shortcutOverride.chord = {vanguard::input::Key::F12, editor::EditorShortcutModifiers::None};
            editor::EditorCommandFailure failure;
            if (!m_commands->SetShortcutOverride(editor::testing::NativeUiLocalContextId,
                                                 editor::testing::NativeUiCommandId,
                                                 shortcutOverride, &failure))
                return false;

            editor::EditorShortcutBinding binding;
            if (!m_commands->GetShortcutBinding(editor::testing::NativeUiLocalContextId,
                                                editor::testing::NativeUiCommandId,
                                                binding, &failure))
                return false;
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI test shortcut: custom=%u mode=%u bound=%u key=%u",
                        vanguard::u32(custom), static_cast<vanguard::u32>(binding.mode), vanguard::u32(binding.bound),
                        static_cast<vanguard::u32>(binding.chord.key));
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
            return true;
        }

        [[nodiscard]] bool ApplyPersistenceActivityPreferences() noexcept
        {
            if (m_shellPersistence == nullptr)
                return false;
            editor::EditorTypedPreferenceRecord records[3]{};
            records[0].extension = editor::detail::ActivityPresentationExtensionId;
            records[0].providerId = editor::detail::ActivityPreferenceProviderId;
            records[0].fieldId = editor::detail::ActivityCategoryPreferenceFieldId;
            records[0].value.type = editor::EditorPreferenceValueType::SignedInteger;
            records[0].value.value.signedInteger = PersistenceActivityCategory;

            records[1].extension = editor::detail::ActivityPresentationExtensionId;
            records[1].providerId = editor::detail::ActivityPreferenceProviderId;
            records[1].fieldId = editor::detail::ActivityLevelsPreferenceFieldId;
            records[1].value.type = editor::EditorPreferenceValueType::UnsignedBitMask;
            records[1].value.value.unsignedBitMask = PersistenceActivityLevels;

            records[2].extension = editor::detail::ActivityPresentationExtensionId;
            records[2].providerId = editor::detail::ActivityPreferenceProviderId;
            records[2].fieldId = editor::detail::ActivityTextPreferenceFieldId;
            records[2].value.type = editor::EditorPreferenceValueType::Utf8String;
            records[2].value.value.string.byteCount =
                static_cast<vanguard::u16>(std::strlen(PersistenceActivityText));
            std::memcpy(records[2].value.value.string.bytes, PersistenceActivityText,
                        records[2].value.value.string.byteCount);
            records[2].value.value.string.bytes[records[2].value.value.string.byteCount] = '\0';

            editor::EditorTypedPreferenceItemResult results[3]{};
            editor::EditorPreferenceFailure failure;
            return m_shellPersistence->ValidateTypedPreferences({records, 3}, {results, 3},
                                                                 &failure) &&
                   m_shellPersistence->ApplyTypedPreferences({records, 3}, {results, 3},
                                                              &failure);
        }

        [[nodiscard]] bool CaptureActivityPreferences(
            const vanguard::u64 expectedLevels, const vanguard::i64 expectedCategory,
            const char* const expectedText) noexcept
        {
            if (m_shellPersistence == nullptr || expectedText == nullptr)
                return false;
            editor::EditorTypedPreferenceRecord
                records[editor::MaximumEditorLivePreferenceFields]{};
            vanguard::u32 count = 0;
            editor::EditorPreferenceFailure failure;
            if (!m_shellPersistence->CaptureTypedPreferences(
                    records, editor::MaximumEditorLivePreferenceFields, count, &failure))
                return false;

            bool levels = false;
            bool category = false;
            bool text = false;
            for (vanguard::u32 index = 0; index < count; ++index)
            {
                const editor::EditorTypedPreferenceRecord& record = records[index];
                if (record.extension != editor::detail::ActivityPresentationExtensionId ||
                    record.providerId != editor::detail::ActivityPreferenceProviderId)
                    continue;
                if (record.fieldId == editor::detail::ActivityLevelsPreferenceFieldId)
                    levels = record.value.type == editor::EditorPreferenceValueType::UnsignedBitMask &&
                             record.value.value.unsignedBitMask == expectedLevels;
                else if (record.fieldId == editor::detail::ActivityCategoryPreferenceFieldId)
                    category = record.value.type == editor::EditorPreferenceValueType::SignedInteger &&
                               record.value.value.signedInteger == expectedCategory;
                else if (record.fieldId == editor::detail::ActivityTextPreferenceFieldId)
                    text = record.value.type == editor::EditorPreferenceValueType::Utf8String &&
                           record.value.value.string.byteCount == std::strlen(expectedText) &&
                           std::memcmp(record.value.value.string.bytes, expectedText,
                                       record.value.value.string.byteCount) == 0;
            }
            return levels && category && text;
        }

        [[nodiscard]] bool CapturePersistenceActivityPreferences() noexcept
        {
            return CaptureActivityPreferences(PersistenceActivityLevels,
                                              PersistenceActivityCategory,
                                              PersistenceActivityText);
        }

        [[nodiscard]] bool CaptureDefaultActivityPreferences() noexcept
        {
            return CaptureActivityPreferences(DefaultActivityLevels, DefaultActivityCategory,
                                              DefaultActivityText);
        }

        [[nodiscard]] bool ObservePersistencePanelState(
            const bool expectedOpen, const bool expectedPlaceholder) const noexcept
        {
            if (m_ui == nullptr)
                return false;
            PersistencePanelObservation observation;
            m_ui->GetPanels().VisitPanels(ObservePersistencePanel, &observation);
            return observation.found && observation.open == expectedOpen &&
                   observation.placeholder == expectedPlaceholder;
        }

        [[nodiscard]] bool VerifyPersistenceShortcut() const noexcept
        {
            if (m_commands == nullptr)
                return false;
            editor::EditorShortcutBinding binding;
            editor::EditorCommandFailure failure;
            return m_commands->GetShortcutBinding(
                       editor::testing::NativeUiLocalContextId,
                       editor::testing::NativeUiCommandId, binding, &failure) &&
                   binding.mode == editor::EditorShortcutOverrideMode::Custom &&
                   binding.bound && binding.chord.key == vanguard::input::Key::F12 &&
                   binding.chord.modifiers == editor::EditorShortcutModifiers::None;
        }

        [[nodiscard]] bool VerifyDefaultPersistenceShortcut() const noexcept
        {
            if (m_commands == nullptr)
                return false;
            editor::EditorShortcutBinding binding;
            editor::EditorCommandFailure failure;
            return m_commands->GetShortcutBinding(
                       editor::testing::NativeUiLocalContextId,
                       editor::testing::NativeUiCommandId, binding, &failure) &&
                   binding.mode == editor::EditorShortcutOverrideMode::Default && binding.bound &&
                   binding.chord.key == vanguard::input::Key::F8 &&
                   binding.chord.modifiers == editor::EditorShortcutModifiers::None;
        }

        [[nodiscard]] bool InitializePersistenceScenario() noexcept
        {
            if (m_userState == nullptr && m_config.services != nullptr)
                m_userState = editor::FindEditorUserStateService(*m_config.services);
            if (m_userState == nullptr || m_ui == nullptr || m_commands == nullptr ||
                m_shellPersistence == nullptr)
                return false;
            const editor::EditorUserStateStatus status = m_userState->GetStatus();
            if (status.state != editor::EditorUserStateServiceState::Running)
                return false;

            editor::testing::RecordNativeUiPersistenceRestoreObserved();
            const auto scenario = m_config.persistenceScenario;
            const bool expectedOpen = m_config.interactive;
            editor::testing::NativeUiPersistencePanelTelemetry fixture;
            const bool fixtureActive =
                editor::testing::GetNativeUiPersistencePanelTelemetry(fixture);

            if (scenario == editor::testing::NativeUiPersistenceScenario::Seed)
            {
                if (status.load != editor::EditorUserStateLoadResult::Missing || !fixtureActive ||
                    !editor::testing::SetNativeUiPersistencePanelOpen(expectedOpen))
                    return false;
                editor::EditorShortcutOverride shortcut;
                shortcut.mode = editor::EditorShortcutOverrideMode::Custom;
                shortcut.chord = {vanguard::input::Key::F12,
                                  editor::EditorShortcutModifiers::None};
                editor::EditorCommandFailure failure;
                if (!m_commands->SetShortcutOverride(
                        editor::testing::NativeUiLocalContextId,
                        editor::testing::NativeUiCommandId, shortcut, &failure) ||
                    !ApplyPersistenceActivityPreferences())
                    return false;
                return ObservePersistencePanelState(expectedOpen, false) &&
                       VerifyPersistenceShortcut() &&
                       CapturePersistenceActivityPreferences();
            }

            if (status.load != editor::EditorUserStateLoadResult::Restored)
            {
                const editor::EditorUserStateLoadResult expected =
                    scenario == editor::testing::NativeUiPersistenceScenario::RejectedCorrupt
                        ? editor::EditorUserStateLoadResult::RejectedCorrupt
                        : scenario == editor::testing::NativeUiPersistenceScenario::RejectedTruncated
                              ? editor::EditorUserStateLoadResult::RejectedCorrupt
                        : scenario == editor::testing::NativeUiPersistenceScenario::Future
                              ? editor::EditorUserStateLoadResult::RejectedFuture
                              : scenario == editor::testing::NativeUiPersistenceScenario::ProjectMismatch
                                    ? editor::EditorUserStateLoadResult::RejectedProjectMismatch
                                    : editor::EditorUserStateLoadResult::Restored;
                return status.load == expected && status.writesInhibited && fixtureActive &&
                       ObservePersistencePanelState(true, false) &&
                       VerifyDefaultPersistenceShortcut() &&
                       CaptureDefaultActivityPreferences();
            }

            if (scenario == editor::testing::NativeUiPersistenceScenario::RejectedCorrupt ||
                scenario == editor::testing::NativeUiPersistenceScenario::RejectedTruncated ||
                scenario == editor::testing::NativeUiPersistenceScenario::Future ||
                scenario == editor::testing::NativeUiPersistenceScenario::ProjectMismatch)
                return false;

            if (!fixtureActive ||
                !ObservePersistencePanelState(expectedOpen, false) ||
                !VerifyPersistenceShortcut() ||
                !CapturePersistenceActivityPreferences())
                return false;
            if (scenario == editor::testing::NativeUiPersistenceScenario::Topology)
                return status.repairedPlacements > 0u;
            return status.panelsRestored >= 1u && status.shortcutsApplied >= 1u &&
                   status.preferencesApplied >= 3u;
        }

        [[nodiscard]] engine::FrameParticipantStatus TickPersistenceScenario() noexcept
        {
            ++m_persistenceFrames;
            if (!m_persistenceInitialized)
            {
                if (!InitializePersistenceScenario())
                    return engine::FrameParticipantStatus::Failure(
                        "native UI persistence restore/seed validation failed");
                m_persistenceInitialized = true;
                editor::testing::RecordNativeUiPersistenceFirstFrame();
                LogPersistenceTelemetry(m_userState->GetStatus(), "first-frame");
                if (m_config.persistenceScenario ==
                    editor::testing::NativeUiPersistenceScenario::Seed)
                    return engine::FrameParticipantStatus::Success();
            }
            else if (m_config.persistenceScenario ==
                         editor::testing::NativeUiPersistenceScenario::Seed &&
                     !m_persistenceDirtyObserved)
            {
                const editor::EditorUserStateStatus status = m_userState->GetStatus();
                LogPersistenceTelemetry(status, "dirty-observed");
                if (status.dirtyGeneration == status.publishedGeneration ||
                    status.dirtyGeneration == 0u)
                    return engine::FrameParticipantStatus::Failure(
                        "native UI persistence seed mutation was not observed as dirty");
                m_persistenceDirtyObserved = true;
            }

            if (!m_config.interactive && !m_config.states->RequestExit())
                return engine::FrameParticipantStatus::Failure(
                    "native UI persistence test could not request application exit");
            return engine::FrameParticipantStatus::Success();
        }

        void LogPersistenceTelemetry(const editor::EditorUserStateStatus& status,
                                     const char* const stage) const noexcept
        {
            editor::testing::NativeUiPersistencePanelTelemetry fixture;
            const bool fixtureActive =
                editor::testing::GetNativeUiPersistencePanelTelemetry(fixture);
            PersistencePanelObservation panel;
            m_ui->GetPanels().VisitPanels(ObservePersistencePanel, &panel);
            VG_LOG_INFO(
                vanguard::diagnostics::Category::Engine,
                "Editor native UI persistence telemetry: stage=%s scenario=%u state=%u load=%u save=%u writesInhibited=%u panelsRestored=%u panelsCreated=%u placeholders=%u panelsSkipped=%u shortcutsApplied=%u shortcutsOrphaned=%u preferencesApplied=%u preferencesOrphaned=%u repaired=%u dirty=%llu published=%llu fixture=%u panel=%u panelOpen=%u panelPlaceholder=%u create=%u destroy=%u",
                stage, static_cast<vanguard::u32>(m_config.persistenceScenario),
                static_cast<vanguard::u32>(status.state),
                static_cast<vanguard::u32>(status.load),
                static_cast<vanguard::u32>(status.save), vanguard::u32(status.writesInhibited),
                status.panelsRestored, status.panelsCreated, status.panelPlaceholders,
                status.panelsSkipped, status.shortcutsApplied, status.shortcutsOrphaned,
                status.preferencesApplied, status.preferencesOrphaned, status.repairedPlacements,
                static_cast<unsigned long long>(status.dirtyGeneration),
                static_cast<unsigned long long>(status.publishedGeneration),
                vanguard::u32(fixtureActive), vanguard::u32(panel.found),
                vanguard::u32(panel.open), vanguard::u32(panel.placeholder),
                fixture.creates, fixture.destroys);
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
        }

        editor::testing::NativeUiIntegrationTestServiceConfig m_config;
        editor::testing::NativeUiIntegrationTestHarness m_harness;
        editor::EditorUiService* m_ui = nullptr;
        editor::EditorActivityService* m_activity = nullptr;
        editor::EditorCommandService* m_commands = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        editor::EditorUserStateService* m_userState = nullptr;
        editor::detail::EditorShellPersistencePrivateService* m_shellPersistence = nullptr;
        editor::EditorActivityCursor m_diagnosticCursor;
        bool m_workerDiagnosticEmitted = false;
        bool m_workerDiagnosticObserved = false;
        vanguard::u32 m_persistenceFrames = 0;
        vanguard::crypto::Digest256 m_rejectionCanonicalDigest;
        bool m_persistenceInitialized = false;
        bool m_persistenceDirtyObserved = false;
        bool m_rejectionCanonicalDigestValid = false;
        bool m_started = false;
    };

    app::Service* CreateNativeUiIntegrationTestService(void* const userData) noexcept
    {
        const auto* const config = static_cast<const editor::testing::NativeUiIntegrationTestServiceConfig*>(userData);
        if (config == nullptr)
            return nullptr;
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(NativeUiIntegrationTestService), alignof(NativeUiIntegrationTestService));
        return block ? ::new (block.address) NativeUiIntegrationTestService(*config) : nullptr;
    }

    void DestroyNativeUiIntegrationTestService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<NativeUiIntegrationTestService*>(service)->~NativeUiIntegrationTestService();
        vanguard::memory::MemoryBlock block{service, sizeof(NativeUiIntegrationTestService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    [[nodiscard]] bool RegisterNativeUiIntegrationTestService(app::EngineHost& host,
                                                              editor::testing::NativeUiIntegrationTestServiceConfig& config,
                                                              app::HostFailure* const failure) noexcept
    {
        constexpr app::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, app::DependencyKind::Required},
            {engine::RenderingServiceId, app::DependencyKind::Required},
            {editor::EditorActivityServiceId, app::DependencyKind::Required},
            {editor::EditorCommandServiceId, app::DependencyKind::Required},
            {editor::EditorUiServiceId, app::DependencyKind::Required},
            {editor::EditorShellServiceId, app::DependencyKind::Required}};
        app::ServiceDescriptor descriptor;
        descriptor.id = NativeUiIntegrationTestServiceId;
        descriptor.name = "nativeUiIntegrationTest";
        descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        descriptor.scope = app::ServiceScope::TestSandbox;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.dependencies = dependencies;
        descriptor.create = CreateNativeUiIntegrationTestService;
        descriptor.destroy = DestroyNativeUiIntegrationTestService;
        descriptor.userData = &config;
        return host.RegisterService(editor::EditorModuleId, descriptor, failure);
    }
} // namespace

namespace vanguard::editor::testing
{
    ApplicationTraits NativeUiIntegrationTestApplication::GetTraits() const noexcept
    {
        return m_editor.GetTraits();
    }

    application::CompositionStatus NativeUiIntegrationTestApplication::Compose(const application::ApplicationStartupContext& startup,
                                                                               application::EngineHost& services,
                                                                               application::ApplicationStateMachine& states) noexcept
    {
        const bool visualPreview = startup.commandLine.HasArgument("--visual-style-preview");
        const bool interactive = startup.commandLine.HasArgument("--inspect-editor-ui") || visualPreview;
        const bool lifecycle = startup.commandLine.HasArgument("--validate-editor-ui");
        NativeUiPersistenceScenario persistenceScenario = NativeUiPersistenceScenario::None;
        const struct
        {
            const char* argument;
            NativeUiPersistenceScenario scenario;
        } persistenceArguments[]{
            {"--persistence-seed", NativeUiPersistenceScenario::Seed},
            {"--persistence-restore", NativeUiPersistenceScenario::Restore},
            {"--persistence-topology", NativeUiPersistenceScenario::Topology},
            {"--persistence-corrupt", NativeUiPersistenceScenario::RejectedCorrupt},
            {"--persistence-truncated", NativeUiPersistenceScenario::RejectedTruncated},
            {"--persistence-future", NativeUiPersistenceScenario::Future},
            {"--persistence-project-mismatch", NativeUiPersistenceScenario::ProjectMismatch}};
        for (const auto& candidate : persistenceArguments)
        {
            if (!startup.commandLine.HasArgument(candidate.argument))
                continue;
            if (persistenceScenario != NativeUiPersistenceScenario::None)
                return application::CompositionStatus::Failure(
                    "native UI persistence test accepts exactly one persistence scenario");
            persistenceScenario = candidate.scenario;
        }
        if (!interactive && !lifecycle)
            return application::CompositionStatus::Failure(
                "native UI integration-test executable requires --inspect-editor-ui, --visual-style-preview or --validate-editor-ui");
        ConfigureNativeUiPersistenceRuntime(persistenceScenario);

        const application::CompositionStatus productComposition = m_editor.Compose(startup, services, states);
        if (!productComposition)
            return productComposition;

        m_serviceConfig.services = &services;
        m_serviceConfig.states = &states;
        m_serviceConfig.interactive = interactive;
        m_serviceConfig.visualPreview = visualPreview;
        m_serviceConfig.persistenceScenario = persistenceScenario;
        application::HostFailure failure;
        if (!RegisterNativeUiIntegrationTestService(services, m_serviceConfig, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr ? failure.message
                                                                                      : "native UI integration-test service registration failed");
        if (persistenceScenario != NativeUiPersistenceScenario::None)
            services.SetLifecycleSink(LogPersistenceLifecycleEvent);
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::editor::testing
