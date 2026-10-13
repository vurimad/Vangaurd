/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "bufferedReader.h"

namespace fs
{

	BufferedReader::BufferedReader( red::UniquePtr<IFile> reader, const Uint32 size, const Uint32 alignment )
		: IFile( reader->GetFlags() )
		, m_reader( std::move( reader ) )
		, m_bufferBase( 0 )
		, m_bufferCount( 0 )
		, m_offset( m_reader->GetOffset() )
		, m_size( m_reader->GetSize() )
	{
		// Set IFile version as writer
		m_version = m_reader->GetVersion();
		m_buffer = red::CreateUniqueBuffer< red::PoolEngine >( size, alignment );
	}

	BufferedReader::~BufferedReader() = default;

	void BufferedReader::Serialize( void* buffer, size_t size )
	{
		if ( HasErrors() )
		{
			return;
		}

		// Validate read
		if ( m_offset + size > m_size )
		{
			HandleIOError( "buffered read error: Offset: %d, Size: %d, SizeToRead: %d", m_offset, m_size, size );
			red::Memzero( buffer, size );
			return;
		}

		// While there is something to read
		while ( size > 0 )
		{
			// Calculate how much data we can copy from internal buffer
			size_t bytesInBuffer = Min( size, static_cast< size_t >( m_bufferBase + m_bufferCount - m_offset ) );

			// There's nothing left in the buffer, fill the buffer
			if ( bytesInBuffer == 0 )
			{
				// If remaining data is larger than buffer size read it directly
				if ( size >= m_buffer.Size() )
				{
					// Read file
					m_reader->Serialize( buffer, size );

					// Move position by the number of bytes read
					m_offset += size;

					// Reset buffer
					m_bufferBase = m_offset;
					m_bufferCount = 0;  

					// Nothing more to do
					break;
				}
				else
				{
					// Precache data in buffer
					m_bufferBase = m_offset;
					m_bufferCount = Min< Uint64 >( m_buffer.Size(), static_cast< size_t >( m_size - m_offset ) );
					if ( m_bufferCount )
					{
						m_reader->Serialize( m_buffer.Data(), m_bufferCount );
						continue;
					}
					else
					{
						// Out of file bounds
						break;
					}
				}
			}

			// Copy data to buffer
			red::Memcpy( buffer, red::OffsetAddress( m_buffer.Data(), m_offset - m_bufferBase ), bytesInBuffer );

			// Move file offset
			m_offset += bytesInBuffer;

			// Decrement amount of work
			size -= bytesInBuffer;
			buffer = ( Uint8* ) buffer + bytesInBuffer;
		}
	}

	Uint64 BufferedReader::GetOffset() const
	{
		return m_offset;
	}

	Uint64 BufferedReader::GetSize() const
	{
		return m_size;
	}

	void BufferedReader::Seek( Int64 offset )
	{
		// Out of bounds seek ?
		if ( offset > (Int64) m_size )
		{
			HandleIOError( "Buffered seek error: Offset: %lld, Size: %llu", offset, m_size );
		}
		else
		{
			// valid seek - try the reading again
			ClearError();
		}

		// If we are inside the buffered region just move the pointer
		const Uint64 bufferEnd = m_bufferBase + m_bufferCount;
		if ( (Uint64)offset >= m_bufferBase && (Uint64)offset < bufferEnd )
		{
			m_offset = offset;
		}
		else
		{
			// Seek to new file position
			// TODO: implement sector align here
			const Uint64 seekOffset = offset;
			m_reader->Seek( seekOffset );

			// Reset buffer
			m_bufferCount = 0;
			m_bufferBase = seekOffset;
			m_offset = offset;
		}
	}

	void BufferedReader::Flush()
	{
	}

	const char* BufferedReader::GetFileNameForDebug() const
	{
		return m_reader->GetFileNameForDebug();
	}
	
} // fs