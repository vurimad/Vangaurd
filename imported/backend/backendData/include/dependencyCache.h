/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyTypes.h"

#include "../../../common/redMemory/include/linearAllocator.h"

namespace dependency
{

// Manages a cache of dependency information that has been calculated at runtime
// This is so that reading the same dependency information repeatedly does not result
// in loading and hashing the entire file multiple times
class BACKEND_DATA_API Cache
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	explicit Cache( data::ResourceDepot* depot );
	~Cache();

	// Perform a look up of the resource given by the path member of info in the specified depot
	// This function always adds or updates the entry in the database, if the timestamp on
	// disk does not match what is in the cache then the file is loaded and the hash is computed
	// Returns true if the file exists and false if it does not, sets the hash and timestamp fields
	// in the info structure to the most up to date values
	bool Lookup( FileInfo& info );


	//
	// Advanced Interface
	//

	// Returns the default filename used to load and save a part of the this cache to disk for future use
	static const char* GetDefaultFilename();

	// Load part of this cache from a file on disk specified by cachePath which corresponds to 
	// the provided depot. Merges data with what already exists in this cache
	// Returns true on success and false on failure
	bool Load( const red::AbsolutePath& cachePath );

	// Saves the cache entries that correspond to a particular depot to the specified file on disk
	// Returns true on success and false on failure
	bool Save( const red::AbsolutePath& cachePath );

	// Checks to see if the entry is in the cache and returns information about it if found
	// Returns true if the entry has been found in the cache and fills in the information,
	// or returns false if the entry was not found and leaves the info unchanged
	bool Find( FileInfo& info ) const;

	// Sets the entry in the cache to the specified values
	void Set( const FileInfo& info );

	// Returns the depot used for this cache
	data::ResourceDepot* GetDepot() const;

	// Returns the list of file entries stored in the cache to be used in diagnostic debugging commandlets
	red::DynArray< const FileInfo* > Interal_GetEntries() const;

private:
	using CacheKey = res::ResourcePath;
	using BasicInfoCacheMap = red::HashMap< CacheKey, FileInfo* >;

	bool ComputeFileHash( const res::ResourcePath& path, dependency::Hash& hash );
	
	data::ResourceDepot* m_depot;
	BasicInfoCacheMap m_basicInfoCache;
	red::memory::DynamicLinearAllocator m_basicInfoStorage;
	mutable red::RWSpinLock m_guard;

	red::AbsolutePath m_debugCachePath;
};

} // dependency
