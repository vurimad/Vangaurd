/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "platformLinux.h"

#include <fcntl.h>
#include <errno.h>

const struct in6_addr& red::Network::RED_INADDR6_ANY = in6addr_any;

const int c_SocketError = -1;

namespace red
{
	namespace Network
	{
		namespace Base
		{

			//----

			SocketId Socket( ProtocolFamily family, Int32 type, Int32 protocol )
			{
				return socket( family, type, protocol );
			}

			Bool SetNonBlocking( SocketId socketDescriptor )
			{
				int flags = fcntl( socketDescriptor, F_GETFL, 0 );
				if ( flags < 0 ) 
					return false;

				flags = ( flags | O_NONBLOCK );
				int result = fcntl( socketDescriptor, F_SETFL, flags );
				return result > c_SocketError;
			}

			Bool Bind( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return bind( socketDescriptor, address, size ) != c_SocketError;
			}

			Bool Listen( SocketId socketDescriptor )
			{
				return listen( socketDescriptor, SOMAXCONN ) != c_SocketError;
			}

			Bool Accept( SocketId listenSocketDescriptor, SocketId& newSocketDescriptor, Sockaddr* address, SockaddrLen* size )
			{
				newSocketDescriptor = accept( listenSocketDescriptor, address, size );
				return newSocketDescriptor != InvalidSocket;
			}

			Bool Connect( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return connect( socketDescriptor, address, size ) != c_SocketError;
			}

			Bool Shutdown( SocketId socketDescriptor, int how )
			{
				return shutdown( socketDescriptor, how ) != c_SocketError;
			}

			Bool Close( SocketId socketDescriptor )
			{
				return close( socketDescriptor ) != c_SocketError;
			}

			Int32 Send( SocketId socketDescriptor, const void* buffer, Uint32 size )
			{
				return send( socketDescriptor, static_cast< const char* >( buffer ), size, 0 );
			}

			Int32 Recv( SocketId socketDescriptor, void* buffer, Uint32 size )
			{
				return recv( socketDescriptor, static_cast< char* >( buffer ), size, 0 );
			}

			Int32 SendTo( SocketId socketDescriptor, const void* buffer, Uint32 bufferSize, const Sockaddr* address, SockaddrLen addressSize )
			{
				return sendto( socketDescriptor, static_cast< const char* >( buffer ), bufferSize, 0, address, addressSize );
			}

			Int32 RecvFrom( SocketId socketDescriptor, void* buffer, Uint32 bufferSize, Sockaddr* address, SockaddrLen* addressSize )
			{
				return recvfrom( socketDescriptor, static_cast< char* >( buffer ), bufferSize, 0, address, addressSize );
			}

			Bool GetPeerName( SocketId socketDescriptor, Sockaddr* address, SockaddrLen* size )
			{
				return getpeername( socketDescriptor, address, size ) != c_SocketError;
			}

			Bool SetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, const void* value, SocketOptionLength length )
			{
				return setsockopt( socketDescriptor, level, option, value, length ) != c_SocketError;
			}

			Bool GetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, void* value, SocketOptionLength* length )
			{
				return getsockopt( socketDescriptor, level, option, value, length ) != c_SocketError;
			}

			//----

			ErrorCode GetLastError()
			{
				return errno;
			}

			Bool Initialize()
			{
				return true;
			}

			Bool Shutdown()
			{
				return true;
			}

		} // Base

		//----

		red::Int32 INetPtoN( AddressFamily family, const red::AnsiChar* source, void* destination )
		{
			return inet_pton( family, source, destination );
		}

		red::Int32 INetPtoN( AddressFamily family, const red::UniChar* source, void* destination )
		{
			red::AnsiChar convertedIp[RED_NET_MAX_ADDRSTRLEN];
			red::WideCharToStdChar( convertedIp, source, RED_NET_MAX_ADDRSTRLEN );

			return inet_pton( family, convertedIp, destination );
		}

		red::Bool INetNtoP( AddressFamily family, const void* source, red::AnsiChar* destination, red::Uint32 destinationSize )
		{
			return inet_ntop( family,  source, destination, destinationSize ) != nullptr;
		}

		red::Bool INetNtoP( AddressFamily family, const void* source, red::UniChar* destination, red::Uint32 destinationSize )
		{
			RED_HALT( "wchar INetNtoP not implemented" );
			return false;
		}

	} // Network

} // red