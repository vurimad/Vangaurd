/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "socket.h"

#include "../../redSystem/include/assert.h"

red::Network::Socket::Socket()
	: m_descriptor( InvalidSocket )
	, m_state( State_Uninitialised )
	, m_lastErrorCode( 0 )
{
}

red::Network::Socket::Socket( SocketId connectedSocket, State state, const Address& peer )
	: m_descriptor( connectedSocket )
	, m_state( state )
	, m_lastErrorCode( 0 )
	, m_peer( peer )
{	
	// At the moment, any socket created like this will be TCP
	m_protocol = TCP;
}

red::Network::Socket::Socket( Socket&& socket )
	: m_descriptor( socket.m_descriptor )
	, m_state( socket.m_state )
	, m_protocol ( socket.m_protocol )
	, m_lastErrorCode( socket.m_lastErrorCode )
	, m_peer( socket.m_peer )
{
	socket.m_descriptor = InvalidSocket;
	socket.m_state = State_Uninitialised;
	socket.m_protocol = TCP;
	socket.m_lastErrorCode = 0;
	socket.m_peer = Address();
}

red::Network::Socket& red::Network::Socket::operator=( Socket&& socket )
{
	if ( this != &socket )
	{
		m_descriptor = socket.m_descriptor;
		m_state = socket.m_state;
		m_protocol = socket.m_protocol;
		m_lastErrorCode = socket.m_lastErrorCode;
		m_peer = socket.m_peer;

		socket.m_descriptor = InvalidSocket;
		socket.m_state = State_Uninitialised;
		socket.m_protocol = TCP;
		socket.m_lastErrorCode = 0;
		socket.m_peer = Address();
	}
	return *this;
}

red::Network::Socket::~Socket()
{
	// Close socket if not yet closed
	if ( m_state != State_Closed && 
		m_state != State_Dropped && 
		m_state != State_Uninitialised )
	{
		Close();
	}

	RED_ASSERT( m_descriptor == InvalidSocket );
}

red::Bool red::Network::Socket::Create( Bool reuseAddress, Protocol protocol, AddressType addressType )
{
	RED_ASSERT( m_descriptor == InvalidSocket, "Socket is already in use" );

	ProtocolFamily pf = ( addressType == IPv4 )? RED_NET_PF_INET : RED_NET_PF_INET6;

	Int32 type = RED_NET_SOCK_STREAM;
	Int32 proto = RED_NET_IPPROTO_TCP;

	if( protocol == UDP )
	{
		type = RED_NET_SOCK_DGRAM;
		proto = RED_NET_IPPROTO_UDP;
	}

	m_protocol = protocol;
	m_descriptor = Base::Socket( pf, type, proto );

	if( m_descriptor == InvalidSocket )
	{
		m_lastErrorCode = Base::GetLastError();
		return false;
	}

	m_state = State_Unbound;

	if ( reuseAddress )
	{
		if ( !SetOptionReuseAddress( true ) )
		{
			m_lastErrorCode = GetLastError();
			Close( State_Uninitialised );
			return false;
		}
	}

	if( !Base::SetNonBlocking( m_descriptor ) )
	{
		m_lastErrorCode = Base::GetLastError();
		Close( State_Uninitialised );
		return false;
	}

	return true;
}

red::Bool red::Network::Socket::Bind( const Address& address )
{
	RED_ASSERT( m_descriptor != InvalidSocket, "Socket is uninitialised" );
	RED_ASSERT( m_state == State_Unbound, "Socket is not unbound: %i", m_state );

	if( !Base::Bind( m_descriptor, address.GetNative(), address.GetNativeSize() ) )
	{
		m_lastErrorCode = Base::GetLastError();

		if( m_lastErrorCode != RED_NET_ERR_ADDR_IN_USE )
		{
			Close( State_Uninitialised );
		}

		return false;
	}

	m_state = State_Bound;

	return true;
}

red::Bool red::Network::Socket::Bind( red::Uint16 port )
{
	SockaddrIpv4 address;
	address.sin_family = RED_NET_AF_INET;
	address.sin_addr.s_addr = RED_NET_INADDR_ANY;
	address.sin_port = HostToNetwork( port );
	red::Memzero( address.sin_zero, sizeof( address.sin_zero ) );

	return Bind( address );
}

red::Bool red::Network::Socket::Listen()
{
	RED_ASSERT( m_descriptor != InvalidSocket, "Socket is uninitialised" );
	RED_ASSERT( m_state == State_Bound, "Socket is not bound: %i", m_state );
	
	if( !Base::Listen( m_descriptor ) )
	{
		m_lastErrorCode = Base::GetLastError();
		Close( State_Uninitialised );
		return false;
	}

	m_state = State_Listening;

	return true;
}

red::Network::Socket red::Network::Socket::Accept()
{
	SockaddrStorage destinationAddress;

	red::Memzero( &destinationAddress, sizeof( SockaddrStorage ) );
	SockaddrLen sizeofDestinationAddress = sizeof( SockaddrStorage );

	SocketId connectedSocket;
	
	State newSocketState = State_Connected;

	if( !Base::Accept( m_descriptor, connectedSocket, reinterpret_cast< Sockaddr* >( &destinationAddress ), &sizeofDestinationAddress ) )
	{
		newSocketState = State_Uninitialised;

		ErrorCode errorCode = Base::GetLastError();

		if( errorCode != RED_NET_ERR_ACCEPT_OK )
		{
			m_lastErrorCode = errorCode;
			Close( State_Dropped );
		}

		return red::Network::Socket();
	}

	Address peer;

	SockaddrLen size = peer.GetNativeSize();
	if( !Base::GetPeerName( connectedSocket, peer.GetNative(), &size ) )
	{
		m_lastErrorCode = Base::GetLastError();
	}

	return red::Network::Socket( connectedSocket, newSocketState, peer );
}

