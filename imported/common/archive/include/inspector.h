/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "common.h"
#include "instance.h"
#include "fileTable.h"

#include "../../../common/redContainers/include/arraySpan.h"

namespace archive
{

#ifndef RED_READONLY_ARCHIVES

namespace fileformat
{
	struct MainHeader;
	struct IndexHeader;
}

class ARCHIVE_API Inspector
{
	RED_USE_MEMORY_POOL( archive::PoolArchive );

public:
	Inspector( const Instance& archive, const FileTable& fileTable );
	~Inspector();

	void Push( const fileformat::MainHeader& mainHeader );
	void Push( const fileformat::IndexHeader& indexHeader );

	Uint64 GetFileSize() const;
	Uint64 GetMetadataSize() const;
	FileSpan GetIndexSpan() const;
	FileSpan GetFileTableSpan() const;

	Uint32 GetArchiveVersion() const;

	Uint64 GetValidContentStart() const;
	Uint64 GetValidContentEnd() const;

	Uint64 GetFileTableCRC() const;

	red::ArraySpan< const FileMetadata > GetFileList() const;

	static Uint32 CalculatePaddingByteSize( FileSpan fileDataSpan );

private:
	Inspector( const Inspector& other );
	Inspector( Inspector&& other );

	Uint32 m_archiveVersion;

	Uint64 m_fileSize;
	FileSpan m_indexSpan;
	FileSpan m_fileTableSpan;

	const Instance& m_archive;
	const FileTable& m_fileTable;
};

#else // RED_READONLY_ARCHIVES

class Inspector
{
	RED_USE_MEMORY_POOL( archive::PoolArchive );
public:
	Inspector();
	~Inspector();
};

#endif // RED_READONLY_ARCHIVES

} // namespace archive
