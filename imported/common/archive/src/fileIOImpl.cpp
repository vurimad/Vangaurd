/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileIOImpl.h"

#include "../../../common/redCore/include/absolutePath.h"

namespace archive
{

//------------------------------------------------------------------------------

bool NullFileReader::Read( void* buffer, Uint32 size )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

bool NullFileReader::Read( void* buffer, Uint64 position, Uint32 size )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

bool NullFileReader::Seek( Uint64 position )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

Uint64 NullFileReader::Tell() const
{
	RED_FATAL( "Unsupported operation!" );
	return static_cast<Uint64>( -1ll );
}

Uint64 NullFileReader::Size() const
{
	RED_FATAL( "Unsupported operation!" );
	return 0ull;
}

//------------------------------------------------------------------------------

bool NullFileWriter::Write( const void* buffer, Uint32 size )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

bool NullFileWriter::Write( const void* buffer, Uint64 position, Uint32 size )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

bool NullFileWriter::Seek( Uint64 position )
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

Uint64 NullFileWriter::Tell() const
{
	RED_FATAL( "Unsupported operation!" );
	return static_cast<Uint64>( -1ll );
}

Uint64 NullFileWriter::Size() const
{
	RED_FATAL( "Unsupported operation!" );
	return 0ull;
}

bool NullFileWriter::Flush()
{
	RED_FATAL("Unsupported operation!");
	return false;
}

bool NullFileWriter::SetEndOfFile()
{
	RED_FATAL( "Unsupported operation!" );
	return false;
}

bool NullFileWriter::IsFileModified()
{
	RED_FATAL("Unsupported operation!");
	return false;
}

//------------------------------------------------------------------------------

SyncFileReader::SyncFileReader()
{
}

SyncFileReader::SyncFileReader( io::NativeFileHandle&& handle )
	: m_handle( std::move( handle ) )
{
}

SyncFileReader::SyncFileReader( SyncFileReader&& other )
	: m_handle( std::move( other.m_handle ) )
{
}

SyncFileReader::~SyncFileReader()
{
}

SyncFileReader& SyncFileReader::operator=( SyncFileReader&& other )
{
	if ( this != &other )
	{
		m_handle = std::move( other.m_handle );
	}
	return *this;
}

SyncFileReader SyncFileReader::Open( const red::AbsolutePath& path )
{
	io::NativeFileHandle handle;
	if ( handle.Open( path.AsChar(), io::eOpenFlag_Read ) )
	{
		return SyncFileReader( std::move( handle ) );
	}
	return SyncFileReader();
}

bool SyncFileReader::Opened() const
{
	return m_handle.IsValid();
}

void SyncFileReader::Close()
{
	m_handle.Close();
}

bool SyncFileReader::Read( void* buffer, Uint32 size )
{
	Uint32 bytesRead = 0;
	return m_handle.Read( buffer, size, bytesRead ) && bytesRead == size;
}

bool SyncFileReader::Read( void* buffer, Uint64 position, Uint32 size )
{
	Uint32 bytesRead = 0;
	if ( !Seek( position ) )
	{
		return false;
	}

	if ( !m_handle.Read( buffer, size, bytesRead ) )
	{
		return false;
	}

	return bytesRead == size;
}

bool SyncFileReader::Seek( Uint64 position )
{
	return m_handle.Seek( static_cast<Int64>( position ), io::eSeekOrigin_Set );
}

Uint64 SyncFileReader::Tell() const
{
	Int64 pos = m_handle.Tell();
	RED_FATAL_ASSERT( pos != -1, "Error in getting position from file" );
	return pos;
}

Uint64 SyncFileReader::Size() const
{
	return m_handle.GetFileSize();
}


//------------------------------------------------------------------------------

SyncFileWriter::SyncFileWriter()
	: m_isFileModifiedByWriter( false )
{
}

SyncFileWriter::SyncFileWriter( io::NativeFileHandle&& handle )
	: m_handle( std::move( handle ) )
	, m_isFileModifiedByWriter( false ) // can't know if is already modified through the handle
{
}

SyncFileWriter SyncFileWriter::Open( const red::AbsolutePath& path )
{
	io::NativeFileHandle handle;
	if ( handle.Open( path.AsChar(), io::eOpenFlag_Write | io::eOpenFlag_Create ) )
	{
		return SyncFileWriter( std::move( handle ) );
	}
	return SyncFileWriter();
}

bool SyncFileWriter::Opened() const
{
	return m_handle.IsValid();
}

void SyncFileWriter::Close()
{
	m_handle.Close();
}

bool SyncFileWriter::Write( const void* buffer, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	Uint32 bytesWritten = 0;
	return m_handle.Write( buffer, size, bytesWritten ) && bytesWritten == size;
}

bool SyncFileWriter::Write( const void* buffer, Uint64 position, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	Uint32 bytesWritten = 0;

	if ( !Seek( position ) )
	{
		return false;
	}

	if ( !m_handle.Write( buffer, size, bytesWritten ) )
	{
		return false;
	}

	return bytesWritten == size;
}

bool SyncFileWriter::Seek( Uint64 position )
{
	return m_handle.Seek( static_cast<Int64>( position ), io::eSeekOrigin_Set );
}

Uint64 SyncFileWriter::Tell() const
{
	Int64 pos = m_handle.Tell();
	RED_FATAL_ASSERT( pos != -1, "Error in getting position from file" );
	return pos;
}

Uint64 SyncFileWriter::Size() const
{
	return m_handle.GetFileSize();
}

bool SyncFileWriter::Flush()
{
	return m_handle.Flush();
}

bool SyncFileWriter::SetEndOfFile()
{
	m_isFileModifiedByWriter = true;

	return m_handle.Truncate( m_handle.Tell() );
}

bool SyncFileWriter::IsFileModified()
{
	return m_isFileModifiedByWriter;
}

//------------------------------------------------------------------------------

// This one is here temporarily until an async-read and sync-write interface version
// can be created. Hence the duplication of code.

SyncFileReaderWriter::SyncFileReaderWriter()
	: m_isFileModifiedByWriter( false )
{
}

SyncFileReaderWriter::SyncFileReaderWriter( io::NativeFileHandle&& handle )
	: m_handle( std::move( handle ) )
	, m_isFileModifiedByWriter( false ) // can't know if modified through the handle before, only by ourselves
{
}

SyncFileReaderWriter SyncFileReaderWriter::Open( const red::AbsolutePath& path )
{
	io::NativeFileHandle handle;
	if ( handle.Open( path.AsChar(), io::eOpenFlag_Write | io::eOpenFlag_Read ) )
	{
		return SyncFileReaderWriter( std::move( handle ) );
	}
	return SyncFileReaderWriter();
}

bool SyncFileReaderWriter::Opened() const
{
	return m_handle.IsValid();
}

void SyncFileReaderWriter::Close()
{
	m_handle.Close();
}

bool SyncFileReaderWriter::Read( void* buffer, Uint32 size )
{
	Uint32 bytesRead = 0;
	return m_handle.Read( buffer, size, bytesRead ) && bytesRead == size;
}

bool SyncFileReaderWriter::Read( void* buffer, Uint64 position, Uint32 size )
{
	Uint32 bytesRead = 0;
	if ( !Seek( position ) )
	{
		return false;
	}

	if ( !m_handle.Read( buffer, size, bytesRead ) )
	{
		return false;
	}

	return bytesRead == size;
}

bool SyncFileReaderWriter::Write( const void* buffer, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	Uint32 bytesWritten = 0;
	return m_handle.Write( buffer, size, bytesWritten ) && bytesWritten == size;
}

bool SyncFileReaderWriter::Write( const void* buffer, Uint64 position, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	Uint32 bytesWritten = 0;

	if ( !Seek( position ) )
	{
		return false;
	}

	if ( !m_handle.Write( buffer, size, bytesWritten ) )
	{
		return false;
	}

	return bytesWritten == size;
}

bool SyncFileReaderWriter::Seek( Uint64 position )
{
	return m_handle.Seek( static_cast<Int64>( position ), io::eSeekOrigin_Set );
}

Uint64 SyncFileReaderWriter::Tell() const
{
	Int64 pos = m_handle.Tell();
	RED_FATAL_ASSERT( pos != -1, "Error in getting position from file" );
	return pos;
}

Uint64 SyncFileReaderWriter::Size() const
{
	return m_handle.GetFileSize();
}

bool SyncFileReaderWriter::Flush()
{
	return m_handle.Flush();
}

bool SyncFileReaderWriter::SetEndOfFile()
{
	m_isFileModifiedByWriter = true;

	return m_handle.Truncate( m_handle.Tell() );
}

bool SyncFileReaderWriter::IsFileModified()
{
	return m_isFileModifiedByWriter;
}

//------------------------------------------------------------------------------

BufferedFileWriter::BufferedFileWriter()
	: m_bufferOffset( 0 )
	, m_isFileModifiedByWriter( false )
{
}

BufferedFileWriter::BufferedFileWriter( io::NativeFileHandle&& handle )
	: m_handle( std::move( handle ) )
	, m_buffer( c_bufferSize, 16 )
	, m_bufferOffset( 0 )
	, m_isFileModifiedByWriter( false )
{
}

BufferedFileWriter::~BufferedFileWriter()
{
	Close();
}

BufferedFileWriter::BufferedFileWriter( BufferedFileWriter&& other ) = default;

BufferedFileWriter& BufferedFileWriter::operator=( BufferedFileWriter&& other ) = default;

BufferedFileWriter BufferedFileWriter::Open( const red::AbsolutePath& path )
{
	io::NativeFileHandle handle;
	if ( handle.Open( path.AsChar(), io::eOpenFlag_Write | io::eOpenFlag_Create ) )
	{
		return BufferedFileWriter( std::move( handle ) );
	}
	return BufferedFileWriter();
}

bool BufferedFileWriter::Opened() const
{
	return m_handle.IsValid() && m_buffer.Size() != 0;
}

void BufferedFileWriter::Close()
{
	RED_VERIFY( Flush() );
	m_handle.Close();
}

bool BufferedFileWriter::Write( const void* buffer, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	return Buffer_Write( buffer, size );
}

bool BufferedFileWriter::Write( const void* buffer, Uint64 position, Uint32 size )
{
	m_isFileModifiedByWriter = true;

	if ( !Seek( position ) )
	{
		return false;
	}

	return Buffer_Write( buffer, size );
}

bool BufferedFileWriter::Seek( Uint64 position )
{
	if ( !Flush() )
	{
		return false;
	}

	return m_handle.Seek( static_cast<Int64>( position ), io::eSeekOrigin_Set );
}

Uint64 BufferedFileWriter::Tell() const
{
	Int64 pos = m_handle.Tell();
	RED_FATAL_ASSERT( pos != -1, "Error in getting position from file" );
	return pos + m_bufferOffset;
}

Uint64 BufferedFileWriter::Size() const
{
	return m_handle.GetFileSize();
}

bool BufferedFileWriter::Flush()
{
	if ( m_bufferOffset == 0 )
	{
		return true;
	}

	bool result = Internal_WriteToFile( m_buffer.Data(), m_bufferOffset );
	m_bufferOffset = 0;
	m_isFileModifiedByWriter = true;

	return result;
}

bool BufferedFileWriter::SetEndOfFile()
{
	m_isFileModifiedByWriter = true;

	if ( !Flush() )
	{
		return false;
	}

	return m_handle.Truncate( m_handle.Tell() );
}

bool BufferedFileWriter::IsFileModified()
{
	return m_isFileModifiedByWriter;
}

bool BufferedFileWriter::Buffer_Write( const void* buffer, Uint32 size )
{
	red::BlobView source{ buffer, size };
	Uint32 sourceOffset = 0;

	// if buffer not empty, write bytes into buffer
	if ( m_bufferOffset != 0 )
	{
		const Uint32 bufferRemaining = c_bufferSize - m_bufferOffset;
		const Uint32 bytesToCopy = Min( bufferRemaining, size );

		Internal_WriteToBuffer( source.Data(), bytesToCopy );
		sourceOffset += bytesToCopy;
		size -= bytesToCopy;
	}

	// if buffer is full, flush buffer
	if ( m_bufferOffset == c_bufferSize )
	{
		if ( !Flush() )
		{
			return false;
		}
	}

	// if size to write is larger than or equal to buffer, write directly
	if ( size >= c_bufferSize )
	{
		return Internal_WriteToFile( source.Data( sourceOffset ), size );
	}

	// if size to write remains, write bytes into buffer
	if ( size > 0 )
	{
		RED_ASSERT( size < c_bufferSize - m_bufferOffset );
		Internal_WriteToBuffer( source.Data( sourceOffset ), size );
	}

	return true;
}

bool BufferedFileWriter::Internal_WriteToFile( const void* buffer, Uint32 size )
{
	Uint32 bytesWritten = 0;
	return m_handle.Write( buffer, size, bytesWritten ) && bytesWritten == size;
}

void BufferedFileWriter::Internal_WriteToBuffer( const void* buffer, Uint32 size )
{
	RED_ASSERT( size <= c_bufferSize - m_bufferOffset );

	void* dest = m_buffer.Data( m_bufferOffset );
	red::Memcpy( dest, buffer, size );
	m_bufferOffset += size;
}

} // namespace archive
