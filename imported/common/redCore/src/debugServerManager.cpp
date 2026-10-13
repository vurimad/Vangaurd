/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

RED_NO_EMPTY_FILE()

#ifdef RED_DEBUG_SERVER_ENABLED

//////////////////////////////////////////////////////////////////////////
// headers
#include "redCorePublic.h"
#include "singleton.h"
#include "debugServerManager.h"
#include "debugServerHelpers.h"
#include "debugServerPlugin.h"
#include "debugServerInternalPlugin.h"
#include "../../redNetwork/include/manager.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::DynArray;
using red::String;
using red::HashMap;
using red::Network::ChannelPacket;
using red::Network::IncomingPacket;
using NetworkManager = red::Network::Manager;


//////////////////////////////////////////////////////////////////////////
// consts
// aantonik: commented this because unused variable generates error on Orbis
//const Double DebugServerSendStatsTime = 1.0;	// once per second


//////////////////////////////////////////////////////////////////////////
// implementations
red::DebugServerManager& GetDbgManager()
{
	return red::TSingleton< red::DebugServerManager >::GetInstance();
}

//////////////////////////////////////////////////////////////////////////
//
// ctor
red::DebugServerManager::DebugServerManager() :
	m_name( "Debug Server" ),
	m_gameThreadCommands( red::PoolDebug() ),
	m_commandMap( red::PoolDebug() ),
	m_plugins( red::PoolDebug() ),
	m_ownedPlugins( red::PoolDebug() ),
	m_propertiesMap( red::PoolDebug() ),
	m_debugMode( false ),
	m_sendStatsEnabled( false ),
	m_frameTimeLogging( false )
{
	// stats
	m_commandsTime.SetValue( 0 );
	m_sentCount.SetValue( 0 );
	m_receivedCount.SetValue( 0 );
	m_frame.SetValue( 0 );

	// states
	m_connected.SetValue( false );
	m_attached.SetValue( false );
	m_gameRunning.SetValue( false );
	m_initialized.SetValue( false );
	m_propsTrace.SetValue( false );
}
//////////////////////////////////////////////////////////////////////////
//
// dtor
red::DebugServerManager::~DebugServerManager()
{
}
//////////////////////////////////////////////////////////////////////////
//
// init
Bool red::DebugServerManager::Init( comm::ChannelFactory& channelFactory )
{
	// init
	m_gameThreadMutex.SetSpinCount( 100 );

	// register network listeners
	NetworkManager::GetInstance()->RegisterListener( RED_NET_CHANNEL_DEBUG_SERVER, this );

	// internal plugin
	RegisterPlugin( RED_NEW( DebugServerInternalPlugin ), true );

	// init all plugins
	for ( DebugServerPlugin* it : m_plugins )
	{
		it->Init();
	}

	// initialized
	m_initialized.SetValue( true );
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// shutdown
Bool red::DebugServerManager::ShutDown()
{
	RED_THREADS_MEMORY_BARRIER();
	m_initialized.SetValue( false );
	m_connected.SetValue( false );

	// unregister network listeners
	NetworkManager::GetInstance()->UnregisterListener( RED_NET_CHANNEL_DEBUG_SERVER, this );

	// remove commands queue
	m_gameThreadMutex.Acquire();
	for ( DebugServerCommandData* it : m_gameThreadCommands )
	{
		RED_DELETE( it );
	}
	m_gameThreadCommands.Clear();
	m_gameThreadMutex.Release();

	// remove all plugins
	for ( DebugServerPlugin* it : m_plugins )
	{
		it->ShutDown();
	}
	m_plugins.Clear();

	// destroy plugins owned by manager
	for ( DebugServerPlugin* it : m_ownedPlugins )
	{
		RED_DELETE( it );
	}
	m_ownedPlugins.Clear();

	// result
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// send packet
void red::DebugServerManager::Send( const AnsiChar* channelName, const ChannelPacket& packet )
{
	NetworkManager::GetInstance()->Send( channelName, packet );
}
//////////////////////////////////////////////////////////////////////////
//
// game is started
void red::DebugServerManager::GameStarted()
{
	if ( !m_initialized.GetValue() )
		return;

	// set state
	RED_ASSERT( !m_gameRunning.GetValue(), "DbgSrv: game already started" );
	m_gameRunning.SetValue( true );

	// debug
	if ( m_debugMode )
	{
		Log( "DbgSrv", "GameStarted" );
	}

	// send to clients
	if ( m_connected.GetValue() )
	{
		SendGameStarted();

		// broadcast to all plugins
		for ( red::DebugServerPlugin* it : m_plugins )
		{
			it->GameStarted();
		}
	}
}
//////////////////////////////////////////////////////////////////////////
//
// game stopped
void red::DebugServerManager::GameStopped()
{
	if ( !m_initialized.GetValue() )
		return;

	// set state
	RED_ASSERT( m_gameRunning.GetValue(), "DbgSrv: game already stopped!" );
	m_gameRunning.SetValue( false );

	// debug
	if ( m_debugMode )
	{
		Log( "DbgSrv", "GameStopped" );
	}

	// send to clients
	if ( m_connected.GetValue() )
	{
		SendGameStopped();

		// broadcast to all plugins
		for ( red::DebugServerPlugin* it : m_plugins )
		{
			it->GameStopped();
		}
	}
}
//////////////////////////////////////////////////////////////////////////
//
// game thread commands
void red::DebugServerManager::Update()
{
	if ( !m_initialized.GetValue() )
		return;

	PC_SCOPE( DbgSrv_Tick );

	// init
	red::Timer tm;
	const Uint64 begTicks = tm.GetTicks();
	DynArray< red::DebugServerCommandData* > gameCommandsCopy{ red::PoolDebug() };

	// acquire list
	{
		PC_SCOPE( DbgSrv_Tick_Acquire_List );
		if ( m_gameThreadMutex.TryAcquire() )
		{
			if ( !m_gameThreadCommands.Empty() )
			{
				// copy list of commands
				if ( m_connected.GetValue() )
				{
					PC_SCOPE( DbgSrv_Tick_ListCopy );
					gameCommandsCopy = m_gameThreadCommands;
				}
				else
				{
					RED_LOG( "DebugServer - dropped [%u] commands caused by disconnection..", m_gameThreadCommands.Size() );
				}

				// clear commands
				m_gameThreadCommands.Clear();
			}

			// release list
			m_gameThreadMutex.Release();
		}
	}

	// process commands
	{
		for ( red::DebugServerCommandData* it : gameCommandsCopy )
		{
			PC_SCOPE( DbgSrv_GameThread_Execute );

			if ( m_connected.GetValue() )
			{
				// get command
				red::CommandHandler Command = it->m_command->m_handler;

				// call if exists
				if ( Command )
				{
					const Uint32 result = Command( it->m_command->m_owner, it->m_data );

					// stats
					RED_THREADS_MEMORY_BARRIER();
					m_sentCount.ExchangeAdd( result );
				}
				else
				{
					// not processed
					RED_LOG( "DebugServer - Cannot process game thread command.. skipping" );
				}
			}
			else
			{
				RED_LOG( "DebugServer - dropped [%u] commands caused by disconnection..", gameCommandsCopy.Size() );
			}

			// delete command from list
			RED_DELETE( it );
		}
	}

	// properties trace
	RED_THREADS_MEMORY_BARRIER();
	if ( m_propsTrace.GetValue() )
	{
		SendProperties();
	}

	// update of all plugins
	for ( red::DebugServerPlugin* it : m_plugins )
	{
		PC_SCOPE( DbgSrv_PluginsUpdate );
		it->Update();
	}

	// stats
	m_commandsTime.ExchangeAdd( tm.GetTicks()-begTicks );
	m_frame.Increment();

	// todo: send stats
	//static Float DebugServerSendStatsTimer = 0.0f;
	//DebugServerSendStatsTimer += GEngine->GetLastTimeDelta();
	//if ( m_sendStatsEnabled && m_connected.GetValue() && m_attached.GetValue() && (DebugServerSendStatsTimer >= DebugServerSendStatsTime) )
	//{
	//	DebugServerSendStatsTimer = 0.0f;
	//	SendStats();
	//}

	// frame time logging
	if ( m_frameTimeLogging && m_connected.GetValue() )
	{
		SendFrameTime();
	}
}
//////////////////////////////////////////////////////////////////////////
//
// start properties tracing
void red::DebugServerManager::StartPropsTrace()
{
	if ( m_propertiesMap.Size() == 0 )
	{
		RED_LOG( "DebugServer - StartPropsTrace() - try to register properties to send first.." );
		return;
	}

	RED_THREADS_MEMORY_BARRIER();
	m_propsTrace.SetValue( true );
	SendPropertiesNames();
}
//////////////////////////////////////////////////////////////////////////
//
// finish properties tracing
void red::DebugServerManager::FinishPropsTrace()
{
	RED_THREADS_MEMORY_BARRIER();
	m_propsTrace.SetValue( false );
}
//////////////////////////////////////////////////////////////////////////
//
// send traced properties
void red::DebugServerManager::SendProperties()
{
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "Properties" );
	
	// fill data
	HashMap< String, String >::iterator it = m_propertiesMap.Begin();
	while ( it != m_propertiesMap.End() )
	{
		/*2..2+props*/packet.WriteString( it.Value().AsChar() );
		++it;
	}

	// send
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );
}
//////////////////////////////////////////////////////////////////////////
//
// send traced properties names
void red::DebugServerManager::SendPropertiesNames()
{
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "PropertiesNames" );

	// fill data
	HashMap< String, String >::iterator it = m_propertiesMap.Begin();
	while ( it != m_propertiesMap.End() )
	{
		/*2..2+props*/packet.WriteString( it.Key().AsChar() );
		++it;
	}

	// send
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );
}
//////////////////////////////////////////////////////////////////////////
//
// register property to trace
Bool red::DebugServerManager::RegisterNativeProperty( const String& propName )
{
	RED_THREADS_MEMORY_BARRIER();
	if ( m_propsTrace.GetValue() )
		return false;

	// check existence
	if ( m_propertiesMap.KeyExist( propName ) )
	{
		RED_LOG( "DebugServer - RegisterNativeProperty() [%s] - handler registered already!..", propName.AsChar() );
		return false;
	}

	// add new native property
	m_propertiesMap[ propName ] = "";
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// set property value by string directly
Bool red::DebugServerManager::SetPropertyValue( const String& propName, String value )
{
	HashMap< String, String >::iterator it = m_propertiesMap.Find( propName );
	if ( it == m_propertiesMap.End() )
	{
		RED_LOG( "DebugServer - SetPropertyValue( %s ) - cannot find property!..", propName.AsChar() );
		return false;
	}

	// set native property value
	it.Value() = value.AsChar();
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// log
void red::DebugServerManager::Log( const String& channel, const AnsiChar* format, ... )
{
	if ( !m_initialized.GetValue() || !m_connected.GetValue() )
		return;

	PC_SCOPE( DbgSrv_Log );

	// init
	AnsiChar buffer[ 2048 ];
	va_list arglist;
	va_start( arglist, format );
	red::VSNPrintF( buffer, 2048, (channel + ": " + format).AsChar(), arglist );
	va_end( arglist );

	red::Network::ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "Log" );

	// fill data
	/*2*/packet.WriteString( buffer );

	//// send
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );

	//// stats
	RED_THREADS_MEMORY_BARRIER();
	m_sentCount.Increment();
}
//////////////////////////////////////////////////////////////////////////
//
// register debug plugin
Bool red::DebugServerManager::RegisterPlugin( DebugServerPlugin* plugin, Bool ownedByManager )
{
	if ( !plugin )
	{
		RED_ERROR( !plugin, "DebugServer - plugin is NULL!.." );
		return false;
	}

	// add plugin
	if ( !m_plugins.Exist( plugin ) )
	{
		m_plugins.PushBack( plugin );

		if ( ownedByManager )
		{
			m_ownedPlugins.PushBack( plugin );
		}

		// initialize plugin
		if ( m_initialized.GetValue() )
		{
			plugin->Init();
		}

		return true;
	}
	return false;
}
//////////////////////////////////////////////////////////////////////////
//
// unregister debug plugin
Bool red::DebugServerManager::UnregisterPlugin( DebugServerPlugin* plugin )
{
	if ( !plugin )
	{
		RED_ERROR( !plugin, "DebugServer - plugin is NULL!.." );
		return false;
	}

	// remove plugin
	if ( m_plugins.Remove( plugin ).IsSuccessful() )
	{
		// shutdown plugin
		if ( m_initialized.GetValue() )
		{
			plugin->ShutDown();
		}

		// if plugin was owned by manager - destroy it
		if ( m_ownedPlugins.Remove( plugin ).IsSuccessful() )
		{
			RED_DELETE( plugin );
		}

		return true;
	}
	return false;
}
//////////////////////////////////////////////////////////////////////////
//
// register command handler
Bool red::DebugServerManager::RegisterCommandHandler( DebugServerPlugin* owner, const String& commandName, const Bool gameThread, CommandHandler handler )
{
	// check owner
	if ( !owner )
	{
		RED_LOG( "DebugServer - command [%hs:%i] - owner is NULL!..", commandName.AsChar(), !!gameThread );
		return false;
	}

	// check existence
	if ( m_commandMap.KeyExist( commandName ) )
	{
		RED_LOG( "DebugServer - command [%hs:%i] - handler registered already!..", commandName.AsChar(), !!gameThread );
		return false;
	}

	// add new command
	m_commandMap[ commandName ] = DebugServerCommand( owner, handler, gameThread );
	return true;
}
//////////////////////////////////////////////////////////////////////////
//
// unregister command handler
Bool red::DebugServerManager::UnregisterCommandHandler( const String& commandName )
{
	return m_commandMap.Remove( commandName ).IsSuccessful();
}
//////////////////////////////////////////////////////////////////////////
//
// on packet received
void red::DebugServerManager::OnPacketReceived( const AnsiChar* channelName, IncomingPacket& packet )
{
	PC_SCOPE( DbgSrv_OnPacketReceived );

	// init
	red::Timer tm;
	const Uint64 begTicks = tm.GetTicks();

	// debugger communication channels
	if ( red::Strcmp( channelName, RED_NET_CHANNEL_DEBUG_SERVER ) == 0 )
	{
		// get command
		const Uint32 c_packetBuffersize = 2028;
		AnsiChar buffer[ c_packetBuffersize ];
		{
			PC_SCOPE( DbgSrv_OnPacketReceive_ReadCmd );
			RED_VERIFY( packet.ReadString( buffer, RED_ARRAY_COUNT_U32( buffer ) ) );
		}

		const String commandName( buffer );
		m_receivedCount.Increment();

		//LOG_ENGINE( TXT( ">> next instruction #%i: %s %i" ), m_receivedCount.GetValue(), command.AsChar(), m_gameThreadCommands.Size() );

		// check command
		const Bool found = m_commandMap.KeyExist( commandName );
		if ( found )
		{
			PC_SCOPE( DbgSrv_OnPacketReceive_Found );

			// init
			red::DebugServerCommand& commandDef = m_commandMap[ commandName ];

			// get data
			DynArray< String > data{ red::PoolDebug() };
			{
				PC_SCOPE( DbgSrv_OnPacketReceive_Found_Read );
				while ( packet.ReadString( buffer, RED_ARRAY_COUNT_U32( buffer ) ) )
				{
					if ( strlen( buffer ) )
					{
						data.PushBack( buffer );
					}
				}
			}

			// check is need to run on game thread
			if ( commandDef.m_gameThread )
			{
				PC_SCOPE( DbgSrv_OnPacketReceive_Found_PushOnGameThread );

				// store for future processing		
				m_gameThreadMutex.Acquire();
				m_gameThreadCommands.PushBack( RED_NEW( DebugServerCommandData )( &commandDef, data ) );
				m_gameThreadMutex.Release();
			}
			// run immediately
			else
			{
				PC_SCOPE( DbgSrv_OnPacketReceived_Found_Execute );

				// get handler
				CommandHandler Command = commandDef.m_handler;
				RED_ASSERT( Command, "Empty command handler detected!!" );

				// call if exists
				const Uint32 result = Command( commandDef.m_owner, data );

				// stats
				RED_THREADS_MEMORY_BARRIER();
				m_sentCount.ExchangeAdd( result );
			}	
		}

		// unknown command
		else
		{
			RED_LOG( "DebugServer - unknown command id [%s]..", commandName.AsChar() );
		}
	}

	// stats
	m_commandsTime.ExchangeAdd( tm.GetTicks()-begTicks );
}
//////////////////////////////////////////////////////////////////////////
//
// send stats to debugger client
void red::DebugServerManager::SendStats()
{
	// TODO!!! PB

#if 0

	// init
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "Stats" );

	// fill data
	// internal debugger time
	/*2*/packet.WriteString( ToString( (Double)m_commandsTime.GetValue() / 1000.0 ).AsChar() );

	// frame no
	/*3*/packet.WriteString( ToString( m_frame.GetValue() ).AsChar() );

	// memory stats
	const Int64 totalBytesAllocated = Memory::GetTotalBytesAllocated();
	/*4*/packet.WriteString( ToString( (Uint32)(totalBytesAllocated/1024) ).AsChar() );

	// last frame time
	/*5*/packet.WriteString( ToString( GEngine->GetLastTimeDelta() ).AsChar() );

	// engine time
	/*6*/packet.WriteString( ToString( (Double)GEngine->GetRawEngineTime() ).AsChar() );

	// network outgoing packet count
	const Uint32 outgoingPacketsCount = NetworkManager::GetInstance()->GetOutgoingPacketsCount();
	/*7*/packet.WriteString( ToString( outgoingPacketsCount ).AsChar() );

	// gpu frame time
	extern IRender* GRender;
	if ( GRender )
		/*8.9*/packet.WriteString( ToString( GRender->GetLastGPUFrameDuration() ).AsChar() );
	else
		/*8.9*/packet.WriteString( "0.0" );

	// avg fps
	/*9.9*/packet.WriteString( ToString( GEngine->GetLastTickRate() ).AsChar() );

	// min fps
	/*10.9*/packet.WriteString( ToString( GEngine->GetMinTickRate() ).AsChar() );

	// send
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );

	// stats
	m_sentCount.Increment();

#endif
}
//////////////////////////////////////////////////////////////////////////
//
// send frame time to debugger client
void red::DebugServerManager::SendFrameTime()
{
	// init
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "FrameTime" );

	//TODO!!! PB
	//// last frame time
	///*2*/packet.WriteString( ToString( GEngine->GetLastTimeDelta() ).AsChar() );

	// send
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );

	// stats
	m_sentCount.Increment();
}
//////////////////////////////////////////////////////////////////////////
//
// send game started
void red::DebugServerManager::SendGameStarted()
{
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "GameStarted" );
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );
}
//////////////////////////////////////////////////////////////////////////
//
// send game stopped
void red::DebugServerManager::SendGameStopped()
{
	ChannelPacket packet( RED_NET_CHANNEL_DEBUG_SERVER );
	/*1*/packet.WriteString( "GameStopped" );
	Send( RED_NET_CHANNEL_DEBUG_SERVER, packet );
}

#endif // NO_DEBUG_SERVER
