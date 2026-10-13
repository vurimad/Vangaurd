/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packetNetworkManager.h"
#include "packetNetworkPacketParser.h"
#include "packetNetworkSendingDispositor.h"
#include "packetNetworkInterfaces.h"
#include "rawManager.h"

namespace red
{
	namespace Network
	{
		namespace Consts
		{
			PacketConnectionID InvalidPacketConnectionId = 0;
		}

		//------------------------------

		namespace Helper
		{
			RED_INLINE PacketConnectionID TranslateToPacketConnectionID( TConnectionID connectionId ) { return static_cast<PacketConnectionID>( connectionId ); }
			RED_INLINE TConnectionID TranslateToConnectionID( PacketConnectionID packetConnectionId ) { return static_cast<TConnectionID>( packetConnectionId ); }

			RED_INLINE PacketConnectionID TranslateToPacketListenerID( TListenerID listenerId ) { return static_cast<PacketConnectionID>( listenerId ); }
			RED_INLINE TListenerID TranslateToListenerID( PacketConnectionID packetConnectionId ) { return static_cast<TListenerID>( packetConnectionId ); }
		}

		//------------------------------
		PacketNetworkManager::PacketNetworkManager( red::UniquePtr< IRawManager > rawManager )
			: red::Thread( "PacketNetworkManager", { RED_KILO_BYTE( 128 ) } )
			, m_network( std::move( rawManager ) )
			, m_packetsToSendQueue( red::PoolEngine() )
			, m_listeners( red::PoolEngine() )
		{
			m_packetsToSendQueue.Reserve( InitialCapacity );
		}

		PacketNetworkManager::~PacketNetworkManager()
		{
		}

		//------------------------------

		Bool PacketNetworkManager::Initialize()
		{
			m_network->Initialize();
			m_sendingThreadActive.SetValue( true );
			InitThread();

#ifdef RED_PLATFORM_CONSOLE
			SetAffinityMask(0x7F);//intentional - share with watchdog
			SetPriority(red::TP_TimeCritical);
#else
			SetPriority(red::TP_AboveNormal);
#endif

			return true;
		}

		void PacketNetworkManager::Shutdown()
		{
			m_network->Shutdown();
			m_sendingThreadActive.SetValue( false );
			m_sendingThreadEvent.SetEvent();

			JoinThread();

			m_connections.Clear();
			m_listeners.Clear();
		}

		red::Network::PacketConnectionID PacketNetworkManager::OpenConnection( const Utils::CommAddress& address, IPacketNetworkConnectionListener* listener )
		{
			red::ScopedLock<red::Network::IRawManager> lock( *m_network );

			TConnectionID connectionId = m_network->CreateConnection( address, this );

			if ( connectionId )
			{
				PacketConnectionID packetConnectionId = Helper::TranslateToPacketConnectionID( connectionId );

				PacketConnectionPtr connection = CreateUniquePtr< PacketConnection >( packetConnectionId, listener );
				m_connections.Set( packetConnectionId, std::move( connection ) );

				return packetConnectionId;
			}
			return Consts::InvalidPacketConnectionId;
		}

		void PacketNetworkManager::Send( PacketConnectionID target, Utils::Packet packet )
		{
			RED_ASSERT( packet.Size() <= red::Network::MAX_PACKET, "You can't send packet bigger than %u. Requested size=%u", red::Network::MAX_PACKET, packet.Size() );

			red::ScopedLock<red::Mutex> lockGuard( m_packetsToSendQueueMutex );
			m_packetsToSendQueue.Push( { target, std::move( packet ) } );
			m_sendingThreadEvent.SetEvent();
		}

