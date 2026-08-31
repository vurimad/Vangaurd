#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    VANGUARD_MEMORY_POOL_STATIC(PoolObjectTests, vanguard::memory::DefaultAllocator);
    VANGUARD_MEMORY_POOL_STATIC(PoolPolymorphicTests, vanguard::memory::DefaultAllocator);

    struct PooledObject
    {
        VANGUARD_USE_MEMORY_POOL(PoolObjectTests);

    public:
        explicit PooledObject(const vanguard::u32 initialValue = 0xC0FFEEu) : value(initialValue) {}

        ~PooledObject()
        {
            ++destructionCount;
        }

        vanguard::u32 value;
        inline static vanguard::u32 destructionCount = 0;
    };

    struct PolymorphicObject
    {
        VANGUARD_USE_POLYMORPHIC_MEMORY_POOL(PoolObjectTests);

    public:
        virtual ~PolymorphicObject()
        {
            ++destructionCount;
        }

        inline static vanguard::u32 destructionCount = 0;
    };

    struct SpecializedPolymorphicObject final : PolymorphicObject
    {
        VANGUARD_USE_MEMORY_POOL(PoolPolymorphicTests);
    };

    int failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAILED: %s\n", message);
            ++failures;
        }
    }

    struct VisitState
    {
        vanguard::u32 count = 0;
        bool known[static_cast<vanguard::u16>(vanguard::memory::PoolId::Count)]{};
        bool sawCompatibilityPool = false;
    };

    void CapturePool(const vanguard::memory::PoolSnapshot& pool, void* const userData) noexcept
    {
        auto* const state = static_cast<VisitState*>(userData);
        ++state->count;
        if (pool.vanguardOwned)
        {
            state->known[static_cast<vanguard::u16>(pool.id)] = true;
        }
        else
        {
            state->sawCompatibilityPool = true;
        }
    }
} // namespace

