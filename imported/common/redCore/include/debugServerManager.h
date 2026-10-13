/**
 * Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include <functional>
#include "redCoreApi.h"
#include "singleton.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redNetwork/include/channel.h"

#define RED_NET_CHANNEL_DEBUG_SERVER "DebugServer"

#ifdef RED_DEBUG_SERVER_ENABLED


//////////////////////////////////////////////////////////////////////////
// defines

namespace comm
{
	class ChannelFactory;
}

namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// forwards
	class DebugServerPlugin;


	//////////////////////////////////////////////////////////////////////////
	// consts
	const Uint32 DebugServerAPI = 20;


	//////////////////////////////////////////////////////////////////////////
	// typedefs
	typedef std::function<Uint32( DebugServerPlugin* owner, const red::DynArray< red::String >& data )> CommandHandler;


	//////////////////////////////////////////////////////////////////////////
	// structs
	struct REDCORE_API DebugServerCommand
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

		RED_INLINE DebugServerCommand()
		{
			m_gameThread = false;
			m_owner = nullptr;
			m_handler = nullptr;
		}

		RED_INLINE DebugServerCommand( DebugServerPlugin* owner, CommandHandler handler, Bool gameThread )
		{
			m_gameThread = gameThread;
			m_owner = owner;
			m_handler = handler;
		}

		Bool				m_gameThread;
		DebugServerPlugin*	m_owner;
		CommandHandler		m_handler;
	};

	struct REDCORE_API DebugServerCommandData
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

		RED_INLINE DebugServerCommandData( DebugServerCommand* command, const red::DynArray< red::String >& data )
		{
			m_command = command;
			m_data = data;
		}

		DebugServerCommand*				m_command;
		red::DynArray< red::String >	m_data{ red::PoolDebug() };
	};

	//////////////////////////////////////////////////////////////////////////
	// declarations
	class REDCORE_API DebugServerManager : public red::Network::ChannelListener
	{
	public:

		RED_USE_MEMORY_POOL( red::PoolDebug );

		DebugServerManager();
		~DebugServerManager();

		// common
		Bool Init( comm::ChannelFactory& channelFactory );
		Bool ShutDown();
		void Send( const AnsiChar* channelName, const red::Network::ChannelPacket& packet );

		// states
		RED_INLINE const Bool IsInitialized() { return m_initialized.GetValue(); }
		RED_INLINE const Bool IsConnected() { return m_connected.GetValue(); }
		RED_INLINE const Bool IsAttached() { return m_attached.GetValue(); }
		RED_INLINE const Bool IsGameRunning() { return m_gameRunning.GetValue(); }
		RED_INLINE const Bool IsPropertiesTracing() { return m_propsTrace.GetValue(); }
		RED_INLINE const Bool IsDebugMode() { return m_debugMode; }
		RED_INLINE void SetConnected() { m_connected.SetValue( true ); }
		RED_INLINE void SetDisconnected() { m_connected.SetValue( false ); }
		RED_INLINE void EnableFrameTimeLogging( Bool enable ) { m_frameTimeLogging = enable; }

		// accessors
		RED_INLINE const Uint32 GetAPIVersion() { return DebugServerAPI; }
		RED_INLINE void SetName( const AnsiChar* name ) { m_name = name; }
		RED_INLINE const AnsiChar* GetName() { return m_name; }
		RED_INLINE const Uint32 GetReceivedCount() { return m_receivedCount.GetValue(); }
		RED_INLINE const Uint32 GetSentCount() { return m_sentCount.GetValue(); }
		RED_INLINE const Uint32 GetGameThreadCommandsCount() { return m_gameThreadCommands.Size(); }

		// life-time
		void GameStarted();
		void GameStopped();
		void Update();

		// property tracking
		void StartPropsTrace();
		void FinishPropsTrace();
		Bool RegisterNativeProperty( const red::String& propName );
		Bool SetPropertyValue( const red::String& propName, red::String value );

		// log
		void Log( const red::String& channel, const AnsiChar* format, ... );

		// plugins
		Bool RegisterPlugin( DebugServerPlugin* plugin, Bool ownedByManager );
		Bool UnregisterPlugin( DebugServerPlugin* plugin );

		// commands
		Bool RegisterCommandHandler( DebugServerPlugin* owner, const red::String& commandName, const Bool gameThread, CommandHandler handler );
		Bool UnregisterCommandHandler( const red::String& commandName );

	protected:

		// common
		const AnsiChar*					m_name;

		// stats
		red::Atomic< Uint64 >			m_commandsTime;
		red::Atomic< Uint32 >			m_sentCount;
		red::Atomic< Uint32 >			m_receivedCount;
		red::Atomic< Uint32 >			m_frame;

		// game thread queue
		red::Mutex											m_gameThreadMutex;
		red::DynArray< DebugServerCommandData* >			m_gameThreadCommands;

		// commands map
		red::HashMap< red::String, DebugServerCommand >		m_commandMap;

		// plugins
		red::DynArray< DebugServerPlugin* >					m_plugins;
		red::DynArray< DebugServerPlugin* >					m_ownedPlugins;

		// properties
		red::HashMap< red::String, red::String >			m_propertiesMap;

		// states
		red::Atomic< Bool >				m_initialized;
		red::Atomic< Bool >				m_connected;
		red::Atomic< Bool >				m_attached;
		red::Atomic< Bool >				m_gameRunning;
		red::Atomic< Bool >				m_propsTrace;
		Bool							m_debugMode;
		Bool							m_sendStatsEnabled;
		Bool							m_frameTimeLogging;

	private:

		// listener to remote clients
		virtual void OnPacketReceived( const AnsiChar* channelName, red::Network::IncomingPacket& packet ) override final;

		// helpers
		void SendStats();
		void SendGameStarted();
		void SendGameStopped();
		void SendProperties();
		void SendPropertiesNames();
		void SendFrameTime();

		// friendships
		// friend class DebugServerCommandAreWeConnected;
	};
}


//////////////////////////////////////////////////////////////////////////
// Singleton as requested (vs e.g., part of backend engine)
REDCORE_API red::DebugServerManager& GetDbgManager();


//////////////////////////////////////////////////////////////////////////
// macros
#define DBGSRV()  GetDbgManager()
#define DBGSRV_CALL( func )  GetDbgManager().func
#define DBGSRV_REG_PLUGIN( plugin, ownedByManager )  GetDbgManager().RegisterPlugin( plugin, ownedByManager )
#define DBGSRV_UNREG_PLUGIN( plugin )  GetDbgManager().UnregisterPlugin( plugin )
#define DBGSRV_REG_COMMAND( commandName, handler, gameThreadOnly )  GetDbgManager().RegisterCommandHandler( this, ( commandName ), gameThreadOnly, handler )
#define DBGSRV_UNREG_COMMAND( commandName )  GetDbgManager().UnregisterCommandHandler( commandName )
#define DBGSRV_REG_NATIVE_PROP( propName )  GetDbgManager().RegisterNativeProperty( propName )
#define DBGSRV_SET_NATIVE_PROP_VALUE( propName, value )  GetDbgManager().SetPropertyValue( propName, ToString( value ) )
#define DBGSRV_SET_STRING_PROP_VALUE( propName, value )  GetDbgManager().SetPropertyValue( propName, value )
#define DBGSRV_LOG( channel, format, ... )  GetDbgManager().Log( channel, format, __VA_ARGS__ )

#else

#define DBGSRV() (0(void))
#define DBGSRV_CALL( func ) false
#define DBGSRV_REG_PLUGIN( plugin, ownedByManager ) false
#define DBGSRV_UNREG_PLUGIN( plugin ) false
#define DBGSRV_REG_COMMAND( commandName, handlerClass, gameThreadOnly ) false
#define DBGSRV_UNREG_COMMAND( commandName ) false
#define DBGSRV_REG_NATIVE_PROP( propName ) false
#define DBGSRV_SET_NATIVE_PROP_VALUE( propName, value ) false
#define DBGSRV_SET_STRING_PROP_VALUE( propName, value ) false
#define DBGSRV_LOG( channel, format, ... ) (0(void))

#endif // NO_DEBUG_SERVER
