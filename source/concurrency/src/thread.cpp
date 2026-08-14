#include <vanguard/concurrency/thread.hpp>

#include <vanguard/concurrency/concurrency_backend.hpp>

namespace
{
    vanguard::concurrency::ThreadId g_mainThreadId;
}

namespace vanguard::concurrency
{
    ThreadId ThreadId::CurrentThread() noexcept
    {
        return backend::CurrentThreadId();
    }
    void YieldCurrentThread() noexcept
    {
        backend::YieldCurrentThread();
    }
    void SleepOnCurrentThread(const u32 milliseconds) noexcept
    {
        backend::SleepOnCurrentThread(milliseconds);
    }
    void SetCurrentThreadAffinity(const u64 mask) noexcept
    {
        backend::SetCurrentThreadAffinity(mask);
    }
    void SetCurrentThreadName(const char* const name) noexcept
    {
        backend::SetCurrentThreadName(name);
    }
    u32 GetMaxHardwareConcurrency() noexcept
    {
        return backend::GetMaxHardwareConcurrency();
    }

    void InitializeMainThread() noexcept
    {
        g_mainThreadId = ThreadId::CurrentThread();
    }
    bool IsMainThread() noexcept
    {
        return !g_mainThreadId.IsValid() || g_mainThreadId == ThreadId::CurrentThread();
    }

    Thread::Thread(const char* const name, const ThreadMemoryParameters parameters) noexcept
        : m_backend(backend::ConstructThread(m_backendStorage, sizeof(m_backendStorage), this, name, parameters.stackSize))
    {
    }

    Thread::~Thread()
    {
        backend::DestroyThread(m_backend);
    }
    void Thread::InitThread() noexcept
    {
        backend::InitThread(m_backend);
    }
    void Thread::JoinThread() noexcept
    {
        backend::JoinThread(m_backend);
    }
    void Thread::DetachThread() noexcept
    {
        backend::DetachThread(m_backend);
    }
    bool Thread::IsValid() const noexcept
    {
        return backend::IsThreadValid(m_backend);
    }
    void Thread::SetAffinityMask(const u64 mask) noexcept
    {
        backend::SetThreadAffinity(m_backend, mask);
    }
    void Thread::SetPriority(const ThreadPriority priority) noexcept
    {
        backend::SetThreadPriority(m_backend, priority);
    }
    void Thread::DisablePriorityBoost(const bool disabled) noexcept
    {
        backend::DisableThreadPriorityBoost(m_backend, disabled);
    }
    const char* Thread::GetThreadName() const noexcept
    {
        return backend::GetThreadName(m_backend);
    }
} // namespace vanguard::concurrency
