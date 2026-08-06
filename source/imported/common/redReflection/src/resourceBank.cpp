/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceBank.h"
#include "resourceDepot.h"
#include "resource.h"
#include "resourceSnapshot.h"
#include "resourceSwapList.h"
#include "../../redJobs2/include/jobBuilder.h"

namespace Config
{
	static TConfigVar<Int32> cvGameResourceCacheExpirySeconds("ResourceBank", "GameResourceCacheExpirySeconds", 20, eConsoleVarFlag_Developer);
}

namespace res
{
	ResourceBank::ResourceBank()
		: m_resourceDictionary( red::PoolEngine() )
		, m_resolvedPathDictionary( red::PoolEngine() )
		, m_gameResourceCache( red::PoolEngine() )
		, m_depot( nullptr )
		, m_censorSwapList( nullptr )
	{}

	ResourceBank::~ResourceBank()
	{}

	Bool ResourceBank::IsResourceAlive_NoLock(const ResourcePath& path) const
	{
		auto iter = m_resourceDictionary.Find(path);
		if (iter != m_resourceDictionary.End())
		{
			const WeakHandle< CResource >& resource = iter.Value().resource;
			return !resource.Expired();
		}

		return false;
	}

	THandle< CResource > ResourceBank::FindResource_NoLock(const ResourcePath& path) const
	{
		auto iter = m_resourceDictionary.Find(path);
		if (iter != m_resourceDictionary.End())
		{
			const WeakHandle< CResource > & resource = iter.Value().resource;

			const THandle< CResource > result = resource.ToHandle();

			return result;
		}

		return THandle< CResource >();
	}
	
	THandle< CResource > ResourceBank::FindResource( const ResourcePath & path ) const
	{
		red::ScopedSharedLock< red::RWSpinLock > lock( m_resourceDictionaryLock );

		return FindResource_NoLock(path);
	}

	void ResourceBank::IsResourceLoadedBulkQuery(const red::ArraySpan< const ResourcePath >& paths, red::BitSet64Dynamic& outResults) const
	{
		RED_FATAL_ASSERT(outResults.Size() >= paths.Size(), "%u vs %u", outResults.Size(), paths.Size());
		outResults.ClearAll();

		// DON'T REFACTOR THIS to make it 'cleaner': Don't use ResolvePath() because don't wan't to normally hit any exclusive lock along that path.
		// Yes, this means we don't m_resolvedPathDictionary. No big deal in the game where m_depot->ResolveResourcePath is a no-op anyway.

		red::DynArray<res::ResourcePath> resolvedPaths{ job::PoolJobScope() };
		resolvedPaths = paths;

		ResolveCensorsedPathBulk(resolvedPaths);
		m_depot->ResolveResourcePathBulk(resolvedPaths);

		red::ScopedSharedLock< red::RWSpinLock > lock(m_resourceDictionaryLock);
		for (Uint32 i = 0, len = resolvedPaths.Size(); i < len; ++i)
		{
			if (IsResourceAlive_NoLock(resolvedPaths[i]))
			{
				outResults.Set(i);
			}
		}
	}

