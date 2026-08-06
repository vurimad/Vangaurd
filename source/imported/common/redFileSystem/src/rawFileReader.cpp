/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rawFileReader.h"

namespace fs
{

	RawFileReader::RawFileReader()
		: IFile( FF_Reader | FF_FileBased )
	{
	}

	RawFileReader::~RawFileReader() = default;

	void RawFileReader::Serialize( void* buffer, size_t size )
	{
		RED_FATAL_ASSERT( size < UINT_MAX, "Read size is to big" );

		Uint32 numRead = 0;
		m_fileHandle.Read( buffer, (Uint32)size, numRead );

		// TODO: handle errors
		if ( numRead != size )
		{
			HandleIOError( "read error: requested %llu bytes, read %llu", static_cast< Uint64 >( size ), static_cast< Uint64 >( numRead ) );
		}
	}

	Uint64 RawFileReader::GetOffset() const
	{
		return static_cast< Uint64 >( m_fileHandle.Tell() );
	}

	Uint64 RawFileReader::GetSize() const
	{
		return m_fileHandle.GetFileSize();
	}

	void RawFileReader::Seek( Int64 offset )
	{
		if ( !m_fileHandle.Seek( offset, io::eSeekOrigin_Set ) )
		{
			HandleIOError( "seek error: position %lld, file size=%llu", offset, GetSize() );
		}
	}

	void RawFileReader::Flush()
	{
	}

	const char* RawFileReader::GetFileNameForDebug() const
	{
		return m_filePath.AsChar();
	}
		
	red::UniquePtr<RawFileReader> RawFileReader::Create( const red::AbsolutePath& absoluteFilePath )
	{
		auto ret = red::CreateUniquePtr< RawFileReader >();
		if ( ret->m_fileHandle.Open( absoluteFilePath.AsChar(), io::eOpenFlag_Read ) )
		{
			ret->m_filePath = absoluteFilePath;
			return ret;
		}
		return nullptr;
	}

} // fs