red::Bool red::Network::Socket::Connect( const Address& destination )
{
	if( !Base::Connect( m_descriptor, destination.GetNative(), destination.GetNativeSize() ) )
	{
		red::Memcpy( &m_peer, &destination, sizeof( Address ) );

		ErrorCode errorCode = Base::GetLastError();

		if( errorCode != RED_NET_ERR_CONNECT_OK && errorCode != RED_NET_ERR_CONNECT_INPROGRESS )
		{
			m_lastErrorCode = errorCode;
			Close( State_Uninitialised );
			return false;
		}
	}

	m_state = State_Connecting;

	return true;
}

void red::Network::Socket::Shutdown( State resultantState, int how )
{
	Base::Shutdown( m_descriptor, how );
	m_state = resultantState;
}

void red::Network::Socket::Close( State resultantState )
{
	Base::Close( m_descriptor );
	m_descriptor = InvalidSocket;
	m_state = resultantState;
}

red::Uint32 red::Network::Socket::Send( const void* buffer, Uint32 size )
{
	Int32 result = Base::Send( m_descriptor, buffer, size );

	if ( result <= 0 )
	{
		m_lastErrorCode = Base::GetLastError();

		if ( m_lastErrorCode != RED_NET_ERR_SEND_OK && m_lastErrorCode != RED_NET_ERR_SEND_NO_BUF )
		{
			// Ungraceful disconnection
			Close( State_Dropped );
		}
		
		return 0;
	}

	return result;
}

red::Uint32 red::Network::Socket::Receive( void* buffer, Uint32 size )
{
	Int32 result = Base::Recv( m_descriptor, buffer, size );

	if( result < 0 )
	{
		ErrorCode errorCode = Base::GetLastError();

		if( errorCode != RED_NET_ERR_RECEIVE_OK )
		{
			// Ungraceful disconnection
			m_lastErrorCode = errorCode;
			result = 0;
			Close( State_Dropped );
		}
		else
		{
			// No bytes received, connection still active
			result = 0;
		}
	}
	else if( result == 0 )
	{
		// Graceful disconnection
		m_state = State_ShutDown;
	}

	return result;
}

red::Uint32 red::Network::Socket::SendTo( const void* buffer, Uint32 size, const Address& destination )
{
	Int32 result = Base::SendTo( m_descriptor, buffer, size, destination.GetNative(), destination.GetNativeSize() );

	return result;
}

red::Uint32 red::Network::Socket::ReceiveFrom( void* buffer, Uint32 size, Address& source )
{
	SockaddrLen len = source.GetNativeSize();
	Int32 result = Base::RecvFrom( m_descriptor, buffer, size, source.GetNative(), &len );

	if( result < 0 )
	{
		ErrorCode errorCode = Base::GetLastError();
		
		if( errorCode != RED_NET_ERR_RECEIVE_OK )
		{
			Close( State_Dropped );
		}

		return 0;
	}

	return result;
}

red::Bool red::Network::Socket::IsReady() const
{
	timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	fd_set writeset;
	FD_ZERO( &writeset );

	FD_SET( m_descriptor, &writeset );

	if ( select( ( int ) m_descriptor + 1, nullptr, &writeset, nullptr, &timeout ) > 0 )
	{
		return ( FD_ISSET( m_descriptor, &writeset ) )? true : false;
	}

	return false;
}

red::Bool red::Network::Socket::GetPeer( Address& peer ) const
{
	RED_ASSERT( IsConnected() );

	SockaddrLen size = peer.GetNativeSize();
	return Base::GetPeerName( m_descriptor, peer.GetNative(), &size );
}

Bool red::Network::Socket::FinishConnecting()
{
	if ( m_state != State_Connecting )
		return true;

	// Causes crashes. Some race going on somewhere here.
	RED_ASSERT( m_descriptor != InvalidSocket, "Socket is uninitialised" );

	timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 50000; // 50ms

	fd_set writeset;
	FD_ZERO( &writeset );

	fd_set errorset;
	FD_ZERO( &errorset );

	FD_SET( m_descriptor, &writeset );
	FD_SET( m_descriptor, &errorset );

	auto result = select( ( int ) m_descriptor + 1, nullptr, &writeset, &errorset, &timeout );

	if ( result == 0 )
	{
		return false;
	}
	else if ( result > 0 )
	{
		if ( FD_ISSET( m_descriptor, &writeset ) )
		{
			m_state = State_Connected;
			return true;
		}

		if ( FD_ISSET( m_descriptor, &errorset ) )
		{
			int err;
			SocketOptionLength size = sizeof( err );

			if ( !Base::GetSocketOption( m_descriptor, RED_NET_SOL_SOCKET, RED_NET_SO_ERROR, ( char* ) &err, &size ) )
			{
				if ( err == RED_NET_ERR_CONNECT_INPROGRESS )
				{
					return false;
				}
			}
			m_lastErrorCode = err;
			Close( State_Dropped );
			return true;
		}
	}
	else
	{
		m_lastErrorCode = Base::GetLastError();
		Close( State_Dropped );
		return true;
	}
	return false;
}

red::Bool red::Network::Socket::SetOptionReuseAddress( Bool enabled )
{
	SocketOptionReuseAddr value = ( enabled )? 1 : 0;

	return Base::SetSocketOption( m_descriptor, RED_NET_SOL_SOCKET, RED_NET_SO_REUSEADDR, &value, sizeof( SocketOptionReuseAddr ) );
}
