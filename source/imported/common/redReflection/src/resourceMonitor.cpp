/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceMonitor.h"

namespace res
{
	IModificationListener::~IModificationListener()
	{}

	class ModificationNotifier
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ModificationNotifier()
		{
			
		}

		~ModificationNotifier()
		{
	
		}

		// Raise OnModified event
		void NotifyModified(const res::ResourcePath& resPath) const
		{
			for ( auto* listener : m_listeners )
				listener->OnModified( resPath );
		}

		// Raise OnUnmodified event
		void NotifyUnmodified(const res::ResourcePath& resPath) const
		{
			for ( auto* listener : m_listeners )
				listener->OnUnmodified(resPath);
		}

		// Register listener
		void RegisterListener( IModificationListener* listener )
		{
			RED_FATAL_ASSERT( ::SIsMainThread(), "Can be called from the main thread." );
			RED_FATAL_ASSERT( listener, "Trying to add null listener." );

			red::alg::PushBackUnique( m_listeners, listener );
		}

		// Unregister listener
		void UnregisterListener( IModificationListener* listener )
		{
			RED_FATAL_ASSERT( ::SIsMainThread(), "Can be called from the main thread." );
			RED_FATAL_ASSERT( listener, "Trying to add null listener." );

			if ( listener )
				m_listeners.Remove( listener );
		}

		// Clear listeners list
		void Clear()
		{
			RED_FATAL_ASSERT( ::SIsMainThread(), "Can be called from the main thread." );
			m_listeners.Clear();
		}

