/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/queue.h"

#include "network.h"

#include "address.h"
#include "socket.h"
#include "packet.h"
#include "channel.h"
#include "memory.h"

namespace red
{
	namespace Network
	{
		class REDNETWORK_API ConnectionEventListener
		{
		public:
			ConnectionEventListener() {}
			virtual ~ConnectionEventListener();

			// Called when a connection to a peer completed
			virtual void OnConnectionSucceeded( const Address& ) {}

			// Called when there is already a valid connection to a specified peer
			virtual void OnConnectionAvailable( const Address& ) {}

			// Called when a connection attempt fails/times out
			virtual void OnConnectionDropped( const Address& ) {}

			// Called when a connection is gracefully closed
			virtual void OnConnectionClosed( const Address& ) {}
		};

		class REDNETWORK_API Manager : private red::Thread
		{
			RED_USE_MEMORY_POOL( PoolEngine );

		private:
			struct Connection;
			struct PendingPacket;

		private:
			virtual void ThreadFunc();

			// Platform specific initialization, and any other important startup functionality this system requires
			Bool InitializeInternal();

			// Main update loop for the network system
			void Update();

			// Closes all connections and unregisters all channels and listeners (and any necessary platform specific shutdown functionality)
			void Shutdown();

			// Update loop

			// accept() new incoming connections
			void CheckForNewConnections();

			// Check newly established outgoing sockets to see if they've connected successfully
			void UpdatePendingConnections();

			// recv() incoming data on each of the connected sockets and pass it through to various appropriate listeners
			void UpdateIncoming();

			// send() queued outgoing data on specified sockets
			void UpdateOutgoing();

			// Suspend the thread until one of the active sockets needs attention
			void WaitUntilNeeded();

			void Wake();

			// Examine packet and figure out what to do with it
			void ProcessIncomingPacket( IncomingPacket& packet, Socket& socket );

			void BindRemoteChannelToSocket( Channel* channel, Socket* socket );

			// Send packet to specified channel
			void ToChannel( const AnsiChar* channelName, IncomingPacket& packet );

			// Find an existing socket connected to the specified target address, or create one if it doesn't find one
			// Returns nullptr if there is no more space available for a new connection
			Connection* CreateConnection( const Address& target );

			// Find an existing socket connected to the specified target address
			// Returns nullptr if it doesn't exist
			Connection* FindConnection( const Address& target );

			// Cleanup everything related to a connection
			void RemoveConnection( Socket* socket );

			// Find an existing channel with the specified name
			// Returns nullptr if there isn't one
			Channel* FindChannelLock( const AnsiChar* channelName );
			Channel* FindChannelNoLock( const AnsiChar* channelName );

			// Find an existing channel with the specified name, or create one if it doesn't find one
			// Returns nullptr if there is no more space available for a new channel
			Channel* CreateChannelInternal( const AnsiChar* channelName );

			// Send a packet to a specific destination
			red::Bool Send( Socket* destination, const OutgoingPacket& packet );

		public:

			Manager();
			virtual ~Manager();

			void Initialize();

			// Instruct the network system to shut down gracefully
			RED_INLINE void QueueShutdown()
			{
				m_shutdown.SetValue( true );
				Wake();
			}

			// Only call after having called QueueShutdown()!
			RED_INLINE void WaitForShutdown()
			{
				JoinThread();
			}

			// Instruct the network system to shut down gracefully
			// And wait for it to do so
			RED_INLINE void ImmediateShutdown()
			{
				QueueShutdown();
				WaitForShutdown();
			}

			// Is the system ready for use
			RED_INLINE Bool IsInitialized() const { return m_initialized.GetValue(); }

			// Returns true if channel exists or has been created
			red::Bool CreateChannel( const AnsiChar* channelName )
			{
				if( CreateChannelInternal( channelName ) )
				{
					return true;
				}

				return false;
			}

			// Activate the server listen socket on first available port in given range
			// This is needed to allow for incoming connections to be established
			red::Bool ListenForIncomingConnections( Uint16 startPort, Uint16 numPortsToTry = 1 );

