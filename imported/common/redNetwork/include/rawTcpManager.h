/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "socket.h"
#include "memory.h"

#include "rawManager.h"

namespace red
{ namespace Network
{
	/// Manager for RAW network
	class REDNETWORK_API RawTcpManager : public IRawManager, public red::Thread
	{
	public:
		RawTcpManager();
		virtual ~RawTcpManager() override;

		// Initialize raw network
		virtual void Initialize() override;

		// Shutdown raw network
		virtual void Shutdown() override;

		//! Open a TCP connection to given address
		//! Specifying a callback interface is required
		//! This returns valid connection ID if successful or 0 if not
		//! This is a blocking call
		virtual TConnectionID CreateConnection( const Utils::CommAddress& address, IRawConnectionInterface* callbackInterface ) override;

		//! Open a TCP listener at given port
		//! Specifying a callback interface is required
		//! This returns valid listener ID if successful or 0 if not
		//! This is a blocking call
		virtual TListenerID CreateListener( const Utils::CommAddress& address, Bool reuseAddress, IRawListenerInterface* callbackInterface ) override;

		//! Close given connection, returns true if successful
		//! Requires valid connection to be specified
		//! This is a blocking call
		virtual Bool CloseConnection( const TConnectionID connectionID ) override;

		//! Close given listener, returns true if successful
		//! Requires valid listener to be specified
		//! This is a blocking call
		virtual Bool CloseListener( const TListenerID listenerID ) override;

		//! Send data through connection, returns number of bytes sent
		//! Requires valid connection to be specified
		//! This can fail and call OnDisconnected on the connection
		//! This is blocking call
		virtual Uint32 Send( const TConnectionID connectionID, const void* data, const Uint32 dataSize ) override;

		//! Check if connection with given ID is active
		virtual Bool IsConnectionActive( const TConnectionID connectionID ) override;

	private:

		// Process socket events
		SocketId BuildSocketSets( fd_set& readSet, fd_set& errorSet );
		Bool WaitForAction( SocketId highestSocketId, fd_set& readSet, fd_set& errorSet );
		void ProcessListeners( fd_set& readSet, fd_set& errorSet );
		void ProcessConnections( fd_set& readSet, fd_set& errorSet );

		// Internal processing function
		virtual void ThreadFunc() override;

		static const Uint32 FLAG_SHIFT = 4;					// bit shift for flag mask vs ID
		static const Uint32 FLAG_MASK = 0xF;				// mask for flags in the ID
		static const Uint32 FLAG_CONNECTION = 0x1;			// connection flag
		static const Uint32 FLAG_LISTENER = 0x2;			// listener flag
		static const Uint32 MAX_WAIT_TIME_MS = 10;
		static const Uint32 BUFFER_SIZE = 8192;

		struct RawListener
		{
			RED_INLINE RawListener( Socket socket, TConnectionID assignedID, IRawListenerInterface* connectionInterface )
				: m_socket( std::move( socket ) )
				, m_assignedID( assignedID )
				, m_interface ( connectionInterface )
			{} 

			Socket						m_socket;			// network socket
			TListenerID					m_assignedID;		// assigned ID, zero if not assigned
			IRawListenerInterface*		m_interface;		// communication interface
		};

		struct RawConnection
		{
			RED_INLINE RawConnection( Socket socket, TConnectionID assignedID, RawListener* owner, IRawConnectionInterface* connectionInterface )
				: m_socket( std::move( socket ) )
				, m_assignedID( assignedID )
				, m_owner( owner )
				, m_interface ( connectionInterface )
			{}

			Socket						m_socket;			// connection
			TConnectionID				m_assignedID;		// assigned ID, zero if not assigned
			RawListener*				m_owner;			// null if manual connection
			IRawConnectionInterface*	m_interface;		// communication interface
		};

		//! Initialized/Shutdown flag 
		red::Atomic< Bool > m_initializedFlag;
		red::Atomic< Bool > m_shutdownFlag;

		//! ID allocator for connections and listeners
		red::Atomic< Int32 > m_idAllocator;

		//! Active outgoing connections
		red::StaticArray< RawConnection, MAX_CONNECTIONS > m_connections;

		//! Active listeners
		red::StaticArray< RawListener, MAX_LISTENERS > m_listeners;
	};
}
}
