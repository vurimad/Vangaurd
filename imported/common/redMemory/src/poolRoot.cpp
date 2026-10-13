/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

namespace red
{
namespace memory
{
	static bool s_poolDefaultAllocationAllowed = true;

	void BreakOnPoolDefaultAllocation()
	{
		s_poolDefaultAllocationAllowed = false;
	}

	void CheckPoolDefaultAuthorization()
	{
		if(!s_poolDefaultAllocationAllowed)
		{
			// ctremblay: This won't break for production.
			// If you hit this and you are a programmers, that means you have a PoolDefault Allocation
			// most likely coming from a Container. FIX ASAP!
			RED_BREAKPOINT(); 
		}
	}
}
	
	red::memory::Block PoolDefault::OnAllocate( red::memory::u32 size ) const 
	{ 
		memory::CheckPoolDefaultAuthorization();
		return ProxyType::Allocate( size ); 
	}
	
	red::memory::Block PoolDefault::OnAllocateAligned( red::memory::u32 size, red::memory::u32 alignment ) const
	{ 
		memory::CheckPoolDefaultAuthorization();
		return ProxyType::AllocateAligned( size, alignment ); 
	}
	
	red::memory::Block PoolDefault::OnReallocate( red::memory::Block & block, red::memory::u32 size ) const
	{ 
		memory::CheckPoolDefaultAuthorization();
		return ProxyType::Reallocate( block, size ); 
	}
	
	red::memory::Block PoolDefault::OnReallocateAligned( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment ) const
	{ 
		memory::CheckPoolDefaultAuthorization();
		return ProxyType::ReallocateAligned( block, size, alignment ); 
	}
	
	void PoolDefault::OnFree( red::memory::Block & block ) const
	{ 
		ProxyType::Free( block ); 
	}
	
	red::memory::u64 PoolDefault::OnGetBlockSize( red::memory::u64 address ) const
	{ 
		return ProxyType::GetBlockSize( address ); 
	}
	
	red::memory::PoolHandle PoolDefault::OnGetHandle() const
	{ 
		return ProxyType::GetHandle(); 
	}
}

