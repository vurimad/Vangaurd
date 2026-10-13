/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "streamedResourceCache.h"
#include "resourceDepot.h"

#include "../../redSystem/include/stopWatch.h"

namespace res
{
	// Use this to select a single dependency from the set of all reverse dependencies. This exists because of instance prefabs and the unholy mess of what resource should be the parent prefab.
	// If we move a node in an instance prefab then we should only update the reverse dependency of which prefab parent has the instance data.
	// Not every prefab parent in the world which has its own different instantiation. Nothing bad will happen other than it can take seconds to update whenever moving anything
	// in the editor...
	//
	// Better would be to just update the correct prefabs in the first place, but a bit easier said than done at the moment.
	// We track dependencies at the resource level only, but here it's a combination of resource and instance data.
	static thread_local res::ResourcePath t_HACK_rootReverseDependencyOverride;
	void RED_REFLECTION_API HACK_SetSingleRootReverseDependencyForInstancedPrefab(const res::ResourcePath& path)
	{
		t_HACK_rootReverseDependencyOverride = path;
	}

	enum class MarkType : Uint8
	{
		Temporary = 0,
		Permanent,
	};

	Bool StreamedResourceCache::Updater::ScanForCircuralDependencies( const ResourcePath& rootPath, red::DynArray< res::ResourcePath >& brokenResources ) const
	{
		Bool circuralDependencyDetected = false;

		red::HashMap< ResourcePath, MarkType > visited{ red::PoolDebug() };

		std::function< void( const ResourcePath& ) > scanFunc;
		scanFunc = [&] ( const ResourcePath& path )
		{
			const auto iter = visited.Find( path );
			if ( iter != visited.End() )
			{
				if ( iter.Value() == MarkType::Temporary )
				{
					circuralDependencyDetected = true;
					RED_LOG_ERROR( "StreamedResourceCache: A cycle has been detected in the streaming cache dependency information. Faulty resource: '%hs'", path.ToDebugString() );
					brokenResources.PushBack( path );
					return;
				}
			}
			else
			{
				visited.Insert( path, MarkType::Temporary );

				if ( CacheEntry* entry = m_cache.TryGetCachedStreamingData_NoLock( path ) )
				{
					for ( const ResourcePath& depPath : entry->m_dependencies )
					{
						scanFunc( depPath );
					}
				}

				visited[ path ] = MarkType::Permanent;
			}
		};

		scanFunc( rootPath );

		return circuralDependencyDetected;
	}

	Bool StreamedResourceCache::Updater::UpdateTopDown( const ResourcePath& rootPath, red::DynArray< res::ResourcePath >& brokenResources ) const
	{
		red::StopWatch stopWatch;

		red::HashMap< ResourcePath, MarkType > visited{ red::PoolDebug() };
		red::DynArray< ResourcePath > toCheck{ red::PoolDebug() };

		std::function< void( const ResourcePath& ) > collectDepFunc;
		collectDepFunc = [&] ( const ResourcePath& path )
		{
			const auto iter = visited.Find( path );
			if ( iter != visited.End() )
			{
				if ( iter.Value() == MarkType::Temporary )
				{
					RED_LOG_ERROR( "StreamedResourceCache: A cycle has been detected in the streaming cache dependency information. Faulty resource: '%hs'", path.ToDebugString() );
					toCheck.PushBack( path );
					return;
				}
			}
			else
			{
				visited.Insert( path, MarkType::Temporary );

				if ( CacheEntry* entry = m_cache.TryGetCachedStreamingData_NoLock( path ) )
				{
					toCheck.PushBack( path );
					for ( const ResourcePath& depPath : entry->m_dependencies )
					{
						collectDepFunc( depPath );
					}
				}

				visited[path] = MarkType::Permanent;
			}
		};

		red::ScopedLock< CacheLock > lock( m_cache.m_cacheLock );

		red::DynArray< ResourcePath > toUpdate{ red::PoolDebug() };

		collectDepFunc( rootPath );

		red::alg::RemoveDuplicates( toCheck );

		// iterate from the bottom
		for ( auto iter = toCheck.RBegin(); iter != toCheck.REnd(); ++iter )
		{
			const ResourcePath& path = *iter;
			CacheEntry* entry = m_cache.TryGetCachedStreamingData_NoLock( path );
			RED_FATAL_ASSERT( entry );

			if( entry->m_isDirty )
			{
				const Uint64 fileTime = m_cache.m_depot->GetResourceTimestamp( path );
				if(fileTime == 0)
				{
					RED_LOG_ERROR( "StreamedResourceCache: Resource '%hs' does not exist", path.ToDebugString() ); 
					entry->m_isDirty = false;
				}
				else if(entry->m_fileTimeUTC != fileTime)
				{
					// TODO possible optimization:
					// if we are refreshing data, old dependencies could be omitted, because they also may be not valid

					// file is outdated - update all the data
					m_cache.LoadStreamingData_NoLock( path, CName(), entry );
					toUpdate.PushBack( path );
				}
				else
				{
					entry->m_isDirty = false;
				}
			}	
		}

		UpdateEntries( toUpdate );

		RED_LOG_INFO( "StreamedResourceCache: Updating resource '%hs' top-down took %f ms", rootPath.ToDebugString(), stopWatch.GetDeltaMS() );
		return !ScanForCircuralDependencies( rootPath, brokenResources );
	}

