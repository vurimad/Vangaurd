/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "reporter.h"
#include "assert.h"
#include "poolRegistry.h"
#include "systemAllocator.h"
#include "allocatorMetrics.h"
#include "../include/allocatorMetricsLogger.h"
#include <cstdio>

namespace red
{
namespace memory
{
#ifdef RED_MEMORY_ENABLE_REPORT
	FILE* s_oomFile = nullptr;
	FILE* s_oomJsonFile = nullptr;
#endif

namespace dd
{
	static red::CrashData< const char*, 16 > s_oomSource{ "Engine/OOM", "Source" };
	static red::CrashData< const char*, 40 > s_oomAllocatorName{ "Engine/OOM", "AllocatorName" };
	static red::CrashData< const char*, c_poolNameMaxSize > s_oomPoolName{ "Engine/OOM", "PoolName" };
	static red::CrashData< ThreadId > s_oomThreadId{ "Engine/OOM", "ThreadID" };
	static red::CrashData< Uint64 > s_oomAllocationSize{ "Engine/OOM", "AllocationSize" };
	static red::CrashData< Uint32 > s_oomAllocationAlignment{ "Engine/OOM", "AllocationAlignment" };
	static red::CrashData< Uint32 > s_oomAllocationPageSize{ "Engine/OOM", "AllocationPageSize" };
}
}
}

namespace
{
	enum class ReportFormat : red::memory::u8
	{
		ReportFormat_TXT,
		ReportFormat_JSON
	};

#ifdef RED_MEMORY_ENABLE_REPORT

	RED_INLINE constexpr const char* GetPlatformName()
	{
#if defined( RED_PLATFORM_WINPC )
		return "PC";
#elif defined( RED_PLATFORM_DURANGO )
		return "Durango";
#elif defined( RED_PLATFORM_ORBIS )
		return "Orbis";
#elif defined( RED_PLATFORM_LINUX )
		return "Linux";
#else
		return "Unknown";
#endif
	}

	const char* GetOOMReportPath( ReportFormat format, Bool useFallbackDirectory )
	{
		RED_TOUCH( useFallbackDirectory );

		const char* ext = format == ReportFormat::ReportFormat_TXT ? ".txt" : ".json";
		red::DateTime time;
		red::Clock::GetInstance().GetLocalTime( time );

		constexpr Uint32 moduleFullPathSize = 260;
		static char moduleFullPath[moduleFullPathSize];

#if defined( RED_PLATFORM_WINPC )

		char moduleFileName[MAX_PATH];
		::GetModuleFileNameA( NULL, moduleFileName, MAX_PATH );
		::GetFullPathNameA( moduleFileName, MAX_PATH, moduleFullPath, NULL );

		// Strip EXE name
		char* pEnd = red::StrchrR( moduleFullPath, '\\' );
		if ( !pEnd )
		{
			return nullptr;
		}
		*pEnd = '\0';

		if ( !std::snprintf( pEnd, moduleFullPathSize - (Uint32)red::Strlen( moduleFullPath ), "\\oom_report_%04d-%02d-%02d_%02d.%02d.%02d.%03d%s",
			time.GetYear(), time.GetMonth() + 1, time.GetDay() + 1,
			time.GetHour(), time.GetMinute(), time.GetSecond(), time.GetMilliSeconds(), ext ) )
		{
			return nullptr;
		}

		return moduleFullPath;

#elif defined( RED_PLATFORM_DURANGO )
		if ( !std::snprintf( moduleFullPath, moduleFullPathSize, "d:\\oom_report_%04d-%02d-%02d_%02d.%02d.%02d.%03d%s",
			time.GetYear(), time.GetMonth() + 1, time.GetDay() + 1,
			time.GetHour(), time.GetMinute(), time.GetSecond(), time.GetMilliSeconds(), ext ) )
		{
			return nullptr;
		}

		return moduleFullPath;
#elif defined( RED_PLATFORM_ORBIS )
		if ( !std::snprintf( moduleFullPath, moduleFullPathSize, "%soom_report_%04d-%02d-%02d_%02d.%02d.%02d.%03d%s",
			useFallbackDirectory ? "/devlog/system/" : "/data/",
			time.GetYear(), time.GetMonth() + 1, time.GetDay() + 1,
			time.GetHour(), time.GetMinute(), time.GetSecond(), time.GetMilliSeconds(), ext ) )
		{
			return nullptr;
		}

		return moduleFullPath;
#elif defined( RED_PLATFORM_LINUX )
		if ( !std::snprintf( moduleFullPath, moduleFullPathSize, "/var/log/oom_report_%04d-%02d-%02d_%02d.%02d.%02d.%03d%s",
			time.GetYear(), time.GetMonth() + 1, time.GetDay() + 1,
			time.GetHour(), time.GetMinute(), time.GetSecond(), time.GetMilliSeconds(), ext ) )
		{
			return nullptr;
		}

		return moduleFullPath;
#else
#	error Unsupported platform
#endif
	}

