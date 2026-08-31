#pragma once

#include <vanguard/system/compiler.hpp>
#include <vanguard/system/types.hpp>

#include <intrin.h>
#include <type_traits>

namespace vanguard::concurrency
{
    template <typename T> class Atomic
    {
        static_assert(std::is_integral_v<T>);
        static_assert(!std::is_same_v<T, bool>);
        static_assert(sizeof(T) == 4 || sizeof(T) == 8);

        using Storage = std::conditional_t<sizeof(T) == 4, long, long long>;

    public:
        explicit Atomic(const T value = T{}) noexcept : m_target(static_cast<Storage>(value)) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] T Increment() noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedIncrement(&m_target));
            }
            else
            {
                return static_cast<T>(_InterlockedIncrement64(&m_target));
            }
        }

        [[nodiscard]] T Decrement() noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedDecrement(&m_target));
            }
            else
            {
                return static_cast<T>(_InterlockedDecrement64(&m_target));
            }
        }

        [[nodiscard]] T PostIncrement() noexcept
        {
            return ExchangeAdd(static_cast<T>(1));
        }

        [[nodiscard]] T PostDecrement() noexcept
        {
            return ExchangeAdd(static_cast<T>(-1));
        }

        [[nodiscard]] T Or(const T value) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedOr(&m_target, static_cast<Storage>(value)));
            }
            else
            {
                return static_cast<T>(_InterlockedOr64(&m_target, static_cast<Storage>(value)));
            }
        }

        [[nodiscard]] T And(const T value) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedAnd(&m_target, static_cast<Storage>(value)));
            }
            else
            {
                return static_cast<T>(_InterlockedAnd64(&m_target, static_cast<Storage>(value)));
            }
        }

        [[nodiscard]] T Exchange(const T value) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedExchange(&m_target, static_cast<Storage>(value)));
            }
            else
            {
                return static_cast<T>(_InterlockedExchange64(&m_target, static_cast<Storage>(value)));
            }
        }

        [[nodiscard]] T CompareExchange(const T exchange, const T comparand) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedCompareExchange(&m_target, static_cast<Storage>(exchange), static_cast<Storage>(comparand)));
            }
            else
            {
                return static_cast<T>(_InterlockedCompareExchange64(&m_target, static_cast<Storage>(exchange), static_cast<Storage>(comparand)));
            }
        }

        [[nodiscard]] T ExchangeAdd(const T value) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedExchangeAdd(&m_target, static_cast<Storage>(value)));
            }
            else
            {
                return static_cast<T>(_InterlockedExchangeAdd64(&m_target, static_cast<Storage>(value)));
            }
        }

        void SetValue(const T value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] T GetValue() const noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_InterlockedCompareExchange(&m_target, 0, 0));
            }
            else
            {
                return static_cast<T>(_InterlockedCompareExchange64(&m_target, 0, 0));
            }
        }

    private:
        mutable volatile Storage m_target;
    };

    template <typename T> class Atomic<T*>
    {
    public:
        explicit Atomic(T* const value = nullptr) noexcept : m_target(value) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] T* Exchange(T* const value) noexcept
        {
            return static_cast<T*>(_InterlockedExchangePointer(&m_target, value));
        }

        [[nodiscard]] T* CompareExchange(T* const exchange, T* const comparand) noexcept
        {
            return static_cast<T*>(_InterlockedCompareExchangePointer(&m_target, exchange, comparand));
        }

        void SetValue(T* const value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] T* GetValue() const noexcept
        {
            return static_cast<T*>(_InterlockedCompareExchangePointer(&m_target, nullptr, nullptr));
        }

    private:
        mutable void* volatile m_target;
    };

    template <> class Atomic<bool>
    {
    public:
        explicit Atomic(const bool value = false) noexcept : m_target(value ? 1L : 0L) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] bool Exchange(const bool value) noexcept
        {
            return _InterlockedExchange(&m_target, value ? 1L : 0L) != 0;
        }

        [[nodiscard]] bool CompareExchange(const bool exchange, const bool comparand) noexcept
        {
            return _InterlockedCompareExchange(&m_target, exchange ? 1L : 0L, comparand ? 1L : 0L) != 0;
        }

        void SetValue(const bool value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] bool GetValue() const noexcept
        {
            return _InterlockedCompareExchange(&m_target, 0L, 0L) != 0;
        }

    private:
        mutable volatile long m_target;
    };
} // namespace vanguard::concurrency
