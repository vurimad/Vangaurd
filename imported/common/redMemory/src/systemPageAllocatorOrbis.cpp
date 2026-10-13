/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemPageAllocatorOrbis.h"
#include "utils.h"
#include "scopedLock.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#endif

namespace red
{
namespace memory
{
namespace  
{
	// TODO. Orbis sceKernelReserveVirtualRange doesn't work. and I'm too lazy to write a full fledge page allocator.
	// However we have a terabyte of address range.... 
	// Also, sceKernelMapDirectMemory will search from starting address, so it "should" skip mapped address safely.

	//const u64 c_systemReserveMemoryMapRange = SCE_KERNEL_APP_MAP_AREA_START_ADDR;
	const u64 c_orbisSystemPageSize = 64 * 1024;	// Make it 64KB even though it could be 16KB. Less bookkeeping cost.
	
	const u64 c_orbisStartRange = SCE_KERNEL_APP_MAP_AREA_START_ADDR;
	const u64 c_orbisEndRange = SCE_KERNEL_APP_MAP_AREA_END_ADDR;
}

	SystemPageAllocatorOrbis::SystemPageAllocatorOrbis()
		: m_freeRangeCount( 0 )
	{}

	SystemPageAllocatorOrbis::~SystemPageAllocatorOrbis()
	{}

	void SystemPageAllocatorOrbis::OnInitialize()
	{
		SetPageSize( c_orbisSystemPageSize );
	
		VirtualRange range = { c_orbisStartRange, c_orbisEndRange };
		m_freeRanges.Front() = range;
		++m_freeRangeCount;
	}

	VirtualRange SystemPageAllocatorOrbis::OnReserveRange( u64 size, u32 pageSize, u32 flags ) 
	{
		const u64 sizeRoundedToPageSize = RoundUp( size, static_cast< u64 >( pageSize ) );
		VirtualRange result = { 0, 0 };

		{
			ScopedLock< Mutex > scopedLock( m_lock );
			for( u32 index = 0; index != m_freeRangeCount; ++index )
			{
				VirtualRange & range = m_freeRanges[ index ];
				const u64 rangeSize = GetVirtualRangeSize( range );
				if( sizeRoundedToPageSize <= rangeSize )
				{
					result = { range.start, range.start + sizeRoundedToPageSize };
					range.start = result.end;
					
					if( !GetVirtualRangeSize( range ) )
					{
						std::move(	m_freeRanges.Begin() + index + 1, 
									m_freeRanges.Begin() + m_freeRangeCount,
									m_freeRanges.Begin() + index );
						--m_freeRangeCount;
					}

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
					sceMatReserveVirtualRange( reinterpret_cast< void* >( result.start ), GetVirtualRangeSize( result ), 0, pageSize );
#endif

					break;
				}
			}
		}

		return result;
	}

	VirtualRange SystemPageAllocatorOrbis::OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment )
	{
		const u64 sizeRoundedToPageSize = RoundUp( size, static_cast< u64 >( pageSize ) );

		VirtualRange result = NullVirtualRange();
		VirtualRange newFreeRange = NullVirtualRange();
		auto matchedRange = m_freeRanges.End();

		ScopedLock< Mutex > scopedLock( m_lock );
		for ( u32 index = 0; index < m_freeRangeCount; ++index )
		{
			VirtualRange & range = m_freeRanges[index];

			const u64 allocationAddress = RoundUp( range.start, static_cast< u64 >( alignment ) );
			const u64 rangeSize = range.end > allocationAddress ? range.end - allocationAddress : 0;
			if ( sizeRoundedToPageSize <= rangeSize )
			{
				result = { allocationAddress, allocationAddress + sizeRoundedToPageSize };
				if ( allocationAddress == range.start )
				{
					// range.start has the same alignment as allocationAddress

					range.start = result.end;
					RED_MEMORY_ASSERT( range.start <= range.end, "Invalid memory range" );

					if ( !GetVirtualRangeSize( range ) )
					{
						std::move( m_freeRanges.Begin() + index + 1,
							m_freeRanges.Begin() + m_freeRangeCount,
							m_freeRanges.Begin() + index );
						--m_freeRangeCount;
					}
				}
				else
				{
					// range.start has different alignment than allocationAddress

					const u64 newFreeRangeSize = range.end - result.end;
					if ( newFreeRangeSize > 0 )
					{
						matchedRange = index + 1 < m_freeRangeCount ? m_freeRanges.Begin() + index + 1 : m_freeRanges.End();
						newFreeRange = { result.end, range.end };
					}

					range.end = allocationAddress;
				}

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
				sceMatReserveVirtualRange( reinterpret_cast< void* >( result.start ), GetVirtualRangeSize( result ), 0, alignment );
#endif

				break;
			}
		}

		if ( newFreeRange != NullVirtualRange() )
		{
			RED_MEMORY_ASSERT( m_freeRangeCount < m_freeRanges.Size(), "Out of bound allocation list." );
			if ( matchedRange != m_freeRanges.End() )
			{
				std::move_backward( matchedRange, m_freeRanges.Begin() + m_freeRangeCount, m_freeRanges.Begin() + m_freeRangeCount + 1 );
				*matchedRange = newFreeRange;
				++m_freeRangeCount;
			}
			else
			{
				m_freeRanges[m_freeRangeCount++] = newFreeRange;
			}
		}

		return result;
	}

	void SystemPageAllocatorOrbis::OnReleaseRange( const VirtualRange & range )
	{
		ScopedLock< Mutex > scopedLock( m_lock );

		auto beginIter = m_freeRanges.Begin();
		auto endIter = m_freeRanges.Begin() + m_freeRangeCount;
		auto iter = std::lower_bound( beginIter, endIter, range );

		RED_MEMORY_ASSERT( iter != endIter, "Out of Bound VirtualRange. Last free range is the absolute limit." );

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatUnmapMemory( reinterpret_cast< void* >( range.start ), GetVirtualRangeSize( range ) );
#endif

		VirtualRange & previous = iter == beginIter? *iter : *( iter - 1 );
		VirtualRange & next = iter == beginIter ? *beginIter : *iter;

		if( iter != beginIter && previous.end == range.start )
		{
			// Can merge with previous.
			previous.end = range.end;
			// Can merge also with next ?
			if( next.start == previous.end )
			{
				previous.end = next.end;
				std::move( iter + 1, endIter, iter );
				--m_freeRangeCount;
			}
		}
		else if( next.start == range.end )
		{
			// Can't merge with previous, but can merge with next.
			next.start = range.start;
		}
		else
		{
			RED_MEMORY_ASSERT( m_freeRangeCount < m_freeRanges.Size(), "Too many free ranges." );

			// Can't merge with previous or next.
			std::move_backward( iter, endIter, endIter + 1 );
			*iter = range;

			++m_freeRangeCount;
		}
	}

	SystemPageAllocatorOrbis::FreeRangesContainer::const_iterator SystemPageAllocatorOrbis::InternalFreeRangesBegin() const
	{
		return m_freeRanges.Begin();
	}

	SystemPageAllocatorOrbis::FreeRangesContainer::const_iterator SystemPageAllocatorOrbis::InternalFreeRangesEnd() const
	{
		return m_freeRanges.Begin() + m_freeRangeCount;
	}
}
}
