/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_POOL_ROOT_H_
#define _RED_MEMORY_INCLUDE_POOL_ROOT_H_

#include "redMemoryInternal.h"
#include "pool.h"
#include "defaultAllocator.h"
#include "frameAllocator.h"


namespace red
{
namespace memory
{
#ifndef RED_CONFIGURATION_FINAL
	using ThreadSafeFrameAllocator = LocklessDebugBanFrameAllocator;
#else
	using ThreadSafeFrameAllocator = LocklessFrameAllocator;
#endif

	RED_MEMORY_POOL( PoolRoot, NullAllocator, RED_MEMORY_API );
		RED_MEMORY_POOL( PoolCPU, NullAllocator, RED_MEMORY_API );
		RED_MEMORY_POOL( PoolGPU, NullAllocator, RED_MEMORY_API );
		RED_MEMORY_POOL( PoolFlexible, NullAllocator, RED_MEMORY_API );

	RED_MEMORY_API void ResetFrameAllocators();

	// redMemory will break on any PoolDefault Allocation.
	RED_MEMORY_API void BreakOnPoolDefaultAllocation();
}

#ifdef RED_PLATFORM_CONSOLE
	RED_MEMORY_POOL_EXPLICIT( PoolDebug, memory::ConsoleDebugAllocator, RED_MEMORY_API );
#else
	RED_MEMORY_POOL_EXPLICIT( PoolDebug, memory::DefaultAllocator, RED_MEMORY_API );
#endif
	RED_MEMORY_POOL_EXPLICIT( PoolRefCount, memory::DefaultAllocator, RED_MEMORY_API );
	RED_MEMORY_POOL_EXPLICIT( PoolLegacyOperator, memory::DefaultAllocator, RED_MEMORY_API );

	RED_MEMORY_POOL( PoolFrame, red::memory::FrameAllocatorWithFallback, RED_MEMORY_API );
	RED_MEMORY_POOL( PoolDoubleBufferedFrame, red::memory::FrameAllocatorWithFallback, RED_MEMORY_API );

	RED_MEMORY_POOL_EXPLICIT( PoolEngine, red::memory::DefaultAllocator, RED_MEMORY_API ); // ctremblay: Root pool for all engine allocations.
	RED_MEMORY_POOL_EXPLICIT( PoolBackend, red::memory::DefaultAllocator, RED_MEMORY_API ); // ctremblay: Root pool for all backend and editor only allocations.

	// Allocation made without explicit Pool will be routed to PoolDefault Pool.
	// This pool is explicitly declared and defined to allow disallowing allocation from it at runtime.
	class RED_MEMORY_API PoolDefault : public red::memory::Pool
	{
	public:
		typedef memory::DefaultAllocator AllocatorType;
		typedef red::memory::PoolStorageProxy< PoolDefault > ProxyType;
		RED_MEMORY_INLINE PoolDefault() {}
		RED_MEMORY_INLINE ~PoolDefault() {}
		RED_MEMORY_DECLARE_PROXY( PoolDefault, AllocatorType::DefaultAlignmentType::value );
		RED_MEMORY_INLINE static red::memory::PoolHandle GetHandle() { static constexpr red::THash32 nameHash = red::CalculateHash32( "PoolDefault" ); return nameHash; }
		RED_MEMORY_INLINE static AllocatorType & GetAllocator() { return ProxyType::GetAllocator(); }
		RED_MEMORY_INLINE static PoolDefault & GetInstance() { static PoolDefault pool; return pool; }
	private:
		virtual red::memory::Block OnAllocate( red::memory::u32 size ) const override final;
		virtual red::memory::Block OnAllocateAligned( red::memory::u32 size, red::memory::u32 alignment ) const override final;
		virtual red::memory::Block OnReallocate( red::memory::Block & block, red::memory::u32 size ) const override final;
		virtual red::memory::Block OnReallocateAligned( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment ) const override final;
		virtual void OnFree( red::memory::Block & block ) const override final;
		virtual red::memory::u64 OnGetBlockSize( red::memory::u64 address ) const override final;
		virtual red::memory::PoolHandle OnGetHandle() const override final;
	};
}

RED_MEMORY_DECLARE_POOL_STORAGE( red::memory::PoolRoot, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::memory::PoolCPU, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::memory::PoolGPU, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::memory::PoolFlexible, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolDebug, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolRefCount, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolLegacyOperator, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolFrame, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolDoubleBufferedFrame, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolEngine, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolBackend, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::PoolDefault, RED_MEMORY_API );

#endif
