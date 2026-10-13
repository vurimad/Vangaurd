#pragma once

#include <vanguard/memory/memory.hpp>

namespace vanguard::vellum::detail
{
    inline constexpr usize MaximumRuntimeBlockBytes = 0xffffffffull;

    [[nodiscard]] constexpr bool IsPowerOfTwo(const usize value) noexcept
    {
        return value != 0 && (value & (value - 1u)) == 0;
    }

    [[nodiscard]] constexpr bool CheckedAdd(const usize left, const usize right, usize& result) noexcept
    {
        constexpr usize maximum = ~usize{0};
        if (right > maximum - left)
            return false;
        result = left + right;
        return true;
    }

    [[nodiscard]] constexpr bool CheckedMultiply(const usize left, const usize right, usize& result) noexcept
    {
        constexpr usize maximum = ~usize{0};
        if (left != 0 && right > maximum / left)
            return false;
        result = left * right;
        return true;
    }

    [[nodiscard]] constexpr bool CheckedAlignUp(const usize value, const usize alignment, usize& result) noexcept
    {
        if (!IsPowerOfTwo(alignment))
            return false;
        usize biased = 0;
        if (!CheckedAdd(value, alignment - 1u, biased))
            return false;
        result = biased & ~(alignment - 1u);
        return result >= value;
    }

    struct BlockLayout
    {
        usize size = 0;
        usize alignment = 1;

        template <typename Type> [[nodiscard]] bool AddArray(const u32 count, usize& offset) noexcept
        {
            usize aligned = 0;
            if (!CheckedAlignUp(size, alignof(Type), aligned))
                return false;
            usize bytes = 0;
            if (!CheckedMultiply(static_cast<usize>(count), sizeof(Type), bytes))
                return false;
            usize end = 0;
            if (!CheckedAdd(aligned, bytes, end) || end > MaximumRuntimeBlockBytes)
                return false;
            offset = aligned;
            size = end;
            if (alignof(Type) > alignment)
                alignment = alignof(Type);
            return true;
        }

        [[nodiscard]] bool Finish() noexcept
        {
            usize aligned = 0;
            if (!CheckedAlignUp(size, alignment, aligned) || aligned > MaximumRuntimeBlockBytes)
                return false;
            size = aligned;
            return size != 0;
        }
    };

    [[nodiscard]] inline bool AllocateRuntimeBlock(const BlockLayout& layout, memory::MemoryBlock& block) noexcept
    {
        if (layout.size == 0 || layout.size > MaximumRuntimeBlockBytes || !IsPowerOfTwo(layout.alignment))
            return false;
        block = memory::Allocate(memory::PoolId::Runtime, layout.size, layout.alignment);
        return static_cast<bool>(block);
    }

    template <typename Type> [[nodiscard]] Type* BindArray(memory::MemoryBlock& block, const usize offset) noexcept
    {
        auto* const bytes = static_cast<u8*>(block.address);
        return reinterpret_cast<Type*>(bytes + offset);
    }

    template <typename Type> struct FixedRing
    {
        Type* values = nullptr;
        u32 capacity = 0;
        u32 head = 0;
        u32 count = 0;

        [[nodiscard]] bool Empty() const noexcept { return count == 0; }
        [[nodiscard]] bool Full() const noexcept { return count == capacity; }

        [[nodiscard]] Type* Front() noexcept
        {
            return count != 0 ? &values[head] : nullptr;
        }

        [[nodiscard]] const Type* Front() const noexcept
        {
            return count != 0 ? &values[head] : nullptr;
        }

        [[nodiscard]] bool Push(const Type& value) noexcept
        {
            if (Full() || values == nullptr || capacity == 0)
                return false;
            const u32 tailDistance = capacity - count;
            const u32 index = head >= tailDistance ? head - tailDistance : head + count;
            values[index] = value;
            ++count;
            return true;
        }

        void Pop() noexcept
        {
            if (count == 0)
                return;
            head = (head + 1u) % capacity;
            --count;
        }
    };
} // namespace vanguard::vellum::detail
