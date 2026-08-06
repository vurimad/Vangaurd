#include "build.h"

#include <vanguard/memory/memory.hpp>

#include "memory_backend.hpp"

namespace vanguard::memory
{
    bool Initialize() noexcept
    {
        return backend::Initialize();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    bool IsValidPool(const PoolId pool) noexcept
    {
        return backend::IsValidPool(pool);
    }

    bool IsAllocatablePool(const PoolId pool) noexcept
    {
        return backend::IsAllocatablePool(pool);
    }

    PoolId ParentPool(const PoolId pool) noexcept
    {
        return backend::ParentPool(pool);
    }

    const char* PoolName(const PoolId pool) noexcept
    {
        return backend::PoolName(pool);
    }

    MemoryBlock Allocate(const usize size, const usize alignment) noexcept
    {
        return backend::Allocate(PoolId::Engine, size, alignment);
    }

    MemoryBlock Allocate(const PoolId pool, const usize size, const usize alignment) noexcept
    {
        return backend::Allocate(pool, size, alignment);
    }

    bool Reallocate(MemoryBlock& block, const usize newSize, const usize alignment) noexcept
    {
        return backend::Reallocate(block, newSize, alignment);
    }

    void Free(MemoryBlock& block) noexcept
    {
        backend::Free(block);
    }

    bool SetPoolBudget(const PoolId pool, const u64 budgetBytes) noexcept
    {
        return backend::SetPoolBudget(pool, budgetBytes);
    }

    u64 GetPoolBudget(const PoolId pool) noexcept
    {
        return backend::GetPoolBudget(pool);
    }

    bool GetPoolMetrics(const PoolId pool, PoolMetrics& metrics) noexcept
    {
        return backend::GetPoolMetrics(pool, metrics);
    }

    bool GetPoolSnapshot(const PoolId pool, PoolSnapshot& snapshot) noexcept
    {
        if (!IsValidPool(pool))
        {
            snapshot = {};
            return false;
        }

        snapshot.id = pool;
        snapshot.parent = ParentPool(pool);
        snapshot.handle = backend::PoolHandleValue(pool);
        snapshot.parentHandle = backend::PoolHandleValue(snapshot.parent);
        snapshot.name = PoolName(pool);
        snapshot.allocatable = IsAllocatablePool(pool);
        snapshot.vanguardOwned = true;
        return GetPoolMetrics(pool, snapshot.metrics);
    }

    void VisitPools(const PoolVisitor visitor, void* const userData) noexcept
    {
        backend::VisitPools(visitor, userData);
    }

    void ResetFramePools() noexcept
    {
        backend::ResetFramePools();
    }

    void PrepareMetricsForNextFrame() noexcept
    {
        backend::PrepareMetricsForNextFrame();
    }
} // namespace vanguard::memory
