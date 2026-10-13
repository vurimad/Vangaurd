/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "network.h"

namespace red
{
	namespace Network
	{
		class REDNETWORK_API Address
		{
		public:

			static const Uint32 MAX_IP_STRING_LENGTH = RED_NET_MAX_ADDRSTRLEN;

			enum EFamily
			{
				Family_IPv4 = RED_NET_AF_INET,
				Family_IPv6 = RED_NET_AF_INET6
			};

			Address();

			Address( red::Uint16 port, EFamily family );

			Address( const AnsiChar* ip, Uint16 port, EFamily family = Family_IPv4 );
			Address( const UniChar* ip, Uint16 port, EFamily family = Family_IPv4 );
			
			Address( const Address& address );

			Address( const SockaddrIpv4& ip4address );
			Address( const SockaddrIpv6& ip6address );
			Address( const Sockaddr& addr );
			Address( const SockaddrStorage& addr );

			RED_INLINE const Address& operator=( const Address& other )
			{
				m_address = other.m_address;
				return *this;
			}

			RED_INLINE const Address& operator=( const SockaddrIpv4& other )
			{
				m_address = other;
				return *this;
			}

			RED_INLINE const Address& operator=( const SockaddrIpv6& other )
			{
				m_address = other;
				return *this;
			}

			RED_INLINE const Address& operator=( const Sockaddr& other )
			{
				m_address = other;
				return *this;
			}

			RED_INLINE const Address& operator=( const SockaddrStorage& other )
			{
				m_address = other;
				return *this;
			}

			RED_INLINE Bool IsIPv4() const
			{
				return m_address.ss_family == RED_NET_AF_INET;
			}

			RED_INLINE Sockaddr* GetNative()
			{
				return reinterpret_cast< Sockaddr* >( &m_address );
			}

			RED_INLINE const Sockaddr* GetNative() const
			{
				return reinterpret_cast< const Sockaddr* >( &m_address );
			}

			RED_INLINE SockaddrLen GetNativeSize() const
			{
				return ( IsIPv4() )? sizeof( SockaddrIpv4 ) : sizeof( SockaddrIpv6 );
			}

			void SetPort( Uint16 port );

			RED_INLINE Bool operator==( const Address& other ) const
			{
				SockaddrLen size = GetNativeSize();
				return size == other.GetNativeSize() && red::Memcmp( &m_address, &other.m_address, size ) == 0;
			}

			Bool GetIp( AnsiChar* buffer, Uint32 size ) const;
			Uint16 GetPort() const;
			Uint32 GetIPv4() const;

			// Parses ip and port; expected format: "192.168.0.1:34243"
			Bool Parse( const AnsiChar* buffer );

#if defined( RED_PLATFORM_WINPC )
			static Bool GetLocalIpAddress( AnsiChar* buffer, Uint32 size );
#endif

		private:
			const void* GetIPStorage() const;
			RED_INLINE void* GetIPStorage() { return const_cast< void* >( static_cast< const Address* >( this )->GetIPStorage() ); }

		private:
			SockaddrStorage m_address;
		};
	}
}
