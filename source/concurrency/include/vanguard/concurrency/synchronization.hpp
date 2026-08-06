#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::concurrency
{
    namespace detail
    {
        template <usize Size, usize Alignment> struct alignas(Alignment) InlineStorage
        {
            byte bytes[Size] = {};
        };
    } // namespace detail

    template <typename T> class ScopedLock
    {
    public:
        explicit ScopedLock(T& lock) noexcept : m_lock(lock)
        {
            m_lock.Acquire();
        }

        ~ScopedLock()
        {
            m_lock.Release();
        }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        T& m_lock;
    };

    template <typename T> class ScopedSharedLock
    {
    public:
        explicit ScopedSharedLock(T& lock) noexcept : m_lock(lock)
        {
            m_lock.AcquireShared();
        }

        ~ScopedSharedLock()
        {
            m_lock.ReleaseShared();
        }

        ScopedSharedLock(const ScopedSharedLock&) = delete;
        ScopedSharedLock& operator=(const ScopedSharedLock&) = delete;

    private:
        T& m_lock;
    };

    class Mutex
    {
    public:
        Mutex() noexcept;
        ~Mutex();
        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void Acquire() noexcept;
        [[nodiscard]] bool TryAcquire() noexcept;
        void Release() noexcept;
        void SetSpinCount(u32 count) noexcept;

    private:
        friend class ConditionVariable;
        detail::InlineStorage<40, 8> m_storage;
    };

    class SpinLock
    {
    public:
        explicit SpinLock(bool initiallyAcquired = false) noexcept;
        ~SpinLock();
        SpinLock(const SpinLock&) = delete;
        SpinLock& operator=(const SpinLock&) = delete;

        [[nodiscard]] bool TryAcquire() noexcept;
        void Acquire() noexcept;
        void Release() noexcept;

    private:
        detail::InlineStorage<1, 1> m_storage;
    };

    class LightMutex
    {
    public:
        LightMutex() noexcept;
        ~LightMutex();
        LightMutex(const LightMutex&) = delete;
        LightMutex& operator=(const LightMutex&) = delete;

        void Acquire() noexcept;
        void Release() noexcept;

    private:
        detail::InlineStorage<8, 4> m_storage;
    };

    class RWLock
    {
    public:
        RWLock() noexcept;
        ~RWLock();
        RWLock(const RWLock&) = delete;
        RWLock& operator=(const RWLock&) = delete;

        void Acquire() noexcept;
        void Release() noexcept;
        void AcquireShared() noexcept;
        void ReleaseShared() noexcept;

    private:
        detail::InlineStorage<8, 8> m_storage;
    };

    class RWSpinLock
    {
    public:
        RWSpinLock() noexcept;
        ~RWSpinLock();
        RWSpinLock(const RWSpinLock&) = delete;
        RWSpinLock& operator=(const RWSpinLock&) = delete;

        void Acquire() noexcept;
        [[nodiscard]] bool TryAcquire() noexcept;
        void Release() noexcept;
        void AcquireShared() noexcept;
        [[nodiscard]] bool TryAcquireShared() noexcept;
        void ReleaseShared() noexcept;

    private:
        detail::InlineStorage<1, 1> m_storage;
    };

    class Semaphore
    {
    public:
        Semaphore(i32 initialCount, i32 maximumCount) noexcept;
        ~Semaphore();
        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        void Acquire() noexcept;
        [[nodiscard]] bool TryAcquire(u32 timeoutMilliseconds = 0) noexcept;
        void Release(i32 count = 1) noexcept;
        [[nodiscard]] const void* GetOSHandle() const noexcept;

    private:
        detail::InlineStorage<8, 8> m_storage;
    };

    class ConditionVariable
    {
    public:
        ConditionVariable() noexcept;
        ~ConditionVariable();
        ConditionVariable(const ConditionVariable&) = delete;
        ConditionVariable& operator=(const ConditionVariable&) = delete;

        void Wait(Mutex& mutex) noexcept;
        void Wait(Mutex& mutex, u32 timeoutMilliseconds) noexcept;
        void WakeAll() noexcept;
        void WakeAny() noexcept;

    private:
        detail::InlineStorage<8, 8> m_storage;
    };

    class ManualResetEvent
    {
    public:
        explicit ManualResetEvent(bool signalled = false) noexcept;
        ~ManualResetEvent();
        ManualResetEvent(const ManualResetEvent&) = delete;
        ManualResetEvent& operator=(const ManualResetEvent&) = delete;

        void Signal() noexcept;
        void Reset() noexcept;
        void Wait() noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) noexcept;
        [[nodiscard]] const void* GetOSHandle() const noexcept;

    private:
        detail::InlineStorage<8, 8> m_storage;
    };
} // namespace vanguard::concurrency

#define VG_CONCURRENCY_JOIN_IMPL(left, right) left##right
#define VG_CONCURRENCY_JOIN(left, right) VG_CONCURRENCY_JOIN_IMPL(left, right)
#define VG_SCOPE_LOCK(lock) ::vanguard::concurrency::ScopedLock<decltype(lock)> VG_CONCURRENCY_JOIN(vgScopedLock, __LINE__)(lock)
#define VG_SCOPE_SHARED_LOCK(lock)                                                                                                         \
    ::vanguard::concurrency::ScopedSharedLock<decltype(lock)> VG_CONCURRENCY_JOIN(vgScopedSharedLock, __LINE__)(lock)
