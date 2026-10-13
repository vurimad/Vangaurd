/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataPlatform.h"

namespace red
{
class CommandLine;
}

namespace data
{

enum CommandletSettingsOptions : Uint32
{
	REQUIRES_VALID_OUTPUT = RED_FLAG( 0 ),		// Requires that valid output depot and cache directories be specified
	REQUIRES_VALID_PLATFORM = RED_FLAG( 1 ),	// Require that a valid platform is specified (not NONE)
	DEFAULT_PLATFORM_PC = RED_FLAG( 2 ),		// Set the platform by default to PC if none specified
	PLATFORM_NOT_REQUIRED = 0,					// Empty value to signify that platform is not required
};

struct BACKEND_DATA_API CommandletSettings
{
	red::AbsolutePath outputDepotPath;
	red::AbsolutePath outputCachePath;
	red::AbsolutePath workerTaskListPath;
	red::AbsolutePath dumpMetricsPath;
	red::String dataCacheConfig;
	data::TargetPlatform platform;
	Uint32 maxConcurrentTasks;
	bool forced;
	bool serverBuild;
	bool serverWorker;
	bool dumpDependencies;
	bool dumpMetrics;

	CommandletSettings();

	bool Parse( const red::CommandLine& commandline, Uint32 options = 0 );
};

} // data
