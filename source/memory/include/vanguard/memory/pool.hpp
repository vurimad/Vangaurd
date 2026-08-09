#pragma once

#ifndef _SILENCE_CXX20_IS_POD_DEPRECATION_WARNING
#define _SILENCE_CXX20_IS_POD_DEPRECATION_WARNING
#endif

#include <vanguard/memory/memory.hpp>

// This is the Vanguard-facing adaptation boundary for the compile-time
// pool machinery. Engine code uses only the VANGUARD_* surface below.
#if defined(_MSC_VER) && !defined(RED_COMPILER_MSC)
#define RED_COMPILER_MSC
#endif

#if defined(VG_BUILD_DEBUG) && !defined(RED_CONFIGURATION_DEBUG)
#define RED_CONFIGURATION_DEBUG
#elif (defined(VG_BUILD_DEVELOPMENT) || defined(VG_BUILD_PROFILE)) && !defined(RED_CONFIGURATION_RELEASE)
#define RED_CONFIGURATION_RELEASE
#elif defined(VG_BUILD_SHIPPING) && !defined(RED_CONFIGURATION_FINAL)
#define RED_CONFIGURATION_FINAL
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#include "../../../../imported/common/redMemory/include/redMemoryInternal.h"
#include "../../../../imported/common/redMemory/include/defaultAllocator.h"
#include "../../../../imported/common/redMemory/include/operators.h"
#include "../../../../imported/common/redMemory/include/pool.h"
#include "../../../../imported/common/redMemory/include/poolUtils.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vanguard::memory
{
    using Pool = ::red::memory::Pool;
    using DefaultAllocator = ::red::memory::DefaultAllocator;

    [[nodiscard]] inline DefaultAllocator& AcquireDefaultAllocator() noexcept
    {
        return ::red::memory::AcquireDefaultAllocator();
    }

    namespace pools
    {
        using Root = ::red::memory::PoolRoot;
        using Cpu = ::red::memory::PoolCPU;
        using Gpu = ::red::memory::PoolGPU;
        using Engine = ::red::PoolEngine;
        using Backend = ::red::PoolBackend;
        using Debug = ::red::PoolDebug;
        using RefCount = ::red::PoolRefCount;
        using Frame = ::red::PoolFrame;
        using DoubleBufferedFrame = ::red::PoolDoubleBufferedFrame;

#define VANGUARD_DECLARE_CANONICAL_POOL(name) RED_MEMORY_POOL(name, ::red::memory::DefaultAllocator, RED_MEMORY_API);

        VANGUARD_DECLARE_CANONICAL_POOL(Runtime)
        VANGUARD_DECLARE_CANONICAL_POOL(Editor)
        VANGUARD_DECLARE_CANONICAL_POOL(Tools)
        VANGUARD_DECLARE_CANONICAL_POOL(Diagnostics)
        VANGUARD_DECLARE_CANONICAL_POOL(Containers)
        VANGUARD_DECLARE_CANONICAL_POOL(Concurrency)
        VANGUARD_DECLARE_CANONICAL_POOL(Jobs)
        VANGUARD_DECLARE_CANONICAL_POOL(Io)
        VANGUARD_DECLARE_CANONICAL_POOL(Filesystem)
        VANGUARD_DECLARE_CANONICAL_POOL(Serialization)
        VANGUARD_DECLARE_CANONICAL_POOL(Reflection)
        VANGUARD_DECLARE_CANONICAL_POOL(Resources)
        VANGUARD_DECLARE_CANONICAL_POOL(Assets)
        VANGUARD_DECLARE_CANONICAL_POOL(World)
        VANGUARD_DECLARE_CANONICAL_POOL(Streaming)
        VANGUARD_DECLARE_CANONICAL_POOL(Rendering)
        VANGUARD_DECLARE_CANONICAL_POOL(Physics)
        VANGUARD_DECLARE_CANONICAL_POOL(Animation)
        VANGUARD_DECLARE_CANONICAL_POOL(Audio)
        VANGUARD_DECLARE_CANONICAL_POOL(Input)
        VANGUARD_DECLARE_CANONICAL_POOL(Window)
        VANGUARD_DECLARE_CANONICAL_POOL(Navigation)
        VANGUARD_DECLARE_CANONICAL_POOL(Networking)
        VANGUARD_DECLARE_CANONICAL_POOL(Gameplay)

#undef VANGUARD_DECLARE_CANONICAL_POOL
    } // namespace pools
} // namespace vanguard::memory

// Pool declaration and storage use compile-time type identity.
#define VANGUARD_MEMORY_POOL_STATIC(poolName, allocatorType) RED_MEMORY_POOL_STATIC(poolName, allocatorType)

#define VANGUARD_MEMORY_POOL(poolName, allocatorType, moduleApi) RED_MEMORY_POOL(poolName, allocatorType, moduleApi)

#define VANGUARD_MEMORY_DEFINE_POOL_STORAGE(poolName, moduleApi) RED_MEMORY_DEFINE_POOL_STORAGE(poolName, moduleApi)

#define VANGUARD_INITIALIZE_MEMORY_POOL(...) RED_INITIALIZE_MEMORY_POOL(__VA_ARGS__)

// Object-to-pool resolution supports static and polymorphic contracts.
#define VANGUARD_USE_MEMORY_POOL(poolName) RED_USE_MEMORY_POOL(poolName)

#define VANGUARD_USE_POLYMORPHIC_MEMORY_POOL(poolName) RED_USE_POLYMORPHIC_MEMORY_POOL(poolName)

// Typed construction/destruction, including explicit pool overrides.
#define VANGUARD_NEW(...) RED_NEW(__VA_ARGS__)
#define VANGUARD_NEW_ARRAY(...) RED_NEW_ARRAY(__VA_ARGS__)
#define VANGUARD_DELETE(...) RED_DELETE(__VA_ARGS__)
#define VANGUARD_DELETE_ARRAY(...) RED_DELETE_ARRAY(__VA_ARGS__)

// Raw pool operations used by allocator-aware containers and specialized
// storage. The proxy argument is a pool type.
#define VANGUARD_ALLOCATE(poolProxy, size) RED_ALLOCATE(poolProxy, size)

#define VANGUARD_ALLOCATE_ALIGNED(poolProxy, size, alignment) RED_ALLOCATE_ALIGNED(poolProxy, size, alignment)

#define VANGUARD_REALLOCATE(poolProxy, address, size) RED_REALLOCATE(poolProxy, address, size)

#define VANGUARD_REALLOCATE_ALIGNED(poolProxy, address, size, alignment) RED_REALLOCATE_ALIGNED(poolProxy, address, size, alignment)

#define VANGUARD_FREE(poolProxy, address) RED_FREE(poolProxy, address)
