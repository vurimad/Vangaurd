/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileStreamWin.h"
#include "functions.h"
#include "assert.h"
#include "../include/operators.h"
#include "../include/utils.h"
#include "../include/uniquePtr.h"
#include <limits>

namespace red
{
namespace memory
{
namespace
{
	const u64 c_fileStreamBufferSize = RED_KILO_BYTE( 64 );	
}

	FileStreamWin::FileStreamWin()
		:	m_fileHandle( INVALID_HANDLE_VALUE ),
			m_buffer( nullptr ),	
			m_bufferMarker( nullptr )
	{}
	
	FileStreamWin::~FileStreamWin()
	{
		Flush();
		Close();
	}

	FileResult FileStreamWin::Open( const char * filePath )
	{
		m_fileHandle = CreateFileA( filePath, 
			GENERIC_WRITE,			// We are only writing to the file
			0,						// Do not let other processes access it while we have it open
			NULL,					// Do not let inherited processes access the file
			CREATE_ALWAYS,			// Never append
			FILE_ATTRIBUTE_NORMAL,	// Normal file attribs in the OS
			nullptr );

		if( m_fileHandle != INVALID_HANDLE_VALUE )
		{
			m_buffer = static_cast< u8* >( AllocateAligned< PoolDebug >( c_fileStreamBufferSize, 64 ) );
			m_bufferMarker = m_buffer;
			
			return FileResult_OK;
		}

		return FileResult_OpenFailed;
	}
	
	FileResult FileStreamWin::Close()
	{
		if( m_fileHandle != INVALID_HANDLE_VALUE )
		{
			FlushFileBuffers( m_fileHandle );
			const FileResult result = CloseHandle( m_fileHandle ) == TRUE ? FileResult_OK : FileResult_CloseFailed;
			m_fileHandle = INVALID_HANDLE_VALUE;	// Always assume close was successful
			Free< PoolDebug >( m_buffer );
			m_bufferMarker = nullptr;
			return result;
		}

		return FileResult_OK;
	}
	
	FileResult FileStreamWin::Flush()
	{
		if( m_fileHandle != INVALID_HANDLE_VALUE )
		{
			DWORD bytesWritten = 0;
			DWORD bytesToWrite = static_cast< DWORD >( m_bufferMarker - m_buffer );
			if( WriteFile( m_fileHandle, m_buffer, bytesToWrite, &bytesWritten, nullptr ) )
			{
				if( bytesWritten == bytesToWrite )
				{
					m_bufferMarker = m_buffer;
					return FileResult_OK;
				}
			}

			return FileResult_WriteFailed; 
		}
		
		return FileResult_FileNotOpen;
	}

	u32 FileStreamWin::OnDataAvailableToWrite() const
	{
		return std::numeric_limits< u32 >::max();
	}

	u32 FileStreamWin::OnDataAvailableToRead() const
	{
		RED_MEMORY_ASSERT( 0, "OnDataAvailableToRead is not implemented for FileStreamWin." );
		return 0;
	}

	Bool FileStreamWin::OnWriteBuffer( const void * buffer, u32 size )
	{
		if( m_bufferMarker + size >= m_buffer + c_fileStreamBufferSize )
		{
			Flush();
		}

		Memcpy( m_bufferMarker, buffer, size );
		m_bufferMarker += size;
		return true;
	}

	Bool FileStreamWin::OnReadBuffer( void * , u32 , u32 &  )
	{
		RED_MEMORY_ASSERT( 0, "OnReadBuffer is not implemented for FileStreamWin." );
		return false;
	}

	void* FileStreamWin::OnGetReadData() const
	{
		RED_MEMORY_ASSERT( 0, "OnGetReadData is not implemented for FileStreamWin." );
		return nullptr;
	}

	void FileStreamWin::OnSeekReadIndex( u32 )
	{
		RED_MEMORY_ASSERT( 0, "OnSeekReadIndex is not implemented for FileStreamWin." );
	}

	red::UniquePtr<Stream> OpenStream( const char * filePath )
	{
		red::UniquePtr<FileStreamWin> stream = red::CreateUniquePtr<FileStreamWin>();
		FileResult result = stream->Open( filePath );
		
		return result == FileResult_OK ? std::move( stream ) : nullptr;
	}
}
}
