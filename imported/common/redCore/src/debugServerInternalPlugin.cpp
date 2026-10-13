/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_DEBUG_SERVER_ENABLED

//////////////////////////////////////////////////////////////////////////
// headers
#include "../../redContainers/include/redContainersPublic.h"
#include "debugServerPlugin.h"
#include "debugServerInternalPlugin.h"
#include "debugServerInternalCommands.h"
#include "debugServerManager.h"


//////////////////////////////////////////////////////////////////////////
// implementations

//////////////////////////////////////////////////////////////////////////
//
// init
Bool red::DebugServerInternalPlugin::Init()
{
	// assign internal command handlers
	DBGSRV_REG_COMMAND( "areWeConnected?", red::dbg::internal_commands::ProcessCommand_AreWeConnected, false );
	DBGSRV_REG_COMMAND( "disconnect", red::dbg::internal_commands::ProcessCommand_Disconnect, false );
	DBGSRV_REG_COMMAND( "enableFrameTime", red::dbg::internal_commands::ProcessCommand_EnableFrameTime, true );

	// remote console
	DBGSRV_REG_COMMAND( "consoleExec", red::dbg::internal_commands::ProcessCommand_ConsoleExec, true );

	// remote properties tracer
	DBGSRV_REG_COMMAND( "startPropsTrace", red::dbg::internal_commands::ProcessCommand_StartPropsTrace, false );
	DBGSRV_REG_COMMAND( "finishPropsTrace", red::dbg::internal_commands::ProcessCommand_FinishPropsTrace, false );

	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// cleaning up
Bool red::DebugServerInternalPlugin::ShutDown()
{
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// when game is started
void red::DebugServerInternalPlugin::GameStarted()
{
}
//////////////////////////////////////////////////////////////////////////
//
// when game is stopped
void red::DebugServerInternalPlugin::GameStopped()
{
}
//////////////////////////////////////////////////////////////////////////
//
// on every game tick
void red::DebugServerInternalPlugin::Update()
{
}

#else

RED_NO_EMPTY_FILE()

#endif // NO_DEBUG_SERVER