	Bool PrepareOOMReporter( ReportFormat format )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		Bool useFallbackDirectory = false;
		Bool retry =
#ifdef RED_PLATFORM_ORBIS
			true;
#else
			false;
#endif

		FILE** file = nullptr;
		const char* filePath = nullptr;

#ifndef RED_PLATFORM_LINUX
		int errorCode = 0;
#endif

		do
		{
			switch ( format )
			{
			case ReportFormat::ReportFormat_TXT:
				file = &red::memory::s_oomFile;
				filePath = GetOOMReportPath( ReportFormat::ReportFormat_TXT, useFallbackDirectory );
				break;

			case ReportFormat::ReportFormat_JSON:
				file = &red::memory::s_oomJsonFile;
				filePath = GetOOMReportPath( ReportFormat::ReportFormat_JSON, useFallbackDirectory );
				break;

			default:
				return false;
			}

#ifdef RED_PLATFORM_LINUX
			*file = fopen( filePath, "w+" );
			if ( !*file )
			{
				return false;
			}

#elif defined( RED_PLATFORM_ORBIS )
			errorCode = fopen_s( file, filePath, "w+" );
			if ( errorCode )
			{
				if ( !useFallbackDirectory )
				{
					useFallbackDirectory = true;
				}
				else
				{
					return false;
				}
			}
			else
			{
				retry = false;
			}
#else
			errorCode = fopen_s( file, filePath, "w+" );
			if ( errorCode )
			{
				return false;
			}
#endif
		} while( retry );


#ifdef RED_PLATFORM_ORBIS
		errorCode = sceKernelChmod( filePath, SCE_KERNEL_S_IRWU );
		if ( errorCode != SCE_OK )
		{
			std::fclose( *file );
			*file = nullptr;
			return false;
		}
#endif /* RED_PLATFORM_ORBIS */

		red::RegisterAttachmentForErrorReport( filePath );

#endif /* RED_MEMORY_ENABLE_REPORT */

		return true;
	}

	void CloseOOMReporter( ReportFormat format )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		FILE** file = nullptr;
		switch ( format )
		{
		case ReportFormat::ReportFormat_TXT:
			file = &red::memory::s_oomFile;
			break;

		case ReportFormat::ReportFormat_JSON:
			file = &red::memory::s_oomJsonFile;
			break;

		default:
			return;
		}

		std::fflush( *file );
		std::fclose( *file );
		*file = nullptr;
#endif
	}

	bool OpenCustomOOMReporter( const char* filePath )
	{
#if defined( RED_MEMORY_ENABLE_REPORT ) && defined( RED_PLATFORM_WINPC )
		FILE** file = &red::memory::s_oomFile;

		int errorCode = fopen_s( file, filePath, "w+" );
		if ( errorCode )
		{
			return false;
		}
#else
		RED_UNUSED( filePath );
#endif
		return true;
	}

	void CloseCustomOOMReporter()
	{
		CloseOOMReporter( ReportFormat::ReportFormat_TXT );
	}

#endif /* RED_MEMORY_ENABLE_REPORT */

}

namespace red
{
namespace memory
{
	Reporter::Reporter()
	{
		std::memset( &m_parameter, 0, sizeof( m_parameter ) );
	}

	Reporter::~Reporter()
	{}

	void Reporter::Initialize( const ReporterParameter & param )
	{
		m_parameter = param;

		RED_MEMORY_ASSERT( m_parameter.poolRegistry, "Pool Registry can't be null." );
		RED_MEMORY_ASSERT( m_parameter.metricsRegistry, "Metrics Registry can't be null." );
		RED_MEMORY_ASSERT( m_parameter.systemAllocator, "System Allocator can't be null." );
	}

#ifdef RED_MEMORY_ENABLE_REPORT

