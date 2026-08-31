#include "build.h"

#include <vanguard/memory/memory_backend.hpp>
#include <vanguard/memory/pool.hpp>

#include "../../imported/common/redMemory/include/defaultAllocator.h"
#include "../../imported/common/redMemory/include/metricsUtils.h"
#include "../../imported/common/redMemory/include/poolUtils.h"
#include "../../imported/common/redMemory/src/poolStorage.h"

#include <limits>

#define VG_MEMORY_CATEGORY_POOLS(X)                                                                                                                            \
    X(Runtime)                                                                                                                                                 \
    X(Editor)                                                                                                                                                  \
    X(Tools)                                                                                                                                                   \
    X(Diagnostics)                                                                                                                                             \
    X(Containers)                                                                                                                                              \
    X(Concurrency)                                                                                                                                             \
    X(Jobs)                                                                                                                                                    \
    X(Io)                                                                                                                                                      \
    X(Filesystem)                                                                                                                                              \
    X(Serialization)                                                                                                                                           \
    X(Reflection)                                                                                                                                              \
    X(Resources)                                                                                                                                               \
    X(Assets)                                                                                                                                                  \
    X(World)                                                                                                                                                   \
    X(Streaming)                                                                                                                                               \
    X(Rendering)                                                                                                                                               \
    X(Physics)                                                                                                                                                 \
    X(Animation)                                                                                                                                               \
    X(Audio)                                                                                                                                                   \
    X(Input)                                                                                                                                                   \
    X(Window)                                                                                                                                                  \
    X(Navigation)                                                                                                                                              \
    X(Networking)                                                                                                                                              \
    X(Gameplay)

#define VG_DEFINE_CATEGORY_POOL(name) RED_MEMORY_DEFINE_POOL_STORAGE(vanguard::memory::pools::name, RED_MEMORY_API);

VG_MEMORY_CATEGORY_POOLS(VG_DEFINE_CATEGORY_POOL)

#undef VG_DEFINE_CATEGORY_POOL

namespace
{
    using vanguard::memory::PoolId;

    bool g_vanguardMemoryInitialized = false;

    template <typename Pool, typename Parent> void InitializeCategoryPool(const char* const name)
    {
        red::memory::PoolParameter parameter = {name, &red::memory::StaticPoolStorage<Pool>::storage, 0, Parent::GetHandle()};
        red::memory::InitializePool<Pool>(parameter, red::memory::AcquireDefaultAllocator());
    }

    const red::memory::Pool* ResolvePool(const PoolId pool) noexcept
    {
        using namespace vanguard::memory::pools;

        switch (pool)
        {
        case PoolId::Root:
            return &red::memory::PoolRoot::GetInstance();
        case PoolId::Cpu:
            return &red::memory::PoolCPU::GetInstance();
        case PoolId::Gpu:
            return &red::memory::PoolGPU::GetInstance();
        case PoolId::Engine:
            return &red::PoolEngine::GetInstance();
        case PoolId::Backend:
            return &red::PoolBackend::GetInstance();
        case PoolId::Debug:
            return &red::PoolDebug::GetInstance();
        case PoolId::RefCount:
            return &red::PoolRefCount::GetInstance();
        case PoolId::Frame:
            return &red::PoolFrame::GetInstance();
        case PoolId::DoubleBufferedFrame:
            return &red::PoolDoubleBufferedFrame::GetInstance();
#define VG_RESOLVE_CATEGORY_POOL(name)                                                                                                                         \
    case PoolId::name:                                                                                                                                         \
        return &name::GetInstance();

            VG_MEMORY_CATEGORY_POOLS(VG_RESOLVE_CATEGORY_POOL)

#undef VG_RESOLVE_CATEGORY_POOL
        case PoolId::Count:
            break;
        }
        return nullptr;
    }

    bool IsValidSizeAndAlignment(const vanguard::usize size, const vanguard::usize alignment) noexcept
    {
        return size != 0 && size <= std::numeric_limits<red::memory::u32>::max() && alignment != 0 &&
               alignment <= std::numeric_limits<red::memory::u32>::max() && (alignment & (alignment - 1)) == 0;
    }

