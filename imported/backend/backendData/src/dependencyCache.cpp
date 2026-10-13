/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyCache.h"

#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/backendResourceDepot.h"

#include "../../../common/archive/include/common.h"

namespace dependency
{

Cache::Cache( data::ResourceDepot* depot )
	: m_depot( depot )
{
	RED_FATAL_ASSERT( depot != nullptr );

	red::memory::DynamicLinearAllocatorParameter param{ 
		&red::memory::AcquireSystemAllocator(),
		RED_KILO_BYTE( 64 ),
		red::memory::Flags::Flags_CPU_Read_Write 
	};
	m_basicInfoStorage.Initialize( param );
}

Cache::~Cache()
{
}

data::ResourceDepot* Cache::GetDepot() const
{
	return m_depot;
}

red::DynArray< const FileInfo* > Cache::Interal_GetEntries() const
{
	return m_basicInfoCache.GetValues();
}

//-----------------------------------------------------------------------------

#pragma pack(push, 4)

namespace fileformat
{

//
// File Structure
//
// Header
// [ Record ... ]
// [ String ... ]
//

struct Header
{
	Uint32 magic;
	Uint32 recordCount;
	Uint32 pathsOffset;
	Uint32 pathsBytes;

	static constexpr Uint32 MAGIC_VALUE = 'CHFD'; // Dependency File Hash Cache 'DFHC' backwards for endianness
};

struct Record
{
	Uint32 pathOffset;
	Uint64 timestamp;
	archive::FileHash hash;
};

} // fileformat

#pragma pack(pop)

const char* Cache::GetDefaultFilename()
{
	return "DependencyFileHashCache.cache";
}

bool Cache::Load( const red::AbsolutePath& cachePath )
{
	red::StopWatch timer;
	m_debugCachePath = cachePath;

	red::Blob fileBuffer;
	if ( !red::LoadFileToBuffer( cachePath, fileBuffer ) )
	{
		RED_LOG_ERROR( "DependencyCache: Could not load Dependency Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	if ( fileBuffer.Size() <= sizeof( fileformat::Header ) )
	{
		RED_LOG_ERROR( "DependencyCache: Invalid Dependency Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	const auto& header = fileBuffer.As< const fileformat::Header >();
	if ( header.magic != fileformat::Header::MAGIC_VALUE || 
		header.pathsOffset + header.pathsBytes > fileBuffer.Size() )
	{
		RED_LOG_ERROR( "DependencyCache: Invalid Dependency Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	const auto* recordStart = fileBuffer.Pointer< const fileformat::Record >( sizeof( fileformat::Header ) );
	red::ArraySpan< const fileformat::Record > records{ recordStart, header.recordCount };

	red::BlobView resourcePathStrings = fileBuffer.Range( header.pathsOffset, header.pathsBytes );

	m_basicInfoCache.Reserve( m_basicInfoCache.Size() + header.recordCount );
	for ( const auto& record : records )
	{
		auto resourcePath = res::ResourcePath::Build( resourcePathStrings.Pointer< char >( record.pathOffset ) );
		CacheKey key = resourcePath;
		
		FileInfo* value = RED_NEW( FileInfo, m_basicInfoStorage )();
		value->path = resourcePath;
		value->timestamp = record.timestamp;
		value->hash = dependency::Hash::FromFileHash( record.hash );

		auto result = m_basicInfoCache.Set( key, value );
		RED_FATAL_ASSERT( result.IsSuccessful() );
	}

	RED_LOG( "DependencyCache: Loading Dependency Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

bool Cache::Save( const red::AbsolutePath& cachePath )
{
	red::StopWatch timer;

	Uint32 written = 0;
	io::NativeFileHandle writer;
	if ( !writer.Open( cachePath.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		RED_LOG_ERROR( "DependencyCache: Could not open the cache file for writing '%hs'", cachePath.AsChar() );
		return false;
	}

	// Grab entries matching the depot we want to save the cache for
	red::DynArray< dependency::FileInfo* > cacheValues = m_basicInfoCache.GetValues();

	std::sort( cacheValues.Begin(), cacheValues.End(), 
		[]( const dependency::FileInfo* left, const dependency::FileInfo* right ) { return left->path < right->path; } );

	// Create records for all of the desired items
	red::DynArray< fileformat::Record > records;
	records.Reserve( cacheValues.Size() );

	red::Blob pathsBuffer{ cacheValues.Size() * res::ResourcePath::MAX_LENGTH };
	Uint32 pathsOffset = 0;

	for ( const dependency::FileInfo* info : cacheValues )
	{
		records.PushBack( { pathsOffset, info->timestamp, dependency::Hash::ToFileHash( info->hash ) } );

		const red::StringView path = info->path.ToStringView();
		const Uint32 pathLength = path.Length();
		char* pathsWritePtr = pathsBuffer.Pointer<char>( pathsOffset );
		red::Memcpy( pathsWritePtr, path.Data(), pathLength );
		pathsWritePtr[ pathLength ] = '\0';
		pathsOffset += pathLength + 1;
	}

	fileformat::Header header;
	red::Memzero( &header, sizeof( fileformat::Header ) );
	RED_VERIFY( writer.Write( &header, sizeof ( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	header.magic = fileformat::Header::MAGIC_VALUE;
	header.recordCount = records.Size();
	header.pathsOffset = sizeof( fileformat::Header ) + ( sizeof( fileformat::Record ) * header.recordCount );
	header.pathsBytes = pathsOffset;

	RED_VERIFY( writer.Write( records.Data(), records.DataSize(), written ) && written == records.DataSize() );
	RED_FATAL_ASSERT( header.pathsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( pathsBuffer.Data(), pathsOffset, written ) && written == pathsOffset );
	const auto endOfFile = writer.Tell();

	// Write out correct header now that we've written the entire file out to disk
	writer.Seek( 0, io::eSeekOrigin_Set );
	RED_VERIFY( writer.Write( &header, sizeof( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	writer.Flush();
	writer.Truncate( endOfFile );
	writer.Close();

	RED_LOG( "DependencyCache: Saving Dependency Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

//-----------------------------------------------------------------------------

bool Cache::ComputeFileHash( const res::ResourcePath& path, dependency::Hash& hash )
{
	// Use serial file IO interface as we might be running this from a job
	auto file = m_depot->CreateResourceReader( path );
	if ( file )
	{
		// TODO: Optimise this to either read/process the file in batches or use 
		// memory mapped IO instead and treat the file as a simple buffer
		Uint32 fileSize = static_cast<Uint32>( file->GetSize() );
		red::UniqueBuffer buffer = red::CreateUniqueBuffer< red::PoolBackend >( fileSize, 1 );
		RED_FATAL_ASSERT( buffer );

		file->Serialize( buffer.Get(), buffer.GetSize() );
		if ( !file->HasErrors() )
		{
			hash = Hash::Compute( buffer );
			return true;
		}
	}

	hash.Clear();
	return false;
}

bool Cache::Lookup( FileInfo& info )
{
	CacheKey key = info.path;
	FileInfo cachedInfo;
	bool updated = false;

	{
		red::ScopedLock< red::RWSpinLock > lock( m_guard );

		// Ensure cached entry exists
		auto iter = m_basicInfoCache.Find( key );
		if ( iter == m_basicInfoCache.End() )
		{
			FileInfo* value = RED_NEW( FileInfo, m_basicInfoStorage )( info );
			auto result = m_basicInfoCache.Set( key, value );
			RED_FATAL_ASSERT( result.IsSuccessful() );
			iter = result.Iterator();
		}
		cachedInfo = *iter.Value();
	}

	//RED_FATAL_ASSERT( cachedInfo->GetPath() == info->GetPath() );

	// Check that the cached values are up to date
	Uint64 timestamp = m_depot->GetResourceTimestamp( cachedInfo.path );
	const bool exists = timestamp != 0;
	if ( exists )
	{
		if ( timestamp != cachedInfo.timestamp )
		{
			// Timestamps differ, so update the stored hash with the value read from disk
			cachedInfo.timestamp = timestamp;

			if ( !ComputeFileHash( cachedInfo.path, cachedInfo.hash ) )
			{
				// Reset the timestamp so the file can get hashed again at a later date
				cachedInfo.timestamp = 0ull;
			}

			updated = true;
		}
	}
	else
	{
		cachedInfo.timestamp = 0ull;
		cachedInfo.hash.Clear();

		updated = true;
	}

	if ( updated )
	{
		// Update information computed
		red::ScopedLock< red::RWSpinLock > lock( m_guard );
		*m_basicInfoCache.GetRef( key ) = cachedInfo;
	}

	info = cachedInfo;
	return exists;
}

bool Cache::Find( FileInfo& info ) const
{
	CacheKey key = info.path;

	red::ScopedLock< red::RWSpinLock > lock( m_guard );

	auto iter = m_basicInfoCache.Find( key );
	if ( iter != m_basicInfoCache.End() )
	{
		info = *iter.Value();
		return true;
	}

	return false;
}

void Cache::Set( const FileInfo& info )
{
	CacheKey key = info.path;

	red::ScopedLock< red::RWSpinLock > lock( m_guard );

	auto iter = m_basicInfoCache.Find( key );
	if ( iter == m_basicInfoCache.End() )
	{
		FileInfo* value = RED_NEW( FileInfo, m_basicInfoStorage )( info );
		auto result = m_basicInfoCache.Set( key, value );
		RED_FATAL_ASSERT( result.IsSuccessful() );
	}
	else
	{
		*iter.Value() = info;
	}
}


} // dependency
