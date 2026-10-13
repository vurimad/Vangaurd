/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inspector.h"
#include "fileFormat.h"
#include "utils.h"

namespace archive
{

#ifndef RED_READONLY_ARCHIVES

Inspector::Inspector( const Instance& archive, const FileTable& fileTable )
	: m_archive( archive )
	, m_fileTable( fileTable )
{
}

Inspector::~Inspector()
{
}

void Inspector::Push( const fileformat::MainHeader& mainHeader )
{
	m_archiveVersion = mainHeader.version;
	m_fileSize = mainHeader.totalFileSize;
	m_indexSpan.position = mainHeader.indexPosition;
	m_indexSpan.size = static_cast<Uint32>( mainHeader.totalFileSize - mainHeader.indexPosition );
}

void Inspector::Push( const fileformat::IndexHeader& indexHeader )
{
	const Uint64 indexPosition = m_indexSpan.position;
	m_fileTableSpan.position = indexPosition + indexHeader.fileTableOffset;
	m_fileTableSpan.size = indexHeader.fileTableSize;
}

Uint64 Inspector::GetFileSize() const
{
	return m_fileSize;
}

Uint64 Inspector::GetMetadataSize() const
{
	Uint64 metadataSize = 0ull;
	metadataSize += sizeof(fileformat::MainHeader);
	metadataSize += sizeof(fileformat::IndexHeader);

	metadataSize += sizeof(fileformat::FileTableHeader);
	metadataSize += sizeof(fileformat::FileTableEntry) * m_fileTable.Count();

	return metadataSize;
}

FileSpan Inspector::GetIndexSpan() const
{
	return m_indexSpan;
}

FileSpan Inspector::GetFileTableSpan() const
{
	return m_fileTableSpan;
}

Uint32 Inspector::GetArchiveVersion() const
{
	return m_archiveVersion;
}

Uint64 Inspector::GetValidContentStart() const
{
	return archive::utils::InitialFileDataPosition();
}

Uint64 Inspector::GetValidContentEnd() const
{
	return m_indexSpan.position;
}

Uint64 Inspector::GetFileTableCRC() const
{
	return m_fileTable.GetCRC();
}

red::ArraySpan< const FileMetadata > Inspector::GetFileList() const
{
	return m_fileTable.GetFileList();
}

Uint32 Inspector::CalculatePaddingByteSize( FileSpan fileDataSpan )
{
	return utils::GetAlignedPaddingBytes( fileDataSpan.position + fileDataSpan.size, fileformat::GlobalAlignment );
}

#else // RED_READONLY_ARCHIVES

Inspector::Inspector() = default;
Inspector::~Inspector() = default;

#endif // RED_READONLY_ARCHIVES

} // namespace archive