    PoolId ResolvePoolId(const red::memory::PoolHandle handle) noexcept
    {
        for (vanguard::u16 value = 0; value < static_cast<vanguard::u16>(PoolId::Count); ++value)
        {
            const PoolId candidate = static_cast<PoolId>(value);
            const red::memory::Pool* const pool = ResolvePool(candidate);
            if (pool != nullptr && pool->GetHandle() == handle)
            {
                return candidate;
            }
        }
        return PoolId::Count;
    }

    void FillPoolMetrics(const red::memory::PoolInfo& info, vanguard::memory::PoolMetrics& metrics) noexcept
    {
        metrics = {};
        metrics.budgetBytes = info.budget;
        metrics.allocatedBytesIncludingChildren = static_cast<vanguard::u64>(red::memory::GetTotalBytesAllocated(info.handle, true));
        if (info.storage != nullptr)
        {
            metrics.allocatedBytes = static_cast<vanguard::u64>(info.storage->bytesAllocated);
            metrics.peakAllocatedBytes = static_cast<vanguard::u64>(info.storage->maxBytesAllocated);
        }

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
        red::memory::RuntimePoolMetrics importedMetrics{};
        red::memory::GetRuntimePoolMetrics(info.handle, importedMetrics);
        metrics.allocatedBytesThisFrame = importedMetrics.bytesAllocatedPerFrame;
        metrics.allocatedBytesPreviousFrame = importedMetrics.bytesAllocatedPerFramePrevious;
        metrics.freedBytesThisFrame = importedMetrics.bytesDeallocatedPerFrame;
        metrics.freedBytesPreviousFrame = importedMetrics.bytesDeallocatedPerFramePrevious;
        metrics.allocationCount = static_cast<vanguard::u32>(importedMetrics.allocationCount);
        metrics.allocationsThisFrame = static_cast<vanguard::u32>(importedMetrics.allocationPerFrameCount);
        metrics.allocationsPreviousFrame = static_cast<vanguard::u32>(importedMetrics.allocationPerFrameCountPrevious);
        metrics.extendedMetricsAvailable = true;
#endif
    }
} // namespace

namespace vanguard::memory::backend
{
    bool Initialize() noexcept
    {
        if (g_vanguardMemoryInitialized)
        {
            return true;
        }

        red::memory::InitializeRootPools();

        using namespace pools;

        InitializeCategoryPool<Runtime, red::PoolEngine>("Runtime");
        InitializeCategoryPool<Editor, red::PoolBackend>("Editor");
        InitializeCategoryPool<Tools, red::PoolBackend>("Tools");

        InitializeCategoryPool<Diagnostics, red::PoolEngine>("Diagnostics");
        InitializeCategoryPool<Containers, red::PoolEngine>("Containers");
        InitializeCategoryPool<Concurrency, red::PoolEngine>("Concurrency");
        InitializeCategoryPool<Jobs, red::PoolEngine>("Jobs");
        InitializeCategoryPool<Io, red::PoolEngine>("IO");
        InitializeCategoryPool<Filesystem, red::PoolEngine>("Filesystem");
        InitializeCategoryPool<Serialization, red::PoolEngine>("Serialization");
        InitializeCategoryPool<Reflection, red::PoolEngine>("Reflection");

        InitializeCategoryPool<Resources, Runtime>("Resources");
        InitializeCategoryPool<World, Runtime>("World");
        InitializeCategoryPool<Streaming, Runtime>("Streaming");
        InitializeCategoryPool<Rendering, Runtime>("Rendering");
        InitializeCategoryPool<Physics, Runtime>("Physics");
        InitializeCategoryPool<Animation, Runtime>("Animation");
        InitializeCategoryPool<Audio, Runtime>("Audio");
        InitializeCategoryPool<Input, Runtime>("Input");
        InitializeCategoryPool<Window, Runtime>("Window");
        InitializeCategoryPool<Navigation, Runtime>("Navigation");
        InitializeCategoryPool<Networking, Runtime>("Networking");
        InitializeCategoryPool<Gameplay, Runtime>("Gameplay");

        InitializeCategoryPool<Assets, Tools>("Assets");

        g_vanguardMemoryInitialized = true;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_vanguardMemoryInitialized;
    }

    bool IsValidPool(const PoolId pool) noexcept
    {
        return static_cast<u16>(pool) < static_cast<u16>(PoolId::Count);
    }

