/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobDispatcherInitParam.h"
#include "jobSystem.h"

namespace dd
{
	static red::CrashData< Uint32 > maxLatentJobs( "Jobs/InitParam", "MaxLatentJobs" );
	static red::CrashData< Uint32 > maxCriticalPathJobs( "Jobs/InitParam", "MaxCriticalPathJobs" );
	static red::CrashData< Uint32 > maxImmediateJobs( "Jobs/InitParam", "MaxImmediateJobs" );
	static red::CrashData< Uint32 > workerThreadStackSizeKB( "Jobs/InitParam", "WorkerThreadStackSizeKB" );
	static red::CrashData< Uint32 > maxThreads( "Jobs/InitParam", "MaxThreads" );
	static red::CrashData< Bool > allJobsCriticalPath( "Jobs/InitParam", "AllJobsCriticalPath" );
	static red::CrashData< Bool > useJobDebugger( "Jobs/InitParam", "UseJobDebugger" );

	static void SetCrashData( const ::job::InitParam& param )
	{
		maxLatentJobs.Set( param.maxLatentJobs );
		maxCriticalPathJobs.Set( param.maxCriticalPathJobs );
		maxImmediateJobs.Set( param.maxImmediateJobs );
		workerThreadStackSizeKB.Set( param.workerThreadStackSizeKB );
		maxThreads.Set( param.maxThreads );
		useJobDebugger.Set( param.useJobDebugger );
		allJobsCriticalPath.Set( param.allJobsCriticalPath );
	}
}

namespace job
{
	// E3: FIXME: I/O spams the queues - need some throttling or backlog to avoid queue saturation and deadlock during initial game loading
#ifdef RED_PLATFORM_WINPC
	const Uint32 c_debugJobMult = 4;
#else
	const Uint32 c_debugJobMult = 1;
#endif

#ifdef RED_PLATFORM_DURANGO
#define CONSOLE_TYPE_XBOX_SERIES_S ( (CONSOLE_TYPE)5 )
#define CONSOLE_TYPE_XBOX_SERIES_X ( (CONSOLE_TYPE)6 )
#define CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT ( (CONSOLE_TYPE)7 )
#endif


	InitParam::InitParam()
		: maxLatentJobs( 32 * 1024 * c_debugJobMult )
		, maxCriticalPathJobs( 16 * 1024 * c_debugJobMult )
		, maxImmediateJobs( 2 * 1024 )
#ifdef RED_CONSOLE_CORE_7_SUPPORT
		, maxCore7Jobs( 4 * 1024 )
#endif
		, workerThreadStackSizeKB( 1024 )
		, maxThreads( RED_MAX_JOB_THREADS )
		, allJobsCriticalPath( false )
		, useJobDebugger( false )
	{
#ifdef RED_PLATFORM_DURANGO
		Uint32 jobMultiplier = 1;
		CONSOLE_TYPE consoleType = ::GetConsoleType();
		if( consoleType == CONSOLE_TYPE_XBOX_ONE_X
			|| consoleType == CONSOLE_TYPE_XBOX_ONE_X_DEVKIT
			|| consoleType == CONSOLE_TYPE_XBOX_SERIES_S
			|| consoleType == CONSOLE_TYPE_XBOX_SERIES_X
			|| consoleType == CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT )
		{
			jobMultiplier = 2;
		}
	
		maxLatentJobs *= jobMultiplier;
		maxCriticalPathJobs *= jobMultiplier;
		maxImmediateJobs *= jobMultiplier;
#ifdef RED_CONSOLE_CORE_7_SUPPORT
		maxCore7Jobs *= jobMultiplier;
#endif

#endif

	}

	void InitParam::SetCrashData( const InitParam& param )
	{
		dd::SetCrashData( param );
	}

	job::InitParam DefaultEditorInitParam()
	{
		job::InitParam jobInitParam;
		jobInitParam.maxLatentJobs *= 4;
		jobInitParam.maxCriticalPathJobs *= 4;
		jobInitParam.maxImmediateJobs *= 4;
		return jobInitParam;
	}

	job::InitParam DefaultToolInitParam()
	{
		job::InitParam jobInitParam;
		jobInitParam.maxLatentJobs = 1;
		jobInitParam.maxCriticalPathJobs = 4 * 1024 * 1024; // huge queue, so we can handle loading huge worlds, etc.
		jobInitParam.maxImmediateJobs = 1;
		jobInitParam.allJobsCriticalPath = true;
		return jobInitParam;
	}
}
