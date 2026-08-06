#include "build.h"

#include "../src/concurrency_backend.hpp"

#include "redThreadsThread.h"

#include <new>

namespace
{
    template<typename T>
    T& As(void* const storage) noexcept
    {
        return *std::launder(reinterpret_cast<T*>(storage));
    }

    template<typename T>
    const T& As(const void* const storage) noexcept
    {
        return *std::launder(reinterpret_cast<const T*>(storage));
    }

    static_assert(sizeof(red::Mutex) == 40);
    static_assert(alignof(red::Mutex) <= 8);
    static_assert(sizeof(red::SpinLock) == 1);
    static_assert(sizeof(red::LightMutex) == 8);
    static_assert(sizeof(red::RWLock) == 8);
    static_assert(sizeof(red::RWSpinLock) == 1);
    static_assert(sizeof(red::Semaphore) == 8);
    static_assert(sizeof(red::ConditionVariable) == 8);
    static_assert(sizeof(red::ManualResetEvent) == 8);

    class ThreadBridge final : public red::Thread
    {
    public:
        ThreadBridge(
            vanguard::concurrency::Thread* const owner,
            const char* const name,
            const vanguard::u32 stackSize)
            : red::Thread(name, red::ThreadMemParams(stackSize))
            , m_owner(owner)
        {
        }

        void ThreadFunc() override
        {
            m_owner->ThreadFunction();
        }

    private:
        vanguard::concurrency::Thread* m_owner;
    };

    static_assert(sizeof(ThreadBridge) <= 128);
    static_assert(alignof(ThreadBridge) <= 16);
}

namespace vanguard::concurrency::backend
{
    void ConstructMutex(void* s) noexcept { new (s) red::Mutex(); }
    void DestroyMutex(void* s) noexcept { As<red::Mutex>(s).~Mutex(); }
    void AcquireMutex(void* s) noexcept { As<red::Mutex>(s).Acquire(); }
    bool TryAcquireMutex(void* s) noexcept { return As<red::Mutex>(s).TryAcquire(); }
    void ReleaseMutex(void* s) noexcept { As<red::Mutex>(s).Release(); }
    void SetMutexSpinCount(void* s, const u32 count) noexcept { As<red::Mutex>(s).SetSpinCount(count); }

    void ConstructSpinLock(void* s, const bool acquired) noexcept { new (s) red::SpinLock(acquired); }
    void DestroySpinLock(void* s) noexcept { As<red::SpinLock>(s).~SpinLock(); }
    bool TryAcquireSpinLock(void* s) noexcept { return As<red::SpinLock>(s).TryAcquire(); }
    void AcquireSpinLock(void* s) noexcept { As<red::SpinLock>(s).Acquire(); }
    void ReleaseSpinLock(void* s) noexcept { As<red::SpinLock>(s).Release(); }

    void ConstructLightMutex(void* s) noexcept { new (s) red::LightMutex(); }
    void DestroyLightMutex(void* s) noexcept { As<red::LightMutex>(s).~LightMutex(); }
    void AcquireLightMutex(void* s) noexcept { As<red::LightMutex>(s).Acquire(); }
    void ReleaseLightMutex(void* s) noexcept { As<red::LightMutex>(s).Release(); }

    void ConstructRWLock(void* s) noexcept { new (s) red::RWLock(); }
    void DestroyRWLock(void* s) noexcept { As<red::RWLock>(s).~RWLock(); }
    void AcquireRWLock(void* s) noexcept { As<red::RWLock>(s).Acquire(); }
    void ReleaseRWLock(void* s) noexcept { As<red::RWLock>(s).Release(); }
    void AcquireSharedRWLock(void* s) noexcept { As<red::RWLock>(s).AcquireShared(); }
    void ReleaseSharedRWLock(void* s) noexcept { As<red::RWLock>(s).ReleaseShared(); }

    void ConstructRWSpinLock(void* s) noexcept { new (s) red::RWSpinLock(); }
    void DestroyRWSpinLock(void* s) noexcept { As<red::RWSpinLock>(s).~RWSpinLock(); }
    void AcquireRWSpinLock(void* s) noexcept { As<red::RWSpinLock>(s).Acquire(); }
    bool TryAcquireRWSpinLock(void* s) noexcept { return As<red::RWSpinLock>(s).TryAcquire(); }
    void ReleaseRWSpinLock(void* s) noexcept { As<red::RWSpinLock>(s).Release(); }
    void AcquireSharedRWSpinLock(void* s) noexcept { As<red::RWSpinLock>(s).AcquireShared(); }
    bool TryAcquireSharedRWSpinLock(void* s) noexcept { return As<red::RWSpinLock>(s).TryAcquireShared(); }
    void ReleaseSharedRWSpinLock(void* s) noexcept { As<red::RWSpinLock>(s).ReleaseShared(); }

