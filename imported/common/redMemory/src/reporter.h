/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_REPORTER_H_
#define _RED_MEMORY_REPORTER_H_

#include "proxyTypeId.h"

namespace red
{
namespace memory
{
	class PoolRegistry;
	class MetricsRegistry;
	class SystemAllocator;

	typedef void ( *DumpRTTIMetricsToJson )( FILE* );

	struct ReporterParameter
	{
		PoolRegistry * poolRegistry;
		const MetricsRegistry * metricsRegistry;
		const SystemAllocator * systemAllocator;
		const SystemAllocator * flexibleAllocator;
		DumpRTTIMetricsToJson dumpRTTIMetricsToJson;
	};

#ifdef RED_MEMORY_ENABLE_REPORT
#ifdef RED_CONFIGURATION_FINAL
	extern FILE* s_oomFile;
#endif
#endif

	class Reporter
	{
	public:
		Reporter();
		~Reporter();

		void Initialize( const ReporterParameter & param );

		void SavePoolOOMCrashData( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) const;
		void SaveAllocatorOOMCrashData( ProxyTypeId proxyId, u32 size, u32 alignment ) const;
		void SaveSystemCommitOOMCrashData( u64 size, u32 alignment ) const;
		void SaveSystemReserveOOMCrashData( u64 size, u32 alignment, u32 pageSize ) const;

		void WriteReportToLog() const;
		void WriteReportToLog( ProxyTypeId proxyId, void* proxy ) const;
		void WriteReportToTxt() const;
		void WriteReportToJson() const;
		void WriteReportToTxt_CustomFile( const char* filename ) const;

		void WritePoolOOMReportToLog( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) const;
		void WriteAllocatorOOMReportToLog( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment ) const;
		void WriteSystemCommitOOMReportToLog( u64 size, u32 alignment ) const;
		void WriteSystemReserveOOMReportToLog( u64 size, u32 alignment, u32 pageSize ) const;

		void SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIMetricsToJson );
		void SetPlayTime(Float time);
		void SetTrackedQuest(const char* questName);
		void SetCmdLine( const char* cmdLine );
	private:
		void WriteReportPrefaceToLog() const;
		void WritePoolReportToLog() const;
		void WritePoolReportToJson() const;
		void WriteAllocatorReportToJson() const;
		void WriteAllocatorReportToLog( ProxyTypeId proxyId, void* proxy ) const;

		ReporterParameter m_parameter;

#ifdef RED_MEMORY_ENABLE_REPORT_EXTRAS
	struct ReportData
	{
		Float playTime = 0.f;
		char trackedQuest[1024] = { 0 };
		char location[1024] = { 0 };
		char cmdLine[1024] = { 0 };
	} m_reportData;
#endif
	public:
		void SetLocation(const char* location);
	};
}
}

#endif
