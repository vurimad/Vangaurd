/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rawManager.h"
#include "namedPipe.h"

#ifdef RED_PLATFORM_WINPC

namespace red
{
	namespace Network
	{		
		class REDNETWORK_API RawPipeManager : public IRawManager, public red::Thread
		{
		public:
			RawPipeManager();
			virtual ~RawPipeManager() override;

			// Initialize raw network
			virtual void Initialize() override;
			// Shutdown raw network
			virtual void Shutdown() override;

			//! Open a named pipe connection to given address
			//! Specifying a callback interface is required
			//! This returns valid connection ID if successful or 0 if not
			//! This is a blocking call
			virtual TConnectionID CreateConnection( const Utils::CommAddress& address, IRawConnectionInterface* callbackInterface ) override;

			//! Open a named pipe listener at given address
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
			//! This is blocking call
			virtual Uint32 Send( const TConnectionID connectionID, const void* data, const Uint32 dataSize ) override;

			//! Check if connection with given ID is active
			virtual Bool IsConnectionActive( const TConnectionID connectionID ) override;

		private:

			// Process pipe events
			void ProcessPipes( red::DynArray< HANDLE >& events );
			void ProcessListeners( red::DynArray< HANDLE >& events );
			Bool ProcessConnections( red::DynArray< HANDLE >& events );
			void WaitForAction( red::DynArray< HANDLE >& events );

			// Internal processing function
			virtual void ThreadFunc() override;

			struct RawListener
			{
				RED_INLINE RawListener( const red::SharedPtr< FullDuplexPipe > pipe, TConnectionID assignedID, IRawListenerInterface* connectionInterface )
					: m_pipe( pipe )
					, m_assignedID( assignedID )
					, m_interface ( connectionInterface )
				{}

				red::SharedPtr< FullDuplexPipe >	m_pipe;				// network pipe
				TListenerID							m_assignedID;		// assigned ID, zero if not assigned
				IRawListenerInterface*				m_interface;		// communication interface
			};

			struct RawConnection
			{
				RED_INLINE RawConnection( red::SharedPtr< FullDuplexPipe > pipe, TConnectionID assignedID, RawListener* owner, IRawConnectionInterface* connectionInterface )
					: m_pipe( pipe )
					, m_assignedID( assignedID )
					, m_owner( owner )
					, m_interface ( connectionInterface )
				{}

				red::SharedPtr< FullDuplexPipe >		m_pipe;				// connection
				TConnectionID							m_assignedID;		// assigned ID, zero if not assigned
				RawListener*							m_owner;			// null if manual connection
				IRawConnectionInterface*				m_interface;		// communication interface
			};

			constexpr static const Uint32 FLAG_SHIFT = 4;				// bit shift for flag mask vs ID
			constexpr static const Uint32 FLAG_MASK = 0xF;				// mask for flags in the ID
			constexpr static const Uint32 FLAG_CONNECTION = 0x1;		// connection flag
			constexpr static const Uint32 FLAG_LISTENER = 0x2;			// listener flag
			constexpr static const Uint32 MAX_WAIT_TIME = 10;

			//! Initialized/Shutdown flag 
			red::Atomic< Bool > m_initializedFlag;
			red::Atomic< Bool > m_shutdownFlag;

			//! ID allocator for connections and listeners
			red::Atomic< Int32 > m_idAllocator;

			//! Active outgoing connections
			red::StaticArray< RawConnection, red::Network::MAX_CONNECTIONS >	m_connections;

			//! Active listeners
			red::StaticArray< RawListener, red::Network::MAX_LISTENERS >		m_listeners;
		};
	}
}

#endif