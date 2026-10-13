/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileMetadata.h"
#include "fileFormat.h"
#include "fileTable.h"

#include "../../../common/redSystem/include/crc.h"
#include "../../../common/redContainers/include/blob.h"

#include <numeric>

namespace archive
{

#ifdef RED_READONLY_ARCHIVES

FileMetadata::FileMetadata()
	: m_id()
{
}

FileMetadata::FileMetadata( const FileID& id )
	: m_id( id )
	, m_numInlinedBufferSegments( 0 )
{
}

FileMetadata::~FileMetadata() = default;

FileMetadata::FileMetadata( const fileformat::FileTableEntry& data, red::IndexRange segments, red::IndexRange resourceDependencies )
	: m_id( data.id )
	, m_segments( segments )
	, m_resourceDependencies( resourceDependencies )
	, m_numInlinedBufferSegments( data.numInlineBufferSegments )
{
}

bool FileMetadata::IsValid() const
{
	return m_id.GetCode() != 0ull;
}

Uint64 FileMetadata::GetTimestamp() const
{
	// Because other things consider 0 timestamp to be file missing
	return IsValid() ? 1ull : 0ull;
}

Uint32 FileMetadata::GetNumInlinedBufferSegments() const
{
	return m_numInlinedBufferSegments;
}

const char* FileMetadata::ToDebugString() const
{
	return "<no path>";
}

red::ArraySpan< const FileSegment > FileMetadata::GetSegments( const FileTable& table ) const
{
	return table.GetAllFileSegments().Slice( m_segments.StartIndex(), m_segments.EndIndex() );
}

red::ArraySpan< const Uint64 > FileMetadata::GetResourceDependencies( const FileTable& table ) const
{
	return table.GetAllDependencies().Slice( m_resourceDependencies.StartIndex(), m_resourceDependencies.EndIndex() );
}

#else // !RED_READONLY_ARCHIVES


FileMetadata::FileMetadata()
	: m_id()
	, m_timestamp( 0ull )
	, m_segments( archive::PoolArchive() )
	, m_resourceDependencies( archive::PoolArchive() )
	, m_numInlinedBufferSegments( 0 )
{
}

FileMetadata::FileMetadata( const FileID& id, Uint64 timestamp )
	: m_id( id )
	, m_timestamp( timestamp )
	, m_segments( archive::PoolArchive() )
	, m_resourceDependencies( archive::PoolArchive() )
	, m_numInlinedBufferSegments( 0 )
{
}

FileMetadata::~FileMetadata() = default;

FileMetadata::FileMetadata( const fileformat::FileTableEntry& data, red::ArraySpan< const FileSegment > segments, red::ArraySpan< const Uint64 > resourceDependencies )
	: m_id( data.id )
	, m_timestamp( data.timestamp )
	, m_segments( segments, archive::PoolArchive() )
	, m_resourceDependencies( resourceDependencies, archive::PoolArchive() )
	, m_numInlinedBufferSegments( data.numInlineBufferSegments )
{
	static_assert( FileHash::NUM_BYTES == 20, "Size of the file / dependency hash has changed, please verify this is correct!" );
	red::Memcpy( m_hash.value, data.hash, FileHash::NUM_BYTES );
}

fileformat::FileTableEntry FileMetadata::CreateFileTableEntry() const
{
	fileformat::FileTableEntry entry;
	entry.id = m_id.GetCode();
	entry.timestamp = m_timestamp;
	entry.numInlineBufferSegments = m_numInlinedBufferSegments;
	red::Memcpy( entry.hash, m_hash.value, FileHash::NUM_BYTES );

	// Leave this for the table to fill in
	entry.segmentsStart = entry.segmentsEnd = 0;
	entry.resourceDependenciesStart = entry.resourceDependenciesEnd = 0;

	return entry;
}

void FileMetadata::SetResourceDependencies(const red::DynArray<Uint64>& deps)
{
	m_resourceDependencies = deps;
}

bool FileMetadata::IsValid() const
{
	return m_id.GetCode() != 0ull && m_timestamp != 0ull;
}

Uint64 FileMetadata::GetTimestamp() const
{
	return m_timestamp;
}

Uint32 FileMetadata::GetTotalSizeInMemory() const
{
	return std::accumulate( m_segments.Begin(), m_segments.End(), 0u, 
		[]( Uint32 total, const FileSegment& segment ) -> Uint32 { return total + segment.sizeInMemory; } );
}

Uint64 FileMetadata::GetPositionOnDisk() const
{
	return m_segments.Front().position;
}

Uint32 FileMetadata::GetTotalSizeOnDisk() const
{
	return std::accumulate( m_segments.Begin(), m_segments.End(), 0u, 
		[]( Uint32 total, const FileSegment& segment ) -> Uint32 { return total + segment.sizeOnDisk; } );
}

Uint32 FileMetadata::GetFirstSegmentSizeOnDisk() const
{
	return m_segments.Empty() ? 0 : m_segments[0].sizeOnDisk;
}

const FileHash& FileMetadata::GetHash() const
{
	return m_hash;
}

void FileMetadata::SetHash( const FileHash& hash )
{
	m_hash = hash;
}

void FileMetadata::SetSegments( red::ArraySpan< const FileSegment > segments )
{
	m_segments = red::DynArray< FileSegment >( segments, archive::PoolArchive() );
}

void FileMetadata::AddSegment( const FileSegment& segment )
{
	m_segments.PushBack( segment );
}

void FileMetadata::SetNumInlinedBufferSegments( Uint32 numSegments )
{
	RED_FATAL_ASSERT( numSegments < m_segments.Size() );
	m_numInlinedBufferSegments = numSegments;
}

Uint32 FileMetadata::GetNumInlinedBufferSegments() const
{
	return m_numInlinedBufferSegments;
}

void FileMetadata::UpdateSegmentPosition( Uint64 position )
{
	for ( auto& segment : m_segments )
	{
		segment.position = position;
		position += segment.sizeOnDisk;
	}
}

void FileMetadata::UpdateSegmentPositionAtIndex( Uint32 index, Uint64 position )
{
	m_segments[ index ].position = position;
}

void FileMetadata::UpdateSegmentSizeOnDiskAtIndex( Uint32 index, Uint32 sizeOnDisk )
{
	m_segments[ index ].sizeOnDisk = sizeOnDisk;
}

const char* FileMetadata::ToDebugString() const
{
	if ( m_debugPath.Empty() )
	{
		m_debugPath = m_id.ToDebugString();
	}
	return m_debugPath.AsChar();
}

#endif // !RED_READONLY_ARCHIVES

} // namespace archive
