/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemPageAllocatorDurango.h"
#include "assert.h"
#include "utils.h"
#include "flags.h"

namespace red
{
namespace memory
{
namespace
{
	const u64 c_durangoSystemPageSize = 64 * 1024; 
	const u64 c_durangoPageAlignment = 4 * 1024;
}
	
	SystemPageAllocatorDurango::SystemPageAllocatorDurango()
	{}

	SystemPageAllocatorDurango::~SystemPageAllocatorDurango()
	{}

	void SystemPageAllocatorDurango::OnInitialize()
	{
		SetPageSize( c_durangoSystemPageSize );
	}

	VirtualRange SystemPageAllocatorDurango::OnReserveRange( u64 size, u32 /*pageSize*/, u32 flags ) 
	{
		const u64 sizeRoundedToPageSize = RoundUp( size, c_durangoSystemPageSize );
		u32 reserveFlags = flags & Flags_GPU_Read_Write ? MEM_RESERVE | MEM_LARGE_PAGES | MEM_GRAPHICS : MEM_RESERVE | MEM_LARGE_PAGES;
		void * pages = ::VirtualAlloc( nullptr, sizeRoundedToPageSize, reserveFlags, PAGE_READONLY );

		if ( !pages )
		{
			return NullVirtualRange();
		}

		const u64 start = AddressOf( pages );
		const u64 end = start + sizeRoundedToPageSize;
		VirtualRange range = { start, end };
		return range;
	}

	VirtualRange SystemPageAllocatorDurango::OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment )
	{
		if ( alignment == c_durangoSystemPageSize )
		{
			return OnReserveRange( size, pageSize, flags );
		}
		else
		{
			RED_MEMORY_HALT( "OnReserveAlignedRange with alignment different than page size is not available on Durango" );
			return NullVirtualRange();
		}
	}

	void SystemPageAllocatorDurango::OnReleaseRange( const VirtualRange & range )
	{
		void * ptr = reinterpret_cast< void* >( range.start );
		::VirtualFree( ptr, 0, MEM_RELEASE );
	}
}
}
