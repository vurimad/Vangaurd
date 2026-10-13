/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "streamedResourceCache.h"

namespace res
{
	class StreamedResource;
	class IStreamedResourceListener;

	/// a "hub" for querying streaming distances of the resources
	class RED_REFLECTION_API StreamingResourceManager
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		explicit StreamingResourceManager( IResourceDepot* depot );
		~StreamingResourceManager();

		static const String& GetMasterCacheFilePath();

		// listener management
		void RegisterListener(IStreamedResourceListener* listener);
		void UnregisterListener(IStreamedResourceListener* listener);

		// reset cached distances, all requires will read again from the file
		void ClearCache();

		// force refresh streaming data for a given resource - updates the cache
		void RefreshStreamingData( const res::ResourcePath& path );

		// notify listeners about streaming data change
		void NotifyListenersStreamingDataChanged( const res::ResourcePath& path, const StreamingData& data );

		// get streaming data for given resource path
		// Note: this will load the resource if the values are not in cache
		StreamingData GetStreamingData( const res::ResourcePath& path ) const;
		StreamingData GetStreamingData( const res::ResourcePath& path, CName optionalName ) const; // ctremblay: use for Appearance where streaming distance is different per definition.

		red::DynArray< res::ResourcePath > GetReverseDependencies(const res::ResourcePath& path) const;

		// Refresh streaming data for given resource path
		// Note: this will load all the dependent resources if they are updated or not in cache
		// Note: this function will return false if circular prefab dependency is detected
		Bool UpdateRecursively( const res::ResourcePath& path, red::DynArray< res::ResourcePath >& brokenResources );

		// Get default streaming distance (used only when distance calculation failed)
		Float GetDefaultStreamingDistance() const;

		Bool Load( bool isCmdlet );
		Bool Save( );

		void InitializeDataExtractors();

		void ExportMeshLocalBounds(red::HashMap<Uint64, Box>& outBounds) const;

	private:
		StreamedResourceCache m_cache;

		typedef red::HashSet< IStreamedResourceListener* > TListeners;
		mutable red::RWLock m_listenersLock;
		TListeners	m_listeners{ red::PoolEngine() };
	};

} // res

// Returns the current active streaming resource manager
// Can return nullptr if there is no active streaming resource manager
RED_REFLECTION_API res::StreamingResourceManager* GetStreamingResourceManager();
RED_REFLECTION_API void SetStreamingResourceManager( res::StreamingResourceManager* manager );