	void ResourceBank::RegisterResource(const ResourcePath& path, const THandle< CResource >& resource, Bool gameCache)
	{
		ALWAYSENABLED_RED_FATAL_ASSERT(path.IsValid());

		red::ScopedLock< red::RWSpinLock > lock( m_resourceDictionaryLock );
		auto iter = m_resourceDictionary.Find( path );
		if( iter != m_resourceDictionary.End() )
		{
			// NOTE: check for game mode because resource cancellation can mean we wind up loading the same resource twice in some cases
			RED_FATAL_ASSERT( IsGameMode() || iter.Value().resource.Expired(), "Internal Logic Error. Resource with same path already exist." );
			iter.Value() = { resource, resource->GetCreationTimeStamp() };
		}
		else
		{
			m_resourceDictionary.Insert( path, { resource, resource->GetCreationTimeStamp() } );
		}

		Bool gameResourceCacheLoadingMode = false;
		{
			RED_SCOPE_SHARED_LOCK(m_gameResourceCacheLock);
			gameResourceCacheLoadingMode = m_gameResourceCacheLoadingMode;
		}

		if ((gameCache || gameResourceCacheLoadingMode) && IsGameMode())
		{
			RED_SCOPE_LOCK(m_gameResourceCacheLock);

			if (!m_gameResourceCacheShutdown)
			{
	#ifdef RED_ASSERTS_ENABLED
				auto findIt = std::find_if(m_gameResourceCache.Begin(), m_gameResourceCache.End(), [path](const auto& entry) {
					return path == entry.resource->GetPath();
				});
				RED_FATAL_ASSERT(findIt == m_gameResourceCache.End(), "Regitering cached resource %hs multiple times. Will leak!", path.ToDebugString());
	#endif
				m_gameResourceCache.PushBack({ resource, EngineTime::ZERO });
			}
		}
	}

	THandle< CResource > ResourceBank::TryRegisterResource( const ResourcePath& path, const THandle< CResource >& resource, Bool gameCache )
	{
		ALWAYSENABLED_RED_FATAL_ASSERT(path.IsValid());

		// Assume already cached if already exists in the resourceDictionary.
		{
			RED_SCOPE_SHARED_LOCK( m_resourceDictionaryLock );
			auto iter = m_resourceDictionary.Find( path );
			if( iter != m_resourceDictionary.End() )
			{
				THandle< CResource > existingResource = iter.Value().resource.ToHandle();
				if( existingResource )
				{
					return existingResource;
				}
			}
		}

		RED_SCOPE_LOCK(m_resourceDictionaryLock);
		auto iter = m_resourceDictionary.Find(path);
		if (iter != m_resourceDictionary.End())
		{
			THandle< CResource > existingResource = iter.Value().resource.ToHandle();
			if( existingResource )
			{
				return existingResource;
			}
			else
			{
				iter.Value() = { resource, resource->GetCreationTimeStamp() };
			}
		}
		else
		{
			m_resourceDictionary.Insert( path, {resource, resource->GetCreationTimeStamp()} );
		}
		
		Bool gameResourceCacheLoadingMode = false;
		{
			RED_SCOPE_SHARED_LOCK(m_gameResourceCacheLock);
			gameResourceCacheLoadingMode = m_gameResourceCacheLoadingMode;
		}

		if ((gameCache || gameResourceCacheLoadingMode) && IsGameMode())
		{
			RED_SCOPE_LOCK(m_gameResourceCacheLock);

			if (!m_gameResourceCacheShutdown)
			{
#ifdef RED_ASSERTS_ENABLED
				auto findIt = std::find_if(m_gameResourceCache.Begin(), m_gameResourceCache.End(), [path](const auto& entry) {
					return path == entry.resource->GetPath();
				});
				RED_FATAL_ASSERT(findIt == m_gameResourceCache.End(), "Regitering cached resource %hs multiple times. Will leak!", path.ToDebugString());
#endif
				m_gameResourceCache.PushBack({ resource, EngineTime::ZERO });
			}
		}

		return resource;
	}

	THandle< CResource > ResourceBank::UnregisterResource( const ResourcePath & path )
	{
		{
			red::ScopedLock< red::RWSpinLock > lock(m_gameResourceCacheLock);
			auto findIt = std::find_if(m_gameResourceCache.Begin(), m_gameResourceCache.End(), [path](const auto& entry) {
				return entry.resource->GetPath() == path;
			});
			if (findIt != m_gameResourceCache.End())
			{
				m_gameResourceCache.Remove(findIt);
			}
		}

		red::ScopedLock< red::RWSpinLock > lock( m_resourceDictionaryLock );
		auto iter = m_resourceDictionary.Find( path );
		if( iter != m_resourceDictionary.End() )
		{
			const WeakHandle< CResource > & resource = iter.Value().resource;
			THandle< CResource > returnValue = resource.ToHandle();
			m_resourceDictionary.Remove( iter );
			return returnValue;
		}

		return nullptr;
	}