	Uint32 StreamedResourceCache::Updater::UpdateEntries( const red::ArraySpan< const ResourcePath >& paths, Bool updateOnlyLoadedResources ) const
	{
		RED_FATAL_ASSERT(!t_HACK_rootReverseDependencyOverride.IsValid() || paths.Size() <= 1, "This hack is really meant for 1 (or zero) paths, otherwise each path may need its own override");

		red::StopWatch sw;

		red::HashMap< ResourcePath, MarkType > visitedEntries{ red::PoolDebug() };	// list of visited entries
		red::DynArray< ResourcePath > toUpdate{ red::PoolDebug() };					// list of entries that require updating (in reversed topological order)
		Uint32 counter = 0;										// number of updated entries

		std::function<void( const ResourcePath&, const Bool isRoot )> exploreEntry;
		exploreEntry = [&] ( const ResourcePath& path, const Bool isRoot )
		{
			const auto iter = visitedEntries.Find( path );
			if ( iter != visitedEntries.End() ) // entry has been visited
			{
				if ( iter.Value() == MarkType::Temporary )
				{
					RED_LOG_ERROR( "StreamedResourceCache: A cycle has been detected in the streaming cache dependency information" );
					toUpdate.PushBack( path );
					return;
				}
			}
			else // entry has not been visited
			{
				visitedEntries.Insert( path, MarkType::Temporary );

				if ( CacheEntry* entry = m_cache.TryGetCachedStreamingData_NoLock( path ) )
				{
					counter++;

					if (isRoot && t_HACK_rootReverseDependencyOverride.IsValid())
					{
						if (entry->m_revDependencies.Exist(t_HACK_rootReverseDependencyOverride))
						{
							exploreEntry( t_HACK_rootReverseDependencyOverride, false );
						}
					}
					else
					{
						for ( const ResourcePath& revDependency : entry->m_revDependencies )
						{
							exploreEntry( revDependency, false );
						}
					}
				}

				visitedEntries[path] = MarkType::Permanent;

				if ( !isRoot )
				{
					toUpdate.PushBack( path );
				}
			}
		};

		for ( const ResourcePath& path : paths )
		{
			exploreEntry( path, true );
		}

		for( const ResourcePath& path : paths )
		{
			toUpdate.PushBack( path );
		}

		for ( auto iter = toUpdate.RBegin(); iter != toUpdate.REnd(); ++iter )
		{
			const ResourcePath& path = *iter;
			if ( CacheEntry* entry = m_cache.TryGetCachedStreamingData_NoLock( path ) )
			{
				CacheEntry* updatedEntry = m_cache.LoadStreamingData_NoLock( path, CName(), entry, updateOnlyLoadedResources );
				// TODO no need to update parents if streaming data did not get changed
			}
		}

		RED_LOG_INFO( "StreamedResourceCache: Updating %u entries took %f ms", counter, sw.GetDeltaMS() );

		return counter;
	}

} // res