/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "directoryListingCache.h"

#include "../../redContainers/include/string/stringUtils.h"
#include "../../redFileSystem/include/system.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redSystem/include/stopWatch.h"

namespace res
{
namespace prv
{

//-----------------------------------------------------------------------------

DirectoryListingCache::DirectoryListingCache()
	: m_directories( red::PoolEngine() )
	, m_entryStrings( red::PoolEngine() )
{
	red::memory::DynamicLinearAllocatorParameter param{ 
		&red::memory::AcquireSystemAllocator(),
		RED_KILO_BYTE( 64 ),
		red::memory::Flags::Flags_CPU_Read_Write 
	};
	m_stringStorage.Initialize( param );
}

DirectoryListingCache::~DirectoryListingCache()
{
}

DirectoryListingCache::DirectoryEntry::DirectoryEntry()
	: timestamp( 0ull )
{}

//-----------------------------------------------------------------------------

void DirectoryListingCache::Clear()
{
	m_directories.Clear();
	m_entryStrings.Clear();
}

#pragma pack(push, 4)

namespace fileformat
{

//
// File Structure
//
// Header
// [ Record ... ]
// [ String Offsets ... ]
// [ Strings ... ]
//

struct Header
{
	Uint32 magic;
	Uint32 recordCount;
	Uint32 offsetsOffset;
	Uint32 offsetsCount;
	Uint32 stringsOffset;
	Uint32 stringsBytes;

	static constexpr Uint32 MAGIC_VALUE = ' CLD'; // Directory Listings Cache 'DLC ' backwards for endianness
};

struct Record
{
	Uint32 nameOffset;
	Uint64 timestamp;
	red::IndexRange directoryIndexes;
	red::IndexRange fileIndexes;
};

} // fileformat

#pragma pack(pop)

const char* DirectoryListingCache::GetDefaultFilename()
{
	return "DirectoryListingCache.cache";
}

bool DirectoryListingCache::Load( const red::AbsolutePath& cachePath )
{
	RED_SCOPE_LOCK( m_guard );

	red::StopWatch timer;

	red::Blob fileBuffer;
	if ( !red::LoadFileToBuffer( cachePath, fileBuffer ) )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Could not load the Directory Listing Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	if ( fileBuffer.Size() < sizeof(fileformat::Header) )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Invalid Directory Listing Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	const auto& header = fileBuffer.As< const fileformat::Header >();
	if ( header.magic != fileformat::Header::MAGIC_VALUE || 
		header.stringsOffset + header.stringsBytes > fileBuffer.Size() ||
		header.offsetsOffset + header.offsetsCount * sizeof( Uint32 ) > fileBuffer.Size() )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Invalid Directory Listing Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	if ( header.offsetsCount == 0 || header.recordCount == 0 || header.stringsBytes == 0 )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Empty Directory Listing Cache file '%hs'", cachePath.AsChar() );
		return false;
	}

	const auto* recordStart = fileBuffer.Pointer< const fileformat::Record >( sizeof( fileformat::Header ) );
	red::ArraySpan< const fileformat::Record > records{ recordStart, header.recordCount };

	const auto* offsetsStart = fileBuffer.Pointer< const Uint32 >( header.offsetsOffset );
	red::ArraySpan< const Uint32 > offsets{ offsetsStart, header.offsetsCount };

	red::BlobView pathStrings = fileBuffer.Range( header.stringsOffset, header.stringsBytes );

	m_directories.Reserve( m_directories.Size() + header.recordCount );
	m_entryStrings.Reserve( header.offsetsCount );
	for ( const auto& record : records )
	{
		const red::AbsolutePath path = red::AbsolutePath::CreateDirPath( pathStrings.Pointer< char >( record.nameOffset ) );

		DirectoryEntry entry;
		entry.timestamp = record.timestamp;

		const Uint32 dirStartIndex = m_entryStrings.Size();
		for ( Uint32 dirIndex : record.directoryIndexes )
		{
			const char* ptr = pathStrings.Pointer< const char >( offsets[ dirIndex ] );
			m_entryStrings.PushBack( SaveStringData( ptr ) );
		}
		const Uint32 dirEndIndex = m_entryStrings.Size();
		for ( Uint32 fileIndex : record.fileIndexes )
		{
			const char* ptr = pathStrings.Pointer< const char >( offsets[ fileIndex ] );
			m_entryStrings.PushBack( SaveStringData( ptr ) );
		}
		const Uint32 fileEndIndex = m_entryStrings.Size();

		entry.directoryIndexes = red::IndexRange( dirStartIndex, dirEndIndex );
		entry.fileIndexes = red::IndexRange( dirEndIndex, fileEndIndex );

		m_directories.Set( path, entry );
	}

