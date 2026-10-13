/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redContainers/include/dynArray.h"
#include "../../../common/redContainers/include/set.h"

namespace res
{
class IBackendResourceDepot;
}

namespace job
{
class Builder;
class Counter;
}

namespace dependency
{
namespace prv
{
enum class ScanDependencyType : Uint8;
enum class ScanDependencyLocation : Uint8;
enum class ScanResult : Uint8;
}
}

namespace resdb
{
class FileSystemWatcher;
using FileSystemWatcherPtr = red::UniquePtr< FileSystemWatcher >;

// Describes a link between two resources
// The from resource must always, where as the to resource may or may not exist
// The type determines if it's a hard link (TResRef or similar) which is loaded
// when the from resource is loaded or a soft link (TResAsyncRef or similar) which
// is not automatically loaded
struct Link
{
	using Type = dependency::prv::ScanDependencyType;
	using Location = dependency::prv::ScanDependencyLocation;

	res::ResourcePath from;
	res::ResourcePath to;
	Type type;
	Location location;
	bool inplace;
};

using Links = red::ArraySpan< const Link >;

// Describes a resource which must exist on disk
struct Resource
{
	using Status = dependency::prv::ScanResult;

	res::ResourcePath path;
	Uint64 timestamp;
	Status status;

	Uint32 totalSize;
	Uint32 objectSize;
	Uint32 bufferSize;

	bool isLink;

	// Helper function to create a blank resource with default values
	static Resource CreateBlank( const res::ResourcePath& path );
};

BACKEND_DATA_API bool SortByPath( const Resource& left, const Resource& right );
BACKEND_DATA_API bool SortByTimestamp( const Resource& left, const Resource& right );
BACKEND_DATA_API bool SortByStatus( const Resource& left, const Resource& right );
BACKEND_DATA_API bool SortByTotalSize( const Resource& left, const Resource& right );
BACKEND_DATA_API bool SortByObjectSize( const Resource& left, const Resource& right );
BACKEND_DATA_API bool SortByBufferSize( const Resource& left, const Resource& right );

BACKEND_DATA_API bool operator==( const Resource& left, const Resource& right );
BACKEND_DATA_API bool operator==( const res::ResourcePath& path, const Resource& resource );
BACKEND_DATA_API bool operator==( const Resource& resource, const res::ResourcePath& path );
BACKEND_DATA_API bool operator<( const Resource& left, const Resource& right );
BACKEND_DATA_API bool operator<( const res::ResourcePath& path, const Resource& resource );
BACKEND_DATA_API bool operator<( const Resource& resource, const res::ResourcePath& path );

using Resources = red::DynArray< const Resource* >;
using ResourcePaths = red::DynArray< res::ResourcePath >;

class RecursiveLookup;

// Database that stores resource information and the linkages between resources
// which can be serialised to and from a cache on disk.
// Provides resource information only for resources which exist on disk, but 
// links can refer to resources which do not exist
// Forward links are dependencies of a given resource
class BACKEND_DATA_API Database
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	Database();
	Database( const Database& ) = delete;
	Database( Database&& other );
	~Database();

	Database& operator=( const Database& ) = delete;
	Database& operator=( Database&& other );

	static const char* GetDefaultFilename();

	void Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath );
	void Shutdown();
	void Update();

	void InitFileSystemWatcher();
	void ShutFileSystemWatcher();
	// Durable transactions publish through the base filesystem and therefore do
	// not necessarily traverse the watcher wrapper. Queue canonical paths only
	// after their Committed journal is durable, then call Update/await its counter
	// before admitting dependent build work.
	void EnqueueCommittedPaths( red::ArraySpan< const res::ResourcePath > paths );

	void Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter );
	void Initialise_NoUpdate( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter );
	void Update( job::Counter& counter );

	// Lookup a single resource, will return nullptr if not found on disk
	const Resource* LookupResource( const res::ResourcePath& path ) const;

	// Lookup resources given a set of paths, the returned list may be smaller than
	// the number of input paths as resources that are not on disk will not be returned
	Resources LookupResources( red::ArraySpan< const res::ResourcePath > paths ) const;
	Resources LookupFromResources( Links links ) const;
	Resources LookupToResources( Links links ) const;

	// Find forward links for a resource, the 'from' field will be the given resource
	// These are the dependencies of that resource which it references
	Links GetForwardLinks( const res::ResourcePath& path ) const;

	// Find backward links for a resource, the 'to' field will be the given resource
	// These are the resources dependent on that resource
	Links GetBackwardLinks( const res::ResourcePath& path ) const;

	// Finds forward links for a resource and follows any link files to find the final non-link resource.
	ResourcePaths GetForwardResourcePaths( const res::ResourcePath& path ) const;

	// Find forward references for a resource recursively, caching intermediate results internally
	const red::Set< res::ResourcePath >& GetRecursiveForwardPaths( const res::ResourcePath& path );

	template < typename FilterFunc >
	Resources FilterResources( FilterFunc&& filterFunction );

	Links GetAllForwardLinks() const;

	Links GetAllBackwardLinks() const;

	// Gets all the resources that exist on disk
	red::ArraySpan< const Resource > GetResources() const;

	// Gets all the resource paths that exist on disk
	const red::DynArray< res::ResourcePath >& GetResourcePaths() const;

	// Gets the resource depot used by this database
	res::IBackendResourceDepot& GetResourceDepot() const;

	bool HasChanged() const;

	// Helper functions to extract the ResourcePaths from the given links
	static ResourcePaths ExtractFromPaths( Links links );
	static ResourcePaths ExtractToPaths( Links links );

