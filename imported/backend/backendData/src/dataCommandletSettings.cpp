/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCommandletSettings.h"

#include "dataSystemInstance.h"
#include "dataTaskSystem.h"

#include "../../../common/redCore/include/commandline.h"
#include "../../../common/app/include/appPath.h"

namespace data
{

CommandletSettings::CommandletSettings()
	: platform( data::TargetPlatform::NONE )
	, maxConcurrentTasks( data::TaskSystem::DefaultMaxConcurrentTasks() )
	, forced( false )
	, serverBuild( false )
	, serverWorker( false )
	, dumpDependencies( false )
	, dumpMetrics( false )
{
}

bool CommandletSettings::Parse( const red::CommandLine& commandline, Uint32 options )
{
	String param;
	if ( commandline.GetFirstOption( "platform", param ) )
	{
		platform = data::ParseTargetPlatfrom( param.AsChar() );
	}

	if ( platform == data::TargetPlatform::NONE )
	{
		if ( (options & REQUIRES_VALID_PLATFORM) != 0 )
		{
			RED_LOG_ERROR( "No cooking platform specified" );
			return false;
		}
		else if ( (options & DEFAULT_PLATFORM_PC) != 0 )
		{
			platform = data::TargetPlatform::PC;
		}
	}

	if ( platform != data::TargetPlatform::NONE )
	{
		outputDepotPath = data::DefaultCookedDepotPath( platform );
		outputCachePath = data::DefaultCookedCachePath( platform );
	}

	if ( commandline.GetFirstOption( "outputDepot", param ) )
	{
		outputDepotPath = red::AbsolutePath::ParseDirPath( param );
	}

	if ( commandline.GetFirstOption( "outputCache", param ) )
	{
		outputCachePath = red::AbsolutePath::ParseDirPath( param );
	}

	if ( (options & REQUIRES_VALID_OUTPUT) != 0 )
	{
		if ( outputDepotPath.Empty() || outputCachePath.Empty() )
		{
			RED_LOG_ERROR( "Requires valid -outputDepot and -outputCache options spectied, either both must be specified or none." );
			return false;
		}
	}

	if ( commandline.GetFirstOption( "dataCache", param ) )
	{
		dataCacheConfig = param;
	}

	if ( commandline.GetFirstOption( "maxTasks", param ) )
	{
		auto number = atol( param.AsChar() );
		if ( number > 0 )
		{
			maxConcurrentTasks = number;
		}
	}

	serverBuild = commandline.HasOption( "serverBuild" );

	if ( commandline.HasOption( "serverWorker" ) )
	{
		serverBuild = true;
		serverWorker = true;
	}
	else
	{
		serverWorker = false;
	}

	if ( commandline.GetFirstOption( "workerTaskList", param ) )
	{
		if ( serverBuild && !serverWorker )
		{
			workerTaskListPath = red::AbsolutePath::ParseFilePath( param );
		}
		else
		{
			RED_LOG_ERROR( "Worker task list can only be specified on a non-worker server build. Ignoring now." );
		}
	}

	forced = commandline.HasOption( "force" );

	dumpDependencies = commandline.HasOption( "debugDumpDependencies" );

	dumpMetrics = commandline.HasOption( "dumpMetrics" );

	if ( dumpMetrics )
	{
		if ( commandline.GetFirstOption( "metricsPath", param ) )
		{
			dumpMetricsPath = red::AbsolutePath::ParseFilePath( param );
		}
		else
		{
			// Grab date/time, parsing the settings is close enough to when we started for this
			red::DateTime startDateTime;
			red::Clock::GetInstance().GetLocalTime( startDateTime );

			// Grab the commandlet name, at this stage it will be the first command line parameter for no option
			RED_VERIFY( commandline.GetFirstOption( "", param ) );

			dumpMetricsPath = data::metrics::Writer::ComputeMetricsFilePath( param.AsChar(), startDateTime );
		}
	}

	return true;
}

} // data
