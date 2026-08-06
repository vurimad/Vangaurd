/*
* Copyright © 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "jobPriority.h"

namespace job
{

struct REDJOBS2_API InitParam
{
	InitParam();

	static void SetCrashData( const InitParam& param );

	Uint32 maxLatentJobs;
	Uint32 maxCriticalPathJobs;
	Uint32 maxImmediateJobs;
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	Uint32 maxCore7Jobs;
#endif
	Uint32 workerThreadStackSizeKB;
	Uint32 maxThreads;
	Bool allJobsCriticalPath;
	Bool useJobDebugger;
};

REDJOBS2_API InitParam DefaultEditorInitParam();

REDJOBS2_API InitParam DefaultToolInitParam();

}