		red::Network::PacketListenerID PacketNetworkManager::CreateListener( const Utils::CommAddress& address, Bool reuseAddress, IPacketNetworkConnectionListener* listener )
		{
			red::ScopedLock<red::Network::IRawManager> lock( *m_network );

			TListenerID listenerId = m_network->CreateListener( address, reuseAddress, this );
			if( listenerId == 0 )
				return 0;

			PacketListenerID packetListenerId = Helper::TranslateToPacketListenerID( listenerId );

			PacketListenerPtr packetListener = CreateUniquePtr< PacketListener >();
			packetListener->m_packetListenerId = packetListenerId;
			packetListener->m_packetListener = listener;

			m_listeners.Set( packetListenerId, std::move( packetListener ) );

			return packetListenerId;
		}

		void PacketNetworkManager::CloseListener( PacketListenerID id )
		{
			red::ScopedLock<red::Network::IRawManager> lock( *m_network );

			TListenerID listenerID = Helper::TranslateToListenerID( id );
			Bool listenerClosed = m_network->CloseListener( listenerID );
			if( listenerClosed )
			{
				RemoveListener( id );
			}
		}

		void PacketNetworkManager::CloseConnection( PacketConnectionID id )
		{
			red::ScopedLock<red::Network::IRawManager> lock( *m_network );

			TConnectionID connectionID = Helper::TranslateToConnectionID( id );
			Bool connectionClosed = m_network->CloseConnection( connectionID );
			if( connectionClosed )
			{
				RemoveConnectionLockless( id, false );
			}
		}

		void PacketNetworkManager::ThreadFunc()
		{

#ifdef RED_PLATFORM_CONSOLE
			//SetAffinityMask( RED_FLAG( 6 ) );
#endif

			while( m_sendingThreadActive.GetValue() )
			{
				m_sendingThreadEvent.Wait();
				m_sendingThreadEvent.ResetEvent();

				red::Queue< PacketToSend > packetsToSendQueue{ red::PoolEngine() };
				packetsToSendQueue.Reserve( InitialCapacity );
				{
					red::ScopedLock< red::Mutex > lockGuard( m_packetsToSendQueueMutex );
					packetsToSendQueue.Swap( m_packetsToSendQueue );
				}

				while ( !packetsToSendQueue.Empty() )
				{
					// Get next packet to send
					const PacketToSend& packetToSend = packetsToSendQueue.Pop();
					RED_FATAL_ASSERT( packetToSend.IsValid() );

					HeaderUtils::Header header = HeaderUtils::CreatePacketHeaderFor( packetToSend.m_packet );

					Utils::DataDisposer dataDisposer( 2 );
					dataDisposer.AddDataBlock( &header, sizeof( HeaderUtils::Header ) );
					dataDisposer.AddDataBlock( packetToSend.m_packet.Data(), packetToSend.m_packet.Size() );

					// Send packetB
					TConnectionID connectionId = Helper::TranslateToConnectionID( packetToSend.m_target );

					const Uint32 maxPacketSize = RED_MEGA_BYTE( 2 );

					while ( !dataDisposer.IsEndOfData() )
					{
						const void* data = nullptr;
						Uint32 dataSize = 0;

						dataDisposer.GetNextData( data, dataSize );

						Uint32 dataSizeToSend = dataSize;

						Bool isConnectionActive = true;
						do
						{
							Uint32 bytesSent = 0;

							{
								red::ScopedLock<red::Network::IRawManager> lock( *m_network );

								const Uint32 packageSize = Min( dataSizeToSend, maxPacketSize );
								bytesSent = m_network->Send( connectionId, data, packageSize );
								dataSizeToSend -= bytesSent;
								dataDisposer.UpdateState( bytesSent );
								dataDisposer.GetNextData( data, dataSize );

								if ( !bytesSent && !m_network->IsConnectionActive( connectionId ) )
								{
									isConnectionActive = false;
									break;
								}
								else if ( !dataSizeToSend )
								{
									break;
								}
							}

							if ( !bytesSent )
							{
								red::SleepOnCurrentThread( 1 );
							}

						} while ( true );

						if ( !isConnectionActive )
						{
							break;
						}
					}
				}

#ifdef RED_PLATFORM_CONSOLE
				//red::SleepOnCurrentThread( 1 );
#endif
			}
		}

