/**
* Copyright (c) 2019-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "chunkedLZ4FileReader.h"
#include "chunkedLZ4Utils.h"
#include "../../redCompression/include/compression.h"

namespace fs
{
	ChunkedLZ4FileReader::ChunkedLZ4FileReader( IFile& reader )
		: IFile( reader.GetFlags() )
		, m_reader( reader )
		, m_chunkHeaderData( red::PoolEngine() )
		, m_firstChunkOffset( 0 )
		, m_decompressedFileSize( 0 )
		, m_decompressedBuffer( red::PoolEngine() )
		, m_currentChunk( m_chunkHeaderData.End() )
		, m_localChunkOffset( 0 )
	{
	}

	ChunkedLZ4FileReader::~ChunkedLZ4FileReader() = default;

	void ChunkedLZ4FileReader::Init()
	{
		if ( LoadChunkMetadata() )
		{
			PrepareForReading();

			// Pre-cache first block if any available
			if ( !m_chunkHeaderData.Empty() )
			{
				DecompressDataForReading( m_chunkHeaderData.Begin() );
			}
		}
	}

	void ChunkedLZ4FileReader::Serialize( void* buffer, const size_t size )
	{
		RED_ASSERT( m_currentChunk != m_chunkHeaderData.End(), "No chunk to read from. Corrupted data?" );
		RED_ASSERT( !m_decompressedBuffer.Empty(), "No decompressed data to read form. Corrupted data?" );

		Uint32 bytesRead = 0;
		Uint8* targetBuffer = static_cast< Uint8* >( buffer );
		while ( bytesRead < size && m_decompressedBuffer.Size() > 0 && m_currentChunk != m_chunkHeaderData.End() )
		{
			// Clamp bytes to read to buffer remaining
			Uint32 bytesToRead = std::min( static_cast< Uint32 >( size ) - bytesRead, m_decompressedBuffer.Size() - m_localChunkOffset );

			// Read from local buffer
			if ( bytesToRead > 0 )
			{
				red::Memcpy( targetBuffer, m_decompressedBuffer.TypedData() + m_localChunkOffset, bytesToRead );
				m_localChunkOffset += bytesToRead;
				targetBuffer += bytesToRead;
				bytesRead += bytesToRead;
			}

			// If there is more to read in the next chunk, decompress it now
			if ( m_localChunkOffset >= m_decompressedBuffer.Size() && bytesRead < size )
			{
				if ( !DecompressDataForReading( m_currentChunk + 1 ) )
				{
					// Attempted to read beyond size
					return;
				}
			}
		}
	}

	Uint64 ChunkedLZ4FileReader::GetOffset() const
	{
		RED_ASSERT( !m_chunkHeaderData.Empty() && m_currentChunk != m_chunkHeaderData.End() );
		return m_currentChunk != m_chunkHeaderData.End() ? ( m_currentChunk->uncompressedOffset + m_localChunkOffset ) : 0;
	}

	Uint64 ChunkedLZ4FileReader::GetSize() const
	{
		return m_decompressedFileSize;
	}

	void ChunkedLZ4FileReader::Seek( const Int64 offset )
	{
		RED_ASSERT( offset >= 0 );

		Uint32 offset32 = static_cast< Uint32 >( offset );

		// Fast path - seeking in the same chunk
		if ( m_currentChunk != m_chunkHeaderData.End() )
		{
			if ( ( offset32 >= m_currentChunk->uncompressedOffset ) && ( offset32 < m_currentChunk->uncompressedOffset + m_currentChunk->uncompressedSize ) )
			{
				// // Found a match, seek internally
				m_localChunkOffset = offset32 - m_currentChunk->uncompressedOffset;
				return;
			}
		}

		// Slow path - search for matching chunk
		for ( auto chunk = m_chunkHeaderData.Begin(); chunk != m_chunkHeaderData.End(); ++chunk )
		{
			if ( ( offset32 >= chunk->uncompressedOffset ) && ( offset32 < chunk->uncompressedOffset + chunk->uncompressedSize ) )
			{
				// Found a match, decompress and seek
				DecompressDataForReading( chunk );
				m_localChunkOffset = offset32 - chunk->uncompressedOffset;
				return;
			}
		}

		// If we reach this point, then we can't handle the seek
		HandleIOError( "Attempted to seek (%u) outside of usable area, size is (%u)", offset32, GetSize() );
		RED_FATAL_ASSERT( 0, "Attempted to seek (%u) outside of usable area, size is (%u)", offset32, GetSize() );
	}

	void ChunkedLZ4FileReader::Flush()
	{
	}

	Bool ChunkedLZ4FileReader::LoadChunkMetadata()
	{
		Uint32 magic = 0;
		m_reader << magic;

		if ( magic != c_headerMarker )
		{
			HandleIOError( "Failed to load compressed chunk metadata" );
			RED_FATAL_ASSERT( 0, "Failed to load compressed chunk metadata. Corrupted file." );
			return false;
		}

		Uint32 chunkCount = 0;
		m_reader << chunkCount;

		for ( Uint32 chunk = 0; chunk < chunkCount; ++chunk )
		{
			LZ4ChunkMetadata metadata;
			m_reader.Serialize( &metadata, sizeof( metadata ) );

			// Uncompressed offset calculated later
			ChunkMetadata chunkData;
			chunkData.compressedOffset = metadata.compressedOffset;
			chunkData.compressedSize = metadata.compressedSize;
			chunkData.uncompressedSize = metadata.uncompressedSize;
			chunkData.uncompressedOffset = 0;
			m_chunkHeaderData.PushBack( chunkData );
		}

#if !defined( RED_CONFIGURATION_FINAL )
		// Validate that all compressed data are contiguous
		for ( Uint32 chunk = 0; chunk + 1 < m_chunkHeaderData.Size(); ++chunk )
		{
			RED_FATAL_ASSERT( ( m_chunkHeaderData[chunk].compressedOffset + m_chunkHeaderData[chunk].compressedSize ) == m_chunkHeaderData[chunk + 1].compressedOffset, "Chunks are not contiguous" );
		}
#endif

		return true;
	}

	void ChunkedLZ4FileReader::PrepareForReading()
	{
		// Calculate the largest decompressed buffer and pre-allocate
		Uint32 maxDecompressedBufferSize = 0;
		Uint32 maxCompressedBufferSize = 0;
		Uint32 firstChunkOffset = static_cast< Uint32 >( -1 );
		for ( const auto& chunk : m_chunkHeaderData )
		{
			maxDecompressedBufferSize = std::max( maxDecompressedBufferSize, chunk.uncompressedSize );
			maxCompressedBufferSize = std::max( maxCompressedBufferSize, chunk.compressedSize );
			firstChunkOffset = std::min( firstChunkOffset, chunk.compressedOffset );
		}

		// Pre-calculate uncompressed offsets
		Uint32 uncompressedOffset = firstChunkOffset;
		for ( auto& chunk : m_chunkHeaderData )
		{
			chunk.uncompressedOffset = uncompressedOffset;
			uncompressedOffset += chunk.uncompressedSize;
		}

		m_decompressedFileSize = uncompressedOffset;

		RED_LOG_INFO( "Pre-allocating %d bytes for ChunkedLZ4FileReader buffer (%d compressed, %d decompressed)",
			maxCompressedBufferSize + maxDecompressedBufferSize, maxCompressedBufferSize, maxDecompressedBufferSize );

		m_decompressedBuffer.Resize( maxDecompressedBufferSize );
		m_compressedBuffer = red::CreateUniqueBuffer< red::PoolEngine >( maxCompressedBufferSize, 8 );
		m_currentChunk = m_chunkHeaderData.End();
	}

	Bool ChunkedLZ4FileReader::DecompressDataForReading( const ChunkHeaderMetadata::iterator chunk )
	{
		if ( chunk == m_chunkHeaderData.End() )
		{
			HandleIOError( "Cannot decompress beyond last chunk, size is (%u)", GetSize() );
			RED_FATAL_ASSERT( 0, "Cannot decompress beyond last chunk, size is (%u)", GetSize() );
			return false;
		}

		Uint32 bufferSizeRequired = chunk->uncompressedSize;
		RED_FATAL_ASSERT( bufferSizeRequired <= m_decompressedBuffer.DataCapacity(), "Decompression buffer is too small" );
		m_decompressedBuffer.Resize( bufferSizeRequired );

		Uint32 compressedBufferSize = chunk->compressedSize;
		RED_FATAL_ASSERT( compressedBufferSize <= m_compressedBuffer.GetSize(), "Compression buffer is too small" );

		m_reader.Seek( chunk->compressedOffset );
		m_reader.Serialize( m_compressedBuffer.Get(), compressedBufferSize );

		const auto decompressionAllocator = compression::GetInplaceCompressionAllocator( m_decompressedBuffer.Data(), bufferSizeRequired );
		const auto data = compression::DecompressData( compression::CT_LZ4, m_compressedBuffer.Get(), compressedBufferSize, decompressionAllocator );
		
		if ( !data )
		{
			HandleIOError( "Failed to decompress chunk, size is (%u)", GetSize() );
			RED_FATAL_ASSERT( 0, "Failed to decompress chunk, size is (%u)", GetSize() );
			return false;
		}

		m_currentChunk = chunk;
		m_localChunkOffset = 0;
		return true;
	}

}