	private:
		red::DynArray< IModificationListener* > m_listeners{ red::PoolBackend() };
	};

	//----

	IMonitorUserData::~IMonitorUserData()
	{
	}

	//----

	MonitorRouter::Change::Change(CName message, const MonitorUserDataPtr& data, const Bool canStack)
		: m_message(message)
		, m_data(data)
		, m_canStack(canStack)
	{}

	//----

	MonitorRouter::MonitorRouter()
		: m_modificationNotifier( red::CreateUniquePtr< ModificationNotifier >() )
		, m_monitors( red::PoolBackend() )
		, m_changes( red::PoolBackend() )
		, m_dispatchBufferedRemoves( red::PoolBackend() )
		, m_dispatchBufferedAdds( red::PoolBackend() )
	{}

	MonitorRouter::~MonitorRouter()
	{}

	void MonitorRouter::QueueChange( const ResourcePath& path, const CName& message, const MonitorUserDataPtr& userData, ChangeType type )
	{
		red::ScopedLock<red::Mutex> lock(m_changesLock);

		auto& changes = m_changes.GetRef( path, red::PoolBackend() );

		Bool stackable = false;
		Bool addNew = true;
		if ( type == ChangeType::Stackable )
		{
			for (auto& it : changes)
			{
				// stack in existing action
				if (it.m_message == message && it.m_canStack)
				{
					it.m_data = userData;
					addNew = false;
					break;
				}
			}

			stackable = true;
		}

		if (addNew)
			changes.EmplaceBack(message, userData, stackable);
	}

	void MonitorRouter::DispatchChanges()
	{
		TChangesQueue changes{ red::PoolEngine() };

		{
			red::ScopedLock<red::Mutex> lock(m_changesLock);
			changes = std::move(m_changes);
		}

		for (const auto& resourceChanges : changes)
		{
			for (const auto& change : resourceChanges.Value())
			{
				DispatchChange(resourceChanges.Key(), change.m_message, change.m_data);
			}
		}
	}

	Bool MonitorRouter::IsResourceMonitored( const ResourcePath& path )
	{
		red::ScopedLock<red::Mutex> lock( m_monitorsLock );

		const auto iter = m_monitors.Find( path.GetHash() );
		if ( iter == m_monitors.End() )
			return false;

		if ( iter.Value() == nullptr )
			return false;

		return true;
	}

	void MonitorRouter::DispatchChange(const ResourcePath& path, const CName message, const MonitorUserDataPtr& userData)
	{
		TMonitorSet* listeners = nullptr;

		{
			red::ScopedLock<red::Mutex> lock( m_monitorsLock );

			listeners = m_monitors[ path.GetHash() ].Get();
			if ( nullptr == listeners )
			{
				return;
			}

			RED_FATAL_ASSERT( m_dispatchedMonitorSet == nullptr, "Already dispatching something" );
			RED_FATAL_ASSERT( m_dispatchBufferedRemoves.Empty(), "Leftovers from previous iteration" );
			RED_FATAL_ASSERT( m_dispatchBufferedAdds.Empty(), "Leftovers from previous iteration" );

			m_dispatchedMonitorSet = listeners;
		}

		// iterate over hashset, we have exclusive access now
		for (auto it = listeners->Begin(); it != listeners->End(); ++it)
		{
			auto* monitor = *it;

			{
				red::ScopedLock<red::Mutex> lock( m_monitorsLock );

				if ( m_dispatchBufferedRemoves.Exist( monitor ) )  // skip over the ones we want to remove
				{
					continue;
				}
			}

			monitor->Dispatch(path, message, userData);
		}


		{
			red::ScopedLock<red::Mutex> lock( m_monitorsLock );

			m_dispatchedMonitorSet = nullptr;

			// remove the cached monitors
			for (auto* it : m_dispatchBufferedRemoves)
				listeners->Remove(it);
			
			// add the cached monitors
			for (auto* it : m_dispatchBufferedAdds)
				listeners->Insert(it);

			m_dispatchBufferedRemoves.Clear();
			m_dispatchBufferedAdds.Clear();
		}
	}

	void MonitorRouter::RegisterMonitor(const Uint64 pathHash, Monitor* monitor)
	{
		red::ScopedLock<red::Mutex> lock(m_monitorsLock);

		auto& listeners = m_monitors[pathHash];
		if (listeners == nullptr)
		{
			listeners = red::CreateUniquePtr< TMonitorSet, red::PoolEngine >( TMonitorSet( red::PoolEngine() ) );
		}

		if (listeners.Get() == m_dispatchedMonitorSet)
		{
			m_dispatchBufferedRemoves.Remove(monitor);
			m_dispatchBufferedAdds.Insert(monitor);
		}
		else
		{
			RED_FATAL_ASSERT(!listeners->Exist(monitor), "Double registration of resource monitor");
			listeners->Insert(monitor);
		}
	}

	void MonitorRouter::UnregisterMonitor(const Uint64 pathHash, Monitor* monitor)
	{
		red::ScopedLock<red::Mutex> lock(m_monitorsLock);

		auto& listeners = m_monitors[pathHash];
		if (listeners != nullptr)
		{
			if (listeners.Get() == m_dispatchedMonitorSet)
			{
				m_dispatchBufferedAdds.Remove(monitor);
				m_dispatchBufferedRemoves.Insert(monitor);
			}
			else
			{
				listeners->Remove(monitor);
			}
		}
	}

	void MonitorRouter::Cleanup()
	{
		{
			red::ScopedLock<red::Mutex> lock(m_monitorsLock);
			m_monitors.Clear();
			m_dispatchBufferedRemoves.Clear();
			m_dispatchBufferedAdds.Clear();
			m_dispatchedMonitorSet = nullptr;
		}

		{
			red::ScopedLock<red::Mutex> lock(m_changesLock);
			m_changes.Clear();
		}
	}

	MonitorRouter& MonitorRouter::GetInstance()
	{
		static MonitorRouter theInstance;
		return theInstance;
	}

	void MonitorRouter::RegisterModificationListener( IModificationListener* listener )
	{
		m_modificationNotifier->RegisterListener( listener );
	}

 	void MonitorRouter::UnregisterModificationListener( IModificationListener* listener )
	{
		m_modificationNotifier->UnregisterListener( listener );
	}

	void MonitorRouter::OnModifiedResource(const res::ResourcePath& resPath) const
	{
		m_modificationNotifier->NotifyModified(resPath);
	}

	void MonitorRouter::OnUnmodifiedResource(const res::ResourcePath& resPath ) const
	{
		m_modificationNotifier->NotifyUnmodified( resPath );
	}

	//----

	Monitor::Monitor()
		: m_pathHash(0)
	{}

	Monitor::~Monitor()
	{
		ConditionalUnbind();
	}

	void Monitor::Bind(const ResourcePath& path)
	{
		const auto pathHash = path.IsValid() ? path.GetHash() : 0;
		if (m_pathHash != pathHash)
		{
			ConditionalUnbind();
			m_pathHash = pathHash;
			ConditionalBind();
		}
	}

	void Monitor::ConditionalUnbind()
	{
		if (m_pathHash)
			MonitorRouter::GetInstance().UnregisterMonitor(m_pathHash, this);
	}

	void Monitor::ConditionalBind()
	{
		if (m_pathHash && m_callback)
			MonitorRouter::GetInstance().RegisterMonitor(m_pathHash, this);
	}

	void Monitor::SetCallback(const TMonitorCallback& callback)
	{
		const auto wasValid = (Bool)m_callback;
		const auto isValid = (Bool)callback;

		m_callback = callback;

		if (wasValid && !isValid)
			ConditionalUnbind();
		else if (!wasValid && isValid)
			ConditionalBind();
	}

	void Monitor::Dispatch(const ResourcePath& path, const CName& message, const MonitorUserDataPtr& userData)
	{
		if (m_callback)
			m_callback(path, message, userData);
	}

	//----

} // res
