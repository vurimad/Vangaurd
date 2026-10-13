/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redSystemPublic.h"

#include "network.h"
#include "address.h"

namespace red
{
	namespace Network
	{
		class REDNETWORK_API Socket : public red::NonCopyable
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:

			enum State
			{
				State_Uninitialised = 0,
				State_Unbound,
				State_Bound,
				State_Listening,
				State_Connecting,
				State_Connected,

				State_Dropped,			//!< Connection timed out
				State_ShuttingDown,		//!< Connection shutting down
				State_ShutDown,			//!< Connection shut down
				State_Closed,			//!< Connection closed

				State_Max
			};

			enum AddressType
			{
				IPv4 = 0,
				IPv6
			};

			enum Protocol
			{
				TCP = 0,
				UDP
			};

			Socket();
			Socket( SocketId connectedSocket, State state, const Address& peer );
			Socket( Socket&& socket );
			~Socket();

			Socket& operator=( Socket&& other );

			// Interface
			Bool Create( Bool reuseAddress = false, Protocol protocol = TCP, AddressType addressType = IPv4 );
			Bool Bind( const Address& address );
			Bool Bind( Uint16 port );
			Bool Listen();
			Socket Accept();
			Bool Connect( const Address& destination );

			// Return true if connection procedure has finished, false otherwise.
			Bool FinishConnecting();
			Bool IsReady() const;

			Uint32 Send( const void* buffer, Uint32 size );
			Uint32 Receive( void* buffer, Uint32 size );

			Uint32 SendTo( const void* buffer, Uint32 size, const Address& destination );
			Uint32 ReceiveFrom( void* buffer, Uint32 size, Address& source );

			Bool SetOptionReuseAddress( Bool enabled );

			RED_INLINE void Shutdown( int how ) { Shutdown( State_ShuttingDown, how ); }
			RED_INLINE void Close() { Close( State_Closed ); }

			RED_INLINE Bool IsOpen() const { return static_cast< Uint32 >( m_state ) >= State_Bound; }
			RED_INLINE Bool IsUnbound() const { return m_state == State_Unbound; }
			RED_INLINE Bool IsBound() const { return m_state == State_Bound; }
			RED_INLINE Bool IsListening() const { return m_state == State_Listening; }
			RED_INLINE Bool IsConnected() const { return m_state == State_Connected; }
			RED_INLINE Bool IsConnecting() const { return m_state == State_Connecting; }
			RED_INLINE Bool IsShuttingDown() const { return m_state == State_ShuttingDown; }
			RED_INLINE Bool IsShutDown() const { return m_state == State_ShutDown; }
			RED_INLINE Bool IsClosed() const { return m_state == State_Closed; }
			RED_INLINE Bool IsDropped() const { return m_state == State_Dropped; }
			RED_INLINE Bool operator==( const Socket& other ) const { return m_descriptor == other.m_descriptor; }

			RED_INLINE const Address& GetPeer() const { return m_peer; }

			RED_INLINE SocketId GetRawDescriptor() const { return m_descriptor; }
			RED_INLINE Int32 GetLastError() const { return m_lastErrorCode; }

		private:
			Bool GetPeer( Address& peer ) const;
			void Shutdown( State resultantState, int how );
			void Close( State resultantState );

		private:
			SocketId m_descriptor;
			State m_state;
			Protocol m_protocol;
			Int32 m_lastErrorCode;
			
			Address m_peer;
		};
	}
}