	void OutputTLFHistogram( const TLSFAllocatorMetrics & tlsfMetrics, u32 maxGraphWidth )
	{
		u64 maxBlockCount = 0;
		u64 maxTotalAllocSize = 0;
		u32 lastIndexToProcess = 0;
		const auto & blockMetrics = tlsfMetrics.freeBlocksMetrics;
		for( u32 index = 0; index != blockMetrics.Size(); ++index )
		{
			const auto & blockMetric = blockMetrics[ index ]; 
			maxBlockCount = std::max( maxBlockCount, blockMetric.totalCount );
			maxTotalAllocSize = std::max( maxTotalAllocSize, blockMetric.totalSize );
			lastIndexToProcess = blockMetric.totalSize != 0 ? index : lastIndexToProcess;
		}

		const float totalSizeStep = maxTotalAllocSize / static_cast< float >( maxGraphWidth );
		const float blockCountStep = maxBlockCount / static_cast< float >( maxGraphWidth );
		
		RED_MEMORY_LOG( "Memory: Free block Histogram. X -> Total Size, * -> Block Count." );

		for( u32 index = 0; index != lastIndexToProcess + 1; ++index )
		{
			const auto & blockMetric = blockMetrics[ index ]; 

			if( blockMetric.totalCount )
			{
				char sizeGraphBlock[ 128 ] = { 0 };
				char countGraphBlock[ 128 ] = { 0 };
				u32 sizeMarkerCount = static_cast< u32 >( blockMetric.totalSize / totalSizeStep );
				u32 countMarkerCount = static_cast< u32 >( blockMetric.totalCount / blockCountStep );

				std::memset( sizeGraphBlock, 'X', sizeMarkerCount );
				std::memset( countGraphBlock, '*', countMarkerCount );

				SNPrintFUnsafe( sizeGraphBlock + sizeMarkerCount,  128 - sizeMarkerCount, " %lld", blockMetric.totalSize );
				SNPrintFUnsafe( countGraphBlock + countMarkerCount, 128 - countMarkerCount, " %lld", blockMetric.totalCount );

				RED_MEMORY_LOG( "Memory: <%15" PRIu64 " |%s", blockMetric.blockSize, sizeGraphBlock );
				RED_MEMORY_LOG( "Memory:  %15s |%s", " ", countGraphBlock );
			}
		}
	}

#endif

	void Reporter::SavePoolOOMCrashData( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) const
	{
#ifdef RED_USE_CRASHDATA
		dd::s_oomSource.Set( "Pool" );

		if( poolName != nullptr )
		{
			dd::s_oomPoolName.Set( poolName );
		}

		if( allocatorName != nullptr )
		{
			dd::s_oomAllocatorName.Set( allocatorName );
		}

		dd::s_oomThreadId.Set( ThreadIdProvider().GetCurrentId() );
		dd::s_oomAllocationSize.Set( size );
		dd::s_oomAllocationAlignment.Set( alignment );
#endif
	}

	void Reporter::SaveAllocatorOOMCrashData( ProxyTypeId proxyId, u32 size, u32 alignment ) const
	{
#ifdef RED_USE_CRASHDATA
		dd::s_oomSource.Set( "Allocator" );

		if( const char* allocatorName = GetAllocatorName( proxyId ) )
		{
			dd::s_oomAllocatorName.Set( allocatorName );
		}

		dd::s_oomThreadId.Set( ThreadIdProvider().GetCurrentId() );
		dd::s_oomAllocationSize.Set( size );
		dd::s_oomAllocationAlignment.Set( alignment );
#endif
	}

	void Reporter::SaveSystemCommitOOMCrashData( u64 size, u32 alignment ) const
	{
#ifdef RED_USE_CRASHDATA
		dd::s_oomSource.Set( "SystemCommit" );
		dd::s_oomThreadId.Set( ThreadIdProvider().GetCurrentId() );
		dd::s_oomAllocationSize.Set( size );
		dd::s_oomAllocationAlignment.Set( alignment );
#endif
	}

