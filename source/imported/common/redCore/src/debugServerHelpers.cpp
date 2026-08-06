/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugServerHelpers.h"
#include "debugServerManager.h"
#include "../../../common/redNetwork/include/manager.h"

namespace red
{

namespace
{
	// PB: We should probably have some global access to info about port used by various systems of engine/game.
	// PB: Todo once Network::Manager is refactored.
	const Uint32 c_editorPort = 37000;
	const Uint32 c_gamePort = 37001;
	const char* c_serverAddress = "127.0.0.1";
	const char* c_channelName = RED_NET_CHANNEL_DEBUG_SERVER;
}

void DebugServerHelpers::ConnectToEditor()
{
	// PB: Need it here to properly initialized channel's destination.
	// Probably should do it via DebugServer interface, but for now it's not possible.
	red::Network::Manager::GetInstance()->ConnectTo( red::Network::Address( c_serverAddress , c_editorPort ), c_channelName );
}

void DebugServerHelpers::ConnectToGame()
{
	red::Network::Manager::GetInstance()->ConnectTo( red::Network::Address( c_serverAddress, c_gamePort ), c_channelName );
}

const char* DebugServerHelpers::GetChannelName()
{
	return c_channelName;
}

} // red