	ResourcePath ResourceBank::ResolveCensorsedPath( const ResourcePath& path ) const
	{
		RED_SCOPE_SHARED_LOCK( m_censorshipLock );
		if ( m_censorSwapList != nullptr )
		{
			return m_censorSwapList->ResolvePath( path );
		}
		return path;
	}

	void ResourceBank::ResolveCensorsedPathBulk(const red::ArraySpan<ResourcePath>& inOutPaths) const
	{
		RED_SCOPE_SHARED_LOCK(m_censorshipLock);
		if (m_censorSwapList != nullptr)
		{
			for (Uint32 i : inOutPaths.Indices())
			{
				inOutPaths[i] = m_censorSwapList->ResolvePath(inOutPaths[i]);
			}
		}
	}

	ResourcePath ResourceBank::ResolvePath( const ResourcePath& path )
	{
		{
			red::ScopedSharedLock< red::RWSpinLock > lock( m_resolvedPathDictionaryLock );
			auto iter = m_resolvedPathDictionary.Find( path );
			if( iter != m_resolvedPathDictionary.End() )
			{
				return iter.Value();
			}
		}

		// Resolve censorship before resolving links
		const ResourcePath lookupPath = ResolveCensorsedPath( path );
		const ResourcePath resolvedPath = m_depot->ResolveResourcePath( lookupPath );
		if( resolvedPath.IsValid() )
		{
			red::ScopedLock< red::RWSpinLock > lock( m_resolvedPathDictionaryLock );
			m_resolvedPathDictionary[ path ] = resolvedPath;
			return resolvedPath;
		}

		return lookupPath;
	}

	void ResourceBank::ResolvePathBulk(const red::ArraySpan< ResourcePath >& inOutPaths)
	{
		// DON'T REFACTOR THIS to make it 'cleaner': Don't use ResolvePath() because don't wan't to normally hit any exclusive lock along that path.
		// Yes, this means we don't m_resolvedPathDictionary. No big deal in the game where m_depot->ResolveResourcePath is a no-op anyway.

		// Resolve censorship before resolving links
		ResolveCensorsedPathBulk(inOutPaths);
		m_depot->ResolveResourcePathBulk(inOutPaths);
	}

	void ResourceBank::Initialize(IResourceDepot* depot)
	{
		m_depot = depot;
	}

	void ResourceBank::Shutdown()
	{
#ifdef RED_LOGGING_ENABLED
	
		if( !IsGameMode() )
			return;

		m_gameResourceCache.Clear();

		struct Leak
		{
			EngineTime time;
			Uint32 refCount;
			ResourcePath path;
		};

		red::DynArray< Leak > leaks{ red::PoolDebug() };

		{
			red::ScopedSharedLock< red::RWSpinLock > lock( m_resourceDictionaryLock );

			for( auto iter = m_resourceDictionary.Begin(), end = m_resourceDictionary.End(); iter != end ; ++iter )
			{
				const ResourceEntry & entry = iter.Value();
				if( !entry.resource.Expired() )
				{
					const Uint32 refCount = entry.resource.GetRefCount();
					leaks.PushBack( { entry.creationTime, refCount, iter.Key() } );
				}
			}
		}
		
		if( !leaks.Empty() )
		{
			std::sort( leaks.Begin(), leaks.End(), []( const Leak& left, const Leak & right ){ return left.time < right.time; } );

			RED_LOG_WARNING( "Resource: ***********************************************************************" );
			RED_LOG_WARNING( "Resource: LEAK DETECTED: %d leak", leaks.Size() );
			RED_LOG_WARNING( "Resource: Dumping all resource leak, sorted by creation time." );
			RED_LOG_WARNING( "Resource: Resource Path - RefCount value" );
			RED_LOG_WARNING( "Resource: ***********************************************************************" );

			for( auto & leak : leaks )
			{
				RED_LOG_WARNING( "Resource: %hs - %d", leak.path.ToDebugString(), leak.refCount );
			}
			RED_LOG_WARNING( "Resource: ***********************************************************************" );
		}
		else
		{
			RED_LOG_INFO( "Resource: ***********************************************************************" );
			RED_LOG_INFO( "Resource: Leak Count: 0. Awesome." );
			RED_LOG_INFO( "Resource: ***********************************************************************" );
		}

		RED_LOG_FLUSH();
#endif
	}

