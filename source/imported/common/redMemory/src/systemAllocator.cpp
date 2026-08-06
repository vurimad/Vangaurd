/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemAllocator.h"
#include "systemPageAllocator.h"
#include "systemOOMHandler.h"

namespace red
{
namespace memory
{
	SystemAllocator::SystemAllocator()
		:	m_commitedMemoryInBytes( 0 ),
			m_pageAllocator( nullptr ),
			m_oomHandler( nullptr )
	{}

	SystemAllocator::~SystemAllocator()
	{}

	void SystemAllocator::Initialize( SystemPageAllocator * pageAllocator, SystemOOMHandler* oomHandler )
	{
		RED_MEMORY_ASSERT( pageAllocator, "INTERNAL ERROR. PageAllocator is null." );
		RED_MEMORY_ASSERT( oomHandler, "INTERNAL ERROR. SystemOOMHandler is null." );
		m_pageAllocator = pageAllocator;
		m_oomHandler = oomHandler;
		OnInitialize();
	}

	VirtualRange SystemAllocator::ReserveVirtualRange( u64 size, u32 flags )
	{
		RED_MEMORY_ASSERT( m_pageAllocator, "INTERNAL ERROR. PageAllocator is null." );
		RED_MEMORY_ASSERT( size, "Cannot reserve range of size 0." );
		return m_pageAllocator->ReserveRange( size, flags );
	}

	VirtualRange SystemAllocator::ReserveAlignedVirtualRange( u64 size, u32 flags, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_pageAllocator, "INTERNAL ERROR. PageAllocator is null." );
		RED_MEMORY_ASSERT( size, "Cannot reserve range of size 0." );
		return m_pageAllocator->ReserveAlignedRange( size, flags, alignment );
	}

	void SystemAllocator::ReleaseVirtualRange( const VirtualRange & block )
	{
		RED_MEMORY_ASSERT( m_pageAllocator, "INTERNAL ERROR. PageAllocator is null." );

		if( block != NullVirtualRange() )
		{
			const u64 memoryDecommited = OnReleaseVirtualRange( block );
			atomic::ExchangeAdd64( &m_commitedMemoryInBytes, -static_cast< i64 >( memoryDecommited ) );
			m_pageAllocator->ReleaseRange( block );
		}
	}

	SystemBlock SystemAllocator::Commit( const SystemBlock & block, u32 flags )
	{
		const SystemBlock result = OnCommit( block, flags );
		if ( result == NullSystemBlock() && !( flags & Flags_Skip_System_OOM ) )
		{
			m_oomHandler->HandleSystemCommitFailure( block.size, static_cast< u32 >( GetPageSize() ) );
			return result;
		}

		atomic::ExchangeAdd64( &m_commitedMemoryInBytes, result.size );
		return result;
	}

	SystemBlock SystemAllocator::CommitAligned( const SystemBlock & block, u32 flags, u32 alignment )
	{
		const SystemBlock result = OnCommitAligned( block, flags, alignment );
		if ( result == NullSystemBlock() && !( flags & Flags_Skip_System_OOM ) )
		{
			m_oomHandler->HandleSystemCommitFailure( block.size, alignment );
			return result;
		}

		atomic::ExchangeAdd64( &m_commitedMemoryInBytes, result.size );
		return result;
	}

	void SystemAllocator::Decommit( const SystemBlock & block )
	{
		OnDecommit( block );
		atomic::ExchangeAdd64( &m_commitedMemoryInBytes, -static_cast< i64 >( block.size ) );
	}

	void SystemAllocator::PartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		OnPartialDecommit( block, partialBlock );
		atomic::ExchangeAdd64( &m_commitedMemoryInBytes, -static_cast< i64 >( partialBlock.size ) );
	}

	void SystemAllocator::WriteReportToLog() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		OnWriteReportToLog();
#endif
	}

	void SystemAllocator::WriteReportToJson( FILE* file ) const
	{
		RED_UNUSED( file );

#ifdef RED_MEMORY_ENABLE_REPORT
		std::fprintf( file, "\"committed_kb\":%.2f", m_commitedMemoryInBytes / 1024.0f );
		std::fprintf( file, ",\"available_kb\":%.2f", GetCurrentPageMemoryAvailable() / 1024.0f );
		OnWriteReportToJson( file );
#endif
	}

	u64 SystemAllocator::GetTotalPhysicalMemoryAvailable() const
	{
		return OnGetTotalPhysicalMemoryAvailable();
	}

	u64 SystemAllocator::GetCurrentPhysicalMemoryAvailable() const
	{
		return OnGetTotalPhysicalMemoryAvailable() - m_commitedMemoryInBytes;
	}

	u64 SystemAllocator::GetCurrentPageMemoryAvailable() const
	{
		return OnGetCurrentPageMemoryAvailable();
	}

	u64 SystemAllocator::GetPageSize() const
	{
		return OnGetPageSize();
	}
}
}
