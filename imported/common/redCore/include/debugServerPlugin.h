/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redMemory/include/uniquePtr.h"
#include <functional>

namespace red
{
	namespace Network
	{
		class ChannelPacket;
	}

	class REDCORE_API DebugServerPlugin
	{
	public:
		RED_USE_MEMORY_POOL( red::PoolDebug );

		RED_INLINE DebugServerPlugin() {}
		virtual ~DebugServerPlugin() {}

		// common
		virtual Bool Init() = 0;
		virtual Bool ShutDown() = 0;

		// life-time
		virtual void GameStarted() = 0;
		virtual void GameStopped() = 0;
		virtual void Update() = 0;
	};

	class REDCORE_API DebugServerPluginImpl : public DebugServerPlugin, red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:

		struct Functor
		{
			RED_USE_MEMORY_POOL( red::PoolDebug );

			virtual ~Functor() {}
			virtual void Call( const red::DynArray< red::String >& args ) = 0;
			virtual Uint32 GetArgsCount() const = 0;
			template < typename T >
			RED_INLINE T Parse( const red::String& str );
		};

		DebugServerPluginImpl( const red::String& pluginName );
		virtual ~DebugServerPluginImpl();

		virtual Bool Init();
		virtual Bool ShutDown();
		virtual void GameStarted();
		virtual void GameStopped();
		virtual void Update();

		template < typename... Args >
		void Call( const red::String& targetPluginName, const red::String& functorName, const Args&... args );

		template < typename T1, typename T2, typename T3, typename T4, typename T5 >
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3, T4, T5 ) >& func );
		template < typename T1, typename T2, typename T3, typename T4 >
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3, T4 ) >& func );
		template < typename T1, typename T2, typename T3 >
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( T1, T2, T3 ) >& func );
		template < typename T1, typename T2 >
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( T1, T2 ) >& func );
		template < typename T1 >
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( T1 ) >& func );
		RED_INLINE Bool RegisterFunctor( const red::String& name, const std::function< void( void ) >& func );

	private:

		typedef red::UniquePtr< Functor > FunctorPtr;
		typedef red::HashMap< red::String, FunctorPtr > FunctorsMap;

		red::String m_pluginName;				
		FunctorsMap m_functors{ red::PoolDebug() };

		void ProcessCommand( const red::DynArray< red::String >& data );
		void Send( red::Network::ChannelPacket& packet ) const;

		friend Uint32 Command_DebugServerPluginImpl( red::DebugServerPlugin* owner, const red::DynArray< red::String >& data );
	};
}

#include "debugServerPlugin.hpp"