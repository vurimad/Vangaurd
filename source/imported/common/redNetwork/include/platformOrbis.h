/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

// System Includes
#include <net.h>

#pragma comment( lib, "libSceNet_stub_weak.a" )
#pragma comment( lib, "libScePosix_stub_weak.a" )

#include "../../redSystem/include/assert.h"

// Enumerations and defines

#define RED_NET_MAX_ADDRSTRLEN	SCE_NET_INET_ADDRSTRLEN

#define RED_NET_INADDR_ANY		SCE_NET_INADDR_ANY

#define RED_NET_PF_INET			SCE_NET_AF_INET
#define RED_NET_PF_INET6		(SceNetSaFamily_t)-1

#define RED_NET_AF_INET			SCE_NET_AF_INET
#define RED_NET_AF_INET6		(SceNetSaFamily_t)-1

#define RED_NET_SOCK_STREAM		SCE_NET_SOCK_STREAM
#define RED_NET_IPPROTO_TCP		SCE_NET_IPPROTO_TCP

#define RED_NET_SOCK_DGRAM		SCE_NET_SOCK_DGRAM
#define RED_NET_IPPROTO_UDP		SCE_NET_IPPROTO_UDP

#define RED_NET_SO_ERROR		SCE_NET_SO_ERROR
#define RED_NET_SOL_SOCKET		SCE_NET_SOL_SOCKET

#define RED_NET_SO_REUSEADDR	SCE_NET_SO_REUSEADDR

#define RED_NET_LISTEN_BACKLOG	10

#define RED_NET_ERR_CONNECT_OK	SCE_NET_EINPROGRESS
#define RED_NET_ERR_ACCEPT_OK	SCE_NET_EWOULDBLOCK
#define RED_NET_ERR_RECEIVE_OK	SCE_NET_EWOULDBLOCK
#define RED_NET_ERR_SEND_OK		SCE_NET_EWOULDBLOCK
#define RED_NET_ERR_SEND_NO_BUF	SCE_NET_ENOBUFS

#define RED_NET_ERR_CONNECT_INPROGRESS SCE_NET_EINPROGRESS 
#define RED_NET_ERR_ALREADY_CONNECTED SCE_NET_EISCONN 

#define RED_NET_ERR_ADDR_IN_USE	SCE_NET_EADDRINUSE

#define RED_NET_SHUTDOWN_RECEIVE	SCE_NET_SHUT_RD
#define RED_NET_SHUTDOWN_SEND		SCE_NET_SHUT_WR
#define RED_NET_SHUTDOWN_BOTH		SCE_NET_SHUT_RDWR

// Structs
namespace red
{
	namespace Network
	{
		typedef int					ErrorCode;

		//////////////////////////////////////////////////////////////////////////
		// Socket
		//////////////////////////////////////////////////////////////////////////
		typedef SceNetId			SocketId;
		static const SocketId		InvalidSocket = -1;
		typedef int					ProtocolFamily;

		//////////////////////////////////////////////////////////////////////////
		// Socket Options
		//////////////////////////////////////////////////////////////////////////
		typedef int      			SocketOptionLength;

		typedef int					SocketOptionReuseAddr;

		//////////////////////////////////////////////////////////////////////////
		// Address
		//////////////////////////////////////////////////////////////////////////
		typedef SceNetSockaddrIn	SockaddrIpv4;
		typedef SceNetSockaddr		Sockaddr;
		typedef SceNetSaFamily_t	AddressFamily;

		typedef SceNetSocklen_t		SockaddrLen;

		// PS4 currently doesn't support IPv6, so we'll have some dummy structs for now
		struct in6_addr
		{
			unsigned char   s6_addr[16];   // load with inet_pton()

			in6_addr() {}
		};

		struct SockaddrIpv6
		{
			u_int16_t       sin6_family;   // address family, AF_INET6
			u_int16_t       sin6_port;     // port number, Network Byte Order
			u_int32_t       sin6_flowinfo; // IPv6 flow information
			in6_addr		sin6_addr;     // IPv6 address
			u_int32_t       sin6_scope_id; // Scope ID
		};

		const in6_addr		RED_INADDR6_ANY;

