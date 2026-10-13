/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemPageAllocator.h"
#include "assert.h"
#include "utils.h"
#include "systemOOMHandler.h"

namespace red
{
namespace memory
{
	SystemPageAllocator::SystemPageAllocator()
		:	m_platformPageSize( 0 ),
			m_pageReservedCount( 0 ),
			m_oomHandler( nullptr )
	{}

	SystemPageAllocator::~SystemPageAllocator()
	{}

	void SystemPageAllocator::Initialize( SystemOOMHandler* oomHandler )
	{
		RED_MEMORY_ASSERT( oomHandler, "SystemOOMHandler is null" );
		m_oomHandler = oomHandler;
		OnInitialize();
	}

	VirtualRange SystemPageAllocator::ReserveRange( u64 size, u32 flags )
	{
		RED_MEMORY_ASSERT( m_platformPageSize, "INTERNAL ERROR. Page size is 0." );
		RED_MEMORY_ASSERT( IsPowerOf2( m_platformPageSize ), "INTERNAL ERROR. Page size is not power of 2." );

		const VirtualRange range = OnReserveRange( size, m_platformPageSize, flags );
		if ( range == NullVirtualRange() && !( flags & Flags_Skip_System_OOM ) )
		{
			m_oomHandler->HandleSystemReservePagesFailure( size, m_platformPageSize, m_platformPageSize );
			return range;
		}

		const atomic::TAtomic32 pageCount = static_cast< atomic::TAtomic32 >( GetVirtualRangeSize( range ) / m_platformPageSize );
		atomic::ExchangeAdd32( &m_pageReservedCount, pageCount ); 
		return range;
	}

	VirtualRange SystemPageAllocator::ReserveAlignedRange( u64 size, u32 flags, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_platformPageSize, "INTERNAL ERROR. Page size is 0." );
		RED_MEMORY_ASSERT( IsPowerOf2( m_platformPageSize ), "INTERNAL ERROR. Page size is not power of 2." );
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Alignment is not power of 2." );
		RED_MEMORY_ASSERT( alignment >= m_platformPageSize, "Alignment is not greater or equal to page size." );

		const VirtualRange range = OnReserveAlignedRange( size, m_platformPageSize, flags, alignment );
		if ( range == NullVirtualRange() && !( flags & Flags_Skip_System_OOM ) )
		{
			m_oomHandler->HandleSystemReservePagesFailure( size, alignment, m_platformPageSize );
			return range;
		}

		const atomic::TAtomic32 pageCount = static_cast< atomic::TAtomic32 >( GetVirtualRangeSize( range ) / m_platformPageSize );
		atomic::ExchangeAdd32( &m_pageReservedCount, pageCount ); 
		return range;
	}

	void SystemPageAllocator::ReleaseRange( const VirtualRange & range )
	{
		RED_MEMORY_ASSERT( m_platformPageSize, "INTERNAL ERROR. Page size is 0." );
		RED_MEMORY_ASSERT( IsPowerOf2( m_platformPageSize ), "INTERNAL ERROR. Page size are not power of 2." );

		if( range != NullVirtualRange() )
		{
			OnReleaseRange( range );
			const atomic::TAtomic32 pageCount = static_cast< atomic::TAtomic32 >( GetVirtualRangeSize( range ) / m_platformPageSize );
			atomic::ExchangeAdd32( &m_pageReservedCount, -pageCount ); 
		}
	}

	u32 SystemPageAllocator::GetPageSize() const
	{
		return m_platformPageSize;
	}

	u32 SystemPageAllocator::GetReservedPageCount() const
	{
		return m_pageReservedCount;
	}

	void SystemPageAllocator::SetPageSize( u32 pageSize )
	{
		m_platformPageSize = pageSize;
	}
}
}
