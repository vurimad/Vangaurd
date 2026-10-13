/**
* Copyright (c) 2019-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "chunkedLZ4FileWriter.h"
#include "chunkedLZ4Utils.h"
#include "../../redCompression/include/compression.h"

namespace fs
{
	ChunkedLZ4FileWriter::ChunkedLZ4FileWriter( IFile& writer, const Uint32 compressedChunkSize, const Uint32 maximumChunks )
		: IFile( writer.GetFlags() )
		, m_writer( writer )
		, m_chunkBuffer( red::CreateUniqueBuffer< red::PoolEngine >( compressedChunkSize, 8 ) )
		, m_chunkBufferPtr( static_cast< char* >( m_chunkBuffer.Get() ) )
		, m_bytesCached( 0 )
		, m_isSeeked( false )
		, m_chunkHeaderData( red::PoolEngine() )
	{
		m_chunkHeaderData.Reserve( maximumChunks );

		// Reserve space for compression
		const Uint32 compressionBufferSizeRequired = compression::GetLZ4MaxRequiredSize( compressedChunkSize );
		m_compressedBuffer = red::CreateUniqueBuffer< red::PoolEngine >( compressionBufferSizeRequired, 8 );

		m_compressionAllocator = compression::GetInplaceCompressionAllocator( m_compressedBuffer.Get(), m_compressedBuffer.GetSize() );
	}

	ChunkedLZ4FileWriter::~ChunkedLZ4FileWriter() = default;

	void ChunkedLZ4FileWriter::Init()
	{
		m_originalFileOffset = static_cast< Uint32 >( m_writer.GetOffset() );

		// Reserve space for the header + seek in the file
		const Uint64 streamHeaderBytesRequired = sizeof( c_headerMarker ) + sizeof( Uint32 ) + ( m_chunkHeaderData.Capacity() * sizeof( LZ4ChunkMetadata ) );
		m_writer.Seek( m_originalFileOffset + streamHeaderBytesRequired );

		// First chunk offset immediately after header
		m_chunkStartOffset = static_cast< Uint32 >( m_writer.GetOffset() );
	}

	void ChunkedLZ4FileWriter::Serialize( void* buffer, size_t size )
	{
		size_t bytesWritten = 0;
		while ( bytesWritten < size )
		{
			if ( m_isSeeked )
			{
				RED_FATAL_ASSERT( size < m_chunkBuffer.GetSize(), "Cannot serialize bigger data than chunk in seeked mode" );

				Uint32 bytesToWrite = std::min( m_chunkBuffer.GetSize(), static_cast< Uint32 >( size - bytesWritten ) );
				if ( bytesToWrite > 0 )
				{
					red::Memcpy( m_chunkBufferPtr, reinterpret_cast< void* >( reinterpret_cast< char* >( buffer ) + bytesWritten ), bytesToWrite );
					RED_FATAL_ASSERT( m_chunkBufferPtr + bytesToWrite <= static_cast< char* >( m_chunkBuffer.Get() ) + m_chunkBuffer.GetSize() );
					m_bytesCached = ::Max( m_bytesCached, static_cast< Uint32 >( size ) );
					m_chunkBufferPtr += bytesToWrite;
					bytesWritten += bytesToWrite;
				}
			}
			else
			{
				Uint32 ptrOffset = static_cast< Uint32 >( m_chunkBufferPtr - static_cast< char* >( m_chunkBuffer.Get() ) );
				Uint32 bytesToWrite = std::min( m_chunkBuffer.GetSize() - ptrOffset, static_cast< Uint32 >( size - bytesWritten ) );
				if ( bytesToWrite > 0 )
				{
					red::Memcpy( m_chunkBufferPtr, reinterpret_cast< void* >( reinterpret_cast< char* >( buffer ) + bytesWritten ), bytesToWrite );
					m_bytesCached += bytesToWrite;
					RED_FATAL_ASSERT( m_bytesCached <= m_chunkBuffer.GetSize() );
					m_chunkBufferPtr += bytesToWrite;
					RED_FATAL_ASSERT( m_chunkBufferPtr <= static_cast< char* >( m_chunkBuffer.Get() ) + m_chunkBuffer.GetSize() );
					bytesWritten += bytesToWrite;
				}
			}

			if ( m_bytesCached == m_chunkBuffer.GetSize() )
			{
				InternalFlush();
			}
		}
	}

	Uint64 ChunkedLZ4FileWriter::GetOffset() const
	{
		return m_chunkStartOffset + m_bytesCached;
	}

	Uint64 ChunkedLZ4FileWriter::GetSize() const
	{
		return m_writer.GetSize();
	}

	void ChunkedLZ4FileWriter::Seek( Int64 offset )
	{
		m_isSeeked = true;

		const Int64 currentOffset = GetOffset();
		RED_FATAL_ASSERT( offset <= currentOffset, "Cannot seek outside of current chunk" );

		if ( currentOffset == offset )
		{
			InternalFlush();
			m_isSeeked = false;
			return;
		}

		const Uint32 offsetDiff = static_cast< Uint32 >( currentOffset - offset );
		RED_FATAL_ASSERT( offsetDiff < m_chunkBuffer.GetSize(), "Cannot seek outside of current chunk " );

		const Uint32 cachedBytes = m_bytesCached - offsetDiff;
		m_bytesCached = cachedBytes;
		InternalFlush();
		m_bytesCached = offsetDiff;

		m_chunkBufferPtr = static_cast< char* >( m_chunkBuffer.Get() ) + cachedBytes;
		red::Memmove( m_chunkBuffer.Get(), m_chunkBufferPtr, offsetDiff );
		m_chunkBufferPtr = static_cast< char* >( m_chunkBuffer.Get() );
	}

	void ChunkedLZ4FileWriter::InternalFlush()
	{
		if ( !m_bytesCached )
		{
			return;
		}

		RED_FATAL_ASSERT( m_chunkHeaderData.Size() + 1 < m_chunkHeaderData.DataCapacity(), "Maximum chunk count must be increased" );
		RED_FATAL_ASSERT( m_compressedBuffer.Get(), "Compressed buffer does not exist" );
		RED_FATAL_ASSERT( m_chunkBuffer.Get(), "Decompressed buffer does not exist" );

		if ( !m_compressedBuffer.Get() )
		{
			return;
		}

		auto compressedData = compression::CompressData( compression::CT_LZ4, m_chunkBuffer.Get(), m_bytesCached, m_compressionAllocator );

		// Cache header
		LZ4ChunkMetadata metadata;
		metadata.compressedOffset = static_cast< Uint32 >( m_writer.GetOffset() );
		metadata.compressedSize = static_cast< Uint32 >( compressedData->GetDataSize() );
		metadata.uncompressedSize = m_bytesCached;
		m_chunkHeaderData.PushBack( metadata );

#if !defined( RED_CONFIGURATION_FINAL )
		// Validate that compressed data are contiguous
		const Uint32 lastChunkHeaderDataIndex = m_chunkHeaderData.Size() - 1;
		if ( lastChunkHeaderDataIndex )
		{
			RED_FATAL_ASSERT( ( m_chunkHeaderData[lastChunkHeaderDataIndex - 1].compressedOffset + m_chunkHeaderData[lastChunkHeaderDataIndex - 1].compressedSize ) == m_chunkHeaderData[lastChunkHeaderDataIndex].compressedOffset, "Chunks are not contiguous" );
		}
#endif

		// Stream out
		m_writer.Serialize( const_cast< void* >( compressedData->GetData() ), compressedData->GetDataSize() );

		// Reset state
		m_chunkStartOffset += m_bytesCached;
		m_bytesCached = 0;
		m_chunkBufferPtr = static_cast< char* >( m_chunkBuffer.Get() );
	}

	void ChunkedLZ4FileWriter::Flush()
	{
		InternalFlush();

		Uint64 currentPosition = m_writer.GetOffset();

		// Write the header (uncompressed)
		m_writer.Seek( m_originalFileOffset );

		Uint32 magic = c_headerMarker;
		m_writer << magic;

		Uint32 chunkCount = m_chunkHeaderData.Size();
		m_writer << chunkCount;

#if !defined( RED_CONFIGURATION_FINAL )
		// Validate that all compressed data are contiguous
		for ( Uint32 chunk = 0; chunk + 1 < m_chunkHeaderData.Size(); ++chunk )
		{
			RED_FATAL_ASSERT( ( m_chunkHeaderData[chunk].compressedOffset + m_chunkHeaderData[chunk].compressedSize ) == m_chunkHeaderData[chunk + 1].compressedOffset, "Chunks are not contiguous" );
		}
#endif

		for ( auto& chunkData : m_chunkHeaderData )
		{
			m_writer.Serialize( &chunkData, sizeof( LZ4ChunkMetadata ) );
		}

		m_writer.Seek( currentPosition );
	}

}