		// Wrapper for sockaddr_storage
		// This exists because Orbis doesn't support IPv6
		struct SockaddrStorage : public SceNetSockaddr
		{
			AddressFamily& ss_family;

			RED_INLINE SockaddrStorage() : ss_family( sa_family )
			{
				red::Memzero( this, sizeof( SceNetSockaddr ) );
			}

			RED_INLINE const SockaddrStorage& operator=( const SockaddrStorage& other )
			{
				static_assert( sizeof *this >= sizeof other, "Something is seriously wrong with the size of SockaddrStorage" );

				red::Memcpy( this, &other, sizeof( SceNetSockaddr ) );
				return *this;
			}

			RED_INLINE const SockaddrStorage& operator=( const Sockaddr& other )
			{
				static_assert( sizeof *this >= sizeof other, "Sockaddr should always be smaller than SockaddrStorage, as it's technically only big enough for IPv4" );

				red::Memcpy( this, &other, sizeof( Sockaddr ) );
				return *this;
			}

			RED_INLINE const SockaddrStorage& operator=( const SockaddrIpv4& other )
			{
				static_assert( sizeof *this >= sizeof other, "SockaddrStorage should always be bigger than SockaddrIpv4, as it can also contain IPv6 addresses" );

				red::Memcpy( this, &other, sizeof( SockaddrIpv4 ) );
				return *this;
			}

			RED_INLINE const SockaddrStorage& operator=( const SockaddrIpv6& other )
			{
				// Once Orbis *does* support IPv6, we can remove this custom SockaddrStorage
				// class and replace it with a simple typedef (For all platforms)
				RED_HALT( "ORBIS DOES NOT SUPPORT IPv6" );

				red::Memcpy( this, &other, sizeof( SockaddrIpv6 ) );
				return *this;
			}
		};

		//////////////////////////////////////////////////////////////////////////
		// Socket Functions
		//////////////////////////////////////////////////////////////////////////

		namespace Base
		{
			RED_INLINE SocketId Socket( AddressFamily family, Int32 type, Int32 protocol )
			{
				return sceNetSocket( nullptr, family, type, protocol );
			}

			RED_INLINE Bool SetNonBlocking( SocketId socketDescriptor )
			{
				int on = 1;
				return sceNetSetsockopt( socketDescriptor, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &on, sizeof( int ) ) == 0;
			}

			RED_INLINE Bool Bind( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return sceNetBind( socketDescriptor, address, size ) == 0;
			}

			RED_INLINE Bool Listen( SocketId socketDescriptor )
			{
				return sceNetListen( socketDescriptor, RED_NET_LISTEN_BACKLOG ) == 0;
			}

			RED_INLINE Bool Accept( SocketId listenSocketDescriptor, SocketId& newSocketDescriptor, Sockaddr* address, SockaddrLen* size )
			{
				newSocketDescriptor = sceNetAccept( listenSocketDescriptor, address, size );
				return newSocketDescriptor >= 0;
			}

			RED_INLINE Bool Connect( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return sceNetConnect( socketDescriptor, address, size ) == 0;
			}

			RED_INLINE Bool Shutdown( SocketId socketDescriptor, int how )
			{
				return sceNetShutdown( socketDescriptor, how ) == 0;
			}

			RED_INLINE Bool Close( SocketId socketDescriptor )
			{
				return sceNetSocketClose( socketDescriptor ) == 0;
			}

			RED_INLINE Int32 Send( SocketId socketDescriptor, const void* buffer, Uint32 size )
			{
				return sceNetSend( socketDescriptor, buffer, size, 0 );
			}

			RED_INLINE Int32 Recv( SocketId socketDescriptor, void* buffer, Uint32 size )
			{
				return sceNetRecv( socketDescriptor, buffer, size, 0 );
			}

			RED_INLINE Int32 SendTo( SocketId socketDescriptor, const void* buffer, Uint32 bufferSize, const Sockaddr* address, SockaddrLen addressSize )
			{
				return sceNetSendto( socketDescriptor, buffer, bufferSize, 0, address, addressSize );
			}

