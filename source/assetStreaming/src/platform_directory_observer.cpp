#include <vanguard/asset_streaming/platform_directory_observer.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstddef>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace vanguard::asset_streaming
{
#if defined(_WIN32)
    namespace
    {
        constexpr DWORD DirectoryChangeFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;

        constexpr DWORD RootRetryMilliseconds = 250u;
        constexpr u32 NotificationHeaderBytes = static_cast<u32>(offsetof(FILE_NOTIFY_INFORMATION, FileName));

        [[nodiscard]] bool ClassifyNotification(const DWORD action, filesystem::DirectoryObservationKind& kind) noexcept
        {
            switch (action)
            {
            case FILE_ACTION_ADDED:
                kind = filesystem::DirectoryObservationKind::Added;
                return true;
            case FILE_ACTION_REMOVED:
                kind = filesystem::DirectoryObservationKind::Removed;
                return true;
            case FILE_ACTION_RENAMED_OLD_NAME:
            case FILE_ACTION_RENAMED_NEW_NAME:
                kind = filesystem::DirectoryObservationKind::Renamed;
                return true;
            case FILE_ACTION_MODIFIED:
                kind = filesystem::DirectoryObservationKind::Modified;
                return true;
            default:
                return false;
            }
        }
    } // namespace

    struct PlatformDirectoryObserver::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        class RootWorker final : public concurrency::Thread
        {
        public:
            VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

            RootWorker(Impl& owner, const u32 rootIndex, const filesystem::AbsolutePath& root) noexcept
                : concurrency::Thread("vanguard-source-watch"), m_owner(&owner), m_rootIndex(rootIndex), m_root(root),
                  m_buffer(memory::pools::Assets::GetInstance())
            {
            }

            void ThreadFunction() noexcept override
            {
                while (!m_owner->stopRequested.GetValue())
                {
                    if (m_buffer.Size() != m_owner->notificationBufferBytes)
                        m_buffer.Resize(m_owner->notificationBufferBytes);
                    if (m_buffer.Size() != m_owner->notificationBufferBytes)
                    {
                        m_owner->PublishOverflow(m_rootIndex);
                        if (WaitForRetryOrStop())
                            break;
                        continue;
                    }

                    const HANDLE directory = ::CreateFileW(m_root.ToUtf16String().AsChar(), FILE_LIST_DIRECTORY,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                    if (directory == INVALID_HANDLE_VALUE)
                    {
                        m_owner->PublishOverflow(m_rootIndex);
                        if (WaitForRetryOrStop())
                            break;
                        continue;
                    }

                    const HANDLE changeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    if (changeEvent == nullptr)
                    {
                        ::CloseHandle(directory);
                        m_owner->PublishOverflow(m_rootIndex);
                        if (WaitForRetryOrStop())
                            break;
                        continue;
                    }

                    WatchDirectory(directory, changeEvent);
                    ::CloseHandle(changeEvent);
                    ::CloseHandle(directory);

                    if (!m_owner->stopRequested.GetValue() && WaitForRetryOrStop())
                        break;
                }
            }

        private:
            [[nodiscard]] bool WaitForRetryOrStop() const noexcept
            {
                return ::WaitForSingleObject(m_owner->stopEvent, RootRetryMilliseconds) == WAIT_OBJECT_0;
            }

            static void CancelPending(const HANDLE directory, OVERLAPPED& overlapped) noexcept
            {
                static_cast<void>(::CancelIoEx(directory, &overlapped));
                DWORD ignored = 0;
                static_cast<void>(::GetOverlappedResult(directory, &overlapped, &ignored, TRUE));
            }

            void WatchDirectory(const HANDLE directory, const HANDLE changeEvent) noexcept
            {
                while (!m_owner->stopRequested.GetValue())
                {
                    ::ResetEvent(changeEvent);
                    OVERLAPPED overlapped{};
                    overlapped.hEvent = changeEvent;

                    const BOOL issued = ::ReadDirectoryChangesW(directory, m_buffer.TypedData(), static_cast<DWORD>(m_buffer.Size()), TRUE,
                                                                DirectoryChangeFilter, nullptr, &overlapped, nullptr);
                    if (issued == FALSE && ::GetLastError() != ERROR_IO_PENDING)
                    {
                        m_owner->PublishOverflow(m_rootIndex);
                        return;
                    }

                    const HANDLE waits[2] = {m_owner->stopEvent, changeEvent};
                    const DWORD wait = ::WaitForMultipleObjects(2u, waits, FALSE, INFINITE);
                    if (wait == WAIT_OBJECT_0)
                    {
                        CancelPending(directory, overlapped);
                        return;
                    }
                    if (wait != WAIT_OBJECT_0 + 1u)
                    {
                        CancelPending(directory, overlapped);
                        m_owner->PublishOverflow(m_rootIndex);
                        return;
                    }

                    DWORD bytes = 0;
                    if (::GetOverlappedResult(directory, &overlapped, &bytes, FALSE) == FALSE)
                    {
                        if (!m_owner->stopRequested.GetValue())
                            m_owner->PublishOverflow(m_rootIndex);
                        return;
                    }
                    if (m_owner->stopRequested.GetValue())
                        return;
                    if (bytes == 0)
                    {
                        // Windows reports dropped changes as a zero-byte
                        // completion. Never discard it as a no-op.
                        m_owner->PublishOverflow(m_rootIndex);
                        continue;
                    }
                    if (!ParseNotifications(bytes))
                        m_owner->PublishOverflow(m_rootIndex);
                }
            }

            [[nodiscard]] bool ParseNotifications(const DWORD byteCount) noexcept
            {
                if (byteCount > m_buffer.Size())
                    return false;
                const u8* const base = m_buffer.TypedData();
                u32 offset = 0;
                while (offset < byteCount)
                {
                    const u32 remaining = byteCount - offset;
                    if (remaining < NotificationHeaderBytes)
                        return false;
                    const auto* const information = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset);
                    const u32 recordBytes = information->NextEntryOffset == 0u ? remaining : information->NextEntryOffset;
                    if (recordBytes < NotificationHeaderBytes || recordBytes > remaining ||
                        (information->NextEntryOffset != 0u && (information->NextEntryOffset % sizeof(DWORD)) != 0u) ||
                        (information->NextEntryOffset != 0u && recordBytes > remaining - NotificationHeaderBytes) ||
                        information->FileNameLength == 0u || (information->FileNameLength % sizeof(WCHAR)) != 0u ||
                        information->FileNameLength > recordBytes - NotificationHeaderBytes)
                        return false;
                    if (!PublishNotification(information))
                        return false;
                    if (information->NextEntryOffset == 0u)
                        return true;
                    offset += information->NextEntryOffset;
                }
                return offset == byteCount;
            }

            [[nodiscard]] bool PublishNotification(const FILE_NOTIFY_INFORMATION* const information) noexcept
            {
                filesystem::DirectoryObservationKind kind;
                if (!ClassifyNotification(information->Action, kind))
                    return false;

                char utf8[1024];
                const int written = ::WideCharToMultiByte(CP_UTF8, 0u, information->FileName,
                                                          static_cast<int>(information->FileNameLength / sizeof(WCHAR)), utf8,
                                                          static_cast<int>(sizeof(utf8)), nullptr, nullptr);
                if (written <= 0)
                    return false;

                const containers::StringView rootView = m_root.AsStringView();
                containers::String full(rootView.Data(), rootView.Length());
                full.Append(utf8, static_cast<u32>(written));

                filesystem::DirectoryObservation observation;
                observation.path = filesystem::AbsolutePath::CreateFilePath(containers::StringView(full.AsChar(), full.Length()));
                observation.root = m_rootIndex;
                observation.kind = kind;
                if (observation.path.IsFilePath())
                {
                    // ReadDirectoryChangesW includes directory entries in its
                    // recursive stream. CreateFilePath is purely syntactic and
                    // otherwise misclassifies an existing parent folder as an
                    // authored source file. In particular, touching a large
                    // transaction-stage sibling may update its parent folder's
                    // last-write metadata: treating that notification as an
                    // unrelated file edit makes the planned-import ownership
                    // fence roll back its own otherwise-valid transaction.
                    //
                    // A directory-only MODIFIED notification does not identify
                    // an authored file change; real children have independent
                    // notifications, and a dropped stream still reports overflow.
                    // Creation/renaming of a directory may change the source
                    // inventory, so retain conservative full-rescan behavior.
                    const DWORD attributes = ::GetFileAttributesW(observation.path.ToUtf16String().AsChar());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
                    {
                        if (information->Action != FILE_ACTION_MODIFIED)
                            m_owner->PublishOverflow(m_rootIndex);
                        return true;
                    }
                    m_owner->Publish(observation);
                    return true;
                }
                return false;
            }

            Impl* m_owner = nullptr;
            u32 m_rootIndex = 0;
            filesystem::AbsolutePath m_root;
            containers::DynamicArray<u8> m_buffer;
        };

        Impl() noexcept
            : roots(memory::pools::Assets::GetInstance()), workers(memory::pools::Assets::GetInstance())
        {
        }

        ~Impl()
        {
            stopRequested.SetValue(true);
            if (stopEvent != nullptr)
                ::SetEvent(stopEvent);
            for (RootWorker* const worker : workers)
                if (worker->IsValid())
                    worker->JoinThread();
            for (RootWorker* const worker : workers)
                VANGUARD_DELETE(worker);
            if (stopEvent != nullptr)
                ::CloseHandle(stopEvent);
        }

        void Publish(const filesystem::DirectoryObservation& observation) noexcept
        {
            if (callback != nullptr)
                callback(observation, userData);
        }

        void PublishOverflow(const u32 root) noexcept
        {
            filesystem::DirectoryObservation observation;
            observation.root = root;
            observation.kind = filesystem::DirectoryObservationKind::Overflow;
            Publish(observation);
        }

        filesystem::DirectoryObservationCallback callback = nullptr;
        void* userData = nullptr;
        containers::DynamicArray<filesystem::AbsolutePath> roots;
        containers::DynamicArray<RootWorker*> workers;
        concurrency::Atomic<bool> stopRequested;
        HANDLE stopEvent = nullptr;
        u32 notificationBufferBytes = 0;
        bool running = false;
    };

    PlatformDirectoryObserver::~PlatformDirectoryObserver() { static_cast<void>(Stop()); }

    bool PlatformDirectoryObserver::Start(const containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                          const filesystem::DirectoryObservationCallback callback, void* const userData,
                                          const filesystem::DirectoryObserverConfig& config) noexcept
    {
        if (m_impl != nullptr || roots.Empty() || roots.Data() == nullptr || callback == nullptr || !config.IsValid() ||
            roots.Count() > config.maximumRoots)
            return false;

        Impl* const impl = VANGUARD_NEW(Impl)();
        if (impl == nullptr)
            return false;
        impl->callback = callback;
        impl->userData = userData;
        impl->notificationBufferBytes = config.notificationBufferBytes;
        impl->stopRequested.SetValue(false);
        impl->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        impl->roots.Reserve(roots.Count());
        impl->workers.Reserve(roots.Count());
        if (impl->stopEvent == nullptr || impl->roots.Capacity() < roots.Count() || impl->workers.Capacity() < roots.Count())
        {
            VANGUARD_DELETE(impl);
            return false;
        }

        for (u32 index = 0; index < roots.Count(); ++index)
        {
            impl->roots.PushBack(roots[index]);
            if (impl->roots.Size() != index + 1u)
            {
                VANGUARD_DELETE(impl);
                return false;
            }
            Impl::RootWorker* const worker = VANGUARD_NEW(Impl::RootWorker)(*impl, index, roots[index]);
            if (worker == nullptr)
            {
                VANGUARD_DELETE(impl);
                return false;
            }
            const u32 previousCount = impl->workers.Size();
            impl->workers.PushBack(worker);
            if (impl->workers.Size() != previousCount + 1u)
            {
                VANGUARD_DELETE(worker);
                VANGUARD_DELETE(impl);
                return false;
            }
            worker->InitThread();
            if (!worker->IsValid())
            {
                VANGUARD_DELETE(impl);
                return false;
            }
        }

        impl->running = true;
        m_impl = impl;
        return true;
    }

    bool PlatformDirectoryObserver::Stop() noexcept
    {
        if (m_impl == nullptr)
            return true;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->running = false;
        VANGUARD_DELETE(impl);
        return true;
    }

    bool PlatformDirectoryObserver::IsRunning() const noexcept { return m_impl != nullptr && m_impl->running; }
#else
    PlatformDirectoryObserver::~PlatformDirectoryObserver() = default;

    bool PlatformDirectoryObserver::Start(const containers::ArraySpan<const filesystem::AbsolutePath>,
                                          const filesystem::DirectoryObservationCallback, void*,
                                          const filesystem::DirectoryObserverConfig&) noexcept
    {
        return false;
    }

    bool PlatformDirectoryObserver::Stop() noexcept { return true; }
    bool PlatformDirectoryObserver::IsRunning() const noexcept { return false; }
#endif
} // namespace vanguard::asset_streaming
