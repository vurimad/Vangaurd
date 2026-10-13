/**
 * Copyright (c) 2007-2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#if defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )
#include "../../../common/redSystem/include/redThreadsAtomic.h"
#include "../../../common/redMemory/include/unsafeDynamicLinearAllocator.h"
#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redCore/include/absolutePath.h"
#endif

namespace res
{
namespace prv
{

#if defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

//------------------------------------------------------------------------------

// Stores the path string and computed hash inside the cache
// The path string is stored in another buffer
class ResourcePathCacheItem
{
public:
	ResourcePathCacheItem( const Uint64 hash, const red::StringView path );
	~ResourcePathCacheItem();

	String ToString() const;
	red::StringView ToStringView() const;
	Uint64 GetHash() const;

	const char* ToDebugString() const;

private:
	Uint64 m_hash;
	red::StringView m_path;
};

static_assert( sizeof( ResourcePathCacheItem ) == 24, "" );

//------------------------------------------------------------------------------

// Manages the string data and metadata of the resource paths
// This cache only grows and never gets released, it does not exist in final
class ResourcePathCache
{
public:
	ResourcePathCache();
	~ResourcePathCache();

	ResourcePathCache( const ResourcePathCache& ) = delete;
	ResourcePathCache( ResourcePathCache&& ) = delete;

	ResourcePathCache& operator=( const ResourcePathCache& ) = delete;
	ResourcePathCache& operator=( ResourcePathCache&& ) = delete;

	// Finds an existing or creates a new path item entry and returns it
	ResourcePathCacheItem* FindOrCreateItem( const red::StringView& sanitizedPath );

	// Only finds an existing item, returns nullptr on failure
	ResourcePathCacheItem* FindItem( const Uint64 hash ) const;

	// Load a cache that has been serialised to disk
	RED_REFLECTION_API bool Load( const red::AbsolutePath& cachePath );

	// Save the contents of the cache to a file on disk
	RED_REFLECTION_API bool Save( const red::AbsolutePath& cachePath ) const;

	// Get the default filename of the cache on disk
	RED_REFLECTION_API static const char* GetDefaultFilename();

private:
	ResourcePathCacheItem* CreateItem( Uint64 hash, const red::StringView& sanitizedPath );

	struct SimpleHashFunc
	{
		static RED_INLINE red::THash32 GetHash( const Uint64 k ) { return static_cast<Uint32>( k ); }
	};

	using PathMapHashPolicy = red::HashPolicyDefaultEqual< SimpleHashFunc, Uint64 >;
	typedef red::HashMap< Uint64, ResourcePathCacheItem*, PathMapHashPolicy > PathMap;

	// Allocates memory for this cache - note never deallocated
	red::memory::UnsafeDynamicLinearAllocator m_itemAllocator;
	red::memory::UnsafeDynamicLinearAllocator m_stringAllocator;

	mutable red::RWSpinLock m_pathLock;
	PathMap m_pathMap;
};

// For use only with ResourcePath
RED_REFLECTION_API ResourcePathCache& GetResourcePathCache();

//------------------------------------------------------------------------------

RED_INLINE ResourcePathCacheItem::ResourcePathCacheItem( const Uint64 hash, const red::StringView path )
	: m_hash( hash )
	, m_path( path )
{
}

RED_INLINE ResourcePathCacheItem::~ResourcePathCacheItem() = default;

RED_INLINE String ResourcePathCacheItem::ToString() const
{
	return m_path.ToString();
}

RED_INLINE red::StringView ResourcePathCacheItem::ToStringView() const
{
	return m_path;
}

RED_INLINE Uint64 ResourcePathCacheItem::GetHash() const
{
	return m_hash;
}

RED_INLINE const char* ResourcePathCacheItem::ToDebugString() const
{
	// In this case this is safe to do because the string data held in the
	// ResourcePathCache is guaranteed to be null terminated.
	return m_path.Data();
}

//------------------------------------------------------------------------------

#endif // defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

} // prv
} // res
