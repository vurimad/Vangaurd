/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "platformWindows.h"

CONST IN6_ADDR& red::Network::RED_INADDR6_ANY = in6addr_any;

namespace red
{
	namespace Network
	{
		namespace Base
		{

			//----

			SocketId Socket( ProtocolFamily family, Int32 type, Int32 protocol )
			{
				return WSASocket(family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED|WSA_FLAG_NO_HANDLE_INHERIT);
			}

			Bool SetNonBlocking( SocketId socketDescriptor )
			{
				DWORD on = 1;
				return ioctlsocket( socketDescriptor, FIONBIO, &on ) != SOCKET_ERROR;
			}

			Bool Bind( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return bind( socketDescriptor, address, size ) != SOCKET_ERROR;
			}

			Bool Listen( SocketId socketDescriptor )
			{
				return listen( socketDescriptor, SOMAXCONN ) != SOCKET_ERROR;
			}

			Bool Accept( SocketId listenSocketDescriptor, SocketId& newSocketDescriptor, Sockaddr* address, SockaddrLen* size )
			{
				newSocketDescriptor = accept( listenSocketDescriptor, address, size );
				return newSocketDescriptor != INVALID_SOCKET;
			}

			Bool Connect( SocketId socketDescriptor, const Sockaddr* address, SockaddrLen size )
			{
				return connect( socketDescriptor, address, size ) != SOCKET_ERROR;
			}

			Bool Shutdown( SocketId socketDescriptor, int how )
			{
				return shutdown( socketDescriptor, how ) != SOCKET_ERROR;
			}

			Bool Close( SocketId socketDescriptor )
			{
				return closesocket( socketDescriptor ) != SOCKET_ERROR;
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
				return getpeername( socketDescriptor, address, size ) != SOCKET_ERROR;
			}

			Bool SetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, const void* value, SocketOptionLength length )
			{
				return setsockopt( socketDescriptor, level, option, static_cast< const char* >( value ), length ) != SOCKET_ERROR;
			}

			Bool GetSocketOption( SocketId socketDescriptor, Int32 level, Int32 option, void* value, SocketOptionLength* length )
			{
				return getsockopt( socketDescriptor, level, option, static_cast<char*>(value), length ) != SOCKET_ERROR;
			}

			//----

			ErrorCode GetLastError()
			{
				return WSAGetLastError();
			}

			Bool Initialize()
			{
				WSADATA wsaData;
				return WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) == 0;
			}

			Bool Shutdown()
			{
				return WSACleanup() != SOCKET_ERROR;
			}

		} // Base

		//----

		red::Int32 INetPtoN( AddressFamily family, const red::AnsiChar* source, void* destination )
		{
			return InetPtonA( family, source, destination );
		}

		red::Int32 INetPtoN( AddressFamily family, const red::UniChar* source, void* destination )
		{
			return InetPtonW( family, source, destination );
		}

		red::Bool INetNtoP( AddressFamily family, const void* source, red::AnsiChar* destination, red::Uint32 destinationSize )
		{
			return ( InetNtopA( family, const_cast< PVOID >( source ), destination, destinationSize ) ) ? true : false;
		}

		red::Bool INetNtoP( AddressFamily family, const void* source, red::UniChar* destination, red::Uint32 destinationSize )
		{
			return ( InetNtopW( family, const_cast< PVOID >( source ), destination, destinationSize ) )? true : false;
		}

	} // Network

} // red