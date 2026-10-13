/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once


//////////////////////////////////////////////////////////////////////////
//
// commands
namespace red
{
	class DebugServerPlugin;

	namespace dbg
	{
		namespace internal_commands
		{
			Uint32 ProcessCommand_AreWeConnected( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
			Uint32 ProcessCommand_Disconnect( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
			Uint32 ProcessCommand_EnableFrameTime( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
			Uint32 ProcessCommand_ConsoleExec( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
			Uint32 ProcessCommand_StartPropsTrace( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
			Uint32 ProcessCommand_FinishPropsTrace( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
		};
	}
}