	void ResourceBank::ApplyCensorship( red::UniquePtr< ResourceSwapList > censorSwapList )
	{
		{
			RED_SCOPE_LOCK( m_censorshipLock );
			m_censorSwapList = std::move( censorSwapList );
		}

		{
			red::ScopedLock< red::RWSpinLock > lock( m_resolvedPathDictionaryLock );
			m_resolvedPathDictionary.Clear();
		}

		{
			RED_SCOPE_LOCK(m_gameResourceCacheLock);
			m_gameResourceCache.Clear();
		}
	}

	void ResourceBank::EnumLoadedResources( red::DynArray< ResourcePath >& outPaths ) const
	{
		red::ScopedSharedLock< red::RWSpinLock > lock( m_resourceDictionaryLock );

		for ( const auto& iter : m_resourceDictionary )
		{
			if ( iter.Value().resource.ToHandle() )
			{
				outPaths.PushBack( iter.Key() );
			}
		}
	}

	void ResourceBank::Internal_SetResourceDepot( const IResourceDepot * depot )
	{
		m_depot = depot;
	}

	THandle< res::ResourceSnapshot > ResourceBank::CreateSnapshot(Bool includeExpired) const
	{
		PC_SCOPE(ResourceBank_CreateSnapshot);

		red::Timer timer;

		red::ScopedSharedLock< red::RWSpinLock > lock(m_resourceDictionaryLock);

		red::DynArray<TResAsyncRef<CResource>> loadedResources{ red::PoolEngine() };
		loadedResources.Reserve(m_resourceDictionary.Size());

		// NOTE: race condition where the resource expires is acceptable.
		// We don't do anything with the resource anyway
		for (const auto& kv : m_resourceDictionary)
		{
			if (includeExpired || !kv.Value().resource.Expired())
			{
				loadedResources.EmplaceBack(kv.Key());
			}
		}

		RED_LOG_INFO("ResourceBank::CreateSnapshot for %u/%u resources finished in %.3f sec", loadedResources.Size(), m_resourceDictionary.Size(), timer.GetSeconds());

		return CreateHandle< res::ResourceSnapshot >(std::move(loadedResources));
	}

	void ResourceBank::TryLockResourcesInSnapshot(const THandle< ResourceSnapshot > & snapshot, red::DynArray<THandle<CResource>>& outLockedResources)
	{
		// This whole thing needs revisiting anyway
#if 0
		PC_SCOPE(ResourceBank_TryLockResourcesInSnapshot);

		red::Timer timer;

		RED_FATAL_ASSERT(snapshot);

		Uint32 numLocked = 0;

		RED_SCOPE_LOCK(m_resourceDictionaryLock);
		outLockedResources.Reserve(snapshot->GetLoadedResourceHashes().Size());
		for (Uint64 resourceHash : snapshot->GetLoadedResourceHashes())
		{
			const auto resourcePath = res::ResourcePath::Build(resourceHash);
			auto resource = FindResource_NoLock(resourcePath);
			if (resource)
			{
				numLocked += 1;
				outLockedResources.PushBack(resource);
			}
		}

		RED_LOG_INFO("ResourceBank::TryLockResourcesInSnapshot locked %u/%u resources in %.3f sec", numLocked, snapshot->GetLoadedResourceHashes().Size(), timer.GetSeconds());
#endif
	}

