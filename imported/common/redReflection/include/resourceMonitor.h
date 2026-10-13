/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/hashMap.h"
#include "../../redContainers/include/dynArray.h"
#include "../../redMemory/include/sharedPtr.h"
#include "resourcePath.h"

namespace res
{
	class Monitor;

	/// helper class for passing data to the monitor events, try not to use
	class RED_REFLECTION_API IMonitorUserData
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IMonitorUserData();
	};

	typedef red::SharedPtr<IMonitorUserData> MonitorUserDataPtr;

	typedef red::FixedSizeFunction<void(const ResourcePath& path, const CName& message, const MonitorUserDataPtr& userData)> TMonitorCallback;


	// helper notification class that listens to the changes in the file modification state
	// ctremblay: NOTE this will be moved out ResourceLoader in due time. It literally as nothing to do with Resource Loading ...
	class RED_REFLECTION_API IModificationListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IModificationListener() = 0;

		// Called when resource was modified
		virtual void OnModified( const ResourcePath& resPath ) = 0;

		// Called when resource was unmodified
		virtual void OnUnmodified( const ResourcePath& resPath ) = 0;
	};

	class ModificationNotifier;

	/// helper class (editor only) that allows others to externally monitor changes in resources
	class RED_REFLECTION_API MonitorRouter
	{
	public:
		MonitorRouter();
		~MonitorRouter();

		enum class ChangeType
		{
			Stackable,		// sending the same message to the same resource notifies only once
			NonStackable,	// every event is handled
		};

		/// Check if a given resource is currently monitored for changes
		Bool IsResourceMonitored( const ResourcePath& path );

		/// signal listeners about change in resource
		/// NOTE: changes are queued and dispatched in a safe place in main thread (but not necessarily every frame)
		/// NOTE: it's assumed that by default changes are stackable (ie. sending "refresh" multiple times refreshes resource only once)
		void QueueChange( const ResourcePath& path, const CName& message, const MonitorUserDataPtr& userData = MonitorUserDataPtr(), ChangeType type = ChangeType::NonStackable );

		/// dispatch all pending changes
		/// NOTE: can take a lot of time
		void DispatchChanges();

		//----

		/// register resource monitor
		void RegisterMonitor(const Uint64 pathHash, Monitor* monitor);

		/// unregister resource monitor
		void UnregisterMonitor(const Uint64 pathHash, Monitor* monitor);

		//----

		/// Unregister all the monitors and cleanup the queue
		void Cleanup();

		// ctremblay: Moved from ResourceLoader. Never belonged there in the first place. 
		// This whole class need some love. 
		void RegisterModificationListener( IModificationListener* listener );
 		void UnregisterModificationListener( IModificationListener* listener );
		void OnModifiedResource(const res::ResourcePath& resPath) const;
		void OnUnmodifiedResource(const res::ResourcePath& resPath ) const;
		/// we have only one instance for the whole engine
		static MonitorRouter& GetInstance();

	private:
		struct Change
		{
			CName m_message;
			MonitorUserDataPtr m_data;
			Bool m_canStack;

			Change(CName message, const MonitorUserDataPtr& data, const Bool canStack);
		};

		typedef red::HashSet< Monitor* > TMonitorSet;
		typedef red::HashMap<Uint64, red::UniquePtr< TMonitorSet, red::PoolEngine > > TRegisteredMonitors;
		typedef red::HashMap<ResourcePath, red::DynArray< Change > > TChangesQueue;

		red::Mutex				m_monitorsLock;
		red::Mutex				m_changesLock;
		TRegisteredMonitors		m_monitors;
		TChangesQueue			m_changes;

		typedef red::HashSet< Monitor* > TBlacklistedMonitors;
		TBlacklistedMonitors	m_dispatchBufferedRemoves; // removed during dispatch
		TBlacklistedMonitors	m_dispatchBufferedAdds; // added during dispatch
		const TMonitorSet*		m_dispatchedMonitorSet;

		red::UniquePtr< ModificationNotifier > m_modificationNotifier;

		void DispatchChange(const ResourcePath& path, const CName message, const MonitorUserDataPtr& userData);
	};

	/// resource monitor that will "monitor" state of a single resource
	/// NOTE: the monitor is not registered unless there is actual callback
	class RED_REFLECTION_API Monitor : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		Monitor();
		~Monitor();

		/// bind to path
		void Bind(const ResourcePath& path);

		/// set callback, note: we support only one callback for now
		void SetCallback(const TMonitorCallback& callback);

		/// dispatch resource change
		void Dispatch(const ResourcePath& path, const CName& message, const MonitorUserDataPtr& userData);

	private:
		TMonitorCallback		m_callback;
		Uint64					m_pathHash;

		void ConditionalBind();
		void ConditionalUnbind();
	};

	/// wrapper user data
	template< typename T >
	class TMonitorUserData : public IMonitorUserData
	{
	public:
		RED_FORCE_INLINE TMonitorUserData(const T& data)
			: m_data(data)
		{}

		RED_FORCE_INLINE TMonitorUserData(T&& data)
			: m_data(std::move(data))
		{}

		RED_FORCE_INLINE const T& GetData() const
		{
			return m_data;
		}

		RED_FORCE_INLINE T& GetData()
		{
			return m_data;
		}

	private:
		T m_data;
	};

	template< typename T >
	RED_FORCE_INLINE const T& GetMonitorUserData(const MonitorUserDataPtr& userData)
	{
		RED_FATAL_ASSERT(userData, "Empty data");
		return (static_cast<const TMonitorUserData<T>*>(userData.Get()))->GetData();
	}

} // res