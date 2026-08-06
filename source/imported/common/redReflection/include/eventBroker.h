/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "event.h"

namespace red
{
	class RED_REFLECTION_API EventBroker
	{
	public:
		EventBroker( const red::memory::Pool& memoryPool = red::PoolEventBroker() );
		~EventBroker();

		// THREAD SAFE with respect to QueueEvent but to with respect to other systems
		void ServiceEvents();

		// THREAD SAFE. Can be called from any thread and while servicing Event
		void QueueEvent( const THandle< Event >& event );

		// THREAD SAFE in general. NOT THREAD SAFE if call at same time than Register/Unregister listener.
		template< typename T >
		bool CanServiceEvent() const;
		bool CanServiceEvent( const rtti::ClassType* eventClass ) const;
		bool CanServiceEvent( const CName& eventName ) const;

		// THREAD SAFE.
		bool HasRegisteredEventListeners() const;

		// THREAD SAFE
		bool HasPendingEvents() const;

		// THREAD SAFE
		void RegisterListener( const WeakHandle< ISerializable > object );
		void UnregisterListener( const WeakHandle< ISerializable > object );
        
		// THREAD SAFE
		void ClearPendingEvent();
		
		// THREAD SAFE
		void ClearListeners();

		void ShrinkConnectorContainer();

	protected:
		static const Uint16 c_invalid = ~0;

		struct Connector
		{
            EventConnector eventConnector;
            Uint16 listenerIndex = c_invalid;
			Uint16 previousConnectorIndex = c_invalid;
			Uint16 nextConnectorIndex = c_invalid;
			Uint16 eventId = c_invalid;
		};
        struct DictionaryEntry
        {
            Uint16 eventId = c_invalid;
            Uint16 connectorIndex = c_invalid;

            static DictionaryEntry Compatible( Uint16 evtId ) { return { evtId, c_invalid };}
            bool operator < ( const DictionaryEntry& other ) const { return eventId < other.eventId; }
            bool operator == ( const DictionaryEntry& other ) const { return eventId == other.eventId; }
        };

        using WeakListener = WeakHandle<ISerializable>;

        typedef red::DynArray< WeakListener> ListenerContainer;
		typedef red::DynArray< Connector > ConnectorContainer;
        typedef red::SortedArray<DictionaryEntry> ConnectorDictionary1;
		typedef red::DynArray< THandle< Event > > EventContainer;

        Uint16 AcquireListener( const WeakHandle< ISerializable > object );
        Uint16 RegisterConnector( const Connector& connector );
		void ServiceEvent( const THandle< Event > & evt, const WeakHandle< ISerializable > listener = WeakHandle<ISerializable>() );
		Connector & AllocateConnector();
		void FreeConnector( const Connector& connector );
		void FreeConnector( Uint16 connectorIndex );

		Uint16 GetConnectorIndex( const Connector& connector ) const;
		Bool ValidConnector( Uint16 connectionId ) const;
		Uint16 FindConnector( const Uint16 eventId ) const;
		const Connector& GetConnector( Uint16 index ) const;
               
	private:
		// ctremblay: a mask of all supported event could be added at the cost of 128byte. 
		// This could eliminate early event that can't be handled. 
		// Is the 128 byte worth the cpu save ?

		class ConnectorCollector;

		EventContainer m_pendingEvents;
		EventContainer m_processingEvents;

        ConnectorDictionary1 m_firstConnectorDictionary;
		ConnectorContainer m_connectorContainer;
        ListenerContainer m_listenerContainer;
		Uint16 m_freeConnector;

    protected:
        mutable red::RWSpinLock m_pendingEventLock;
        mutable red::RWSpinLock m_connectorLock;
	};

	template< typename T >
	RED_INLINE bool EventBroker::CanServiceEvent() const
	{
		return CanServiceEvent( ClassID< T >() );
	}
}
