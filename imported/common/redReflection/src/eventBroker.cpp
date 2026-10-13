/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "eventBroker.h"
#include "event.h"
#include "eventConnectorCollector.h"
#include "eventDebug.h"

namespace red
{
	const Uint32 c_maxEventConnector = 512;

	class EventBroker::ConnectorCollector : public EventConnectorCollector
	{
	public:
		ConnectorCollector( EventBroker * broker, const WeakHandle< ISerializable > & listener );
		virtual ~ConnectorCollector();

		virtual void CollectConnector( Uint16 eventId, const EventConnector & connector ) override final;
		virtual void CollectScriptedConnector( Uint16 eventId, const EventConnector & connector, CName functionName ) override final;
	
	private:

		EventBroker * m_broker;
		WeakHandle< ISerializable > m_listener;
        Uint16 m_listenerIndex = ~0;
		red::StaticArray< std::pair< Uint16, CName >, c_maxEventConnector > m_registeredConnector;
	};

	EventBroker::ConnectorCollector::ConnectorCollector( EventBroker * broker, const WeakHandle< ISerializable > & listener )
		: m_broker( broker )
		, m_listener( listener )
	{
        m_listenerIndex = broker->AcquireListener( listener );
	}

	EventBroker::ConnectorCollector::~ConnectorCollector()
	{
	}

	void EventBroker::ConnectorCollector::CollectConnector( Uint16 eventId, const EventConnector & eventConnector )
	{
		const EventBroker::Connector connector =
		{
			eventConnector,
            m_listenerIndex,
			c_invalid,
			c_invalid,
			eventId
		};

		m_broker->RegisterConnector( connector );
	}

	void EventBroker::ConnectorCollector::CollectScriptedConnector( Uint16 eventId, const EventConnector & eventConnector, CName functionName )
	{
		auto iter = std::find( m_registeredConnector.Begin(), m_registeredConnector.End(), std::make_pair( eventId, functionName ) );
		if(iter == m_registeredConnector.End())
		{
			CollectConnector( eventId, eventConnector );

			m_registeredConnector.PushBack( std::make_pair( eventId, functionName ) );
		}
	}

	EventBroker::EventBroker( const red::memory::Pool& pool )
		: m_pendingEvents( pool )
		, m_processingEvents( pool )
        , m_firstConnectorDictionary( pool )
		, m_connectorContainer( pool )
        , m_listenerContainer( pool )
		, m_freeConnector( c_invalid ) 
	{
	}

	EventBroker::~EventBroker()
	{
	}

	bool EventBroker::HasRegisteredEventListeners() const
	{
		RED_SCOPE_SHARED_LOCK( m_connectorLock );
        return !m_firstConnectorDictionary.Empty();
	}

	bool EventBroker::HasPendingEvents() const
	{
		RED_SCOPE_SHARED_LOCK( m_pendingEventLock );
		return !m_pendingEvents.Empty();
	}

	bool EventBroker::CanServiceEvent( const rtti::ClassType* eventClass ) const
	{
		RED_SCOPE_SHARED_LOCK( m_connectorLock );
		const Uint16 eventId = eventClass->GetEventClassId();
        const Bool result = m_firstConnectorDictionary.Exist( DictionaryEntry::Compatible( eventId ) );
        return result;
	}

	bool EventBroker::CanServiceEvent( const CName& eventName ) const
	{
		const rtti::ClassType* eventClass = GetRttiSystem().FindClass( eventName );
		return eventClass ? CanServiceEvent( eventClass ) : false;
	}

	void EventBroker::RegisterListener( const WeakHandle< ISerializable > object )
	{
		const THandle< ISerializable > listener = object.ToHandle();
		if( listener )
		{
			const rtti::ClassType * classType = listener->GetClass();
			ConnectorCollector collector( this, object );
			classType->Internal_CollectEventConnector( collector );
		}
	}

	void EventBroker::QueueEvent( const THandle< Event >& event )
	{
		if( event )
		{
#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
			event->DEBUG_CaptureCodeStacktrace();
#endif
			RED_SCOPE_LOCK( m_pendingEventLock );
			m_pendingEvents.PushBack( event );
		}
	}

