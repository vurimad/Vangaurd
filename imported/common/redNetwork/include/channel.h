/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "packet.h"
#include "address.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace red
{
	namespace Network
	{
		//////////////////////////////////////////////////////////////////////////
		// Packet
		//////////////////////////////////////////////////////////////////////////
		class REDNETWORK_API ChannelPacket : public OutgoingPacket
		{
		public:
			ChannelPacket( const AnsiChar* channelName );
			~ChannelPacket();

			void Clear( const AnsiChar* channelName );
		};

		//////////////////////////////////////////////////////////////////////////
		// Listener
		//////////////////////////////////////////////////////////////////////////
		class REDNETWORK_API ChannelListener
		{
		public:
			virtual ~ChannelListener() {}
			virtual void OnPacketReceived( const AnsiChar* channelName, IncomingPacket& packet ) = 0;
		};

		//////////////////////////////////////////////////////////////////////////
		// Channel
		//////////////////////////////////////////////////////////////////////////
		class REDNETWORK_API Channel
		{
		private:
			static const Uint32 MAX_DESTINATIONS = 16;
			static const Uint32 MAX_LISTENERS = 32;

		public:
			static const Uint32 NAME_MAX_LENGTH = 32;

		public:
			typedef red::StaticArray< Socket*, MAX_DESTINATIONS > DestinationArray;

		public:
			Channel();
			~Channel();

			void operator=( const Channel& ) {}

			RED_INLINE void SetName( const AnsiChar* name ) { red::Strcpy( m_name, name, NAME_MAX_LENGTH ); }
			RED_INLINE const AnsiChar* GetName() const { return m_name; }

			void ReceivePacket( IncomingPacket& packet );

			void RegisterListener( ChannelListener* listener );
			void UnregisterListener( ChannelListener* listener );

			void RegisterDestination( Socket* destination );
			void UnregisterDestination( Socket* destination );

			RED_INLINE DestinationArray& GetDestinations() { return m_destinations; }

		private:
			AnsiChar m_name[ NAME_MAX_LENGTH ];

			// List of sockets with which to send outgoing packets associated with this channel
			DestinationArray m_destinations;

			// List of receivers to which incoming packets on this channel will be sent
			red::Mutex m_listenerLock;
			red::StaticArray< ChannelListener*, MAX_LISTENERS > m_listeners;
		};
	}
}
