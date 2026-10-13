/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/blob.h"

namespace archive
{
	class FileMetadata;
	class FileTable;
}

namespace serialization
{
class BufferHandleRO;

class IAsyncSource;
using AsyncSourceUniquePtr = red::UniquePtr<IAsyncSource>;

// Create a memory based async source
// This uses the passed in buffer as the data and frees it once the last reference to the async source has been released
RED_RESOURCE_API AsyncSourceUniquePtr CreateMemoryAsyncSource( red::UniqueBuffer&& buffer );
RED_RESOURCE_API AsyncSourceUniquePtr CreateMemoryAsyncSource( const BufferHandleRO& bufferHandle );
RED_RESOURCE_API AsyncSourceUniquePtr CreateMemoryAsyncSource( const red::BlobView& bufferView );

// Create a file based async source 
// This uses async IO with the given file handle to read data from loose files on disk
RED_RESOURCE_API AsyncSourceUniquePtr CreateFileAsyncSource( const res::ResourcePath& resourcePathForDebug, io::TFileHandle fileHandle );

// Create an archive based async source
// This uses async IO to read the data of the file from the archive
RED_RESOURCE_API AsyncSourceUniquePtr CreateArchiveAsyncSource( const red::BlobView& archiveBlobView, io::TFileHandle fileHandle, const archive::FileMetadata& metadata, const archive::FileTable& table );

RED_RESOURCE_API void TEMPHACK_SetBinkVideoOpen_ThreadLocal(Bool value);

} // namespace serialization
