/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/
#include "build.h"
#include "../include/address.h"
#include "../../redSystem/include/assert.h"

red::Network::Address::Address()
{
}

red::Network::Address::Address( red::Uint16 port, EFamily family )
{
	if( family == Family_IPv4 )
	{
		SockaddrIpv4* address = reinterpret_cast< SockaddrIpv4* >( &m_address );

		address->sin_family = RED_NET_AF_INET;
		address->sin_addr.s_addr = RED_NET_INADDR_ANY;
		address->sin_port = HostToNetwork( port );
	}
	else if( family == Family_IPv6 )
	{
		SockaddrIpv6* address = reinterpret_cast< SockaddrIpv6* >( &m_address );

		address->sin6_family = RED_NET_AF_INET6;
		address->sin6_addr = RED_INADDR6_ANY;
		address->sin6_port = HostToNetwork( port );
	}
}

red::Network::Address::Address( const red::AnsiChar* ip, red::Uint16 port, EFamily family )
{
	m_address.ss_family = static_cast< AddressFamily >( family );
	SetPort( port );

	Int32 result = INetPtoN( m_address.ss_family, ip, GetIPStorage() );
	RED_UNUSED( result );
}

red::Network::Address::Address( const red::UniChar* ip, red::Uint16 port, EFamily family )
{
	m_address.ss_family = static_cast< AddressFamily >( family );
	SetPort( port );

	Int32 result = INetPtoN( m_address.ss_family, ip, GetIPStorage() );
	RED_UNUSED( result );
}

red::Network::Address::Address( const Address& address )
{
	*this = address;
}

red::Network::Address::Address( const SockaddrIpv4& address )
{
	*this = address;
}

red::Network::Address::Address( const SockaddrIpv6& address )
{
	*this = address;
}

red::Network::Address::Address( const Sockaddr& address )
{
	*this = address;
}

red::Network::Address::Address( const SockaddrStorage& address )
{
	*this = address;
}

Bool red::Network::Address::Parse( const AnsiChar* buffer )
{
	Uint32 parsedIPParts[ 4 ];
	Uint32 parsedPort;

	const int scanfResult =
		red::SScanf( buffer, "%u.%u.%u.%u:%u",
			&parsedIPParts[0], &parsedIPParts[1], &parsedIPParts[2], &parsedIPParts[3],
			&parsedPort
		);
	
	if ( scanfResult != 5 )
	{
		return false;
	}

	union
	{
		Uint32 IP;
		Uint8 IPBytes[ 4 ];
	};

	// Validate ip

	for ( Uint32 i = 0; i < 4; ++i )
	{
		if ( parsedIPParts[ i ] >= ( 1 << 8 ) )
		{
			return false;
		}
		IPBytes[ i ] = ( Uint8 ) parsedIPParts[ i ];
	}
	const Uint32 ip = IP;

	// Validate port

	if ( parsedPort >= ( 1 << 16 ) )
	{
		return false;
	}

	const Uint16 port = ( Uint16 ) parsedPort;

	// Parsed successfully - initialize address

	SockaddrIpv4* address = reinterpret_cast< SockaddrIpv4* >( &m_address );

	address->sin_family = RED_NET_AF_INET;
	address->sin_addr.s_addr = ip;
	address->sin_port = HostToNetwork( port );
	
	return true;
}

void red::Network::Address::SetPort( Uint16 port )
{
	if( m_address.ss_family == RED_NET_AF_INET )
	{
		SockaddrIpv4* ipv4Address = reinterpret_cast< SockaddrIpv4* >( &m_address );
		ipv4Address->sin_port = HostToNetwork( port );
	}
	else if( m_address.ss_family == RED_NET_AF_INET6 )
	{
		SockaddrIpv6* ipv6Address = reinterpret_cast< SockaddrIpv6* >( &m_address );
		ipv6Address->sin6_port = HostToNetwork( port );
	}
	else
	{
		RED_HALT( "Invalid or Uninitialised family" );
	}
}

const void* red::Network::Address::GetIPStorage() const
{
	if( m_address.ss_family == RED_NET_AF_INET )
	{
		const SockaddrIpv4* ipv4Address = reinterpret_cast< const SockaddrIpv4* >( &m_address );
		return &( ipv4Address->sin_addr );
	}
	else if( m_address.ss_family == RED_NET_AF_INET6 )
	{
		const SockaddrIpv6* ipv6Address = reinterpret_cast< const SockaddrIpv6* >( &m_address );
		return &( ipv6Address->sin6_addr );
	}
	else
	{
		RED_HALT( "Invalid or Uninitialised family" );
	}

	return nullptr;
}

red::Bool red::Network::Address::GetIp( AnsiChar* buffer, Uint32 size ) const
{
	return INetNtoP( m_address.ss_family, GetIPStorage(), buffer, size );
}

red::Uint16 red::Network::Address::GetPort() const
{
	if( m_address.ss_family == RED_NET_AF_INET )
	{
		const SockaddrIpv4* ipv4Address = reinterpret_cast< const SockaddrIpv4* >( &m_address );
		return HostToNetwork( ipv4Address->sin_port );
	}
	else if( m_address.ss_family == RED_NET_AF_INET6 )
	{
		const SockaddrIpv6* ipv6Address = reinterpret_cast< const SockaddrIpv6* >( &m_address );
		return HostToNetwork( ipv6Address->sin6_port );
	}
	else
	{
		RED_HALT( "Invalid or Uninitialised family" );
	}

	return 0;
}

Uint32 red::Network::Address::GetIPv4() const
{
	RED_ASSERT( m_address.ss_family == RED_NET_AF_INET );
	const SockaddrIpv4* ipv4Address = reinterpret_cast< const SockaddrIpv4* >( &m_address );
	return HostToNetwork( ( Uint32 ) ipv4Address->sin_addr.s_addr );
}

#if defined( RED_PLATFORM_WINPC )
Bool red::Network::Address::GetLocalIpAddress( AnsiChar* buffer, Uint32 size )
{
	char hostName[255] = { '\0' };
	int errorCode = 0;
	errorCode = gethostname( hostName, RED_ARRAY_COUNT( hostName ) );
	if ( errorCode != 0 )
	{
		RED_LOG_ERROR( "Failed to retrieve host name, errorCode=%d", errorCode );
		return false;
	}

	struct addrinfo hints;
	Memzero( &hints, sizeof( hints ) );
	hints.ai_family = AF_INET;

	struct addrinfo *res;
	errorCode = getaddrinfo( hostName, nullptr, &hints, &res );
	if ( errorCode != 0 )
	{
		RED_LOG_ERROR( "Failed to retrieve address info, errorCode=%d", errorCode );
		return false;
	}

	struct sockaddr_in* socketAddress = nullptr;
	for ( auto i = res; i != nullptr; i = i->ai_next )
	{
		socketAddress = reinterpret_cast< struct sockaddr_in* >( i->ai_addr );
		break;
	}

	const Bool result = socketAddress != nullptr ? INetNtoP( Address::Family_IPv4, &socketAddress->sin_addr, buffer, size ) : false;
	freeaddrinfo( res );
	return result;
}
#endif