			RED_INLINE Int32 RecvFrom( SocketId socketDescriptor, void* buffer, Uint32 bufferSize, Sockaddr* address, SockaddrLen* addressSize )
			{
				return sceNetRecvfrom( socketDescriptor, buffer, bufferSize, 0, address, addressSize );
			}

			RED_INLINE Bool GetPeerName( SocketId socketDescriptor, Sockaddr* address, SockaddrLen* size )
			{
				return sceNetGetpeername( socketDescriptor, address, size ) == 0;
			}

			RED_INLINE Bool SetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, const void* value, SocketOptionLength length )
			{
				return sceNetSetsockopt( socketDescriptor, level, option, value, length ) == 0;
			}

			RED_INLINE Bool GetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, void* value, SocketOptionLength* length )
			{
				return sceNetGetsockopt( socketDescriptor, level, option, value, reinterpret_cast<unsigned*>(length) ) == 0;
			}

			RED_INLINE ErrorCode GetLastError()
			{
				return sce_net_errno;
			}

			RED_INLINE Bool Initialize()
			{
				return sceNetInit() == 0;
			}

			RED_INLINE Bool Shutdown()
			{
				return sceNetTerm() == 0;
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Ip string <-> Data Conversion
		//////////////////////////////////////////////////////////////////////////
		RED_INLINE red::Int32 INetPtoN( AddressFamily family, const red::AnsiChar* source, void* destination ) { return sceNetInetPton( family, source, destination ); }
		RED_INLINE red::Int32 INetPtoN( AddressFamily family, const red::UniChar* source, void* destination )
		{
			red::AnsiChar convertedIp[ RED_NET_MAX_ADDRSTRLEN ];
			red::WideCharToStdChar( convertedIp, source, RED_NET_MAX_ADDRSTRLEN );

			return sceNetInetPton( family, convertedIp, destination );
		}

		RED_INLINE red::Bool INetNtoP( AddressFamily family, const void* source, red::AnsiChar* destination, red::Uint32 destinationSize )
		{
			return ( sceNetInetNtop( family, source, destination, destinationSize ) )? true : false;
		}


		//////////////////////////////////////////////////////////////////////////
		// Endian Conversion
		//////////////////////////////////////////////////////////////////////////
		RED_INLINE Int8		NetworkToHost( const Int8 data )			{ return data; }
		RED_INLINE Int32	NetworkToHost( const Int32 data )			{ return sceNetNtohl( data ); }
		RED_INLINE Int16	NetworkToHost( const Int16 data )			{ return sceNetNtohs( data ); }
		RED_INLINE Int64	NetworkToHost( const Int64 data )			{ return sceNetNtohll( data ); }

		RED_INLINE Int8		HostToNetwork( const Int8 data )			{ return data; }
		RED_INLINE Int16	HostToNetwork( const Int16 data )			{ return sceNetHtons( data ); }
		RED_INLINE Int32	HostToNetwork( const Int32 data )			{ return sceNetHtonl( data ); }
		RED_INLINE Int64	HostToNetwork( const Int64 data )			{ return sceNetHtonll( data ); }

		RED_INLINE Uint8	NetworkToHost( const Uint8 data )			{ return data; }
		RED_INLINE Uint32	NetworkToHost( const Uint32 data )			{ return sceNetNtohl( data ); }
		RED_INLINE Uint16	NetworkToHost( const Uint16 data )			{ return sceNetNtohs( data ); }
		RED_INLINE Uint64	NetworkToHost( const Uint64 data )			{ return sceNetNtohll( data ); }

		RED_INLINE Uint8	HostToNetwork( const Uint8 data )			{ return data; }
		RED_INLINE Uint16	HostToNetwork( const Uint16 data )			{ return sceNetHtons( data ); }
		RED_INLINE Uint32	HostToNetwork( const Uint32 data )			{ return sceNetHtonl( data ); }
		RED_INLINE Uint64	HostToNetwork( const Uint64 data )			{ return sceNetHtonll( data ); }

		RED_INLINE UniChar	NetworkToHost( const UniChar data )			{ return sceNetNtohs( data ); }
		RED_INLINE UniChar	HostToNetwork( const UniChar data )			{ return sceNetHtons( data ); }
	}
}