		void PacketNetworkManager::OnClosed( const TListenerID listenerID )
		{
			PacketListenerID id = Helper::TranslateToPacketListenerID( listenerID );
			RemoveAndInformListener( id );
		}

		bool PacketNetworkManager::OnConnection( const TListenerID listenerID, const TConnectionID connectionID, IRawConnectionInterface*& outConnectionInterface )
		{
			PacketListenerID packetListenerID = Helper::TranslateToPacketListenerID( listenerID );

			auto listener = m_listeners.Find( packetListenerID );
			if( listener == m_listeners.End() )
				return false;

			PacketConnectionID packetConnectionID = Helper::TranslateToPacketConnectionID( connectionID );

			PacketConnectionPtr connection = CreateUniquePtr< PacketConnection >( packetConnectionID, listener.Value()->m_packetListener, packetListenerID );

			m_connections.Set( packetConnectionID, std::move( connection ) );
			outConnectionInterface = this;

			listener.Value()->m_packetListener->OnConnection( packetConnectionID, ConnectionDirection::Incoming );

			return true;
		}

		void PacketNetworkManager::OnDisconnected( const TConnectionID connectionID )
		{
			PacketConnectionID id = Helper::TranslateToPacketConnectionID( connectionID );
			RemoveAndInformConnection( id );
		}

		void PacketNetworkManager::OnData( const void* data, const Uint32 dataSize, const TConnectionID connectionID )
		{
			PacketConnectionID packetConnectionID = Helper::TranslateToPacketConnectionID( connectionID );

			auto iter = m_connections.Find( packetConnectionID );
			if( iter == m_connections.End() )
			{
				return;
			}

			PacketConnectionPtr& connection = iter.Value();

			// Parse incoming data and extract packets if possible
			red::Network::PacketArray receivedPackets{ red::PoolEngine() };
			connection->m_packetParser.AppendAndParse( data, dataSize, receivedPackets );

			// Iterate through all received packets
			for ( Uint32 i = 0; i < receivedPackets.Size(); ++i )
			{
				connection->m_packetListener->OnPacket( std::move( receivedPackets[ i ] ), packetConnectionID );
			}
		}

		RED_INLINE PacketNetworkManager::PacketConnectIterator PacketNetworkManager::RemoveConnectionLockless( PacketConnectIterator iter, Bool notify )
		{
			if ( iter != m_connections.End() )
			{
				if ( notify )
				{
					iter.Value()->m_packetListener->OnConnectionClosed( iter.Value()->m_packetConnectionId );
				}

				return m_connections.Remove( iter ).Iterator();
			}

			return m_connections.End();
		}

		RED_INLINE void PacketNetworkManager::RemoveConnectionLockless( PacketConnectionID id, Bool notify )
		{
			auto conn = m_connections.Find( id );
			RemoveConnectionLockless( conn, notify );
		}

		void PacketNetworkManager::RemoveListener( PacketListenerID listenerId, Bool notify )
		{
			auto listener = m_listeners.Find( listenerId );

			if( listener == m_listeners.End() )
				return;

			if( notify )
			{
				listener.Value()->m_packetListener->OnListenerClosed( listenerId );
			}

			m_listeners.Remove( listener );

			auto it = m_connections.Begin();
			while( it != m_connections.End() )
			{
				if ( it.Value()->m_ownerId == listenerId )
				{
					it = RemoveConnectionLockless( it, notify );
				}
				else
				{
					++it;
				}
			}
		}

		void PacketNetworkManager::RemoveAndInformListener( PacketListenerID id )
		{
			RemoveListener( id, true );
		}

		void PacketNetworkManager::RemoveAndInformConnection( PacketConnectionID id )
		{
			RemoveConnectionLockless( id, true );
		}
	}
}