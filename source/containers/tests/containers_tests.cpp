#include <vanguard/containers/containers.hpp>

#include <cstdio>
#include <cstring>
#include <utility>

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAILED: %s\n", message);
            ++failures;
        }
    }

    struct LifetimeValue
    {
        explicit LifetimeValue(const vanguard::u32 initial = 0) : value(initial)
        {
            ++alive;
        }

        LifetimeValue(const LifetimeValue& other) : value(other.value)
        {
            ++alive;
        }

        LifetimeValue(LifetimeValue&& other) noexcept : value(other.value)
        {
            other.value = 0;
            ++alive;
        }

        LifetimeValue& operator=(const LifetimeValue&) = default;
        LifetimeValue& operator=(LifetimeValue&&) = default;

        ~LifetimeValue()
        {
            --alive;
        }

        bool operator==(const LifetimeValue& other) const
        {
            return value == other.value;
        }

        bool operator<(const LifetimeValue& other) const
        {
            return value < other.value;
        }

        vanguard::u32 value = 0;
        inline static vanguard::i32 alive = 0;
    };
} // namespace

int main()
{
    namespace memory = vanguard::memory;
    namespace containers = vanguard::containers;

    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(containers::IsInitialized(), "containers initialized state");
    Check(containers::Initialize(), "containers idempotent initialization");

    memory::PoolMetrics before;
    Check(memory::GetPoolMetrics(memory::PoolId::Containers, before), "Containers pool baseline metrics");

    {
        const memory::Pool& pool = memory::pools::Containers();

        containers::DynamicArray<vanguard::u32> values(pool);
        for (vanguard::u32 value = 0; value < 4096; ++value)
        {
            values.PushBack(value);
        }
        Check(values.Size() == 4096, "dynamic array growth");
        Check(values.Front() == 0 && values.Back() == 4095, "dynamic array ordering");

        containers::ArraySpan<const vanguard::u32> view(values);
        Check(view.Size() == values.Size() && view[2048] == 2048, "non-owning array span");

        containers::DynamicArray<vanguard::u32> copied(values, pool);
        Check(copied.Size() == values.Size() && copied[1024] == 1024, "pool-aware array copy");

        containers::DynamicArray<vanguard::u32> moved(std::move(copied));
        Check(moved.Size() == 4096 && copied.Empty(), "pool-preserving array move");

        containers::StaticArray<vanguard::u32, 8> fixed;
        fixed.PushBack(7);
        fixed.PushBack(9);
        Check(fixed.Size() == 2 && fixed[1] == 9, "inline static array");

        LifetimeValue::alive = 0;
        {
            containers::DynamicArray<LifetimeValue> lifetimeValues(pool);
            lifetimeValues.EmplaceBack(11);
            lifetimeValues.EmplaceBack(22);
            lifetimeValues.EmplaceBack(33);
            Check(LifetimeValue::alive == 3, "element construction");
            lifetimeValues.RemoveAt(1);
            Check(LifetimeValue::alive == 2 && lifetimeValues[1].value == 33, "element removal and lifetime");
        }
        Check(LifetimeValue::alive == 0, "element destruction");

        containers::HashMap<vanguard::u32, vanguard::u32> hashMap(pool);
        hashMap.Insert(vanguard::u32{10}, vanguard::u32{100});
        hashMap.Insert(vanguard::u32{20}, vanguard::u32{200});
        vanguard::u32 foundValue = 0;
        Check(hashMap.Find(vanguard::u32{20}, foundValue) && foundValue == 200, "hash map lookup");

        containers::Map<vanguard::u32, vanguard::u32> orderedMap(pool);
        orderedMap.Insert(30, 300);
        orderedMap.Insert(10, 100);
        Check(orderedMap.Begin().Key() == 10, "ordered map iteration");

        containers::HashSet<vanguard::u32> hashSet(pool);
        hashSet.Insert(vanguard::u32{4});
        hashSet.Insert(vanguard::u32{8});
        Check(hashSet.Exist(vanguard::u32{8}), "hash set lookup");

        containers::Set<vanguard::u32> orderedSet(pool);
        orderedSet.Insert(9);
        orderedSet.Insert(3);
        Check(*orderedSet.Begin() == 3, "ordered set iteration");

        containers::Queue<vanguard::u32> queue(pool);
        queue.Push(5);
        queue.Push(7);
        Check(queue.Front() == 5 && queue.Back() == 7, "pool-aware queue");
        queue.Pop();
        Check(queue.Front() == 7, "queue FIFO behavior");

        containers::CircularBuffer<vanguard::u32> circular(8, pool);
        circular.PushBack(1);
        circular.PushBack(2);
        Check(circular.Size() == 2 && circular.Front() == 1, "circular buffer");

        containers::PackedArray<8> packed(32, pool);
        packed.Set(7, 255);
        Check(packed.Get(7) == 255, "packed array bit storage");

        containers::BitSet<128> bits;
        bits.Set(3);
        bits.Set(97);
        Check(bits.Get(3) && bits.Get(97) && !bits.Get(4), "fixed bit set");

        containers::BitSetDynamic dynamicBits(130, pool);
        dynamicBits.Set(129);
        Check(dynamicBits.Get(129), "dynamic bit set");

        containers::MPMCLockFreeQueue<vanguard::u32, 16> lockFreeQueue;
        Check(lockFreeQueue.Push(77), "lock-free queue push");
        vanguard::u32 popped = 0;
        Check(lockFreeQueue.Pop(popped) && popped == 77, "lock-free queue pop");

        containers::Blob blob(256, 64);
        Check(blob.Size() == 256 && reinterpret_cast<vanguard::usize>(blob.Data()) % 64 == 0, "owned aligned blob");
        containers::BlobView blobView(blob);
        Check(blobView.Size() == 256, "blob view");

        containers::String text("Vanguard", pool);
        Check(text.Length() == 8 && std::strcmp(text.AsChar(), "Vanguard") == 0, "pool-aware string");
        containers::StringView textView(text);
        Check(textView.Size() == 8, "string view");

        memory::PoolMetrics during;
        Check(memory::GetPoolMetrics(memory::PoolId::Containers, during), "Containers pool active metrics");
        Check(during.allocatedBytes > before.allocatedBytes, "owning containers route through Containers pool");
    }

    memory::PoolMetrics after;
    Check(memory::GetPoolMetrics(memory::PoolId::Containers, after), "Containers pool final metrics");
    Check(after.allocatedBytes == before.allocatedBytes, "container allocations return to baseline");

    if (failures == 0)
    {
        std::puts("containersTests: all container checks passed");
    }
    return failures == 0 ? 0 : 1;
}