	void ResourceBank::UpdateGameCache()
	{
		PC_SCOPE(ResourceBank_UpdateGameCache);

		if (!IsGameMode())
		{
			return;
		}

		const EngineTime now = EngineTime::GetNow();
		const EngineTime nextExpirationTime = now + Config::cvGameResourceCacheExpirySeconds.Get();

		RED_SCOPE_LOCK(m_gameResourceCacheLock);

		// Don't remove any resources while loading, they're likely to be reloaded otherwise
		if (m_gameResourceCacheLoadingMode)
		{
			return;
		}

		Uint32 i = 0;
		red::DynArray< THandle< CResource > > evictList{ red::PoolEngine() };

		while (i < m_gameResourceCache.Size())
		{
			auto& entry = m_gameResourceCache[i];

			if( entry.expirationTime == EngineTime::ZERO )
			{
				entry.expirationTime = nextExpirationTime;
			}
			else if( now > entry.expirationTime )
			{
				if( entry.resource.GetRefCount() == 1 )
				{
					evictList.PushBack( entry.resource );
					m_gameResourceCache.RemoveAtReorder( i );
				}
				else
				{
					entry.expirationTime = nextExpirationTime;
				}
			}

			i += 1;
		}

		if( !evictList.Empty() )
		{
			job::Builder builder( job::Priority::Latent );
			builder.DispatchJob( "EvictResources", [list = std::move( evictList )]( const job::RunContext& ) {} );
		}

	}

	void ResourceBank::EnableGameCacheLoadingMode(Bool value)
	{
		RED_SCOPE_LOCK(m_gameResourceCacheLock);
		
 		m_gameResourceCacheLoadingMode = false; // ctremblay: HACK. We have 200mb of resource we load that we don't need, spiking memory, causing OOM. Needs investigation.
		
		if( value )
		{
			m_gameResourceCache.Clear(); // ctremblay: HACK. I'm not sure why, but something gets reloaded regardless and destroy memory. flushing cache for now.
		}
	
		return;

		if (m_gameResourceCacheLoadingMode == value)
		{
			return;
		}

		m_gameResourceCacheLoadingMode = value;

		if (m_gameResourceCacheLoadingMode)
		{
			m_gameResourceCacheSizeBeforeLoadingMode = m_gameResourceCache.Size();
		}
		else
		{
			if (m_gameResourceCache.Size() > m_gameResourceCacheSizeBeforeLoadingMode)
			{
				m_gameResourceCache.Resize(m_gameResourceCacheSizeBeforeLoadingMode);
				m_gameResourceCacheSizeBeforeLoadingMode = 0;
			}
		}
	}

	Uint32 ResourceBank::GetNumGameCacheEntries() const
	{
		RED_SCOPE_SHARED_LOCK(m_gameResourceCacheLock);

		return m_gameResourceCache.Size();
	}

	void ResourceBank::ShutdownGameCacheManuallyBecauesRenderingCrashesOtherwise()
	{
		RED_SCOPE_LOCK(m_gameResourceCacheLock);
		m_gameResourceCache.Clear();
		m_gameResourceCacheShutdown = true;
	}

	void ResourceBank::FlushGameCache()
	{
		RED_SCOPE_LOCK( m_gameResourceCacheLock ); // ctremblay: keep under lock as multiple thread might call it, and expect it to be flused when this return
		m_gameResourceCache.Clear();
	}

	red::UniquePtr< ResourceBank > CreateResourceBank(IResourceDepot* depot)
	{
		red::UniquePtr< ResourceBank > bank = red::CreateUniquePtr< ResourceBank >();
		bank->Initialize( depot );
		return bank;
	}
}
