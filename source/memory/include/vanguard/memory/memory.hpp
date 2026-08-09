#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::memory
{
    enum class PoolId : u16
    {
        Root = 0,
        Cpu,
        Gpu,

        Engine,
        Backend,
        Debug,
        RefCount,
        Frame,
        DoubleBufferedFrame,

        Runtime,
        Editor,
        Tools,

        Diagnostics,
        Containers,
        Concurrency,
        Jobs,
        Io,
        Filesystem,
        Serialization,
        Reflection,
        Resources,
        Assets,
        World,
        Streaming,
        Rendering,
        Physics,
        Animation,
        Audio,
        Input,
        Window,
        Navigation,
        Networking,
        Gameplay,

        Count
    };

    struct MemoryBlock
    {
        void* address = nullptr;
        usize size = 0;
        PoolId pool = PoolId::Engine;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return address != nullptr;
        }
    };

    struct PoolMetrics
    {
        u64 budgetBytes = 0;
        u64 allocatedBytes = 0;
        u64 peakAllocatedBytes = 0;
        u64 allocatedBytesIncludingChildren = 0;
        i64 allocatedBytesThisFrame = 0;
        i64 allocatedBytesPreviousFrame = 0;
        i64 freedBytesThisFrame = 0;
        i64 freedBytesPreviousFrame = 0;
        u32 allocationCount = 0;
        u32 allocationsThisFrame = 0;
        u32 allocationsPreviousFrame = 0;
        bool extendedMetricsAvailable = false;
    };

    struct PoolSnapshot
    {
        PoolId id = PoolId::Engine;
        PoolId parent = PoolId::Root;
        u32 handle = 0;
        u32 parentHandle = 0;
        const char* name = "";
        bool allocatable = false;
        bool vanguardOwned = true;
        PoolMetrics metrics;
    };

    using PoolVisitor = void (*)(const PoolSnapshot& pool, void* userData) noexcept;

    // Root memory initialization is a startup operation and must be serialized
    // by the application composition root.
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    [[nodiscard]] bool IsValidPool(PoolId pool) noexcept;
    [[nodiscard]] bool IsAllocatablePool(PoolId pool) noexcept;
    [[nodiscard]] PoolId ParentPool(PoolId pool) noexcept;
    [[nodiscard]] const char* PoolName(PoolId pool) noexcept;

    // Requests larger than the active backend's 32-bit allocation contract
    // fail without modifying allocator state.
    [[nodiscard]] MemoryBlock Allocate(usize size, usize alignment = 16) noexcept;

    [[nodiscard]] MemoryBlock Allocate(PoolId pool, usize size, usize alignment = 16) noexcept;

    [[nodiscard]] bool Reallocate(MemoryBlock& block, usize newSize, usize alignment = 16) noexcept;

    void Free(MemoryBlock& block) noexcept;

    [[nodiscard]] bool SetPoolBudget(PoolId pool, u64 budgetBytes) noexcept;
    [[nodiscard]] u64 GetPoolBudget(PoolId pool) noexcept;
    [[nodiscard]] bool GetPoolMetrics(PoolId pool, PoolMetrics& metrics) noexcept;
    [[nodiscard]] bool GetPoolSnapshot(PoolId pool, PoolSnapshot& snapshot) noexcept;
    void VisitPools(PoolVisitor visitor, void* userData = nullptr) noexcept;

    // Frame-pool lifecycle is explicit. The composition root calls these
    // once at the corresponding frame boundary after all users are finished.
    void ResetFramePools() noexcept;
    void PrepareMetricsForNextFrame() noexcept;
} // namespace vanguard::memory
