/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FUNCTIONS_HPP_
#define _RED_MEMORY_FUNCTIONS_HPP_

#include "hookUtils.h"
#include "poolStorage.h"
#include "pool.h"

#ifdef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
#include "debugAllocator.h"
#endif

namespace red
{
namespace memory
{
#if defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
namespace internal
{
	template< typename Proxy >
	Proxy& AcquireProxy( Proxy & proxy, typename std::enable_if< std::is_base_of< Pool, Proxy >::value, void** >::type = nullptr )
	{
		return proxy;
	}

	template< typename Proxy >
	DebugAllocator& AcquireProxy( Proxy &, typename std::enable_if< !std::is_base_of< Pool, Proxy >::value, void** >::type = nullptr )
	{
		return AcquireDebugAllocator();
	}
}
#endif

	template< typename PoolType >
	RED_MEMORY_INLINE void * Allocate( u32 size, u32 disabledHooks )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;

		ProcessPreAllocateHooks< PoolType >( size, disabledHooks );
		Block block = ProxyType::Allocate( size );
		ProcessPostAllocateHooks< PoolType >( block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * Allocate( Proxy & proxy, u32 size, u32 disabledHooks )
	{
		ProcessPreAllocateHooks( proxy, size, disabledHooks );

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		Block block = proxy.Allocate( size );
#else
		Block block = internal::AcquireProxy( proxy ).Allocate( size );
#endif
		ProcessPostAllocateHooks( proxy, block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	RED_MEMORY_INLINE void * Allocate( Pool & pool, u32 size, u32 disabledHooks )
	{
		ProcessPreAllocateHooks( pool, size, disabledHooks );
		Block block = pool.Allocate( size );
		ProcessPostAllocateHooks( pool, block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void * AllocateAligned( u32 size, u32 alignment, u32 disabledHooks )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;

		ProcessPreAllocateHooks< PoolType >( size, disabledHooks );
		Block block = ProxyType::AllocateAligned( size, alignment );
		ProcessPostAllocateHooks< PoolType >( block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * AllocateAligned( Proxy & proxy, u32 size, u32 alignment, u32 disabledHooks )
	{
		ProcessPreAllocateHooks( proxy, size, disabledHooks );

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		Block block = proxy.AllocateAligned( size, alignment );
#else
		Block block = internal::AcquireProxy( proxy ).AllocateAligned( size, alignment );
#endif
		ProcessPostAllocateHooks( proxy, block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	RED_MEMORY_INLINE void * AllocateAligned( Pool & pool, u32 size, u32 alignment, u32 disabledHooks )
	{
		ProcessPreAllocateHooks( pool, size, disabledHooks );
		Block block = pool.AllocateAligned( size, alignment );
		ProcessPostAllocateHooks( pool, block, disabledHooks );
		return reinterpret_cast< void* >( block.address );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void Free( const void * ptr, u32 disabledHooks )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;

		Block block = { reinterpret_cast< u64 >( ptr ), 0 };
		ProcessPreFreeHooks< PoolType >( block, disabledHooks );
		ProxyType::Free( block );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void Free( Proxy & proxy, const void * ptr, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreFreeHooks( proxy, block, disabledHooks );

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		proxy.Free( block );
#else
		internal::AcquireProxy( proxy ).Free( block );
#endif
	}

	RED_MEMORY_INLINE void Free( Pool & pool, const void * ptr, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };
		ProcessPreFreeHooks( pool, block, disabledHooks );
		pool.Free( block );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void * Reallocate( void * ptr, u32 size, u32 disabledHooks )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks< PoolType >( block, size, disabledHooks );
		Block result = ProxyType::Reallocate( block, size );
		ProcessPostReallocateHooks< PoolType >( block, result, disabledHooks );
	
		return reinterpret_cast< void* >( result.address );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * Reallocate( Proxy & proxy, void * ptr, u32 size, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks( proxy, block, size, disabledHooks );

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		Block result = proxy.Reallocate( block, size );
#else
		Block result = internal::AcquireProxy( proxy ).Reallocate( block, size );
#endif
		ProcessPostReallocateHooks( proxy, block, result, disabledHooks );
		
		return reinterpret_cast< void* >( result.address );
	}

	RED_MEMORY_INLINE void * Reallocate( Pool & pool, void * ptr, u32 size, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks( pool, block, size, disabledHooks );
		Block result = pool.Reallocate( block, size );
		ProcessPostReallocateHooks( pool, block, result, disabledHooks );
	
		return reinterpret_cast< void* >( result.address );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void * ReallocateAligned( void * ptr, u32 size, u32 alignment, u32 disabledHooks )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks< PoolType >( block, size, disabledHooks );
		Block result = ProxyType::ReallocateAligned( block, size, alignment );
		ProcessPostReallocateHooks< PoolType >( block, result, disabledHooks );

		return reinterpret_cast< void* >( result.address );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * ReallocateAligned( Proxy & proxy, void * ptr, u32 size, u32 alignment, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks( proxy, block, size, disabledHooks );

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		Block result = proxy.ReallocateAligned( block, size, alignment );
#else
		Block result = internal::AcquireProxy( proxy ).ReallocateAligned( block, size, alignment );
#endif
		ProcessPostReallocateHooks( proxy, block, result, disabledHooks );
		
		return reinterpret_cast< void* >( result.address );
	}

	RED_MEMORY_INLINE void * ReallocateAligned( Pool & pool, void * ptr, u32 size, u32 alignment, u32 disabledHooks )
	{
		Block block = { reinterpret_cast< u64 >( ptr ), 0 };

		ProcessPreReallocateHooks( pool, block, size, disabledHooks );
		Block result = pool.ReallocateAligned( block, size, alignment );
		ProcessPostReallocateHooks( pool, block, result, disabledHooks );
		
		return reinterpret_cast< void* >( result.address );
	}
}
}

#endif
