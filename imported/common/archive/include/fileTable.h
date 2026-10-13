/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "common.h"
#include "fileMetadata.h"

#include "../../../common/redContainers/include/map.h"
#include "../../../common/redContainers/include/arraySpan.h"

namespace red
{
class BlobView;
}

namespace archive
{
class StringTable;
class FileWriter;

class ARCHIVE_API FileTable
{
	RED_USE_MEMORY_POOL( PoolArchive );
public:
	FileTable();
	FileTable( FileTable&& other );
	~FileTable();

	FileTable& operator=( FileTable&& other );

	void Clear();

	LoadingResult Load( const red::BlobView& fileTableData, Uint32 archivesVersion, red::DynArray< res::ResourcePath >& existingResources );
	SavingResult Save( FileWriter& writer );

	bool Empty() const;
	Uint32 Count() const;

	bool Exists( const FileIDCode id ) const;
	bool Find( const FileIDCode id, FileMetadata& result ) const;
	bool FindClosest( const FileIDCode id, Uint64 position, FileMetadata& result ) const;

	red::ArraySpan< const FileMetadata > GetFileList() const;

	red::ArraySpan< const FileSegment > GetAllFileSegments() const;
	red::ArraySpan< const Uint64 > GetAllDependencies() const;

#ifndef RED_READONLY_ARCHIVES
	void Reserve( Uint32 fileCount );

	void Add( const FileMetadata& file );

	// Only used when loaded for OpenMode::INSPECTION
	Uint64 GetCRC() const;
#endif

private:
	LoadingResult Load_OldVersion(const red::BlobView& fileTableData, Uint32 archivesVersion);

	typedef red::Map< FileIDCode, FileMetadata > FileEntryMap;

	FileEntryMap m_files;
	red::DynArray< FileSegment > m_segments;
	red::DynArray< Uint64 > m_resourceDependencies;
	Uint64 m_crc;
};

} // namespace archive
