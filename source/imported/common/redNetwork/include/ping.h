/**
* Copyright (c) 2014 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_NETWORK_PING_H_
#define _RED_NETWORK_PING_H_

#include "manager.h"
#include "channel.h"

namespace red
{
	namespace Network
	{
		//////////////////////////////////////////////////////////////////////////
		// Custom Ping/Pong protocol
		//////////////////////////////////////////////////////////////////////////
		class REDNETWORK_API Ping : public ChannelListener
		{
			RED_USE_MEMORY_POOL( PoolEngine );
		public:
			Ping();
			virtual ~Ping();

			// Listen and respond to pings
			Bool Initialize();

			// Send pings to specified address
			template< typename TChar >
			Bool ConnectTo( const TChar* ip, Uint16 port );

			Bool Send();
			virtual void OnPacketReceived( const AnsiChar* channelName, IncomingPacket& packet ) override final;
			
			virtual void OnPongReceived( Double ms ) { RED_UNUSED( ms ); }

		private:
			Bool m_initialized;

			static const AnsiChar* CHANNEL;
			static const UniChar* PING;
			static const UniChar* PONG;
		};

		template< typename TChar >
		Bool Ping::ConnectTo( const TChar* ip, Uint16 port )
		{
			if( m_initialized )
			{
				red::Network::Address address( ip, port );
				red::Network::Manager::GetInstance()->ConnectTo( address, CHANNEL );
			}

			return false;
		}
	}
}

#endif // _RED_NETWORK_PING_H_
