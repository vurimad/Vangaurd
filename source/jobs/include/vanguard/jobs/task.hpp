#pragma once

#include <vanguard/memory/memory.hpp>
#include <vanguard/system/types.hpp>

#include <new>
#include <type_traits>
#include <utility>

namespace vanguard::jobs
{
    struct JobContext
    {
        const char* debugName = "";
        i32 parallelForTeamIndex = -1;
        u32 dispatcherThreadIndex = 0;

    private:
        const void* m_backendContext = nullptr;

        friend class Builder;
        friend struct BackendContextAccess;
    };

    namespace detail
    {
        struct TaskPacket
        {
            void* state = nullptr;
            void (*execute)(void*, const JobContext&) noexcept = nullptr;
            void (*destroy)(void*) noexcept = nullptr;
        };

        struct ParallelTaskPacket
        {
            void* state = nullptr;
            void (*execute)(void*, u32, const JobContext&) noexcept = nullptr;
            void (*destroy)(void*) noexcept = nullptr;
        };
    } // namespace detail

    class Task
    {
    public:
        Task() noexcept = default;
        ~Task()
        {
            Reset();
        }

        Task(Task&& other) noexcept : m_packet(other.Release()) {}

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_packet = other.Release();
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        template <typename Function> [[nodiscard]] static Task Create(Function&& function) noexcept
        {
            using StoredFunction = std::remove_cv_t<std::remove_reference_t<Function>>;
            static_assert(std::is_nothrow_invocable_v<StoredFunction&, const JobContext&>, "Vanguard jobs must be noexcept-callable with const JobContext&.");

            struct Payload
            {
                StoredFunction function;
                memory::MemoryBlock allocation;
            };

            memory::MemoryBlock allocation = memory::Allocate(memory::PoolId::Jobs, sizeof(Payload), alignof(Payload));
            if (!allocation)
            {
                return {};
            }

            auto* payload = new (allocation.address) Payload{std::forward<Function>(function), allocation};

            Task task;
            task.m_packet = {payload, [](void* state, const JobContext& context) noexcept { static_cast<Payload*>(state)->function(context); },
                             [](void* state) noexcept
                             {
                                 auto* payloadToDestroy = static_cast<Payload*>(state);
                                 memory::MemoryBlock block = payloadToDestroy->allocation;
                                 payloadToDestroy->~Payload();
                                 memory::Free(block);
                             }};
            return task;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_packet.state != nullptr;
        }

    private:
        friend class Builder;

        [[nodiscard]] detail::TaskPacket Release() noexcept
        {
            detail::TaskPacket packet = m_packet;
            m_packet = {};
            return packet;
        }

        void Reset() noexcept
        {
            if (m_packet.state != nullptr)
            {
                m_packet.destroy(m_packet.state);
                m_packet = {};
            }
        }

        detail::TaskPacket m_packet;
    };

    class ParallelTask
    {
    public:
        ParallelTask() noexcept = default;
        ~ParallelTask()
        {
            Reset();
        }

        ParallelTask(ParallelTask&& other) noexcept : m_packet(other.Release()) {}

        ParallelTask& operator=(ParallelTask&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_packet = other.Release();
            }
            return *this;
        }

        ParallelTask(const ParallelTask&) = delete;
        ParallelTask& operator=(const ParallelTask&) = delete;

        template <typename Function> [[nodiscard]] static ParallelTask Create(Function&& function) noexcept
        {
            using StoredFunction = std::remove_cv_t<std::remove_reference_t<Function>>;
            static_assert(std::is_nothrow_invocable_v<StoredFunction&, u32, const JobContext&>,
                          "Parallel jobs must be noexcept-callable with (u32, const JobContext&).");

            struct Payload
            {
                StoredFunction function;
                memory::MemoryBlock allocation;
            };

            memory::MemoryBlock allocation = memory::Allocate(memory::PoolId::Jobs, sizeof(Payload), alignof(Payload));
            if (!allocation)
            {
                return {};
            }

            auto* payload = new (allocation.address) Payload{std::forward<Function>(function), allocation};

            ParallelTask task;
            task.m_packet = {payload,
                             [](void* state, u32 index, const JobContext& context) noexcept { static_cast<Payload*>(state)->function(index, context); },
                             [](void* state) noexcept
                             {
                                 auto* payloadToDestroy = static_cast<Payload*>(state);
                                 memory::MemoryBlock block = payloadToDestroy->allocation;
                                 payloadToDestroy->~Payload();
                                 memory::Free(block);
                             }};
            return task;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_packet.state != nullptr;
        }

    private:
        friend class Builder;

        [[nodiscard]] detail::ParallelTaskPacket Release() noexcept
        {
            detail::ParallelTaskPacket packet = m_packet;
            m_packet = {};
            return packet;
        }

        void Reset() noexcept
        {
            if (m_packet.state != nullptr)
            {
                m_packet.destroy(m_packet.state);
                m_packet = {};
            }
        }

        detail::ParallelTaskPacket m_packet;
    };
} // namespace vanguard::jobs
