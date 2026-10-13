#include "build.h"
#include "resourcePathCache.h"
#include "resourcePath.h"

#if defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#ifdef RED_PLATFORM_WINPC

RED_MEMORY_POOL_STATIC( PoolResourcePathCache, red::memory::DefaultAllocator );
RED_MEMORY_POOL_STATIC( PoolResourcePathCacheItems, red::memory::UnsafeDynamicLinearAllocator );
RED_MEMORY_POOL_STATIC( PoolResourcePathCacheStrings, red::memory::UnsafeDynamicLinearAllocator );

#endif // RED_PLATFORM_WINPC

#endif // defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

namespace res
{
namespace prv
{

#if defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

static const ResourcePathCache* s_resourcePathCacheForDebugging = nullptr;

ResourcePathCache& GetResourcePathCache()
{
	static ResourcePathCache s_cache;
	return s_cache;
}

ResourcePathCache::ResourcePathCache()
	: m_pathMap( red::PoolDebug() )
{
	red::memory::UnsafeDynamicLinearAllocatorParameter itemAllocatorParams
	{ 
		&red::memory::AcquireSystemAllocator(),
		RED_KILO_BYTE( 64u ),
		red::memory::Flags::Flags_CPU_Read_Write | red::memory::Flags::Flags_Skip_System_OOM
	};
	m_itemAllocator.Initialize( itemAllocatorParams );

	red::memory::UnsafeDynamicLinearAllocatorParameter stringAllocatorParams
	{
		&red::memory::AcquireSystemAllocator(),
		RED_KILO_BYTE( 64u ),
		red::memory::Flags::Flags_CPU_Read_Write | red::memory::Flags::Flags_Skip_System_OOM
	};
	m_stringAllocator.Initialize( stringAllocatorParams );

#ifdef RED_PLATFORM_WINPC

	RED_INITIALIZE_MEMORY_POOL( PoolResourcePathCache, red::PoolDebug, red::memory::AcquireDefaultAllocator(), 0 );
	RED_INITIALIZE_MEMORY_POOL( PoolResourcePathCacheItems, PoolResourcePathCache, m_itemAllocator, RED_MEGA_BYTE( 40 ) );
	RED_INITIALIZE_MEMORY_POOL( PoolResourcePathCacheStrings, PoolResourcePathCache, m_stringAllocator, RED_MEGA_BYTE( 200 ) );

#endif // RED_PLATFORM_WINPC

	s_resourcePathCacheForDebugging = this;
}

ResourcePathCache::~ResourcePathCache()
{
	s_resourcePathCacheForDebugging = nullptr;
}

ResourcePathCacheItem* ResourcePathCache::FindOrCreateItem( const red::StringView& sanitisedPath )
{
	RED_FATAL_ASSERT( sanitisedPath.Length() < ResourcePath::MAX_LENGTH, "Resource path too long!" );
	RED_FATAL_ASSERT( !sanitisedPath.Empty(), "Resource path cannot be empty here" );
	// Assume here that the string in sanitizedPath and in the stored paths are null terminated 
	// as we explicitly add a null terminator in ResourcePath::Build and when we store it in CreateItem

	Uint64 hash = ComputeResourcePathHash_Sanitised( sanitisedPath );

	ResourcePathCacheItem* item = FindItem( hash );
	if( item )
	{
		return item;
	}

	RED_SCOPE_LOCK( m_pathLock );
	PathMap::const_iterator iter = m_pathMap.Find( hash );
	if ( iter != m_pathMap.End() )
	{
		item = iter.Value();

		RED_FATAL_ASSERT( item->ToStringView() == sanitisedPath, 
			"Path hash collision between '%hs' and '%hs', both generate %016llX", 
			item->ToStringView().Data(), sanitisedPath.Data(), item->GetHash() );
	}
	else
	{
		item = CreateItem( hash, sanitisedPath );
		auto result = m_pathMap.Insert( item->GetHash(), item );
		RED_FATAL_ASSERT( result.IsSuccessful(), "Failed to insert ResourcePath data into cache" );
	}

	return item;
}

ResourcePathCacheItem* ResourcePathCache::FindItem( const Uint64 hash ) const
{
	RED_SCOPE_SHARED_LOCK( m_pathLock );
	PathMap::const_iterator iter = m_pathMap.Find( hash );
	if ( iter != m_pathMap.End() )
	{
		return iter.Value();
	}
	return nullptr;
}

ResourcePathCacheItem* ResourcePathCache::CreateItem( Uint64 hash, const red::StringView& sanitisedPath )
{
	const auto length = sanitisedPath.Length();
	const Uint32 bufferSize = red::memory::RoundUp( length + 1, red::memory::UnsafeDynamicLinearAllocator::DefaultAlignmentType::value );
	char* stringBuffer = static_cast<char*>( RED_ALLOCATE_WITHOUT_HOOKS( m_stringAllocator, bufferSize, red::memory::HookType_All ) );

	red::Memcpy( stringBuffer, sanitisedPath.Data(), length );
	red::Memzero( stringBuffer + length, bufferSize - length ); // Explicitly clear memory at the end of the string, also sets null terminator

	return RED_NEW_WITHOUT_HOOKS( ResourcePathCacheItem, m_itemAllocator, red::memory::HookType_All )( hash, red::StringView( stringBuffer, sanitisedPath.Length() ) );
}

namespace fileformat
{

struct Header
{
	Uint32 magic;
	Uint32 version;
	Uint32 itemsOffset;
	Uint32 itemsCount;
	Uint32 stringsOffset;
	Uint32 stringsSize;

