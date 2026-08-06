# Vanguard memory pools

Vanguard Memory preserves RED's proven pool model: pools are declared ahead of
use, initialized into a hierarchy, backed by explicit allocator implementations,
and registered with budgets and metrics. Pools are not arbitrary heap-created
objects.

## Public contract

`vanguard::memory::PoolId` identifies canonical engine categories.
`MemoryBlock` records the pool that produced it, together with the exact size
reported by RED. Free and reallocate therefore return through the originating
pool automatically.

The default `Allocate(size, alignment)` overload remains available and routes
to `PoolId::Engine`. New subsystem code should use the explicit overload:

```cpp
auto block = vanguard::memory::Allocate(
    vanguard::memory::PoolId::Streaming,
    byteCount,
    alignment);
```

This block API is only the low-level runtime facade. Vanguard also exposes
RED's compile-time pool contract through `vanguard/memory/pool.hpp`.

## Declaring and initializing subsystem pools

Pools remain compile-time types, as they are in RED:

```cpp
VANGUARD_MEMORY_POOL_STATIC(
    PoolBatchers,
    vanguard::memory::DefaultAllocator);
```

The owning module initializes its pool after Vanguard Memory:

```cpp
VANGUARD_INITIALIZE_MEMORY_POOL(
    PoolBatchers,
    vanguard::memory::pools::Rendering,
    vanguard::memory::AcquireDefaultAllocator(),
    64ull * 1024ull * 1024ull);
```

`VANGUARD_MEMORY_POOL`, `VANGUARD_MEMORY_DEFINE_POOL_STORAGE`, and the
allocator argument preserve RED's cross-module and specialized-allocator
forms. Initialization remains explicit and serialized by the owning module.

## Binding objects to pools

`VANGUARD_USE_MEMORY_POOL` supplies the compile-time context consumed by RED's
pool resolver:

```cpp
class Batcher
{
    VANGUARD_USE_MEMORY_POOL(PoolBatchers);

public:
    explicit Batcher(Renderer& renderer);
};

Batcher* batcher = VANGUARD_NEW(Batcher)(renderer);
VANGUARD_DELETE(batcher);
```

The binding deliberately prohibits ordinary class `new` and `new[]`.
`VANGUARD_NEW`, `VANGUARD_DELETE`, and their array forms preserve RED's
construction, destruction, alignment, and pool resolution.

Polymorphic bases use `VANGUARD_USE_POLYMORPHIC_MEMORY_POOL`. Deletion through
a base pointer obtains the object's virtual pool before destruction, allowing a
derived type to override its allocation pool exactly as in RED.

## Container allocator contract

`vanguard::memory::Pool` is the pool reference accepted by allocator-aware
containers. A container retains the supplied pool and uses it for buffer
allocation, growth, reallocation, move, and release:

```cpp
DynamicArray<RenderItem> items{
    vanguard::memory::pools::Rendering()};
```

The Vanguard Containers adaptation must preserve this contract. Specialized
storage can use `VANGUARD_ALLOCATE`, `VANGUARD_REALLOCATE`, and
`VANGUARD_FREE` with a pool type directly.

## Hierarchy

```text
Root
|-- CPU
|   |-- Engine
|   |   |-- Runtime
|   |   |   |-- Resources, World, Streaming, Rendering, Physics
|   |   |   `-- Animation, Audio, Input, Navigation, Networking, Gameplay
|   |   |-- Diagnostics, Containers, Concurrency, Jobs
|   |   `-- IO, Filesystem, Serialization, Reflection
|   |-- RefCount
|   |-- Frame
|   `-- DoubleBufferedFrame
|-- GPU
|-- Backend
|   |-- Editor
|   `-- Tools
|       `-- Assets
`-- Debug
```

Root, CPU, and GPU are hierarchy nodes backed by RED null allocators and cannot
be selected for ordinary allocations. The other canonical pools use the same
mature RED allocators while retaining separate storage, metrics, peak usage,
and budgets.

## Budgets

RED budgets describe and validate the pool hierarchy; they are not hard
allocation limits. `SetPoolBudget` and `GetPoolBudget` preserve those semantics.
Canonical category pools start with RED's commonly used zero/unconfigured
budget until platform and workload configuration supplies measured values.

## Metrics and inspection

The public metrics contract exposes:

- exclusive and inclusive allocated bytes;
- peak allocated bytes;
- budget;
- allocation counts;
- current and previous frame allocation/deallocation activity.

Extended per-frame counters are available in Debug, Development, and Profile.
Shipping retains RED's bounded current/peak report data but does not manufacture
extended counters that RED compiled out.

`VisitPools` traverses the complete registered RED hierarchy. Canonical pools
carry a valid Vanguard `PoolId`. Compatibility and Vanguard modules may
register additional typed pools. These remain visible by runtime handle and
name even without a canonical `PoolId`; typed code allocates through their pool
class rather than the canonical block facade.

The visitor runs synchronously during RED registry traversal. It must copy any
required data and must not register pools or reenter pool traversal.

## Frame lifecycle

`ResetFramePools` resets RED's frame and double-buffered frame allocators.
Every pointer and `MemoryBlock` obtained from those pools becomes invalid at
that boundary and must not be freed individually afterward.

`PrepareMetricsForNextFrame` advances RED's per-frame counters. The managed
Frame Pipeline owns both operations and calls them exactly once after all work
for an executed frame has completed, including a frame terminated by a
participant failure. Product applications and individual subsystems must not
perform a second rollover.
