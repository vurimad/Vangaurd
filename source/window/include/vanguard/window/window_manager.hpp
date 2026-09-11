#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/window/window_backend.hpp>

namespace vanguard::window
{
    class WindowManager final
    {
    public:
        struct Impl;

        WindowManager() noexcept = default;
        ~WindowManager();

        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;

        [[nodiscard]] bool Initialize(IWindowBackend& backend, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RefreshDisplays(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool Create(const WindowDescriptor& descriptor, WindowHandle& window, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestState(WindowHandle window, const WindowStateRequest& request, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetTitle(WindowHandle window, const char* title, Failure* failure = nullptr) noexcept;
        // Whole-window opacity, from transparent (0) to opaque (1); owner-thread only.
        [[nodiscard]] bool SetOpacity(WindowHandle window, f32 opacity, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestFocus(WindowHandle window, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetCursor(CursorShape shape, bool visible, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReadClipboardText(char* destination, u32 capacity, u32& requiredCapacity, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool WriteClipboardText(const char* text, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetTextInput(WindowHandle window, const TextInputRequest& request, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResolveCloseRequest(WindowHandle window, u64 closeRequestSerial, CloseDecision decision, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyWindow(WindowHandle window, Failure* failure = nullptr) noexcept;

        // The renderer owns the presentation object. Detach is a promise that all GPU/native references have been
        // safely released; until then the associated window cannot be destroyed.
        [[nodiscard]] bool AttachPresentation(WindowHandle window, PresentationAttachmentHandle& attachment, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool DetachPresentation(PresentationAttachmentHandle attachment, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcknowledgePresentation(PresentationAttachmentHandle attachment, const PresentationAcknowledgement& acknowledgement,
                                                   Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResolvePresentationSurface(PresentationAttachmentHandle attachment, NativePresentationSurface& surface,
                                                      Failure* failure = nullptr) noexcept;

        [[nodiscard]] bool ProcessBackendEvent(const BackendWindowEvent& event, Failure* failure = nullptr) noexcept;

        [[nodiscard]] bool GetSnapshot(WindowHandle window, WindowSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool GetSnapshot(DisplayHandle display, DisplaySnapshot& snapshot) const noexcept;
        [[nodiscard]] bool GetSnapshot(PresentationAttachmentHandle attachment, PresentationAttachmentSnapshot& snapshot) const noexcept;
        [[nodiscard]] WindowHandle ResolveBackendWindow(BackendWindowId window) const noexcept;
        [[nodiscard]] DisplayHandle GetPrimaryDisplay() const noexcept;
        [[nodiscard]] u32 VisitWindows(WindowSnapshot* snapshots, u32 capacity) const noexcept;
        [[nodiscard]] u32 VisitDisplays(DisplaySnapshot* snapshots, u32 capacity) const noexcept;
        [[nodiscard]] u32 VisitPresentationAttachments(PresentationAttachmentSnapshot* snapshots, u32 capacity) const noexcept;

        [[nodiscard]] bool CreateEventCursor(EventCursorOrigin origin, WindowEventCursor& cursor) const noexcept;
        [[nodiscard]] WindowEventReadResult ReadEvents(WindowEventCursor& cursor, containers::ArraySpan<WindowEvent> events) const noexcept;
        [[nodiscard]] ManagerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::window