private:
	bool Load();
	bool Save();
	void Clear();

	void UpdateScan( job::Builder& builder );
	void FullScan( job::Builder& builder );

	struct FullScanState;
	using FullScanStatePtr = red::SharedPtr< FullScanState, red::PoolBackend >;
	void ComputeResources( FullScanStatePtr state );
	void ScanResource( FullScanStatePtr state, Uint32 index );
	void ApplyResults( FullScanStatePtr state );

	struct IncrementalScanState;
	using IncrementalScanStatePtr = red::SharedPtr< IncrementalScanState, red::PoolBackend >;
	void ComputeDifferences( IncrementalScanStatePtr state );
	void ScanResource( IncrementalScanStatePtr state, Uint32 index );
	void ApplyResults( IncrementalScanStatePtr state );

	res::ResourcePath ResolveResourcePath( const res::ResourcePath& path ) const;

	void InitialiseRecursiveLookup();
	const red::Set< res::ResourcePath >& CollectRecursive( const res::ResourcePath& path );

	res::IBackendResourceDepot* m_depot;
	red::AbsolutePath m_cachePath;

	FileSystemWatcherPtr m_watcher;

	red::DynArray< Resource > m_resources;
	red::DynArray< Link > m_forwardLinks;
	red::DynArray< Link > m_backwardLinks;
	red::DynArray< res::ResourcePath > m_resourcePaths;

	red::HashSet< res::ResourcePath > m_recursiveVisited;
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > m_recursiveForwardDeps;
	red::Mutex m_recursiveGuard;

	bool m_changed;
};

//------------------------------------------------------------------------------

template < typename FilterFunc >
Resources Database::FilterResources( FilterFunc&& filterFunction )
{
	Resources result;
	for ( const auto& resource : m_resources )
	{
		if ( filterFunction( resource ) )
		{
			result.PushBack( &resource );
		}
	}
	return result;
}

//------------------------------------------------------------------------------

RED_INLINE bool SortByPath( const Resource& left, const Resource& right )
{
	return left.path < right.path;
}

RED_INLINE bool SortByTimestamp( const Resource& left, const Resource& right )
{
	return left.timestamp < right.timestamp;
}

RED_INLINE bool SortByStatus( const Resource& left, const Resource& right )
{
	return left.status < right.status;
}

RED_INLINE bool SortByTotalSize( const Resource& left, const Resource& right )
{
	return left.totalSize > right.totalSize;
}

RED_INLINE bool SortByObjectSize( const Resource& left, const Resource& right )
{
	return left.objectSize > right.objectSize;
}

RED_INLINE bool SortByBufferSize( const Resource& left, const Resource& right )
{
	return left.bufferSize > right.bufferSize;
}

RED_INLINE bool operator==( const Resource& left, const Resource& right )
{
	return left.path == right.path;
}

RED_INLINE bool operator==( const res::ResourcePath& path, const Resource& resource )
{
	return path == resource.path;
}

RED_INLINE bool operator==( const Resource& resource, const res::ResourcePath& path )
{
	return resource.path == path;
}

RED_INLINE bool operator<( const Resource& left, const Resource& right )
{
	return left.path < right.path;
}

RED_INLINE bool operator<( const res::ResourcePath& path, const Resource& resource )
{
	return path < resource.path;
}

RED_INLINE bool operator<( const Resource& resource, const res::ResourcePath& path )
{
	return resource.path < path;
}

RED_INLINE bool operator==( const Link& left, const Link& right )
{
	return left.from == right.from && left.to == right.to && left.type == right.type && left.location == right.location && left.inplace == right.inplace;
}

namespace prv
{
// Helper function for sorting the details of a Link once the to/from have been evaluated
RED_FORCE_INLINE bool SortDetails( const Link& left, const Link& right )
{
	if ( left.location == right.location )
	{
		if ( left.type == right.type )
		{
			return left.inplace < right.inplace;
		}
		return left.type < right.type;
	}
	return left.location < right.location;
}

} // prv

RED_INLINE bool SortByFrom( const Link& left, const Link& right )
{
	if ( left.from == right.from )
	{
		if ( left.to == right.to )
		{
			return prv::SortDetails( left, right );
		}
		return left.to < right.to;
	}
	return left.from < right.from;
}

RED_INLINE bool SortByTo( const Link& left, const Link& right )
{
	if ( left.to == right.to )
	{
		if ( left.from == right.from )
		{
			return prv::SortDetails( left, right );
		}
		return left.from < right.from;
	}
	return left.to < right.to;
}

} // resdb
