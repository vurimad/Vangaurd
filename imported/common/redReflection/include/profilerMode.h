/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace script
{
	enum ProfilerMode : red::Uint16
	{
		ProfilerMode_Off					= 0,
		ProfilerMode_All					= RED_FLAG( 1 ),	// profile all functions
		ProfilerMode_MarkedFunctions		= RED_FLAG( 2 ),	// profile only marked functions
		ProfilerMode_EntryFunctions			= RED_FLAG( 3 )		// profile marked and entry point functions
	};

	RED_INLINE ProfilerMode ParseProfilerMode( red::StringView mode )
	{
		if ( mode == "ProfilingAll" )
		{
			return ProfilerMode_All;
		}
		else if ( mode == "ProfilingMarked" )
		{
			return ProfilerMode_MarkedFunctions;
		}
		else if ( mode == "ProfilingEntry" )
		{
			return ProfilerMode_EntryFunctions;
		}
		else
		{
			return ProfilerMode_Off;
		}
	}
}