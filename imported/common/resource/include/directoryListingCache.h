/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redMemory/include/linearAllocator.h"

namespace res
{
namespace prv
{

class RED_RESOURCE_API DirectoryListingCache
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	DirectoryListingCache();
	~DirectoryListingCache();

	void UpdateTree( const red::AbsolutePath& basePath );

	// Returns the default filename used to load and save a part of the this cache to disk for future use
	static const char* GetDefaultFilename();

	bool Load( const red::AbsolutePath& cachePath );
	bool Save( const red::AbsolutePath& cachePath );
	void Clear();

	void LookupDirectories( const red::AbsolutePath& searchPathPattern, red::DynArray< String >& output );
	void LookupFiles( const red::AbsolutePath& searchPathPattern, const char* pattern, red::DynArray< String >& output );

	red::DynArray< String > Debug_GetEntireTree();

private:

	struct DirectoryEntry
	{
		Uint64 timestamp;
		red::IndexRange directoryIndexes;
		red::IndexRange fileIndexes;

		DirectoryEntry();
	};

	using DirectoryMap = red::HashMap< red::AbsolutePath, DirectoryEntry >;

	DirectoryMap::const_iterator FindEntry( const red::AbsolutePath& path );
	void CreateOutputStrings( const red::IndexRange& range, const red::StringView pattern, red::DynArray< red::String >& output );
	void UpdateEntry( red::AbsolutePath path, DirectoryEntry& entry );
	red::StringView SaveStringData( red::StringView str );

	DirectoryMap m_directories;
	red::DynArray< red::StringView > m_entryStrings;
	red::memory::DynamicLinearAllocator m_stringStorage;
	mutable red::Mutex m_guard;
};

} // prv
} // res
