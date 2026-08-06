/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rawFileWriter.h"

namespace fs
{

	RawFileWriter::RawFileWriter()
		: IFile( FF_Writer | FF_FileBased )
	{
	}

	RawFileWriter::~RawFileWriter() = default;

	void RawFileWriter::Serialize( void* buffer, size_t size )
	{
		Uint32 actualWrite = 0;
		m_fileHandle.Write( buffer, static_cast< Uint32 >( size ), actualWrite );

		// For now IO errors MUST be handled even in the final build
		if ( actualWrite != size )
		{
			HandleIOError( "Write error: requested %llu bytes, written %llu bytes", static_cast< Uint64 >( size ), static_cast< Uint64 >( actualWrite ) );
		}
	}

	Uint64 RawFileWriter::GetOffset() const
	{
		return static_cast< Uint64 >( m_fileHandle.Tell() );
	}

	Uint64 RawFileWriter::GetSize() const
	{
		return m_fileHandle.GetFileSize();
	}

	void RawFileWriter::Seek( Int64 offset )
	{
		if ( !m_fileHandle.Seek( offset, io::eSeekOrigin_Set ) )
		{
			HandleIOError( "seek error: position %lld, file size=%llu", offset, GetSize() );
		}
	}

	void RawFileWriter::Flush()
	{
		if ( !m_fileHandle.Flush() )
		{
			HandleIOError( "flush error: file size=%llu", GetSize() );
		}
	}

	const char* RawFileWriter::GetFileNameForDebug() const
	{
		return  m_filePath.AsChar();
	}
	
	red::UniquePtr<RawFileWriter> RawFileWriter::Create( const red::AbsolutePath& absoluteFilePath, Bool append )
	{
		auto ret = red::CreateUniquePtr< RawFileWriter >();
		if ( ret->m_fileHandle.Open( absoluteFilePath.AsChar(), append ? io::eOpenFlag_Write : io::eOpenFlag_WriteNew ) )
		{
			if ( append )
			{
				ret->m_fileHandle.Seek( 0, io::eSeekOrigin_End );
			}

			ret->m_filePath = absoluteFilePath;
			return ret;
		}
		return nullptr;
	}

} // fs