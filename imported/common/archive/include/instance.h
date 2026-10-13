/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redMemory/include/uniquePtr.h"
#include "../../../common/redContainers/include/arraySpan.h"

#include "common.h"
#include "fileTable.h"

namespace red
{
class BlobView;
}

namespace archive
{
class FileReader;
class FileWriter;
class Inspector;

class ARCHIVE_API Instance
{
	RED_USE_MEMORY_POOL( archive::PoolArchive );

public:
	Instance( OpenMode openMode, FileReader& reader, FileWriter& writer );
	Instance( Instance&& other );
	~Instance();

	LoadingResult Load();
	LoadingResult Load( red::DynArray< res::ResourcePath >& existingResources );
	
	Bool LoadEntireArchiveIntoMemory(red::Blob& outBlob);

	bool FindFileClosestToOffset( const FileID& id, Uint64 offset, FileMetadata& result ) const;

#ifndef RED_READONLY_ARCHIVES
	SavingResult Save();
	void ClearHeader();
	void Clear();

	void Reserve( Uint32 filesCount );

	bool WriteFileSegment( red::BlobView data, FileSegment& segmentInfo, Uint32 alignStart, Uint32 alignSize );
	void AddFileToIndex( FileMetadata& metadata );

	// Only valid when opened for OpenMode::INSPECTION
	const Inspector& GetInspector() const;
#endif

	red::ArraySpan< const FileMetadata > AllFiles() const;

	const FileTable& GetFileTable() const
	{
		return m_fileTable;
	}

private:

	LoadingResult LoadIndex( const red::BlobView& indexData, Uint32 archivesVersion, red::DynArray< res::ResourcePath >& existingResources );

#ifndef RED_READONLY_ARCHIVES
	SavingResult SaveIndex( FileWriter& writer, const Uint64 position );
#endif

	FileReader& m_reader;
	FileWriter& m_writer;

	FileTable m_fileTable;

#ifndef RED_READONLY_ARCHIVES
	Uint64 m_fileDataStartPosition;
	Uint64 m_fileDataEndPosition;

	red::UniquePtr< Inspector > m_inspector;
#endif


	// I don't know what's up with this READ_ONLY_ARCHIVES, but kind of need to know this
	Uint64 m_fileDataEndPositionValidAfterLoading{ 0 };
	
	Uint32 m_version;
	OpenMode m_openMode;
};

namespace utils
{
	Uint64 InitialFileDataPosition();
}

} // namespace archive
