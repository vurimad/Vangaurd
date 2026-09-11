#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/system/types.hpp>

namespace vanguard::rendering
{
    // Graph-array wrapper. The usage array is
    // intentionally separate from item storage: graph composition depends on
    // newly allocated items occupying the tail of usage order even when a freed
    // storage slot is reused.
    template <typename T, u32 IdTypeMask>
    class RenderNodeGraphArray
    {
        struct DataWrapper
        {
            i32 usageIndex = -1;
            T data;
        };

    public:
        [[nodiscard]] static constexpr bool IsValidId(const u32 id) noexcept
        {
            return (id & IdTypeMask) != 0;
        }

        [[nodiscard]] static u32 IdToIndex(const u32 id) noexcept
        {
            VG_ASSERT(IsValidId(id));
            return id & ~IdTypeMask;
        }

        [[nodiscard]] static u32 IndexToId(const u32 index) noexcept
        {
            const u32 id = index | IdTypeMask;
            VG_ASSERT(id != index);
            return id;
        }

        RenderNodeGraphArray(const u32 capacityStart, const u32 capacityGrow)
            : m_capacityStart(capacityStart), m_capacityGrow(capacityGrow), m_items(memory::pools::Rendering::GetInstance()), m_usage(memory::pools::Rendering::GetInstance())
        {
            m_items.Reserve(capacityStart);
            m_usage.Reserve(capacityStart);
        }

        [[nodiscard]] bool IsAllocated(const u32 id) const noexcept
        {
            const u32 index = IdToIndex(id);
            return index < m_items.Size() && m_items[index].usageIndex != -1;
        }

        // The allocated item is appended to usage order. Existing usage order
        // is retained even when allocation recycles a free storage slot.
        [[nodiscard]] u32 Allocate()
        {
            u32 resultItemIndex = 0xffffffffu;

            if (m_numUsed < m_usage.Size())
            {
                const u32 newItemIndex = m_usage[m_numUsed];
                const u32 newUsageIndex = m_numUsed;
                VG_ASSERT(m_items[newItemIndex].usageIndex == -1);
                m_items[newItemIndex].usageIndex = static_cast<i32>(newUsageIndex);
                ++m_numUsed;
                resultItemIndex = newItemIndex;
            }
            else
            {
                if (m_items.Size() == m_items.Capacity())
                    m_items.Reserve(m_items.Size() + m_capacityGrow);
                if (m_usage.Size() == m_usage.Capacity())
                    m_usage.Reserve(m_usage.Size() + m_capacityGrow);

                VG_ASSERT(m_usage.Size() == m_items.Size());
                const u32 newItemIndex = m_items.Size();
                const i32 newUsageIndex = static_cast<i32>(newItemIndex);
                m_items.Grow(1);
                m_items[newItemIndex].usageIndex = newUsageIndex;
                m_usage.PushBack(newItemIndex);
                ++m_numUsed;
                resultItemIndex = newItemIndex;
            }

            VG_ASSERT(resultItemIndex < m_items.Size());
            VG_ASSERT(m_items[resultItemIndex].usageIndex != -1);
            VG_ASSERT(m_usage[m_items[resultItemIndex].usageIndex] == resultItemIndex);
            return IndexToId(resultItemIndex);
        }

        void Allocate(const u32 numItems)
        {
            for (u32 index = 0; index < numItems; ++index)
                static_cast<void>(Allocate());
        }

        void Free(const u32 id)
        {
            VG_ASSERT(IsAllocated(id));

            const u32 itemToFreeIndex = IdToIndex(id);
            const u32 itemAtBackIndex = m_usage[m_numUsed - 1];
            DataWrapper& itemToFree = m_items[itemToFreeIndex];
            DataWrapper& itemAtBack = m_items[itemAtBackIndex];
            VG_ASSERT(itemToFree.usageIndex < static_cast<i32>(m_numUsed));
            VG_ASSERT(itemAtBack.usageIndex < static_cast<i32>(m_numUsed));
            VG_ASSERT(m_usage[itemToFree.usageIndex] == itemToFreeIndex);
            VG_ASSERT(m_usage[itemAtBack.usageIndex] == itemAtBackIndex);

            const u32 usageItem = m_usage[itemToFree.usageIndex];
            m_usage[itemToFree.usageIndex] = m_usage[itemAtBack.usageIndex];
            m_usage[itemAtBack.usageIndex] = usageItem;
            const i32 usageIndex = itemToFree.usageIndex;
            itemToFree.usageIndex = itemAtBack.usageIndex;
            itemAtBack.usageIndex = usageIndex;

            itemToFree.usageIndex = -1;
            --m_numUsed;
            VG_ASSERT(!IsAllocated(id));
        }

        [[nodiscard]] u32 GetReindexedItemId(const RenderNodeGraphArray& importGraph, const u32 importItemId) const noexcept
        {
            VG_ASSERT(this != &importGraph);
            VG_ASSERT(importGraph.IsAllocated(importItemId));
            VG_ASSERT(m_numUsed >= importGraph.m_numUsed);

            const u32 usageOffset = m_numUsed - importGraph.m_numUsed;
            const u32 importItemIndex = importGraph.IdToIndex(importItemId);
            const u32 importUsageIndex = static_cast<u32>(importGraph.m_items[importItemIndex].usageIndex);
            VG_ASSERT(importGraph.m_usage[importUsageIndex] == importItemIndex);

            const u32 thisUsageIndex = usageOffset + importUsageIndex;
            VG_ASSERT(thisUsageIndex < m_numUsed);
            const u32 thisItemIndex = m_usage[thisUsageIndex];
            VG_ASSERT(m_items[thisItemIndex].usageIndex == static_cast<i32>(thisUsageIndex));
            VG_ASSERT(usageOffset != 0 || importItemIndex == thisItemIndex);
            return IndexToId(thisItemIndex);
        }

        [[nodiscard]] u32 GetNumItems() const noexcept
        {
            return m_numUsed;
        }

        [[nodiscard]] u32 GetItemIdByUsageIndex(const u32 usageIndex) const noexcept
        {
            VG_ASSERT(usageIndex < m_numUsed);
            VG_ASSERT(m_items[m_usage[usageIndex]].usageIndex == static_cast<i32>(usageIndex));
            const u32 id = IndexToId(m_usage[usageIndex]);
            VG_ASSERT(usageIndex == GetItemUsageIndex(id));
            return id;
        }

        [[nodiscard]] u32 GetItemUsageIndex(const u32 id) const noexcept
        {
            VG_ASSERT(IsAllocated(id));
            const i32 usageIndex = m_items[IdToIndex(id)].usageIndex;
            VG_ASSERT(usageIndex >= 0);
            VG_ASSERT(usageIndex < static_cast<i32>(m_numUsed));
            VG_ASSERT(IdToIndex(id) == m_usage[usageIndex]);
            return static_cast<u32>(usageIndex);
        }

        [[nodiscard]] T& operator[](const u32 id) noexcept
        {
            VG_ASSERT(IsAllocated(id));
            return m_items[IdToIndex(id)].data;
        }

        [[nodiscard]] const T& operator[](const u32 id) const noexcept
        {
            VG_ASSERT(IsAllocated(id));
            return m_items[IdToIndex(id)].data;
        }

        void Reset() noexcept
        {
            m_numUsed = 0;
            m_items.Clear();
            m_usage.Clear();
        }

    private:
        const u32 m_capacityStart;
        const u32 m_capacityGrow;
        u32 m_numUsed = 0;
        containers::DynamicArray<DataWrapper> m_items;
        containers::DynamicArray<u32> m_usage;
    };
} // namespace vanguard::rendering
