/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "streamedResource.h"
#include "streamedResourceManager.h"
#include "streamedResourceListener.h"
#include "resourceDepot.h"

#include "serializationFileTables.h"
#include "serializationAsyncSource.h"
#include "serializationBinaryUtilities.h"

namespace res
{

	const char * c_editorStreamingCacheFilename = "streaming.cache";
	const char * c_cmdletStreamingCacheFilename = "cmdlet_streaming.cache";

	//----

	IStreamedResourceListener::~IStreamedResourceListener()
	{
	}

	//---

	StreamingResourceManager::StreamingResourceManager(IResourceDepot* depot)
		: m_cache( depot )
	{
	}

	StreamingResourceManager::~StreamingResourceManager()
	{
		m_cache.Save();
	}

	void StreamingResourceManager::ClearCache()
	{
 		m_cache.ClearCache();
	}

	void StreamingResourceManager::RegisterListener( IStreamedResourceListener* listener )
	{
		RED_FATAL_ASSERT(listener != nullptr, "Invalid listener");

		RED_SCOPE_LOCK(m_listenersLock);
		RED_VERIFY( m_listeners.Insert( listener ).IsSuccessful() );
	}

	void StreamingResourceManager::UnregisterListener(IStreamedResourceListener* listener)
	{
		RED_FATAL_ASSERT(listener != nullptr, "Invalid listener");

		RED_SCOPE_LOCK(m_listenersLock);
		RED_VERIFY( m_listeners.Remove( listener ).IsSuccessful() );
	}

	void StreamingResourceManager::NotifyListenersStreamingDataChanged( const res::ResourcePath& path, const StreamingData& data )
	{
		// Assumes doesn't matter if values were actually changed if event triggered
		// This currently could be used as a way of initially updating systems with data, as well as notifying of changes
		// notify dynamic listeners
		RED_SCOPE_SHARED_LOCK( m_listenersLock );
		for ( auto* ptr : m_listeners )
			ptr->OnStreamingDataChanged( path, data );
	}

	void StreamingResourceManager::RefreshStreamingData( const res::ResourcePath& path )
	{
		m_cache.UpdateStreamingData( path );
		NotifyListenersStreamingDataChanged( path, m_cache.GetStreamingData( path ) );
	}

	StreamingData StreamingResourceManager::GetStreamingData( const res::ResourcePath& path ) const
	{
		return m_cache.GetStreamingData( path );
	}

	StreamingData StreamingResourceManager::GetStreamingData( const res::ResourcePath& path, CName optionalName ) const
	{
		return m_cache.GetStreamingData( path, optionalName );
	}

	red::DynArray< res::ResourcePath > StreamingResourceManager::GetReverseDependencies(const res::ResourcePath& path) const
	{
		return m_cache.GetReverseDependencies(path);
	}

	Bool StreamingResourceManager::UpdateRecursively(const res::ResourcePath& path, red::DynArray< res::ResourcePath >& brokenResources)
	{
		const StreamedResourceCache::Updater cacheUpdater( m_cache );
		return cacheUpdater.UpdateTopDown( path, brokenResources );
	}

	Bool StreamingResourceManager::Load( bool isCmdlet )
	{
		if( isCmdlet )
		{
			m_cache.RetryOnFileNotFound();	
		}

		return m_cache.Load( isCmdlet ? c_cmdletStreamingCacheFilename : c_editorStreamingCacheFilename );
	}

	Bool StreamingResourceManager::Save()
	{
		return m_cache.Save();
	}

	void StreamingResourceManager::InitializeDataExtractors()
	{
		m_cache.InitializeDataExtractors();
	}

	void StreamingResourceManager::ExportMeshLocalBounds(red::HashMap<Uint64, Box>& outBounds) const
	{
		m_cache.ExportMeshLocalBounds(outBounds);
	}

	Float StreamingResourceManager::GetDefaultStreamingDistance() const
	{
		// TODO this should depend on resource type
		return 75.0f;
	}

	//----

}

static res::StreamingResourceManager* GStreamingResourceManager = nullptr;

res::StreamingResourceManager* GetStreamingResourceManager()
{
	return GStreamingResourceManager;
}

void SetStreamingResourceManager( res::StreamingResourceManager* manager )
{
	GStreamingResourceManager = manager;
}