			// Gets port on which we're listening for incoming connections
			Uint16 GetIncomingConnectionListeningPort() { return m_listenerPort; }

			// Send a packet to all destinations registered with a specific channel
			red::Bool Send( const AnsiChar* channelName, const OutgoingPacket& packet );

			// Register a new listener to receive all packets sent to a specific channel
			red::Bool RegisterListener( const AnsiChar* channelName, ChannelListener* listener );

			// Stop listening to packets on a specific channel
			red::Bool UnregisterListener( const AnsiChar* channelName, ChannelListener* listener );

			// Stop listening for connection events
			void UnregisterListener( ConnectionEventListener* listener );

			Uint32 GetNumberOfConnectionsToChannel( const AnsiChar* channelName );

			// Request a new connection be made to a specific target on the specified channel
			// An optional listener parameter may be specified to listen for connection events
			Bool ConnectTo( const Address& target, const AnsiChar* channelName, ConnectionEventListener* listener = nullptr );

			// Close socket between self and target address
			Bool DisconnectFrom( const Address& target );

			// Return no. of outgoing packets
			RED_INLINE Uint32 GetOutgoingPacketsCount() { return m_outgoingPackets.Size(); }

			RED_INLINE Bool IsListenSocketActive() const { return m_listener.IsListening(); }

		public:
			static const Uint16 DEFAULT_PORT = 37000;
			static const Uint16 MAX_CONNECTIONS = 16;
			static const Uint16 MAX_CHANNELS = 16;
			static const Uint16 MAX_LISTENERS = 16;
			
			static const AnsiChar BIND_TO_CHANNEL_PACKET_IDENTIFIER[];

		private:
			// A Connection consists of:
			// A connected socket
			// A packet to store incoming data (which will be passed through to listeners)
			// Associated connection listeners to be notified when the socket's state changes
			struct Connection
			{
				RED_INLINE Connection( Socket socket ) 
					:  m_socket( std::move(socket))
				{}

				Socket m_socket;
				IncomingPacket m_incoming;

				red::StaticArray< ConnectionEventListener*, MAX_LISTENERS > m_listeners;
			};

			// A pending packet consists of:
			// A packet of data ready to be sent
			// A list of connections to send the packet to
			struct PendingPacket
			{
				red::StaticArray< OutgoingPacketDestination, MAX_CONNECTIONS > m_destinations;
				OutgoingPacket m_packet;
			};

			// A Pending Connection consists of:
			// A socket that was State_Connecting at the time of being added
			// The name of the channel to be associated with the socket in the event of a successful connection
			struct PendingConnection
			{
				Socket* m_socket;
				AnsiChar m_channelName[ Channel::NAME_MAX_LENGTH ];
			};

		private:
			// Start up/Shutdown
			red::Atomic< Bool > m_initialized;
			red::Atomic< Bool > m_shutdown;
			
			// Server listen() and accept() socket
			Socket m_listener;
			Uint16 m_listenerPort;

			// Dummy socket used to trigger thread wakeup when we're sending data
			Socket m_threadWaker;
			Address m_threadWakerBoundAddress;

			// For both connections and pending connections
			red::Mutex m_lock;

			// Connection list and associated mutex to be locked when the list is accessed
			red::StaticArray< Connection, MAX_CONNECTIONS > m_connections;

			// Pending connection list and associated mutex to be locked when the list is accessed
			red::StaticArray< PendingConnection, MAX_CONNECTIONS > m_pendingConnections;

			red::Mutex m_channelLock;

			// Channels are never removed, only added
			red::StaticArray< Channel, MAX_CHANNELS > m_channels;

			// Pending connection list and associated mutex to be locked when the list is accessed
			red::Queue< PendingPacket > m_outgoingPackets{ red::PoolEngine() };

			static Manager* m_instance;

		public:
			static Manager* GetInstance() { return m_instance; }
		};
	}
}
