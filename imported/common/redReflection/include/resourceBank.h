/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"
#include "engineTime.h"
#include "resourceSwapList.h"

namespace res
{
	class IResourceDepot;
	class ResourceSnapshot;

	class RED_REFLECTION_API ResourceBank
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ResourceBank();
		RED_MOCKABLE ~ResourceBank();
		
		void Initialize( IResourceDepot* depot );
		void Shutdown();

		void ApplyCensorship( red::UniquePtr< ResourceSwapList > censorSwapList );

		void EnumLoadedResources( red::DynArray< ResourcePath >& outPaths ) const;

		RED_MOCKABLE THandle< CResource > FindResource( const ResourcePath & path ) const;
		void IsResourceLoadedBulkQuery(const red::ArraySpan< const ResourcePath >& paths, red::BitSet64Dynamic& outResults) const;


		void RegisterResource( const ResourcePath & path, const THandle< CResource > & resource, Bool gameCache = false );
		THandle< CResource > UnregisterResource( const ResourcePath & path );
		THandle< CResource > TryRegisterResource( const ResourcePath& path, const THandle< CResource >& resource, Bool gameCache = false );

		// ctremblay: IMPORTANT ResolvePath could do IO operation on loose file. Only ResourceLoader should call this function! 
		ResourcePath ResolvePath( const ResourcePath & path ); 
		void ResolvePathBulk(const red::ArraySpan< ResourcePath >& inOutPaths);

		void Internal_SetResourceDepot( const IResourceDepot * depot );

		THandle< ResourceSnapshot > CreateSnapshot(Bool includeExpired) const;
		void TryLockResourcesInSnapshot(const THandle< ResourceSnapshot > & snapshot, red::DynArray<THandle<CResource>>& outLockedResources);

		void UpdateGameCache();
		void EnableGameCacheLoadingMode(Bool value);
		void FlushGameCache();

		Uint32 GetNumGameCacheEntries() const;

		void ShutdownGameCacheManuallyBecauesRenderingCrashesOtherwise();

	private:
		// NOTE: Doesn't ensure the resource will STAY alive after calling this
		// so be careful of using for any such cases
		Bool IsResourceAlive_NoLock(const ResourcePath& path) const;

		THandle< CResource > FindResource_NoLock(const ResourcePath & path) const;
		ResourcePath ResolveCensorsedPath( const ResourcePath& path ) const;
		void ResolveCensorsedPathBulk(const red::ArraySpan<ResourcePath>& inOutPaths) const;

		struct ResourceEntry
		{
			WeakHandle< CResource > resource;
			EngineTime creationTime;
		};

		struct GameCacheEntry
		{
			THandle< CResource > resource;
			EngineTime expirationTime;
		};

		typedef red::HashMap< ResourcePath, ResourceEntry > ResourceDictionary;
		typedef red::HashMap< ResourcePath, ResourcePath > ResolvedPathDictionary;
		typedef red::DynArray< GameCacheEntry > GameResourceCache;
		
		ResourceDictionary m_resourceDictionary;
		ResolvedPathDictionary m_resolvedPathDictionary;
		GameResourceCache m_gameResourceCache;

		const IResourceDepot* m_depot;
		red::UniquePtr< ResourceSwapList > m_censorSwapList;

		mutable red::RWSpinLock m_resourceDictionaryLock;
		mutable red::RWSpinLock m_resolvedPathDictionaryLock;
		mutable red::RWSpinLock m_gameResourceCacheLock;
		mutable red::RWSpinLock m_censorshipLock;
		Bool m_gameResourceCacheShutdown{ false };
		Bool m_gameResourceCacheLoadingMode{ false };
		Uint32 m_gameResourceCacheSizeBeforeLoadingMode{ 0 };
	};

	red::UniquePtr< ResourceBank > CreateResourceBank( IResourceDepot* depot );
}
