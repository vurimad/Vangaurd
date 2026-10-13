/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

/// Raw Network is the proper network system with no BS wrapping around it
#include "network.h"
#include "address.h"
#include "packetNetworkUtils.h"

namespace red
{
	namespace Network
	{
		// ID typedefs
		typedef Uint32 TConnectionID;
		typedef Uint32 TListenerID;

		// raw connection interface
		class REDNETWORK_API IRawConnectionInterface
		{
		public:
			virtual ~IRawConnectionInterface() {};

			//! Called when connection is dropped, not called if that's us that closed the connection, not called on exit
			//! This callback is asynchronous and can happen at any time but is not reentrant
			virtual void OnDisconnected( const TConnectionID connectionID ) = 0;

			//! Called whenever we receive data, it's up to us to process it
			//! This callback is asynchronous and can happen at any time but is not reentrant
			virtual void OnData( const void* data, const Uint32 dataSize, const TConnectionID connectionID ) = 0;
		};

		// raw listener interface
		class REDNETWORK_API IRawListenerInterface
		{
		public:
			virtual ~IRawListenerInterface() {};

			//! Called when listener is closed due to some problems, not called when listener is closes by us or on exit
			//! This callback is asynchronous and can happen at any time but is not reentrant
			virtual void OnClosed( const TListenerID listenerID ) = 0;

			//! Called whenever we receive data, it's up to us to process it, returning FALSE will deny the connection
			//! This callback is asynchronous and can happen at any time but is not reentrant
			virtual bool OnConnection( const TListenerID listenerID, const TConnectionID connectionID, IRawConnectionInterface*& outConnectionInterface ) = 0;				
		};

		class REDNETWORK_API IRawManager
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			virtual ~IRawManager()
			{
			}
			
			virtual void Initialize() = 0;
			virtual void Shutdown() = 0;

			virtual TConnectionID CreateConnection( const Utils::CommAddress& address, IRawConnectionInterface* callbackInterface ) = 0;
			virtual TListenerID CreateListener( const Utils::CommAddress& address, Bool reuseAddress, IRawListenerInterface* callbackInterface ) = 0;

			virtual Bool CloseConnection( const TConnectionID connectionID )  = 0;
			virtual Bool CloseListener( const TListenerID listenerID ) = 0;

			virtual Uint32 Send( const TConnectionID connectionID, const void* data, const Uint32 dataSize ) = 0;
			virtual Bool IsConnectionActive( const TConnectionID connectionID ) = 0;

			RED_INLINE void Acquire()							{ m_lock.Acquire();  };
			RED_INLINE Bool TryAcquire()						{ return m_lock.TryAcquire(); }
			RED_INLINE void Release()							{ return m_lock.Release(); }
			RED_INLINE void SetSpinCount( TSpinCount count )	{ return m_lock.SetSpinCount(count); }

		protected:
			red::Mutex m_lock;
		};
	}
}
