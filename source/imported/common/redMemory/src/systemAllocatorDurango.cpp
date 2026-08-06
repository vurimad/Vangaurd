/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemAllocatorDurango.h"
#include "utils.h"
#include "flags.h"
#include "vault.h"

namespace red
{
namespace memory
{

namespace dd
{
	static red::CrashData< Int32 > s_commitFailedErrorCode{ "Engine", "CommitFailedErrorCode", 0 };
}

namespace 
{
	const u64 c_durangoSystemPageSize = 64 * 1024; 
	const u64 c_durangoPageAlignment = 4 * 1024;

	u32 ComputePageProtectionFlags( u32 flags )
	{
		u32 result = 0;
		if( flags & Flags_GPU_Read_Write ) // CPU/GPU visibility
		{
			if( !( flags & Flags_GPU_Write ) )
			{
				result |= PAGE_GPU_READONLY; // Read Only GPU memory, not visible by CPU
			}
			
			if( flags & Flags_GPU_Write )
			{
				result |= PAGE_GPU_COHERENT | PAGE_READWRITE;
			}

			if( flags & Flags_GPU_Coherent )
			{
				result |= PAGE_GPU_COHERENT;
			}
		}
		
		if( flags & Flags_CPU_Write )
		{
			result |= PAGE_READWRITE;
		}
		else if( flags & Flags_CPU_Read )
		{
			result |= PAGE_READONLY;
		}

		return result ? result : PAGE_NOACCESS;
	}

	u32 ComputeCommitFlags( u32 flags )
	{
		u32 result = MEM_COMMIT | MEM_LARGE_PAGES;

		if( flags & Flags_GPU_Read_Write )
		{
			result |= MEM_GRAPHICS;
		}

		return result;
	}
}

	SystemAllocatorDurango::SystemAllocatorDurango()
		: m_totalPhysicalMemoryAvailable( 0 )
	{}

	SystemAllocatorDurango::~SystemAllocatorDurango()
	{}

	void SystemAllocatorDurango::OnInitialize()
	{
		TITLEMEMORYSTATUS titleMemoryStatus;
		std::memset( &titleMemoryStatus, 0, sizeof( TITLEMEMORYSTATUS ) );
		titleMemoryStatus.dwLength = sizeof( titleMemoryStatus );
		BOOL result = TitleMemoryStatus( &titleMemoryStatus );
	
		RED_MEMORY_ASSERT( result != 0, "SYSTEM ERROR cannot fetch Durango available memory." );
		RED_UNUSED( result );

		m_totalPhysicalMemoryAvailable = titleMemoryStatus.ullAvailMem;
	}

	u64 SystemAllocatorDurango::OnReleaseVirtualRange( const VirtualRange & range )
	{
		u64 commitedMemory = 0;
		u64 currentAddress = range.start;
		u64 queryResult = 0;

		do
		{
			const void * address = reinterpret_cast< const void * >( currentAddress );
			MEMORY_BASIC_INFORMATION info;
			queryResult = VirtualQuery( address, &info, sizeof( info ) );
			if( queryResult )
			{
				if( info.State & MEM_COMMIT )
				{
					commitedMemory += info.RegionSize;
				}

				currentAddress = AddressOf( info.BaseAddress ) + info.RegionSize;
			}
		}
		while( queryResult && currentAddress < range.end );

		return commitedMemory;
	}
	
	SystemBlock SystemAllocatorDurango::OnCommit( const SystemBlock & block, u32 flags )
	{
		void * ptr = reinterpret_cast< void* >( block.address );
		const u32 protectFlags = ComputePageProtectionFlags( flags );
		const u32 commitFlags = ComputeCommitFlags( flags );
		const u64 size = RoundUp( block.size, c_durangoSystemPageSize );

		void * result = ::VirtualAlloc( ptr, size, commitFlags, protectFlags );

		if( result )
		{	
			SystemBlock resultBlock = { reinterpret_cast< u64 >( ptr ), size };
			return resultBlock;
		}
		else
		{
			const DWORD errorCode = GetLastError();
			dd::s_commitFailedErrorCode.Set( errorCode );
			RED_LOG_ERROR( "Failed to commit system pages (%llu bytes), errorCode: %d", size, errorCode );
		}
	
		return NullSystemBlock();
	}