    bool IsAllocatablePool(const PoolId pool) noexcept
    {
        return vanguard::memory::backend::IsValidPool(pool) && pool != PoolId::Root && pool != PoolId::Cpu && pool != PoolId::Gpu;
    }

    PoolId GetParentPool(const PoolId pool) noexcept
    {
        switch (pool)
        {
        case PoolId::Root:
            return PoolId::Root;
        case PoolId::Cpu:
        case PoolId::Gpu:
        case PoolId::Backend:
        case PoolId::Debug:
            return PoolId::Root;
        case PoolId::Engine:
            return PoolId::Cpu;
        case PoolId::RefCount:
            return PoolId::Engine;
        case PoolId::Frame:
        case PoolId::DoubleBufferedFrame:
            return PoolId::Cpu;
        case PoolId::Runtime:
            return PoolId::Engine;
        case PoolId::Editor:
        case PoolId::Tools:
            return PoolId::Backend;
        case PoolId::Resources:
        case PoolId::World:
        case PoolId::Streaming:
        case PoolId::Rendering:
        case PoolId::Physics:
        case PoolId::Animation:
        case PoolId::Audio:
        case PoolId::Input:
        case PoolId::Window:
        case PoolId::Navigation:
        case PoolId::Networking:
        case PoolId::Gameplay:
            return PoolId::Runtime;
        case PoolId::Assets:
            return PoolId::Tools;
        case PoolId::Diagnostics:
        case PoolId::Containers:
        case PoolId::Concurrency:
        case PoolId::Jobs:
        case PoolId::Io:
        case PoolId::Filesystem:
        case PoolId::Serialization:
        case PoolId::Reflection:
            return PoolId::Engine;
        case PoolId::Count:
            break;
        }
        return PoolId::Root;
    }

    const char* GetPoolName(const PoolId pool) noexcept
    {
        switch (pool)
        {
        case PoolId::Root:
            return "Root";
        case PoolId::Cpu:
            return "CPU";
        case PoolId::Gpu:
            return "GPU";
        case PoolId::Engine:
            return "Engine";
        case PoolId::Backend:
            return "Backend";
        case PoolId::Debug:
            return "Debug";
        case PoolId::RefCount:
            return "RefCount";
        case PoolId::Frame:
            return "Frame";
        case PoolId::DoubleBufferedFrame:
            return "DoubleBufferedFrame";
#define VG_CATEGORY_POOL_NAME(name)                                                                                                                            \
    case PoolId::name:                                                                                                                                         \
        return #name;

            VG_MEMORY_CATEGORY_POOLS(VG_CATEGORY_POOL_NAME)

#undef VG_CATEGORY_POOL_NAME
        case PoolId::Count:
            break;
        }
        return "Invalid";
    }

    u32 GetPoolHandleValue(const PoolId pool) noexcept
    {
        const red::memory::Pool* const importedPool = ResolvePool(pool);
        return importedPool != nullptr ? static_cast<u32>(importedPool->GetHandle()) : 0;
    }

    MemoryBlock Allocate(const PoolId pool, const usize size, const usize alignment) noexcept
    {
        const red::memory::Pool* const importedPool = ResolvePool(pool);
        if (!g_vanguardMemoryInitialized || !vanguard::memory::backend::IsAllocatablePool(pool) || importedPool == nullptr ||
            !IsValidSizeAndAlignment(size, alignment))
        {
            return {};
        }

        const red::memory::Block importedBlock = importedPool->AllocateAligned(static_cast<red::memory::u32>(size), static_cast<red::memory::u32>(alignment));

        return {reinterpret_cast<void*>(importedBlock.address), static_cast<usize>(importedBlock.size), pool};
    }

    bool Reallocate(MemoryBlock& block, const usize newSize, const usize alignment) noexcept
    {
        const red::memory::Pool* const importedPool = ResolvePool(block.pool);
        if (!g_vanguardMemoryInitialized || !block || !vanguard::memory::backend::IsAllocatablePool(block.pool) || importedPool == nullptr ||
            !IsValidSizeAndAlignment(newSize, alignment))
        {
            return false;
        }

        red::memory::Block importedBlock = {reinterpret_cast<red::memory::u64>(block.address), static_cast<red::memory::u64>(block.size)};

        const red::memory::Block reallocated =
            importedPool->ReallocateAligned(importedBlock, static_cast<red::memory::u32>(newSize), static_cast<red::memory::u32>(alignment));
        if (reallocated.address == 0)
        {
            return false;
        }

        block.address = reinterpret_cast<void*>(reallocated.address);
        block.size = static_cast<usize>(reallocated.size);
        return true;
    }

