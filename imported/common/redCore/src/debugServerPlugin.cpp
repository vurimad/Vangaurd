/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/redContainers/include/redContainersPublic.h"
#include "debugServerPlugin.h"
#include "debugServerManager.h"

namespace red
{

//////////////////////////////////////////////////////////////////////////

Uint32 Command_DebugServerPluginImpl( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data )
{
	static_cast< DebugServerPluginImpl* >( owner )->ProcessCommand( data );
	return true;
}

//////////////////////////////////////////////////////////////////////////

DebugServerPluginImpl::DebugServerPluginImpl( const red::String& pluginName )
	: m_pluginName( pluginName )
{

}

DebugServerPluginImpl::~DebugServerPluginImpl()
{
	ShutDown();
}

//////////////////////////////////////////////////////////////////////////

Bool DebugServerPluginImpl::Init()
{
	DBGSRV_REG_COMMAND( m_pluginName.AsChar(), Command_DebugServerPluginImpl, false );

	return true;
}

Bool DebugServerPluginImpl::ShutDown()
{
	DBGSRV_UNREG_COMMAND( m_pluginName.AsChar() );

	m_functors.Clear();

	return true;
}

void DebugServerPluginImpl::GameStarted()
{
}

void DebugServerPluginImpl::GameStopped()
{
}

void DebugServerPluginImpl::Update()
{
}

//////////////////////////////////////////////////////////////////////////

void DebugServerPluginImpl::ProcessCommand( const red::DynArray< red::String >& args )
{
	RED_FATAL_ASSERT( args.Size() > 0, "DebugServerPluginImpl command needs to have at least one parameter (functor name)" );

	typename FunctorsMap::iterator it = m_functors.Find( args[ 0 ] );
	if ( it == m_functors.End() )
	{
		RED_LOG( "DebugServerPluginImpl: cannot find subcommand '%hs' for plugin '%hs'.", args[ 0 ].AsChar(), m_pluginName.AsChar() );
		return;
	}

	Functor* func = it.Value().Get();
	const Uint32 argsCount = func->GetArgsCount();
	if ( argsCount != args.Size() - 1 )
	{
		RED_LOG( "DebugServerPluginImpl: wrong args number for command '%hs'. Should be %d but is %d.", args[ 0 ].AsChar(), argsCount, args.Size() - 1 );
		return;
	}

	func->Call( args );
}


void DebugServerPluginImpl::Send( red::Network::ChannelPacket& packet ) const
{
	DBGSRV_CALL( Send( RED_NET_CHANNEL_DEBUG_SERVER, packet ) );
}

} // red
