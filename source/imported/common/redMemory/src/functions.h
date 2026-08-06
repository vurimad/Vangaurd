/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FUNCTIONS_H_
#define _RED_MEMORY_FUNCTIONS_H_

#include "../include/hookType.h"

namespace red
{
namespace memory
{
	class Pool;

	template< typename PoolType >
	void * Allocate( u32 size, u32 disabledHooks = HookType::HookType_None );

	template< typename Proxy >
	void * Allocate( Proxy & proxy, u32 size, u32 disabledHooks = HookType::HookType_None );

	void * Allocate( Pool & pool, u32 size, u32 disabledHooks = HookType::HookType_None );

	template< typename PoolType >
	void * AllocateAligned( u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	template< typename Proxy >
	void * AllocateAligned( Proxy & proxy, u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	void * AllocateAligned( Pool & pool, u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	template< typename Pool >
	void * Reallocate( void * block, u32 size, u32 disabledHooks = HookType::HookType_None );

	template< typename Proxy >
	void * Reallocate( Proxy & proxy, void * block, u32 size, u32 disabledHooks = HookType::HookType_None );

	void * Reallocate( Pool & pool, u32 size, u32 disabledHooks = HookType::HookType_None );

	template< typename PoolType >
	void * ReallocateAligned( void * block, u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	template< typename Proxy >
	void * ReallocateAligned( Proxy & proxy, void * block, u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	void * ReallocateAligned( Pool & pool, void * block, u32 size, u32 alignment, u32 disabledHooks = HookType::HookType_None );

	template< typename PoolType >
	void Free( const void * block, u32 disabledHooks = HookType::HookType_None );

	template< typename Proxy >
	void Free( Proxy & allocator, const void * block, u32 disabledHooks = HookType::HookType_None );

	void Free( Pool & pool, const void * block, u32 disabledHooks = HookType::HookType_None );
}
}

#include "functions.hpp"

#endif
