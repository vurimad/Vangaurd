/**
* Copyright (c) 2019-20 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "file.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redCompression/include/compression.h"

namespace fs
{
	struct LZ4ChunkMetadata;

	class RED_FILESYSTEM_API ChunkedLZ4FileWriter final : public IFile
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ChunkedLZ4FileWriter( IFile& writer, Uint32 compressedChunkSize, Uint32 maximumChunks );
		~ChunkedLZ4FileWriter();

		void Init();

		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

	private:
		void InternalFlush();

		IFile& m_writer;
		red::UniqueBuffer m_chunkBuffer;
		char* m_chunkBufferPtr;
		red::UniqueBuffer m_compressedBuffer;
		compression::TCompressionAllocator m_compressionAllocator;
		Uint32 m_originalFileOffset;
		Uint32 m_bytesCached;
		Uint32 m_chunkStartOffset; // Start offset of the chunk data (uncompressed)
		Bool m_isSeeked;
		red::DynArray< LZ4ChunkMetadata > m_chunkHeaderData;
	};
}