    void ConstructSemaphore(void* s, const i32 initial, const i32 maximum) noexcept { new (s) red::Semaphore(initial, maximum); }
    void DestroySemaphore(void* s) noexcept { As<red::Semaphore>(s).~Semaphore(); }
    void AcquireSemaphore(void* s) noexcept { As<red::Semaphore>(s).Acquire(); }
    bool TryAcquireSemaphore(void* s, const u32 timeout) noexcept { return As<red::Semaphore>(s).TryAcquire(timeout); }
    void ReleaseSemaphore(void* s, const i32 count) noexcept { As<red::Semaphore>(s).Release(count); }
    const void* GetSemaphoreHandle(const void* s) noexcept { return As<red::Semaphore>(s).GetOSHandle(); }

    void ConstructConditionVariable(void* s) noexcept { new (s) red::ConditionVariable(); }
    void DestroyConditionVariable(void* s) noexcept { As<red::ConditionVariable>(s).~ConditionVariable(); }
    void WaitConditionVariable(void* s, void* m) noexcept { As<red::ConditionVariable>(s).Wait(As<red::Mutex>(m)); }
    void WaitConditionVariable(void* s, void* m, const u32 timeout) noexcept { As<red::ConditionVariable>(s).Wait(As<red::Mutex>(m), timeout); }
    void WakeAllConditionVariable(void* s) noexcept { As<red::ConditionVariable>(s).WakeAll(); }
    void WakeAnyConditionVariable(void* s) noexcept { As<red::ConditionVariable>(s).WakeAny(); }

    void ConstructManualResetEvent(void* s, const bool signalled) noexcept { new (s) red::ManualResetEvent(signalled); }
    void DestroyManualResetEvent(void* s) noexcept { As<red::ManualResetEvent>(s).~ManualResetEvent(); }
    void SignalManualResetEvent(void* s) noexcept { As<red::ManualResetEvent>(s).SetEvent(); }
    void ResetManualResetEvent(void* s) noexcept { As<red::ManualResetEvent>(s).ResetEvent(); }
    void WaitManualResetEvent(void* s) noexcept { As<red::ManualResetEvent>(s).Wait(); }
    bool TryWaitManualResetEvent(void* s, const u32 timeout) noexcept { return As<red::ManualResetEvent>(s).TryWait(timeout); }
    const void* GetManualResetEventHandle(const void* s) noexcept { return As<red::ManualResetEvent>(s).GetOSHandle(); }

    ThreadId CurrentThreadId() noexcept { return {red::ThreadId::CurrentThread().AsNumber()}; }
    void YieldCurrentThread() noexcept { red::YieldCurrentThread(); }
    void SleepOnCurrentThread(const u32 ms) noexcept { red::SleepOnCurrentThread(ms); }
    void SetCurrentThreadAffinity(const u64 mask) noexcept { red::SetCurrentThreadAffinity(mask); }
    void SetCurrentThreadName(const char* name) noexcept { red::SetCurrentThreadName(name); }
    u32 GetMaxHardwareConcurrency() noexcept { return red::GetMaxHardwareConcurrency(); }

    void* ConstructThread(
        void* const storage,
        const usize storageSize,
        Thread* const owner,
        const char* const name,
        const u32 stackSize) noexcept
    {
        RED_FATAL_ASSERT(
            storage != nullptr &&
                storageSize >= sizeof(ThreadBridge) &&
                (reinterpret_cast<usize>(storage) % alignof(ThreadBridge)) == 0,
            "Vanguard Thread inline backend storage is invalid.");
        return new (storage) ThreadBridge(owner, name, stackSize);
    }

    void DestroyThread(void* thread) noexcept
    {
        if (thread != nullptr)
        {
            As<ThreadBridge>(thread).~ThreadBridge();
        }
    }

    void InitThread(void* t) noexcept { if (t != nullptr) As<ThreadBridge>(t).InitThread(); }
    void JoinThread(void* t) noexcept { if (t != nullptr) As<ThreadBridge>(t).JoinThread(); }
    void DetachThread(void* t) noexcept { if (t != nullptr) As<ThreadBridge>(t).DetachThread(); }
    bool IsThreadValid(const void* t) noexcept { return t != nullptr && As<ThreadBridge>(t).IsValid(); }
    void SetThreadAffinity(void* t, const u64 mask) noexcept { if (t != nullptr) As<ThreadBridge>(t).SetAffinityMask(mask); }
    void SetThreadPriority(void* t, const ThreadPriority p) noexcept { if (t != nullptr) As<ThreadBridge>(t).SetPriority(static_cast<red::EThreadPriority>(p)); }
    void DisableThreadPriorityBoost(void* t, const bool disabled) noexcept { if (t != nullptr) As<ThreadBridge>(t).DisablePriorityBoost(disabled); }
    const char* GetThreadName(const void* t) noexcept { return t != nullptr ? As<ThreadBridge>(t).GetThreadName() : ""; }
}