	void Reporter::SaveSystemReserveOOMCrashData( u64 size, u32 alignment, u32 pageSize ) const
	{
#ifdef RED_USE_CRASHDATA
		dd::s_oomSource.Set( "SystemReserve" );
		dd::s_oomThreadId.Set( ThreadIdProvider().GetCurrentId() );
		dd::s_oomAllocationSize.Set( size );
		dd::s_oomAllocationAlignment.Set( alignment );
		dd::s_oomAllocationPageSize.Set( pageSize );
#endif
	}

	void Reporter::WriteReportPrefaceToLog() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		RED_MEMORY_LOG( "Memory: ***********************************************************************" );
		RED_MEMORY_LOG( "Memory: MEMORY REPORT" );
		RED_MEMORY_LOG( "Memory: ***********************************************************************" );
#endif
	}

	void GetPlatformStringForPerf( char strOut[1024] )
	{

		const char * config = "ReleaseOrDebug";
#ifdef RED_CONFIGURATION_FINAL
#ifdef USE_PROFILER
		config = "Profiling";
#else
		config = "Final";
#endif // USE_PROFILER
#else
#endif // RED_CONFIGURATION_FINAL

		const char* platform =
#if defined( RED_PLATFORM_WINPC )
			"windows"
#elif defined( RED_PLATFORM_DURANGO )
			GetConsoleType() <= CONSOLE_TYPE_XBOX_ONE_S ? "xone" : ( GetConsoleType() >= (CONSOLE_TYPE)5 ? "scarlett" : "xonex")
#elif defined( RED_PLATFORM_ORBIS )
			sceKernelIsNeoMode() ? "ps4pro" : "ps4"
#elif defined( RED_PLATFORM_LINUX )
			"linux"
#else
			"unknown"
#endif
			;

		red::SNPrintFSafe(strOut, 1024, "%s : %s", platform, config);

	}

	void Reporter::WritePoolReportToLog() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		RED_MEMORY_ASSERT( m_parameter.poolRegistry, "Pool Registry can't be null. Initialize before using Reporter." );
		RED_MEMORY_ASSERT( m_parameter.metricsRegistry, "Metrics Registry can't be null. Initialize before using Reporter." );
		RED_MEMORY_ASSERT( m_parameter.systemAllocator, "System Allocator can't be null. Initialize before using Reporter." );

		// 1) System Allocators resume. 
		// 2) Pool resume. KB Allocated, KB budget, Alloc count. Graph check.
		// 3) Pool details and Worst offender.
		// 4) Allocators resume. Pools bound to it, KB Allocated, KB budget, Alloc count.
		// 5) Allocators details. 

		m_parameter.systemAllocator->WriteReportToLog();

#ifdef RED_PLATFORM_ORBIS
		m_parameter.flexibleAllocator->WriteReportToLog();
#endif

#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS 
		char platformString[1024] = { 0 };
		GetPlatformStringForPerf(platformString);
		RED_MEMORY_LOG( "Memory: PlayTime: %f", m_reportData.playTime);
		RED_MEMORY_LOG( "Memory: Quest: %s", strlen(m_reportData.trackedQuest) > 0 ? m_reportData.trackedQuest : "No Quest" );
		RED_MEMORY_LOG( "Memory: Location: %s", strlen(m_reportData.location) > 0 ? m_reportData.location : "No Location" );
		RED_MEMORY_LOG( "Memory: Platform: %s", strlen(platformString) > 0 ? platformString : "No platform" );
		RED_MEMORY_LOG( "Memory: Cmdline: %s", strlen(m_reportData.cmdLine) > 0 ? m_reportData.cmdLine : "No cmdline" );
#endif
		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		m_parameter.poolRegistry->WritePoolReportToLog( *m_parameter.metricsRegistry );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		m_parameter.poolRegistry->WriteBoundAllocatorReportsToLog( *m_parameter.metricsRegistry );
#endif
	}

	void Reporter::WritePoolReportToJson() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		Float time = 0.f;
		const char* trackedQuest = "";
		const char* location = "";
		char platformString[1024] = { 0 };
		GetPlatformStringForPerf(platformString);
#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
		time = m_reportData.playTime;
		trackedQuest = m_reportData.trackedQuest;
		location = m_reportData.location;