int main()
{
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initialization");
    Check(memory::IsInitialized(), "memory initialized state");

    VANGUARD_INITIALIZE_MEMORY_POOL(PoolObjectTests, memory::pools::Engine, memory::AcquireDefaultAllocator(), 16ull * 1024ull * 1024ull);
    VANGUARD_INITIALIZE_MEMORY_POOL(PoolPolymorphicTests, memory::pools::Engine, memory::AcquireDefaultAllocator(), 16ull * 1024ull * 1024ull);

    PooledObject::destructionCount = 0;
    PooledObject* const pooledObject = VANGUARD_NEW(PooledObject)(42);
    Check(pooledObject != nullptr, "pool-resolved typed construction");
    Check(pooledObject->value == 42, "pool-resolved constructor arguments");
    Check(pooledObject->GetMemoryPool().GetHandle() == PoolObjectTests::GetHandle(), "class memory-pool binding");
    VANGUARD_DELETE(pooledObject);
    Check(PooledObject::destructionCount == 1, "pool-resolved typed destruction");

    PooledObject* const pooledArray = VANGUARD_NEW_ARRAY(PooledObject, 8);
    Check(pooledArray != nullptr, "pool-resolved array construction");
    VANGUARD_DELETE_ARRAY(pooledArray, 8);
    Check(PooledObject::destructionCount == 9, "pool-resolved array destruction");

    PolymorphicObject::destructionCount = 0;
    PolymorphicObject* const polymorphicObject = VANGUARD_NEW(SpecializedPolymorphicObject);
    Check(polymorphicObject->GetMemoryPool().GetHandle() == PoolPolymorphicTests::GetHandle(), "derived polymorphic pool override");
    VANGUARD_DELETE(polymorphicObject);
    Check(PolymorphicObject::destructionCount == 1, "polymorphic deletion resolves runtime pool");

    void* rawPoolMemory = VANGUARD_ALLOCATE(PoolObjectTests, 128);
    Check(rawPoolMemory != nullptr, "raw typed-pool allocation");
    rawPoolMemory = VANGUARD_REALLOCATE(PoolObjectTests, rawPoolMemory, 1024);
    Check(rawPoolMemory != nullptr, "raw typed-pool reallocation");
    VANGUARD_FREE(PoolObjectTests, rawPoolMemory);

    const auto invalidPool = static_cast<memory::PoolId>(static_cast<vanguard::u16>(memory::PoolId::Count));
    Check(!memory::IsValidPool(invalidPool), "invalid pool rejection");
    Check(!memory::IsAllocatablePool(memory::PoolId::Root), "root pool is a hierarchy node");
    Check(!memory::IsAllocatablePool(memory::PoolId::Cpu), "CPU pool is a hierarchy node");
    Check(memory::IsAllocatablePool(memory::PoolId::Jobs), "Jobs pool is allocatable");

    Check(memory::GetParentPool(memory::PoolId::Runtime) == memory::PoolId::Engine, "Runtime pool hierarchy");
    Check(memory::GetParentPool(memory::PoolId::Rendering) == memory::PoolId::Runtime, "Rendering pool hierarchy");
    Check(memory::GetParentPool(memory::PoolId::Window) == memory::PoolId::Runtime, "Window pool hierarchy");
    Check(memory::GetParentPool(memory::PoolId::Assets) == memory::PoolId::Tools, "Assets pool hierarchy");
    Check(std::strcmp(memory::GetPoolName(memory::PoolId::Jobs), "Jobs") == 0, "pool name");

    VisitState visitState;
    memory::VisitPools(&CapturePool, &visitState);
    Check(visitState.count >= static_cast<vanguard::u32>(memory::PoolId::Count), "complete pool traversal count");
    bool visitedEveryVanguardPool = true;
    for (vanguard::u16 value = 0; value < static_cast<vanguard::u16>(memory::PoolId::Count); ++value)
    {
        visitedEveryVanguardPool &= visitState.known[value];
    }
    Check(visitedEveryVanguardPool, "all Vanguard pools are traversed");
    Check(visitState.sawCompatibilityPool, "compatibility-internal pools remain visible");

    memory::PoolMetrics jobsBefore;
    memory::PoolMetrics engineBefore;
    Check(memory::GetPoolMetrics(memory::PoolId::Jobs, jobsBefore), "Jobs metrics before allocation");
    Check(memory::GetPoolMetrics(memory::PoolId::Engine, engineBefore), "Engine metrics before allocation");

    memory::MemoryBlock jobsBlock = memory::Allocate(memory::PoolId::Jobs, 256, 64);
    Check(static_cast<bool>(jobsBlock), "Jobs pool allocation");
    Check(jobsBlock.pool == memory::PoolId::Jobs, "allocation retains originating pool");

    memory::PoolMetrics jobsDuring;
    memory::PoolMetrics engineDuring;
    Check(memory::GetPoolMetrics(memory::PoolId::Jobs, jobsDuring), "Jobs metrics during allocation");
    Check(memory::GetPoolMetrics(memory::PoolId::Engine, engineDuring), "Engine metrics during allocation");
    Check(jobsDuring.allocatedBytes >= jobsBefore.allocatedBytes + jobsBlock.size, "exclusive pool allocation metrics");
    Check(engineDuring.allocatedBytesIncludingChildren >= engineBefore.allocatedBytesIncludingChildren + jobsBlock.size, "inclusive parent metrics");

    std::memset(jobsBlock.address, 0x6D, 256);
    Check(memory::Reallocate(jobsBlock, 2048, 64), "pool-preserving reallocation");
    Check(jobsBlock.pool == memory::PoolId::Jobs, "reallocation preserves originating pool");
    const auto* const reallocatedBytes = static_cast<const unsigned char*>(jobsBlock.address);
    bool contentsPreserved = true;
    for (vanguard::usize index = 0; index < 256; ++index)
    {
        if (reallocatedBytes[index] != 0x6D)
        {
            contentsPreserved = false;
            break;
        }
    }
    Check(contentsPreserved, "reallocation preserves existing contents");

    const vanguard::u64 originalBudget = memory::GetPoolBudget(memory::PoolId::Jobs);
    Check(memory::SetPoolBudget(memory::PoolId::Jobs, 64 * 1024 * 1024), "set pool budget");
    Check(memory::GetPoolBudget(memory::PoolId::Jobs) == 64 * 1024 * 1024, "read pool budget");
    Check(memory::SetPoolBudget(memory::PoolId::Jobs, originalBudget), "restore pool budget");

    memory::PrepareMetricsForNextFrame();
    memory::PoolMetrics jobsNextFrame;
    Check(memory::GetPoolMetrics(memory::PoolId::Jobs, jobsNextFrame), "next-frame pool metrics");
    if (jobsNextFrame.extendedMetricsAvailable)
    {
        Check(jobsNextFrame.allocatedBytesPreviousFrame > 0, "extended previous-frame allocation metrics");
    }

    memory::Free(jobsBlock);
    Check(!jobsBlock, "pool block invalidated by free");

    memory::PoolMetrics jobsAfter;
    Check(memory::GetPoolMetrics(memory::PoolId::Jobs, jobsAfter), "Jobs metrics after free");
    Check(jobsAfter.allocatedBytes == jobsBefore.allocatedBytes, "exclusive metrics return to baseline");

    Check(!memory::Allocate(memory::PoolId::Root, 64), "non-allocatable root rejects allocation");
    Check(!memory::Allocate(invalidPool, 64), "invalid pool rejects allocation");

    memory::MemoryBlock frameBlock = memory::Allocate(memory::PoolId::Frame, 512, 16);
    Check(static_cast<bool>(frameBlock), "frame pool allocation");
    memory::ResetFramePools();
    frameBlock = {};

    constexpr vanguard::u32 threadCount = 8;
    constexpr vanguard::u32 iterations = 2000;
    std::atomic<bool> threadFailure = false;
    std::thread threads[threadCount];

    for (vanguard::u32 threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads[threadIndex] = std::thread(
            [threadIndex, &threadFailure]()
            {
                constexpr memory::PoolId pools[] = {memory::PoolId::Containers, memory::PoolId::Jobs, memory::PoolId::Streaming, memory::PoolId::Rendering};

                for (vanguard::u32 iteration = 0; iteration < iterations; ++iteration)
                {
                    memory::MemoryBlock block = memory::Allocate(pools[threadIndex % 4], 32 + (iteration % 1024), 16);
                    if (!block)
                    {
                        threadFailure.store(true, std::memory_order_relaxed);
                        return;
                    }
                    std::memset(block.address, 0xA7, block.size);
                    memory::Free(block);
                }
            });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
    Check(!threadFailure.load(std::memory_order_relaxed), "multithreaded cross-pool allocation stress");

    memory::PoolSnapshot jobsSnapshot;
    Check(memory::GetPoolSnapshot(memory::PoolId::Jobs, jobsSnapshot), "pool snapshot");
    Check(jobsSnapshot.id == memory::PoolId::Jobs && jobsSnapshot.parent == memory::PoolId::Engine && jobsSnapshot.allocatable, "pool snapshot identity");

    if (failures == 0)
    {
        std::puts("memoryPoolTests: all pool checks passed");
    }
    return failures == 0 ? 0 : 1;
}
