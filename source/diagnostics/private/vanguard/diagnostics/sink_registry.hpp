#pragma once

#include <vanguard/diagnostics/diagnostics.hpp>

#include <atomic>
#include <thread>

namespace vanguard::diagnostics::detail
{
    inline void PublishSinkCompletedGeneration(std::atomic<u64>& completedGeneration, const u64 generation) noexcept
    {
        u64 completed = completedGeneration.load(std::memory_order_relaxed);
        while (completed < generation &&
               !completedGeneration.compare_exchange_weak(completed, generation, std::memory_order_release,
                                                          std::memory_order_relaxed))
        {
        }
    }

    struct SinkRegistrationGenerationSource final
    {
        std::atomic<u64>* counter = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return counter != nullptr;
        }
    };

    inline thread_local u32 SinkCallbackDepth = 0;

    class SinkRegistryCore final
    {
    public:
        explicit SinkRegistryCore(const SinkRegistrationGenerationSource generations) noexcept
            : m_generations(generations)
        {
        }

        SinkRegistryCore(const SinkRegistryCore&) = delete;
        SinkRegistryCore& operator=(const SinkRegistryCore&) = delete;

        void SetLegacyCallback(const SinkCallback callback, void* const userData) noexcept
        {
            Lock();
            m_legacyCallback = callback;
            m_legacyUserData = userData;
            Unlock();
        }

        [[nodiscard]] SinkRegistrationResult Register(const SinkCallback callback, void* const userData,
                                                      SinkRegistrationToken& output) noexcept
        {
            if (SinkCallbackDepth != 0)
                return SinkRegistrationResult::ReentrantOperation;
            if (callback == nullptr || !m_generations.IsValid())
                return SinkRegistrationResult::InvalidArgument;

            Lock();
            u32 index = MaximumSinkRegistrations;
            for (u32 candidate = 0; candidate < MaximumSinkRegistrations; ++candidate)
            {
                if (m_slots[candidate].state == SlotState::Free)
                {
                    index = candidate;
                    break;
                }
            }
            if (index == MaximumSinkRegistrations)
            {
                Unlock();
                return SinkRegistrationResult::CapacityExceeded;
            }

            const u64 generation = ReserveGeneration();
            if (generation == 0)
            {
                Unlock();
                return SinkRegistrationResult::GenerationExhausted;
            }

            Slot& slot = m_slots[index];
            slot.callback = callback;
            slot.userData = userData;
            slot.generation = generation;
            slot.inFlight.store(0, std::memory_order_relaxed);
            slot.joinedRetirementGeneration = 0;
            slot.state = SlotState::Live;
            Unlock();

            output = {index, generation};
            return SinkRegistrationResult::Success;
        }

        [[nodiscard]] SinkRegistrationResult Unregister(const SinkRegistrationToken token) noexcept
        {
            if (SinkCallbackDepth != 0)
                return SinkRegistrationResult::ReentrantOperation;
            if (!token.IsValid())
                return SinkRegistrationResult::StaleToken;

            bool retirementOwner = false;
            Lock();
            Slot& slot = m_slots[token.index];
            if (slot.generation != token.generation || slot.state == SlotState::Free)
            {
                Unlock();
                return SinkRegistrationResult::StaleToken;
            }
            if (slot.state == SlotState::Live)
            {
                slot.state = SlotState::Retiring;
                slot.joinedRetirementGeneration = 0;
                retirementOwner = true;
            }
            else if (slot.state != SlotState::Retiring)
            {
                Unlock();
                return SinkRegistrationResult::InternalFailure;
            }
            else
            {
                slot.joinedRetirementGeneration = token.generation;
            }
            Unlock();

            if (retirementOwner)
            {
                while (slot.inFlight.load(std::memory_order_acquire) != 0 &&
                       slot.completedGeneration.load(std::memory_order_acquire) < token.generation)
                    std::this_thread::yield();
                PublishSinkCompletedGeneration(slot.completedGeneration, token.generation);
                Lock();
                if (slot.state == SlotState::Retiring && slot.generation == token.generation)
                {
                    slot.callback = nullptr;
                    slot.userData = nullptr;
                    slot.generation = 0;
                    slot.state = SlotState::Free;
                }
                Unlock();
            }
            else
            {
                while (slot.completedGeneration.load(std::memory_order_acquire) < token.generation)
                    std::this_thread::yield();
            }
            return SinkRegistrationResult::Success;
        }

        [[nodiscard]] bool IsRegistered(const SinkRegistrationToken token) noexcept
        {
            if (!token.IsValid())
                return false;
            Lock();
            const Slot& slot = m_slots[token.index];
            const bool live = slot.state == SlotState::Live && slot.generation == token.generation;
            Unlock();
            return live;
        }

        [[nodiscard]] bool HasRetirementJoiner(const SinkRegistrationToken token) noexcept
        {
            if (!token.IsValid())
                return false;
            Lock();
            const Slot& slot = m_slots[token.index];
            const bool joined = slot.state == SlotState::Retiring && slot.generation == token.generation &&
                                slot.joinedRetirementGeneration == token.generation;
            Unlock();
            return joined;
        }

        void Dispatch(const MessageView& message) noexcept
        {
            SinkCallback legacyCallback = nullptr;
            void* legacyUserData = nullptr;
            ClaimedSlot claimed[MaximumSinkRegistrations]{};
            u32 claimedCount = 0;

            Lock();
            legacyCallback = m_legacyCallback;
            legacyUserData = m_legacyUserData;
            for (u32 index = 0; index < MaximumSinkRegistrations; ++index)
            {
                Slot& slot = m_slots[index];
                if (slot.state != SlotState::Live || slot.callback == nullptr)
                    continue;
                slot.inFlight.fetch_add(1, std::memory_order_acq_rel);
                claimed[claimedCount++] = {&slot, slot.callback, slot.userData};
            }
            Unlock();

            if (legacyCallback != nullptr)
            {
                ++SinkCallbackDepth;
                legacyCallback(message, legacyUserData);
                --SinkCallbackDepth;
            }

            for (u32 index = 0; index < claimedCount; ++index)
            {
                ClaimedSlot& callback = claimed[index];
                ++SinkCallbackDepth;
                callback.callback(message, callback.userData);
                --SinkCallbackDepth;
                callback.slot->inFlight.fetch_sub(1, std::memory_order_acq_rel);
            }
        }

        void ClearRegisteredAfterAggregateFence() noexcept
        {
            Lock();
            for (Slot& slot : m_slots)
            {
                slot.callback = nullptr;
                slot.userData = nullptr;
                if (slot.generation != 0)
                    PublishSinkCompletedGeneration(slot.completedGeneration, slot.generation);
                slot.generation = 0;
                slot.inFlight.store(0, std::memory_order_relaxed);
                slot.joinedRetirementGeneration = 0;
                slot.state = SlotState::Free;
            }
            Unlock();
        }

    private:
        enum class SlotState : u8
        {
            Free,
            Live,
            Retiring
        };

        struct Slot final
        {
            std::atomic<u32> inFlight{0};
            std::atomic<u64> completedGeneration{0};
            SinkCallback callback = nullptr;
            void* userData = nullptr;
            u64 generation = 0;
            u64 joinedRetirementGeneration = 0;
            SlotState state = SlotState::Free;
        };

        struct ClaimedSlot final
        {
            Slot* slot = nullptr;
            SinkCallback callback = nullptr;
            void* userData = nullptr;
        };

        void Lock() noexcept
        {
            while (m_lock.test_and_set(std::memory_order_acquire))
                std::this_thread::yield();
        }

        void Unlock() noexcept
        {
            m_lock.clear(std::memory_order_release);
        }

        [[nodiscard]] u64 ReserveGeneration() const noexcept
        {
            u64 current = m_generations.counter->load(std::memory_order_relaxed);
            for (;;)
            {
                if (current == ~u64{0})
                    return 0;
                if (m_generations.counter->compare_exchange_weak(current, current + 1u, std::memory_order_relaxed,
                                                                 std::memory_order_relaxed))
                    return current + 1u;
            }
        }

        SinkRegistrationGenerationSource m_generations;
        std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
        SinkCallback m_legacyCallback = nullptr;
        void* m_legacyUserData = nullptr;
        Slot m_slots[MaximumSinkRegistrations]{};
    };
} // namespace vanguard::diagnostics::detail