#endif
		RED_MEMORY_ASSERT( s_oomJsonFile, "JSON output file does not exist" );
		RED_MEMORY_ASSERT( m_parameter.poolRegistry, "Pool Registry can't be null. Initialize before using Reporter." );
		RED_MEMORY_ASSERT( m_parameter.metricsRegistry, "Metrics Registry can't be null. Initialize before using Reporter." );
		RED_MEMORY_ASSERT( m_parameter.systemAllocator, "System Allocator can't be null. Initialize before using Reporter." );

		std::fprintf( s_oomJsonFile, "{\"platform\":\"%s\",\"trackedQuest\":\"%s\",\"time\":%f,\"location\":\"%s\",\"platform\":\"%s\",\"cmdLine\":\"%s\",\"system_allocator\":{", platformString, strlen(trackedQuest) > 0 ? trackedQuest : "No quest", time, strlen(location) > 0 ? location : "No location", GetPlatformName(), m_reportData.cmdLine );
		m_parameter.systemAllocator->WriteReportToJson( s_oomJsonFile );

#ifdef RED_PLATFORM_ORBIS
		std::fprintf( s_oomJsonFile, "},\"flexible_allocator\":{");
		m_parameter.flexibleAllocator->WriteReportToJson( s_oomJsonFile );
#endif

		std::fprintf( s_oomJsonFile, "},\"pools\":[" );
		m_parameter.poolRegistry->WritePoolReportToJson( s_oomJsonFile, *m_parameter.metricsRegistry );

		std::fprintf( s_oomJsonFile, "],\"allocators\":[" );
		m_parameter.poolRegistry->WriteBoundAllocatorReportsToJson( *m_parameter.metricsRegistry );

		if( m_parameter.dumpRTTIMetricsToJson )
		{
			std::fprintf( s_oomJsonFile, "],\"rtti\":[\n" );
			m_parameter.dumpRTTIMetricsToJson( s_oomJsonFile );
		}

		std::fprintf( s_oomJsonFile, "]}" );
#endif
	}

	void Reporter::WriteAllocatorReportToJson() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		std::fprintf( s_oomJsonFile, "},\"gpu allocator\":{");
		//WriteGpuAllocatorToJson( s_oomJsonFile );
#endif
	}

	void Reporter::WriteAllocatorReportToLog( ProxyTypeId proxyId, void* proxy ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		RED_MEMORY_ASSERT( m_parameter.systemAllocator, "System Allocator can't be null. Initialize before using Reporter." );

		LogAllocatorMetrics( proxyId, proxy );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );
#else
		RED_UNUSED( proxyId );
		RED_UNUSED( proxy );
#endif
	}

	void Reporter::WriteReportToLog() const
	{
		WriteReportPrefaceToLog();
		WritePoolReportToLog();
	}

	void Reporter::WriteReportToLog( ProxyTypeId proxyId, void* proxy ) const
	{
		WriteReportPrefaceToLog();
		WriteAllocatorReportToLog( proxyId, proxy );
		WritePoolReportToLog();
	}

	void Reporter::WriteReportToTxt() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		if( PrepareOOMReporter( ReportFormat::ReportFormat_TXT ) )
		{
			WriteReportToLog();

			CloseOOMReporter( ReportFormat::ReportFormat_TXT );
		}
#endif
	}

	void Reporter::WriteReportToTxt_CustomFile( const char* filePath ) const
	{
		RED_UNUSED( filePath );

#ifdef RED_MEMORY_ENABLE_REPORT
		if( OpenCustomOOMReporter( filePath ) )
		{
			WriteReportToLog();

			CloseCustomOOMReporter();
		}
#endif
	}

	void Reporter::WriteReportToJson() const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		if ( PrepareOOMReporter( ReportFormat::ReportFormat_JSON ) )
		{
			WritePoolReportToJson();

			CloseOOMReporter( ReportFormat::ReportFormat_JSON );
		}
