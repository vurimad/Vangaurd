/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "hookPool.h"
#include "hook.h"

namespace red
{
namespace memory
{
namespace 
{
	class HookImpl : public Hook
	{};
}

	HookPool::HookPool()
	{
		LocklessStaticFixedSizeAllocatorParameter param = 
		{
			m_buffer,
			sizeof( m_buffer ),
			sizeof( HookImpl ),
			__alignof( HookImpl ) 
		};

		m_allocator.Initialize( param );
	}

	Hook * HookPool::TakeHook()
	{
		Block block = m_allocator.Allocate( sizeof( HookImpl ) );
		RED_MEMORY_ASSERT( block.address, "No more hook available." );
		HookImpl * hook = new( reinterpret_cast< void* >( block.address ) ) HookImpl;
		return hook;
	}
	
	void HookPool::GiveHook( Hook * hook )
	{
		static_cast< HookImpl* >( hook )->~HookImpl();
		Block block = { AddressOf( hook ), 0 };
		m_allocator.Free( block );
	}

	u32 HookPool::GetTotalHookCount() const
	{
		return m_allocator.GetTotalBlockCount();
	}
}
}
