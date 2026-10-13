/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializableValueNotifier.h"

#include "rttiValueHolder.h"

namespace tools
{
	SharedCallback::SharedCallback( TSerializableValueNotifierCallback callback )
		: m_callback( callback )
	{
	}

	void SharedCallback::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
	{
		if ( m_callback )
		{
			m_callback( id, path, oldValue, targetValue );
		}
	}

	//-----

	SerializableValueNotifier::SerializableValueNotifier()
	{
	}

	SerializableValueNotifier::~SerializableValueNotifier()
	{
	}

	void SerializableValueNotifier::SetCallback( TSerializableValueNotifierCallback callback )
	{
		m_callback.Reset();

		if ( callback )
		{
			m_callback = red::CreateSharedPtr< SharedCallback >( callback );
			SerializableValueNotificationDispatcher::GetInstance().RegisterCallback( m_callback );
		}
	}

	//-----

	SerializableValueNotificationDispatcher::SerializableValueNotificationDispatcher()
		: m_disableNotifications( false )
	{
	}

	SerializableValueNotificationDispatcher::~SerializableValueNotificationDispatcher()
	{
	}

	SerializableValueNotificationDispatcher& SerializableValueNotificationDispatcher::GetInstance()
	{
		static SerializableValueNotificationDispatcher theInstance;
		return theInstance;
	}

	void SerializableValueNotificationDispatcher::NotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		RED_SCOPE_LOCK( m_notificationsLock );
		if ( !m_disableNotifications )
		{
			m_notifications.PushBack( Notification( id, path, oldValue, newValue ) );
		}
	}

	void SerializableValueNotificationDispatcher::RegisterCallback( red::WeakPtr< SharedCallback > callback )
	{
		RED_SCOPE_LOCK( m_pendingNotifiersLock );
		m_pendingAdds.PushBack( std::move( callback ) );
	}

	void SerializableValueNotificationDispatcher::SendPendingNotifications()
	{
		// add new notifiers
		{
			RED_SCOPE_LOCK( m_pendingNotifiersLock );
			m_callbacks.PushBack( std::move( m_pendingAdds ) );
			m_pendingAdds.Clear();
		}

		// get data
		m_notificationsLock.Acquire();
		auto notifications = std::move( m_notifications );
		m_notificationsLock.Release();

		// dispatch notifications
		for ( const auto& info : notifications )
		{
			auto it = std::remove_if( m_callbacks.Begin(), m_callbacks.End(), [this, &info]( const red::WeakPtr< SharedCallback >& weakClb )
			{
				if ( red::SharedPtr< SharedCallback > strongClb = weakClb.Lock() )
				{
					strongClb->OnNotifyPropertyChange( info.m_id, info.m_path, info.m_oldValue, info.m_newValue );

					return false;
				}

				return true;
			} );

			m_callbacks.Remove( it, m_callbacks.End() );
		}
	}

	void SerializableValueNotificationDispatcher::DisableNotifications()
	{
		RED_SCOPE_LOCK(m_notificationsLock);
		m_notifications.Clear();
		m_disableNotifications = true;
	}

	//////////////////////////////////////////////////////////////////////////

	SerializableValueNotificationDispatcher::Notification::Notification( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
		: m_id( id )
		, m_path( path )
		, m_oldValue( oldValue )
		, m_newValue( newValue )
	{
	}

} // tools