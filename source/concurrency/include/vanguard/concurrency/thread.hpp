#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::concurrency
{
    inline constexpr usize MaxThreadNameLength = 31;
    inline constexpr u32 DefaultThreadStackSize = 2 * 1024 * 1024;

    struct ThreadId
    {
        u32 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] constexpr u32 AsNumber() const noexcept
        {
            return value;
        }

        [[nodiscard]] static ThreadId GetCurrentThread() noexcept;

        [[nodiscard]] friend constexpr bool operator==(ThreadId lhs, ThreadId rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        [[nodiscard]] friend constexpr bool operator!=(ThreadId lhs, ThreadId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    enum class ThreadPriority : i32
    {
        Idle,
        Lowest,
        BelowNormal,
        Normal,
        AboveNormal,
        Highest,
        TimeCritical
    };

    struct ThreadMemoryParameters
    {
        u32 stackSize = DefaultThreadStackSize;
    };

    void YieldCurrentThread() noexcept;
    void SleepOnCurrentThread(u32 milliseconds) noexcept;
    void SetCurrentThreadAffinity(u64 affinityMask) noexcept;
    void SetCurrentThreadName(const char* name) noexcept;
    [[nodiscard]] u32 GetMaxHardwareConcurrency() noexcept;

    void InitializeMainThread() noexcept;
    [[nodiscard]] bool IsMainThread() noexcept;

    class Thread
    {
    public:
        explicit Thread(const char* name, ThreadMemoryParameters parameters = {}) noexcept;
        virtual ~Thread();

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        void InitThread() noexcept;
        void JoinThread() noexcept;
        void DetachThread() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

        void SetAffinityMask(u64 mask) noexcept;
        void SetPriority(ThreadPriority priority) noexcept;
        void DisablePriorityBoost(bool disabled) noexcept;
        [[nodiscard]] const char* GetThreadName() const noexcept;

        virtual void ThreadFunction() noexcept = 0;

    private:
        static constexpr usize BackendStorageSize = 128;
        static constexpr usize BackendStorageAlignment = 16;

        alignas(BackendStorageAlignment) byte m_backendStorage[BackendStorageSize]{};
        void* m_backend = nullptr;
    };
} // namespace vanguard::concurrency