    void Free(MemoryBlock& block) noexcept
    {
        if (!block)
        {
            return;
        }

        const red::memory::Pool* const importedPool = ResolvePool(block.pool);
        RED_FATAL_ASSERT(g_vanguardMemoryInitialized && vanguard::memory::backend::IsAllocatablePool(block.pool) && importedPool != nullptr,
                         "Vanguard memory block contains an invalid originating pool.");

        red::memory::Block importedBlock = {reinterpret_cast<red::memory::u64>(block.address), static_cast<red::memory::u64>(block.size)};

        importedPool->Free(importedBlock);
        block = {};
    }

    bool SetPoolBudget(const PoolId pool, const u64 budgetBytes) noexcept
    {
        const red::memory::Pool* const importedPool = ResolvePool(pool);
        if (!g_vanguardMemoryInitialized || !vanguard::memory::backend::IsValidPool(pool) || importedPool == nullptr)
        {
            return false;
        }

        red::memory::SetPoolBudget(importedPool->GetHandle(), vanguard::memory::backend::GetPoolName(pool), budgetBytes);
        return true;
    }

    u64 GetPoolBudget(const PoolId pool) noexcept
    {
        const red::memory::Pool* const importedPool = ResolvePool(pool);
        if (!g_vanguardMemoryInitialized || !vanguard::memory::backend::IsValidPool(pool) || importedPool == nullptr)
        {
            return 0;
        }
        return red::memory::GetPoolBudget(importedPool->GetHandle());
    }

    bool GetPoolMetrics(const PoolId pool, PoolMetrics& metrics) noexcept
    {
        metrics = {};

        const red::memory::Pool* const importedPool = ResolvePool(pool);
        if (!g_vanguardMemoryInitialized || !vanguard::memory::backend::IsValidPool(pool) || importedPool == nullptr)
        {
            return false;
        }

        const red::memory::PoolHandle handle = importedPool->GetHandle();
        bool found = false;
        red::memory::VisitPoolInfos(
            [&metrics, handle, &found](const red::memory::PoolInfo* const info, const red::memory::PoolInfo*)
            {
                if (info->handle == handle)
                {
                    FillPoolMetrics(*info, metrics);
                    found = true;
                }
            });
        return found;
    }

    void VisitPools(const PoolVisitor visitor, void* const userData) noexcept
    {
        if (!g_vanguardMemoryInitialized || visitor == nullptr)
        {
            return;
        }

        red::memory::VisitPoolInfos(
            [visitor, userData](const red::memory::PoolInfo* const info, const red::memory::PoolInfo* const parent)
            {
                PoolSnapshot snapshot;
                snapshot.id = ResolvePoolId(info->handle);
                snapshot.parent = parent != nullptr ? ResolvePoolId(parent->handle) : PoolId::Root;
                snapshot.handle = static_cast<u32>(info->handle);
                snapshot.parentHandle = parent != nullptr ? static_cast<u32>(parent->handle) : 0;
                snapshot.name = info->name;
                snapshot.vanguardOwned = snapshot.id != PoolId::Count;
                snapshot.allocatable = snapshot.vanguardOwned && vanguard::memory::backend::IsAllocatablePool(snapshot.id);
                FillPoolMetrics(*info, snapshot.metrics);
                visitor(snapshot, userData);
            });
    }

    void ResetFramePools() noexcept
    {
        if (g_vanguardMemoryInitialized)
        {
            red::memory::ResetFrameAllocators();
        }
    }

    void PrepareMetricsForNextFrame() noexcept
    {
        if (g_vanguardMemoryInitialized)
        {
            red::memory::PrepareMetricsForNextFrame();
        }
    }
} // namespace vanguard::memory::backend

#undef VG_MEMORY_CATEGORY_POOLS
