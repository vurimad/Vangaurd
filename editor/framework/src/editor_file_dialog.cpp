#include <vanguard/editor/editor_file_dialog.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/system/time.hpp>

#include <algorithm>
#include <cstring>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace ctr = vanguard::containers;
    namespace editor = vanguard::editor;
    namespace memory = vanguard::memory;
    using vanguard::u32;
    using vanguard::u64;

    struct FileDialogMailbox final
    {
        vanguard::concurrency::Mutex lock;
        editor::EditorFileDialogHandle handle;
        editor::EditorFileDialogState state = editor::EditorFileDialogState::Pending;
        u32 pathCount = 0;
        u32 pathBytes = 0;
        u32 pathOffsets[editor::MaximumEditorFileDialogSelections]{};
        u32 pathLengths[editor::MaximumEditorFileDialogSelections]{};
        char paths[editor::MaximumEditorFileDialogPathBytes]{};
        char error[editor::MaximumEditorFileDialogErrorBytes]{};
        bool callbackComplete = false;
        bool ownerAttached = true;
    };

    struct FileDropMailbox final
    {
        vanguard::concurrency::Mutex lock;
        editor::EditorFileDropState state = editor::EditorFileDropState::None;
        vanguard::window::WindowHandle window;
        vanguard::window::WindowPoint position;
        u64 timestampNanoseconds = 0u;
        u64 completedAtTick = 0u;
        u32 pathCount = 0u;
        u32 pathBytes = 0u;
        u32 pathOffsets[editor::MaximumEditorFileDialogSelections]{};
        u32 pathLengths[editor::MaximumEditorFileDialogSelections]{};
        char paths[editor::MaximumEditorFileDialogPathBytes]{};
        bool accumulating = false;
        bool overflow = false;
        bool discardBatch = false;
    };

    inline constexpr u64 MaximumCompletedFileDropAgeMilliseconds = 250u;

    [[nodiscard]] bool IsCompletedDropExpiredLocked(const FileDropMailbox& drop) noexcept
    {
        if (drop.state == editor::EditorFileDropState::None || drop.completedAtTick == 0u)
            return false;
        const u64 frequency = vanguard::system::GetMonotonicFrequency();
        const u64 now = vanguard::system::GetMonotonicTicks();
        if (frequency == 0u || now < drop.completedAtTick)
            return false;
        const u64 maximumTicks =
            std::max<u64>(1u, (frequency * MaximumCompletedFileDropAgeMilliseconds) / 1000u);
        return now - drop.completedAtTick > maximumTicks;
    }

    void DestroyMailbox(FileDialogMailbox* const mailbox) noexcept
    {
        if (mailbox == nullptr)
            return;
        mailbox->~FileDialogMailbox();
        memory::MemoryBlock block{mailbox, sizeof(FileDialogMailbox), memory::PoolId::Editor};
        memory::Free(block);
    }

    void DetachMailbox(FileDialogMailbox* const mailbox) noexcept
    {
        if (mailbox == nullptr)
            return;
        mailbox->lock.Acquire();
        mailbox->ownerAttached = false;
        const bool destroy = mailbox->callbackComplete;
        mailbox->lock.Release();
        if (destroy)
            DestroyMailbox(mailbox);
    }

    [[nodiscard]] u32 BoundedLength(const char* const text, const u32 maximum) noexcept
    {
        if (text == nullptr)
            return 0;
        u32 length = 0;
        while (length < maximum && text[length] != '\0')
            ++length;
        return length;
    }

    void CompleteFileDialog(void* const userData, const app::PlatformFileDialogResultView& platformResult) noexcept
    {
        auto* const mailbox = static_cast<FileDialogMailbox*>(userData);
        if (mailbox == nullptr)
            return;

        mailbox->lock.Acquire();
        if (mailbox->ownerAttached)
        {
            mailbox->pathCount = 0;
            mailbox->pathBytes = 0;
            mailbox->error[0] = '\0';
            switch (platformResult.outcome)
            {
            case app::PlatformFileDialogOutcome::Cancelled:
                mailbox->state = editor::EditorFileDialogState::Cancelled;
                break;
            case app::PlatformFileDialogOutcome::Error:
            {
                mailbox->state = editor::EditorFileDialogState::BackendError;
                const u32 length = BoundedLength(platformResult.error, editor::MaximumEditorFileDialogErrorBytes - 1u);
                if (length != 0)
                    std::memcpy(mailbox->error, platformResult.error, length);
                mailbox->error[length] = '\0';
                break;
            }
            case app::PlatformFileDialogOutcome::Selected:
            {
                bool withinLimits = platformResult.paths != nullptr && platformResult.pathCount != 0 &&
                                    platformResult.pathCount <= editor::MaximumEditorFileDialogSelections;
                for (u32 index = 0; withinLimits && index < platformResult.pathCount; ++index)
                {
                    const u32 remaining = editor::MaximumEditorFileDialogPathBytes - mailbox->pathBytes;
                    if (remaining == 0 || platformResult.paths[index] == nullptr)
                    {
                        withinLimits = false;
                        break;
                    }
                    const u32 length = BoundedLength(platformResult.paths[index], remaining);
                    if (length == remaining)
                    {
                        withinLimits = false;
                        break;
                    }
                    mailbox->pathOffsets[index] = mailbox->pathBytes;
                    mailbox->pathLengths[index] = length;
                    std::memcpy(mailbox->paths + mailbox->pathBytes, platformResult.paths[index], length + 1u);
                    mailbox->pathBytes += length + 1u;
                    ++mailbox->pathCount;
                }
                if (withinLimits)
                    mailbox->state = editor::EditorFileDialogState::Selected;
                else
                {
                    mailbox->state = editor::EditorFileDialogState::LimitExceeded;
                    mailbox->pathCount = 0;
                    mailbox->pathBytes = 0;
                }
                break;
            }
            }
        }
        mailbox->callbackComplete = true;
        const bool destroy = !mailbox->ownerAttached;
        mailbox->lock.Release();
        if (destroy)
            DestroyMailbox(mailbox);
    }

    class ManagedEditorFileDialogService final : public editor::EditorFileDialogService,
                                                 public vanguard::engine::IWindowFileDropSink
    {
    public:
        explicit ManagedEditorFileDialogService(app::IPlatformHost* const platform) noexcept : m_platform(platform) {}

        ~ManagedEditorFileDialogService() override
        {
            for (FileDialogMailbox* const mailbox : m_dialogs)
                VG_ASSERT_MSG(mailbox == nullptr, "EditorFileDialogService requires explicit shutdown/release of owned mailboxes");
        }

        [[nodiscard]] bool OpenFiles(const bool allowMultiple, editor::EditorFileDialogHandle& dialog,
                                     editor::EditorFileDialogFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            dialog = {};
            if (!vanguard::concurrency::IsMainThread())
                return Reject(failure, editor::EditorFileDialogFailureCode::WrongThread, "file-dialog submission requires the editor main thread");
            if (!m_running || m_platform == nullptr)
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidState, "file-dialog service is not running");
            u32 slot = editor::MaximumEditorFileDialogs;
            for (u32 index = 0; index < editor::MaximumEditorFileDialogs; ++index)
            {
                if (m_dialogs[index] == nullptr)
                {
                    slot = index;
                    break;
                }
            }
            if (slot == editor::MaximumEditorFileDialogs)
                return Reject(failure, editor::EditorFileDialogFailureCode::CapacityExceeded, "file-dialog request capacity is exhausted");

            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor, sizeof(FileDialogMailbox), alignof(FileDialogMailbox));
            if (!block)
                return Reject(failure, editor::EditorFileDialogFailureCode::CapacityExceeded, "failed to allocate file-dialog mailbox");
            auto* const mailbox = ::new (block.address) FileDialogMailbox();
            if (++m_nextHandle == 0)
                ++m_nextHandle;
            mailbox->handle.value = m_nextHandle;
            m_dialogs[slot] = mailbox;

            const app::PlatformOpenFileDialogRequest request{allowMultiple};
            const app::PlatformStatus status = m_platform->ShowOpenFileDialog(request, CompleteFileDialog, mailbox);
            if (!status)
            {
                m_dialogs[slot] = nullptr;
                // Platform submission failure guarantees that no callback was
                // accepted, so this mailbox has no foreign-thread owner.
                DestroyMailbox(mailbox);
                return Reject(failure, editor::EditorFileDialogFailureCode::PlatformFailure,
                              status.message != nullptr ? status.message : "platform rejected native open-file dialog");
            }
            dialog = mailbox->handle;
            return true;
        }

        [[nodiscard]] bool Poll(const editor::EditorFileDialogHandle dialog, editor::EditorFileDialogPollResult& result,
                                ctr::DynamicArray<ctr::String>& paths, ctr::String& error,
                                editor::EditorFileDialogFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            result = {};
            paths.Clear();
            error.Clear();
            if (!vanguard::concurrency::IsMainThread())
                return Reject(failure, editor::EditorFileDialogFailureCode::WrongThread, "file-dialog polling requires the editor main thread");
            FileDialogMailbox* const mailbox = Find(dialog);
            if (mailbox == nullptr)
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidHandle, "file-dialog handle is not live");

            mailbox->lock.Acquire();
            result.state = mailbox->state;
            result.pathCount = mailbox->pathCount;
            if (!mailbox->callbackComplete)
            {
                mailbox->lock.Release();
                return true;
            }
            if (mailbox->state == editor::EditorFileDialogState::Selected)
            {
                for (u32 index = 0; index < mailbox->pathCount; ++index)
                    paths.PushBack(ctr::String(mailbox->paths + mailbox->pathOffsets[index], mailbox->pathLengths[index]));
            }
            else if (mailbox->state == editor::EditorFileDialogState::BackendError && mailbox->error[0] != '\0')
            {
                error = ctr::String(mailbox->error);
            }
            mailbox->ownerAttached = false;
            mailbox->lock.Release();
            Remove(mailbox);
            DestroyMailbox(mailbox);
            return true;
        }

        [[nodiscard]] bool Release(const editor::EditorFileDialogHandle dialog,
                                   editor::EditorFileDialogFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return Reject(failure, editor::EditorFileDialogFailureCode::WrongThread, "file-dialog release requires the editor main thread");
            FileDialogMailbox* const mailbox = Find(dialog);
            if (mailbox == nullptr)
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidHandle, "file-dialog handle is not live");
            Remove(mailbox);
            DetachMailbox(mailbox);
            return true;
        }

        [[nodiscard]] bool PollFileDrop(editor::EditorFileDropPollResult& result,
                                        ctr::DynamicArray<ctr::String>& paths,
                                        editor::EditorFileDialogFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            result = {};
            paths.Clear();
            if (!vanguard::concurrency::IsMainThread())
                return Reject(failure, editor::EditorFileDialogFailureCode::WrongThread,
                              "file-drop polling requires the editor main thread");
            if (!m_running)
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidState,
                              "file-drop polling requires a running editor file service");

            m_drop.lock.Acquire();
            if (IsCompletedDropExpiredLocked(m_drop))
                ResetDropMailboxLocked();
            result.state = m_drop.state;
            result.window = m_drop.window;
            result.position = m_drop.position;
            result.timestampNanoseconds = m_drop.timestampNanoseconds;
            result.pathCount = m_drop.state == editor::EditorFileDropState::Ready ? m_drop.pathCount : 0u;
            if (m_drop.state == editor::EditorFileDropState::Ready)
            {
                paths.Reserve(m_drop.pathCount);
                if (paths.Capacity() < m_drop.pathCount)
                {
                    m_drop.lock.Release();
                    return Reject(failure, editor::EditorFileDialogFailureCode::CapacityExceeded,
                                  "file-drop result copy allocation failed");
                }
                for (u32 index = 0u; index < m_drop.pathCount; ++index)
                    paths.PushBack(ctr::String(m_drop.paths + m_drop.pathOffsets[index], m_drop.pathLengths[index]));
            }
            if (m_drop.state != editor::EditorFileDropState::None)
                ResetDropMailboxLocked();
            m_drop.lock.Release();
            return true;
        }

        [[nodiscard]] bool RevealFileLocation(const char* const utf8FilePath,
                                              editor::EditorFileDialogFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return Reject(failure, editor::EditorFileDialogFailureCode::WrongThread,
                              "revealing a file location requires the editor main thread");
            if (!m_running || m_platform == nullptr)
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidState,
                              "revealing a file location requires a running editor file service");
            if (utf8FilePath == nullptr || utf8FilePath[0] == '\0')
                return Reject(failure, editor::EditorFileDialogFailureCode::InvalidArgument,
                              "revealing a file location requires a file path");
            const app::PlatformStatus status = m_platform->RevealFileLocation(utf8FilePath);
            return status ? true
                          : Reject(failure, editor::EditorFileDialogFailureCode::PlatformFailure,
                                   status.message != nullptr ? status.message
                                                             : "platform failed to reveal the file location");
        }

        [[nodiscard]] bool ProcessFileDrop(const vanguard::engine::WindowFileDropEvent& event) noexcept override
        {
            m_drop.lock.Acquire();
            switch (event.type)
            {
            case vanguard::engine::WindowFileDropEventType::Begin:
                if (IsCompletedDropExpiredLocked(m_drop))
                    ResetDropMailboxLocked();
                if (m_drop.state != editor::EditorFileDropState::None || m_drop.accumulating)
                {
                    m_drop.discardBatch = true;
                    m_drop.accumulating = false;
                    m_drop.lock.Release();
                    return true;
                }
                m_drop.window = event.window;
                m_drop.position = event.position;
                m_drop.timestampNanoseconds = event.timestampNanoseconds;
                m_drop.pathCount = 0u;
                m_drop.pathBytes = 0u;
                m_drop.overflow = false;
                m_drop.discardBatch = false;
                m_drop.accumulating = true;
                break;
            case vanguard::engine::WindowFileDropEventType::File:
                if (m_drop.discardBatch)
                    break;
                if (!m_drop.accumulating)
                {
                    m_drop.window = event.window;
                    m_drop.pathCount = 0u;
                    m_drop.pathBytes = 0u;
                    m_drop.overflow = false;
                    m_drop.accumulating = true;
                }
                if (!(m_drop.window == event.window))
                {
                    m_drop.overflow = true;
                    break;
                }
                m_drop.position = event.position;
                m_drop.timestampNanoseconds = event.timestampNanoseconds;
                if (m_drop.overflow)
                    break;
                if (event.path == nullptr || m_drop.pathCount >= editor::MaximumEditorFileDialogSelections)
                {
                    m_drop.overflow = true;
                    m_drop.pathCount = 0u;
                    m_drop.pathBytes = 0u;
                    break;
                }
                {
                    const u32 remaining = editor::MaximumEditorFileDialogPathBytes - m_drop.pathBytes;
                    const u32 length = BoundedLength(event.path, remaining);
                    if (remaining == 0u || length == remaining)
                    {
                        m_drop.overflow = true;
                        m_drop.pathCount = 0u;
                        m_drop.pathBytes = 0u;
                        break;
                    }
                    const u32 index = m_drop.pathCount++;
                    m_drop.pathOffsets[index] = m_drop.pathBytes;
                    m_drop.pathLengths[index] = length;
                    std::memcpy(m_drop.paths + m_drop.pathBytes, event.path, length + 1u);
                    m_drop.pathBytes += length + 1u;
                }
                break;
            case vanguard::engine::WindowFileDropEventType::Complete:
                if (m_drop.discardBatch)
                {
                    m_drop.discardBatch = false;
                    break;
                }
                if (m_drop.accumulating)
                {
                    m_drop.position = event.position;
                    m_drop.timestampNanoseconds = event.timestampNanoseconds;
                    m_drop.state = m_drop.overflow || m_drop.pathCount == 0u
                                       ? editor::EditorFileDropState::LimitExceeded
                                       : editor::EditorFileDropState::Ready;
                    m_drop.completedAtTick = vanguard::system::GetMonotonicTicks();
                    m_drop.accumulating = false;
                }
                break;
            }
            m_drop.lock.Release();
            return true;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_windows = static_cast<vanguard::engine::WindowService*>(
                context.FindCapability(vanguard::engine::WindowCapabilityId));
            return m_platform != nullptr ? app::LifecycleStatus::Success()
                                         : app::LifecycleStatus::Failure("EditorFileDialogService requires a platform host");
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (m_windows != nullptr && !m_windows->AttachFileDropSink(this))
                return app::LifecycleStatus::Failure("Window service rejected editor file-drop ownership");
            m_dropAttached = m_windows != nullptr;
            m_running = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            DetachDropSink();
            Quiesce();
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            DetachDropSink();
            Quiesce();
            m_windows = nullptr;
            m_platform = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static void ClearFailure(editor::EditorFileDialogFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        static bool Reject(editor::EditorFileDialogFailure* const failure,
                           const editor::EditorFileDialogFailureCode code,
                           const char* const message) noexcept
        {
            if (failure != nullptr)
                *failure = {code, message};
            return false;
        }

        [[nodiscard]] FileDialogMailbox* Find(const editor::EditorFileDialogHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            for (FileDialogMailbox* const mailbox : m_dialogs)
                if (mailbox != nullptr && mailbox->handle == handle)
                    return mailbox;
            return nullptr;
        }

        void Remove(FileDialogMailbox* const mailbox) noexcept
        {
            for (FileDialogMailbox*& candidate : m_dialogs)
            {
                if (candidate == mailbox)
                {
                    candidate = nullptr;
                    return;
                }
            }
        }

        void Quiesce() noexcept
        {
            m_running = false;
            for (FileDialogMailbox*& mailbox : m_dialogs)
            {
                FileDialogMailbox* const detached = mailbox;
                mailbox = nullptr;
                DetachMailbox(detached);
            }
            m_drop.lock.Acquire();
            ResetDropMailboxLocked();
            m_drop.lock.Release();
        }

        void ResetDropMailboxLocked() noexcept
        {
            m_drop.state = editor::EditorFileDropState::None;
            m_drop.window = {};
            m_drop.position = {};
            m_drop.timestampNanoseconds = 0u;
            m_drop.completedAtTick = 0u;
            m_drop.pathCount = 0u;
            m_drop.pathBytes = 0u;
            m_drop.accumulating = false;
            m_drop.overflow = false;
            m_drop.discardBatch = false;
        }

        void DetachDropSink() noexcept
        {
            if (m_dropAttached && m_windows != nullptr)
                m_windows->DetachFileDropSink(this);
            m_dropAttached = false;
        }

        app::IPlatformHost* m_platform = nullptr;
        vanguard::engine::WindowService* m_windows = nullptr;
        FileDialogMailbox* m_dialogs[editor::MaximumEditorFileDialogs]{};
        FileDropMailbox m_drop;
        u64 m_nextHandle = 0;
        bool m_running = false;
        bool m_dropAttached = false;
    };

    app::Service* CreateEditorFileDialogService(void* const userData) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor, sizeof(ManagedEditorFileDialogService),
                                                    alignof(ManagedEditorFileDialogService));
        return block ? ::new (block.address) ManagedEditorFileDialogService(static_cast<app::IPlatformHost*>(userData)) : nullptr;
    }

    void DestroyEditorFileDialogService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorFileDialogService*>(service)->~ManagedEditorFileDialogService();
        memory::MemoryBlock block{service, sizeof(ManagedEditorFileDialogService), memory::PoolId::Editor};
        memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorFileDialogService(application::EngineHost& host, application::IPlatformHost* const platform,
                                         application::HostFailure* const failure) noexcept
    {
        if (platform == nullptr)
        {
            if (failure != nullptr)
                *failure = {application::HostFailureCode::InvalidArgument, EditorFileDialogServiceId,
                            application::InvalidServiceId, application::InvalidCapabilityId,
                            "EditorFileDialogService requires a platform host"};
            return false;
        }
        constexpr application::CapabilityId capabilities[]{EditorFileDialogCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorFileDialogServiceId;
        descriptor.name = "editorFileDialog";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        constexpr application::ServiceDependency dependencies[]{
            {vanguard::engine::WindowServiceId, application::DependencyKind::Optional}};
        descriptor.dependencies = dependencies;
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorFileDialogService;
        descriptor.destroy = DestroyEditorFileDialogService;
        descriptor.userData = platform;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorFileDialogService* FindEditorFileDialogService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorFileDialogService*>(host.FindCapability(EditorFileDialogCapabilityId));
    }

    EditorFileDialogService* FindEditorFileDialogService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorFileDialogService*>(context.FindCapability(EditorFileDialogCapabilityId));
    }
} // namespace vanguard::editor
