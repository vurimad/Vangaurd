/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemAllocatorWin.h"
#include "assert.h"
#include "utils.h"
#include "flags.h"
#include "../../redSystem/include/processUtility.h"
#include <psapi.h>

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
	u32 ComputePageProtectionFlags( u32 flags )
	{
		u32 result = 0;
		if( flags & Flags_CPU_Write )
		{
			result |= PAGE_READWRITE;
		}
		else if( flags & Flags_CPU_Read )
		{
			result |= PAGE_READONLY;
		}	
		else
		{
			result |= PAGE_NOACCESS;
		}

		return result;
	}

}

	SystemAllocatorWin::SystemAllocatorWin()
		:	m_pageSize( 0 ),
			m_totalPhysicalMemoryAvailable( 0 )
	{
		SYSTEM_INFO sysInfo;
		::GetSystemInfo( &sysInfo );

		// On Windows machines, allocations are aligned to dwAllocationGranularity, but page size is dwPageSize
		// and is generally smaller
		// In order to reduce virtual memory fragmentation, we will allocate in chunks of dwAllocationGranularity
		// otherwise, we will introduce holes
		m_pageSize = sysInfo.dwAllocationGranularity;
	}

	SystemAllocatorWin::~SystemAllocatorWin()
	{}

	void SystemAllocatorWin::OnInitialize()
	{
		MEMORYSTATUS status;
		status.dwLength = sizeof( status );
		GlobalMemoryStatus( &status );
		m_totalPhysicalMemoryAvailable = status.dwAvailPhys;
	}

	u64 SystemAllocatorWin::OnReleaseVirtualRange( const VirtualRange & range )
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
	
	SystemBlock SystemAllocatorWin::OnCommit( const SystemBlock & block, u32 flags )
	{
		void * ptr = reinterpret_cast< void* >( block.address );
		const u64 size = RoundUp( block.size, m_pageSize );
		void * result = ::VirtualAlloc( ptr, size, MEM_COMMIT, ComputePageProtectionFlags( flags )  );

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

	SystemBlock SystemAllocatorWin::OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment )
	{
		if ( alignment == m_pageSize )
		{
			return OnCommit( block, flags );
		}
		else
		{
			RED_MEMORY_HALT( "OnCommitAligned with alignment different that page size is not available on Win(PC)" );
			return NullSystemBlock();
		}
	}
	
	void SystemAllocatorWin::OnDecommit( const SystemBlock & block )
	{
		void * ptr = reinterpret_cast< void* >( block.address );
		::VirtualFree( ptr, block.size, MEM_DECOMMIT );
	}

	void SystemAllocatorWin::OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		RED_MEMORY_ASSERT( partialBlock.address >= block.address && partialBlock.address + partialBlock.size <= block.address + block.size, "Partial block needs to fit in to given block." );
		(void)block;
		OnDecommit( partialBlock );
	}

	u64 SystemAllocatorWin::OnGetTotalPhysicalMemoryAvailable() const
	{
		return m_totalPhysicalMemoryAvailable;
	}

	u64 SystemAllocatorWin::OnGetCurrentPageMemoryAvailable() const
	{
		MEMORYSTATUS status;
		std::memset( &status, 0, sizeof( MEMORYSTATUS ) );
		status.dwLength = sizeof( status );
		GlobalMemoryStatus( &status );
		return status.dwAvailPageFile;
	}

	u64 SystemAllocatorWin::OnGetPageSize() const
	{
		return m_pageSize;
	}

	static void LogProcessMemoryInfo( const PROCESS_MEMORY_COUNTERS &pmc )
	{
		RED_TOUCH( pmc );
		RED_MEMORY_LOG( "Memory: \t\tPageFaultCount: %lu", pmc.PageFaultCount );
		RED_MEMORY_LOG( "Memory: \t\tPeakWorkingSetSize: %llu", pmc.PeakWorkingSetSize );
		RED_MEMORY_LOG( "Memory: \t\tWorkingSetSize: %llu", pmc.WorkingSetSize );
		RED_MEMORY_LOG( "Memory: \t\tQuotaPeakPagedPoolUsage: %llu", pmc.QuotaPeakPagedPoolUsage );
		RED_MEMORY_LOG( "Memory: \t\tQuotaPagedPoolUsage: %llu", pmc.QuotaPagedPoolUsage );
		RED_MEMORY_LOG( "Memory: \t\tQuotaPeakNonPagedPoolUsage: %llu", pmc.QuotaPeakNonPagedPoolUsage );
		RED_MEMORY_LOG( "Memory: \t\tQuotaNonPagedPoolUsage: %llu", pmc.QuotaNonPagedPoolUsage );
		RED_MEMORY_LOG( "Memory: \t\tPagefileUsage: %llu", pmc.PagefileUsage );
		RED_MEMORY_LOG( "Memory: \t\tPeakPagefileUsage: %llu", pmc.PeakPagefileUsage );
	}

	static Bool IsProcessMemoryInforGreaterEqualThan( const PROCESS_MEMORY_COUNTERS &pmc, Int32 memorySize )
	{
		return pmc.PeakWorkingSetSize >= memorySize
			|| pmc.WorkingSetSize >= memorySize
			|| pmc.QuotaPeakPagedPoolUsage >= memorySize
			|| pmc.QuotaPagedPoolUsage >= memorySize
			|| pmc.QuotaPeakNonPagedPoolUsage >= memorySize
			|| pmc.QuotaNonPagedPoolUsage >= memorySize
			|| pmc.PagefileUsage >= memorySize
			|| pmc.PeakPagefileUsage >= memorySize;
	}

	static void SumProcessMemoryInfo( PROCESS_MEMORY_COUNTERS& outSum, const PROCESS_MEMORY_COUNTERS& pmc )
	{
		outSum.PageFaultCount += pmc.PageFaultCount;
		outSum.PeakWorkingSetSize += pmc.PeakWorkingSetSize;
		outSum.WorkingSetSize += pmc.WorkingSetSize;
		outSum.QuotaPeakPagedPoolUsage += pmc.QuotaPeakPagedPoolUsage;
		outSum.QuotaPagedPoolUsage += pmc.QuotaPagedPoolUsage;
		outSum.QuotaPeakNonPagedPoolUsage += pmc.QuotaPeakNonPagedPoolUsage;
		outSum.QuotaNonPagedPoolUsage += pmc.QuotaNonPagedPoolUsage;
		outSum.PagefileUsage += pmc.PagefileUsage;
		outSum.PeakPagefileUsage += pmc.PeakPagefileUsage;
	}

	static Bool GatherProcessMemoryInfo( DWORD processID, char ( &processName )[ MAX_PATH ], wchar_t ( &processCommandLine )[ 1024 ], PROCESS_MEMORY_COUNTERS& pmc )
	{
		HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID );
		if( hProcess == NULL )
		{
			return false;
		}

		Bool result = true;

		GetProcessName( hProcess, processName );

		if( !GetProcessMemoryInfo( hProcess, &pmc, sizeof( pmc ) ) )
		{
			result = false;
		}

		GetProcessCommandLine( hProcess, processCommandLine );

		CloseHandle( hProcess );
		return result;
	}

	static void PrintProcessesMemoryInfo()
	{
		DWORD processIds[ 1024 ];
		DWORD processesSizeBytes;

		if( !EnumProcesses( processIds, sizeof( processIds ), &processesSizeBytes ) )
		{
			RED_MEMORY_LOG( "Memory: Failed to enumerate processes for printing of memory info. (GetLastError() = %X)", GetLastError() );
			return;
		}
		
		PROCESS_MEMORY_COUNTERS otherPmcSum = {};
		Uint32 otherCount = 0;


		const Int32 processesCount = processesSizeBytes / sizeof( DWORD );
		for( Int32 i = 0; i < processesCount; ++i )
		{
			const DWORD processID = processIds[ i ];
			char processName[ MAX_PATH ] = "<unknown>";
			wchar_t processCommandLine[ 1024 ] = L"";
			PROCESS_MEMORY_COUNTERS pmc = {};
			if( GatherProcessMemoryInfo( processID, processName, processCommandLine, pmc ) )
			{
				const Bool isCurrentProcess = GetCurrentProcessId() == processID;
				const Bool isOneGbProcess = IsProcessMemoryInforGreaterEqualThan( pmc, 1000 * 1000 * 1000 );
#if !defined( RED_CONFIGURATION_FINAL ) // Do not log detailed information about other processes in Final version.
				const Bool logProcessMemory = isCurrentProcess || isOneGbProcess;
#else
				const Bool logProcessMemory = isCurrentProcess;
				RED_UNUSED( isOneGbProcess );
#endif
				if( logProcessMemory )
				{
					RED_MEMORY_LOG( "Memory: \t%hs%hs (%X)", ( isCurrentProcess ? "(current process)" : "" ), processName, processID );
#if !defined( RED_CONFIGURATION_FINAL ) // Do not log command line as it can contain user folder name.
					RED_MEMORY_LOG( "Memory: \t\tCMD: %ls", processCommandLine );
#endif
					LogProcessMemoryInfo( pmc );
				}
				else
				{
					++otherCount;
					SumProcessMemoryInfo( otherPmcSum, pmc );
				}
			}
		}

		if( otherCount )
		{
			RED_MEMORY_LOG( "Memory: \tOther, count: %u", otherCount );
			LogProcessMemoryInfo( otherPmcSum );
		}
	}

	void SystemAllocatorWin::OnWriteReportToLog() const
	{
		MEMORYSTATUS status;
		status.dwLength = sizeof( status );
		GlobalMemoryStatus( &status );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		RED_MEMORY_LOG( "Memory: \tWindows(PC) MEMORYSTATUS" );
		RED_MEMORY_LOG( "Memory: \t\tdwMemoryLoad: %lu", status.dwMemoryLoad );
		RED_MEMORY_LOG( "Memory: \t\tdwTotalPhys: %llu", status.dwTotalPhys );
		RED_MEMORY_LOG( "Memory: \t\tdwAvailPhys: %llu", status.dwAvailPhys );
		RED_MEMORY_LOG( "Memory: \t\tdwTotalPageFile: %llu", status.dwTotalPageFile );
		RED_MEMORY_LOG( "Memory: \t\tdwAvailPageFile: %llu", status.dwAvailPageFile );
		RED_MEMORY_LOG( "Memory: \t\tdwTotalVirtual: %llu", status.dwTotalVirtual );
		RED_MEMORY_LOG( "Memory: \t\tdwAvailVirtual: %llu", status.dwAvailVirtual );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		RED_MEMORY_LOG( "Memory: \tMemory usage by (other) processes, dump" );

		PrintProcessesMemoryInfo();

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );
	}

	void SystemAllocatorWin::OnWriteReportToJson( FILE* file ) const
	{
		MEMORYSTATUS status;
		status.dwLength = sizeof( status );
		GlobalMemoryStatus( &status );

		std::fprintf( file,
			",\"global_memory_status\":{\"dwMemoryLoad\":%lu,\"dwTotalPhys\":%llu,\"dwAvailPhys\":%llu,\"dwTotalPageFile\":%llu,\"dwAvailPageFile\":%llu,\"dwTotalVirtual\":%llu,\"tdwAvailVirtual\":%llu}",
			status.dwMemoryLoad,
			status.dwTotalPhys,
			status.dwAvailPhys,
			status.dwTotalPageFile,
			status.dwAvailPageFile,
			status.dwTotalVirtual,
			status.dwAvailVirtual );
	}
}
}
