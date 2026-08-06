/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "streamedResourceCache.h"
#include "streamedResourceDataExtractor.h"
#include "resourceLoader.h"
#include "resourceDepot.h"
#include "serializationAsyncSource.h"
#include "serializationBinaryUtilities.h"
#include "containersSerialization.h"

#include "../../redFileSystem/include/fileVersionList.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/system.h"
#include "../../redSystem/include/stopWatch.h"
#include "../../redSystem/include/threads.h"

//#define EXPORT_MESH_LOCAL_BOUNDS_CACHE

namespace res
{
	const Uint32 StreamedResourceCache::c_fileMagic = 'MRTS'; // STRM
	const Uint32 StreamedResourceCache::c_cacheVersion = 150;
	const char * c_autohide_override = "config\\autohide_boost.csv";

	static_assert( sizeof( StreamingData ) <= 128, "Streaming data structure size increased" );

	static red::AbsolutePath GetStreamingCacheFile( const red::StringView& fileName )
	{
		return GFileManager->GetCacheDirectory().AddDirPath( "streaming" ).AddFilePath( fileName );
	}

	StreamedResourceCache::AppearanceKey::AppearanceKey() = default;
	StreamedResourceCache::AppearanceKey::AppearanceKey(const ResourcePath& resPath, CName appearanceName)
		: path( resPath )
		, name( appearanceName )
	{}

	Uint32 StreamedResourceCache::AppearanceKey::CalcHash() const
	{
		return static_cast< Uint32 >( path.GetHash() ^ name.GetHash() );
	}

	bool StreamedResourceCache::AppearanceKey::operator==(const AppearanceKey& other) const
	{
		return path == other.path && name == other.name;
	}

	StreamedResourceCache::StreamedResourceCache( IResourceDepot* depot )
		: m_depot( depot )
		, m_cacheIsDirty( false )
		, m_entriesMap( red::PoolBackend() )
		, m_appearanceDictionary(red::PoolBackend())
		, m_retryOnFileNotFound( false )
	{
	}

	void StreamedResourceCache::InitializeDataExtractors()
	{
		if( !m_extractors.Empty() )
			return;

		red::DynArray< const rtti::ClassType* > streamingDataExtractorTypes{ red::PoolBackend() };
		rtti::ITypeSystem::GetInstance().EnumClasses( IStreamedResourceDataExtractor::GetStaticClass(), streamingDataExtractorTypes );

		for ( const rtti::ClassType* type : streamingDataExtractorTypes )
		{
			IStreamedResourceDataExtractor* object = type->CreateObject< IStreamedResourceDataExtractor >();
			m_extractors.PushBack( red::UniquePtr< IStreamedResourceDataExtractor >( object ) );
		}
	}

	void StreamedResourceCache::ClearCache()
	{
		red::ScopedLock< CacheLock > lock( m_cacheLock );

		ClearCache_NoLock();
	}

	void StreamedResourceCache::ClearCache_NoLock()
	{
		m_entriesMap.Clear();
	}

	//////////////////////////////////////////////////////////////////////////

	Bool StreamedResourceCache::Save()
	{
		if ( !m_cacheIsDirty.GetValue() )
		{
			// cache is clean - no need to save to disk
			return true;
		}

		red::ScopedLock< CacheLock > lock( m_cacheLock );
		red::StopWatch timer;

		auto writer = GFileManager->CreateFileWriter( m_cacheFilePath, FOF_AbsolutePath | FOF_Buffered );
		if ( !writer )
		{
			RED_LOG_ERROR( "StreamedResourceCache: failed to open '%hs' for saving", m_cacheFilePath.AsChar() );
			return false;
		}

		Uint32 magic = c_fileMagic;
		Uint32 cacheVersion = c_cacheVersion;
		Uint32 resaveVersion = VER_CURRENT;
		*writer << magic;
		*writer << cacheVersion;
		*writer << resaveVersion;

		SaveResourcePaths_NoLock( *writer );

		if ( !SaveEntriesMap_NoLock( *writer ) )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Failed to load cache entries" );
			return false;
		}

		RED_LOG_INFO( "StreamedResourceCache: saving %u entries took %1.2f ms", m_entriesMap.Size(), timer.GetDeltaMS() );

