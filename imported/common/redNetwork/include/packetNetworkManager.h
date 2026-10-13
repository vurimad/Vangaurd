/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rawManager.h"

#include "packetNetworkInterfaces.h"
#include "packetNetworkPacketParser.h"

#include "../../redContainers/include/queue.h"

namespace red
{
	namespace Network
	{
		//------------------------------

		struct PacketConnection
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

			RED_INLINE PacketConnection( PacketConnectionID packetConnectionID, IPacketNetworkConnectionListener* packetListener, PacketListenerID ownerId = 0 )
				: m_packetConnectionId( packetConnectionID )
				, m_ownerId( ownerId )
				, m_packetListener( packetListener )
				, m_packetParser()
				, m_active( true )
			{}

			PacketConnectionID m_packetConnectionId;				//! Id of packet connection - you can close connection with this id or send data through it
			PacketListenerID m_ownerId;								//! Listener which owns that connection - if none listener owns it, then it's 0
			IPacketNetworkConnectionListener* m_packetListener;		//! Callback for packet handling
			PacketNetworkPacketParser m_packetParser;				//! Parses received data and returns packets if any is parsed
			red::Atomic<Bool> m_active;						//! true when connection is opened and false when closed
		};

		//------------------------------

		struct PacketListener
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

			PacketListenerID m_packetListenerId;					//! Id of packet listener - you can close listener with this id
			IPacketNetworkConnectionListener* m_packetListener;		//! Callback to packet handling, as listener is also a packet handler
		};

		//------------------------------

		class REDNETWORK_API PacketNetworkManager : public IPacketNetworkManager, public IRawListenerInterface, public IRawConnectionInterface, private red::Thread
		{
		public:
			PacketNetworkManager( red::UniquePtr< IRawManager > rawManager );
			~PacketNetworkManager();

			// Initialize network connection
			virtual Bool Initialize() override;

			// Shutdown network connection
			virtual void Shutdown() override;

			// Create new listener on given port
			virtual PacketListenerID CreateListener(const Utils::CommAddress& address, Bool reuseAddress, IPacketNetworkConnectionListener* listener) override;

			// Open new network connection
			virtual PacketConnectionID OpenConnection(const Utils::CommAddress& address, IPacketNetworkConnectionListener* listener) override;

			// Closes given listener (does not call IPacketNetworkConnectionListener::OnClosed)
			virtual void CloseListener(PacketListenerID id) override;

			// Closes given connection (does not call IPacketNetworkConnectionListener::OnClosed)
			virtual void CloseConnection(PacketConnectionID id) override;

			// Send new packet through network
			virtual void Send(PacketConnectionID target, Utils::Packet packet) override;

			//----------------------------------------------------------
			// Callbacks don't get mutex. They are always called from thread that has it already.
			//----------------------------------------------------------

			// IRawListenerInterface::OnClosed implementation
			virtual void OnClosed(const TListenerID listenerID) override;

			// Extended IRawListenerInterface::OnConnection implementation
			virtual bool OnConnection( const TListenerID listenerID, const TConnectionID connectionID, IRawConnectionInterface*& outConnectionInterface) override;

			// IRawConnectionInterface implementation
			virtual void OnDisconnected( const TConnectionID connectionID ) override;

			// IRawConnectionInterface implementation
			virtual void OnData( const void* data, const Uint32 dataSize, const TConnectionID connectionID ) override;

			//----------------------------------------------------------

		private:
			typedef red::UniquePtr< PacketConnection > PacketConnectionPtr;
			typedef red::UniquePtr< PacketListener > PacketListenerPtr;
			typedef red::Map< PacketConnectionID, PacketConnectionPtr >::iterator PacketConnectIterator;

			// Thread interface
			virtual void ThreadFunc() override;

			// Internal connection cleanup (does not call IPacketNetworkConnectionListener::OnClosed)
			void RemoveConnectionLockless( PacketConnectionID id, Bool notify );
			PacketConnectIterator RemoveConnectionLockless( PacketConnectIterator iter, Bool notify );

			// Internal connection cleanup (with IPacketNetworkConnectionListener::OnClosed call)
			void RemoveAndInformConnection(PacketConnectionID id);

			// Internal listener cleanup (does not call IPacketNetworkConnectionListener::OnClosed)
			void RemoveListener(PacketListenerID id, Bool notify = false);

			// Internal listener cleanup (with IPacketNetworkConnectionListener::OnClosed call)
			void RemoveAndInformListener(PacketListenerID id);

			// ---- Raw network ---- //
			red::UniquePtr< IRawManager> m_network;			//! Access to network

			// Simple entry for packet sending - contains target connection ID and packet handle
			// Does not contain packet header, which is created just before sending the packet
			struct PacketToSend
			{
				RED_INLINE PacketToSend( PacketToSend&& packetTosend )
					: m_target( std::move( packetTosend.m_target ) )
					, m_packet( std::move( packetTosend.m_packet ) )
				{
				}

				RED_INLINE PacketToSend& operator=( PacketToSend&& packetTosend )
				{
					if ( this != &packetTosend )
					{
						m_target = std::move( packetTosend.m_target );
						m_packet = std::move( packetTosend.m_packet );
					}
					return *this;
				}

				RED_INLINE PacketToSend ( PacketConnectionID target, Utils::Packet packet )
					: m_target( target )
					, m_packet( std::move( packet ) )
				{
				}

				PacketConnectionID m_target;		//! Packet receiver (where to send)
				Utils::Packet m_packet;				//! Packet content (what to send)

				RED_INLINE Bool IsValid() const { return m_packet.Size() > 0 && m_target != 0; }
			};

			red::Atomic< Bool >									m_sendingThreadActive;		//! When false, sending thread will exit
			red::Mutex											m_packetsToSendQueueMutex;	//! Lock sending queue, as it is accessed by sending thread
			red::Queue< PacketToSend >							m_packetsToSendQueue;		//! Queue of packets to send
			constexpr const static Uint32 InitialCapacity		= 100;						//! Initial capacity of the queue

			red::Map< PacketConnectionID, PacketConnectionPtr >	m_connections{ red::PoolEngine() };				//! Network connections, decoupled from RawConnections, so it's safe to release them
			red::Map< PacketListenerID, PacketListenerPtr >		m_listeners{ red::PoolEngine() };				//! Network listeners, decoupled from RawListeners, so it's safe to release them
			red::ManualResetEvent								m_sendingThreadEvent;		//! Wakes sending thread when there's something to send
		};
	}
}
