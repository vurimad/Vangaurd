/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "profilerFileWriter.h"
#include "../../redMemory/include/redMemoryPublic.h"

namespace Config
{
	Int32	cvWriteBufferSize = 64*1024;
	Bool	cvWriteForceFlush = false;
} // Config


CProfilerFileWriter::CProfilerFileWriter()
	: m_writePos( 0 )
	, m_writeMax( 0 )
	, m_writeBuffer( nullptr )
	, m_activeMessages( 0 )
	, m_pendingMessages( 0 )
	, m_bufferSize( 0 )
	, m_ioError( false )
{
}

CProfilerFileWriter::~CProfilerFileWriter()
{
	// flush current stuff
	Flush();

	// close file
	if ( m_file.IsValid() )
	{
		m_file.Close();
	}

	// free memory
	if ( m_writeBuffer )
	{
		FreeProfilingMemory( (void*) m_writeBuffer );
		m_writeBuffer = nullptr;
	}
}

CProfilerFileWriter* CProfilerFileWriter::Open( const red::AbsolutePath& absoluteFilePath, const Uint32 defaultBufferSize /*= 0*/ )
{
	// FIXME: def'd out for PS4 since GFileManager fails to "create" /data/ and returns
#ifndef RED_PLATFORM_ORBIS
	// make sure output path can be created
 	//if ( !GFileManager->CreatePath( absoluteFilePath ) )
 	//	return nullptr;
#endif

	// open native file handle
	CProfilerFileWriter* ret = RED_NEW( CProfilerFileWriter )();

	if ( !ret->m_file.Open( absoluteFilePath.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		RED_LOG_ERROR( "Core: Failed to open file '%hs' for writing", absoluteFilePath.AsChar() );
		RED_DELETE( ret );
		return nullptr;
	}

	// initialize first buffer
	ret->m_bufferSize = defaultBufferSize ? defaultBufferSize : Config::cvWriteBufferSize;
	ret->m_writePos = 0;
	ret->m_writeBuffer = AllocProfilingMemory( ret->m_bufferSize );
	ret->m_writeMax = ret->m_bufferSize;

	// ready
	return ret;
}

Uint8* CProfilerFileWriter::AllocMessage( const Uint32 size )
{
	RED_FATAL_ASSERT( size < m_writeMax, "Trying to allocate hughe memory block in profiler writer - this usually indicates an error (size:%d max:%d)", size, m_writeMax );

	// allocate space
	while ( 1 )
	{
		red::ScopedLock< red::Mutex > lock( m_fastLock );

		// allocate space in the buffer
		if ( m_writePos + size > m_writeMax )
		{
			red::ScopedLock< red::Mutex > lock( m_fullLock );

			// add current buffer to flush list
			BufferInfo info;
			info.m_data = m_writeBuffer;
			info.m_size = m_writePos;
			m_flushList.PushBack( info );

			// allocate new buffer
			m_writeBuffer = AllocProfilingMemory( m_bufferSize );
			m_writeMax = m_bufferSize;
			m_writePos = 0;
			continue;
		}

		// setup message
		m_activeMessages.Increment();
		m_pendingMessages += 1;

		// allocate memory for message
		const Uint32 writePos = m_writePos;
		m_writePos += size;
		return (Uint8*)( m_writeBuffer ) + writePos;
	}
}

void CProfilerFileWriter::FinishMessage()
{
	m_activeMessages.Decrement();

	// flush after every message
	if ( Config::cvWriteForceFlush )
	{
		Flush();
	}
}

void CProfilerFileWriter::SafeWrite( const void* data, const Uint32 size )
{
	// flush data
	if ( size && !m_ioError )
	{
		Uint32 written;
		m_file.Write( data, size, written );
		if ( size != written )
		{
			RED_LOG_ERROR( "Core: FileWrite Error in profiling writer, requested: %d, written: %d", size, written );
			m_ioError = true;
		}
	}
}

void CProfilerFileWriter::Flush()
{
	// prevent other threads from writing
	red::ScopedLock< red::Mutex > lock( m_fastLock );

	// wait for the current profiling messages to be finished
	while ( m_activeMessages.GetValue() != 0 )
		red::YieldCurrentThread();

	// save data from previous buffers
	for ( Uint32 i=0; i<m_flushList.Size(); ++i )
	{
		const auto& buf = m_flushList[i];
		
		// flush
		SafeWrite( buf.m_data, buf.m_size );

		// free memory
		FreeProfilingMemory( buf.m_data );
	}

	// flush table
	m_flushList.Clear();

	// finish with current buffer, rewind it
	SafeWrite( m_writeBuffer, m_writePos );
	m_writePos = 0;

	// reset count
	m_pendingMessages = 0;
}

Uint8* CProfilerFileWriter::AllocProfilingMemory( const Uint32 size )
{
	return (Uint8*) RED_ALLOCATE( red::PoolDebug, size );
}

void CProfilerFileWriter::FreeProfilingMemory( void* memory )
{
	RED_FREE( red::PoolDebug, memory );
}
