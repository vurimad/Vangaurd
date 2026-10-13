/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "common.h"
#include "fileId.h"

#ifdef RED_READONLY_ARCHIVES
#include "../../redContainers/include/indexRange.h"
#endif

namespace archive
{
class FileTable;

namespace fileformat
{
struct FileTableEntry;
}

#ifdef RED_READONLY_ARCHIVES

// Represents the file inside of the archive, actually stores only the metadata
class ARCHIVE_API FileMetadata
{
	RED_USE_MEMORY_POOL( PoolArchive );

public:
	FileMetadata();
	explicit FileMetadata( const FileID& id );
	~FileMetadata();

	FileMetadata( const fileformat::FileTableEntry& data, red::IndexRange segments, red::IndexRange resourceDependencies );

	FileMetadata( const FileMetadata& ) = default;
	FileMetadata( FileMetadata&& ) = default;
	FileMetadata& operator=( const FileMetadata& ) = default;
	FileMetadata& operator=( FileMetadata&& ) = default;

	bool IsValid() const;

	const FileID& GetID() const;
	FileIDCode GetIDCode() const;

	Uint32 GetSegmentCount() const;
	Uint32 GetResourceDependencyCount() const;
	red::ArraySpan< const FileSegment > GetSegments( const FileTable& table ) const;
	red::ArraySpan< const Uint64 > GetResourceDependencies( const FileTable& table ) const;

	Uint64 GetTimestamp() const;

	Uint32 GetNumInlinedBufferSegments() const;

	const char* ToDebugString() const;

private:
	FileID m_id;

	red::IndexRange m_segments;
	red::IndexRange m_resourceDependencies;
	Uint32 m_numInlinedBufferSegments;
};

#else // !RED_READONLY_ARCHIVES

class StringTable;

// Represents the file inside of the archive, actually stores only the metadata
class ARCHIVE_API FileMetadata
{
	RED_USE_MEMORY_POOL( PoolArchive );

public:
	FileMetadata();
	explicit FileMetadata( const FileID& id, Uint64 timestamp = 0ull );
	~FileMetadata();

	FileMetadata( const fileformat::FileTableEntry& data, red::ArraySpan< const FileSegment > segments, red::ArraySpan< const Uint64 > resourceDependencies );

	FileMetadata( const FileMetadata& ) = default;
	FileMetadata( FileMetadata&& ) = default;
	FileMetadata& operator=( const FileMetadata& ) = default;
	FileMetadata& operator=( FileMetadata&& ) = default;

	fileformat::FileTableEntry CreateFileTableEntry() const;

	bool IsValid() const;

	const FileID& GetID() const;
	FileIDCode GetIDCode() const;

	Uint32 GetTotalSizeInMemory() const;

	Uint64 GetPositionOnDisk() const;
	Uint32 GetTotalSizeOnDisk() const;
	Uint32 GetFirstSegmentSizeOnDisk() const;

	const FileHash& GetHash() const;
	void SetHash( const FileHash& hash );

	Uint32 GetSegmentCount() const;
	red::ArraySpan< FileSegment > GetSegments();
	red::ArraySpan< const FileSegment > GetSegments() const;
	red::ArraySpan< FileSegment > GetSegments( const FileTable& table );
	red::ArraySpan< const FileSegment > GetSegments( const FileTable& table ) const;
	void SetSegments( red::ArraySpan< const FileSegment > segments );
	void AddSegment( const FileSegment& segment );

	void SetNumInlinedBufferSegments(Uint32 numSegments);
	Uint32 GetNumInlinedBufferSegments() const;

	void UpdateSegmentPosition( Uint64 position );

	void UpdateSegmentPositionAtIndex( Uint32 index, Uint64 position );
	void UpdateSegmentSizeOnDiskAtIndex( Uint32 index, Uint32 sizeOnDisk );

	Uint64 GetTimestamp() const;

	const char* ToDebugString() const;

	Uint32 GetResourceDependencyCount() const;
	red::ArraySpan< const Uint64 > GetResourceDependencies() const;
	red::ArraySpan< const Uint64 > GetResourceDependencies( const FileTable& table ) const;
	void SetResourceDependencies( const red::DynArray<Uint64>& deps );

private:
	FileID m_id;
	Uint64 m_timestamp;

	FileHash m_hash;

	mutable red::String m_debugPath;
	red::DynArray< FileSegment > m_segments;
	red::DynArray< Uint64 > m_resourceDependencies;
	Uint32 m_numInlinedBufferSegments;
};

RED_INLINE red::ArraySpan< FileSegment > FileMetadata::GetSegments()
{
	return m_segments;
}

RED_INLINE red::ArraySpan< const FileSegment > FileMetadata::GetSegments() const
{
	return m_segments;
}

RED_INLINE red::ArraySpan< FileSegment > FileMetadata::GetSegments( const FileTable& table )
{
	RED_UNUSED( table );
	return m_segments;
}

RED_INLINE red::ArraySpan< const FileSegment > FileMetadata::GetSegments( const FileTable& table ) const
{
	RED_UNUSED( table );
	return m_segments;
}

RED_INLINE red::ArraySpan< const Uint64 > FileMetadata::GetResourceDependencies() const
{
	return m_resourceDependencies;
}

RED_INLINE red::ArraySpan< const Uint64 > FileMetadata::GetResourceDependencies( const FileTable& table ) const
{
	RED_UNUSED( table );
	return m_resourceDependencies;
}

RED_INLINE bool operator<( const FileMetadata& left, const FileMetadata& right )
{
	return left.GetIDCode() < right.GetIDCode();
}

#endif // !RED_READONLY_ARCHIVES

RED_INLINE const FileID& FileMetadata::GetID() const
{
	return m_id;
}

RED_INLINE FileIDCode FileMetadata::GetIDCode() const
{
	return m_id.GetCode();
}

RED_INLINE Uint32 FileMetadata::GetSegmentCount() const
{
	return m_segments.Size();
}

RED_INLINE Uint32 FileMetadata::GetResourceDependencyCount() const
{
	return m_resourceDependencies.Size();
}


} // namespace archive
