/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../include/bufferedWriter.h"

namespace fs
{
	BufferedWriter::BufferedWriter( red::UniquePtr<IFile> writer, const Uint32 size, const Uint32 alignment )
		: IFile( writer->GetFlags() )
		, m_writer( std::move( writer ) )
		, m_writerOffset( m_writer->GetOffset() )
		, m_bufferUsed( 0 )
		, m_bufferLocation( m_writerOffset )
		, m_offset( m_writerOffset )
		, m_size( m_writer->GetSize() )
	{
		// Set IFile version as writer
		m_version = m_writer->GetVersion();
		m_buffer = red::CreateUniqueBuffer< red::PoolEngine >( size, alignment );
	}

	BufferedWriter::~BufferedWriter()
	{
		Flush();
	}

	Uint64 BufferedWriter::GetOffset() const
	{
		return m_offset;
	}

	Uint64 BufferedWriter::GetSize() const
	{
		return m_size;
	}

	Bool BufferedWriter::IsOffsetWithinBuffer( Uint64 offset ) const
	{
		return ( offset >= m_bufferLocation ) && ( offset < m_bufferLocation + m_bufferUsed );
	}

	void BufferedWriter::Serialize( void* buffer, size_t size )
	{
		if ( HasErrors() )
		{
			return;
		}

		// cursor location within the buffer (it's not always equal to m_bufferUsed)
		const Uint64 bufferWriteLocation = m_offset - m_bufferLocation;
		RED_FATAL_ASSERT( bufferWriteLocation <= m_bufferUsed );
	
		// determine maximum number of bytes that can be copied to the internal buffer
		size_t sizeToCopy = Min( m_buffer.Size() - bufferWriteLocation, size );

		// don't copy to a buffer if it's empty ('size' can be greater than g_bufferSize)
		if ( m_bufferUsed == 0 )
		{
			sizeToCopy = 0;
		}

		const size_t sizeRemaining = size - sizeToCopy;

		// copy as many bytes to the internal buffer as possible
		if ( sizeToCopy > 0 )
		{
			Uint8* bufferPtr = reinterpret_cast< Uint8* >( m_buffer.Get() ) + bufferWriteLocation;
			red::Memcpy( bufferPtr, buffer, sizeToCopy );
			m_bufferUsed = Max< Uint64 >( m_bufferUsed, bufferWriteLocation + sizeToCopy );
			m_offset += sizeToCopy;
			RED_FATAL_ASSERT( m_bufferUsed <= m_buffer.Size() );
		}

		// the rest won't fit - we must flush
		if ( sizeRemaining > 0 )
		{
			Flush();

			Uint8* remainingBuffer = reinterpret_cast< Uint8* >( buffer ) + sizeToCopy;

			if ( sizeRemaining > m_buffer.Size() ) // bypass buffering if write size is bigger than buffer size
			{
				m_writer->Serialize( remainingBuffer, sizeRemaining );

				// Capture IO error
				if ( m_writer->HasErrors() )
				{
					HandleIOError( "Buffered writer error: Writing %llu bytes", size );
				}

				m_writerOffset += sizeRemaining;
				m_bufferLocation = m_writerOffset;
			}
			else // copy rest at the new buffer beginning
			{
				red::Memcpy( m_buffer.Get(), remainingBuffer, sizeRemaining );
				m_bufferUsed = sizeRemaining;
				RED_FATAL_ASSERT( m_bufferUsed <= m_buffer.Size() );
			}

			m_offset += sizeRemaining;
		}

		RED_FATAL_ASSERT( m_offset - m_bufferLocation <= m_bufferUsed );
		RED_FATAL_ASSERT( m_offset >= m_bufferLocation );

		// Track size
		m_size = Max< Uint64 >( m_size, m_offset );
	}

	void BufferedWriter::Seek( Int64 offset )
	{
		// seek in the low-level writer only if the offset would be outside the buffer
		if ( !IsOffsetWithinBuffer( offset ) )
		{
			Flush();
			m_bufferLocation = offset;
		}

		m_offset = offset;

		RED_FATAL_ASSERT( m_offset >= m_bufferLocation );
		RED_FATAL_ASSERT( m_offset - m_bufferLocation <= m_bufferUsed );
	}

	void BufferedWriter::Flush()
	{
		// Seek file to the buffer location
		if ( m_writerOffset != m_bufferLocation )
		{
			m_writer->Seek( m_bufferLocation );
			m_writerOffset = m_bufferLocation;
		}

		if ( m_bufferUsed > 0 )
		{
			// Write data in buffer
			m_writer->Serialize( m_buffer.Get(), m_bufferUsed );

			// Capture IO error
			if ( m_writer->HasErrors() )
			{
				HandleIOError( "Buffered writer error: Writing %llu bytes", m_bufferUsed );
			}

			// update pointers
			m_writerOffset += m_bufferUsed;
			m_bufferLocation = m_writerOffset;
			m_offset = m_writerOffset;
			m_bufferUsed = 0;
		}

		RED_FATAL_ASSERT( m_offset >= m_bufferLocation );
		RED_FATAL_ASSERT( m_offset - m_bufferLocation <= m_bufferUsed );
	}

	const char* BufferedWriter::GetFileNameForDebug() const
	{
		return m_writer->GetFileNameForDebug();
	}
	
} // fs
