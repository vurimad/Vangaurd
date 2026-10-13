/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileTable.h"
#include "fileFormat.h"
#include "fileIOInterfaces.h"
#include "utils.h"

#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redSystem/include/crc.h"

namespace archive
{

FileTable::FileTable()
	: m_files( archive::PoolArchive() ) 
	, m_segments( archive::PoolArchive() )
	, m_resourceDependencies( archive::PoolArchive() )
	, m_crc( 0ull )
{
}

FileTable::FileTable( FileTable&& other )
	: m_files( std::move( other.m_files ) )
	, m_segments( std::move( other.m_segments ) )
	, m_resourceDependencies( std::move( other.m_resourceDependencies ) )
	, m_crc( other.m_crc )
{
	other.m_crc = 0ull;
}

FileTable::~FileTable()
{
}

FileTable& FileTable::operator=( FileTable&& other )
{
	if ( this != &other )
	{
		m_files = std::move( other.m_files );
		m_segments = std::move( other.m_segments );
		m_resourceDependencies = std::move( other.m_resourceDependencies );
		m_crc = other.m_crc;
		other.m_crc = 0ull;
	}
	return *this;
}

static fileformat::FileTableEntry UpgradeOldFileTableEntry( const fileformat::old::FileTableEntry& entry )
{
	fileformat::FileTableEntry result;
	result.id = entry.id;
	result.timestamp = entry.timestamp;
	result.numInlineBufferSegments = entry.numInlineBufferSegments;
	result.segmentsStart = entry.segmentsStart;
	result.segmentsEnd = entry.segmentsEnd;
	result.resourceDependenciesStart = 0;
	result.resourceDependenciesEnd = 0;
	red::Memcpy( &result.hash, &entry.hash, sizeof(entry.hash) );
	return result;
}

LoadingResult FileTable::Load_OldVersion(const red::BlobView& fileTableData, Uint32 archivesVersion)
{
	RED_FATAL_ASSERT(archivesVersion == 11);

	if (fileTableData.Size() <= sizeof(fileformat::old::FileTableHeader))
	{
		RED_LOG_ERROR("File table data in the archive is too small to contain the header");
		return LoadingResult::INVALID_ARCHIVE;
	}

	const auto& header = fileTableData.As< const fileformat::old::FileTableHeader >();

	// Check CRC first
	const Uint64 crc = red::CalculateCRC64(fileTableData.Data(sizeof(header.crc)), fileTableData.Size() - sizeof(header.crc));
	if (crc != header.crc)
	{
		RED_LOG_ERROR("Calculated File Table CRC differs from the CRC read from the archive");
		return LoadingResult::INVALID_CRC;
	}
	m_crc = header.crc;

	const Uint32 filesOffset = sizeof(fileformat::old::FileTableHeader);
	const Uint32 segmentsOffset = filesOffset + sizeof(fileformat::old::FileTableEntry) * header.fileEntryCount;
	const Uint32 dependencyBufferOffset = segmentsOffset + sizeof(fileformat::old::FileTableSegmentEntry) * header.fileSegmentCount;

	const Uint32 totalSize =
		sizeof(fileformat::old::FileTableHeader) +
		sizeof(fileformat::old::FileTableEntry) * header.fileEntryCount +
		sizeof(fileformat::old::FileTableSegmentEntry) * header.fileSegmentCount;

	if (totalSize > fileTableData.Size())
	{
		RED_LOG_ERROR("Error found while processing File Table entries");
		return LoadingResult::INVALID_ARCHIVE;
	}

	// Get ArraySpan for file entries
	auto files = fileTableData.AsArray< const fileformat::old::FileTableEntry >(filesOffset, header.fileEntryCount);
	RED_FATAL_ASSERT(header.fileEntryCount < 1000 * 1000, "File table in archive seems to contain too many files");
	m_files.Reserve(header.fileEntryCount);

	// Copy segments into memory that we will keep alive
	m_segments.Reserve( header.fileSegmentCount );
	m_segments.PushBack(fileTableData.AsArray< const fileformat::old::FileTableSegmentEntry >(segmentsOffset, header.fileSegmentCount));
	red::ArraySpan< fileformat::old::FileTableSegmentEntry > segments = m_segments;

#ifdef RED_READONLY_ARCHIVES
	red::IndexRange nullEntryDependencies;
#else
	red::ArraySpan< const Uint64 > nullEntryDependencies;
#endif

	for ( const auto& entry : files )
	{
#ifdef RED_READONLY_ARCHIVES
		auto entrySegments = red::IndexRange(entry.segmentsStart, entry.segmentsEnd);
		m_files.InsertUnsorted( entry.id, FileMetadata( UpgradeOldFileTableEntry( entry ), entrySegments, nullEntryDependencies ) );
#else
		auto entrySegments = segments.Slice(entry.segmentsStart, entry.segmentsEnd);
		m_files.InsertUnsorted( entry.id, FileMetadata( UpgradeOldFileTableEntry( entry ), entrySegments, nullEntryDependencies ) );
#endif
	}
	m_files.MakeClean();

	return LoadingResult::SUCCESSFUL;
}


template <typename Callback>
void ForEveryValidFileInArchive( const red::ArraySpan< const fileformat::FileTableEntry >& archiveFiles, const red::DynArray< res::ResourcePath >& existingResources, Callback&& func )
{
	auto archiveIter = archiveFiles.begin(), archiveEnd = archiveFiles.end();
	auto existingIter = existingResources.Begin(), existingEnd = existingResources.End();
	while ( archiveIter != archiveEnd && existingIter != existingEnd )
	{
		if ( archiveIter->id < existingIter->GetHash() )
		{
			func( *archiveIter );
			++archiveIter;
		}
		else if ( archiveIter->id == existingIter->GetHash() )
		{
			++archiveIter;
			++existingIter;
		}
		else // if ( archiveIter->id > existingIter->GetHash() )
		{
			++existingIter;
		}
	}

	while ( archiveIter != archiveEnd )
	{
		func( *archiveIter );
		++archiveIter;
	}
}

LoadingResult FileTable::Load( const red::BlobView& fileTableData, Uint32 archivesVersion, red::DynArray< res::ResourcePath >& existingResources )
{
	if (archivesVersion == fileformat::old::Version)
	{
		return Load_OldVersion(fileTableData, archivesVersion);
	}

	if ( fileTableData.Size() <= sizeof( fileformat::FileTableHeader ) )
	{
		RED_LOG_ERROR( "File table data in the archive is too small to contain the header" );
		return LoadingResult::INVALID_ARCHIVE;
	}

	const auto& header = fileTableData.As< const fileformat::FileTableHeader >();

	// Check CRC first
	const Uint64 crc = red::CalculateCRC64( fileTableData.Data( sizeof(header.crc) ), fileTableData.Size() - sizeof(header.crc) );
	if ( crc != header.crc )
	{
		RED_LOG_ERROR( "Calculated File Table CRC differs from the CRC read from the archive" );
		return LoadingResult::INVALID_CRC;
	}
	m_crc = header.crc;

	const Uint32 filesOffset = sizeof(fileformat::FileTableHeader);
	const Uint32 segmentsOffset = filesOffset + sizeof(fileformat::FileTableEntry) * header.fileEntryCount;
	const Uint32 dependencyBufferOffset = segmentsOffset + sizeof(fileformat::FileTableSegmentEntry) * header.fileSegmentCount;

	const Uint32 totalSize =
		sizeof(fileformat::FileTableHeader) +
		sizeof(fileformat::FileTableEntry) * header.fileEntryCount +
		sizeof(fileformat::FileTableSegmentEntry) * header.fileSegmentCount +
		sizeof(Uint64) * header.resourceDependencyCount;

	if ( totalSize > fileTableData.Size() )
	{
		RED_LOG_ERROR( "Error found while processing File Table entries" );
		return LoadingResult::INVALID_ARCHIVE;
	}

	// Get ArraySpan for file entries
	const auto archiveFiles = fileTableData.AsArray< const fileformat::FileTableEntry >( filesOffset, header.fileEntryCount );
	RED_FATAL_ASSERT( header.fileEntryCount < 1000*1000, "File table in archive seems to contain too many files" );

	const auto archiveSegments = fileTableData.AsArray< const fileformat::FileTableSegmentEntry >( segmentsOffset, header.fileSegmentCount );
	RED_FATAL_ASSERT( header.fileSegmentCount > 0, "Must have some file segments in a non-empty archive" );

	const auto archiveDeps = fileTableData.AsArray< const Uint64 >( dependencyBufferOffset, header.resourceDependencyCount );

#ifdef RED_READONLY_ARCHIVES

	// NOTE: Here we're in production mode so we want to organise this archive's data in the most memory efficient way possible
	// So we will be only taking files that we don't have in other archives and only keeping those file segments and dependencies

	// Figure out how many files, segments, and dependencies we'll actually need to be able to load from the archive
	Uint32 filesCount = 0;
	Uint32 segmentsCount = 0;
	Uint32 depsCount = 0;

	ForEveryValidFileInArchive( archiveFiles, existingResources, [&filesCount, &segmentsCount, &depsCount]( const fileformat::FileTableEntry& entry )
	{
		++filesCount;
		segmentsCount += entry.segmentsEnd - entry.segmentsStart;
		depsCount += entry.resourceDependenciesEnd - entry.resourceDependenciesStart;
	} );

	m_files.Reserve( filesCount );
	m_segments.Reserve( segmentsCount );
	m_resourceDependencies.Reserve( depsCount );

	red::DynArray< res::ResourcePath > currentArchivePaths{ red::PoolEngine() };
	currentArchivePaths.Reserve( filesCount );

	// Copy values from file segments and dependencies arrays into the one's we've allocated
	ForEveryValidFileInArchive( archiveFiles, existingResources, [&files = m_files, &segments = m_segments, &deps = m_resourceDependencies, &archiveSegments, &archiveDeps, &currentArchivePaths]( const fileformat::FileTableEntry& entry )
	{
		const auto fileSegments = archiveSegments.Slice( entry.segmentsStart, entry.segmentsEnd );
		const Uint32 segStart = segments.Size();
		segments.PushBack( fileSegments );
		const red::IndexRange segmentRange{ segStart, segments.Size() };

		const auto fileDeps = archiveDeps.Slice( entry.resourceDependenciesStart, entry.resourceDependenciesEnd );
		const Uint32 depsStart = deps.Size();
		deps.PushBack( fileDeps );
		const red::IndexRange depsRange{ depsStart, deps.Size() };

		files.InsertUnsorted( entry.id, FileMetadata( entry, segmentRange, depsRange ) );
		currentArchivePaths.PushBack( res::ResourcePath::Build( entry.id ) );
	} );

	m_files.MakeClean();

	{
		RED_ASSERT( std::is_sorted( currentArchivePaths.Begin(), currentArchivePaths.End() ) );
		RED_ASSERT( std::is_sorted( existingResources.Begin(), existingResources.End() ) );

		Uint32 middleIndex = existingResources.Size();
		existingResources.PushBack( currentArchivePaths );
		std::inplace_merge( existingResources.Begin(), existingResources.Begin() + middleIndex, existingResources.End() );
		// Note: should be unique and sorted because currentArchivePaths is only ever paths not in existingResources
	}

	//RED_LOG( "Archive: Files %u/%u Segments %u/%u Deps %u/%u", filesCount, archiveFiles.Size(), segmentsCount, archiveSegments.Size(), depsCount, archiveDeps.Size() );
#else

	// NOTE: Here we don't bother to resize the arrays since by definition we're running in development mode

	// Copy segments into memory that we will keep alive
	m_segments.Reserve( header.fileSegmentCount );
	m_segments.PushBack( archiveSegments );
	red::ArraySpan< fileformat::FileTableSegmentEntry > segments = m_segments;

	// Copy optional dependencies into memory that we will keep alive
	if ( header.resourceDependencyCount > 0 )
	{
		m_resourceDependencies.Reserve( header.resourceDependencyCount );
		m_resourceDependencies.PushBack( archiveDeps );
	}
	red::ArraySpan< const Uint64 > dependencies = m_resourceDependencies;

	m_files.Reserve( header.fileEntryCount );
	for ( const auto& entry : archiveFiles )
	{
		auto entrySegments = segments.Slice( entry.segmentsStart, entry.segmentsEnd ); 
		auto entryDependencies = dependencies.Slice( entry.resourceDependenciesStart, entry.resourceDependenciesEnd );
		m_files.InsertUnsorted( entry.id, FileMetadata( entry, entrySegments, entryDependencies ) );
	}
	m_files.MakeClean();

#endif

	return LoadingResult::SUCCESSFUL;
}

SavingResult FileTable::Save( FileWriter& writer )
{
#ifdef RED_READONLY_ARCHIVES

	return SavingResult::NOT_IMPLEMENTED;

#else // RED_READONLY_ARCHIVES

	const Uint64 headerPosition = writer.Tell();
	RED_FATAL_ASSERT( ( headerPosition % fileformat::IndexHeader::TABLE_ALIGNMENT ) == 0,
		"Writer needs to be aligned to a multiple of %u to write the string table!",
		fileformat::IndexHeader::TABLE_ALIGNMENT );

	m_files.MakeClean();

	red::DynArray< fileformat::FileTableEntry > files{ red::PoolBackend() };
	files.Reserve( m_files.Size() );
	red::DynArray< fileformat::FileTableSegmentEntry > segments{ red::PoolBackend() };
	segments.Reserve( m_files.Size() * 2 );
	
	Uint32 numDeps = 0;
	red::DynArray< Uint64 > resourceDependencies{ red::PoolBackend() };
	for ( const auto& file : m_files.Values() )
	{
		numDeps += file.GetResourceDependencyCount();
	}
	resourceDependencies.Reserve( numDeps );

	for ( const auto& file : m_files.Values() )
	{
		files.PushBack( file.CreateFileTableEntry() );

		fileformat::FileTableEntry& entry = files.Back();
		entry.segmentsStart = segments.Size();
		for ( const auto& segment : file.GetSegments( *this ) )
		{
			segments.PushBack( segment );
		}
		entry.segmentsEnd = segments.Size();

		entry.resourceDependenciesStart = resourceDependencies.Size();
		for ( Uint64 depHash : file.GetResourceDependencies( *this ) )
		{
			resourceDependencies.PushBack( depHash );
		}
		entry.resourceDependenciesEnd = resourceDependencies.Size();
	}

	// Do not need to sort here because the Map should guarantee that the 
	// values are sorted by the key. In this case the key is the hash file id which we
	// want the entries sorted by.

	RED_FATAL_ASSERT( files.DataSize() == sizeof(fileformat::FileTableEntry) * m_files.Size(),
		"Size of the data to be written does not match expected size of the file table entries" );

	fileformat::FileTableHeader header;
	header.fileEntryCount = files.Size();
	header.fileSegmentCount = segments.Size();
	header.resourceDependencyCount = resourceDependencies.Size();
	Uint64 crc = red::CalculateCRC64( &header.fileEntryCount, sizeof(fileformat::FileTableHeader) - sizeof(fileformat::FileTableHeader::crc) );
	crc = red::CalculateCRC64( files.Data(), files.DataSize(), crc );
	crc = red::CalculateCRC64( segments.Data(), segments.DataSize(), crc );
	crc = red::CalculateCRC64( resourceDependencies.Data(), resourceDependencies.DataSize(), crc );
	header.crc = crc;
	m_crc = header.crc;

	if ( !writer.Write( &header, sizeof(fileformat::FileTableHeader) ) )
	{
		RED_LOG_ERROR( "Error writing File Table header to the archive" );
		return SavingResult::ERROR_IO;
	}

	if ( !writer.Write( files.Data(), files.DataSize() ) )
	{
		RED_LOG_ERROR( "Error writing File Table entries to the archive" );
		return SavingResult::ERROR_IO;
	}

	if ( !writer.Write( segments.Data(), segments.DataSize() ) )
	{
		RED_LOG_ERROR( "Error writing File Table segments to the archive" );
		return SavingResult::ERROR_IO;
	}

	if (!writer.Write( resourceDependencies.Data(), resourceDependencies.DataSize() ) )
	{
		RED_LOG_ERROR( "Error writing dependecies to the archive" );
		return SavingResult::ERROR_IO;
	}

	return SavingResult::SUCCESSFUL;
#endif // RED_READONLY_ARCHIVES
}

void FileTable::Clear()
{
	m_files.Clear();
	m_files.Shrink();
	m_crc = 0ull;
}

bool FileTable::Empty() const
{
	return m_files.Empty();
}

Uint32 FileTable::Count() const
{
	return m_files.Size();
}


bool FileTable::Exists( const FileIDCode id ) const
{
	return m_files.KeyExist( id );
}

bool FileTable::Find( const FileIDCode id, FileMetadata& result ) const
{
	return m_files.Find( id, result );
}

// #todo: store in sorted order, then can break sooner, although won't be guaranteed the actual closest data, just closest lowest offset
bool FileTable::FindClosest( const FileIDCode id, Uint64 position, FileMetadata& result ) const
{
	auto it = m_files.Find( id );
	if ( it == m_files.End() )
	{
		result = FileMetadata{};
		return false;
	}

	auto bestResult = it;
	for ( ++it; it != m_files.End() && it.Key() == id; ++it )
	{
		const auto& data = it.Value();
		RED_FATAL_ASSERT( !data.GetSegments( *this ).Empty() );

		// Absolute values, assuming we preload dependencies so it doesn't matter if before or after
		Int64 prevDiff = math::Abs( static_cast<Int64>( position ) - static_cast<Int64>( bestResult.Value().GetSegments( *this ).Front().position ) );
		Int64 curDiff = math::Abs( static_cast<Int64>( position ) - static_cast<Int64>( data.GetSegments( *this ).Front().position ) );
		if ( curDiff < prevDiff )
		{
			bestResult = it;
		}
	}

	RED_FATAL_ASSERT( bestResult != m_files.End() );

	result = bestResult.Value();
	return true;
}

red::ArraySpan< const FileSegment > FileTable::GetAllFileSegments() const
{
	return m_segments;
}

red::ArraySpan< const Uint64 > FileTable::GetAllDependencies() const
{
	return m_resourceDependencies;
}

#ifndef RED_READONLY_ARCHIVES

void FileTable::Reserve( Uint32 fileCount )
{
	m_files.Reserve( fileCount );
	m_segments.Reserve( fileCount * 2 ); // Estimate based on average 2 segments per file
	m_resourceDependencies.Reserve( fileCount * 2 ); // Total and complete guess
}

void FileTable::Add( const FileMetadata& file )
{
	m_files.InsertUnsorted( file.GetIDCode(), file );
}

Uint64 FileTable::GetCRC() const
{
	return m_crc;
}

#endif // RED_READONLY_ARCHIVES

red::ArraySpan< const FileMetadata > FileTable::GetFileList() const
{
	return m_files.Values();
}


} // namespace archive
