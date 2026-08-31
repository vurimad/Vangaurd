#pragma once

#include <vanguard/memory/memory.hpp>

namespace vanguard::memory::backend
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] bool IsValidPool(PoolId pool) noexcept;
    [[nodiscard]] bool IsAllocatablePool(PoolId pool) noexcept;
    [[nodiscard]] PoolId GetParentPool(PoolId pool) noexcept;
    [[nodiscard]] const char* GetPoolName(PoolId pool) noexcept;
    [[nodiscard]] u32 GetPoolHandleValue(PoolId pool) noexcept;
    [[nodiscard]] MemoryBlock Allocate(PoolId pool, usize size, usize alignment) noexcept;
    [[nodiscard]] bool Reallocate(MemoryBlock& block, usize newSize, usize alignment) noexcept;
    void Free(MemoryBlock& block) noexcept;
    [[nodiscard]] bool SetPoolBudget(PoolId pool, u64 budgetBytes) noexcept;
    [[nodiscard]] u64 GetPoolBudget(PoolId pool) noexcept;
    [[nodiscard]] bool GetPoolMetrics(PoolId pool, PoolMetrics& metrics) noexcept;
    void VisitPools(PoolVisitor visitor, void* userData) noexcept;
    void ResetFramePools() noexcept;
    void PrepareMetricsForNextFrame() noexcept;
} // namespace vanguard::memory::backend
