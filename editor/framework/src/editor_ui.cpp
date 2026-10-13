#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_ui_imgui_platform.hpp>
#include <vanguard/editor/editor_ui_persistence.hpp>
#include <vanguard/editor/editor_ui_renderer.hpp>
#include <vanguard/editor/editor_svg_icon.hpp>
#include <vanguard/editor/editor_visual_style.hpp>
#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_panels.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/editor/editor_project_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/system/time.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace containers = vanguard::containers;
    namespace editor = vanguard::editor;
    namespace projects = vanguard::projects;
    namespace rhi = vanguard::rhi;
    namespace window = vanguard::window;
    using vanguard::u8;
    using vanguard::u32;
    using vanguard::u64;

    void MakeEditorChromeTitle(const char* const source, char (&output)[editor::MaximumEditorUiSurfaceStringBytes]) noexcept
    {
        output[0] = '\0';
        if (source == nullptr)
            return;
        constexpr char suffix[] = " - Vanguard Editor";
        constexpr char brandPrefix[] = "Vanguard ";
        size_t length = std::strlen(source);
        constexpr size_t suffixLength = sizeof(suffix) - 1u;
        if (length >= suffixLength && std::memcmp(source + length - suffixLength, suffix, suffixLength) == 0)
            length -= suffixLength;
        const char* begin = source;
        constexpr size_t prefixLength = sizeof(brandPrefix) - 1u;
        if (length > prefixLength && std::memcmp(begin, brandPrefix, prefixLength) == 0)
        {
            begin += prefixLength;
            length -= prefixLength;
        }
        const size_t copied = length < sizeof(output) - 1u ? length : sizeof(output) - 1u;
        if (copied != 0)
            std::memcpy(output, begin, copied);
        output[copied] = '\0';
    }

    struct EditorStatusBarSnapshot final
    {
        char state[32]{};
        char project[projects::MaximumTechnicalNameBytes + 1u]{};
        char build[16]{};
        char backend[16]{};
        char adapter[128]{};
        char memory[32]{};
        char fullDevice[256]{};
        char compactDevice[192]{};
    };

    [[nodiscard]] const char* EditorBuildConfigurationName() noexcept
    {
#if VG_BUILD_DEBUG
        return "Debug";
#elif VG_BUILD_DEVELOPMENT
        return "Development";
#elif VG_BUILD_PROFILE
        return "Profile";
#elif VG_BUILD_SHIPPING
        return "Shipping";
#else
        return "Unknown";
#endif
    }

    [[nodiscard]] const char* EditorBackendName(const rhi::BackendKind backend) noexcept
    {
        switch (backend)
        {
        case rhi::BackendKind::D3D12: return "D3D12";
        case rhi::BackendKind::Vulkan: return "Vulkan";
        default: return "Unknown GPU API";
        }
    }

    [[nodiscard]] const char* StartupAutomaticRecookStateName(
        const vanguard::asset_streaming::ProjectAssetAutomaticRecookState state) noexcept
    {
        using State = vanguard::asset_streaming::ProjectAssetAutomaticRecookState;
        switch (state)
        {
        case State::Disabled: return "Disabled";
        case State::Idle: return "Ready";
        case State::Pending: return "Queued";
        case State::Planning: return "Planning";
        case State::Building: return "Building";
        case State::Validating: return "Validating";
        case State::Cancelling: return "Cancelling";
        case State::Failed: return "Failed";
        case State::RecoveryRequired: return "Recovery required";
        }
        return "Unknown";
    }

    template <size_t Capacity>
    void CopyStatusBarText(char (&destination)[Capacity], const char* const source) noexcept
    {
        destination[0] = '\0';
        if (source == nullptr || source[0] == '\0')
            return;
        const size_t bytes = std::min(std::strlen(source), Capacity - 1u);
        std::memcpy(destination, source, bytes);
        destination[bytes] = '\0';
    }

    void BuildStatusBarDeviceText(EditorStatusBarSnapshot& snapshot) noexcept
    {
        if (snapshot.memory[0] != '\0')
        {
            std::snprintf(snapshot.fullDevice, sizeof(snapshot.fullDevice), "%s  |  %s  |  %s  |  %s",
                          snapshot.build, snapshot.backend, snapshot.adapter, snapshot.memory);
        }
        else
        {
            std::snprintf(snapshot.fullDevice, sizeof(snapshot.fullDevice), "%s  |  %s  |  %s",
                          snapshot.build, snapshot.backend, snapshot.adapter);
        }
        std::snprintf(snapshot.compactDevice, sizeof(snapshot.compactDevice), "%s  |  %s",
                      snapshot.backend, snapshot.adapter);
    }

    void ClearFailure(editor::EditorUiFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    bool Fail(editor::EditorUiFailure* const failure, const editor::EditorUiFailureCode code, const char* const message,
              const editor::EditorPanelTypeId panelType = editor::InvalidEditorPanelTypeId,
              const editor::EditorPanelInstanceId panelInstance = editor::InvalidEditorPanelInstanceId,
              const editor::EditorPanelHandle panel = {}, const editor::EditorHostWindowHandle host = {}, const editor::EditorTextureHandle texture = {}) noexcept
    {
        if (failure != nullptr)
            *failure = {code, panelType, panelInstance, panel, host, texture, message};
        return false;
    }

    [[nodiscard]] vanguard::u32 NextGeneration(const vanguard::u32 generation) noexcept
    {
        const vanguard::u32 next = generation + 1u;
        return next != 0 ? next : 1u;
    }

    void AppendShortcutText(char* const output, const vanguard::u32 capacity, vanguard::u32& length,
                            const char* const text) noexcept
    {
        if (output == nullptr || capacity == 0 || text == nullptr)
            return;
        for (vanguard::u32 index = 0; text[index] != '\0' && length + 1u < capacity; ++index)
            output[length++] = text[index];
        output[length] = '\0';
    }

    [[nodiscard]] const char* ShortcutKeyName(const vanguard::input::Key key, char (&scratch)[24]) noexcept
    {
        const vanguard::u32 value = static_cast<vanguard::u32>(key);
        const vanguard::u32 firstLetter = static_cast<vanguard::u32>(vanguard::input::Key::A);
        const vanguard::u32 lastLetter = static_cast<vanguard::u32>(vanguard::input::Key::Z);
        if (value >= firstLetter && value <= lastLetter)
        {
            scratch[0] = static_cast<char>('A' + (value - firstLetter));
            scratch[1] = '\0';
            return scratch;
        }

        const vanguard::u32 firstDigit = static_cast<vanguard::u32>(vanguard::input::Key::Digit1);
        const vanguard::u32 lastDigit = static_cast<vanguard::u32>(vanguard::input::Key::Digit0);
        if (value >= firstDigit && value <= lastDigit)
        {
            constexpr char digits[] = "1234567890";
            scratch[0] = digits[value - firstDigit];
            scratch[1] = '\0';
            return scratch;
        }

        const vanguard::u32 firstF = static_cast<vanguard::u32>(vanguard::input::Key::F1);
        const vanguard::u32 lastF = static_cast<vanguard::u32>(vanguard::input::Key::F12);
        if (value >= firstF && value <= lastF)
        {
            std::snprintf(scratch, sizeof(scratch), "F%u", value - firstF + 1u);
            return scratch;
        }
        const vanguard::u32 firstExtendedF = static_cast<vanguard::u32>(vanguard::input::Key::F13);
        const vanguard::u32 lastExtendedF = static_cast<vanguard::u32>(vanguard::input::Key::F24);
        if (value >= firstExtendedF && value <= lastExtendedF)
        {
            std::snprintf(scratch, sizeof(scratch), "F%u", value - firstExtendedF + 13u);
            return scratch;
        }

        switch (key)
        {
        case vanguard::input::Key::Enter: return "Enter";
        case vanguard::input::Key::Escape: return "Esc";
        case vanguard::input::Key::Backspace: return "Backspace";
        case vanguard::input::Key::Tab: return "Tab";
        case vanguard::input::Key::Space: return "Space";
        case vanguard::input::Key::Insert: return "Insert";
        case vanguard::input::Key::Home: return "Home";
        case vanguard::input::Key::PageUp: return "PageUp";
        case vanguard::input::Key::Delete: return "Delete";
        case vanguard::input::Key::End: return "End";
        case vanguard::input::Key::PageDown: return "PageDown";
        case vanguard::input::Key::Right: return "Right";
        case vanguard::input::Key::Left: return "Left";
        case vanguard::input::Key::Down: return "Down";
        case vanguard::input::Key::Up: return "Up";
        default:
            std::snprintf(scratch, sizeof(scratch), "Key%u", value);
            return scratch;
        }
    }

    void FormatShortcut(const editor::EditorShortcutBinding& binding, char (&output)[64]) noexcept
    {
        output[0] = '\0';
        if (!binding.bound)
            return;
        vanguard::u32 length = 0;
        if (editor::HasEditorShortcutModifier(binding.chord.modifiers, editor::EditorShortcutModifiers::Control))
            AppendShortcutText(output, sizeof(output), length, "Ctrl+");
        if (editor::HasEditorShortcutModifier(binding.chord.modifiers, editor::EditorShortcutModifiers::Shift))
            AppendShortcutText(output, sizeof(output), length, "Shift+");
        if (editor::HasEditorShortcutModifier(binding.chord.modifiers, editor::EditorShortcutModifiers::Alt))
            AppendShortcutText(output, sizeof(output), length, "Alt+");
        if (editor::HasEditorShortcutModifier(binding.chord.modifiers, editor::EditorShortcutModifiers::Gui))
            AppendShortcutText(output, sizeof(output), length, "Super+");
        char keyName[24]{};
        AppendShortcutText(output, sizeof(output), length, ShortcutKeyName(binding.chord.key, keyName));
    }

    void MakeImGuiDisplayLabel(const char* const label,
                               char (&output)[editor::MaximumEditorUiSurfaceStringBytes * 4u]) noexcept
    {
        output[0] = '\0';
        if (label == nullptr)
            return;
        vanguard::u32 write = 0;
        for (vanguard::u32 read = 0; label[read] != '\0' && write + 1u < sizeof(output); ++read)
        {
            if (label[read] == '#' && label[read + 1u] == '#' && write + 4u < sizeof(output))
            {
                output[write++] = '#';
                output[write++] = static_cast<char>(0xe2);
                output[write++] = static_cast<char>(0x80);
                output[write++] = static_cast<char>(0x8b);
                continue;
            }
            output[write++] = label[read];
        }
        output[write] = '\0';
    }

    class ManagedEditorUiService final : public editor::detail::EditorUiPersistencePrivateService
    {
    public:
        ManagedEditorUiService() noexcept
            : m_panels(m_state, editor::detail::GetProductionEditorPanelGenerationSource(), InvalidatePanelFocus, this),
              m_surfaces(m_state, editor::detail::GetProductionEditorSurfaceGenerationSource()),
              m_surfaceCommands(m_state),
              m_hosts(vanguard::memory::pools::Editor::GetInstance()), m_textures(vanguard::memory::pools::Editor::GetInstance())
        {
            m_hosts.Reserve(editor::MaximumEditorHostWindows);
            m_textures.Reserve(editor::MaximumEditorTextures);
        }

        ~ManagedEditorUiService() override = default;

        [[nodiscard]] editor::EditorPanelRegistry& GetPanels() noexcept override { return m_panels; }
        [[nodiscard]] const editor::EditorPanelRegistry& GetPanels() const noexcept override { return m_panels; }

        [[nodiscard]] editor::detail::EditorUiUserStateLayoutLoadResult LoadUserStateLayout(
            const containers::ArraySpan<const u8> bytes,
            const containers::ArraySpan<const editor::detail::EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
            {
                repairedPlacementCount = 0;
                Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                     "editor UI user-state layout load requires ready state");
                return editor::detail::EditorUiUserStateLayoutLoadResult::InvalidArgument;
            }
            editor::EditorUiFailure localFailure;
            editor::EditorUiFailure* const observedFailure = failure != nullptr ? failure : &localFailure;
            const editor::detail::EditorUiUserStateLayoutLoadResult result =
                m_platform.LoadUserStateLayout(bytes, savedDisplays, repairedPlacementCount, observedFailure);
            if (result == editor::detail::EditorUiUserStateLayoutLoadResult::AppliedUsable)
            {
                m_defaultDockLayoutInitialized = true;
                m_restoredDockLayoutNeedsReconcile = true;
                m_dockRoot = editor::detail::EditorPrimaryDockspaceId;
                m_dockCenter = m_dockRoot;
                m_dockLeft = 0;
                m_dockRight = 0;
                m_dockBottom = 0;
            }
            else if (editor::detail::ShouldResetEditorDockLayoutAfterUserStateLoad(result, observedFailure->code))
            {
                ResetDockLayout();
            }
            return result;
        }

        [[nodiscard]] bool CaptureUserStateLayout(
            void* const destination, const u32 capacity, u32& byteCount,
            editor::detail::EditorUiSavedDisplay* const savedDisplays, const u32 displayCapacity,
            u32& displayCount, editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor UI user-state capture requires ready state");
            return m_platform.CaptureUserStateLayout(destination, capacity, byteCount, savedDisplays,
                                                     displayCapacity, displayCount, failure);
        }

        [[nodiscard]] editor::detail::EditorUiUserStateLayoutLoadResult ApplyUserStateLayoutLive(
            const containers::ArraySpan<const u8> bytes,
            const containers::ArraySpan<const editor::detail::EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
            {
                repairedPlacementCount = 0;
                Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                     "editor UI live layout apply requires ready state");
                return editor::detail::EditorUiUserStateLayoutLoadResult::InvalidArgument;
            }
            const editor::detail::EditorUiUserStateLayoutLoadResult result =
                m_platform.ApplyUserStateLayoutLive(bytes, savedDisplays, repairedPlacementCount, failure);
            if (result == editor::detail::EditorUiUserStateLayoutLoadResult::AppliedUsable)
            {
                m_defaultDockLayoutInitialized = true;
                m_restoredDockLayoutNeedsReconcile = true;
                m_dockRoot = editor::detail::EditorPrimaryDockspaceId;
                m_dockCenter = m_dockRoot;
                m_dockLeft = 0;
                m_dockRight = 0;
                m_dockBottom = 0;
            }
            return result;
        }

        [[nodiscard]] bool ResetUserStateLayoutToDefault(editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor UI default layout reset requires ready state");
            if (!m_platform.ResetUserStateLayoutLive(failure))
                return false;
            ResetDockLayout();
            return true;
        }

        [[nodiscard]] bool ConsumeUserStateLayoutDirty(editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
            {
                Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                     "editor UI layout dirty observation requires ready state");
                return false;
            }
            return m_platform.ConsumeUserStateLayoutDirty(failure);
        }

        [[nodiscard]] bool IsUserStateLayoutSettling(editor::EditorUiFailure* const failure) const noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
            {
                Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                     "editor UI layout settling observation requires ready state");
                return false;
            }
            return m_platform.IsUserStateLayoutSettling(failure);
        }

        [[nodiscard]] bool RefreshPanelUserStateRevision(editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor panel persistent-state revision refresh requires ready UI state");
            return m_panels.RefreshPersistentStateRevisions(failure);
        }

        [[nodiscard]] u64 GetPanelUserStateRevision() const noexcept override
        {
            return m_panels.GetUserStateRevision();
        }

        [[nodiscard]] bool ReconcilePanelPresentation(
            const containers::ArraySpan<const editor::detail::EditorPanelUserStateRecord> records,
            editor::detail::EditorPanelUserStateRestoreStatus& status,
            editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor panel presentation reconcile requires ready UI state");
            return m_panels.ReconcilePresentationUserState(records, status, failure);
        }

        [[nodiscard]] bool RequestNamedLayoutDialog(
            const editor::detail::EditorUiNamedLayoutDialogKind kind,
            const containers::ArraySpan<const editor::detail::EditorUiNamedLayoutChoice> choices,
            const u64 activeId, const containers::StringView initialName,
            editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor named-layout dialog request requires ready UI state");
            if (kind == editor::detail::EditorUiNamedLayoutDialogKind::None ||
                choices.Size() > editor::detail::MaximumEditorUiNamedLayouts ||
                initialName.Length() > editor::detail::MaximumEditorUiNamedLayoutNameBytes ||
                m_namedLayoutDialogKind != editor::detail::EditorUiNamedLayoutDialogKind::None ||
                m_namedLayoutDialogResultPending)
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor,
                            "editor named-layout dialog request is invalid or another request is pending");
            m_namedLayoutChoiceCount = choices.Size();
            m_namedLayoutSelectedId = activeId;
            bool selectedFound = activeId == 0;
            for (u32 index = 0; index < choices.Size(); ++index)
            {
                const auto& source = choices[index];
                if (source.id == 0 || source.nameByteCount == 0 ||
                    source.nameByteCount > editor::detail::MaximumEditorUiNamedLayoutNameBytes)
                    return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor,
                                "editor named-layout dialog choice is invalid");
                m_namedLayoutChoices[index] = source;
                selectedFound = selectedFound || source.id == activeId;
            }
            if (!selectedFound || (m_namedLayoutSelectedId == 0 && m_namedLayoutChoiceCount != 0))
                m_namedLayoutSelectedId = m_namedLayoutChoiceCount != 0 ? m_namedLayoutChoices[0].id : 0;
            m_namedLayoutNameByteCount = initialName.Length();
            if (!initialName.Empty())
                std::memcpy(m_namedLayoutName, initialName.Data(), initialName.Length());
            m_namedLayoutName[m_namedLayoutNameByteCount] = '\0';
            m_namedLayoutDialogKind = kind;
            m_namedLayoutDialogOpenRequested = true;
            return true;
        }

        [[nodiscard]] bool ConsumeNamedLayoutDialogResult(
            editor::detail::EditorUiNamedLayoutDialogResult& result,
            editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            result = {};
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor named-layout dialog result requires ready UI state");
            if (!m_namedLayoutDialogResultPending)
                return true;
            result = m_namedLayoutDialogResult;
            m_namedLayoutDialogResult = {};
            m_namedLayoutDialogResultPending = false;
            return true;
        }

        [[nodiscard]] bool RegisterHost(const editor::EditorHostWindowDesc& desc, editor::EditorHostWindowHandle& output,
                                        editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            output = {};
            if (!CanUseRegistries(failure))
                return false;
            if (!ValidHost(desc) || HasConflictingHost(desc, {}))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor host descriptor is invalid or conflicts with an existing host");
            vanguard::u32 index = ~vanguard::u32{0};
            for (vanguard::u32 candidate = 0; candidate < m_hosts.Size(); ++candidate)
                if (!m_hosts[candidate].active)
                {
                    index = candidate;
                    break;
                }
            if (index == ~vanguard::u32{0})
            {
                if (m_hosts.Size() >= editor::MaximumEditorHostWindows)
                    return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor host window capacity is exhausted");
                index = m_hosts.Size();
                static_cast<void>(m_hosts.EmplaceBack());
            }
            HostRecord& host = m_hosts[index];
            host.frameData.Reset();
            host.generation = NextGeneration(host.generation);
            host.handle = {index, host.generation};
            host.desc = desc;
            if (!m_renderer.CreateHost(host.handle, host.desc, failure))
                return false;
            host.active = true;
            output = host.handle;
            return true;
        }

        [[nodiscard]] bool UpdateHost(const editor::EditorHostWindowHandle handle, const editor::EditorHostWindowDesc& desc,
                                      editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor host window handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, handle);
            if (!ValidHost(desc) || HasConflictingHost(desc, handle))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor host update is invalid or conflicts with another host",
                            editor::InvalidEditorPanelTypeId, editor::InvalidEditorPanelInstanceId, {}, handle);
            if (desc.window != host->desc.window || desc.presentationOutput != host->desc.presentationOutput || desc.renderViewport != host->desc.renderViewport)
                return Fail(failure, editor::EditorUiFailureCode::InUse, "editor host output ownership cannot be rebound through registry metadata");
            host->desc = desc;
            return true;
        }

        [[nodiscard]] bool UnregisterHost(const editor::EditorHostWindowHandle handle, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor host window handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, handle);
            if (m_candidatePanelFocus.host == handle)
            {
                m_candidatePanelFocus = {};
                m_candidatePanelFocus.frame = m_frame;
                m_candidatePanelReceiver = nullptr;
            }
            if (m_panelFocus.host == handle)
            {
                editor::EditorPanelFocusSnapshot cleared;
                cleared.frame = m_frame;
                if (!DispatchPanelFocus(cleared, nullptr, failure))
                    return false;
                m_panelFocus = cleared;
            }
            if (m_previousFocusedPanelHost == handle)
            {
                m_previousFocusedPanelHost = {};
                m_previousFocusedPanelAuthored = false;
            }
            m_surfaceInteractionActive = false;
            if (!m_renderer.DestroyHost(handle, failure))
                return false;
            CancelContextMenuRequestsForHost(handle);
            if (m_currentAuthoringHost == handle)
                ClearCurrentAuthoringStamp();
            host->frameData.Reset();
            host->active = false;
            host->desc = {};
            return true;
        }

        [[nodiscard]] bool GetHost(const editor::EditorHostWindowHandle handle, editor::EditorHostWindowInfo& info) const noexcept override
        {
            info = {};
            const HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return false;
            info = {host->handle, host->desc.window, host->desc.presentationOutput, host->desc.renderViewport, host->desc.primary};
            return true;
        }

        void VisitHosts(const editor::EditorHostWindowVisitor visitor, void* const userData) const noexcept override
        {
            if (visitor == nullptr)
                return;
            for (const HostRecord& host : m_hosts)
                if (host.active)
                    visitor({host.handle, host.desc.window, host.desc.presentationOutput, host.desc.renderViewport, host.desc.primary}, userData);
        }

        [[nodiscard]] bool RegisterTexture(const editor::EditorTextureDesc& desc, editor::EditorTextureHandle& output,
                                           editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            output = {};
            if (!CanUseRegistries(failure))
                return false;
            if (!ValidTexture(desc))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor texture descriptor is invalid");
            vanguard::u32 index = ~vanguard::u32{0};
            for (vanguard::u32 candidate = 0; candidate < m_textures.Size(); ++candidate)
                if (!m_textures[candidate].active)
                {
                    index = candidate;
                    break;
                }
            if (index == ~vanguard::u32{0})
            {
                if (m_textures.Size() >= editor::MaximumEditorTextures)
                    return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor texture capacity is exhausted");
                index = m_textures.Size();
                static_cast<void>(m_textures.EmplaceBack());
            }
            TextureRecord& texture = m_textures[index];
            texture.generation = NextGeneration(texture.generation);
            texture.handle = {index, texture.generation};
            texture.texture.Reset(desc.texture);
            texture.sampler.Reset(desc.sampler);
            texture.colorSpace = desc.colorSpace;
            texture.publication = desc.publication;
            texture.active = true;
            output = texture.handle;
            return true;
        }

        [[nodiscard]] bool UnregisterTexture(const editor::EditorTextureHandle handle, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            TextureRecord* const texture = FindTexture(handle);
            if (texture == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor texture handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, {}, handle);
            texture->texture.Reset();
            texture->sampler.Reset();
            texture->publication = {};
            texture->active = false;
            return true;
        }

        [[nodiscard]] bool ResolveTexture(const editor::EditorTextureHandle handle, editor::ResolvedEditorTexture& resolved) const noexcept override
        {
            resolved = {};
            const TextureRecord* const texture = FindTexture(handle);
            if (texture == nullptr)
                return false;
            resolved = {texture->texture.GetRef(), texture->sampler.GetRef(), texture->colorSpace, texture->publication};
            return true;
        }

        [[nodiscard]] bool AcquireFrameData(const editor::EditorHostWindowHandle handle, editor::EditorUiFrameData& frameData) const noexcept override
        {
            frameData.Reset();
            const HostRecord* const host = FindHost(handle);
            if (host == nullptr || !host->frameData.IsValid())
                return false;
            frameData = host->frameData;
            return true;
        }

        [[nodiscard]] bool AcquireTextureFrameData(editor::EditorUiTextureFrameData& frameData) const noexcept override
        {
            frameData.Reset();
            if (!m_textureFrameData.IsValid())
                return false;
            frameData = m_textureFrameData;
            return true;
        }

        [[nodiscard]] bool BeginFrame(const vanguard::engine::FrameContext& frame, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor UI frame can begin only while the service is ready");
            if (m_textureFrameData.IsValid())
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor texture uploads must be submitted before beginning another UI frame");
            if (!m_renderer.BeginFrame(failure) || !m_platform.BeginFrame(frame, failure))
                return false;
            m_frame = frame.frame;
            m_candidatePanelFocus = {};
            m_candidatePanelFocus.frame = frame.frame;
            m_candidatePanelReceiver = nullptr;
            m_contextMenuRequestedPanel = {};
            m_contextMenuRequestedFocus = {};
            m_previousFocusedPanelAuthored = false;
            m_previousFocusedPanelHost = {};
            m_surfaceInteractionActive = false;
            ClearCurrentAuthoringStamp();
            m_state = editor::EditorUiState::BuildingFrame;
            return true;
        }

        [[nodiscard]] bool DrawOpenPanels(editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::BuildingFrame)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor panels can be drawn only while building a UI frame");
            DrawPersistentSurfaces();
            EnsurePrimaryDockspace();
            m_panels.VisitOpenForAuthoring(DrawPanelForAuthoring, this);
            RefreshStartupReadinessLatch();
            if (!m_startupReadinessComplete)
                DrawStartupReadinessOverlay();
            return true;
        }

        [[nodiscard]] bool EndFrame(editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::BuildingFrame)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor UI frame is not being built");
            editor::EditorUiFrameData frames[editor::MaximumEditorHostWindows]{};
            editor::EditorUiTextureFrameData textureFrame;
            editor::EditorUiInputCapture inputCapture;
            vanguard::u32 frameCount = 0;
            if (!m_platform.EndFrame(frames, editor::MaximumEditorHostWindows, frameCount, textureFrame, inputCapture, failure))
            {
                m_platform.AbortFrame();
                m_panels.DiscardPendingVisibility();
                ClearSurfaceInteractionState();
                m_state = editor::EditorUiState::Ready;
                return false;
            }
            for (vanguard::u32 index = 0; index < frameCount; ++index)
            {
                if (!frames[index].IsValid() || frames[index].GetFrame() != m_frame || FindHost(frames[index].GetHost()) == nullptr)
                {
                    m_panels.DiscardPendingVisibility();
                    ClearSurfaceInteractionState();
                    m_state = editor::EditorUiState::Ready;
                    return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor UI platform returned frame data for an invalid host");
                }
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (frames[previous].GetHost() == frames[index].GetHost())
                    {
                        m_panels.DiscardPendingVisibility();
                        ClearSurfaceInteractionState();
                        m_state = editor::EditorUiState::Ready;
                        return Fail(failure, editor::EditorUiFailureCode::DuplicateIdentity, "editor UI platform returned duplicate frame data for one host");
                    }
            }
            if (textureFrame.IsValid() && textureFrame.GetFrame() != m_frame)
            {
                m_panels.DiscardPendingVisibility();
                ClearSurfaceInteractionState();
                m_state = editor::EditorUiState::Ready;
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor UI platform returned texture data for the wrong frame");
            }
            for (HostRecord& host : m_hosts)
                if (host.active)
                    host.frameData.Reset();
            for (vanguard::u32 index = 0; index < frameCount; ++index)
                FindHost(frames[index].GetHost())->frameData = static_cast<editor::EditorUiFrameData&&>(frames[index]);
            m_textureFrameData = static_cast<editor::EditorUiTextureFrameData&&>(textureFrame);
            m_inputCapture = inputCapture;
            m_state = editor::EditorUiState::Ready;
            return true;
        }

        void AbortFrame() noexcept override
        {
            if (m_state == editor::EditorUiState::BuildingFrame)
            {
                m_platform.AbortFrame();
                m_panels.DiscardPendingVisibility();
                m_candidatePanelFocus = {};
                m_candidatePanelReceiver = nullptr;
                m_contextMenuRequestedPanel = {};
                m_contextMenuRequestedFocus = {};
                ClearCurrentAuthoringStamp();
                ClearSurfaceInteractionState();
                m_state = editor::EditorUiState::Ready;
            }
        }

        [[nodiscard]] editor::EditorUiInputCapture GetInputCapture() const noexcept override { return m_inputCapture; }
        [[nodiscard]] editor::EditorPanelFocusSnapshot GetPanelFocus() const noexcept override { return m_panelFocus; }
        [[nodiscard]] vanguard::f32 GetUserScale() const noexcept override { return m_platform.GetUserScale(); }
        [[nodiscard]] bool SetUserScale(const vanguard::f32 scale,
                                        editor::EditorUiFailure* const failure) noexcept override
        {
            return m_platform.SetUserScale(scale, failure);
        }
        [[nodiscard]] editor::EditorUiState GetState() const noexcept override { return m_state; }
        [[nodiscard]] vanguard::u64 GetFrame() const noexcept override { return m_frame; }

        [[nodiscard]] bool RequestContextMenuForPanel(const editor::EditorPanelHandle panel,
                                                     const vanguard::u64 authoringFrame,
                                                     const editor::EditorUiSurfaceId surfaceId,
                                                     editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            auto reject = [&](const editor::EditorUiFailureCode code, const char* const message,
                              const editor::EditorHostWindowHandle host = {}) noexcept
            {
                if (failure != nullptr)
                {
                    *failure = {};
                    failure->code = code;
                    failure->panel = panel;
                    failure->host = host;
                    failure->surface = surfaceId;
                    failure->message = message;
                }
                return false;
            };
            if (!vanguard::concurrency::IsMainThread())
                return reject(editor::EditorUiFailureCode::WrongThread,
                              "editor panel context-menu requests require the main thread");
            if (m_state != editor::EditorUiState::BuildingFrame)
                return reject(editor::EditorUiFailureCode::WrongPhase,
                              "editor panel context-menu requests require active panel authoring");
            if (!panel.IsValid() || authoringFrame == 0 || authoringFrame != m_currentAuthoringFrame ||
                panel != m_currentAuthoringPanel || !m_currentAuthoringOwner.IsValid())
                return reject(editor::EditorUiFailureCode::InvalidContextAssociation,
                              "editor panel context-menu request does not match the currently authored panel");
            void* liveReceiver = nullptr;
            if (!m_panels.ResolveFocusReceiver(panel, liveReceiver))
                return reject(editor::EditorUiFailureCode::InvalidContextAssociation,
                              "editor panel context-menu request panel is no longer open and reachable");
            if (!m_currentAuthoringHost.IsValid() || FindHost(m_currentAuthoringHost) == nullptr)
                return reject(editor::EditorUiFailureCode::InvalidHandle,
                              "editor panel context-menu request host is no longer live", m_currentAuthoringHost);

            editor::detail::EditorUiSurfaceToken surface;
            editor::EditorExtensionOwner owner;
            editor::detail::EditorUiSurfaceKind kind = editor::detail::EditorUiSurfaceKind::ContextMenu;
            if (!m_surfaces.ResolveSurface(surfaceId, surface, owner, kind) || !surface.IsValid() ||
                kind != editor::detail::EditorUiSurfaceKind::ContextMenu)
                return reject(editor::EditorUiFailureCode::InvalidHandle,
                              "editor panel context-menu surface is missing or is not a context-menu root",
                              m_currentAuthoringHost);
            if (!(owner == m_currentAuthoringOwner))
                return reject(editor::EditorUiFailureCode::OwnerMismatch,
                              "editor panel and context-menu surface owners do not match",
                              m_currentAuthoringHost);

            const ImVec2 mouse = ImGui::GetMousePos();
            const editor::detail::EditorUiStampedContextMenuRequest request{
                panel, m_currentAuthoringHost, m_currentAuthoringOwner, surfaceId, surface, m_frame, {mouse.x, mouse.y}};
            if (!m_surfaces.QueueContextMenuRequest(request, failure))
                return false;
            // A context request is an explicit interaction with the authored panel.
            // ImGui may not mark its parent window focused until the next frame
            // (notably for a restored selection inside a docked child).
            m_contextMenuRequestedPanel = panel;
            return true;
        }

        [[nodiscard]] bool RegisterSurfaceForOwner(const editor::EditorExtensionOwner& owner,
                                                  const editor::detail::EditorUiSurfaceDesc& desc,
                                                  editor::detail::EditorUiSurfaceToken& token,
                                                  editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaces.RegisterSurface(owner, desc, token, failure);
        }

        [[nodiscard]] bool UnregisterSurfaceForOwner(const editor::EditorExtensionOwner& owner,
                                                    const editor::detail::EditorUiSurfaceToken& token,
                                                    editor::EditorUiFailure* const failure) noexcept override
        {
            editor::detail::EditorUiCommittedContextMenuRequest committed;
            const bool activePopup = m_surfaces.PeekCommittedContextMenuRequest(committed) && committed.surface == token;
            const bool removed = m_surfaces.UnregisterSurface(owner, token, failure);
            if (removed && activePopup)
                m_contextPopupOpenedRequestFrame = 0;
            return removed;
        }

        [[nodiscard]] bool RegisterContributionForOwner(const editor::EditorExtensionOwner& owner,
                                                       const editor::detail::EditorUiContributionDesc& desc,
                                                       editor::detail::EditorUiContributionToken& token,
                                                       editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaces.RegisterContribution(owner, desc, token, failure);
        }

        [[nodiscard]] bool UnregisterContributionForOwner(const editor::EditorExtensionOwner& owner,
                                                         const editor::detail::EditorUiContributionToken& token,
                                                         editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaces.UnregisterContribution(owner, token, failure);
        }

        void RevokeSurfaceOwner(const editor::EditorExtensionOwner& owner) noexcept override
        {
            editor::detail::EditorUiCommittedContextMenuRequest committed;
            const bool activePopup = m_surfaces.PeekCommittedContextMenuRequest(committed) && committed.owner == owner;
            m_surfaces.RevokeOwner(owner);
            if (activePopup)
                m_contextPopupOpenedRequestFrame = 0;
            m_surfaceInteractionActive = false;
        }

        [[nodiscard]] bool RegisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridge& bridge,
                                                       editor::detail::EditorUiCommandBridgeToken& token,
                                                       editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaceCommands.Register(bridge, token, failure);
        }

        [[nodiscard]] bool UnregisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridgeToken& token,
                                                         editor::EditorUiFailure* const failure) noexcept override
        {
            if (!m_surfaceCommands.Unregister(token, failure))
                return false;
            ClearSurfaceInteractionState();
            return true;
        }

        [[nodiscard]] bool RegisterPanelFocusSink(const editor::detail::EditorPanelFocusSink sink, void* const userData,
                                                 editor::detail::EditorPanelFocusSinkToken& token,
                                                 editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            token = {};
            if (!vanguard::concurrency::IsMainThread())
                return Fail(failure, editor::EditorUiFailureCode::WrongThread,
                            "editor panel focus sink registration requires the main thread");
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor panel focus sink registration requires a ready UI service");
            if (sink == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor,
                            "editor panel focus sink callback is invalid");
            if (m_focusSink != nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InUse,
                            "editor panel focus sink is already registered");
            const vanguard::u32 generation = NextGeneration(m_focusSinkGeneration);
            m_focusSinkGeneration = generation;
            m_focusSink = sink;
            m_focusSinkUserData = userData;
            m_focusSinkToken = {generation};
            token = m_focusSinkToken;
            return true;
        }

        [[nodiscard]] bool UnregisterPanelFocusSink(const editor::detail::EditorPanelFocusSinkToken token,
                                                   editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return Fail(failure, editor::EditorUiFailureCode::WrongThread,
                            "editor panel focus sink removal requires the main thread");
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase,
                            "editor panel focus sink removal requires a ready UI service");
            if (!token.IsValid() || token != m_focusSinkToken || m_focusSink == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle,
                            "editor panel focus sink token is stale or invalid");
            if (m_panelFocus.HasFocus())
            {
                editor::EditorPanelFocusSnapshot cleared;
                cleared.frame = m_frame;
                if (!DispatchPanelFocus(cleared, nullptr, failure))
                    return false;
                m_panelFocus = cleared;
            }
            m_focusSink = nullptr;
            m_focusSinkUserData = nullptr;
            m_focusSinkToken = {};
            return true;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = vanguard::engine::FindFramePipelineService(context);
            m_windows = vanguard::engine::FindWindowService(context);
            m_input = vanguard::engine::FindInputService(context);
            m_resources = editor::FindEditorResourceService(context);
            m_workspace = editor::FindProjectWorkspaceService(context);
            if (m_framePipeline == nullptr || m_windows == nullptr || m_input == nullptr || m_resources == nullptr || m_workspace == nullptr ||
                context.Find(vanguard::engine::RenderingServiceId) == nullptr)
                return app::LifecycleStatus::Failure("Editor UI dependencies are unavailable");
            editor::EditorUiFailure surfaceFailure;
            if (!m_surfaces.InitializeBuiltIns(&surfaceFailure))
                return app::LifecycleStatus::Failure(surfaceFailure.message != nullptr ? surfaceFailure.message
                                                                                      : "Editor surface initialization failed");
            constexpr vanguard::engine::FrameParticipantId dependencies[]{editor::EditorActivityFrameParticipantId};
            vanguard::engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorUiFrameParticipantId;
            descriptor.name = "editorUi";
            descriptor.phase = vanguard::engine::FramePhase::BeginFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            vanguard::engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
            {
                m_surfaces.Reset();
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message : "Editor UI frame registration failed");
            }
            constexpr vanguard::engine::FrameParticipantId renderDependencies[]{vanguard::engine::RenderingFrameTickParticipantId};
            descriptor.id = editor::EditorUiRenderParticipantId;
            descriptor.name = "editorUi.render";
            // Compose this frame's hosts after all scene viewport sources in Render.
            descriptor.phase = vanguard::engine::FramePhase::EndFrame;
            descriptor.after = {renderDependencies, 1};
            descriptor.execute = RenderFrame;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
            {
                m_surfaces.Reset();
                return app::LifecycleStatus::Failure("Editor UI render participant registration failed");
            }
            m_state = editor::EditorUiState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext& context) noexcept override
        {
            editor::EditorUiFailure failure;
            auto* rendering = vanguard::engine::FindRenderingService(context);
            if (rendering == nullptr || !m_renderer.Initialize(*rendering, m_windows->GetManager(), &failure))
            {
                m_renderer.Shutdown();
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI renderer initialization failed");
            }
            if (!m_platform.Initialize(*this, *m_resources, *m_windows, *m_input, rendering->IsFrameDiagnosticsEnabled(), &failure))
            {
                m_platform.Shutdown();
                m_renderer.Shutdown();
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI platform initialization failed");
            }
            RefreshStatusBarSnapshot(context, *rendering);
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            if (m_state == editor::EditorUiState::BuildingFrame)
                return app::LifecycleStatus::Failure("Editor UI cannot quiesce while a frame is being built");
            if (m_surfaceCommands.IsAttached())
                return app::LifecycleStatus::Failure("Editor UI cannot quiesce while the surface command bridge is still registered");
            if (m_focusSink != nullptr)
                return app::LifecycleStatus::Failure("Editor UI cannot quiesce while the panel focus sink is still registered");
            m_platform.Shutdown();
            m_renderer.Shutdown();
            m_textureFrameData.Reset();
            m_inputCapture = {};
            m_statusBar = {};
            m_panelFocus = {};
            m_candidatePanelFocus = {};
            m_candidatePanelReceiver = nullptr;
            ClearSurfaceInteractionState();
            ResetDockLayout();
            m_state = editor::EditorUiState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_platform.Shutdown();
            m_renderer.Shutdown();
            m_textureFrameData.Reset();
            m_inputCapture = {};
            m_statusBar = {};
            m_panelFocus = {};
            m_candidatePanelFocus = {};
            m_candidatePanelReceiver = nullptr;
            m_focusSink = nullptr;
            m_focusSinkUserData = nullptr;
            m_focusSinkToken = {};
            m_surfaceCommands.Reset();
            ClearSurfaceInteractionState();
            m_surfaces.Reset();
            m_panels.Reset();
            ResetDockLayout();
            for (TextureRecord& texture : m_textures)
            {
                texture.texture.Reset();
                texture.sampler.Reset();
                texture.publication = {};
                texture.active = false;
            }
            m_hosts.Clear();
            m_state = editor::EditorUiState::Offline;
            m_frame = 0;
            m_framePipeline = nullptr;
            m_windows = nullptr;
            m_input = nullptr;
            m_resources = nullptr;
            m_workspace = nullptr;
            m_startupReadinessComplete = false;
            m_startupReadinessBeginSeconds = -1.0;
            return app::LifecycleStatus::Success();
        }

    private:
        struct HostRecord
        {
            editor::EditorHostWindowHandle handle;
            editor::EditorHostWindowDesc desc;
            editor::EditorUiFrameData frameData;
            vanguard::u32 generation = 0;
            bool active = false;
        };

        struct TextureRecord
        {
            editor::EditorTextureHandle handle;
            vanguard::rhi::Texture texture;
            vanguard::rhi::SamplerState sampler;
            editor::EditorTextureColorSpace colorSpace = editor::EditorTextureColorSpace::DisplaySrgb;
            editor::EditorTextureRenderOutputPublication publication;
            vanguard::u32 generation = 0;
            bool active = false;
        };

        struct SurfaceAuthoringList final
        {
            vanguard::containers::StaticArray<editor::detail::EditorUiContributionView,
                                              editor::MaximumEditorUiContributions> contributions;
        };

        static void CollectSurfaceContribution(const editor::detail::EditorUiContributionView& contribution,
                                               void* const userData) noexcept
        {
            auto& list = *static_cast<SurfaceAuthoringList*>(userData);
            if (list.contributions.Size() < editor::MaximumEditorUiContributions)
                list.contributions.PushBack(contribution);
        }

        void DrawPersistentSurfaces() noexcept
        {
            DrawMainMenu();
            DrawMainToolbar();
            DrawMainStatusBar();
            DrawNamedLayoutDialog();
        }

        void RefreshStartupReadinessLatch() noexcept
        {
            if (m_startupReadinessComplete)
                return;
            if (m_startupReadinessBeginSeconds < 0.0)
                m_startupReadinessBeginSeconds = ImGui::GetTime();
            const vanguard::asset_streaming::ProjectAssetPipeline* const pipeline =
                m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
            if (pipeline != nullptr && pipeline->IsInitialized() && pipeline->CanStartAssetOperation())
                m_startupReadinessComplete = true;
        }

        void DrawStartupReadinessOverlay() noexcept
        {
            ImGuiViewport* const viewport = ImGui::GetMainViewport();
            if (viewport == nullptr)
                return;
            const vanguard::asset_streaming::ProjectAssetPipeline* const pipeline =
                m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
            if (pipeline == nullptr || !pipeline->IsInitialized())
                return;

            const auto observation = pipeline->GetObservationStatus();
            const auto automatic = pipeline->GetAutomaticRecookStatus();
            const double elapsedSeconds = m_startupReadinessBeginSeconds >= 0.0
                                              ? std::max(0.0, ImGui::GetTime() - m_startupReadinessBeginSeconds)
                                              : 0.0;

            const ImGuiWindowFlags overlayFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowBgAlpha(0.965f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, editor::detail::EditorVisualColor(0x171717u));
            if (ImGui::Begin("##VanguardStartupReadinessOverlay", nullptr, overlayFlags))
            {
                const float margin = editor::detail::EditorVisualPx(32.0f);
                const float maximumWidth = editor::detail::EditorVisualPx(680.0f);
                const float maximumHeight = editor::detail::EditorVisualPx(420.0f);
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const ImVec2 cardSize{
                    std::max(editor::detail::EditorVisualPx(360.0f), std::min(maximumWidth, available.x - margin * 2.0f)),
                    std::max(editor::detail::EditorVisualPx(300.0f), std::min(maximumHeight, available.y - margin * 2.0f))};
                const ImVec2 cardPos{
                    std::max(0.0f, (available.x - cardSize.x) * 0.5f),
                    std::max(0.0f, (available.y - cardSize.y) * 0.5f)};
                ImGui::SetCursorPos(cardPos);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, editor::detail::EditorVisualPx(6.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                    {editor::detail::EditorVisualPx(28.0f), editor::detail::EditorVisualPx(24.0f)});
                ImGui::PushStyleColor(ImGuiCol_ChildBg, editor::detail::EditorVisualColor(0x232323u));
                if (ImGui::BeginChild("##VanguardStartupReadinessCard", cardSize, true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                {
                    ImGui::TextUnformatted("VANGUARD EDITOR");
                    const char* const projectName =
                        m_workspace->GetProject().name.Empty()
                            ? m_workspace->GetProject().technicalName.AsChar()
                            : m_workspace->GetProject().name.AsChar();
                    ImGui::TextDisabled("%s", projectName != nullptr && projectName[0] != '\0' ? projectName : "Project");
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Preparing project workspace");
                    ImGui::TextDisabled("Asset operations stay locked until project state is authoritative.");
                    ImGui::Separator();

                    ImGui::Text("Editor services");
                    ImGui::SameLine();
                    ImGui::TextDisabled("Ready");

                    ImGui::Text("Source observation");
                    ImGui::SameLine();
                    if (!observation.running)
                        ImGui::TextDisabled("Starting");
                    else if (observation.rescanRequired || observation.reconcilePending)
                        ImGui::TextDisabled("Scanning / reconciling");
                    else
                        ImGui::TextDisabled("Caught up");
                    ImGui::TextDisabled("  watched roots: %u   pending paths: %u   ready paths: %u   reconciled records: %u",
                                        observation.watchedRoots, observation.pendingPaths, observation.readyPaths,
                                        observation.reconcileOperationsCompleted);

                    ImGui::Text("Asset recovery");
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", StartupAutomaticRecookStateName(automatic.state));
                    if (automatic.buildOperations != 0u)
                    {
                        const vanguard::u32 completed = std::min(automatic.completedBuildOperations, automatic.buildOperations);
                        const float fraction = static_cast<float>(completed) / static_cast<float>(automatic.buildOperations);
                        char label[96]{};
                        std::snprintf(label, sizeof(label), "%u / %u build operations",
                                      completed, automatic.buildOperations);
                        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label);
                    }
                    else if (automatic.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Pending ||
                             automatic.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Planning ||
                             automatic.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Building ||
                             automatic.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Validating)
                    {
                        ImGui::TextDisabled("  waiting for countable build work");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextUnformatted("Project operations");
                    ImGui::SameLine();
                    ImGui::TextDisabled("Waiting for safe asset-operation gate");
                    ImGui::TextDisabled("Elapsed: %.1f s", elapsedSeconds);
                    if (observation.rescanRequired || observation.reconcilePending)
                        ImGui::TextDisabled("Current step: validating project sources and accepted asset state.");
                    else if (automatic.state != vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                             automatic.state != vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Disabled)
                        ImGui::TextDisabled("Current step: settling project asset recovery/build work.");
                    else
                        ImGui::TextDisabled("Current step: waiting for the final mutation-safety gate.");
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        void RefreshStatusBarSnapshot(app::ServiceContext& context,
                                      const vanguard::engine::RenderingService& rendering) noexcept
        {
            m_statusBar = {};

            CopyStatusBarText(m_statusBar.state, "Ready");
            CopyStatusBarText(m_statusBar.build, EditorBuildConfigurationName());
            if (editor::ProjectWorkspaceService* const workspace = editor::FindProjectWorkspaceService(context);
                workspace != nullptr)
            {
                CopyStatusBarText(m_statusBar.project, workspace->GetProject().technicalName.AsChar());
            }

            const vanguard::engine::RenderingDeviceInfo device = rendering.GetDeviceInfo();
            CopyStatusBarText(m_statusBar.backend, EditorBackendName(device.backend));
            CopyStatusBarText(m_statusBar.adapter,
                              device.available && device.adapterName[0] != '\0' ? device.adapterName : "GPU unavailable");
            if (device.available && device.dedicatedVideoMemory != 0u)
            {
                constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
                const double gib = static_cast<double>(device.dedicatedVideoMemory) / GiB;
                std::snprintf(m_statusBar.memory, sizeof(m_statusBar.memory), "%.1f GiB VRAM", gib);
            }
            BuildStatusBarDeviceText(m_statusBar);
        }

        void DrawNamedLayoutDialog() noexcept
        {
            using DialogKind = editor::detail::EditorUiNamedLayoutDialogKind;
            if (m_namedLayoutDialogKind == DialogKind::None)
                return;
            constexpr const char* PopupId = "Layout Management###VanguardNamedLayoutDialog";
            if (m_namedLayoutDialogOpenRequested)
            {
                ImGui::OpenPopup(PopupId);
                m_namedLayoutDialogOpenRequested = false;
            }
            if (!ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            m_surfaceInteractionActive = true;

            const DialogKind kind = m_namedLayoutDialogKind;
            const bool nameDialog = kind == DialogKind::SaveAs || kind == DialogKind::Rename ||
                                    kind == DialogKind::Duplicate;
            if (kind == DialogKind::SaveOverwrite)
            {
                ImGui::TextUnformatted("Overwrite the saved active layout with the current presentation?");
                ImGui::TextDisabled("Panel/content state is not part of a named layout.");
            }
            else if (nameDialog)
            {
                const char* prompt = kind == DialogKind::SaveAs
                    ? "Name the new layout"
                    : (kind == DialogKind::Rename ? "Rename the active layout" : "Name the duplicated layout");
                ImGui::TextUnformatted(prompt);
                ImGui::SetNextItemWidth(editor::detail::EditorVisualPx(360.0f));
                if (ImGui::InputText("##layoutName", m_namedLayoutName, sizeof(m_namedLayoutName)))
                    m_namedLayoutNameByteCount = static_cast<u32>(std::strlen(m_namedLayoutName));
            }
            else
            {
                ImGui::TextUnformatted(kind == DialogKind::Delete
                                           ? "Choose a layout to delete"
                                           : "Choose a layout to switch to");
                ImGui::BeginChild("##layoutChoices", {editor::detail::EditorVisualPx(420.0f),
                                                       editor::detail::EditorVisualPx(220.0f)}, true);
                for (u32 index = 0; index < m_namedLayoutChoiceCount; ++index)
                {
                    const auto& choice = m_namedLayoutChoices[index];
                    char label[editor::detail::MaximumEditorUiNamedLayoutNameBytes + 32u]{};
                    std::snprintf(label, sizeof(label), "%s%s###layout_%016llx", choice.name,
                                  choice.modified ? " *" : "",
                                  static_cast<unsigned long long>(choice.id));
                    if (ImGui::Selectable(label, choice.id == m_namedLayoutSelectedId))
                        m_namedLayoutSelectedId = choice.id;
                }
                ImGui::EndChild();
                if (kind == DialogKind::Delete && m_namedLayoutSelectedId != 0)
                    ImGui::TextDisabled("This removes the saved layout. The current panel/content state is not destroyed.");
            }

            const bool canAccept = kind == DialogKind::SaveOverwrite
                ? m_namedLayoutSelectedId != 0
                : (nameDialog ? m_namedLayoutName[0] != '\0' : m_namedLayoutSelectedId != 0);
            const char* acceptLabel = kind == DialogKind::SaveAs   ? "Save"
                                      : kind == DialogKind::SaveOverwrite ? "Overwrite"
                                      : kind == DialogKind::Rename ? "Rename"
                                      : kind == DialogKind::Duplicate ? "Duplicate"
                                      : kind == DialogKind::Delete ? "Delete"
                                                                   : "Switch";
            if (!canAccept)
                ImGui::BeginDisabled();
            if (ImGui::Button(acceptLabel))
            {
                m_namedLayoutDialogResult = {};
                m_namedLayoutDialogResult.kind = kind;
                m_namedLayoutDialogResult.id = m_namedLayoutSelectedId;
                m_namedLayoutDialogResult.accepted = true;
                if (nameDialog)
                {
                    m_namedLayoutDialogResult.nameByteCount =
                        static_cast<u32>(std::strlen(m_namedLayoutName));
                    std::memcpy(m_namedLayoutDialogResult.name, m_namedLayoutName,
                                m_namedLayoutDialogResult.nameByteCount + 1u);
                }
                m_namedLayoutDialogResultPending = true;
                m_namedLayoutDialogKind = DialogKind::None;
                ImGui::CloseCurrentPopup();
            }
            if (!canAccept)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                m_namedLayoutDialogResult = {};
                m_namedLayoutDialogResult.kind = kind;
                m_namedLayoutDialogResult.accepted = false;
                m_namedLayoutDialogResultPending = true;
                m_namedLayoutDialogKind = DialogKind::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        void DrawMainMenu() noexcept
        {
            SurfaceAuthoringList list;
            m_surfaces.VisitContributions(editor::detail::EditorMainMenuSurfaceId, CollectSurfaceContribution, &list);
            const ImVec2 headerFramePadding{ImGui::GetStyle().FramePadding.x, editor::detail::EditorVisualPx(12.0f)};
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, headerFramePadding);
            if (!ImGui::BeginMainMenuBar())
            {
                ImGui::PopStyleVar();
                return;
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                m_surfaceInteractionActive = true;
            const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            const float rowHeight = ImGui::GetFrameHeight();
            ImTextureData* wordmark = nullptr;
            float wordmarkAspect = 0.0f;
            if (m_platform.GetVanguardWordmarkTexture(wordmark, wordmarkAspect))
            {
                const float height = editor::detail::EditorMainMenuBrandHeight();
                const float width = height * wordmarkAspect;
                const ImVec2 brandMinimum{rowOrigin.x + editor::detail::EditorMainMenuBrandLeftPadding(),
                                          rowOrigin.y + (rowHeight - height) * 0.5f};
                const ImVec2 brandMaximum{brandMinimum.x + width, brandMinimum.y + height};
                ImGui::GetWindowDrawList()->AddImage(wordmark->GetTexRef(), brandMinimum, brandMaximum,
                                                     {0.0f, 0.0f}, {1.0f, 1.0f},
                                                     ImGui::ColorConvertFloat4ToU32(editor::detail::EditorVisualColor(0xb8b8b8u)));
                const float separatorX = brandMaximum.x + editor::detail::EditorMainMenuBrandRightPadding();
                const float separatorInset = editor::detail::EditorVisualPx(3.0f);
                ImGui::GetWindowDrawList()->AddLine({separatorX, rowOrigin.y + separatorInset},
                                                    {separatorX, rowOrigin.y + rowHeight - separatorInset},
                                                    ImGui::ColorConvertFloat4ToU32(editor::detail::EditorVisualColor(0x4a4a4au)),
                                                    editor::detail::EditorVisualPx(1.0f));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (separatorX + editor::detail::EditorVisualPx(11.0f) - rowOrigin.x));
            }
            editor::detail::PushEditorMenuPopupStyle();
            DrawSurfaceChildren(list, {}, true, false);
            editor::detail::PopEditorMenuPopupStyle();

            const float menuEndX = ImGui::GetCursorScreenPos().x;
            ImGuiViewport* const viewport = ImGui::GetMainViewport();
            const window::WindowHandle primaryWindow = m_windows != nullptr ? m_windows->GetPrimaryWindow() : window::WindowHandle{};
            window::WindowSnapshot primaryState;
            if (viewport != nullptr && primaryWindow.IsValid() && m_windows->GetManager().GetSnapshot(primaryWindow, primaryState))
            {
                constexpr float rightPadding = 0.0f;
                const float buttonWidth = editor::detail::EditorChromeWindowButtonWidth();
                const float controlWidth = buttonWidth * 3.0f;
                const ImVec2 barPosition = ImGui::GetWindowPos();
                const ImVec2 barSize = ImGui::GetWindowSize();
                const float controlsStartX = barPosition.x + barSize.x - controlWidth - rightPadding;
                const float dragStartX = menuEndX + editor::detail::EditorVisualPx(14.0f);
                const float dragEndX = controlsStartX - editor::detail::EditorVisualPx(8.0f);

                window::WindowHitTestConfig hitTest;
                hitTest.enabled = true;
                const float resizeBorder = editor::detail::EditorVisualPx(6.0f);
                hitTest.resizeBorder = resizeBorder > 1.0f ? static_cast<u32>(resizeBorder) : 1u;

                if (dragEndX > dragStartX)
                {
                    char title[editor::MaximumEditorUiSurfaceStringBytes]{};
                    MakeEditorChromeTitle(primaryState.title, title);
                    if (title[0] != '\0')
                    {
                        const ImVec2 titleSize = ImGui::CalcTextSize(title);
                        const float available = dragEndX - dragStartX;
                        if (titleSize.x + editor::detail::EditorVisualPx(24.0f) < available)
                        {
                            const float titleX = dragStartX + (available - titleSize.x) * 0.5f;
                            const float titleY = rowOrigin.y + (rowHeight - titleSize.y) * 0.5f;
                            ImGui::GetWindowDrawList()->AddText({titleX, titleY},
                                                              ImGui::ColorConvertFloat4ToU32(editor::detail::EditorVisualColor(0x8f8f8fu)),
                                                              title);
                        }
                    }

                    const float localLeft = dragStartX - viewport->Pos.x;
                    const float localTop = barPosition.y - viewport->Pos.y;
                    const float localRight = dragEndX - viewport->Pos.x;
                    const float localBottom = barPosition.y + barSize.y - viewport->Pos.y;
                    const float maximumWidth = static_cast<float>(primaryState.nativeState.placement.logicalExtent.width);
                    const float maximumHeight = static_cast<float>(primaryState.nativeState.placement.logicalExtent.height);
                    const float clampedLeft = localLeft > 0.0f ? localLeft : 0.0f;
                    const float clampedTop = localTop > 0.0f ? localTop : 0.0f;
                    const float clampedRight = localRight < maximumWidth ? localRight : maximumWidth;
                    const float clampedBottom = localBottom < maximumHeight ? localBottom : maximumHeight;
                    if (clampedRight > clampedLeft && clampedBottom > clampedTop)
                    {
                        hitTest.dragRegion.origin = {static_cast<vanguard::i32>(clampedLeft), static_cast<vanguard::i32>(clampedTop)};
                        hitTest.dragRegion.extent = {static_cast<u32>(clampedRight - clampedLeft),
                                                     static_cast<u32>(clampedBottom - clampedTop)};
                    }
                }

                {
                    window::Failure ignored;
                    static_cast<void>(m_windows->GetManager().SetHitTest(primaryWindow, hitTest, &ignored));
                }

                const float controlsY = rowOrigin.y;
                ImGui::SetCursorScreenPos({controlsStartX, controlsY});
                if (editor::detail::DrawEditorChromeWindowButton(editor::detail::EditorChromeWindowButton::Minimize))
                {
                    window::Failure ignored;
                    static_cast<void>(m_windows->GetManager().RequestAction(primaryWindow, window::WindowAction::Minimize, &ignored));
                }
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    m_surfaceInteractionActive = true;

                ImGui::SetCursorScreenPos({controlsStartX + buttonWidth, controlsY});
                const editor::detail::EditorChromeWindowButton maximizeButton = primaryState.nativeState.maximized
                    ? editor::detail::EditorChromeWindowButton::Restore
                    : editor::detail::EditorChromeWindowButton::Maximize;
                if (editor::detail::DrawEditorChromeWindowButton(maximizeButton))
                {
                    window::Failure ignored;
                    static_cast<void>(m_windows->GetManager().RequestAction(
                        primaryWindow, primaryState.nativeState.maximized ? window::WindowAction::Restore : window::WindowAction::Maximize, &ignored));
                }
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    m_surfaceInteractionActive = true;

                ImGui::SetCursorScreenPos({controlsStartX + buttonWidth * 2.0f, controlsY});
                if (editor::detail::DrawEditorChromeWindowButton(editor::detail::EditorChromeWindowButton::Close))
                {
                    window::Failure ignored;
                    static_cast<void>(m_windows->GetManager().RequestAction(primaryWindow, window::WindowAction::RequestClose, &ignored));
                }
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    m_surfaceInteractionActive = true;
            }
            ImGui::EndMainMenuBar();
            ImGui::PopStyleVar();
        }

        void DrawMainToolbar() noexcept
        {
            SurfaceAuthoringList list;
            m_surfaces.VisitContributions(editor::detail::EditorMainToolbarSurfaceId, CollectSurfaceContribution, &list);
            bool hasAuthoredContent = false;
            for (const editor::detail::EditorUiContributionView& contribution : list.contributions)
                hasAuthoredContent = hasAuthoredContent || !contribution.builtInAnchor;
            if (!hasAuthoredContent)
                return;
            ImGuiViewport* const viewport = ImGui::GetMainViewport();
            if (viewport == nullptr)
                return;
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus;
            const float height = editor::detail::EditorMainToolbarHeight();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, editor::detail::EditorVisualColor(0x282828u));
            const bool visible = ImGui::BeginViewportSideBar("##VanguardMainToolbar", viewport, ImGuiDir_Up, height, flags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (visible)
            {
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                    m_surfaceInteractionActive = true;
                const ImVec2 position = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                const float buttonExtent = editor::detail::EditorMainToolbarButtonExtent();
                ImGui::SetCursorScreenPos({position.x + editor::detail::EditorVisualPx(4.0f),
                                           position.y + (size.y - buttonExtent) * 0.5f});
                bool first = true;
                for (const editor::detail::EditorUiContributionView& contribution : list.contributions)
                {
                    if (contribution.parent.IsValid() || contribution.builtInAnchor)
                        continue;
                    if (!first)
                        ImGui::SameLine(0.0f, editor::detail::EditorMainToolbarItemSpacing());
                    DrawToolbarContribution(contribution);
                    first = false;
                }
            }
            ImGui::End();
        }

        void DrawMainStatusBar() noexcept
        {
            ImGuiViewport* const viewport = ImGui::GetMainViewport();
            if (viewport == nullptr)
                return;

            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus |
                                               ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus;
            const float height = editor::detail::EditorMainStatusBarHeight();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, editor::detail::EditorVisualColor(0x222222u));
            const bool visible = ImGui::BeginViewportSideBar("##VanguardMainStatusBar", viewport, ImGuiDir_Down, height, flags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (visible)
            {
                const ImVec2 position = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                ImDrawList* const draw = ImGui::GetWindowDrawList();
                const float padding = editor::detail::EditorMainStatusBarHorizontalPadding();
                const float spacing = editor::detail::EditorMainStatusBarItemSpacing();
                const float lineHeight = ImGui::GetTextLineHeight();
                const float textY = position.y + std::max(0.0f, (size.y - lineHeight) * 0.5f);
                const float left = position.x + padding;
                const float right = position.x + size.x - padding;

                draw->AddLine(position, {position.x + size.x, position.y},
                              ImGui::ColorConvertFloat4ToU32(editor::detail::EditorVisualColor(0x464646u)),
                              std::max(1.0f, editor::detail::EditorVisualPx(1.0f)));

                const bool startupLoading = !m_startupReadinessComplete;
                const char* const stateText = startupLoading ? "Loading" : m_statusBar.state;
                const ImU32 stateColor = ImGui::ColorConvertFloat4ToU32(
                    editor::detail::EditorVisualColor(startupLoading ? 0xd5aa63u : 0x88b47du));
                const ImU32 secondaryText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                draw->AddText({left, textY}, stateColor, stateText);
                float leftEnd = left + ImGui::CalcTextSize(stateText).x;
                if (m_statusBar.project[0] != '\0')
                {
                    leftEnd += spacing;
                    draw->AddLine({leftEnd - spacing * 0.5f, textY + editor::detail::EditorVisualPx(2.0f)},
                                  {leftEnd - spacing * 0.5f, textY + lineHeight - editor::detail::EditorVisualPx(2.0f)},
                                  ImGui::GetColorU32(ImGuiCol_Separator));
                    draw->AddText({leftEnd, textY}, secondaryText, m_statusBar.project);
                    leftEnd += ImGui::CalcTextSize(m_statusBar.project).x;
                }

                const float fullWidth = ImGui::CalcTextSize(m_statusBar.fullDevice).x;
                const float compactWidth = ImGui::CalcTextSize(m_statusBar.compactDevice).x;
                const float adapterWidth = ImGui::CalcTextSize(m_statusBar.adapter).x;
                const float rightBudget = std::max(0.0f, right - leftEnd - spacing * 2.0f);
                const char* rightText = nullptr;
                float rightTextWidth = 0.0f;
                if (fullWidth <= rightBudget)
                {
                    rightText = m_statusBar.fullDevice;
                    rightTextWidth = fullWidth;
                }
                else if (compactWidth <= rightBudget)
                {
                    rightText = m_statusBar.compactDevice;
                    rightTextWidth = compactWidth;
                }
                else if (adapterWidth <= rightBudget)
                {
                    rightText = m_statusBar.adapter;
                    rightTextWidth = adapterWidth;
                }

                if (rightText != nullptr && rightText[0] != '\0')
                {
                    draw->AddText({right - rightTextWidth, textY}, secondaryText, rightText);
                }
                else if (rightBudget > editor::detail::EditorVisualPx(48.0f) && m_statusBar.adapter[0] != '\0')
                {
                    const ImVec2 minimum{leftEnd + spacing * 2.0f, textY};
                    const ImVec2 maximum{right, textY + lineHeight};
                    const ImVec2 adapterSize = ImGui::CalcTextSize(m_statusBar.adapter);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::RenderTextEllipsis(draw, minimum, maximum, maximum.x, m_statusBar.adapter, nullptr, &adapterSize);
                    ImGui::PopStyleColor();
                }
            }
            ImGui::End();
        }

        [[nodiscard]] bool DrawToolbarIconButton(const editor::EditorResourceId iconResource,
                                                 const char* const label, const bool enabled,
                                                 const bool checked) noexcept
        {
            editor::detail::EditorSvgIcon* const icon = m_platform.FindSvgIcon(iconResource);
            if (icon == nullptr)
                return false;
            const float buttonExtent = editor::detail::EditorMainToolbarButtonExtent();
            const float iconExtent = editor::detail::EditorMainToolbarIconExtent();
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            const bool activated = ImGui::InvisibleButton("##toolbarTool", {buttonExtent, buttonExtent});
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            const bool held = ImGui::IsItemActive();
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            if (checked || hovered || held)
            {
                const ImVec4 background = held
                    ? editor::detail::EditorVisualColor(0x315d7du)
                    : checked ? editor::detail::EditorVisualColor(0x365f7du)
                              : editor::detail::EditorVisualColor(0x3b3b3bu);
                draw->AddRectFilled(minimum, {minimum.x + buttonExtent, minimum.y + buttonExtent},
                                    ImGui::ColorConvertFloat4ToU32(background));
            }
            const ImVec4 iconColor = !enabled
                ? editor::detail::EditorVisualColor(0x70757au)
                : editor::detail::EditorVisualColor(hovered ? 0xf0f2f4u : 0xc9cdd2u);
            const ImVec2 iconMinimum{minimum.x + (buttonExtent - iconExtent) * 0.5f,
                                     minimum.y + (buttonExtent - iconExtent) * 0.5f};
            editor::detail::DrawEditorSvgIcon(*draw, *icon, iconMinimum, iconExtent,
                                              ImGui::ColorConvertFloat4ToU32(iconColor));
            if (hovered && label != nullptr && label[0] != '\0')
                ImGui::SetTooltip("%s", label);
            return enabled && activated;
        }

        void DrawToolbarSeparator() noexcept
        {
            const float width = editor::detail::EditorMainToolbarSeparatorWidth();
            const float height = editor::detail::EditorMainToolbarButtonExtent();
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            ImGui::Dummy({width, height});
            const float x = minimum.x + width * 0.5f;
            const float inset = editor::detail::EditorVisualPx(4.0f);
            ImGui::GetWindowDrawList()->AddLine({x, minimum.y + inset},
                                                {x, minimum.y + height - inset},
                                                ImGui::ColorConvertFloat4ToU32(
                                                    editor::detail::EditorVisualColor(0x454545u)),
                                                editor::detail::EditorVisualPx(1.0f));
        }

        void DrawToolbarContribution(const editor::detail::EditorUiContributionView& contribution) noexcept
        {
            char stableId[32]{};
            std::snprintf(stableId, sizeof(stableId), "toolbar_%016llx",
                          static_cast<unsigned long long>(contribution.id.value));
            ImGui::PushID(stableId);
            switch (contribution.kind)
            {
            case editor::detail::EditorUiContributionKind::Tool:
            {
                const bool bound = contribution.command.IsValid();
                bool enabled = true;
                bool checked = false;
                const editor::detail::EditorUiCommandBridge* const bridge = m_surfaceCommands.Get();
                editor::EditorCommandPresentation presentation;
                if (bound)
                {
                    const bool querySucceeded = bridge != nullptr &&
                        m_surfaces.QueryContributionPresentation(contribution, *bridge, presentation);
                    enabled = querySucceeded && presentation.state.kind == editor::EditorCommandStateKind::Enabled;
                    checked = enabled && presentation.state.checked;
                }
                const bool activated = DrawToolbarIconButton(contribution.icon, contribution.label, enabled, checked);
                if (activated || ImGui::IsItemActive() || ImGui::IsItemHovered())
                    m_surfaceInteractionActive = true;
                if (activated && bound && bridge != nullptr)
                    static_cast<void>(m_surfaces.InvokeContributionCommand(contribution, *bridge));
                break;
            }
            case editor::detail::EditorUiContributionKind::Command:
            {
                editor::EditorCommandPresentation presentation;
                const editor::detail::EditorUiCommandBridge* const bridge = m_surfaceCommands.Get();
                const bool querySucceeded = bridge != nullptr &&
                    m_surfaces.QueryContributionPresentation(contribution, *bridge, presentation);
                const bool enabled = querySucceeded && presentation.state.kind == editor::EditorCommandStateKind::Enabled;
                const bool checked = enabled && presentation.state.checked;
                char displayLabel[editor::MaximumEditorUiSurfaceStringBytes * 4u]{};
                MakeImGuiDisplayLabel(contribution.label, displayLabel);
                const bool activated = editor::detail::DrawEditorToolbarCommand(displayLabel, enabled, checked);
                if (activated || ImGui::IsItemActive() || ImGui::IsItemHovered())
                    m_surfaceInteractionActive = true;
                if (activated && bridge != nullptr)
                    static_cast<void>(m_surfaces.InvokeContributionCommand(contribution, *bridge));
                break;
            }
            case editor::detail::EditorUiContributionKind::Separator:
                DrawToolbarSeparator();
                break;
            case editor::detail::EditorUiContributionKind::Submenu:
                break;
            }
            ImGui::PopID();
        }

        void DrawSurfaceChildren(const SurfaceAuthoringList& list, const editor::EditorUiContributionId parent,
                                 const bool showShortcut, const bool skipBuiltInSeparators) noexcept
        {
            for (const editor::detail::EditorUiContributionView& contribution : list.contributions)
            {
                if (contribution.parent != parent)
                    continue;
                DrawSurfaceContribution(list, contribution, showShortcut, skipBuiltInSeparators);
            }
        }

        void DrawSurfaceContribution(const SurfaceAuthoringList& list,
                                     const editor::detail::EditorUiContributionView& contribution,
                                     const bool showShortcut, const bool skipBuiltInSeparators) noexcept
        {
            switch (contribution.kind)
            {
            case editor::detail::EditorUiContributionKind::Tool:
                return;
            case editor::detail::EditorUiContributionKind::Submenu:
            {
                if (contribution.label == nullptr)
                    return;
                char displayLabel[editor::MaximumEditorUiSurfaceStringBytes * 4u]{};
                MakeImGuiDisplayLabel(contribution.label, displayLabel);
                char stableId[32]{};
                std::snprintf(stableId, sizeof(stableId), "surface_%016llx",
                              static_cast<unsigned long long>(contribution.id.value));
                ImGui::PushID(stableId);
                const bool open = ImGui::BeginMenu(displayLabel);
                if (open || ImGui::IsItemActive() || ImGui::IsItemFocused())
                    m_surfaceInteractionActive = true;
                if (open)
                {
                    DrawSurfaceChildren(list, contribution.id, true, false);
                    ImGui::EndMenu();
                }
                ImGui::PopID();
                return;
            }
            case editor::detail::EditorUiContributionKind::Command:
            {
                if (contribution.label == nullptr)
                    return;
                editor::EditorCommandPresentation presentation;
                const editor::detail::EditorUiCommandBridge* const bridge = m_surfaceCommands.Get();
                const bool querySucceeded =
                    bridge != nullptr && m_surfaces.QueryContributionPresentation(contribution, *bridge, presentation);
                const bool enabled =
                    querySucceeded && presentation.state.kind == editor::EditorCommandStateKind::Enabled;
                const bool checked = enabled && presentation.state.checked;
                char shortcut[64]{};
                if (showShortcut && querySucceeded)
                    FormatShortcut(presentation.binding, shortcut);
                char displayLabel[editor::MaximumEditorUiSurfaceStringBytes * 4u]{};
                MakeImGuiDisplayLabel(contribution.label, displayLabel);
                char stableId[32]{};
                std::snprintf(stableId, sizeof(stableId), "surface_%016llx",
                              static_cast<unsigned long long>(contribution.id.value));
                ImGui::PushID(stableId);
                const bool activated = skipBuiltInSeparators
                    ? editor::detail::DrawEditorToolbarCommand(displayLabel, enabled, checked)
                    : ImGui::MenuItem(displayLabel, shortcut[0] != '\0' ? shortcut : nullptr, checked, enabled);
                if (activated || ImGui::IsItemActive() || ImGui::IsItemFocused())
                    m_surfaceInteractionActive = true;
                if (activated && bridge != nullptr)
                    static_cast<void>(m_surfaces.InvokeContributionCommand(contribution, *bridge));
                ImGui::PopID();
                return;
            }
            case editor::detail::EditorUiContributionKind::Separator:
                if (!(skipBuiltInSeparators && contribution.builtInAnchor))
                    ImGui::Separator();
                return;
            }
        }

        void DrawContextSurfaceChildren(const editor::detail::EditorUiContextMenuItems& items,
                                        const editor::EditorUiContributionId parent) noexcept
        {
            for (const editor::detail::EditorUiContextMenuItemView& item : items)
            {
                if (item.contribution.parent != parent)
                    continue;
                DrawContextSurfaceItem(items, item);
            }
        }

        void DrawContextSurfaceItem(const editor::detail::EditorUiContextMenuItems& items,
                                    const editor::detail::EditorUiContextMenuItemView& item) noexcept
        {
            const editor::detail::EditorUiContributionView& contribution = item.contribution;
            switch (contribution.kind)
            {
            case editor::detail::EditorUiContributionKind::Tool:
                return;
            case editor::detail::EditorUiContributionKind::Submenu:
            {
                if (contribution.label == nullptr)
                    return;
                char displayLabel[editor::MaximumEditorUiSurfaceStringBytes * 4u]{};
                MakeImGuiDisplayLabel(contribution.label, displayLabel);
                char stableId[32]{};
                std::snprintf(stableId, sizeof(stableId), "surface_%016llx",
                              static_cast<unsigned long long>(contribution.id.value));
                ImGui::PushID(stableId);
                const bool open = ImGui::BeginMenu(displayLabel);
                if (open || ImGui::IsItemActive() || ImGui::IsItemFocused())
                    m_surfaceInteractionActive = true;
                if (open)
                {
                    DrawContextSurfaceChildren(items, contribution.id);
                    ImGui::EndMenu();
                }
                ImGui::PopID();
                return;
            }
            case editor::detail::EditorUiContributionKind::Command:
            {
                if (contribution.label == nullptr)
                    return;
                const bool enabled = item.hasPresentation &&
                                     item.presentation.state.kind == editor::EditorCommandStateKind::Enabled;
                const bool checked = item.hasPresentation && item.presentation.state.checked;
                char shortcut[64]{};
                if (item.hasPresentation)
                    FormatShortcut(item.presentation.binding, shortcut);
                char displayLabel[editor::MaximumEditorUiSurfaceStringBytes * 4u]{};
                MakeImGuiDisplayLabel(contribution.label, displayLabel);
                char stableId[32]{};
                std::snprintf(stableId, sizeof(stableId), "surface_%016llx",
                              static_cast<unsigned long long>(contribution.id.value));
                ImGui::PushID(stableId);
                const bool activated =
                    ImGui::MenuItem(displayLabel, shortcut[0] != '\0' ? shortcut : nullptr, checked, enabled);
                if (activated || ImGui::IsItemActive() || ImGui::IsItemFocused())
                    m_surfaceInteractionActive = true;
                if (activated)
                    if (const editor::detail::EditorUiCommandBridge* const bridge = m_surfaceCommands.Get(); bridge != nullptr)
                        static_cast<void>(m_surfaces.InvokeContributionCommand(contribution, *bridge));
                ImGui::PopID();
                return;
            }
            case editor::detail::EditorUiContributionKind::Separator:
                ImGui::Separator();
                return;
            }
        }

        void DrawCommittedContextMenu(const editor::EditorPanelHandle panel,
                                      const editor::EditorHostWindowHandle authoredHost) noexcept
        {
            editor::detail::EditorUiCommittedContextMenuRequest request;
            if (!m_surfaces.PeekCommittedContextMenuRequest(request) || request.panel != panel)
                return;
            if (!authoredHost.IsValid() || request.host != authoredHost || FindHost(request.host) == nullptr)
            {
                m_surfaces.ClearCommittedContextMenuRequest();
                m_contextPopupOpenedRequestFrame = 0;
                return;
            }
            if (m_frame <= request.frame)
                return;

            editor::detail::EditorUiContextMenuItems items;
            m_surfaces.BuildContextMenuItems(request.surfaceId, m_surfaceCommands.Get(), items);
            if (items.Size() == 0)
            {
                m_surfaces.ClearCommittedContextMenuRequest();
                m_contextPopupOpenedRequestFrame = 0;
                return;
            }

            char popupId[96]{};
            std::snprintf(popupId, sizeof(popupId), "##context_%016llx_%08x_%08x",
                          static_cast<unsigned long long>(request.surfaceId.value), request.surface.index,
                          request.surface.generation);
            const bool firstAuthoringFrame = m_contextPopupOpenedRequestFrame != request.frame;
            if (firstAuthoringFrame)
            {
                ImGui::OpenPopup(popupId);
                m_contextPopupOpenedRequestFrame = request.frame;
            }
            ImGui::SetNextWindowPos({request.anchor.x, request.anchor.y}, ImGuiCond_Appearing);
            editor::detail::PushEditorMenuPopupStyle();
            if (ImGui::BeginPopup(popupId))
            {
                m_surfaceInteractionActive = true;
                DrawContextSurfaceChildren(items, {});
                ImGui::EndPopup();
                editor::detail::PopEditorMenuPopupStyle();
                return;
            }
            editor::detail::PopEditorMenuPopupStyle();

            m_surfaces.ClearCommittedContextMenuRequest();
            m_contextPopupOpenedRequestFrame = 0;
        }

        void ClearCurrentAuthoringStamp() noexcept
        {
            m_currentAuthoringPanel = {};
            m_currentAuthoringOwner = {};
            m_currentAuthoringHost = {};
            m_currentAuthoringFrame = 0;
        }

        void CancelContextMenuRequestsForPanel(const editor::EditorPanelHandle panel) noexcept
        {
            editor::detail::EditorUiCommittedContextMenuRequest request;
            const bool activePopup = m_surfaces.PeekCommittedContextMenuRequest(request) && request.panel == panel;
            m_surfaces.CancelContextMenuRequestsForPanel(panel);
            if (activePopup)
                m_contextPopupOpenedRequestFrame = 0;
        }

        void CancelContextMenuRequestsForHost(const editor::EditorHostWindowHandle host) noexcept
        {
            editor::detail::EditorUiCommittedContextMenuRequest request;
            const bool activePopup = m_surfaces.PeekCommittedContextMenuRequest(request) && request.host == host;
            m_surfaces.CancelContextMenuRequestsForHost(host);
            if (activePopup)
                m_contextPopupOpenedRequestFrame = 0;
        }

        void ClearSurfaceInteractionState() noexcept
        {
            m_surfaceInteractionActive = false;
            m_contextMenuRequestedPanel = {};
            m_contextMenuRequestedFocus = {};
            m_previousFocusedPanelAuthored = false;
            m_previousFocusedPanelHost = {};
            ClearCurrentAuthoringStamp();
            m_surfaces.DiscardPendingContextMenuRequests();
            m_surfaces.ClearCommittedContextMenuRequest();
            m_contextPopupOpenedRequestFrame = 0;
        }

        [[nodiscard]] bool CanUseRegistries(editor::EditorUiFailure* const failure) const noexcept
        {
            if (m_state == editor::EditorUiState::Ready || m_state == editor::EditorUiState::BuildingFrame)
                return true;
            return Fail(failure, editor::EditorUiFailureCode::NotReady, "editor UI registries require an active service");
        }

        [[nodiscard]] const HostRecord* FindHost(const editor::EditorHostWindowHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_hosts.Size() && m_hosts[handle.index].active && m_hosts[handle.index].generation == handle.generation
                       ? &m_hosts[handle.index]
                       : nullptr;
        }

        [[nodiscard]] HostRecord* FindHost(const editor::EditorHostWindowHandle handle) noexcept
        {
            return const_cast<HostRecord*>(static_cast<const ManagedEditorUiService*>(this)->FindHost(handle));
        }

        [[nodiscard]] const TextureRecord* FindTexture(const editor::EditorTextureHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_textures.Size() && m_textures[handle.index].active && m_textures[handle.index].generation == handle.generation
                       ? &m_textures[handle.index]
                       : nullptr;
        }

        [[nodiscard]] TextureRecord* FindTexture(const editor::EditorTextureHandle handle) noexcept
        {
            return const_cast<TextureRecord*>(static_cast<const ManagedEditorUiService*>(this)->FindTexture(handle));
        }

        [[nodiscard]] bool ValidHost(const editor::EditorHostWindowDesc& desc) const noexcept
        {
            return desc.window.IsValid() && (desc.presentationOutput.IsValid() == desc.renderViewport.IsValid());
        }

        [[nodiscard]] bool HasConflictingHost(const editor::EditorHostWindowDesc& desc, const editor::EditorHostWindowHandle ignored) const noexcept
        {
            for (const HostRecord& host : m_hosts)
                if (host.active && host.handle != ignored &&
                    (host.desc.window == desc.window || (desc.presentationOutput.IsValid() && host.desc.presentationOutput == desc.presentationOutput) ||
                     (desc.renderViewport.IsValid() && host.desc.renderViewport == desc.renderViewport) || (host.desc.primary && desc.primary)))
                    return true;
            return false;
        }

        [[nodiscard]] bool ValidTexture(const editor::EditorTextureDesc& desc) const noexcept
        {
            return desc.texture.IsValid() && desc.sampler.IsValid() &&
                   static_cast<vanguard::u32>(desc.colorSpace) <= static_cast<vanguard::u32>(editor::EditorTextureColorSpace::DisplaySrgb) &&
                   (!desc.publication.IsSpecified() || desc.publication.IsValid());
        }

        static void DrawPanelForAuthoring(const editor::detail::EditorPanelAuthoringView& panel, void* const userData) noexcept
        {
            auto& service = *static_cast<ManagedEditorUiService*>(userData);
            char displayTitle[editor::MaximumEditorPanelNameBytes]{};
            const char* const sourceTitle = panel.title != nullptr ? panel.title : "Panel";
            vanguard::u32 titleLength = 0;
            while (sourceTitle[titleLength] != '\0' && titleLength + 1u < editor::MaximumEditorPanelNameBytes)
            {
                displayTitle[titleLength] = sourceTitle[titleLength] == '#' ? '_' : sourceTitle[titleLength];
                ++titleLength;
            }
            char windowName[editor::MaximumEditorPanelNameBytes + 64]{};
            std::snprintf(windowName, sizeof(windowName), "%s###vanguard_panel_%016llx_%016llx",
                          displayTitle,
                          static_cast<unsigned long long>(panel.type), static_cast<unsigned long long>(panel.id));

            if (panel.initialPlacement == editor::EditorPanelInitialPlacement::Floating)
            {
                const ImGuiViewport* const main = ImGui::GetMainViewport();
                if (!panel.allowDocking)
                {
                    ImGui::SetNextWindowPos({main->WorkPos.x + main->WorkSize.x * 0.15f,
                                             main->WorkPos.y + main->WorkSize.y * 0.12f}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({editor::detail::EditorVisualPx(700.0f), editor::detail::EditorVisualPx(560.0f)},
                                             ImGuiCond_FirstUseEver);
                }
                else
                {
                    ImGui::SetNextWindowPos({main->WorkPos.x + main->WorkSize.x + editor::detail::EditorVisualPx(30.0f),
                                             main->WorkPos.y + editor::detail::EditorVisualPx(50.0f)}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({editor::detail::EditorVisualPx(440.0f), editor::detail::EditorVisualPx(340.0f)},
                                             ImGuiCond_FirstUseEver);
                }
            }
            else
            {
                const ImGuiID dock = service.DockForPlacement(panel.initialPlacement);
                if (dock != 0)
                    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
            }

            if (!panel.allowDocking)
            {
                // A saved dock ID survives an initial-placement change. Clear it every frame, so
                // old layouts cannot redock this independent review window on a later reopen.
                ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
                // An undocked ImGui window can still merge into the primary Editor viewport.
                // Non-dockable panels own a native viewport even while overlapping the Editor.
                ImGuiWindowClass standaloneWindowClass;
                standaloneWindowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
                ImGui::SetNextWindowClass(&standaloneWindowClass);

                // A clean, opaque panel surface and restrained one-pixel frame. Avoid
                // drawing shadows beyond the native viewport: they appear as translucent
                // fringes over the Editor and make the window edge look broken.
                ImGui::SetNextWindowBgAlpha(1.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, editor::detail::EditorVisualColor(0x303030u));
                ImGui::PushStyleColor(ImGuiCol_Border, editor::detail::EditorVisualColor(0x484848u));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, editor::detail::EditorVisualPx(1.0f));
            }
            bool open = true;
            const bool authorContents = ImGui::Begin(windowName, &open,
                                                      panel.allowDocking ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoDocking);
            if (!panel.allowDocking)
            {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
            editor::EditorHostWindowHandle authoredHost;
            static_cast<void>(service.m_platform.GetCurrentViewportHost(authoredHost));
            if (panel.handle == service.m_panelFocus.panel && authoredHost.IsValid())
            {
                service.m_previousFocusedPanelAuthored = true;
                service.m_previousFocusedPanelHost = authoredHost;
            }
            editor::EditorPanelContext context = service.MakePanelContext(panel.handle, panel.id, service.m_frame, authoredHost);
            if (!open)
                static_cast<void>(service.m_panels.SetOpen(panel.handle, false));
            if (authorContents && open)
            {
                if (panel.placeholder)
                {
                    ImGui::TextUnformatted("This panel type is currently unavailable.");
                    ImGui::Text("Type: 0x%016llX", static_cast<unsigned long long>(panel.type));
                    ImGui::Text("Instance: 0x%016llX", static_cast<unsigned long long>(panel.id));
                }
                else if (panel.draw != nullptr)
                {
                    service.m_currentAuthoringPanel = panel.handle;
                    service.m_currentAuthoringOwner = panel.owner;
                    service.m_currentAuthoringHost = authoredHost;
                    service.m_currentAuthoringFrame = service.m_frame;
                    panel.draw(context, panel.state, panel.factoryData);
                    service.ClearCurrentAuthoringStamp();

                    if (open && service.m_contextMenuRequestedPanel == panel.handle)
                        service.m_contextMenuRequestedFocus =
                            {service.m_frame, panel.handle, panel.type, panel.id, authoredHost};

                    void* liveReceiver = nullptr;
                    if (open && service.m_panels.ResolveFocusReceiver(panel.handle, liveReceiver))
                        service.DrawCommittedContextMenu(panel.handle, authoredHost);
                    else
                        service.CancelContextMenuRequestsForPanel(panel.handle);
                }
            }
            else
            {
                service.CancelContextMenuRequestsForPanel(panel.handle);
            }
            if (open && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
            {
                service.m_candidatePanelFocus = {service.m_frame, panel.handle, panel.type, panel.id, authoredHost};
                service.m_candidatePanelReceiver = panel.state;
            }
            ImGui::End();
        }

        void EnsurePrimaryDockspace() noexcept
        {
            const ImGuiViewport* const main = ImGui::GetMainViewport();
            m_dockRoot = ImGui::DockSpaceOverViewport(editor::detail::EditorPrimaryDockspaceId, main);
            if (m_dockRoot == 0)
                return;

            if (m_defaultDockLayoutInitialized)
            {
                if (m_restoredDockLayoutNeedsReconcile)
                {
                    ImGuiDockNode* const central = ImGui::DockBuilderGetCentralNode(m_dockRoot);
                    m_dockCenter = central != nullptr ? central->ID : m_dockRoot;
                    m_dockLeft = 0;
                    m_dockRight = 0;
                    m_dockBottom = 0;
                    m_restoredDockLayoutNeedsReconcile = false;
                }
                return;
            }

            ImGuiID remainder = m_dockRoot;
            m_dockLeft = ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Left, 0.20f, nullptr, &remainder);
            m_dockRight = ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.25f, nullptr, &remainder);
            m_dockBottom = ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Down, 0.25f, nullptr, &m_dockCenter);
            if (m_dockCenter == 0)
                m_dockCenter = remainder;
            ImGui::DockBuilderFinish(m_dockRoot);
            m_defaultDockLayoutInitialized = true;
        }

        [[nodiscard]] ImGuiID DockForPlacement(const editor::EditorPanelInitialPlacement placement) const noexcept
        {
            ImGuiID dock = m_dockCenter;
            switch (placement)
            {
            case editor::EditorPanelInitialPlacement::Left: dock = m_dockLeft; break;
            case editor::EditorPanelInitialPlacement::Right: dock = m_dockRight; break;
            case editor::EditorPanelInitialPlacement::Bottom: dock = m_dockBottom; break;
            case editor::EditorPanelInitialPlacement::Center: dock = m_dockCenter; break;
            case editor::EditorPanelInitialPlacement::Floating: return 0;
            }
            return dock != 0 && ImGui::DockBuilderGetNode(dock) != nullptr ? dock : m_dockRoot;
        }

        void ResetDockLayout() noexcept
        {
            m_defaultDockLayoutInitialized = false;
            m_restoredDockLayoutNeedsReconcile = false;
            m_dockRoot = 0;
            m_dockCenter = 0;
            m_dockLeft = 0;
            m_dockRight = 0;
            m_dockBottom = 0;
        }

        [[nodiscard]] bool DispatchPanelFocus(const editor::EditorPanelFocusSnapshot& focus, void* const receiverData,
                                              editor::EditorUiFailure* const failure) noexcept
        {
            if (m_focusSink == nullptr)
                return true;
            const editor::detail::EditorPanelFocusUpdate update{focus, receiverData};
            if (m_focusSink(update, m_focusSinkUserData, failure))
                return true;
            if (failure != nullptr && failure->code == editor::EditorUiFailureCode::None)
                *failure = {editor::EditorUiFailureCode::FocusSynchronizationFailure, focus.type, focus.instance,
                            focus.panel, focus.host, {}, "editor panel focus synchronization failed"};
            return false;
        }

        [[nodiscard]] bool CommitPanelFrame(editor::EditorUiFailure* const failure) noexcept
        {
            editor::EditorPanelFocusSnapshot focus = m_candidatePanelFocus;
            void* receiverData = m_candidatePanelReceiver;
            if (m_contextMenuRequestedFocus.HasFocus() && m_contextMenuRequestedFocus.frame == m_frame)
            {
                // Right-click on a panel item establishes focus even when the
                // underlying ImGui child window has not yet acquired it.
                focus = m_contextMenuRequestedFocus;
                receiverData = nullptr;
            }
            if (!focus.HasFocus() && m_surfaceInteractionActive && m_panelFocus.HasFocus() &&
                m_previousFocusedPanelAuthored && m_previousFocusedPanelHost == m_panelFocus.host &&
                FindHost(m_panelFocus.host) != nullptr)
            {
                void* leasedReceiver = nullptr;
                if (m_panels.ResolveFocusReceiver(m_panelFocus.panel, leasedReceiver))
                {
                    focus = m_panelFocus;
                    focus.frame = m_frame;
                    receiverData = leasedReceiver;
                }
            }
            if (focus.HasFocus())
            {
                void* resolvedReceiver = nullptr;
                if (!m_panels.ResolveFocusReceiver(focus.panel, resolvedReceiver))
                {
                    focus = {};
                    focus.frame = m_frame;
                    receiverData = nullptr;
                }
                else
                {
                    receiverData = resolvedReceiver;
                }
            }
            if (!DispatchPanelFocus(focus, receiverData, failure))
            {
                m_panels.DiscardPendingVisibility();
                m_candidatePanelFocus = {};
                m_candidatePanelReceiver = nullptr;
                ClearSurfaceInteractionState();
                return false;
            }
            m_panels.CommitPendingVisibility();
            m_panelFocus = focus;
            editor::detail::EditorUiCommittedContextMenuRequest activePopup;
            if (m_surfaces.PeekCommittedContextMenuRequest(activePopup) &&
                (!focus.HasFocus() || focus.panel != activePopup.panel || focus.host != activePopup.host))
            {
                m_surfaces.ClearCommittedContextMenuRequest();
                m_contextPopupOpenedRequestFrame = 0;
            }
            m_surfaces.CommitContextMenuRequests(focus);
            m_candidatePanelFocus = {};
            m_candidatePanelReceiver = nullptr;
            m_contextMenuRequestedPanel = {};
            m_contextMenuRequestedFocus = {};
            m_previousFocusedPanelAuthored = false;
            m_previousFocusedPanelHost = {};
            m_surfaceInteractionActive = false;
            return true;
        }

        static bool InvalidatePanelFocus(const editor::EditorPanelHandle panel,
                                         const editor::detail::EditorPanelInvalidationReason reason,
                                         void* const userData, editor::EditorUiFailure* const failure) noexcept
        {
            auto* const service = static_cast<ManagedEditorUiService*>(userData);
            if (service == nullptr)
                return true;
            service->CancelContextMenuRequestsForPanel(panel);
            if (service->m_contextMenuRequestedPanel == panel)
            {
                service->m_contextMenuRequestedPanel = {};
                service->m_contextMenuRequestedFocus = {};
            }
            if (service->m_candidatePanelFocus.panel == panel)
            {
                service->m_candidatePanelFocus = {};
                service->m_candidatePanelFocus.frame = service->m_frame;
                service->m_candidatePanelReceiver = nullptr;
            }
            if (service->m_panelFocus.panel != panel)
                return true;

            editor::EditorPanelFocusSnapshot cleared;
            cleared.frame = service->m_frame;
            editor::EditorUiFailure localFailure;
            editor::EditorUiFailure* const dispatchFailure = failure != nullptr ? failure : &localFailure;
            if (!service->DispatchPanelFocus(cleared, nullptr, dispatchFailure) &&
                reason != editor::detail::EditorPanelInvalidationReason::OwnerRevoke)
                return false;
            service->m_panelFocus = cleared;
            return true;
        }

        static vanguard::engine::FrameParticipantStatus ExecuteFrame(const vanguard::engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorUiService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("Editor UI frame service is unavailable");
            const u64 frequency = vanguard::system::GetMonotonicFrequency();
            const u64 beginTick = vanguard::system::GetMonotonicTicks();
            editor::EditorUiFailure failure;
            if (!service->BeginFrame(context, &failure))
            {
                service->AbortFrame();
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI frame failed");
            }
            const u64 afterBeginTick = vanguard::system::GetMonotonicTicks();
            if (!service->DrawOpenPanels(&failure))
            {
                service->AbortFrame();
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI frame failed");
            }
            const u64 afterDrawTick = vanguard::system::GetMonotonicTicks();
            if (!service->EndFrame(&failure))
            {
                service->AbortFrame();
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI frame failed");
            }
            const u64 afterEndTick = vanguard::system::GetMonotonicTicks();
            if (!service->CommitPanelFrame(&failure))
                return vanguard::engine::FrameParticipantStatus::Failure(
                    failure.message != nullptr ? failure.message : "Editor panel focus commit failed");
            const u64 finishedTick = vanguard::system::GetMonotonicTicks();
            if (frequency != 0)
            {
                const auto milliseconds = [frequency](const u64 from, const u64 to) noexcept
                {
                    return to >= from ? 1000.0 * static_cast<double>(to - from) / static_cast<double>(frequency) : 0.0;
                };
                const double beginMs = milliseconds(beginTick, afterBeginTick);
                const double drawMs = milliseconds(afterBeginTick, afterDrawTick);
                const double endMs = milliseconds(afterDrawTick, afterEndTick);
                const double commitMs = milliseconds(afterEndTick, finishedTick);
                const double totalMs = milliseconds(beginTick, finishedTick);
                if (totalMs >= 40.0)
                    VG_LOG_WARNING(vanguard::diagnostics::Category::Engine,
                                   "editor UI stall: frame=%llu totalMs=%.3f beginMs=%.3f drawMs=%.3f endMs=%.3f commitMs=%.3f",
                                   static_cast<unsigned long long>(context.frame), totalMs, beginMs, drawMs, endMs, commitMs);
            }
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        static vanguard::engine::FrameParticipantStatus RenderFrame(const vanguard::engine::FrameContext& context, void* userData) noexcept
        {
            auto& service = *static_cast<ManagedEditorUiService*>(userData);
            const u64 frequency = vanguard::system::GetMonotonicFrequency();
            const u64 beginTick = vanguard::system::GetMonotonicTicks();
            editor::EditorUiFailure failure;
            if (!service.m_renderer.SubmitTextures(service.m_textureFrameData, &failure))
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message);
            const u64 afterTexturesTick = vanguard::system::GetMonotonicTicks();
            // The retained upload frame now owns these pixels, including when all hosts are minimized.
            service.m_textureFrameData.Reset();
            for (const auto& host : service.m_hosts)
                if (host.active && !service.m_renderer.SubmitHost(host.frameData, &failure))
                    return vanguard::engine::FrameParticipantStatus::Failure(failure.message);
            const u64 finishedTick = vanguard::system::GetMonotonicTicks();
            if (frequency != 0)
            {
                const double textureMs = afterTexturesTick >= beginTick
                    ? 1000.0 * static_cast<double>(afterTexturesTick - beginTick) / static_cast<double>(frequency)
                    : 0.0;
                const double hostMs = finishedTick >= afterTexturesTick
                    ? 1000.0 * static_cast<double>(finishedTick - afterTexturesTick) / static_cast<double>(frequency)
                    : 0.0;
                const double totalMs = finishedTick >= beginTick
                    ? 1000.0 * static_cast<double>(finishedTick - beginTick) / static_cast<double>(frequency)
                    : 0.0;
                if (totalMs >= 40.0)
                    VG_LOG_WARNING(vanguard::diagnostics::Category::Engine,
                                   "editor UI render stall: frame=%llu totalMs=%.3f textureSubmitMs=%.3f hostSubmitMs=%.3f",
                                   static_cast<unsigned long long>(context.frame), totalMs, textureMs, hostMs);
            }
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        editor::EditorUiState m_state = editor::EditorUiState::Offline;
        editor::detail::EditorPanelRegistryCore m_panels;
        editor::detail::EditorSurfaceRegistryCore m_surfaces;
        editor::detail::EditorUiCommandBridgeSlot m_surfaceCommands;
        vanguard::containers::DynamicArray<HostRecord> m_hosts;
        vanguard::containers::DynamicArray<TextureRecord> m_textures;
        editor::EditorUiTextureFrameData m_textureFrameData;
        editor::EditorUiInputCapture m_inputCapture;
        editor::EditorPanelFocusSnapshot m_panelFocus;
        editor::EditorPanelFocusSnapshot m_candidatePanelFocus;
        void* m_candidatePanelReceiver = nullptr;
        editor::EditorPanelHandle m_contextMenuRequestedPanel;
        editor::EditorPanelFocusSnapshot m_contextMenuRequestedFocus;
        editor::EditorPanelHandle m_currentAuthoringPanel;
        editor::EditorExtensionOwner m_currentAuthoringOwner;
        editor::EditorHostWindowHandle m_currentAuthoringHost;
        vanguard::u64 m_currentAuthoringFrame = 0;
        editor::EditorHostWindowHandle m_previousFocusedPanelHost;
        bool m_previousFocusedPanelAuthored = false;
        bool m_surfaceInteractionActive = false;
        vanguard::u64 m_contextPopupOpenedRequestFrame = 0;
        editor::detail::EditorUiNamedLayoutChoice
            m_namedLayoutChoices[editor::detail::MaximumEditorUiNamedLayouts]{};
        editor::detail::EditorUiNamedLayoutDialogResult m_namedLayoutDialogResult;
        editor::detail::EditorUiNamedLayoutDialogKind m_namedLayoutDialogKind =
            editor::detail::EditorUiNamedLayoutDialogKind::None;
        vanguard::u32 m_namedLayoutChoiceCount = 0;
        vanguard::u64 m_namedLayoutSelectedId = 0;
        char m_namedLayoutName[editor::detail::MaximumEditorUiNamedLayoutNameBytes + 1u]{};
        vanguard::u32 m_namedLayoutNameByteCount = 0;
        bool m_namedLayoutDialogOpenRequested = false;
        bool m_namedLayoutDialogResultPending = false;
        editor::detail::EditorPanelFocusSink m_focusSink = nullptr;
        void* m_focusSinkUserData = nullptr;
        editor::detail::EditorPanelFocusSinkToken m_focusSinkToken;
        vanguard::u32 m_focusSinkGeneration = 0;
        editor::detail::EditorUiImGuiPlatform m_platform;
        editor::detail::EditorUiRenderer m_renderer;
        EditorStatusBarSnapshot m_statusBar;
        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::WindowService* m_windows = nullptr;
        vanguard::engine::InputService* m_input = nullptr;
        editor::EditorResourceService* m_resources = nullptr;
        editor::ProjectWorkspaceService* m_workspace = nullptr;
        vanguard::u64 m_frame = 0;
        double m_startupReadinessBeginSeconds = -1.0;
        bool m_startupReadinessComplete = false;
        ImGuiID m_dockRoot = 0;
        ImGuiID m_dockCenter = 0;
        ImGuiID m_dockLeft = 0;
        ImGuiID m_dockRight = 0;
        ImGuiID m_dockBottom = 0;
        bool m_defaultDockLayoutInitialized = false;
        bool m_restoredDockLayoutNeedsReconcile = false;
    };

    app::Service* CreateEditorUiService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Editor, sizeof(ManagedEditorUiService), alignof(ManagedEditorUiService));
        return block ? ::new (block.address) ManagedEditorUiService() : nullptr;
    }

    void DestroyEditorUiService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorUiService*>(service)->~ManagedEditorUiService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorUiService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool EditorPanelContext::RequestContextMenu(const EditorUiSurfaceId surface, EditorUiFailure* const failure) const noexcept
    {
        if (m_ui != nullptr)
            return m_ui->RequestContextMenuForPanel(m_panel, m_authoringFrame, surface, failure);
        if (failure != nullptr)
        {
            *failure = {};
            failure->code = EditorUiFailureCode::InvalidContextAssociation;
            failure->panel = m_panel;
            failure->surface = surface;
            failure->message = "editor panel context is not associated with a UI service";
        }
        return false;
    }

    EditorUiFrameData::EditorUiFrameData(const EditorHostWindowHandle host, const u64 frame, const EditorUiFramePayload& payload) noexcept
    {
        if (!host.IsValid() || !payload.IsValid())
            return;
        m_host = host;
        m_frame = frame;
        m_payload = payload;
        m_payload.retain(m_payload.data);
    }

    EditorUiFrameData::~EditorUiFrameData()
    {
        Reset();
    }

    EditorUiFrameData::EditorUiFrameData(const EditorUiFrameData& other) noexcept : m_host(other.m_host), m_frame(other.m_frame), m_payload(other.m_payload)
    {
        if (m_payload.IsValid())
            m_payload.retain(m_payload.data);
    }

    EditorUiFrameData& EditorUiFrameData::operator=(const EditorUiFrameData& other) noexcept
    {
        if (this == &other)
            return *this;
        EditorUiFrameData replacement(other);
        *this = static_cast<EditorUiFrameData&&>(replacement);
        return *this;
    }

    EditorUiFrameData::EditorUiFrameData(EditorUiFrameData&& other) noexcept : m_host(other.m_host), m_frame(other.m_frame), m_payload(other.m_payload)
    {
        other.m_host = {};
        other.m_frame = 0;
        other.m_payload = {};
    }

    EditorUiFrameData& EditorUiFrameData::operator=(EditorUiFrameData&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_host = other.m_host;
        m_frame = other.m_frame;
        m_payload = other.m_payload;
        other.m_host = {};
        other.m_frame = 0;
        other.m_payload = {};
        return *this;
    }

    bool EditorUiFrameData::IsValid() const noexcept
    {
        return m_host.IsValid() && m_payload.IsValid();
    }

    void EditorUiFrameData::Reset() noexcept
    {
        if (m_payload.IsValid())
            m_payload.release(m_payload.data);
        m_host = {};
        m_frame = 0;
        m_payload = {};
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(const u64 frame, const EditorUiFramePayload& payload) noexcept
    {
        if (!payload.IsValid())
            return;
        m_frame = frame;
        m_payload = payload;
        m_payload.retain(m_payload.data);
    }

    EditorUiTextureFrameData::~EditorUiTextureFrameData()
    {
        Reset();
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(const EditorUiTextureFrameData& other) noexcept : m_frame(other.m_frame), m_payload(other.m_payload)
    {
        if (m_payload.IsValid())
            m_payload.retain(m_payload.data);
    }

    EditorUiTextureFrameData& EditorUiTextureFrameData::operator=(const EditorUiTextureFrameData& other) noexcept
    {
        if (this == &other)
            return *this;
        EditorUiTextureFrameData replacement(other);
        *this = static_cast<EditorUiTextureFrameData&&>(replacement);
        return *this;
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(EditorUiTextureFrameData&& other) noexcept : m_frame(other.m_frame), m_payload(other.m_payload)
    {
        other.m_frame = 0;
        other.m_payload = {};
    }

    EditorUiTextureFrameData& EditorUiTextureFrameData::operator=(EditorUiTextureFrameData&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_frame = other.m_frame;
        m_payload = other.m_payload;
        other.m_frame = 0;
        other.m_payload = {};
        return *this;
    }

    bool EditorUiTextureFrameData::IsValid() const noexcept
    {
        return m_payload.IsValid();
    }

    void EditorUiTextureFrameData::Reset() noexcept
    {
        if (m_payload.IsValid())
            m_payload.release(m_payload.data);
        m_frame = 0;
        m_payload = {};
    }

    bool RegisterEditorUiService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required},
            {engine::WindowServiceId, application::DependencyKind::Required},
            {engine::InputServiceId, application::DependencyKind::Required},
            {engine::RenderingServiceId, application::DependencyKind::Required},
            {ProjectWorkspaceServiceId, application::DependencyKind::Required},
            {EditorResourceServiceId, application::DependencyKind::Required},
            {EditorActivityServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorUiCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorUiServiceId;
        descriptor.name = "editorUi";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, static_cast<u32>(sizeof(dependencies) / sizeof(dependencies[0]))};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorUiService;
        descriptor.destroy = DestroyEditorUiService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorUiService* FindEditorUiService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorUiService*>(host.FindCapability(EditorUiCapabilityId));
    }

    EditorUiService* FindEditorUiService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorUiService*>(context.FindCapability(EditorUiCapabilityId));
    }
} // namespace vanguard::editor