#endif
	}

	void Reporter::WritePoolOOMReportToLog( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) const
	{
		RED_UNUSED( poolName );
		RED_UNUSED( allocatorName );
		RED_UNUSED( size );
		RED_UNUSED( alignment );
#ifdef RED_MEMORY_ENABLE_REPORT
		if ( PrepareOOMReporter( ReportFormat::ReportFormat_TXT ) )
		{
			RED_MEMORY_LOG( "Memory:  ***** OUT OF MEMORY! *****" );
			RED_MEMORY_LOG( "Memory: Failed to allocate %" PRIu32 " bytes with alignment %" PRIu32 " from pool '%s' using '%s' on thread '%" PRIu32 "'", size, alignment, poolName, allocatorName, ThreadIdProvider().GetCurrentId() );

			WriteReportToLog();

			CloseOOMReporter( ReportFormat::ReportFormat_TXT );
		}
#endif
	}

	void Reporter::WriteAllocatorOOMReportToLog( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment ) const
	{
		RED_UNUSED( proxyId );
		RED_UNUSED( proxy );
		RED_UNUSED( size );
		RED_UNUSED( alignment );

#ifdef RED_MEMORY_ENABLE_REPORT
		if ( PrepareOOMReporter( ReportFormat::ReportFormat_TXT ) )
		{
			RED_MEMORY_LOG( "Memory:  ***** OUT OF MEMORY! *****" );
			RED_MEMORY_LOG( "Memory: Failed to commit %" PRIu32 " bytes with alignment %" PRIu32 " from allocator '%s' (%" PRIu32 ") on thread '%" PRIu32 "'", size, alignment, GetAllocatorName( proxyId ), proxyId, ThreadIdProvider().GetCurrentId() );

			WriteReportToLog( proxyId, proxy );

			CloseOOMReporter( ReportFormat::ReportFormat_TXT );
		}
#endif
	}

	void Reporter::WriteSystemCommitOOMReportToLog( u64 size, u32 alignment ) const
	{
		RED_UNUSED( size );
		RED_UNUSED( alignment );

#ifdef RED_MEMORY_ENABLE_REPORT
		if ( PrepareOOMReporter( ReportFormat::ReportFormat_TXT ) )
		{
			RED_MEMORY_LOG( "Memory:  ***** OUT OF MEMORY! *****" );
			RED_MEMORY_LOG( "Memory: Failed to commit %" PRIu64 " bytes with alignment %" PRIu32 " from system allocator on thread '%" PRIu32 "'", size, alignment, ThreadIdProvider().GetCurrentId() );

			WriteReportToLog();

			CloseOOMReporter( ReportFormat::ReportFormat_TXT );
		}
#endif
	}

	void Reporter::WriteSystemReserveOOMReportToLog( u64 size, u32 alignment, u32 pageSize ) const
	{
		RED_UNUSED( size );
		RED_UNUSED( alignment );
		RED_UNUSED( pageSize );

#ifdef RED_MEMORY_ENABLE_REPORT
		if ( PrepareOOMReporter( ReportFormat::ReportFormat_TXT ) )
		{
			RED_MEMORY_LOG( "Memory:  ***** OUT OF MEMORY! *****" );
			const u64 sizeRoundedToPageSize = RoundUp( size, static_cast< u64 >( pageSize ) );
			RED_MEMORY_LOG( "Memory: Failed to reserve %" PRIu64 " system pages (%" PRIu64 " bytes with alignment %" PRIu32 ") from system allocator on thread '%" PRIu32 "'", sizeRoundedToPageSize / pageSize, sizeRoundedToPageSize, alignment, ThreadIdProvider().GetCurrentId() );

			WriteReportToLog();

			CloseOOMReporter( ReportFormat::ReportFormat_TXT );
		}
#endif
	}

	void Reporter::SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIMetricsToJson )
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		m_parameter.dumpRTTIMetricsToJson = dumpRTTIMetricsToJson;
#else
		RED_UNUSED( dumpRTTIMetricsToJson );
#endif
	}

	void Reporter::SetPlayTime(Float time)
	{
#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
		m_reportData.playTime = time;
#else
		RED_UNUSED(time);
#endif
	}

	void Reporter::SetTrackedQuest(const char* questName)
	{
#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
		red::SNPrintFSafe( m_reportData.trackedQuest, 1024, "%s", questName );
#else
		RED_UNUSED(questName);
#endif
	}

	void Reporter::SetCmdLine(const char* cmdLine)
	{
#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
		red::SNPrintFSafe( m_reportData.cmdLine, 1024, "%s", cmdLine );
#else
		RED_UNUSED(cmdLine);
#endif
	}

	void Reporter::SetLocation(const char* location)
	{
#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
		red::SNPrintFSafe( m_reportData.location, 1024, "%s", location );
#else
		RED_UNUSED(location);
#endif
	}

}
}
