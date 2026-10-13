/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packetNetworkUtils.h"

namespace red
{
	namespace Network
	{
		typedef Uint32 PacketConnectionID;
		typedef Uint32 PacketListenerID;

		enum class ConnectionDirection
		{
			Incoming, //< We listened, they opened connection
			Outgoing //< They listened, we opened connection
		};

		// Implement this interface and pass it to Packet Network Manager to receive packets
		class REDNETWORK_API IPacketNetworkConnectionListener
		{
		public:
			virtual ~IPacketNetworkConnectionListener() {}

			virtual void OnListenerClosed( PacketListenerID listenerId ) = 0;
			virtual void OnConnectionClosed( PacketConnectionID connectionId ) = 0;

			// On packet received (called from network thread), return true if packet was handled
			virtual Bool OnPacket( Utils::Packet packet, PacketConnectionID remoteConnectionID ) = 0;

			virtual void OnConnection( PacketConnectionID connectionId, ConnectionDirection direction ) = 0;
		};

		// Manages packets - sending and receiving (dispatching to IPacketNetworkReceiver)
		class REDNETWORK_API IPacketNetworkManager
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			virtual ~IPacketNetworkManager() {}

			// Initialize network connection
			virtual Bool Initialize() = 0;

			// Shutdown network connection
			virtual void Shutdown() = 0;

			// Create new listener on given port
			virtual PacketListenerID CreateListener( const Utils::CommAddress& address, Bool reuseAddress, IPacketNetworkConnectionListener* listener ) = 0;

			// Closes given listener
			virtual void CloseListener( PacketListenerID id ) = 0;

			// Open new network connection
			virtual PacketConnectionID OpenConnection( const Utils::CommAddress& address, IPacketNetworkConnectionListener* listener ) = 0;

			// Closes given connection
			virtual void CloseConnection( PacketConnectionID id ) = 0;

			// Send new packet through network
			virtual void Send( PacketConnectionID target, Utils::Packet packet ) = 0;
		};
	}
}
