/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "streamedResource.h"
#include "streamedResourceDataExtractor.h"
#include "mathBox.h"
#include "../../redCore/include/absolutePath.h"

namespace res
{

class IResourceDepot;

typedef red::UniquePtr< IStreamedResourceDataExtractor > IStreamedResourceDataExtractorPtr;

class StreamedResourceCache
{
public:

	// helper class for performing complex updating that takes dependencies into account
	struct Updater
	{
	public:
		Updater( StreamedResourceCache& cache )
			: m_cache( cache )
		{ }

		Bool ScanForCircuralDependencies( const ResourcePath& rootPath, red::DynArray< ResourcePath >& brokenResources ) const;

		// Scan entries recursively and update all the children
		// Note: this is very costly operation when requested on a resource being high in hierarchy
		Bool UpdateTopDown( const ResourcePath& rootPath, red::DynArray< res::ResourcePath >& brokenResources ) const;

		// Update an entry and its parents (bottom-up)
		// Returns number of cache entries updated
		Uint32 UpdateEntries( const red::ArraySpan< const ResourcePath >& paths, Bool updateOnlyLoadedResources = false ) const;

	private:
		StreamedResourceCache& m_cache;
	};

	// get the streaming data for the resource
	// if the resource does not exist or could not be loaded the zero is returned
	StreamingData GetStreamingData( const res::ResourcePath& path ) const;
	StreamingData GetStreamingData( const res::ResourcePath& path, CName optionalName ) const;

	red::DynArray< res::ResourcePath > GetReverseDependencies(const res::ResourcePath& path) const;

	// Update streaming data for a given resource (invalidate cache entry for given path).
	// Note: this will update all parents recursively.
	void UpdateStreamingData( const ResourcePath& path );

	explicit StreamedResourceCache( IResourceDepot* depot );
	StreamedResourceCache( const StreamedResourceCache& ) = delete;
	StreamedResourceCache& operator=( const StreamedResourceCache& ) = delete;

	Bool Load( const red::StringView& relativePath );
	Bool Save();

	void InitializeDataExtractors();

	void ClearCache();
	
	void RetryOnFileNotFound();

	void ExportMeshLocalBounds(red::HashMap<Uint64, Box>& outBounds) const;

private:
	static const Uint32 c_fileMagic;
	static const Uint32 c_cacheVersion;

	struct AppearanceKey
	{
		AppearanceKey();
		AppearanceKey(const ResourcePath& resPath, CName appearanceName);

		Uint32 CalcHash() const;
		bool operator==( const AppearanceKey& other ) const;

		ResourcePath path;
		CName name;
	};

	struct CacheEntry
	{
		CacheEntry()
			: m_fileTimeUTC( 0 )
			, m_isDirty( true )
			, m_dependencies( red::PoolBackend() )
			, m_revDependencies( red::PoolBackend() )
		{}

		Uint64 m_fileTimeUTC;		// timestamp of the file at the moment of checking
		StreamingData m_data;		// streaming data
		Bool m_isDirty;

		red::DynArray< ResourcePath > m_dependencies{ red::PoolBackend() };	// list of resources that this entry depends on
		red::DynArray< ResourcePath > m_revDependencies{ red::PoolBackend() };	// list of resources that are dependent on this entry
	};

	using EntriesMap = red::HashMap< ResourcePath, CacheEntry >;
	using AppearanceMap = red::HashMap< AppearanceKey, CacheEntry >;
	using ResourcePathsSet = red::HashSet< ResourcePath >;

	Bool LoadResourcePaths_NoLock( IFile& file ) const;

	// Save strings for all resource paths in use
	void SaveResourcePaths_NoLock( IFile& file ) const;

	// Decode resource path from streaming cache file
	ResourcePath LoadResourcePath_NoLock( IFile& file ) const;

	// Save a single cache entry into a file
	Bool SaveEntry_NoLock( IFile& file, const CacheEntry& entry ) const;

	// Load a single cache entry from a file
	Bool LoadEntry_NoLock( IFile& file, ResourcePath& outResourcePath, CacheEntry& outEntry );

	// Save all cache entries from a file
	Bool SaveEntriesMap_NoLock( IFile& file ) const;

	// Load all cache entries from a file
	Bool LoadEntriesMap_NoLock( IFile& file );

	template< typename TDataProcessingFunc >
	void GetValidCacheData(const res::ResourcePath& path, CName optionalName, const TDataProcessingFunc& func ) const;

	void ClearCache_NoLock();
	CacheEntry* TryGetCachedStreamingData_NoLock( const res::ResourcePath& path ) const;
	CacheEntry* TryGetCachedStreamingData_NoLock( const res::ResourcePath& path, CName optionalName ) const;
	CacheEntry* LoadStreamingData_NoLock( const res::ResourcePath& path, CName optionalName, CacheEntry* optUpdateEntry, bool updateOnlyLoadedResource = false ) const;
	CacheEntry* CreateErrorEntry_NoLock( const res::ResourcePath& path, CacheEntry* optUpdateEntry ) const;
	CacheEntry* CreateDirtyEntry_NoLock( const res::ResourcePath& path, CacheEntry* optUpdateEntry ) const;

	// Extract streaming data from a given resource and update the cache entry. Returns true if the entry was updated.
	Bool UpdateCacheEntryStreamingData_NoLock( const ResourcePath& path, CName optionalName, const res::StreamedResource& resData, CacheEntry& entry, const IStreamedResourceDataExtractor* extractor ) const;

	// Update cache entry dependencies information (both forward and reverse)
	Bool UpdateCacheEntryDependencyData_NoLock( const ResourcePath& path, const res::StreamedResource& resData, CacheEntry& entry, const IStreamedResourceDataExtractor* extractor ) const;

	// Find a compatible streaming data extractor for given resource extension
	IStreamedResourceDataExtractor* FindDataExtractor( const res::ResourcePath& path ) const;

	void LoadAutoHideBoostFile();

	mutable EntriesMap m_entriesMap; // cached entries
	mutable AppearanceMap m_appearanceDictionary;

	// TODO move to redThreads when we're sure it works (write unit tests)
	struct ReentrantRWLock : red::NonCopyable
	{
		RED_INLINE ReentrantRWLock()
			: m_exclusiveThreadID( 0 )
			, m_recursionDepth( 0 )
		{}

		void Acquire();
		void Release();
		void AcquireShared();
		void ReleaseShared();

		red::Atomic< Uint32 > m_exclusiveThreadID;
		Int32 m_recursionDepth;
		red::RWLock m_rwLock;
	};

	typedef ReentrantRWLock CacheLock;
	mutable CacheLock m_cacheLock;

	red::DynArray< IStreamedResourceDataExtractorPtr > m_extractors{ red::PoolBackend() };

	// the entries in memory were update, so the cache file needs to be written to disk
	mutable red::Atomic< Bool > m_cacheIsDirty;

	// absolute path to the streaming cache file
	red::AbsolutePath m_cacheFilePath;

	IResourceDepot* m_depot; // resource depot is used to determine the actual file timestamps

	bool m_retryOnFileNotFound;
};

}

