#pragma once

#include <vanguard/system/compiler.hpp>
#include <vanguard/system/types.hpp>

// Inline platform adaptation boundary; no backend call or runtime initialization.
#if !defined(_MSC_VER) || !defined(_M_X64) || defined(__clang__)
#error Vanguard atomics require MSVC x64 with /volatile:ms.
#endif

#ifndef RED_COMPILER_MSC
#define RED_COMPILER_MSC
#endif
#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 0
#endif

#if defined(VG_BUILD_DEBUG) && !defined(RED_CONFIGURATION_DEBUG)
#define RED_CONFIGURATION_DEBUG
#elif (defined(VG_BUILD_DEVELOPMENT) || defined(VG_BUILD_PROFILE)) && !defined(RED_CONFIGURATION_RELEASE)
#define RED_CONFIGURATION_RELEASE
#elif defined(VG_BUILD_SHIPPING) && !defined(RED_CONFIGURATION_FINAL)
#define RED_CONFIGURATION_FINAL
#endif

#include "../../../../imported/common/redSystem/include/architecture.h"
#include "../../../../imported/common/redSystem/include/compilerExtensions.h"
#include "../../../../imported/common/redSystem/include/os.h"
#include "../../../../imported/common/redSystem/include/types.h"
#include "../../../../imported/common/redSystem/include/redThreadsAtomicWinAPI.inl"

namespace vanguard::concurrency
{
    // GetValue is an acquire load under the supported compiler contract, not a full fence.
    // Mutations retain their full-barrier interlocked semantics, including SetValue.
    template <typename T> class Atomic
    {
        static_assert(std::is_integral_v<T>);
        static_assert(!std::is_same_v<T, bool>);
        static_assert(sizeof(T) == 4 || sizeof(T) == 8);

        using Operations = std::conditional_t<sizeof(T) == 4, ::red::WinAPI::AtomicOps32, ::red::WinAPI::AtomicOps64>;
        using Storage = std::conditional_t<sizeof(T) == 4, long, long long>;

    public:
        explicit Atomic(const T value = T{}) noexcept : m_target(static_cast<Storage>(value)) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] T Increment() noexcept
        {
            return static_cast<T>(Operations::Increment(&m_target));
        }

        [[nodiscard]] T Decrement() noexcept
        {
            return static_cast<T>(Operations::Decrement(&m_target));
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
            return static_cast<T>(Operations::Or(&m_target, static_cast<Storage>(value)));
        }

        [[nodiscard]] T And(const T value) noexcept
        {
            return static_cast<T>(Operations::And(&m_target, static_cast<Storage>(value)));
        }

        [[nodiscard]] T Exchange(const T value) noexcept
        {
            return static_cast<T>(Operations::Exchange(&m_target, static_cast<Storage>(value)));
        }

        [[nodiscard]] T CompareExchange(const T exchange, const T comparand) noexcept
        {
            return static_cast<T>(Operations::CompareExchange(&m_target, static_cast<Storage>(exchange), static_cast<Storage>(comparand)));
        }

        [[nodiscard]] T ExchangeAdd(const T value) noexcept
        {
            return static_cast<T>(Operations::ExchangeAdd(&m_target, static_cast<Storage>(value)));
        }

        void SetValue(const T value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] T GetValue() const noexcept
        {
            return static_cast<T>(Operations::FetchValue(&m_target));
        }

    private:
        alignas(sizeof(Storage)) mutable volatile Storage m_target;
    };

    template <typename T> class Atomic<T*>
    {
        using Operations = ::red::WinAPI::AtomicOpsPtr;
        using MutableT = std::remove_const_t<T>;

        [[nodiscard]] static void* StorageValue(T* const value) noexcept
        {
            return const_cast<MutableT*>(value);
        }

    public:
        explicit Atomic(T* const value = nullptr) noexcept : m_target(StorageValue(value)) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] T* Exchange(T* const value) noexcept
        {
            return static_cast<T*>(Operations::Exchange(&m_target, StorageValue(value)));
        }

        [[nodiscard]] T* CompareExchange(T* const exchange, T* const comparand) noexcept
        {
            return static_cast<T*>(Operations::CompareExchange(&m_target, StorageValue(exchange), StorageValue(comparand)));
        }

        void SetValue(T* const value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] T* GetValue() const noexcept
        {
            return static_cast<T*>(Operations::FetchValue(&m_target));
        }

    private:
        alignas(sizeof(void*)) mutable void* volatile m_target;
    };

    template <> class Atomic<bool>
    {
        using Operations = ::red::WinAPI::AtomicOps32;

    public:
        explicit Atomic(const bool value = false) noexcept : m_target(value ? 1L : 0L) {}

        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        [[nodiscard]] bool Exchange(const bool value) noexcept
        {
            return Operations::Exchange(&m_target, value ? 1L : 0L) != 0;
        }

        [[nodiscard]] bool CompareExchange(const bool exchange, const bool comparand) noexcept
        {
            return Operations::CompareExchange(&m_target, exchange ? 1L : 0L, comparand ? 1L : 0L) != 0;
        }

        void SetValue(const bool value) noexcept
        {
            static_cast<void>(Exchange(value));
        }

        [[nodiscard]] bool GetValue() const noexcept
        {
            return Operations::FetchValue(&m_target) != 0;
        }

    private:
        alignas(4) mutable volatile long m_target;
    };

    static_assert(sizeof(Atomic<u32>) == 4 && alignof(Atomic<u32>) == 4);
    static_assert(sizeof(Atomic<u64>) == 8 && alignof(Atomic<u64>) == 8);
    static_assert(sizeof(Atomic<bool>) == 4 && alignof(Atomic<bool>) == 4);
    static_assert(sizeof(Atomic<void*>) == 8 && alignof(Atomic<void*>) == 8);
} // namespace vanguard::concurrency
