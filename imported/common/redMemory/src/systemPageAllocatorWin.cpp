/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemPageAllocatorWin.h"
#include "assert.h"
#include "utils.h"

namespace red
{
namespace memory
{
	SystemPageAllocatorWin::SystemPageAllocatorWin()
	{}
	
	SystemPageAllocatorWin::~SystemPageAllocatorWin()
	{}

	void SystemPageAllocatorWin::OnInitialize()
	{
		SYSTEM_INFO sysInfo;
		::GetSystemInfo( &sysInfo );

		// On Windows machines, allocations are aligned to dwAllocationGranularity, but page size is dwPageSize
		// and is generally smaller
		// In order to reduce virtual memory fragmentation, we will allocate in chunks of dwAllocationGranularity
		// otherwise, we will introduce holes
		const u32 pageSize = sysInfo.dwAllocationGranularity;
		SetPageSize( pageSize );
	}

	VirtualRange SystemPageAllocatorWin::OnReserveRange( u64 size, u32 pageSize, u32 ) 
	{
		const u64 sizeRoundedToPageSize = RoundUp( size, static_cast< u64 >( pageSize ) );
		void * pages = ::VirtualAlloc( nullptr, sizeRoundedToPageSize, MEM_RESERVE, PAGE_READONLY );

		if ( !pages )
		{
			return NullVirtualRange();
		}

		const u64 start = AddressOf( pages );
		const u64 end = start + sizeRoundedToPageSize;
		VirtualRange range = { start, end };
		return range;
	}

	VirtualRange SystemPageAllocatorWin::OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment ) 
	{
		if ( alignment == pageSize )
		{
			return OnReserveRange( size, pageSize, flags );
		}
		else
		{
			RED_MEMORY_HALT( "OnReserveAlignedRange with alignment different than page size is not available on Win(PC)" );
			return NullVirtualRange();
		}
	}

	void SystemPageAllocatorWin::OnReleaseRange( const VirtualRange & range )
	{
		void * ptr = reinterpret_cast< void* >( range.start );
		::VirtualFree( ptr, 0, MEM_RELEASE );
	}
}
}
