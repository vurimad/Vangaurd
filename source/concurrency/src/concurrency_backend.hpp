#pragma once

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/system/types.hpp>

namespace vanguard::concurrency::backend
{
    void ConstructMutex(void* storage) noexcept;
    void DestroyMutex(void* storage) noexcept;
    void AcquireMutex(void* storage) noexcept;
    [[nodiscard]] bool TryAcquireMutex(void* storage) noexcept;
    void ReleaseMutex(void* storage) noexcept;
    void SetMutexSpinCount(void* storage, u32 count) noexcept;

    void ConstructSpinLock(void* storage, bool acquired) noexcept;
    void DestroySpinLock(void* storage) noexcept;
    [[nodiscard]] bool TryAcquireSpinLock(void* storage) noexcept;
    void AcquireSpinLock(void* storage) noexcept;
    void ReleaseSpinLock(void* storage) noexcept;

    void ConstructLightMutex(void* storage) noexcept;
    void DestroyLightMutex(void* storage) noexcept;
    void AcquireLightMutex(void* storage) noexcept;
    void ReleaseLightMutex(void* storage) noexcept;

    void ConstructRWLock(void* storage) noexcept;
    void DestroyRWLock(void* storage) noexcept;
    void AcquireRWLock(void* storage) noexcept;
    void ReleaseRWLock(void* storage) noexcept;
    void AcquireSharedRWLock(void* storage) noexcept;
    void ReleaseSharedRWLock(void* storage) noexcept;

    void ConstructRWSpinLock(void* storage) noexcept;
    void DestroyRWSpinLock(void* storage) noexcept;
    void AcquireRWSpinLock(void* storage) noexcept;
    [[nodiscard]] bool TryAcquireRWSpinLock(void* storage) noexcept;
    void ReleaseRWSpinLock(void* storage) noexcept;
    void AcquireSharedRWSpinLock(void* storage) noexcept;
    [[nodiscard]] bool TryAcquireSharedRWSpinLock(void* storage) noexcept;
    void ReleaseSharedRWSpinLock(void* storage) noexcept;

    void ConstructSemaphore(void* storage, i32 initial, i32 maximum) noexcept;
    void DestroySemaphore(void* storage) noexcept;
    void AcquireSemaphore(void* storage) noexcept;
    [[nodiscard]] bool TryAcquireSemaphore(void* storage, u32 timeout) noexcept;
    void ReleaseSemaphore(void* storage, i32 count) noexcept;
    [[nodiscard]] const void* GetSemaphoreHandle(const void* storage) noexcept;

    void ConstructConditionVariable(void* storage) noexcept;
    void DestroyConditionVariable(void* storage) noexcept;
    void WaitConditionVariable(void* storage, void* mutex) noexcept;
    void WaitConditionVariable(void* storage, void* mutex, u32 timeout) noexcept;
    void WakeAllConditionVariable(void* storage) noexcept;
    void WakeAnyConditionVariable(void* storage) noexcept;

    void ConstructManualResetEvent(void* storage, bool signalled) noexcept;
    void DestroyManualResetEvent(void* storage) noexcept;
    void SignalManualResetEvent(void* storage) noexcept;
    void ResetManualResetEvent(void* storage) noexcept;
    void WaitManualResetEvent(void* storage) noexcept;
    [[nodiscard]] bool TryWaitManualResetEvent(void* storage, u32 timeout) noexcept;
    [[nodiscard]] const void* GetManualResetEventHandle(const void* storage) noexcept;

    [[nodiscard]] ThreadId CurrentThreadId() noexcept;
    void YieldCurrentThread() noexcept;
    void SleepOnCurrentThread(u32 milliseconds) noexcept;
    void SetCurrentThreadAffinity(u64 mask) noexcept;
    void SetCurrentThreadName(const char* name) noexcept;
    [[nodiscard]] u32 GetMaxHardwareConcurrency() noexcept;

    [[nodiscard]] void* ConstructThread(void* storage, usize storageSize, Thread* owner, const char* name, u32 stackSize) noexcept;
    void DestroyThread(void* thread) noexcept;
    void InitThread(void* thread) noexcept;
    void JoinThread(void* thread) noexcept;
    void DetachThread(void* thread) noexcept;
    [[nodiscard]] bool IsThreadValid(const void* thread) noexcept;
    void SetThreadAffinity(void* thread, u64 mask) noexcept;
    void SetThreadPriority(void* thread, ThreadPriority priority) noexcept;
    void DisableThreadPriorityBoost(void* thread, bool disabled) noexcept;
    [[nodiscard]] const char* GetThreadName(const void* thread) noexcept;
} // namespace vanguard::concurrency::backend
