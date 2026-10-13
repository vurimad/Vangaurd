/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace script
{
	enum ScriptVersion : red::Uint8
	{
		ScriptVersion_Debug = 0,					// includes all symbols
		ScriptVersion_Profiling = RED_FLAG( 1 ),	// debugger symbols are excluded
		ScriptVersion_Final = RED_FLAG( 3 )		// debugger, profiler and testonly symbols are excluded
	};

	RED_INLINE ScriptVersion ParseScriptVersion( red::StringView version )
	{
		if ( version == "Debug" )
		{
			return ScriptVersion_Debug;
		}
		else if ( version.StartsWith( "Profiling" ) )
		{
			return ScriptVersion_Profiling;
		}
		else
		{
			return ScriptVersion_Final;
		}
	}

	RED_INLINE const char* GetBlobName( ScriptVersion version )
	{
		switch ( version )
		{
		case ScriptVersion_Debug:
			return "master.redscripts";

		case ScriptVersion_Profiling:
			return "profiling.redscripts";

		case ScriptVersion_Final:
		default:
			return "final.redscripts";
		}
	}
}