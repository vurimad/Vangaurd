/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"

#ifdef RED_DEBUG_SERVER_ENABLED

#include "debugServerManager.h"
#include "debugServerInternalCommands.h"
#include "debugServerHelpers.h"
#include "../../redNetwork/include/manager.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::DynArray;
using red::String;
using red::Network::ChannelPacket;
using NetworkManager = red::Network::Manager;


//////////////////////////////////////////////////////////////////////////
// implementations

//////////////////////////////////////////////////////////////////////////
//
// areWeConnected
Uint32 red::dbg::internal_commands::ProcessCommand_AreWeConnected( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );
	RED_ASSERT( data.Size() == 2 );

	// init
	const AnsiChar* channelName = data[ 1 ].AsChar();
	ChannelPacket packet( channelName );

	// set as connected
	DBGSRV_CALL( SetConnected() );
	packet.WriteString( "yes" );

	// TODO:
	// use ToString once it is moved to redCore / redContainers
	// fill data
	/*1*/ packet.WriteString( String::Printf( "%u", red::DebugServerAPI ).AsChar() );
	/*2*/ packet.WriteString( DBGSRV_CALL( GetName() ) );
	/*3.11*/ packet.WriteString( "CP2077" );
	/*4.11*/ packet.WriteString( __DATE__ );

	// send
	NetworkManager::GetInstance()->Send( channelName, packet );

	// processed
	return 1;	
};
//////////////////////////////////////////////////////////////////////////
//
// disconnect
Uint32 red::dbg::internal_commands::ProcessCommand_Disconnect( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );
	RED_ASSERT( data.Size() == 1 );

	// set as disconnect
	DBGSRV_CALL( SetDisconnected() );

	// processed
	return 1;
};
//////////////////////////////////////////////////////////////////////////
//
// todo: enable frame time - move it to profiler plugin
Uint32 red::dbg::internal_commands::ProcessCommand_EnableFrameTime( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );

	// parse request
	// const AnsiChar* str = data[ 0 ].AsChar();
	// Bool enable = false;
	// GParseBool( str, enable );

	// TODO: change to ToString once it is moved to redCore / redContainers
	const Bool enable = data[ 0 ].EqualsNC( "true" );

	DBGSRV_CALL( EnableFrameTimeLogging( enable ) );

	// processed
	return 1;
};
//////////////////////////////////////////////////////////////////////////
//
// todo: consoleExec
Uint32 red::dbg::internal_commands::ProcessCommand_ConsoleExec( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );

	// TODO: move as external plugin (to "engine")
	//if ( !GGame->GetActiveWorld() || data.Empty() )
	//	return 0;

	//ConsoleExecResultSender consoleSender( data[0] );
	return 0;
}
//////////////////////////////////////////////////////////////////////////
//
// startPropsTrace
Uint32 red::dbg::internal_commands::ProcessCommand_StartPropsTrace( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );

	DBGSRV_CALL( StartPropsTrace() );

	// processed
	return 1;
};
//////////////////////////////////////////////////////////////////////////
//
// finishPropsTrace
Uint32 red::dbg::internal_commands::ProcessCommand_FinishPropsTrace( red::DebugServerPlugin* owner, const DynArray< String >& data )
{
	RED_UNUSED( owner );

	DBGSRV_CALL( FinishPropsTrace() );

	// processed
	return 1;
};

#else

RED_NO_EMPTY_FILE()

#endif // NO_DEBUG_SERVER
