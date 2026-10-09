#include <vanguard/editor/editor_shell_service.hpp>

#include <vanguard/editor/editor_project_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

#include <atomic>
#include <new>

namespace vanguard::editor
{
    class EditorShellServiceAccess final
    {
    public:
        [[nodiscard]] static EditorExtensionActivationContext MakeContext(
            const EditorExtensionId extension, const u64 activationGeneration,
            ProjectWorkspaceService& workspace, EditorActivityService& activity, EditorUiService& ui,
            EditorFileDialogService* const fileDialogs, EditorExtensionRegistrationAccess& registrations) noexcept
        {
            return EditorExtensionActivationContext(extension, activationGeneration, workspace, activity, ui, fileDialogs, registrations);
        }
    };
} // namespace vanguard::editor

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;

    struct ActiveExtension
    {
        const editor::EditorExtensionDescriptor* descriptor = nullptr;
        void* privateState = nullptr;
        editor::EditorExtensionOwner owner;
    };

    struct ExtensionOwnerRecord
    {
        editor::EditorExtensionOwner owner;
        bool live = false;
    };

    struct PanelLookup final
    {
        editor::EditorPanelHandle handle;
        editor::EditorPanelInfo info;
        bool found = false;
    };

    void FindPanelByHandle(const editor::EditorPanelInfo& panel, void* const userData) noexcept
    {
        auto& lookup = *static_cast<PanelLookup*>(userData);
        if (!lookup.found && panel.handle == lookup.handle)
        {
            lookup.info = panel;
            lookup.found = true;
        }
    }

    enum class OwnedRegistrationKind : vanguard::u8
    {
        Command,
        ContextType,
        Handler,
        PanelType,
        SurfaceRoot,
        SurfaceContribution,
        ActivitySource,
        PreferenceProvider
    };

    struct OwnedRegistrationRecord
    {
        editor::EditorExtensionOwner owner;
        OwnedRegistrationKind kind = OwnedRegistrationKind::Command;
        editor::EditorCommandToken command;
        editor::EditorCommandContextTypeToken contextType;
        editor::EditorCommandHandlerToken handler;
        editor::EditorPanelTypeToken panelType;
        editor::EditorPanelTypeId panelTypeId = editor::InvalidEditorPanelTypeId;
        editor::detail::EditorUiSurfaceToken surface;
        editor::detail::EditorUiContributionToken contribution;
        editor::detail::EditorActivitySourceToken activitySource;
        editor::EditorPreferenceProviderToken preferenceProvider;
        editor::EditorPanelLocalContextBinding panelLocalContext;
        bool hasPanelLocalContext = false;
        bool active = false;
    };

    std::atomic<vanguard::u64> g_activationGeneration{0};
    std::atomic<vanguard::u32> g_preferenceProviderGeneration{0};

    [[nodiscard]] vanguard::u64 NextActivationGeneration() noexcept
    {
        vanguard::u64 current = g_activationGeneration.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u64{0})
                return 0;
            if (g_activationGeneration.compare_exchange_weak(current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }

    [[nodiscard]] vanguard::u32 NextPreferenceProviderGeneration() noexcept
    {
        vanguard::u32 current = g_preferenceProviderGeneration.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u32{0})
                return 0;
            if (g_preferenceProviderGeneration.compare_exchange_weak(
                    current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }

    [[nodiscard]] bool ValidPreferenceType(const editor::EditorPreferenceValueType type) noexcept
    {
        return type == editor::EditorPreferenceValueType::Bool ||
               type == editor::EditorPreferenceValueType::SignedInteger ||
               type == editor::EditorPreferenceValueType::UnsignedBitMask ||
               type == editor::EditorPreferenceValueType::Utf8String;
    }

    [[nodiscard]] bool ValidUtf8(const char* const bytes, const vanguard::u32 count) noexcept
    {
        vanguard::u32 index = 0;
        while (index < count)
        {
            const vanguard::u8 first = static_cast<vanguard::u8>(bytes[index]);
            if (first == 0)
                return false;
            if (first < 0x80u)
            {
                ++index;
                continue;
            }
            vanguard::u32 continuationCount = 0;
            vanguard::u32 codePoint = 0;
            if ((first & 0xe0u) == 0xc0u)
            {
                continuationCount = 1;
                codePoint = first & 0x1fu;
                if (codePoint < 2u)
                    return false;
            }
            else if ((first & 0xf0u) == 0xe0u)
            {
                continuationCount = 2;
                codePoint = first & 0x0fu;
            }
            else if ((first & 0xf8u) == 0xf0u)
            {
                continuationCount = 3;
                codePoint = first & 0x07u;
                if (codePoint > 4u)
                    return false;
            }
            else
                return false;
            if (index + continuationCount >= count)
                return false;
            for (vanguard::u32 continuation = 0; continuation < continuationCount; ++continuation)
            {
                const vanguard::u8 next = static_cast<vanguard::u8>(bytes[index + continuation + 1u]);
                if ((next & 0xc0u) != 0x80u)
                    return false;
                codePoint = (codePoint << 6u) | (next & 0x3fu);
            }
            if ((continuationCount == 2u && codePoint < 0x800u) ||
                (continuationCount == 3u && codePoint < 0x10000u) ||
                codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
                return false;
            index += continuationCount + 1u;
        }
        return true;
    }

    [[nodiscard]] bool ValueMatchesField(const editor::EditorPreferenceFieldDescriptor& field,
                                         const editor::EditorPreferenceFieldValue& value) noexcept
    {
        if (value.type != field.type || !ValidPreferenceType(value.type))
            return false;
        if (value.type != editor::EditorPreferenceValueType::Utf8String)
            return true;
        const vanguard::u32 count = value.value.string.byteCount;
        return count <= field.maximumStringBytes &&
               count <= editor::MaximumEditorPreferenceStringBytes &&
               value.value.string.bytes[count] == '\0' &&
               ValidUtf8(value.value.string.bytes, count);
    }

    [[nodiscard]] bool SamePreferenceValue(const editor::EditorPreferenceFieldValue& left,
                                           const editor::EditorPreferenceFieldValue& right) noexcept
    {
        if (left.type != right.type)
            return false;
        switch (left.type)
        {
        case editor::EditorPreferenceValueType::Bool:
            return left.value.boolean == right.value.boolean;
        case editor::EditorPreferenceValueType::SignedInteger:
            return left.value.signedInteger == right.value.signedInteger;
        case editor::EditorPreferenceValueType::UnsignedBitMask:
            return left.value.unsignedBitMask == right.value.unsignedBitMask;
        case editor::EditorPreferenceValueType::Utf8String:
            return left.value.string.byteCount == right.value.string.byteCount &&
                   std::memcmp(left.value.string.bytes, right.value.string.bytes,
                               left.value.string.byteCount) == 0;
        default:
            return false;
        }
    }

    struct PreferenceProviderRecord final
    {
        editor::EditorExtensionOwner owner;
        editor::EditorPreferenceProviderId providerId = 0;
        editor::EditorPreferenceProviderToken token;
        editor::EditorPreferenceFieldDescriptor fields[editor::MaximumEditorPreferenceFieldsPerProvider]{};
        vanguard::u32 fieldCount = 0;
        editor::EditorPreferenceQueryRevision queryRevision = nullptr;
        editor::EditorPreferenceCapture capture = nullptr;
        editor::EditorPreferenceValidate validate = nullptr;
        editor::EditorPreferenceApply apply = nullptr;
        const editor::EditorPreferenceProviderPresentation* presentation = nullptr;
        vanguard::u64 baselineRevision = 0;
        bool reachable = false;
        bool active = false;
    };

    class ManagedEditorShellService final : public editor::detail::EditorShellPersistencePrivateService,
                                            private editor::EditorExtensionRegistrationAccess
    {
    public:
        explicit ManagedEditorShellService(const editor::EditorShellServiceConfig* const config) noexcept
            : m_extensions(config != nullptr ? config->extensions : vanguard::containers::ArraySpan<const editor::EditorExtensionDescriptor>{})
        {
        }

        ~ManagedEditorShellService() override
        {
            if (vanguard::concurrency::IsMainThread())
            {
                m_registrationAdmissionOpen = false;
                static_cast<void>(DetachSurfaceCommandBridge());
                static_cast<void>(DetachPanelFocusBridge());
                static_cast<void>(DeactivateAll());
            }
        }

        [[nodiscard]] editor::EditorShellStatus GetStatus() const noexcept override
        {
            return m_status;
        }

        [[nodiscard]] bool CaptureTypedPreferences(
            editor::EditorTypedPreferenceRecord* const records, const vanguard::u32 capacity,
            vanguard::u32& count, editor::EditorPreferenceFailure* const failure) const noexcept override
        {
            ClearPreferenceFailure(failure);
            count = 0;
            if (!CanUsePreferences(failure))
                return false;
            if (m_typedPreferenceRevision == 0)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::GenerationExhausted,
                                      "editor typed-preference revision space is exhausted");

            for (const PreferenceProviderRecord& provider : m_preferenceProviders)
                if (provider.active && provider.reachable)
                    count += provider.fieldCount;
            if (count > capacity || (count != 0 && records == nullptr))
                return FailPreference(failure,
                                      count > capacity ? editor::EditorPreferenceFailureCode::CapacityExceeded
                                                       : editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                      "editor typed-preference capture destination is insufficient");

            vanguard::u32 write = 0;
            for (const PreferenceProviderRecord& provider : m_preferenceProviders)
            {
                if (!provider.active || !provider.reachable)
                    continue;
                void* privateState = nullptr;
                if (!ResolvePrivateState(provider.owner, privateState))
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::CallbackFailure,
                                          "editor typed-preference provider private state is unavailable",
                                          provider.owner.extension, provider.providerId);
                for (vanguard::u32 fieldIndex = 0; fieldIndex < provider.fieldCount; ++fieldIndex)
                {
                    const editor::EditorPreferenceFieldDescriptor& field = provider.fields[fieldIndex];
                    editor::EditorPreferenceFieldValue value;
                    value.type = field.type;
                    if (!provider.capture(privateState, field.fieldId, value) ||
                        !ValueMatchesField(field, value))
                    {
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::CallbackFailure,
                                              "editor typed-preference capture callback returned an invalid value",
                                              provider.owner.extension, provider.providerId, field.fieldId);
                    }
                    m_preferenceCaptureScratch[write++] =
                        {provider.owner.extension, provider.providerId, field.fieldId, value};
                }
            }

            for (vanguard::u32 index = 1; index < count; ++index)
            {
                const editor::EditorTypedPreferenceRecord value = m_preferenceCaptureScratch[index];
                vanguard::u32 insertion = index;
                while (insertion != 0)
                {
                    const editor::EditorTypedPreferenceRecord& previous =
                        m_preferenceCaptureScratch[insertion - 1u];
                    if (PreferenceKeyLessOrEqual(previous, value))
                        break;
                    m_preferenceCaptureScratch[insertion] = previous;
                    --insertion;
                }
                m_preferenceCaptureScratch[insertion] = value;
            }
            for (vanguard::u32 index = 0; index < count; ++index)
                records[index] = m_preferenceCaptureScratch[index];
            return true;
        }

        [[nodiscard]] bool ValidateTypedPreferences(
            const vanguard::containers::ArraySpan<const editor::EditorTypedPreferenceRecord> records,
            const vanguard::containers::ArraySpan<editor::EditorTypedPreferenceItemResult> results,
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            ClearPreferenceFailure(failure);
            InvalidatePreferenceValidation();
            if (!CanUsePreferences(failure))
                return false;
            if (records.Size() > editor::MaximumEditorLivePreferenceFields ||
                records.Size() != results.Size() ||
                (!records.Empty() && (records.Data() == nullptr || results.Data() == nullptr)))
                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                      "editor typed-preference validation batch is invalid");

            for (vanguard::u32 index = 0; index < results.Size(); ++index)
                results[index] = editor::EditorTypedPreferenceItemResult::Invalid;

            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorTypedPreferenceRecord& record = records[index];
                if (!record.extension.IsValid() || record.providerId == 0 || record.fieldId == 0 ||
                    !ValidPreferenceType(record.value.type))
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                          "editor typed-preference record identity or type is invalid",
                                          record.extension, record.providerId, record.fieldId);
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (SamePreferenceKey(records[previous], record))
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                              "editor typed-preference validation batch contains a duplicate key",
                                              record.extension, record.providerId, record.fieldId);
            }

            bool valid = true;
            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorTypedPreferenceRecord& record = records[index];
                PreferenceProviderRecord* const provider =
                    FindPreferenceProvider(record.extension, record.providerId);
                if (provider == nullptr)
                {
                    results[index] = editor::EditorTypedPreferenceItemResult::MissingProvider;
                    continue;
                }
                const editor::EditorPreferenceFieldDescriptor* const field =
                    FindPreferenceField(*provider, record.fieldId);
                if (field == nullptr)
                {
                    results[index] = editor::EditorTypedPreferenceItemResult::MissingField;
                    continue;
                }
                if (!ValueMatchesField(*field, record.value))
                {
                    valid = false;
                    results[index] = editor::EditorTypedPreferenceItemResult::Invalid;
                    if (failure != nullptr && failure->code == editor::EditorPreferenceFailureCode::None)
                        static_cast<void>(FailPreference(
                            failure, editor::EditorPreferenceFailureCode::ValidationFailed,
                            "editor typed-preference value does not match the live field descriptor",
                            record.extension, record.providerId, record.fieldId));
                    continue;
                }
                void* privateState = nullptr;
                const bool ownerFound = ResolvePrivateState(provider->owner, privateState);
                if (!ownerFound || !provider->validate(privateState, record.fieldId, record.value))
                {
                    valid = false;
                    results[index] = editor::EditorTypedPreferenceItemResult::Invalid;
                    if (failure != nullptr && failure->code == editor::EditorPreferenceFailureCode::None)
                        static_cast<void>(FailPreference(
                            failure, !ownerFound ? editor::EditorPreferenceFailureCode::CallbackFailure
                                                : editor::EditorPreferenceFailureCode::ValidationFailed,
                            "editor typed-preference provider rejected a restored value",
                            record.extension, record.providerId, record.fieldId));
                    continue;
                }
                results[index] = editor::EditorTypedPreferenceItemResult::Valid;
            }
            if (!valid)
                return false;

            m_validatedPreferenceCount = records.Size();
            m_validatedProviderSetSerial = m_providerSetSerial;
            for (vanguard::u32 index = 0; index < records.Size(); ++index)
                m_validatedPreferences[index] = records[index];
            m_preferenceValidationReady = true;
            return true;
        }

        [[nodiscard]] bool ApplyTypedPreferences(
            const vanguard::containers::ArraySpan<const editor::EditorTypedPreferenceRecord> records,
            const vanguard::containers::ArraySpan<editor::EditorTypedPreferenceItemResult> results,
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            ClearPreferenceFailure(failure);
            if (!CanUsePreferences(failure) || records.Size() != results.Size() ||
                !m_preferenceValidationReady || records.Size() != m_validatedPreferenceCount ||
                m_providerSetSerial != m_validatedProviderSetSerial)
            {
                InvalidatePreferenceValidation();
                return FailPreference(failure, editor::EditorPreferenceFailureCode::TransactionMismatch,
                                      "editor typed-preference apply does not match the validated transaction");
            }
            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorTypedPreferenceRecord& expected = m_validatedPreferences[index];
                const editor::EditorTypedPreferenceRecord& actual = records[index];
                if (!SamePreferenceKey(expected, actual) ||
                    !SamePreferenceValue(expected.value, actual.value))
                {
                    InvalidatePreferenceValidation();
                    return FailPreference(
                        failure, editor::EditorPreferenceFailureCode::TransactionMismatch,
                        "editor typed-preference apply records differ from the validated transaction",
                        actual.extension, actual.providerId, actual.fieldId);
                }
            }

            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorTypedPreferenceRecord& record = records[index];
                PreferenceProviderRecord* const provider =
                    FindPreferenceProvider(record.extension, record.providerId);
                if (provider == nullptr)
                {
                    results[index] = editor::EditorTypedPreferenceItemResult::MissingProvider;
                    continue;
                }
                const editor::EditorPreferenceFieldDescriptor* const field =
                    FindPreferenceField(*provider, record.fieldId);
                if (field == nullptr)
                {
                    results[index] = editor::EditorTypedPreferenceItemResult::MissingField;
                    continue;
                }
                void* privateState = nullptr;
                if (!ResolvePrivateState(provider->owner, privateState) ||
                    !ValueMatchesField(*field, record.value))
                {
                    InvalidatePreferenceValidation();
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::TransactionMismatch,
                                          "editor typed-preference validated owner disappeared before apply",
                                          record.extension, record.providerId, record.fieldId);
                }
                provider->apply(privateState, record.fieldId, record.value);
                results[index] = editor::EditorTypedPreferenceItemResult::Valid;
            }
            InvalidatePreferenceValidation();
            return true;
        }

        [[nodiscard]] bool RefreshTypedPreferenceRevision(
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            ClearPreferenceFailure(failure);
            if (!CanUsePreferences(failure))
                return false;
            if (m_typedPreferenceRevision == 0)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::GenerationExhausted,
                                      "editor typed-preference revision space is exhausted");

            vanguard::u64 revisions[editor::MaximumEditorPreferenceProviders]{};
            bool changed = m_providerSetDirty;
            for (vanguard::u32 index = 0; index < m_preferenceProviders.Size(); ++index)
            {
                PreferenceProviderRecord& provider = m_preferenceProviders[index];
                if (!provider.active || !provider.reachable)
                    continue;
                void* privateState = nullptr;
                const bool ownerFound = ResolvePrivateState(provider.owner, privateState);
                const vanguard::u64 revision =
                    ownerFound ? provider.queryRevision(privateState) : 0;
                if (revision == 0)
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::CallbackFailure,
                                          "editor typed-preference provider returned an invalid revision",
                                          provider.owner.extension, provider.providerId);
                revisions[index] = revision;
                changed = changed || provider.baselineRevision != revision;
            }

            if (changed)
            {
                if (m_typedPreferenceRevision == ~vanguard::u64{0})
                {
                    m_typedPreferenceRevision = 0;
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::GenerationExhausted,
                                          "editor typed-preference revision space is exhausted");
                }
                ++m_typedPreferenceRevision;
            }
            for (vanguard::u32 index = 0; index < m_preferenceProviders.Size(); ++index)
                if (m_preferenceProviders[index].active && m_preferenceProviders[index].reachable)
                    m_preferenceProviders[index].baselineRevision = revisions[index];
            m_providerSetDirty = false;
            return true;
        }

        [[nodiscard]] vanguard::u64 GetTypedPreferenceRevision() const noexcept override
        {
            return m_typedPreferenceRevision;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell initialization requires the main thread");
            ResetDerivedState();
            m_workspace = static_cast<editor::ProjectWorkspaceService*>(context.Find(editor::ProjectWorkspaceServiceId));
            auto* const ui = static_cast<editor::EditorUiService*>(context.FindCapability(editor::EditorUiCapabilityId));
            m_fileDialogs = editor::FindEditorFileDialogService(context);
            if (m_workspace == nullptr || ui == nullptr)
                return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr, "editor shell dependencies are unavailable during initialization");
            m_ui = ui;
            m_panels = &ui->GetPanels();
            m_commands = editor::FindEditorCommandService(context);
            m_activityPrivate = static_cast<editor::detail::EditorActivityPrivateService*>(
                context.FindCapability(editor::EditorActivityCapabilityId));
            m_activity = m_activityPrivate;
            if (m_commands == nullptr || m_commands->GetStatus().state != editor::EditorCommandServiceState::Running)
                return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr,
                            "editor shell command domain is unavailable during initialization");
            if (m_activity == nullptr || m_activityPrivate == nullptr)
                return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr,
                            "editor shell activity domain is unavailable during initialization");

            editor::EditorExtensionFailure failure;
            if (!m_catalog.Initialize(m_extensions, &failure))
            {
                m_status.failure = failure;
                m_status.state = editor::EditorShellState::Failed;
                m_status.catalogCount = m_catalog.GetStatus().descriptorCount;
                return app::LifecycleStatus::Failure("editor extension catalog validation failed");
            }

            m_status.catalogCount = m_catalog.GetStatus().descriptorCount;
            if (!m_catalog.ResolveSelection(m_workspace->GetProject().plugins, m_selection, &failure))
            {
                m_status.failure = failure;
                m_status.state = editor::EditorShellState::Failed;
                return app::LifecycleStatus::Failure("editor extension project selection failed");
            }

            m_status.selectedCount = m_selection.Size();
            if (m_commands != nullptr)
            {
                editor::EditorUiFailure uiFailure;
                if (!RegisterPanelFocusSink(*m_ui, OnPanelFocus, this, m_panelFocusSinkToken, &uiFailure))
                    return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr,
                                uiFailure.message != nullptr ? uiFailure.message : "editor panel focus bridge registration failed");

                const editor::detail::EditorUiCommandBridge bridge{
                    QuerySurfaceCommandPresentation, InvokeSurfaceCommand, ValidateSurfaceCommandOwner, this};
                if (!RegisterSurfaceCommandBridge(*m_ui, bridge, m_surfaceCommandBridgeToken, &uiFailure))
                {
                    editor::EditorUiFailure cleanupFailure;
                    if (!UnregisterPanelFocusSink(*m_ui, m_panelFocusSinkToken, &cleanupFailure))
                        return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                                    cleanupFailure.message != nullptr ? cleanupFailure.message
                                                                      : "editor shell partial bridge cleanup failed");
                    m_panelFocusSinkToken = {};
                    return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr,
                                uiFailure.message != nullptr ? uiFailure.message
                                                             : "editor surface command bridge registration failed");
                }
            }
            m_status.state = editor::EditorShellState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell activation requires the main thread");
            if (m_status.state != editor::EditorShellState::Ready || m_workspace == nullptr)
                return Fail(editor::EditorExtensionFailureCode::WrongPhase, nullptr, "editor shell start requires a validated selection");

            const vanguard::u64 generation = NextActivationGeneration();
            if (generation == 0)
                return Fail(editor::EditorExtensionFailureCode::CapacityExceeded, nullptr, "editor shell activation generation is exhausted");

            m_status.activationGeneration = generation;
            m_status.state = editor::EditorShellState::Activating;
            m_registrationAdmissionOpen = true;
            for (const editor::EditorExtensionDescriptor* const descriptor : m_selection.GetActivationOrder())
            {
                m_status.currentExtension = descriptor->id;
                m_status.currentTechnicalName = descriptor->technicalName;
                const editor::EditorExtensionOwner owner{descriptor->id, generation};
                if (!BeginOwner(owner))
                {
                    m_registrationAdmissionOpen = false;
                    static_cast<void>(DeactivateAll());
                    return Fail(editor::EditorExtensionFailureCode::CapacityExceeded, descriptor,
                                "editor extension owner capacity is exhausted");
                }
                void* privateState = nullptr;
                editor::EditorExtensionActivationContext activation =
                    editor::EditorShellServiceAccess::MakeContext(descriptor->id, generation, *m_workspace,
                                                                  *m_activity, *m_ui, m_fileDialogs, *this);
                if (!descriptor->activate(activation, descriptor->data, privateState))
                {
                    m_registrationAdmissionOpen = false;
                    ActiveExtension failed{descriptor, privateState, owner};
                    if (!DeactivateOne(failed))
                    {
                        // Preserve the failed activation's private state and owner tuple for the
                        // same retry-safe cleanup path as already-active extensions. Dropping this
                        // local after a cleanup failure would strand live callback/factoryData.
                        m_active.PushBack(failed);
                        m_status.activeCount = m_active.Size();
                    }
                    m_status.failure.code = editor::EditorExtensionFailureCode::ActivationFailure;
                    m_status.failure.extension = descriptor->id;
                    m_status.failure.technicalName = descriptor->technicalName;
                    VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "editor extension activation failed: id=%llu name=%s",
                                 static_cast<unsigned long long>(descriptor->id.value), descriptor->technicalName);
                    static_cast<void>(DeactivateAll());
                    m_status.state = editor::EditorShellState::Failed;
                    m_status.currentExtension = {};
                    m_status.currentTechnicalName = nullptr;
                    return app::LifecycleStatus::Failure("editor extension activation failed");
                }

                m_active.PushBack({descriptor, privateState, owner});
                m_status.activeCount = m_active.Size();
            }

            m_status.currentExtension = {};
            m_status.currentTechnicalName = nullptr;
            m_status.state = editor::EditorShellState::Active;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell quiesce requires the main thread");
            m_registrationAdmissionOpen = false;
            if (!DetachSurfaceCommandBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell surface command bridge cleanup failed");
            if (!DetachPanelFocusBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell panel focus bridge cleanup failed");
            if (!DeactivateAll())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell extension-owned registration cleanup failed");
            if (m_status.state != editor::EditorShellState::Failed)
                m_status.state = editor::EditorShellState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell drain requires the main thread");
            if (!OwnedStateEmpty())
            {
                m_status.state = editor::EditorShellState::Failed;
                if (m_status.failure.code == editor::EditorExtensionFailureCode::None)
                    m_status.failure.code = editor::EditorExtensionFailureCode::CleanupFailure;
                return app::LifecycleStatus::Failure("editor shell drain found live extension-owned state");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell stop requires the main thread");
            m_registrationAdmissionOpen = false;
            if (!DetachSurfaceCommandBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell stop could not detach the surface command bridge");
            if (!DetachPanelFocusBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell stop could not detach the panel focus bridge");
            return DeactivateAll() ? app::LifecycleStatus::Success()
                                   : Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                                          "editor shell stop could not clear extension-owned state");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(editor::EditorExtensionFailureCode::WrongThread, nullptr,
                            "editor shell shutdown requires the main thread");
            m_registrationAdmissionOpen = false;
            if (!DetachSurfaceCommandBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell shutdown could not detach the surface command bridge");
            if (!DetachPanelFocusBridge())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell shutdown could not detach the panel focus bridge");
            if (!DeactivateAll())
                return Fail(editor::EditorExtensionFailureCode::CleanupFailure, nullptr,
                            "editor shell shutdown could not clear extension-owned state");
            ClearOwnedState();
            m_workspace = nullptr;
            m_ui = nullptr;
            m_fileDialogs = nullptr;
            m_commands = nullptr;
            m_activity = nullptr;
            m_activityPrivate = nullptr;
            m_panels = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] app::LifecycleStatus Fail(const editor::EditorExtensionFailureCode code,
                                                const editor::EditorExtensionDescriptor* const descriptor, const char* const message) noexcept
        {
            m_status.state = editor::EditorShellState::Failed;
            m_status.failure.code = code;
            if (descriptor != nullptr)
            {
                m_status.failure.extension = descriptor->id;
                m_status.failure.technicalName = descriptor->technicalName;
            }
            return app::LifecycleStatus::Failure(message);
        }

        void ResetDerivedState() noexcept
        {
            m_catalog = {};
            m_selection = {};
            m_active.Clear();
            ClearOwnedState();
            m_status = {};
            m_workspace = nullptr;
            m_ui = nullptr;
            m_fileDialogs = nullptr;
            m_commands = nullptr;
            m_activity = nullptr;
            m_activityPrivate = nullptr;
            m_panels = nullptr;
            m_surfaceCommandBridgeToken = {};
            m_panelFocusSinkToken = {};
            m_panelLocalToken = {};
            m_panelLocalOwner = {};
            m_focusedPanel = {};
        }

        [[nodiscard]] bool BeginOwner(const editor::EditorExtensionOwner owner) noexcept
        {
            if (!owner.IsValid() || m_owners.Size() >= editor::MaximumEditorExtensions)
                return false;
            for (const ExtensionOwnerRecord& record : m_owners)
                if (record.owner == owner)
                    return false;
            m_owners.PushBack({owner, true});
            return true;
        }

        [[nodiscard]] ExtensionOwnerRecord* FindOwner(const editor::EditorExtensionOwner owner) noexcept
        {
            for (ExtensionOwnerRecord& record : m_owners)
                if (record.owner == owner)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] const ExtensionOwnerRecord* FindOwner(const editor::EditorExtensionOwner owner) const noexcept
        {
            for (const ExtensionOwnerRecord& record : m_owners)
                if (record.owner == owner)
                    return &record;
            return nullptr;
        }

        static void ClearPreferenceFailure(editor::EditorPreferenceFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool FailPreference(
            editor::EditorPreferenceFailure* const failure,
            const editor::EditorPreferenceFailureCode code,
            const char* const message,
            const editor::EditorExtensionId extension = {},
            const editor::EditorPreferenceProviderId providerId = 0,
            const editor::EditorPreferenceFieldId fieldId = 0) const noexcept
        {
            if (failure != nullptr)
                *failure = {code, extension, providerId, fieldId, message};
            return false;
        }

        [[nodiscard]] bool CanUsePreferences(editor::EditorPreferenceFailure* const failure) const noexcept
        {
            if (!vanguard::concurrency::IsMainThread())
                return FailPreference(failure, editor::EditorPreferenceFailureCode::WrongThread,
                                      "editor typed preferences require the main thread");
            if (m_status.state != editor::EditorShellState::Active)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::WrongPhase,
                                      "editor typed preferences require an active shell");
            return true;
        }

        [[nodiscard]] bool CanRegisterPreferenceDomain(
            const editor::EditorExtensionOwner owner, editor::EditorPreferenceFailure* const failure,
            const editor::EditorPreferenceProviderId providerId) const noexcept
        {
            ClearPreferenceFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return FailPreference(failure, editor::EditorPreferenceFailureCode::WrongThread,
                                      "editor preference-provider registration requires the main thread",
                                      owner.extension, providerId);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::WrongPhase,
                                      "editor preference-provider registration is only available during shell activation",
                                      owner.extension, providerId);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::OwnerRevoked,
                                      "editor preference-provider owner is not live",
                                      owner.extension, providerId);
            if (m_registrationLedger.Size() >= editor::MaximumEditorExtensionOwnedRegistrations)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::CapacityExceeded,
                                      "editor extension owned-registration capacity is exhausted",
                                      owner.extension, providerId);
            return true;
        }

        [[nodiscard]] static bool SamePreferenceKey(
            const editor::EditorTypedPreferenceRecord& left,
            const editor::EditorTypedPreferenceRecord& right) noexcept
        {
            return left.extension == right.extension && left.providerId == right.providerId &&
                   left.fieldId == right.fieldId;
        }

        [[nodiscard]] static bool PreferenceKeyLessOrEqual(
            const editor::EditorTypedPreferenceRecord& left,
            const editor::EditorTypedPreferenceRecord& right) noexcept
        {
            if (left.extension.value != right.extension.value)
                return left.extension.value < right.extension.value;
            if (left.providerId != right.providerId)
                return left.providerId < right.providerId;
            return left.fieldId <= right.fieldId;
        }

        void InvalidatePreferenceValidation() noexcept
        {
            m_preferenceValidationReady = false;
            m_validatedPreferenceCount = 0;
            m_validatedProviderSetSerial = 0;
        }

        void AdvanceProviderSetSerial() noexcept
        {
            if (m_providerSetSerial == 0)
                return;
            if (m_providerSetSerial == ~vanguard::u64{0})
                m_providerSetSerial = 0;
            else
                ++m_providerSetSerial;
            InvalidatePreferenceValidation();
        }

        [[nodiscard]] PreferenceProviderRecord* FindPreferenceProvider(
            const editor::EditorExtensionId extension,
            const editor::EditorPreferenceProviderId providerId) noexcept
        {
            for (PreferenceProviderRecord& provider : m_preferenceProviders)
                if (provider.active && provider.reachable && provider.owner.extension == extension &&
                    provider.providerId == providerId)
                    return &provider;
            return nullptr;
        }

        [[nodiscard]] const PreferenceProviderRecord* FindPreferenceProvider(
            const editor::EditorExtensionId extension,
            const editor::EditorPreferenceProviderId providerId) const noexcept
        {
            for (const PreferenceProviderRecord& provider : m_preferenceProviders)
                if (provider.active && provider.reachable && provider.owner.extension == extension &&
                    provider.providerId == providerId)
                    return &provider;
            return nullptr;
        }

        [[nodiscard]] const PreferenceProviderRecord* FindPreferenceProvider(
            const editor::EditorPreferenceProviderToken token) const noexcept
        {
            return token.IsValid() && token.index < m_preferenceProviders.Size() &&
                           m_preferenceProviders[token.index].active &&
                           m_preferenceProviders[token.index].token == token
                       ? &m_preferenceProviders[token.index]
                       : nullptr;
        }

        [[nodiscard]] PreferenceProviderRecord* FindPreferenceProvider(
            const editor::EditorPreferenceProviderToken token) noexcept
        {
            return token.IsValid() && token.index < m_preferenceProviders.Size() &&
                           m_preferenceProviders[token.index].active &&
                           m_preferenceProviders[token.index].token == token
                       ? &m_preferenceProviders[token.index]
                       : nullptr;
        }

        [[nodiscard]] static const editor::EditorPreferenceFieldDescriptor* FindPreferenceField(
            const PreferenceProviderRecord& provider, const editor::EditorPreferenceFieldId fieldId) noexcept
        {
            for (vanguard::u32 index = 0; index < provider.fieldCount; ++index)
                if (provider.fields[index].fieldId == fieldId)
                    return &provider.fields[index];
            return nullptr;
        }

        [[nodiscard]] bool ResolvePrivateState(const editor::EditorExtensionOwner owner,
                                               void*& privateState) const noexcept
        {
            privateState = nullptr;
            for (const ActiveExtension& active : m_active)
                if (active.owner == owner)
                {
                    privateState = active.privateState;
                    return true;
                }
            return false;
        }

        [[nodiscard]] bool RemovePreferenceProviderRecord(
            const editor::EditorExtensionOwner owner,
            const editor::EditorPreferenceProviderToken token,
            editor::EditorPreferenceFailure* const failure = nullptr) noexcept
        {
            PreferenceProviderRecord* const provider = FindPreferenceProvider(token);
            if (provider == nullptr)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidToken,
                                      "editor preference-provider token is invalid", owner.extension);
            if (!(provider->owner == owner))
                return FailPreference(failure, editor::EditorPreferenceFailureCode::OwnerRevoked,
                                      "editor preference-provider token belongs to another owner",
                                      owner.extension, provider->providerId);
            const bool wasReachable = provider->reachable;
            m_livePreferenceFieldCount -= provider->fieldCount;
            provider->reachable = false;
            provider->active = false;
            provider->fieldCount = 0;
            provider->providerId = 0;
            provider->token = {};
            provider->queryRevision = nullptr;
            provider->capture = nullptr;
            provider->validate = nullptr;
            provider->apply = nullptr;
            provider->baselineRevision = 0;
            if (wasReachable)
            {
                m_providerSetDirty = true;
                AdvanceProviderSetSerial();
            }
            return true;
        }

        void RevokePreferenceOwner(const editor::EditorExtensionOwner owner) noexcept
        {
            bool changed = false;
            for (PreferenceProviderRecord& provider : m_preferenceProviders)
            {
                if (!provider.active || !provider.reachable || !(provider.owner == owner))
                    continue;
                provider.reachable = false;
                changed = true;
            }
            if (changed)
            {
                m_providerSetDirty = true;
                AdvanceProviderSetSerial();
            }
        }

        [[nodiscard]] bool RejectCommandRegistration(editor::EditorCommandFailure* const failure,
                                                     const editor::EditorCommandFailureCode code,
                                                     const char* const message,
                                                     const editor::EditorCommandId command = {},
                                                     const editor::EditorCommandContextTypeId context = {}) const noexcept
        {
            if (failure != nullptr)
                *failure = {code, command, context, {}, {}, message};
            return false;
        }

        [[nodiscard]] bool CanRegisterCommandDomain(const editor::EditorExtensionOwner owner,
                                                    editor::EditorCommandFailure* const failure,
                                                    const editor::EditorCommandId command = {},
                                                    const editor::EditorCommandContextTypeId context = {}) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectCommandRegistration(failure, editor::EditorCommandFailureCode::WrongThread,
                                                 "editor extension command registration requires the main thread", command, context);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating ||
                m_commands == nullptr || m_commands->GetStatus().state != editor::EditorCommandServiceState::Running)
                return RejectCommandRegistration(failure, editor::EditorCommandFailureCode::WrongPhase,
                                                 "editor extension command registration is only available during shell activation",
                                                 command, context);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return RejectCommandRegistration(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                                                 "editor extension command owner is not live", command, context);
            if (m_registrationLedger.Size() >= editor::MaximumEditorExtensionOwnedRegistrations)
                return RejectCommandRegistration(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                                                 "editor extension owned-registration capacity is exhausted", command, context);
            return true;
        }

        [[nodiscard]] bool RevokeOwner(const editor::EditorExtensionOwner owner) noexcept
        {
            // Extension private state may be the backing storage for a panel persistence callback.
            // Reject the entire owner teardown before mutating any domain while such a callback is
            // on the stack; partial revoke followed by extension deactivation would invalidate the
            // callback's panel/factoryData before it returns.
            if (m_panels != nullptr && !CanRevokePanelOwner(*m_panels, owner))
                return false;
            ExtensionOwnerRecord* const record = FindOwner(owner);
            if (record != nullptr)
                record->live = false;
            RevokePreferenceOwner(owner);
            bool success = true;
            if (m_activityPrivate != nullptr)
            {
                editor::EditorActivityFailure activityFailure;
                success = RevokeActivityOwner(*m_activityPrivate, owner, &activityFailure);
            }
            if (m_ui != nullptr)
                RevokeSurfaceOwner(*m_ui, owner);
            if (m_commands != nullptr)
            {
                RevokeCommandOwner(*m_commands, owner);
                if (m_panelLocalOwner == owner)
                {
                    m_panelLocalToken = {};
                    m_panelLocalOwner = {};
                }
            }
            if (m_panels != nullptr)
                RevokePanelOwner(*m_panels, owner);
            return success;
        }

        [[nodiscard]] bool RejectActivityRegistration(editor::EditorActivityFailure* const failure,
                                                      const editor::EditorActivityFailureCode code,
                                                      const char* const message,
                                                      const editor::EditorActivitySourceId source) const noexcept
        {
            if (failure != nullptr)
                *failure = {code, source, editor::InvalidEditorActivityItemId, message};
            return false;
        }

        [[nodiscard]] bool CanRegisterActivityDomain(const editor::EditorExtensionOwner owner,
                                                     editor::EditorActivityFailure* const failure,
                                                     const editor::EditorActivitySourceId source) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectActivityRegistration(failure, editor::EditorActivityFailureCode::WrongThread,
                                                  "editor extension activity registration requires the main thread",
                                                  source);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating ||
                m_activityPrivate == nullptr || m_activity == nullptr ||
                m_activity->GetStatus().state != editor::EditorActivityServiceState::Ready)
                return RejectActivityRegistration(
                    failure, editor::EditorActivityFailureCode::NotReady,
                    "editor extension activity registration is only available during shell activation", source);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return RejectActivityRegistration(failure, editor::EditorActivityFailureCode::OwnerMismatch,
                                                  "editor extension activity owner is not live", source);
            if (m_registrationLedger.Size() >= editor::MaximumEditorExtensionOwnedRegistrations)
                return RejectActivityRegistration(failure, editor::EditorActivityFailureCode::CapacityExceeded,
                                                  "editor extension owned-registration capacity is exhausted",
                                                  source);
            return true;
        }

        [[nodiscard]] bool RejectPanelRegistration(editor::EditorUiFailure* const failure,
                                                   const editor::EditorUiFailureCode code,
                                                   const char* const message,
                                                   const editor::EditorPanelTypeId type) const noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = code;
                failure->panelType = type;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool CanRegisterPanelDomain(const editor::EditorExtensionOwner owner,
                                                  editor::EditorUiFailure* const failure,
                                                  const editor::EditorPanelTypeId type) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::WrongThread,
                                               "editor extension panel registration requires the main thread", type);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating || m_panels == nullptr)
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::WrongPhase,
                                               "editor extension panel registration is only available during shell activation", type);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::OwnerRevoked,
                                               "editor extension panel owner is not live", type);
            if (m_registrationLedger.Size() >= editor::MaximumEditorExtensionOwnedRegistrations)
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::CapacityExceeded,
                                               "editor extension owned-registration capacity is exhausted", type);
            return true;
        }

        [[nodiscard]] bool RejectSurfaceRegistration(editor::EditorUiFailure* const failure,
                                                     const editor::EditorUiFailureCode code,
                                                     const char* const message,
                                                     const editor::EditorUiSurfaceId surface = {},
                                                     const editor::EditorUiContributionId contribution = {}) const noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = code;
                failure->surface = surface;
                failure->contribution = contribution;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool CanRegisterSurfaceDomain(const editor::EditorExtensionOwner owner,
                                                   editor::EditorUiFailure* const failure,
                                                   const editor::EditorUiSurfaceId surface = {},
                                                   const editor::EditorUiContributionId contribution = {}) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectSurfaceRegistration(failure, editor::EditorUiFailureCode::WrongThread,
                                                "editor extension surface registration requires the main thread",
                                                surface, contribution);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating ||
                m_ui == nullptr || m_ui->GetState() != editor::EditorUiState::Ready)
                return RejectSurfaceRegistration(failure, editor::EditorUiFailureCode::WrongPhase,
                                                "editor extension surface registration is only available during shell activation",
                                                surface, contribution);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return RejectSurfaceRegistration(failure, editor::EditorUiFailureCode::OwnerRevoked,
                                                "editor extension surface owner is not live", surface, contribution);
            if (m_registrationLedger.Size() >= editor::MaximumEditorExtensionOwnedRegistrations)
                return RejectSurfaceRegistration(failure, editor::EditorUiFailureCode::CapacityExceeded,
                                                "editor extension owned-registration capacity is exhausted",
                                                surface, contribution);
            return true;
        }

        [[nodiscard]] bool RegisterCommand(const editor::EditorExtensionOwner owner, const editor::EditorCommandDescriptor& descriptor,
                                           editor::EditorCommandToken& token, editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterCommandDomain(owner, failure, descriptor.id))
                return false;
            if (!RegisterOwnedCommand(*m_commands, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::Command;
            registration.command = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterContextType(const editor::EditorExtensionOwner owner,
                                               const editor::EditorCommandContextTypeDescriptor& descriptor,
                                               editor::EditorCommandContextTypeToken& token,
                                               editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterCommandDomain(owner, failure, {}, descriptor.id))
                return false;
            if (!RegisterOwnedContextType(*m_commands, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::ContextType;
            registration.contextType = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterHandler(const editor::EditorExtensionOwner owner,
                                           const editor::EditorCommandHandlerDescriptor& descriptor,
                                           editor::EditorCommandHandlerToken& token,
                                           editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterCommandDomain(owner, failure, descriptor.command, descriptor.contextType))
                return false;
            if (!RegisterOwnedHandler(*m_commands, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::Handler;
            registration.handler = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterPanelType(const editor::EditorExtensionOwner owner,
                                            const editor::EditorPanelTypeDesc& descriptor,
                                            const editor::EditorPanelLocalContextBinding* const localContext,
                                            editor::EditorPanelTypeToken& token,
                                            editor::EditorUiFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterPanelDomain(owner, failure, descriptor.id))
                return false;
            if (localContext != nullptr)
            {
                if (m_commands == nullptr || !localContext->contextType.IsValid() ||
                    static_cast<vanguard::u8>(localContext->mode) >
                        static_cast<vanguard::u8>(editor::EditorCommandContextMode::Override))
                    return RejectPanelRegistration(failure, editor::EditorUiFailureCode::InvalidContextAssociation,
                                                   "editor panel Local context association is invalid", descriptor.id);
                editor::EditorCommandFailure commandFailure;
                if (!ValidateOwnedLocalContext(*m_commands, owner, localContext->contextType, &commandFailure))
                    return RejectPanelRegistration(failure, editor::EditorUiFailureCode::InvalidContextAssociation,
                                                   commandFailure.message != nullptr ? commandFailure.message
                                                                                     : "editor panel Local context association is unavailable",
                                                   descriptor.id);
            }
            if (!RegisterOwnedPanelType(*m_panels, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::PanelType;
            registration.panelType = token;
            registration.panelTypeId = descriptor.id;
            if (localContext != nullptr)
            {
                registration.panelLocalContext = *localContext;
                registration.hasPanelLocalContext = true;
            }
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool CreateOwnedPanel(const editor::EditorExtensionOwner owner,
                                           const editor::EditorPanelDesc& descriptor,
                                           editor::EditorPanelHandle& panel,
                                           editor::EditorUiFailure* const failure) noexcept override
        {
            panel = {};
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::WrongThread,
                                               "editor extension panel creation requires the main thread",
                                               descriptor.type);
            if (!m_registrationAdmissionOpen || m_status.state != editor::EditorShellState::Activating ||
                m_panels == nullptr || m_ui == nullptr || m_ui->GetState() != editor::EditorUiState::Ready)
                return RejectPanelRegistration(
                    failure, editor::EditorUiFailureCode::WrongPhase,
                    "editor extension panel creation is only available during shell activation",
                    descriptor.type);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::OwnerRevoked,
                                               "editor extension panel owner is not live", descriptor.type);
            const OwnedRegistrationRecord* const registration = FindPanelRegistration(descriptor.type);
            if (registration == nullptr || !(registration->owner == owner))
                return RejectPanelRegistration(failure, editor::EditorUiFailureCode::OwnerMismatch,
                                               "editor extension panel type is not owned by this extension",
                                               descriptor.type);
            return m_panels->CreatePanel(descriptor, panel, failure);
        }

        [[nodiscard]] bool SetOwnedPanelOpen(const editor::EditorExtensionOwner owner,
                                            const editor::EditorPanelHandle panel,
                                            const bool open,
                                            editor::EditorUiFailure* const failure) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
            {
                if (failure != nullptr)
                    *failure = {editor::EditorUiFailureCode::WrongThread, editor::InvalidEditorPanelTypeId,
                                editor::InvalidEditorPanelInstanceId, panel, {}, {},
                                "editor extension panel visibility requires the main thread"};
                return false;
            }
            if (m_panels == nullptr || m_ui == nullptr ||
                (m_status.state != editor::EditorShellState::Active &&
                 m_status.state != editor::EditorShellState::Activating) ||
                (m_ui->GetState() != editor::EditorUiState::Ready &&
                 m_ui->GetState() != editor::EditorUiState::BuildingFrame))
            {
                if (failure != nullptr)
                    *failure = {editor::EditorUiFailureCode::WrongPhase, editor::InvalidEditorPanelTypeId,
                                editor::InvalidEditorPanelInstanceId, panel, {}, {},
                                "editor extension panel visibility is unavailable in the current phase"};
                return false;
            }
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
            {
                if (failure != nullptr)
                    *failure = {editor::EditorUiFailureCode::OwnerRevoked, editor::InvalidEditorPanelTypeId,
                                editor::InvalidEditorPanelInstanceId, panel, {}, {},
                                "editor extension panel owner is not live"};
                return false;
            }

            PanelLookup lookup;
            lookup.handle = panel;
            m_panels->VisitPanels(FindPanelByHandle, &lookup);
            if (!lookup.found)
            {
                if (failure != nullptr)
                    *failure = {editor::EditorUiFailureCode::InvalidHandle, editor::InvalidEditorPanelTypeId,
                                editor::InvalidEditorPanelInstanceId, panel, {}, {},
                                "editor extension panel handle is invalid"};
                return false;
            }
            const OwnedRegistrationRecord* const registration = FindPanelRegistration(lookup.info.type);
            if (registration == nullptr || !(registration->owner == owner))
            {
                if (failure != nullptr)
                    *failure = {editor::EditorUiFailureCode::OwnerMismatch, lookup.info.type,
                                lookup.info.id, panel, {}, {},
                                "editor extension panel handle is owned by another extension"};
                return false;
            }
            return m_panels->SetOpen(panel, open, failure);
        }

        [[nodiscard]] bool IsOwnedPanelOpen(const editor::EditorExtensionOwner owner,
                                           const editor::EditorPanelHandle panel) const noexcept override
        {
            if (!vanguard::concurrency::IsMainThread() || m_panels == nullptr ||
                !owner.IsValid())
                return false;
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return false;
            PanelLookup lookup;
            lookup.handle = panel;
            m_panels->VisitPanels(FindPanelByHandle, &lookup);
            if (!lookup.found)
                return false;
            const OwnedRegistrationRecord* const registration = FindPanelRegistration(lookup.info.type);
            return registration != nullptr && registration->owner == owner &&
                   m_panels->IsOpen(panel);
        }

        [[nodiscard]] bool RegisterContextMenuSurface(const editor::EditorExtensionOwner owner,
                                                     const editor::EditorUiSurfaceId surface,
                                                     editor::detail::EditorUiSurfaceToken& token,
                                                     editor::EditorUiFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterSurfaceDomain(owner, failure, surface))
                return false;
            const editor::detail::EditorUiSurfaceDesc descriptor{surface, editor::detail::EditorUiSurfaceKind::ContextMenu};
            if (!RegisterOwnedSurface(*m_ui, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::SurfaceRoot;
            registration.surface = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterSurfaceContribution(const editor::EditorExtensionOwner owner,
                                                      const editor::detail::EditorUiContributionDesc& descriptor,
                                                      editor::detail::EditorUiContributionToken& token,
                                                      editor::EditorUiFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterSurfaceDomain(owner, failure, descriptor.surface, descriptor.id))
                return false;
            if (descriptor.kind == editor::detail::EditorUiContributionKind::Command ||
                (descriptor.kind == editor::detail::EditorUiContributionKind::Tool && descriptor.command.IsValid()))
            {
                if (m_commands == nullptr)
                    return RejectSurfaceRegistration(failure, editor::EditorUiFailureCode::SurfaceCommandFailure,
                                                    "editor surface command domain is unavailable",
                                                    descriptor.surface, descriptor.id);
                editor::EditorCommandFailure commandFailure;
                if (!ValidateOwnedCommand(*m_commands, owner, descriptor.command, &commandFailure))
                    return RejectSurfaceRegistration(
                        failure, editor::EditorUiFailureCode::SurfaceCommandFailure,
                        commandFailure.message != nullptr ? commandFailure.message
                                                          : "editor surface contribution command is unavailable",
                        descriptor.surface, descriptor.id);
            }
            if (!RegisterOwnedContribution(*m_ui, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::SurfaceContribution;
            registration.contribution = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterActivitySource(
            const editor::EditorExtensionOwner owner,
            const editor::detail::EditorActivitySourceDescriptor& descriptor,
            editor::detail::EditorActivitySourceToken& token,
            editor::EditorActivityFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterActivityDomain(owner, failure, descriptor.id))
                return false;
            if (!RegisterOwnedActivitySource(*m_activityPrivate, owner, descriptor, token, failure))
                return false;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::ActivitySource;
            registration.activitySource = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool RegisterPreferenceProvider(
            const editor::EditorExtensionOwner owner,
            const editor::EditorPreferenceProviderDescriptor& descriptor,
            editor::EditorPreferenceProviderToken& token,
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            token = {};
            if (!CanRegisterPreferenceDomain(owner, failure, descriptor.providerId))
                return false;
            if (descriptor.providerId == 0 || descriptor.fields.Empty() ||
                descriptor.fields.Size() > editor::MaximumEditorPreferenceFieldsPerProvider ||
                descriptor.fields.Data() == nullptr || descriptor.queryRevision == nullptr ||
                descriptor.capture == nullptr || descriptor.validate == nullptr || descriptor.apply == nullptr)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                      "editor preference-provider descriptor is invalid",
                                      owner.extension, descriptor.providerId);
            if (m_providerSetSerial == 0 || m_providerSetSerial == ~vanguard::u64{0})
                return FailPreference(failure, editor::EditorPreferenceFailureCode::GenerationExhausted,
                                      "editor preference-provider set revision space is exhausted",
                                      owner.extension, descriptor.providerId);
            if (FindPreferenceProvider(owner.extension, descriptor.providerId) != nullptr)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::DuplicateProvider,
                                      "editor preference-provider identity is already registered",
                                      owner.extension, descriptor.providerId);
            if (m_livePreferenceFieldCount + descriptor.fields.Size() > editor::MaximumEditorLivePreferenceFields)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::CapacityExceeded,
                                      "editor live preference-field capacity is exhausted",
                                      owner.extension, descriptor.providerId);

            for (vanguard::u32 index = 0; index < descriptor.fields.Size(); ++index)
            {
                const editor::EditorPreferenceFieldDescriptor& field = descriptor.fields[index];
                if (field.fieldId == 0 || !ValidPreferenceType(field.type) ||
                    (field.type == editor::EditorPreferenceValueType::Utf8String &&
                     field.maximumStringBytes > editor::MaximumEditorPreferenceStringBytes))
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                          "editor preference-provider field descriptor is invalid",
                                          owner.extension, descriptor.providerId, field.fieldId);
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (descriptor.fields[previous].fieldId == field.fieldId)
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                              "editor preference-provider field IDs must be unique",
                                              owner.extension, descriptor.providerId, field.fieldId);
            }

            if (descriptor.presentation != nullptr)
            {
                const editor::EditorPreferenceProviderPresentation& presentation = *descriptor.presentation;
                if (presentation.categoryId == 0 || presentation.sectionId == 0 ||
                    presentation.categoryLabel == nullptr || presentation.categoryLabel[0] == '\0' ||
                    presentation.sectionLabel == nullptr || presentation.sectionLabel[0] == '\0' ||
                    presentation.fields.Empty() || presentation.fields.Size() > descriptor.fields.Size() ||
                    presentation.fields.Data() == nullptr)
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                          "editor preference presentation descriptor is invalid",
                                          owner.extension, descriptor.providerId);

                for (vanguard::u32 presentationIndex = 0; presentationIndex < presentation.fields.Size(); ++presentationIndex)
                {
                    const editor::EditorPreferenceFieldPresentation& visible = presentation.fields[presentationIndex];
                    const editor::EditorPreferenceFieldDescriptor* persisted = nullptr;
                    for (vanguard::u32 fieldIndex = 0; fieldIndex < descriptor.fields.Size(); ++fieldIndex)
                        if (descriptor.fields[fieldIndex].fieldId == visible.fieldId)
                        {
                            persisted = &descriptor.fields[fieldIndex];
                            break;
                        }
                    if (persisted == nullptr || visible.label == nullptr || visible.label[0] == '\0' || visible.step <= 0)
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                              "editor preference presentation field is invalid",
                                              owner.extension, descriptor.providerId, visible.fieldId);
                    for (vanguard::u32 previous = 0; previous < presentationIndex; ++previous)
                        if (presentation.fields[previous].fieldId == visible.fieldId)
                            return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                  "editor preference presentation field IDs must be unique",
                                                  owner.extension, descriptor.providerId, visible.fieldId);

                    switch (visible.control)
                    {
                    case editor::EditorPreferenceControlKind::Automatic:
                        break;
                    case editor::EditorPreferenceControlKind::Toggle:
                        if (persisted->type != editor::EditorPreferenceValueType::Bool)
                            return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                  "editor toggle preference presentation requires a bool field",
                                                  owner.extension, descriptor.providerId, visible.fieldId);
                        break;
                    case editor::EditorPreferenceControlKind::Choice:
                        if (persisted->type != editor::EditorPreferenceValueType::SignedInteger || visible.choices.Empty() ||
                            visible.choices.Data() == nullptr)
                            return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                  "editor choice preference presentation requires signed choices",
                                                  owner.extension, descriptor.providerId, visible.fieldId);
                        for (vanguard::u32 choice = 0; choice < visible.choices.Size(); ++choice)
                        {
                            if (visible.choices[choice].label == nullptr || visible.choices[choice].label[0] == '\0')
                                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                      "editor choice preference label is invalid",
                                                      owner.extension, descriptor.providerId, visible.fieldId);
                            for (vanguard::u32 previousChoice = 0; previousChoice < choice; ++previousChoice)
                                if (visible.choices[previousChoice].value == visible.choices[choice].value)
                                    return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                          "editor choice preference values must be unique",
                                                          owner.extension, descriptor.providerId, visible.fieldId);
                        }
                        break;
                    case editor::EditorPreferenceControlKind::SignedInteger:
                        if (persisted->type != editor::EditorPreferenceValueType::SignedInteger || visible.minimum > visible.maximum)
                            return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                  "editor integer preference presentation is invalid",
                                                  owner.extension, descriptor.providerId, visible.fieldId);
                        break;
                    case editor::EditorPreferenceControlKind::Text:
                        if (persisted->type != editor::EditorPreferenceValueType::Utf8String)
                            return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                                  "editor text preference presentation requires a string field",
                                                  owner.extension, descriptor.providerId, visible.fieldId);
                        break;
                    }
                }

                for (const PreferenceProviderRecord& existing : m_preferenceProviders)
                {
                    if (!existing.active || !existing.reachable || existing.presentation == nullptr)
                        continue;
                    const editor::EditorPreferenceProviderPresentation& current = *existing.presentation;
                    if (current.categoryId == presentation.categoryId &&
                        (current.categoryOrder != presentation.categoryOrder ||
                         std::strcmp(current.categoryLabel, presentation.categoryLabel) != 0))
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                              "editor preference category identity has conflicting presentation metadata",
                                              owner.extension, descriptor.providerId);
                    if (current.sectionId == presentation.sectionId)
                        return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                              "editor preference section identity is already registered",
                                              owner.extension, descriptor.providerId);
                }
            }

            vanguard::u32 slot = ~vanguard::u32{0};
            for (vanguard::u32 index = 0; index < m_preferenceProviders.Size(); ++index)
                if (!m_preferenceProviders[index].active)
                {
                    slot = index;
                    break;
                }
            if (slot == ~vanguard::u32{0})
            {
                if (m_preferenceProviders.Size() >= editor::MaximumEditorPreferenceProviders)
                    return FailPreference(failure, editor::EditorPreferenceFailureCode::CapacityExceeded,
                                          "editor preference-provider capacity is exhausted",
                                          owner.extension, descriptor.providerId);
                slot = m_preferenceProviders.Size();
            }

            const vanguard::u32 generation = NextPreferenceProviderGeneration();
            if (generation == 0)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::GenerationExhausted,
                                      "editor preference-provider token generation space is exhausted",
                                      owner.extension, descriptor.providerId);

            PreferenceProviderRecord record;
            record.owner = owner;
            record.providerId = descriptor.providerId;
            record.token = {slot, generation};
            record.fieldCount = descriptor.fields.Size();
            for (vanguard::u32 index = 0; index < record.fieldCount; ++index)
                record.fields[index] = descriptor.fields[index];
            record.queryRevision = descriptor.queryRevision;
            record.capture = descriptor.capture;
            record.validate = descriptor.validate;
            record.apply = descriptor.apply;
            record.presentation = descriptor.presentation;
            record.reachable = true;
            record.active = true;

            if (slot == m_preferenceProviders.Size())
                m_preferenceProviders.PushBack(record);
            else
                m_preferenceProviders[slot] = record;
            m_livePreferenceFieldCount += record.fieldCount;
            m_providerSetDirty = true;
            AdvanceProviderSetSerial();
            token = record.token;

            OwnedRegistrationRecord registration;
            registration.owner = owner;
            registration.kind = OwnedRegistrationKind::PreferenceProvider;
            registration.preferenceProvider = token;
            registration.active = true;
            m_registrationLedger.PushBack(registration);
            return true;
        }

        [[nodiscard]] bool UnregisterPreferenceProvider(
            const editor::EditorExtensionOwner owner,
            const editor::EditorPreferenceProviderToken token,
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            ClearPreferenceFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return FailPreference(failure, editor::EditorPreferenceFailureCode::WrongThread,
                                      "editor preference-provider unregister requires the main thread",
                                      owner.extension);
            const ExtensionOwnerRecord* const ownerRecord = FindOwner(owner);
            if (ownerRecord == nullptr || !ownerRecord->live)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::OwnerRevoked,
                                      "editor preference-provider owner is not live",
                                      owner.extension);
            if (!RemovePreferenceProviderRecord(owner, token, failure))
                return false;
            for (OwnedRegistrationRecord& registration : m_registrationLedger)
                if (registration.active && registration.owner == owner &&
                    registration.kind == OwnedRegistrationKind::PreferenceProvider &&
                    registration.preferenceProvider == token)
                {
                    registration.active = false;
                    break;
                }
            return true;
        }

        void VisitPreferenceProviders(const editor::EditorPreferenceProviderVisitor visitor,
                                      void* const userData) const noexcept override
        {
            if (visitor == nullptr || !vanguard::concurrency::IsMainThread() ||
                m_status.state != editor::EditorShellState::Active)
                return;
            for (const PreferenceProviderRecord& provider : m_preferenceProviders)
            {
                if (!provider.active || !provider.reachable || provider.presentation == nullptr)
                    continue;
                visitor({provider.owner, provider.providerId, provider.presentation}, userData);
            }
        }

        [[nodiscard]] bool CapturePreferenceValue(
            const editor::EditorExtensionId extension,
            const editor::EditorPreferenceProviderId providerId,
            const editor::EditorPreferenceFieldId fieldId,
            editor::EditorPreferenceFieldValue& value,
            editor::EditorPreferenceFailure* const failure) const noexcept override
        {
            ClearPreferenceFailure(failure);
            value = {};
            if (!CanUsePreferences(failure))
                return false;
            const PreferenceProviderRecord* const provider = FindPreferenceProvider(extension, providerId);
            if (provider == nullptr)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidToken,
                                      "editor preference provider is unavailable", extension, providerId, fieldId);
            const editor::EditorPreferenceFieldDescriptor* const field = FindPreferenceField(*provider, fieldId);
            if (field == nullptr)
                return FailPreference(failure, editor::EditorPreferenceFailureCode::InvalidDescriptor,
                                      "editor preference field is unavailable", extension, providerId, fieldId);
            void* privateState = nullptr;
            value.type = field->type;
            if (!ResolvePrivateState(provider->owner, privateState) ||
                !provider->capture(privateState, fieldId, value) || !ValueMatchesField(*field, value))
                return FailPreference(failure, editor::EditorPreferenceFailureCode::CallbackFailure,
                                      "editor preference capture callback returned an invalid value",
                                      extension, providerId, fieldId);
            return true;
        }

        [[nodiscard]] bool ApplyPreferenceValue(
            const editor::EditorExtensionId extension,
            const editor::EditorPreferenceProviderId providerId,
            const editor::EditorPreferenceFieldId fieldId,
            const editor::EditorPreferenceFieldValue& value,
            editor::EditorPreferenceFailure* const failure) noexcept override
        {
            editor::EditorTypedPreferenceRecord record{extension, providerId, fieldId, value};
            editor::EditorTypedPreferenceItemResult result = editor::EditorTypedPreferenceItemResult::Invalid;
            if (!ValidateTypedPreferences({&record, 1}, {&result, 1}, failure))
                return false;
            if (result != editor::EditorTypedPreferenceItemResult::Valid)
            {
                InvalidatePreferenceValidation();
                return FailPreference(failure, editor::EditorPreferenceFailureCode::ValidationFailed,
                                      "editor preference value failed validation", extension, providerId, fieldId);
            }
            if (!ApplyTypedPreferences({&record, 1}, {&result, 1}, failure))
                return false;
            return result == editor::EditorTypedPreferenceItemResult::Valid;
        }

        [[nodiscard]] bool RemoveOwnedRegistrations(const editor::EditorExtensionOwner owner) noexcept
        {
            bool success = true;
            for (vanguard::u32 index = m_registrationLedger.Size(); index > 0; --index)
            {
                OwnedRegistrationRecord& registration = m_registrationLedger[index - 1u];
                if (!registration.active || !(registration.owner == owner))
                    continue;
                bool removed = true;
                switch (registration.kind)
                {
                case OwnedRegistrationKind::Command:
                    removed = m_commands != nullptr && UnregisterOwnedCommand(*m_commands, owner, registration.command);
                    break;
                case OwnedRegistrationKind::ContextType:
                    removed = m_commands != nullptr && UnregisterOwnedContextType(*m_commands, owner, registration.contextType);
                    break;
                case OwnedRegistrationKind::Handler:
                    removed = m_commands != nullptr && UnregisterOwnedHandler(*m_commands, owner, registration.handler);
                    break;
                case OwnedRegistrationKind::PanelType:
                    removed = m_panels != nullptr && UnregisterOwnedPanelType(*m_panels, owner, registration.panelType);
                    break;
                case OwnedRegistrationKind::SurfaceRoot:
                    removed = m_ui != nullptr && UnregisterOwnedSurface(*m_ui, owner, registration.surface);
                    break;
                case OwnedRegistrationKind::SurfaceContribution:
                    removed = m_ui != nullptr && UnregisterOwnedContribution(*m_ui, owner, registration.contribution);
                    break;
                case OwnedRegistrationKind::ActivitySource:
                    removed = m_activityPrivate != nullptr &&
                              UnregisterOwnedActivitySource(*m_activityPrivate, owner,
                                                            registration.activitySource);
                    break;
                case OwnedRegistrationKind::PreferenceProvider:
                    removed = RemovePreferenceProviderRecord(owner, registration.preferenceProvider);
                    break;
                }
                if (removed)
                    registration.active = false;
                else
                    success = false;
            }
            return success;
        }

        [[nodiscard]] bool DeactivateOne(ActiveExtension& active) noexcept
        {
            const bool revoked = RevokeOwner(active.owner);
            const bool cleaned = revoked && RemoveOwnedRegistrations(active.owner);
            if (!cleaned && m_status.failure.code == editor::EditorExtensionFailureCode::None)
            {
                m_status.failure.code = editor::EditorExtensionFailureCode::CleanupFailure;
                if (active.descriptor != nullptr)
                {
                    m_status.failure.extension = active.descriptor->id;
                    m_status.failure.technicalName = active.descriptor->technicalName;
                }
            }
            // Never release extension-private storage while any owned registration may still
            // retain callbacks/factoryData into it. A later lifecycle retry can finish cleanup
            // once the transient persistence-callback or registry failure has cleared.
            if (!cleaned)
                return false;
            if (m_workspace == nullptr || active.descriptor == nullptr)
                return true;
            if (m_activity == nullptr)
                return false;
            editor::EditorExtensionActivationContext activation = editor::EditorShellServiceAccess::MakeContext(
                active.descriptor->id, active.owner.activationGeneration, *m_workspace, *m_activity, *m_ui,
                m_fileDialogs, *this);
            active.descriptor->deactivate(activation, active.descriptor->data, active.privateState);
            active.privateState = nullptr;
            return true;
        }

        [[nodiscard]] bool OwnedStateEmpty() const noexcept
        {
            if (m_surfaceCommandBridgeToken.IsValid() || m_panelFocusSinkToken.IsValid() || m_panelLocalToken.IsValid())
                return false;
            if (!m_active.Empty())
                return false;
            for (const ExtensionOwnerRecord& owner : m_owners)
                if (owner.live)
                    return false;
            for (const OwnedRegistrationRecord& registration : m_registrationLedger)
                if (registration.active)
                    return false;
            for (const PreferenceProviderRecord& provider : m_preferenceProviders)
                if (provider.active)
                    return false;
            return true;
        }

        void ClearOwnedState() noexcept
        {
            m_owners.Clear();
            m_registrationLedger.Clear();
            m_preferenceProviders.Clear();
            m_livePreferenceFieldCount = 0;
            m_providerSetSerial = 1;
            m_providerSetDirty = false;
            m_typedPreferenceRevision = 1;
            InvalidatePreferenceValidation();
            m_registrationAdmissionOpen = false;
        }

        [[nodiscard]] bool DeactivateAll() noexcept
        {
            if (!vanguard::concurrency::IsMainThread())
                return false;
            m_registrationAdmissionOpen = false;
            if (m_workspace == nullptr || m_status.activationGeneration == 0)
            {
                m_active.Clear();
                m_status.activeCount = 0;
                return true;
            }

            bool success = true;
            vanguard::containers::StaticArray<ActiveExtension, editor::MaximumEditorExtensions> retained;
            for (vanguard::u32 index = m_active.Size(); index > 0; --index)
            {
                ActiveExtension& active = m_active[index - 1u];
                if (!DeactivateOne(active))
                {
                    retained.PushBack(active);
                    success = false;
                }
            }
            m_active.Clear();
            // The walk is reverse dependency order; restore failed entries to their original
            // activation order so a later retry preserves the same dependency teardown order.
            for (vanguard::u32 index = retained.Size(); index > 0; --index)
                m_active.PushBack(retained[index - 1u]);
            m_status.activeCount = m_active.Size();
            return success;
        }

        [[nodiscard]] const OwnedRegistrationRecord* FindPanelRegistration(
            const editor::EditorPanelTypeId type) const noexcept
        {
            for (const OwnedRegistrationRecord& registration : m_registrationLedger)
                if (registration.active && registration.kind == OwnedRegistrationKind::PanelType &&
                    registration.panelTypeId == type)
                    return &registration;
            return nullptr;
        }

        [[nodiscard]] bool RejectPanelFocus(editor::EditorUiFailure* const failure,
                                            const editor::EditorPanelFocusSnapshot& focus,
                                            const char* const message) const noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = editor::EditorUiFailureCode::FocusSynchronizationFailure;
                failure->panelType = focus.type;
                failure->panelInstance = focus.instance;
                failure->panel = focus.panel;
                failure->host = focus.host;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool ReconcilePanelFocus(const editor::detail::EditorPanelFocusUpdate& update,
                                               editor::EditorUiFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (!vanguard::concurrency::IsMainThread())
                return RejectPanelFocus(failure, update.focus, "editor panel focus reconciliation requires the main thread");
            if (m_commands == nullptr || m_commands->GetStatus().state != editor::EditorCommandServiceState::Running)
                return RejectPanelFocus(failure, update.focus, "editor panel focus reconciliation requires a running command service");

            const OwnedRegistrationRecord* target = nullptr;
            if (update.focus.HasFocus())
                target = FindPanelRegistration(update.focus.type);

            if (m_focusedPanel.panel == update.focus.panel)
            {
                m_focusedPanel = update.focus;
                return true;
            }

            if (m_panelLocalToken.IsValid())
            {
                editor::EditorCommandFailure commandFailure;
                if (!m_commands->PopContext(m_panelLocalToken, &commandFailure))
                    return RejectPanelFocus(failure, update.focus,
                                            commandFailure.message != nullptr ? commandFailure.message
                                                                              : "editor panel Local context could not be removed");
                m_panelLocalToken = {};
                m_panelLocalOwner = {};
            }

            if (target != nullptr && target->hasPanelLocalContext)
            {
                editor::EditorCommandFailure commandFailure;
                editor::EditorCommandContextToken localToken;
                if (!m_commands->PushContext(target->panelLocalContext.contextType, update.receiverData,
                                             target->panelLocalContext.mode, localToken, &commandFailure))
                    return RejectPanelFocus(failure, update.focus,
                                            commandFailure.message != nullptr ? commandFailure.message
                                                                              : "editor panel Local context could not be activated");
                m_panelLocalToken = localToken;
                m_panelLocalOwner = target->owner;
            }

            m_focusedPanel = update.focus;
            return true;
        }

        static bool QuerySurfaceCommandPresentation(const editor::EditorCommandId command,
                                                    editor::EditorCommandPresentation& presentation,
                                                    editor::EditorCommandFailure* const failure,
                                                    void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorShellService*>(userData);
            if (service == nullptr || service->m_commands == nullptr)
            {
                if (failure != nullptr)
                    *failure = {editor::EditorCommandFailureCode::WrongPhase, command, {}, {}, {},
                                "editor surface command bridge is unavailable"};
                presentation = {};
                presentation.state = editor::EditorCommandState::Disabled();
                return false;
            }
            return QueryCommandPresentation(*service->m_commands, command, presentation, failure);
        }

        static bool InvokeSurfaceCommand(const editor::EditorCommandId command,
                                         editor::EditorCommandFailure* const failure,
                                         void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorShellService*>(userData);
            if (service == nullptr || service->m_commands == nullptr)
            {
                if (failure != nullptr)
                    *failure = {editor::EditorCommandFailureCode::WrongPhase, command, {}, {}, {},
                                "editor surface command bridge is unavailable"};
                return false;
            }
            return service->m_commands->Invoke(command, failure);
        }

        static bool ValidateSurfaceCommandOwner(const editor::EditorExtensionOwner& owner,
                                                const editor::EditorCommandId command,
                                                editor::EditorCommandFailure* const failure,
                                                void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorShellService*>(userData);
            if (service == nullptr || service->m_commands == nullptr)
            {
                if (failure != nullptr)
                    *failure = {editor::EditorCommandFailureCode::WrongPhase, command, {}, {}, {},
                                "editor surface command bridge is unavailable"};
                return false;
            }
            return ValidateOwnedCommand(*service->m_commands, owner, command, failure);
        }

        static bool OnPanelFocus(const editor::detail::EditorPanelFocusUpdate& update, void* const userData,
                                 editor::EditorUiFailure* const failure) noexcept
        {
            auto* const service = static_cast<ManagedEditorShellService*>(userData);
            return service != nullptr && service->ReconcilePanelFocus(update, failure);
        }

        [[nodiscard]] bool DetachPanelFocusBridge() noexcept
        {
            if (!m_panelFocusSinkToken.IsValid())
            {
                m_panelLocalToken = {};
                m_panelLocalOwner = {};
                m_focusedPanel = {};
                return true;
            }
            if (m_ui == nullptr)
                return false;
            editor::EditorUiFailure failure;
            if (!UnregisterPanelFocusSink(*m_ui, m_panelFocusSinkToken, &failure))
                return false;
            m_panelFocusSinkToken = {};
            m_panelLocalToken = {};
            m_panelLocalOwner = {};
            m_focusedPanel = {};
            return true;
        }

        [[nodiscard]] bool DetachSurfaceCommandBridge() noexcept
        {
            if (!m_surfaceCommandBridgeToken.IsValid())
                return true;
            if (m_ui == nullptr)
                return false;
            editor::EditorUiFailure failure;
            if (!UnregisterSurfaceCommandBridge(*m_ui, m_surfaceCommandBridgeToken, &failure))
                return false;
            m_surfaceCommandBridgeToken = {};
            return true;
        }

        vanguard::containers::ArraySpan<const editor::EditorExtensionDescriptor> m_extensions;
        editor::ProjectWorkspaceService* m_workspace = nullptr;
        editor::EditorUiService* m_ui = nullptr;
        editor::EditorFileDialogService* m_fileDialogs = nullptr;
        editor::EditorCommandService* m_commands = nullptr;
        editor::EditorActivityService* m_activity = nullptr;
        editor::detail::EditorActivityPrivateService* m_activityPrivate = nullptr;
        editor::EditorPanelRegistry* m_panels = nullptr;
        editor::detail::EditorUiCommandBridgeToken m_surfaceCommandBridgeToken;
        editor::detail::EditorPanelFocusSinkToken m_panelFocusSinkToken;
        editor::EditorCommandContextToken m_panelLocalToken;
        editor::EditorExtensionOwner m_panelLocalOwner;
        editor::EditorPanelFocusSnapshot m_focusedPanel;
        editor::EditorExtensionCatalog m_catalog;
        editor::EditorExtensionSelection m_selection;
        vanguard::containers::StaticArray<ActiveExtension, editor::MaximumEditorExtensions> m_active;
        vanguard::containers::StaticArray<ExtensionOwnerRecord, editor::MaximumEditorExtensions> m_owners;
        vanguard::containers::StaticArray<OwnedRegistrationRecord, editor::MaximumEditorExtensionOwnedRegistrations> m_registrationLedger;
        vanguard::containers::StaticArray<PreferenceProviderRecord, editor::MaximumEditorPreferenceProviders> m_preferenceProviders;
        mutable editor::EditorTypedPreferenceRecord
            m_preferenceCaptureScratch[editor::MaximumEditorLivePreferenceFields]{};
        editor::EditorTypedPreferenceRecord
            m_validatedPreferences[editor::MaximumEditorLivePreferenceFields]{};
        vanguard::u32 m_livePreferenceFieldCount = 0;
        vanguard::u64 m_providerSetSerial = 1;
        vanguard::u64 m_typedPreferenceRevision = 1;
        vanguard::u64 m_validatedProviderSetSerial = 0;
        vanguard::u32 m_validatedPreferenceCount = 0;
        bool m_preferenceValidationReady = false;
        bool m_providerSetDirty = false;
        bool m_registrationAdmissionOpen = false;
        editor::EditorShellStatus m_status;
    };

    app::Service* CreateEditorShellService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(ManagedEditorShellService), alignof(ManagedEditorShellService));
        return block ? ::new (block.address) ManagedEditorShellService(static_cast<const editor::EditorShellServiceConfig*>(userData)) : nullptr;
    }

    void DestroyEditorShellService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorShellService*>(service)->~ManagedEditorShellService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorShellService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorShellService(application::EngineHost& host, const EditorShellServiceConfig& config,
                                    application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {ProjectWorkspaceServiceId, application::DependencyKind::Required},
            {EditorUiServiceId, application::DependencyKind::Required},
            {EditorCommandServiceId, application::DependencyKind::Required},
            {EditorActivityServiceId, application::DependencyKind::Required},
            // Generic shell tests/tools may omit native dialogs. When the editor
            // selects the dialog service this optional edge still guarantees it
            // starts before, and outlives, extension activation/deactivation.
            {EditorFileDialogServiceId, application::DependencyKind::Optional}};
        constexpr application::CapabilityId capabilities[]{EditorShellCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorShellServiceId;
        descriptor.name = "editorShell";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = dependencies;
        descriptor.provides = capabilities;
        descriptor.create = CreateEditorShellService;
        descriptor.destroy = DestroyEditorShellService;
        descriptor.userData = const_cast<EditorShellServiceConfig*>(&config);
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorShellService* FindEditorShellService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorShellService*>(host.FindCapability(EditorShellCapabilityId));
    }

    EditorShellService* FindEditorShellService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorShellService*>(context.FindCapability(EditorShellCapabilityId));
    }
} // namespace vanguard::editor