	void EventBroker::ServiceEvents()
	{
		if(!m_pendingEvents.Empty())
		{
			{
				RED_SCOPE_LOCK( m_pendingEventLock );
				m_processingEvents.Swap( m_pendingEvents );
			}

			for( const THandle< Event >& event : m_processingEvents )
			{
				ServiceEvent( event );
			}

			m_processingEvents.Clear();
		}
	}

	void EventBroker::ServiceEvent( const THandle< Event > & evt, const WeakHandle< ISerializable > listener )
	{
		const rtti::ClassType * eventClassType = evt->GetClass();
		PC_SCOPE_INST_OBJ( eventClassType->GetInstrumentationObject(), eventClassType->GetName().AsChar() );
		Uint16 eventId = eventClassType->GetEventClassId();
		Uint16 connectorIndex = FindConnector( eventId );
		RED_SCOPE_LOCK( m_connectorLock );
		while ( connectorIndex != c_invalid )
		{
			const Connector & connector = GetConnector( connectorIndex );
			connectorIndex = connector.nextConnectorIndex;

			const THandle< ISerializable > connectorListener = m_listenerContainer[ connector.listenerIndex ];
			if ( !connectorListener )
			{
				FreeConnector( connector );
			}
			else if (listener.Expired() || m_listenerContainer[connector.listenerIndex] == listener )
			{
#ifdef RED_ENABLE_EVENT_DEBUG
				red::eventDebug::OnBeginDispatchEvent( evt, connectorListener );
#endif

				EventConnector eventFn = connector.eventConnector;
				m_connectorLock.Release();
				eventFn( *connectorListener, evt );
				// **IMPORTANT** after the callback 'connector' may have been invalidated do not use it!
				m_connectorLock.Acquire(); // AcquireShared lock only for scoped lock to release it.

#ifdef RED_ENABLE_EVENT_DEBUG
				red::eventDebug::OnEndDispatchEvent( evt );
#endif
			}
		}
	}

    Uint16 EventBroker::AcquireListener( const WeakHandle< ISerializable > object )
    {
				RED_SCOPE_LOCK( m_connectorLock );

        Int32 index = m_listenerContainer.GetIndex( object );
        if( index == red::INVALID_INDEX )
        {
            index = m_listenerContainer.Size();
            m_listenerContainer.EmplaceBack( object );
        }
        return index;
    }

	Uint16 EventBroker::RegisterConnector( const Connector& inputConnector )
	{
		RED_SCOPE_LOCK( m_connectorLock );
		Connector& connector = AllocateConnector();
		connector = inputConnector;

		const Uint16 connectorIndex = GetConnectorIndex( connector );
		ConnectorDictionary1::Result insertResult = m_firstConnectorDictionary.InsertUnique( { connector.eventId, connectorIndex } );

		RED_ASSERT( connectorIndex < m_connectorContainer.Size() );
		if( !insertResult.IsSuccessful() )
		{
			// ctremblay: Fast insertion vs not so fast iteration ...
			const Uint16 index = insertResult.Iterator()->connectorIndex;

			RED_ASSERT( index < m_connectorContainer.Size() );
			connector.nextConnectorIndex = index;
			m_connectorContainer[ index ].previousConnectorIndex = connectorIndex;
			insertResult.Iterator()->connectorIndex = connectorIndex;
		}
		return connectorIndex;
	}

	void EventBroker::UnregisterListener( const WeakHandle<ISerializable> object )
	{
		RED_SCOPE_LOCK( m_connectorLock );

        const Uint16 listenerIndex = m_listenerContainer.GetIndex( object );
        if( listenerIndex > m_listenerContainer.Size() )
            return;

        const Uint32 theLastIndex = m_listenerContainer.Size() - 1;
        m_listenerContainer.RemoveAtReorder( listenerIndex );

		for( Connector &connector : m_connectorContainer )
		{
			if( connector.listenerIndex == listenerIndex )
			{
				FreeConnector( connector );
			}
            else if( connector.listenerIndex == theLastIndex )
            {
                connector.listenerIndex = listenerIndex;
            }
		}
	}

