/**
* Copyright (c) 2019-20 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "file.h"
#include "../../redMemory/include/uniqueBuffer.h"

namespace fs
{
	class RED_FILESYSTEM_API ChunkedLZ4FileReader final : public IFile
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		explicit ChunkedLZ4FileReader( IFile& reader );
		~ChunkedLZ4FileReader();

		void Init();

		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

	private:
		struct ChunkMetadata
		{
			Uint32 compressedOffset;	// Position of data in raw file
			Uint32 compressedSize;		// Size of data in raw file
			Uint32 uncompressedOffset;	// Calculated at runtime
			Uint32 uncompressedSize;	// Size after decompression

			Bool operator<( const ChunkMetadata& metadata ) const
			{
				return compressedOffset < metadata.compressedOffset;
			}
		};

		using ChunkHeaderMetadata = red::SortedArray< ChunkMetadata >;

		Bool LoadChunkMetadata();
		void PrepareForReading();
		Bool DecompressDataForReading( ChunkHeaderMetadata::iterator chunk );

		IFile& m_reader;
		
		ChunkHeaderMetadata m_chunkHeaderData;
		
		Uint32 m_firstChunkOffset;									// First chunk data address (for seeking)
		Uint32 m_decompressedFileSize;								// Size of the file if it was decompressed
		red::UniqueBuffer m_compressedBuffer;						// Keep a buffer around for loading compressed data

		// Store info about the current chunk we have in-memory
		red::DynArray< Uint8 > m_decompressedBuffer;				// Current chunk decompressed buffer
		ChunkHeaderMetadata::iterator m_currentChunk;				// Current chunk we have decompressed
		Uint32 m_localChunkOffset;									// Offset into decompressed buffer
	};
}