	SystemBlock SystemAllocatorDurango::OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment )
	{
		if ( alignment == c_durangoSystemPageSize )
		{
			return OnCommit( block, flags );
		}
		else
		{
			RED_MEMORY_HALT( "OnCommitAligned with alignment different that pag size is not available on Durango" );
			return NullSystemBlock();
		}
	}
	
	void SystemAllocatorDurango::OnDecommit( const SystemBlock & block )
	{
		void * ptr = reinterpret_cast< void* >( block.address );
		::VirtualFree( ptr, RoundUp( block.size, c_durangoSystemPageSize ), MEM_DECOMMIT );
	}

	void SystemAllocatorDurango::OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		RED_MEMORY_ASSERT( partialBlock.address >= block.address && partialBlock.address + partialBlock.size <= block.address + block.size, "Partial block needs to fit in to given block." );
		(void)block;
		OnDecommit( partialBlock );
	}

	u64 SystemAllocatorDurango::OnGetTotalPhysicalMemoryAvailable() const
	{
		return m_totalPhysicalMemoryAvailable;
	}

	u64 SystemAllocatorDurango::OnGetCurrentPageMemoryAvailable() const
	{
		TITLEMEMORYSTATUS titleMemoryStatus;
		std::memset( &titleMemoryStatus, 0, sizeof( TITLEMEMORYSTATUS ) );
		titleMemoryStatus.dwLength = sizeof( titleMemoryStatus );
		TitleMemoryStatus( &titleMemoryStatus );
		return titleMemoryStatus.ullAvailMem;
	}

	u64 SystemAllocatorDurango::OnGetPageSize() const
	{
		return c_durangoSystemPageSize;
	}

	void SystemAllocatorDurango::OnWriteReportToLog() const
	{
		TITLEMEMORYSTATUS titleMemoryStatus;
		std::memset( &titleMemoryStatus, 0, sizeof( TITLEMEMORYSTATUS ) );
		titleMemoryStatus.dwLength = sizeof( titleMemoryStatus );

		RED_MEMORY_LOG( "Memory: System Allocator Informations" );
		RED_MEMORY_LOG( "Memory: \tMemory Committed: %" PRIi64, m_commitedMemoryInBytes );
		RED_MEMORY_LOG( "Memory: \tMemory Available: %" PRIu64, GetCurrentPageMemoryAvailable() );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		if( TitleMemoryStatus( &titleMemoryStatus ) != 0 )
		{
			RED_MEMORY_LOG( "Memory: \tDurango TITLEMEMORYSTATUS" );
			RED_MEMORY_LOG( "Memory: \t\tullTotalMem: %llu",			titleMemoryStatus.ullTotalMem );
			RED_MEMORY_LOG( "Memory: \t\tullAvailMem: %llu",			titleMemoryStatus.ullAvailMem );
			RED_MEMORY_LOG( "Memory: \t\tullLegacyUsed: %llu",			titleMemoryStatus.ullLegacyUsed );
			RED_MEMORY_LOG( "Memory: \t\tullLegacyPeak: %llu",			titleMemoryStatus.ullLegacyPeak );
			RED_MEMORY_LOG( "Memory: \t\tullLegacyAvail: %llu",			titleMemoryStatus.ullLegacyAvail );
			RED_MEMORY_LOG( "Memory: \t\tullTitleUsed: %llu",			titleMemoryStatus.ullTitleUsed );
			RED_MEMORY_LOG( "Memory: \t\tullTitleAvail: %llu",			titleMemoryStatus.ullTitleAvail );
			RED_MEMORY_LOG( "Memory: \t\tullLegacyPageTableUsed: %llu",	titleMemoryStatus.ullLegacyPageTableUsed );
			RED_MEMORY_LOG( "Memory: \t\tullTitlePageTableUsed: %llu", 	titleMemoryStatus.ullTitlePageTableUsed );
		}
		else
		{
			RED_MEMORY_LOG( "Memory: \tDurango TITLEMEMORYSTATUS *** call to TitleMemoryStatus() failed ***" );
		}

		// Check if we're seeing the same issue here
		// https://forums.xboxlive.com/questions/9138/malloc-and-virtualalloc-behave-in-a-leaky-way.html
		MEMORYSTATUSEX memStatus;
		memStatus.dwLength = sizeof( memStatus );
		if( GlobalMemoryStatusEx( &memStatus ) != 0 )
		{
			RED_MEMORY_LOG( "Memory: \tDurango MEMORYSTATUSEX" );
			RED_MEMORY_LOG( "Memory: \t\tdwMemoryLoad=%u",				memStatus.dwMemoryLoad );
			RED_MEMORY_LOG( "Memory: \t\tullTotalPhys=%llu",			memStatus.ullTotalPhys );
			RED_MEMORY_LOG( "Memory: \t\tullAvailPhys=%llu",			memStatus.ullAvailPhys );
			RED_MEMORY_LOG( "Memory: \t\tullTotalPageFile=%llu",		memStatus.ullTotalPageFile );
			RED_MEMORY_LOG( "Memory: \t\tullAvailPageFile=%llu",		memStatus.ullAvailPageFile );
			RED_MEMORY_LOG( "Memory: \t\tullTotalVirtual=%llu",			memStatus.ullTotalVirtual );
			RED_MEMORY_LOG( "Memory: \t\tullAvailVirtual=%llu",			memStatus.ullAvailVirtual );
			RED_MEMORY_LOG( "Memory: \t\tullAvailExtendedVirtual=%llu",	memStatus.ullAvailExtendedVirtual );
		}
		else
		{
			RED_MEMORY_LOG( "Memory: \tDurango MEMORYSTATUSEX *** call to GlobalMemoryStatusEx() failed ***" );
		}
	}

	void SystemAllocatorDurango::OnWriteReportToJson( FILE* file ) const
	{
		TITLEMEMORYSTATUS titleMemoryStatus;
		std::memset( &titleMemoryStatus, 0, sizeof( TITLEMEMORYSTATUS ) );
		titleMemoryStatus.dwLength = sizeof( titleMemoryStatus );
		if( TitleMemoryStatus( &titleMemoryStatus ) != 0 )
		{
			std::fprintf( file,
				",\"title_memory_status\":{\"ullTotalMem\":%llu,\"ullAvailMem\":%llu,\"ullLegacyUsed\":%llu,\"ullLegacyPeak\":%llu,\"ullLegacyAvail\":%llu,\"ullTitleUsed\":%llu,\"ullTitleAvail\":%llu,\"ullLegacyPageTableUsed\":%llu,\"ullTitlePageTableUsed\":%llu}",
				titleMemoryStatus.ullTotalMem,
				titleMemoryStatus.ullAvailMem,
				titleMemoryStatus.ullLegacyUsed,
				titleMemoryStatus.ullLegacyPeak,
				titleMemoryStatus.ullLegacyAvail,
				titleMemoryStatus.ullTitleUsed,
				titleMemoryStatus.ullTitleAvail,
				titleMemoryStatus.ullLegacyPageTableUsed,
				titleMemoryStatus.ullTitlePageTableUsed );
		}

		// Check if we're seeing the same issue here
		// https://forums.xboxlive.com/questions/9138/malloc-and-virtualalloc-behave-in-a-leaky-way.html
		MEMORYSTATUSEX memStatus;
		memStatus.dwLength = sizeof( memStatus );
		if( GlobalMemoryStatusEx( &memStatus ) != 0 )
		{
			std::fprintf( file,
				",\"global_memory_statusex\":{\"dwMemoryLoad\":%u,\"ullTotalPhys\":%llu,\"ullAvailPhys\":%llu,\"ullTotalPageFile\":%llu,\"ullAvailPageFile\":%llu,\"ullTotalVirtual\":%llu,\"ullAvailVirtual\":%llu,\"ullAvailExtendedVirtual\":%llu}",
				memStatus.dwMemoryLoad,
				memStatus.ullTotalPhys,
				memStatus.ullAvailPhys,
				memStatus.ullTotalPageFile,
				memStatus.ullAvailPageFile,
				memStatus.ullTotalVirtual,
				memStatus.ullAvailVirtual,
				memStatus.ullAvailExtendedVirtual );
		}
	}
}
}
