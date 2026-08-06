#include <vanguard/concurrency/synchronization.hpp>

#include "concurrency_backend.hpp"

namespace vanguard::concurrency
{
    Mutex::Mutex() noexcept
    {
        backend::ConstructMutex(&m_storage);
    }
    Mutex::~Mutex()
    {
        backend::DestroyMutex(&m_storage);
    }
    void Mutex::Acquire() noexcept
    {
        backend::AcquireMutex(&m_storage);
    }
    bool Mutex::TryAcquire() noexcept
    {
        return backend::TryAcquireMutex(&m_storage);
    }
    void Mutex::Release() noexcept
    {
        backend::ReleaseMutex(&m_storage);
    }
    void Mutex::SetSpinCount(const u32 count) noexcept
    {
        backend::SetMutexSpinCount(&m_storage, count);
    }

    SpinLock::SpinLock(const bool acquired) noexcept
    {
        backend::ConstructSpinLock(&m_storage, acquired);
    }
    SpinLock::~SpinLock()
    {
        backend::DestroySpinLock(&m_storage);
    }
    bool SpinLock::TryAcquire() noexcept
    {
        return backend::TryAcquireSpinLock(&m_storage);
    }
    void SpinLock::Acquire() noexcept
    {
        backend::AcquireSpinLock(&m_storage);
    }
    void SpinLock::Release() noexcept
    {
        backend::ReleaseSpinLock(&m_storage);
    }

    LightMutex::LightMutex() noexcept
    {
        backend::ConstructLightMutex(&m_storage);
    }
    LightMutex::~LightMutex()
    {
        backend::DestroyLightMutex(&m_storage);
    }
    void LightMutex::Acquire() noexcept
    {
        backend::AcquireLightMutex(&m_storage);
    }
    void LightMutex::Release() noexcept
    {
        backend::ReleaseLightMutex(&m_storage);
    }

    RWLock::RWLock() noexcept
    {
        backend::ConstructRWLock(&m_storage);
    }
    RWLock::~RWLock()
    {
        backend::DestroyRWLock(&m_storage);
    }
    void RWLock::Acquire() noexcept
    {
        backend::AcquireRWLock(&m_storage);
    }
    void RWLock::Release() noexcept
    {
        backend::ReleaseRWLock(&m_storage);
    }
    void RWLock::AcquireShared() noexcept
    {
        backend::AcquireSharedRWLock(&m_storage);
    }
    void RWLock::ReleaseShared() noexcept
    {
        backend::ReleaseSharedRWLock(&m_storage);
    }

    RWSpinLock::RWSpinLock() noexcept
    {
        backend::ConstructRWSpinLock(&m_storage);
    }
    RWSpinLock::~RWSpinLock()
    {
        backend::DestroyRWSpinLock(&m_storage);
    }
    void RWSpinLock::Acquire() noexcept
    {
        backend::AcquireRWSpinLock(&m_storage);
    }
    bool RWSpinLock::TryAcquire() noexcept
    {
        return backend::TryAcquireRWSpinLock(&m_storage);
    }
    void RWSpinLock::Release() noexcept
    {
        backend::ReleaseRWSpinLock(&m_storage);
    }
    void RWSpinLock::AcquireShared() noexcept
    {
        backend::AcquireSharedRWSpinLock(&m_storage);
    }
    bool RWSpinLock::TryAcquireShared() noexcept
    {
        return backend::TryAcquireSharedRWSpinLock(&m_storage);
    }
    void RWSpinLock::ReleaseShared() noexcept
    {
        backend::ReleaseSharedRWSpinLock(&m_storage);
    }

    Semaphore::Semaphore(const i32 initial, const i32 maximum) noexcept
    {
        backend::ConstructSemaphore(&m_storage, initial, maximum);
    }
    Semaphore::~Semaphore()
    {
        backend::DestroySemaphore(&m_storage);
    }
    void Semaphore::Acquire() noexcept
    {
        backend::AcquireSemaphore(&m_storage);
    }
    bool Semaphore::TryAcquire(const u32 timeout) noexcept
    {
        return backend::TryAcquireSemaphore(&m_storage, timeout);
    }
    void Semaphore::Release(const i32 count) noexcept
    {
        backend::ReleaseSemaphore(&m_storage, count);
    }
    const void* Semaphore::GetOSHandle() const noexcept
    {
        return backend::GetSemaphoreHandle(&m_storage);
    }

    ConditionVariable::ConditionVariable() noexcept
    {
        backend::ConstructConditionVariable(&m_storage);
    }
    ConditionVariable::~ConditionVariable()
    {
        backend::DestroyConditionVariable(&m_storage);
    }
    void ConditionVariable::Wait(Mutex& mutex) noexcept
    {
        backend::WaitConditionVariable(&m_storage, &mutex.m_storage);
    }
    void ConditionVariable::Wait(Mutex& mutex, const u32 timeout) noexcept
    {
        backend::WaitConditionVariable(&m_storage, &mutex.m_storage, timeout);
    }
    void ConditionVariable::WakeAll() noexcept
    {
        backend::WakeAllConditionVariable(&m_storage);
    }
    void ConditionVariable::WakeAny() noexcept
    {
        backend::WakeAnyConditionVariable(&m_storage);
    }

    ManualResetEvent::ManualResetEvent(const bool signalled) noexcept
    {
        backend::ConstructManualResetEvent(&m_storage, signalled);
    }
    ManualResetEvent::~ManualResetEvent()
    {
        backend::DestroyManualResetEvent(&m_storage);
    }
    void ManualResetEvent::Signal() noexcept
    {
        backend::SignalManualResetEvent(&m_storage);
    }
    void ManualResetEvent::Reset() noexcept
    {
        backend::ResetManualResetEvent(&m_storage);
    }
    void ManualResetEvent::Wait() noexcept
    {
        backend::WaitManualResetEvent(&m_storage);
    }
    bool ManualResetEvent::TryWait(const u32 timeout) noexcept
    {
        return backend::TryWaitManualResetEvent(&m_storage, timeout);
    }
    const void* ManualResetEvent::GetOSHandle() const noexcept
    {
        return backend::GetManualResetEventHandle(&m_storage);
    }
} // namespace vanguard::concurrency
