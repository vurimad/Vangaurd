#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/application/platform_host.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::editor
{
    inline constexpr u32 MaximumEditorFileDialogs = 4u;
    inline constexpr u32 MaximumEditorFileDialogSelections = 64u;
    inline constexpr u32 MaximumEditorFileDialogPathBytes = 256u * 1024u;
    inline constexpr u32 MaximumEditorFileDialogErrorBytes = 512u;

    struct EditorFileDialogHandle
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(const EditorFileDialogHandle&, const EditorFileDialogHandle&) noexcept = default;
    };

    enum class EditorFileDialogState : u8
    {
        Pending,
        Selected,
        Cancelled,
        BackendError,
        LimitExceeded
    };

    enum class EditorFileDialogFailureCode : u8
    {
        None,
        InvalidArgument,
        InvalidState,
        WrongThread,
        CapacityExceeded,
        InvalidHandle,
        PlatformFailure
    };

    struct EditorFileDialogFailure
    {
        EditorFileDialogFailureCode code = EditorFileDialogFailureCode::None;
        const char* message = nullptr;
    };

    struct EditorFileDialogPollResult
    {
        EditorFileDialogState state = EditorFileDialogState::Pending;
        u32 pathCount = 0;

        [[nodiscard]] constexpr bool IsTerminal() const noexcept { return state != EditorFileDialogState::Pending; }
    };

    enum class EditorFileDropState : u8
    {
        None,
        Ready,
        LimitExceeded
    };

    struct EditorFileDropPollResult final
    {
        EditorFileDropState state = EditorFileDropState::None;
        window::WindowHandle window;
        window::WindowPoint position;
        u64 timestampNanoseconds = 0u;
        u32 pathCount = 0u;
    };

    class EditorFileDialogService : public application::Service
    {
    public:
        ~EditorFileDialogService() override = default;

        // Submission, polling and release are editor/main-thread operations. The
        // platform callback itself may run on any thread and is isolated behind
        // a bounded mailbox owned by this service.
        [[nodiscard]] virtual bool OpenFiles(bool allowMultiple, EditorFileDialogHandle& dialog,
                                             EditorFileDialogFailure* failure = nullptr) noexcept = 0;
        // Poll is consuming for terminal results: Selected paths/error text are
        // copied into caller-owned containers and the handle is released. A
        // Pending result leaves the handle live and outputs empty.
        [[nodiscard]] virtual bool Poll(EditorFileDialogHandle dialog, EditorFileDialogPollResult& result,
                                        containers::DynamicArray<containers::String>& paths,
                                        containers::String& error,
                                        EditorFileDialogFailure* failure = nullptr) noexcept = 0;
        // Release abandons local interest only; native dialogs are asynchronous
        // and may still invoke their callback later. That late callback owns no
        // service/UI/project pointers and self-reclaims its detached mailbox.
        [[nodiscard]] virtual bool Release(EditorFileDialogHandle dialog,
                                           EditorFileDialogFailure* failure = nullptr) noexcept = 0;
        // Consumes one completed native file-drop batch. Native event payloads
        // are copied synchronously by the service before the platform callback
        // returns, then exposed here only as caller-owned strings. None means no
        // completed batch is waiting. LimitExceeded is terminal for that batch
        // and returns no partial paths.
        [[nodiscard]] virtual bool PollFileDrop(EditorFileDropPollResult& result,
                                                containers::DynamicArray<containers::String>& paths,
                                                EditorFileDialogFailure* failure = nullptr) noexcept = 0;
        // Opens the platform file manager and reveals one existing project-owned
        // source file. This remains a main-thread native-UI operation; browser
        // code never calls platform/OS APIs directly.
        [[nodiscard]] virtual bool RevealFileLocation(const char* utf8FilePath,
                                                      EditorFileDialogFailure* failure = nullptr) noexcept = 0;

    protected:
        EditorFileDialogService() noexcept = default;
    };

    [[nodiscard]] bool RegisterEditorFileDialogService(application::EngineHost& host,
                                                       application::IPlatformHost* platform,
                                                       application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorFileDialogService* FindEditorFileDialogService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorFileDialogService* FindEditorFileDialogService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