	RED_LOG( "DirectoryListingCache: Loading Directory Listing Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

bool DirectoryListingCache::Save( const red::AbsolutePath& cachePath )
{
	RED_SCOPE_LOCK( m_guard );

	red::StopWatch timer;

	Uint32 written = 0;
	io::NativeFileHandle writer;
	if ( ! writer.Open( cachePath.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Could not open the cache file for writing '%hs'", cachePath.AsChar() );
		return false;
	}

	auto directoryPaths = m_directories.GetKeys();
	auto directoryEntries = m_directories.GetValues();
	RED_FATAL_ASSERT( directoryPaths.Size() == directoryEntries.Size() );

	Uint32 stringByteCount = 0;
	Uint32 stringIndexCount = 0;
	for ( Uint32 i = 0; i < directoryPaths.Size(); ++i )
	{
		stringByteCount += directoryPaths[i].Length() + 1;
		const DirectoryEntry& entry = directoryEntries[i];
		stringIndexCount += entry.fileIndexes.Size() + entry.directoryIndexes.Size();
	}
	for ( const auto& entry : m_entryStrings )
	{
		stringByteCount += entry.Length() + 1;
	}

	red::DynArray< fileformat::Record > records{red::PoolEngine()};
	records.Reserve( directoryPaths.Size() );
	 
	red::DynArray< Uint32 > stringOffsets{red::PoolEngine()};
	stringOffsets.Reserve( stringIndexCount );
	
	red::Blob stringsBuffer{ stringByteCount };
	Uint32 stringsOffset = 0;

	auto PushString = [&stringsOffset, &stringsBuffer]( const red::StringView str ) mutable -> Uint32
	{
		const Uint32 initialOffset = stringsOffset;
		const Uint32 strLength = str.Length();

		if ( strLength > 0 )
		{
			char* writePtr = stringsBuffer.Pointer<char>( stringsOffset );

			RED_FATAL_ASSERT( stringsOffset + strLength + 1 <= stringsBuffer.Size() );

			red::Memcpy( writePtr, str.Data(), strLength );
			writePtr[ strLength ] = '\0';
			stringsOffset += strLength + 1;
		}

		return initialOffset;
	};

	for ( Uint32 i = 0; i < directoryPaths.Size(); ++i )
	{
		fileformat::Record record;
		record.nameOffset = PushString( directoryPaths[i].AsStringView() );

		const DirectoryEntry& entry = directoryEntries[i];
		record.timestamp = entry.timestamp;

		const Uint32 dirStartIndex = stringOffsets.Size();
		for ( Uint32 dirIndex : entry.directoryIndexes )
		{
			stringOffsets.PushBack( PushString( m_entryStrings[ dirIndex ] ) );
		}

		const Uint32 dirEndIndex = stringOffsets.Size();
		for ( Uint32 fileIndex : entry.fileIndexes )
		{
			stringOffsets.PushBack( PushString( m_entryStrings[ fileIndex ] ) );
		}

		const Uint32 fileEndIndex = stringOffsets.Size();

		record.directoryIndexes = red::IndexRange( dirStartIndex, dirEndIndex );
		record.fileIndexes = red::IndexRange( dirEndIndex, fileEndIndex );

		records.PushBack( record );
	}

	// Write out an invalid header first so the file is only valid when completely written out
	fileformat::Header header;
	red::Memzero( &header, sizeof( fileformat::Header ) );
	RED_VERIFY( writer.Write( &header, sizeof( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	header.magic = fileformat::Header::MAGIC_VALUE;
	header.recordCount = records.Size();
	header.offsetsOffset = sizeof( fileformat::Header ) + ( sizeof( fileformat::Record ) * header.recordCount );
	header.offsetsCount = stringOffsets.Size();
	header.stringsOffset = header.offsetsOffset + ( sizeof( Uint32 ) * header.offsetsCount );
	header.stringsBytes = stringsOffset;

	RED_VERIFY( writer.Write( records.Data(), records.DataSize(), written ) && written == records.DataSize() );
	RED_FATAL_ASSERT( header.offsetsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( stringOffsets.Data(), stringOffsets.DataSize(), written ) && written == stringOffsets.DataSize() );
	RED_FATAL_ASSERT( header.stringsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( stringsBuffer.Data(), stringsOffset, written ) && written == stringsOffset );
	const auto endOfFile = writer.Tell();

	// Write out correct header now that we've written the entire file out to disk
	writer.Seek( 0, io::eSeekOrigin_Set );
	RED_VERIFY( writer.Write( &header, sizeof( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	writer.Flush();
	writer.Truncate( endOfFile );
	writer.Close();

	RED_LOG( "DirectoryListingCache: Saving Directory Listing Cache '%hs' took %0.3lf ms", cachePath.AsChar(), timer.GetDeltaMS() );

	return true;
}

//-----------------------------------------------------------------------------

void DirectoryListingCache::UpdateTree( const red::AbsolutePath& basePath )
{
	RED_FATAL_ASSERT( basePath.IsDirectoryPath(), "Must specify a directory to update!" );

	RED_SCOPE_LOCK( m_guard );

	if ( GFileManager->GetFileTimeStamp( basePath ) == 0ull )
	{
		RED_LOG_ERROR( "DirectoryListingCache: Trying to update a tree starting at a non-existant path '%hs'", basePath.AsChar() );
		return;
	}

	red::StopWatch timer;

	red::DynArray< red::AbsolutePath > pathsToSearch{red::PoolEngine()};
	pathsToSearch.Reserve( 1000 );
	pathsToSearch.PushBack( basePath );

	for ( Uint32 i = 0; i < pathsToSearch.Size(); ++i )
	{
		const auto& path = pathsToSearch[i];
		Uint64 timestamp = GFileManager->GetFileTimeStamp( path );
		RED_FATAL_ASSERT( timestamp != 0ull, "Expecting enumerated path '%hs' to have a valid timestamp!", path.AsChar() );

		auto iter = m_directories.Find( path );
		if ( iter == m_directories.End() )
		{
			auto result = m_directories.Set( path, {} );
			RED_FATAL_ASSERT( result.IsSuccessful() );
			iter = result.Iterator();
		}

		DirectoryEntry& entry = iter.Value();
		if ( timestamp != entry.timestamp )
		{
			UpdateEntry( path, entry );
			entry.timestamp = timestamp;
		}

		// Still need to iterate through child directories because their contents might have changed
		for ( const Uint32 index : entry.directoryIndexes )
		{
			red::AbsolutePath subDirPath = path.AddDirPath( m_entryStrings[ index ] );
			pathsToSearch.PushBack( subDirPath );
		}
	}

	RED_LOG( "DirectoryListingCache: Enumerated %u directories in %0.3 ms", pathsToSearch.Size(), timer.GetDeltaMS() );
}

red::StringView DirectoryListingCache::SaveStringData( red::StringView str )
{
	const Uint32 strLength = str.Length();
	char* strPlace = static_cast< char* >( RED_ALLOCATE( m_stringStorage, strLength + 1 ) );
	red::Memcpy( strPlace, str.Data(), strLength );
	strPlace[ strLength ] = '\0';
	return red::StringView( strPlace, strLength );
}

void DirectoryListingCache::UpdateEntry( red::AbsolutePath path, DirectoryEntry& entry )
{
	red::DynArray< red::StringView > directories{red::PoolEngine()};
	const red::DynArray< red::String > foundDirectoryPaths = GFileManager->FindDirectories( path );
	for ( const red::String& directoryPath : foundDirectoryPaths )
	{
		const char* directoryPathBuffer = directoryPath.AsChar();
		// Skip meta directory entries
		if ( red::Strcmp( directoryPathBuffer, "." ) == 0 || red::Strcmp( directoryPathBuffer, ".." ) == 0 )
		{
			continue;
		}
		const red::StringView str = SaveStringData( directoryPathBuffer );
		directories.PushBack( str );
	}

	red::DynArray< red::StringView > files{red::PoolEngine()};
	const red::DynArray< red::String > foundFilesPaths = GFileManager->FindFiles( path.AppendFilePath( "*.*" ) );
	for ( const red::String& filePath : foundFilesPaths )
	{
		const char* filePathBuffer = filePath.AsChar();
		const red::StringView str = SaveStringData( filePathBuffer );
		files.PushBack( str );
	}

	const Uint32 dirStartIndex = m_entryStrings.Size();
	m_entryStrings.PushBack( directories );
	const Uint32 dirEndIndex = m_entryStrings.Size();
	m_entryStrings.PushBack( files );
	const Uint32 fileEndIndex = m_entryStrings.Size();

	entry.directoryIndexes = red::IndexRange( dirStartIndex, dirEndIndex );
	entry.fileIndexes = red::IndexRange( dirEndIndex, fileEndIndex );
}

DirectoryListingCache::DirectoryMap::const_iterator DirectoryListingCache::FindEntry( const red::AbsolutePath& path )
{
	Uint64 timestamp = GFileManager->GetFileTimeStamp( path );
	auto iter = m_directories.Find( path );
	if ( iter == m_directories.End() )
	{
		if ( timestamp == 0ull )
		{
			return m_directories.End();
		}

		DirectoryEntry entry;
		entry.timestamp = timestamp;
		UpdateEntry( path, entry );

		auto result = m_directories.Set( path, entry );
		RED_FATAL_ASSERT( result.IsSuccessful() );
		iter = result.Iterator();
	}
	else
	{
		if ( timestamp == 0ull )
		{
			m_directories.Remove( iter );
			return m_directories.End();
		}

		DirectoryEntry& entry = iter.Value();
		if ( timestamp != entry.timestamp )
		{
			entry.timestamp = timestamp;
			UpdateEntry( path, entry );
		}
	}

	return iter;
}

void DirectoryListingCache::CreateOutputStrings( const red::IndexRange& range, const red::StringView pattern, red::DynArray< red::String >& output )
{
	output.Reserve( output.Size() + range.Size() );
	if ( pattern.Empty() )
	{
		for ( Uint32 index : range )
		{
			output.PushBack( m_entryStrings[ index ].ToString() );
		}
	}
	else
	{
		for ( Uint32 index : range )
		{
			red::StringView str = m_entryStrings[ index ];
			if ( red::GlobMatch( str, pattern ) )
			{
				output.PushBack( str.ToString() );
			}
		}
	}

}

void DirectoryListingCache::LookupDirectories( const red::AbsolutePath& searchPath, red::DynArray< String >& output )
{
	RED_SCOPE_LOCK( m_guard );

	auto iter = FindEntry( searchPath );
	if ( iter != m_directories.End() )
	{
		const DirectoryEntry& cacheEntry = iter.Value();
		output.Reserve( output.Size() + cacheEntry.directoryIndexes.Size() );
		for ( Uint32 index : cacheEntry.directoryIndexes )
		{
			output.PushBack( m_entryStrings[ index ].ToString() );
		}
	}
}

void DirectoryListingCache::LookupFiles( const red::AbsolutePath& searchPath, const char* pattern, red::DynArray< String >& output )
{
	RED_SCOPE_LOCK( m_guard );

	auto iter = FindEntry( searchPath );
	if ( iter != m_directories.End() )
	{
		const DirectoryEntry& cacheEntry = iter.Value();
		output.Reserve( output.Size() + cacheEntry.fileIndexes.Size() );
		if ( pattern != nullptr )
		{
			for ( Uint32 index : cacheEntry.fileIndexes )
			{
				red::StringView str = m_entryStrings[ index ];
				if ( red::GlobMatch( str, pattern ) )
				{
					output.PushBack( str.ToString() );
				}
			}
		}
		else
		{
			for ( Uint32 index : cacheEntry.fileIndexes )
			{
				output.PushBack( m_entryStrings[ index ].ToString() );
			}
		}
	}

}

//-----------------------------------------------------------------------------

red::DynArray< String > DirectoryListingCache::Debug_GetEntireTree()
{
	RED_SCOPE_LOCK( m_guard );

	red::DynArray< std::pair< red::AbsolutePath, red::IndexRange > > directories{ red::PoolEngine() };
	directories.Reserve( m_directories.Size() );

	Uint32 entriesCount = 0;
	for ( auto iter = m_directories.Begin(), end = m_directories.End(); iter != end; ++iter )
	{
		const auto& fileIndexes = iter.Value().fileIndexes;
		entriesCount += fileIndexes.Size();
		directories.PushBack( std::make_pair( iter.Key(), fileIndexes ) );
	}

	std::sort( directories.Begin(), directories.End(), []( const auto& left, const auto& right ) -> bool
	{
		return left.first < right.first;
	} );

	red::DynArray< String > result{ red::PoolEngine() };
	result.Reserve( entriesCount );

	for ( const auto& entry : directories )
	{
		const auto basePath = entry.first.AsStringView();
		for ( const Uint32 index : entry.second )
		{
			result.PushBack( red::StrCat( basePath, m_entryStrings[ index ] ) );
		}
	}

	return result;
}

} // prv
} // res