		m_cacheIsDirty.SetValue( false );
		return true;
	}

	void StreamedResourceCache::SaveResourcePaths_NoLock( IFile& file ) const
	{
		ResourcePathsSet set{ red::PoolBackend() };

		for ( const auto& pair : m_entriesMap )
		{
			const CacheEntry& entry = pair.Value();

			const ResourcePath& path = pair.Key();
			RED_FATAL_ASSERT( path.IsValid() );
			set.Insert( path );

			// map strings for entry dependencies
			for ( const ResourcePath& depPath : entry.m_dependencies )
			{
				RED_FATAL_ASSERT( depPath.IsValid() );
				set.Insert( depPath );
			}

			// map strings for entry reverse dependencies
			for ( const ResourcePath& revDepPath : entry.m_revDependencies )
			{
				RED_FATAL_ASSERT( revDepPath.IsValid() );
				set.Insert( revDepPath );
			}
		}

		for (const auto& pair : m_appearanceDictionary)
		{
			const CacheEntry& entry = pair.Value();

			const ResourcePath& path = pair.Key().path;
			RED_FATAL_ASSERT(path.IsValid());
			set.Insert(path);

			// map strings for entry dependencies
			for (const ResourcePath& depPath : entry.m_dependencies)
			{
				RED_FATAL_ASSERT(depPath.IsValid());
				set.Insert(depPath);
			}

			// map strings for entry reverse dependencies
			for (const ResourcePath& revDepPath : entry.m_revDependencies)
			{
				RED_FATAL_ASSERT(revDepPath.IsValid());
				set.Insert(revDepPath);
			}
		}

		red::DynArray< char > stringsBuffer{ red::PoolBackend() };
		for ( const ResourcePath& path : set )
		{
			const red::StringView str = path.ToStringView();
			const Uint32 index = stringsBuffer.Size();
			const Uint32 len = str.Length();
			RED_FATAL_ASSERT( len > 0 );

			stringsBuffer.Grow( len + 1 );
			char* ptr = &stringsBuffer[ index ];
			red::Strcpy( ptr, str.Data(), len + 1 );
		}

		file << BulkSerialization( stringsBuffer );
	}

	Bool StreamedResourceCache::SaveEntriesMap_NoLock( IFile& file ) const
	{
		Uint32 numEntries = m_entriesMap.Size();
		file << numEntries;

		for ( const auto& entryPair : m_entriesMap )
		{
			const ResourcePath& resourcePath = entryPair.Key();
			const CacheEntry& entry = entryPair.Value();
		
			Uint64 hash = resourcePath.GetHash();
			file << hash;

			if( !SaveEntry_NoLock( file, entry ) )
			{
				return false;
			}	
		}

		Uint32 appearanceCount = m_appearanceDictionary.Size();
		file << appearanceCount;

		for (const auto appearancePair : m_appearanceDictionary)
		{
			const AppearanceKey& key = appearancePair.Key();
			const CacheEntry& entry = appearancePair.Value();
			Uint64 resourceHash = key.path.GetHash();
			Uint64 nameHash = key.name.GetHash();

			file << resourceHash;
			file << nameHash;

			if (!SaveEntry_NoLock(file, entry))
			{
				return false;
			}
		}

		return true;
	}

	Bool StreamedResourceCache::SaveEntry_NoLock( IFile& file, const CacheEntry& entry ) const
	{
		// HACK for our wonderful, const-correct serialization
		CacheEntry& writableEntry = const_cast<CacheEntry&>( entry );

		file << writableEntry.m_fileTimeUTC;
		file.Serialize( &writableEntry.m_data, sizeof( StreamingData ) );

		const auto saveDependencies = [&file] ( const red::DynArray< ResourcePath >& deps )
		{
			Uint32 depsNum = deps.Size();
			file << depsNum;

			for ( const ResourcePath& resPath : deps )
			{
				Uint64 hash = resPath.GetHash();
				file << hash;
			}
		};

		saveDependencies( entry.m_dependencies );
		saveDependencies( entry.m_revDependencies );

		return true;
	}

	//////////////////////////////////////////////////////////////////////////

	Bool StreamedResourceCache::LoadResourcePaths_NoLock( IFile& file ) const
	{
		red::DynArray< char > stringsBuffer{ red::PoolBackend() };
		file << BulkSerialization( stringsBuffer );

		size_t offset = 0;
		while ( offset < stringsBuffer.Size() )
		{
			const size_t len = red::Strlen( stringsBuffer.TypedData() + offset );

			if ( len > 260 || offset + len + 1 > stringsBuffer.Size() )
			{
				return false;
			}

			// populate resource paths cache
			res::ResourcePath::Build( red::StringView( stringsBuffer.TypedData() + offset, len ) );

			offset += len + 1;
		}

		return true;
	}

	ResourcePath StreamedResourceCache::LoadResourcePath_NoLock( IFile& file ) const
	{
		Uint64 hash = 0;
		file << hash;
		return ResourcePath::Build( hash );
	}

	Bool StreamedResourceCache::LoadEntriesMap_NoLock( IFile& file )
	{
		Uint32 numEntries = 0;
		file << numEntries;

		m_entriesMap.Reserve( numEntries );

		for ( Uint32 i = 0; i < numEntries; ++i )
		{
			ResourcePath resourcePath;
			CacheEntry entry;

			if ( !LoadEntry_NoLock( file, resourcePath, entry ) )
			{
				return false;
			}

			m_entriesMap.Emplace( resourcePath, entry );
		}

		Uint32 appearanceCount = 0;
		file << appearanceCount;

		for (Uint32 i = 0; i < appearanceCount; ++i)
		{
			ResourcePath resourcePath = LoadResourcePath_NoLock(file);
			Uint64 nameHash = 0;
			file << nameHash;
			CacheEntry entry;

			file << entry.m_fileTimeUTC;
			file.Serialize(&entry.m_data, sizeof(StreamingData));

			const auto readDependencies = [&file, this](red::DynArray< ResourcePath >& outDeps)
			{
				Uint32 depsNum = 0;
				file << depsNum;

				outDeps.Reserve(depsNum);
				for (Uint32 i = 0; i < depsNum; ++i)
				{
					const ResourcePath path = LoadResourcePath_NoLock(file);
					if (path.IsValid())
					{
						outDeps.PushBack(path);
					}
				}
			};

			readDependencies(entry.m_dependencies);
			readDependencies(entry.m_revDependencies);

			AppearanceKey key(resourcePath, CName(nameHash));
			m_appearanceDictionary.Emplace( key, entry );
		}

		return true;
	}

	Bool StreamedResourceCache::LoadEntry_NoLock(IFile& file, ResourcePath& outResourcePath, CacheEntry& outEntry)
	{
		outResourcePath = LoadResourcePath_NoLock( file );
		if ( !outResourcePath.IsValid() )
			return false;

		file << outEntry.m_fileTimeUTC;
		file.Serialize( &outEntry.m_data, sizeof( StreamingData ) );

		const auto readDependencies = [&file, this] ( red::DynArray< ResourcePath >& outDeps )
		{
			Uint32 depsNum = 0;
			file << depsNum;

			outDeps.Reserve( depsNum );
			for ( Uint32 i = 0; i < depsNum; ++i )
			{
				const ResourcePath path = LoadResourcePath_NoLock( file );
				if ( path.IsValid() )
				{
					outDeps.PushBack( path );
				}
			}
		};

		readDependencies( outEntry.m_dependencies );
		readDependencies( outEntry.m_revDependencies );

		return true;
	}

	Bool StreamedResourceCache::Load( const red::StringView& relativePath )
	{
		red::ScopedLock< CacheLock > lock( m_cacheLock );

		red::StopWatch timer;

		ClearCache_NoLock();

		struct ApplyOverrideOnReturn
		{
			ApplyOverrideOnReturn(StreamedResourceCache* cache)
				: m_cache(cache)
			{
			}

			~ApplyOverrideOnReturn()
			{
				m_cache->LoadAutoHideBoostFile();
			}

			StreamedResourceCache* m_cache;
		};

		ApplyOverrideOnReturn scopedFunction(this);

		m_cacheFilePath = GetStreamingCacheFile( relativePath );

		auto reader = GFileManager->CreateFileReader( m_cacheFilePath, FOF_AbsolutePath | FOF_Buffered );
		if ( !reader )
		{
			RED_LOG_ERROR( "StreamedResourceCache: failed to open '%hs' for loading", m_cacheFilePath.AsChar() );
			return false;
		}

		Uint32 magic = 0;
		*reader << magic;
		if ( magic != c_fileMagic )
		{
			RED_LOG_ERROR( "StreamedResourceCache: magic %u does not match expected %u", magic, c_fileMagic );
			return false;
		}

		Uint32 cacheVersion = 0;
		*reader << cacheVersion;
		if (cacheVersion != c_cacheVersion )
		{
			RED_LOG_ERROR( "StreamedResourceCache: version %u does not match expected %u", cacheVersion, c_cacheVersion );
			return false;
		}

		Uint32 resaveVersion = 0;
		*reader << resaveVersion;
		if ( resaveVersion != VER_CURRENT )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Resave file version mismatch: %u vs current %u", resaveVersion, VER_CURRENT );
			return false;
		}

		if ( !LoadResourcePaths_NoLock( *reader ) )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Failed to load resource paths" );
			return false;
		}

		if ( !LoadEntriesMap_NoLock( *reader ) )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Failed to load cache entries" );
			return false;
		}

		RED_LOG_INFO( "StreamedResourceCache: loading %u entries took %1.2f ms", m_entriesMap.Size(), timer.GetDeltaMS() );

		// copy the cache file so we can restore later on
		const red::AbsolutePath backupFile = red::utils::ReplaceExtension( m_cacheFilePath, "cache.bak" );
		if ( !CSystemIO::CopyFile( m_cacheFilePath.AsChar(), backupFile.AsChar(), false ) )
		{
			RED_LOG_WARNING( "Failed to create streaming cache backup '%hs'", backupFile.AsChar() );
		}
		
		m_cacheIsDirty.SetValue( false );
		return true;
	}

	//////////////////////////////////////////////////////////////////////////

	StreamedResourceCache::CacheEntry* StreamedResourceCache::TryGetCachedStreamingData_NoLock(const ResourcePath& path ) const
	{
		return m_entriesMap.FindPtr(path);
	}

	StreamedResourceCache::CacheEntry* StreamedResourceCache::TryGetCachedStreamingData_NoLock( const ResourcePath& path, CName optionalName ) const
	{
		return optionalName.Empty() ? m_entriesMap.FindPtr(path) : m_appearanceDictionary.FindPtr( AppearanceKey{ path, optionalName } );
	}

	StreamedResourceCache::CacheEntry* StreamedResourceCache::LoadStreamingData_NoLock( const ResourcePath& path, CName optionalName, CacheEntry* optUpdateEntry, bool updateOnlyLoadedResource ) const
	{
		RED_LOG_DEBUG( "StreamedResourceCache: Loading streaming data for '%hs'", path.ToDebugString() );

		IStreamedResourceDataExtractor* matchingExtractor = FindDataExtractor( path );
		if ( !matchingExtractor )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Failed to find matching streamed resource extractor for '%hs'", path.ToDebugString() );
			return CreateErrorEntry_NoLock( path, nullptr );
		}

		// check if the resource is not already loaded via resource loader
		THandle< StreamedResource > resource = Cast< StreamedResource >( GResourceLoader->TryAcquiringLoadedResource( path ) );
		if ( !resource )
		{
			if ( updateOnlyLoadedResource )
			{
				RED_FATAL_ASSERT( optUpdateEntry, "Missing entry to update" );
				return CreateDirtyEntry_NoLock( path, optUpdateEntry );
			}

			// ask the depot for the file access
			auto asyncSource = m_depot->CreateResourceAsyncSource( path );
			if ( !asyncSource )
			{
				RED_LOG_ERROR( "StreamedResourceCache: Failed to open resource '%hs' for inspection - file moved or deleted", path.ToDebugString() );
				return CreateErrorEntry_NoLock( path, optUpdateEntry );
			}

			// load the resource data
			resource = Cast< StreamedResource >( serialization::tools::ExtractSingleObject( *asyncSource ) );
			if ( !resource )
			{
				RED_LOG_ERROR( "StreamedResourceCache: Failed to cast resource '%hs' to StreamedResource for inspection", path.ToDebugString() );
				return CreateErrorEntry_NoLock( path, optUpdateEntry );
			}
		}

		if ( !matchingExtractor->SupportsResource( *resource ) )
		{
			RED_LOG_ERROR( "StreamedResourceCache: Resource '%hs' is not of the expected type!", path.ToDebugString() );
			return CreateErrorEntry_NoLock( path, optUpdateEntry );
		}

		CacheEntry entry;
		if ( optUpdateEntry )
		{
			// copy reverse dependencies from the old entry (avoid scanning the whole cache)
			entry.m_revDependencies = optUpdateEntry->m_revDependencies;
			entry.m_data.m_autoHideBoosted = optUpdateEntry->m_data.m_autoHideBoosted; // ctremblay: For entity. This will unlock streaming distance.
		}

		// DO NOT ACCESS "optUpdateEntry" BELOW THIS LINE !!!

		Bool streamingDataChanged = false;
		if ( UpdateCacheEntryStreamingData_NoLock( path, optionalName, *resource, entry, matchingExtractor ) )
		{
			streamingDataChanged = true;
		}
		else
		{	
			entry = CacheEntry();
		}

		UpdateCacheEntryDependencyData_NoLock( path, *resource, entry, matchingExtractor );

		CacheEntry* newEntry = nullptr;

		if( optionalName.Empty() )
		{
			newEntry = &m_entriesMap[path];
			if( !newEntry )
			{
				m_cacheIsDirty.SetValue(true);
				auto result = m_entriesMap.Insert(path, entry);
				return &result.Iterator().Value();
			}
		}
		else
		{
			newEntry = &m_appearanceDictionary[AppearanceKey( path, optionalName )];
			if (!newEntry)
			{
				m_cacheIsDirty.SetValue(true);
				auto result = m_appearanceDictionary.Insert(AppearanceKey( path, optionalName ), entry);
				return &result.Iterator().Value();
			}
		}


		// overwrite the old entry
		*newEntry = entry;

		if ( streamingDataChanged )
		{
			resource->OnStreamingDataChanged( entry.m_data );
		}

		return newEntry;
	}

	StreamedResourceCache::CacheEntry* StreamedResourceCache::CreateErrorEntry_NoLock( const res::ResourcePath& path, CacheEntry* optUpdateEntry ) const
	{
		CacheEntry* entry = optUpdateEntry;
		if ( !entry )
		{
			auto result = m_entriesMap.Insert( path, CacheEntry() );
			entry = &result.Iterator().Value();
		}

		RED_FATAL_ASSERT( entry );
		entry->m_fileTimeUTC = m_depot->GetResourceTimestamp( path ); // can be zero if the file doesn't exist, doesn't matter
		entry->m_isDirty = false;
		entry->m_data = StreamingData();

		m_cacheIsDirty.SetValue( true );

		return entry;
	}

	StreamedResourceCache::CacheEntry* StreamedResourceCache::CreateDirtyEntry_NoLock( const res::ResourcePath& path, CacheEntry* optUpdateEntry ) const
	{
		CacheEntry* entry = optUpdateEntry;
		if ( !entry )
		{
			auto result = m_entriesMap.Insert( path, CacheEntry() );
			entry = &result.Iterator().Value();
		}

		RED_FATAL_ASSERT( entry );

		// force streaming data recomputation on the next access
		entry->m_fileTimeUTC = 0;
		entry->m_isDirty = true;
		entry->m_data = StreamingData();

		m_cacheIsDirty.SetValue( true );

		return entry;
	}

	IStreamedResourceDataExtractor* StreamedResourceCache::FindDataExtractor( const res::ResourcePath& path ) const
	{
		IStreamedResourceDataExtractor* matchingExtractor = nullptr;
		for ( const auto& extractor : m_extractors )
		{
			if ( extractor->SupportsResource( path ) )
			{
				matchingExtractor = extractor.Get();
				break;
			}
		}

		return matchingExtractor;
	}

	Bool StreamedResourceCache::UpdateCacheEntryStreamingData_NoLock( const ResourcePath& path, CName optionalName, const StreamedResource& resData, CacheEntry& entry, const IStreamedResourceDataExtractor* extractor ) const
	{
		const Uint64 fileTime = m_depot->GetResourceTimestamp( path );
		if ( fileTime == 0 )
		{
			RED_LOG_WARNING( "StreamedResourceCache: Resource '%hs' does not exist", path.ToDebugString() );
			return false;
		}

		entry.m_fileTimeUTC = fileTime;
		entry.m_isDirty = false;

		const StreamingData oldStreamingData = entry.m_data;

		// extract the streaming information
		extractor->ExtractStreamingData( path, optionalName, resData, entry.m_data );

		if ( oldStreamingData != entry.m_data )
		{
			RED_FATAL_ASSERT( entry.m_data.m_autoHideDistance >= 0.0f, "Extracted invalid autohide distance for '%hs'", path.ToDebugString() );
			RED_FATAL_ASSERT( entry.m_data.m_streamingDistance > 0.0f, "Extracted invalid streaming distance for '%hs'", path.ToDebugString() );

			// print debug log
			const Box& box = entry.m_data.m_boundingBox;
			RED_LOG_DEBUG( "StreamedResourceCache: Successfully updated entry for '%hs': distance=%f, boxMin=[%f,%f,%f], boxMax=[%f,%f,%f]",
				path.ToDebugString(), entry.m_data.m_streamingDistance, box.Min.X, box.Min.Y, box.Min.Z, box.Max.X, box.Max.Y, box.Max.Z );

			m_cacheIsDirty.SetValue( true );
			return true;
		}
	
		return false;
	}

	Bool StreamedResourceCache::UpdateCacheEntryDependencyData_NoLock( const ResourcePath& path, const StreamedResource& resData, CacheEntry& entry, const IStreamedResourceDataExtractor* extractor ) const
	{
		RED_FATAL_ASSERT( path.IsValid() );

		// remove this resource hash from old reverse dependencies lists
		for ( const ResourcePath& depPath : entry.m_dependencies )
		{
			CacheEntry* dependentEntry = TryGetCachedStreamingData_NoLock( depPath );
			if ( dependentEntry )
			{
				// TODO this is O(N*M) but may be not that bad if there is no that many dependencies
				dependentEntry->m_revDependencies.RemoveReorder( path );
			}
		}

		// get list updated dependencies
		red::DynArray< ResourcePath > dependencies{ red::PoolBackend() };
		extractor->ExtractDependencies( path, resData, dependencies );

		// remove duplicates from the array
		red::alg::RemoveDuplicates( dependencies );

		// determine reverse dependencies
		for ( auto iter = dependencies.Begin(); iter != dependencies.End(); ++iter )
		{
			const ResourcePath& depPath = *iter;

			RED_FATAL_ASSERT( depPath.IsValid(), "ExtractDependencies should return only valid resource paths. Exactor='%hs'", extractor->GetClass()->GetName().AsChar() );

			if ( depPath == path )
			{
				RED_LOG_ERROR( "StreamedResourceCache: Data error - resource '%hs' reported that it's dependent on itself. Exactor='%hs'", depPath.ToDebugString(), extractor->GetClass()->GetName().AsChar());
			}

			CacheEntry* dependentEntry = TryGetCachedStreamingData_NoLock( depPath );
			if ( dependentEntry )
			{
				// TODO this is O(N*M) but may be not that bad if there is no that many dependencies
				red::alg::PushBackUnique( dependentEntry->m_revDependencies, path );
			}
		}

		entry.m_dependencies = std::move( dependencies );

		m_cacheIsDirty.SetValue( true );

		return true;
	}

	StreamingData StreamedResourceCache::GetStreamingData(const res::ResourcePath& path) const
	{
		StreamingData data{};
		GetValidCacheData(path, CName(), [&data](const CacheEntry& entry) 
		{
			data = entry.m_data;
		});
		return data;
	}

	StreamingData StreamedResourceCache::GetStreamingData(const res::ResourcePath& path, CName optionalName) const
	{
		StreamingData data{};
		if( optionalName.Empty() || optionalName == RED_NAME_CONSTEXPR_NOREG( "random" ) )
		{
			optionalName = RED_NAME_CONSTEXPR( "default" );
		}
		
		GetValidCacheData(path, optionalName, [&data](const CacheEntry& entry) 
		{
			data = entry.m_data;
		});
		return data;
	}

	red::DynArray< res::ResourcePath > StreamedResourceCache::GetReverseDependencies(const res::ResourcePath& path) const
	{
		red::DynArray< res::ResourcePath > ret{ red::PoolBackend() };
		GetValidCacheData(path, CName(), [&ret](const CacheEntry& entry) {
			ret = entry.m_revDependencies;
		});
		return ret;
	}

	template< typename TDataProcessingFunc >
	void res::StreamedResourceCache::GetValidCacheData(const res::ResourcePath& path, CName optionalName, const TDataProcessingFunc& func) const
	{
		// don't bother with invalid/empty paths
		if ( path.IsValid() && !m_extractors.Empty() )
		{
			{
				// Try to get streaming data from cache
				red::ScopedSharedLock< CacheLock > sharedLock( m_cacheLock );
				CacheEntry* entry = TryGetCachedStreamingData_NoLock( path, optionalName );
				if ( entry && !entry->m_isDirty )
				{
					if( m_retryOnFileNotFound && entry->m_fileTimeUTC == 0 )
					{
						if( m_depot->GetResourceTimestamp( path ) ) 
						{
							entry->m_isDirty = true;
						}
						else 
						{
							return;
						}
					}
					else
					{
						func( *entry );
						return;
					}
				}
			}

			{
				red::ScopedLock< CacheLock > exlusiveLock( m_cacheLock );
				CacheEntry* entry = TryGetCachedStreamingData_NoLock( path, optionalName );
				if ( entry )
				{
					// Cached file time is up to date
					if ( !entry->m_isDirty )
					{
						func(*entry);
						return;
					}

					// Hit the filesystem and refresh the timestamp
					const Uint64 fileTime = m_depot->GetResourceTimestamp( path );
				
					// Current cached entry is up to date
					if ( entry->m_fileTimeUTC == fileTime && entry->m_data.IsValid() )
					{
						entry->m_isDirty = false;
						func(*entry);
						return;
					}
				}
				else
				{
					// create dummy entry to prevent stack overflow on resource recursion
					CacheEntry dummyEntry;

					m_cacheIsDirty.SetValue( true );
					if( optionalName.Empty() )
					{
						auto result = m_entriesMap.Insert(path, dummyEntry);
						entry = &result.Iterator().Value();
					}
					else
					{
						auto result = m_appearanceDictionary.Insert( AppearanceKey( path, optionalName ), dummyEntry);
						entry = &result.Iterator().Value();
					}
				}

				// load the streaming data from file
				entry = LoadStreamingData_NoLock( path, optionalName, entry );
				if ( entry )
				{
					func(*entry);
					return;
				}
			}
		}

		// not a streamable resource or file does not exist
	}

	void StreamedResourceCache::UpdateStreamingData( const ResourcePath& path )
	{
		// don't update entries for unsupported resource types
		if ( FindDataExtractor( path ) )
		{
			red::ScopedLock< CacheLock > lock( m_cacheLock );

			auto* entry = TryGetCachedStreamingData_NoLock( path );
			if ( !entry )
			{
				// Guard for the case where the data been requested by anything yet
				// maybe distance being manipulated through interop
				entry = LoadStreamingData_NoLock( path, CName(), nullptr );
				if ( !entry )
				{
					RED_LOG_WARNING( "StreamedResourceCache: resource '%hs' does not exist", path.ToDebugString() );
				}
			}

			if ( entry )
			{
				// TODO update dependencies for this entry

				// Note: we only update loaded resources, the rest is marked as dirty
				Bool updateOnlyLoaded = true;

				const Updater updater( *this );
				const auto entriesToUpdate = red::ArraySpan< const ResourcePath >( &path, 1 );
				const Uint32 updatedCount = updater.UpdateEntries( entriesToUpdate, updateOnlyLoaded );
				RED_LOG_DEBUG( "StreamedResourceCache: Entry for '%hs' updated, %d parents updated", path.ToDebugString(), updatedCount );
			}
		}
	}

	void StreamedResourceCache::ReentrantRWLock::Acquire()
	{
		const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
		const Uint32 snapshotTID = m_exclusiveThreadID.GetValue();

		// What matters is we don't try to lock reentrantly; a thread can't race against itself.
		// Doesn't support upgrading to exclusive from shared lock!
		if ( snapshotTID == tid )
		{
			m_recursionDepth += 1;
		}
		else
		{
			m_rwLock.Acquire();
			m_exclusiveThreadID.SetValue( tid );
		}
	}

	void StreamedResourceCache::ReentrantRWLock::Release()
	{
		RED_FATAL_ASSERT( red::ThreadId::CurrentThread().AsNumber() == m_exclusiveThreadID.GetValue(), "Release on unowned lock!" );
		if ( m_recursionDepth > 0 )
		{
			m_recursionDepth -= 1;
		}
		else
		{
			m_exclusiveThreadID.SetValue( 0 );
			m_rwLock.Release();
		}
	}

	void StreamedResourceCache::ReentrantRWLock::AcquireShared()
	{
		// Check if exclusively locked by this thread already, can't race against thread-self; doesn't support reentrant shared lock
		const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
		const Uint32 snapshotTID = m_exclusiveThreadID.GetValue();
		if ( snapshotTID != tid )
		{
			m_rwLock.AcquireShared();
		}
	}

	void StreamedResourceCache::ReentrantRWLock::ReleaseShared()
	{
		// Check if exclusively locked by this thread already, can't race against thread-self; doesn't support reentrant shared lock
		const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
		const Uint32 snapshotTID = m_exclusiveThreadID.GetValue();
		if ( snapshotTID != tid )
		{
			m_rwLock.ReleaseShared();
		}
	}

	void StreamedResourceCache::RetryOnFileNotFound()
	{
		m_retryOnFileNotFound = true;
	}

	void StreamedResourceCache::ExportMeshLocalBounds(red::HashMap<Uint64, Box>& outBounds) const
	{
#ifndef EXPORT_MESH_LOCAL_BOUNDS_CACHE
		return;
#endif


		for (const auto& it : m_entriesMap)
		{
			const res::ResourcePath path = it.Key();
			const auto& cacheEntry = it.Value();

			if (path.ToStringView().EndsWith(".mesh") || path.ToStringView().EndsWith(".w2mesh"))
			{
				outBounds.Insert(path.GetHash(), cacheEntry.m_data.m_visibleBoundingBox);
			}
		}
	}

	void StreamedResourceCache::LoadAutoHideBoostFile()
	{
		const red::AbsolutePath configFilePath = GFileManager->GetEngineRoot().AddFilePath(c_autohide_override);
		red::String csvContent;
		if (!red::LoadFileToString(configFilePath, csvContent))
		{
			RED_LOG_ERROR("Failed to read autohide override from CSV file: %hs.", configFilePath.AsChar());
			return;
		}

		InitializeDataExtractors();

		// ctremblay: Not full csv support. "path, distance" only. No header, no nothing. This is already hacked enough. And for E3 2020
		auto rowResult = csvContent.Split("\r\n");

		for (Uint32 index = 0, end = rowResult.Size(); index != end; ++index)
		{
			auto columnResult = rowResult[index].Split(",");
			if (columnResult.Size() == 2)
			{
				const res::ResourcePath path = res::ResourcePath::Build(columnResult[0]);
				float factor = 0.0f;
				if (path.IsValid() && FromString(columnResult[1], factor))
				{
					const bool isEntity = path.ToStringView().EndsWith( ".ent" );

					CacheEntry& entry = !isEntity ? m_entriesMap[ path ] : m_appearanceDictionary[ AppearanceKey( path, RED_NAME_CONSTEXPR( "default" ) ) ];
	
					entry.m_isDirty = true;
					entry.m_fileTimeUTC = 0;
					entry.m_data.m_autoHideBoosted = true;
					
					if( isEntity )
					{
						GetStreamingData( path, RED_NAME_CONSTEXPR( "default" ) );
					}
					else
					{
						GetStreamingData( path );
					}
				
					CacheEntry& newEntry = !isEntity ? m_entriesMap[ path ] : m_appearanceDictionary[ AppearanceKey( path, RED_NAME_CONSTEXPR( "default" ) ) ];

					newEntry.m_data.m_autoHideDistance *= factor;
					newEntry.m_data.m_streamingDistance *= factor;
					newEntry.m_data.m_boundingBox *= factor;
					newEntry.m_data.m_visibleBoundingBox *= factor;
					newEntry.m_data.m_surfaceAreaPerAxis *= factor;
					newEntry.m_data.m_autoHideBoosted = true;

					auto dependencies = newEntry.m_revDependencies;

					for (auto& dep : dependencies)
					{
						UpdateStreamingData(dep);
					}
				}
			}
		}
	}

}
