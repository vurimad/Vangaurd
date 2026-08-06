/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/reportUtils.h"
#include "vault.h"

namespace red
{
namespace memory
{
	void LogFullReport()
	{
		AcquireVault().LogMemoryReport();
	}
	void LogFullReportToTxt()
	{
		AcquireVault().LogMemoryReportToTxt();
	}
	void LogFullReportToJson()
	{
		AcquireVault().LogMemoryReportToJson();
	}
	void LogFullReportToCustomFile( const char* filePath )
	{
		AcquireVault().LogMemoryReportToTxt_CustomFile( filePath );
	}

	void SetAutoDumpInterval(Float interval)
	{
		AcquireVault().SetAutoReportInterval(interval);
	}

	void SetAutoDumpFormat(const char* format)
	{
		AcquireVault().SetAutoReportFormat(format);
	}

	void SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIToJsonFunction )
	{
		AcquireVault().SetDumpRTTIToJsonFunction( dumpRTTIToJsonFunction );
	}

	void TryDoAutoMemReport(Float dt)
	{
		AcquireVault().TryDoAutoMemReport(dt);
	}

	void Debug_SetPlayTIme(Float time)
	{
		AcquireVault().Debug_SetPlayTime(time);
	}

	void Debug_SetTrackedQuest( const char* questName)
	{
		AcquireVault().Debug_SetTrackedQuest(questName);
	}

	void DebugSetCmdLine(const char * cmdLine)
	{
		RED_UNUSED(cmdLine);
		// RENABLE WHEN VAULT SIDE IS SUBMITTED AcquireVault().Debug_SetCmdLine(cmdLine);
	}

	void Debug_SetLocation(const char* location)
	{
		AcquireVault().Debug_SetLocation(location);
	}

}
}
