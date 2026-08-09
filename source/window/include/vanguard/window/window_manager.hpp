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
        [[nodiscard]] bool Create(const WindowDescriptor& descriptor, WindowHandle& window,
                                  Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestState(WindowHandle window, const WindowStateRequest& request,
                                        Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetTitle(WindowHandle window, const char* title, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResolveCloseRequest(WindowHandle window, u64 closeRequestSerial, CloseDecision decision,
                                               Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyWindow(WindowHandle window, Failure* failure = nullptr) noexcept;

        // The renderer owns the presentation object. Detach is a promise that all GPU/native references have been
        // safely released; until then the associated window cannot be destroyed.
        [[nodiscard]] bool AttachPresentation(WindowHandle window, PresentationAttachmentHandle& attachment,
                                              Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool DetachPresentation(PresentationAttachmentHandle attachment,
                                              Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcknowledgePresentation(PresentationAttachmentHandle attachment,
                                                   const PresentationAcknowledgement& acknowledgement,
                                                   Failure* failure = nullptr) noexcept;

        [[nodiscard]] bool ProcessBackendEvent(const BackendWindowEvent& event,
                                               Failure* failure = nullptr) noexcept;

        [[nodiscard]] bool Snapshot(WindowHandle window, WindowSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool Snapshot(DisplayHandle display, DisplaySnapshot& snapshot) const noexcept;
        [[nodiscard]] bool Snapshot(PresentationAttachmentHandle attachment,
                                    PresentationAttachmentSnapshot& snapshot) const noexcept;
        [[nodiscard]] WindowHandle ResolveBackendWindow(BackendWindowId window) const noexcept;
        [[nodiscard]] DisplayHandle PrimaryDisplay() const noexcept;
        [[nodiscard]] u32 VisitWindows(WindowSnapshot* snapshots, u32 capacity) const noexcept;
        [[nodiscard]] u32 VisitDisplays(DisplaySnapshot* snapshots, u32 capacity) const noexcept;
        [[nodiscard]] u32 VisitPresentationAttachments(PresentationAttachmentSnapshot* snapshots,
                                                       u32 capacity) const noexcept;

        [[nodiscard]] bool CreateEventCursor(EventCursorOrigin origin, WindowEventCursor& cursor) const noexcept;
        [[nodiscard]] WindowEventReadResult ReadEvents(WindowEventCursor& cursor,
                                                       containers::ArraySpan<WindowEvent> events) const noexcept;
        [[nodiscard]] ManagerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::window
