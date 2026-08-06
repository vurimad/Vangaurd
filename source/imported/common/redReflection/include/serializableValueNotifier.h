/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "serializableId.h"
#include "rttiAccessPath.h"
#include "../../redSystem/include/utility.h"
#include "../../redMemory/include/weakPtr.h"

namespace rtti
{
	class ValueHolder;
	using ValuePtr = red::SharedPtr< ValueHolder >;
}

namespace tools
{
	typedef red::FixedSizeFunction<void(const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )> TSerializableValueNotifierCallback;

	class SharedCallback final : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		SharedCallback( TSerializableValueNotifierCallback callback );

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue );
	private:
		TSerializableValueNotifierCallback m_callback;
	};
	
	/// a engine-wide notification for property changes in ISerializable objects
	class RED_REFLECTION_API SerializableValueNotifier final : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		SerializableValueNotifier();
		~SerializableValueNotifier();

		void SetCallback( TSerializableValueNotifierCallback callback );

	private:
		red::SharedPtr< SharedCallback > m_callback;
	};

	/// a engine-wide notification for property changes in ISerializable objects
	class RED_REFLECTION_API SerializableValueNotificationDispatcher
	{
	public:
		/// singleton
		static SerializableValueNotificationDispatcher& GetInstance();

		/// Notify about value change in given serializable object
		/// All notifications are dispatched only once per frame
		void NotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue );

		/// Register listener, will listen to all value changes
		void RegisterCallback( red::WeakPtr< SharedCallback > callback );

		/// Flush updates
		void SendPendingNotifications();

		/// Permanently prevent any notifications. Useful for commandlets or other systems that won't flush the notifiers.
		void DisableNotifications();

	private:
		SerializableValueNotificationDispatcher();
		~SerializableValueNotificationDispatcher();

		typedef red::SpinLock TLock;

		red::DynArray< red::WeakPtr< SharedCallback > > m_callbacks{ red::PoolBackend() };
		red::DynArray< red::WeakPtr< SharedCallback > > m_pendingAdds{ red::PoolBackend() };

		TLock m_pendingNotifiersLock;

		struct Notification
		{
			Notification( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue );

			SerializableID m_id;
			rtti::AccessPath m_path;
			rtti::ValuePtr m_oldValue;
			rtti::ValuePtr m_newValue;
		};

		red::DynArray< Notification > m_notifications{ red::PoolBackend() };
		TLock m_notificationsLock;
		Bool m_disableNotifications;
	};

} // tools