	void EventBroker::ClearListeners()
	{
		RED_SCOPE_LOCK( m_connectorLock );
        m_firstConnectorDictionary.Clear();
		m_connectorContainer.Clear();
		m_freeConnector = c_invalid;
	}

	void EventBroker::ClearPendingEvent()
	{
		RED_SCOPE_LOCK( m_pendingEventLock );
		m_pendingEvents.Clear();
	}

	EventBroker::Connector& EventBroker::AllocateConnector()
	{
		RED_ASSERT( !m_connectorLock.TryAcquire() ); // The caller is responsible for locking this lock!
		if( m_freeConnector != c_invalid )
		{
			const Uint16 connectorIndex = m_freeConnector;
			Connector & connector = m_connectorContainer[ connectorIndex ];
			m_freeConnector = connector.nextConnectorIndex;
			return connector;
		}
		else
		{
			return m_connectorContainer.EmplaceBack();
		}	
	}

	void EventBroker::FreeConnector( const Connector& connector )
	{
		FreeConnector( GetConnectorIndex( connector ) );
	}

	void EventBroker::FreeConnector( const Uint16 connectorIndex )
	{
		RED_ASSERT( !m_connectorLock.TryAcquire() ); // The caller is responsible for locking this lock!

		Connector& freeConnector = m_connectorContainer[connectorIndex];
		const Uint16 nextConnectorIndex = freeConnector.nextConnectorIndex;
		const Uint16 previousConnnectorIndex = freeConnector.previousConnectorIndex;

		if( nextConnectorIndex != c_invalid )
		{
			m_connectorContainer[ nextConnectorIndex ].previousConnectorIndex = previousConnnectorIndex;
		}

		if( previousConnnectorIndex != c_invalid )
		{
			m_connectorContainer[ previousConnnectorIndex ].nextConnectorIndex = nextConnectorIndex;
		}
		else
		{
			// ctremblay: if there is no previous index, this connector is first connector for event. Update dictionary
            ConnectorDictionary1::iterator iter = m_firstConnectorDictionary.Find( DictionaryEntry::Compatible( freeConnector.eventId ) );
			if ( nextConnectorIndex == c_invalid )
			{
                m_firstConnectorDictionary.Remove( iter );
			}
			else if ( iter != m_firstConnectorDictionary.End() )
			{
                iter->connectorIndex = nextConnectorIndex;
			}
		}

		freeConnector.nextConnectorIndex = m_freeConnector;
		freeConnector.previousConnectorIndex = c_invalid;
        freeConnector.listenerIndex = ~0;
		m_freeConnector = GetConnectorIndex( freeConnector );
	}

	Uint16 EventBroker::GetConnectorIndex( const Connector& connector ) const
	{
		RED_ASSERT( !m_connectorLock.TryAcquire() ); // The caller is responsible for locking this lock!
		return (Uint16)std::distance( &m_connectorContainer.Front(), &connector );
	}

	Bool EventBroker::ValidConnector( Uint16 connectorIndex ) const
	{	
		RED_SCOPE_SHARED_LOCK( m_connectorLock );

		Bool valid = false;
		if ( connectorIndex < m_connectorContainer.Size() )
		{
			const EventBroker::Connector& connector = GetConnector( connectorIndex );
            if( connector.listenerIndex < m_listenerContainer.Size() )
            {
                valid = !m_listenerContainer[connector.listenerIndex].Expired();
            }
		}
		return valid;
	}

	Uint16 EventBroker::FindConnector( const Uint16 eventId ) const
	{
		RED_SCOPE_SHARED_LOCK( m_connectorLock );
        auto it = m_firstConnectorDictionary.Find( DictionaryEntry::Compatible( eventId ) );
        const Uint16 result = it != m_firstConnectorDictionary.End() ? it->connectorIndex : c_invalid;
        return result;
	}

	const EventBroker::Connector& EventBroker::GetConnector( Uint16 index ) const
	{
		return m_connectorContainer[index];
	}

	void EventBroker::ShrinkConnectorContainer()
	{	
		RED_SCOPE_LOCK( m_connectorLock );
		m_connectorContainer.Shrink();
	}
}
