/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_REPORT_UTILS_H_
#define _RED_MEMORY_INCLUDE_REPORT_UTILS_H_

#include "redMemoryInternal.h"

namespace red
{
namespace memory
{
	typedef void ( *DumpRTTIMetricsToJson )( FILE* );

	RED_MEMORY_API void LogFullReport();
	RED_MEMORY_API void LogFullReportToTxt();
	RED_MEMORY_API void LogFullReportToJson();
	RED_MEMORY_API void LogFullReportToCustomFile( const char* filePath );
	RED_MEMORY_API void SetAutoDumpInterval(Float interval);
	RED_MEMORY_API void SetAutoDumpFormat(const char* format);
	RED_MEMORY_API void TryDoAutoMemReport(Float dt);
	RED_MEMORY_API void SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIToJsonFunction );
	RED_MEMORY_API void Debug_SetPlayTIme(Float time);
	RED_MEMORY_API void Debug_SetTrackedQuest(const char* questName);
	RED_MEMORY_API void Debug_SetLocation(const char* location);
	RED_MEMORY_API void DebugSetCmdLine(const char * cmdLine);
}
}

#endif