	static constexpr Uint32 MAGIC_VALUE = 'PSER'; // RESP backwards
	static constexpr Uint32 CurrentVersion = 1;
};

struct ItemEntry
{
	Uint64 hash;
	Uint32 stringOffset;
	Uint32 stringSize;
};

} // fileformat

const char* ResourcePathCache::GetDefaultFilename()
{
	return "ResourcePath.cache";
}

bool ResourcePathCache::Load( const red::AbsolutePath& cachePath )
{
	red::StopWatch timer;

	red::Blob fileBuffer( red::MakeEmptyUniqueBuffer< red::PoolDebug >( 1 ) );
	if ( !red::LoadFileToBuffer( cachePath, fileBuffer ) )
	{
		RED_LOG_ERROR( "ResourcePathCache: Could not load ResourcePath Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	if ( fileBuffer.Size() <= sizeof( fileformat::Header ) )
	{
		RED_LOG_ERROR( "ResourcePathCache: Invalid ResourcePath Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	const auto& header = fileBuffer.As< const fileformat::Header >();
	if ( header.magic != fileformat::Header::MAGIC_VALUE || 
		header.itemsCount + header.itemsCount * sizeof(fileformat::ItemEntry) > fileBuffer.Size() ||
		header.stringsOffset + header.stringsSize > fileBuffer.Size() )
	{
		RED_LOG_ERROR( "ResourcePathCache: Invalid ResourcePath Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	auto items = fileBuffer.AsArray< fileformat::ItemEntry >( header.itemsOffset, header.itemsCount );
	auto strings = fileBuffer.View( header.stringsOffset, header.stringsSize );

	RED_SCOPE_LOCK( m_pathLock );

	red::DynArray< ResourcePathCacheItem* > addedItems{ red::PoolDebug() };
	addedItems.Reserve( items.Count() );

	for ( auto& item : items )
	{
		if ( m_pathMap.Find( item.hash ) != m_pathMap.End() )
		{
			continue;
		}

		const char* stringPtr = strings.Pointer< const char >( item.stringOffset );

		addedItems.PushBack( CreateItem( item.hash, red::StringView( stringPtr, item.stringSize ) ) );
	}

	// Two stage adding of items to the map as we only need to not add items that already exist
	// and the items in the file should not be duplicates of each other, therefore we save searching an ever bigger hashmap
	m_pathMap.Reserve( m_pathMap.Size() + items.Size() );
	for ( const auto& item : addedItems )
	{
		auto result = m_pathMap.Insert( item->GetHash(), item );
		RED_FATAL_ASSERT( result.IsSuccessful(), "Failed to insert ResourcePath data into cache" );
	}

	RED_LOG( "ResourcePathCache: Loading ResourcePath Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

bool ResourcePathCache::Save( const red::AbsolutePath& cachePath ) const
{
	red::StopWatch timer;

	Uint32 written = 0;
	io::NativeFileHandle writer;
	if ( !writer.Open( cachePath.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		RED_LOG_ERROR( "ResourcePathCache: Could not open the cache file for writing '%hs'", cachePath.AsChar() );
		return false;
	}

	RED_SCOPE_SHARED_LOCK( m_pathLock );

	const auto itemRange = m_itemAllocator.GetUsedMemoryRange();
	const auto itemPointer = reinterpret_cast< const ResourcePathCacheItem* >( itemRange.address );
	const auto itemCount = static_cast< Uint32 >( itemRange.size / sizeof( res::prv::ResourcePathCacheItem ) );
	const auto items = red::MakeArraySpan( itemPointer, itemCount );

	const auto stringRange = m_stringAllocator.GetUsedMemoryRange();
	const char* stringStart = reinterpret_cast< const char* >( stringRange.address );
	const Uint32 stringSize = static_cast< Uint32 >( stringRange.size );

	fileformat::Header header;
	red::Memzero( &header, sizeof( fileformat::Header ) );
	RED_VERIFY( writer.Write( &header, sizeof ( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	red::DynArray< fileformat::ItemEntry > fileItems{items.Count(), red::PoolDebug()};
	Uint32 index = 0;
	for ( const auto& item : items )
	{
		if ( item.GetHash() == 0 )
		{
			continue;
		}

		auto& fileItem = fileItems[ index++ ];

		fileItem.hash = item.GetHash();

		const auto& string = item.ToStringView();
		fileItem.stringOffset = static_cast< Uint32 >( string.Data() - stringStart ); // Guatanteed since max memory size is 4GB for this allocator
		fileItem.stringSize = string.Length();
	}

	header.magic = fileformat::Header::MAGIC_VALUE;
	header.version = fileformat::Header::CurrentVersion;
	header.itemsOffset = sizeof( fileformat::Header );
	header.itemsCount = index;
	header.stringsOffset = header.itemsOffset + ( header.itemsCount * sizeof( fileformat::ItemEntry ) );
	header.stringsSize = stringSize;

	RED_FATAL_ASSERT( header.itemsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( fileItems.Data(), fileItems.DataSize(), written ) && written == fileItems.DataSize() );
	RED_FATAL_ASSERT( header.stringsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( stringStart, stringSize, written ) && written == stringSize );
	const auto endOfFile = writer.Tell();

	// Write out correct header now that we've written the entire file out to disk
	writer.Seek( 0, io::eSeekOrigin_Set );
	RED_VERIFY( writer.Write( &header, sizeof( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	writer.Flush();
	writer.Truncate( endOfFile );
	writer.Close();

	RED_LOG( "ResourcePathCache: Saving ResourcePath Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

#endif // defined( RED_USE_RESOURCEPATH_STRINGS ) || defined( RED_ENABLE_RESOURCEPATH_DEBUGGING )

} // prv
} // res
