/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageReadWriteStream.h"

namespace red
{
	PackageReadWriteStream::PackageReadWriteStream()
	{}
	
	PackageReadWriteStream::~PackageReadWriteStream()
	{}

	void PackageReadWriteStream::SetBuffer( const red::BlobSpan & buffer )
	{
		m_buffer = buffer;
		Seek( 0 );
	}

	void * PackageReadWriteStream::GetCursor() const
	{
		return m_buffer.Data( static_cast< Uint32 >( GetPosition() ) );
	}

	Uint32 PackageReadWriteStream::GetBufferSize() const
	{
		return m_buffer.Size();
	}

	Uint64 PackageReadWriteStream::OnSeek( Uint64 requestedPosition )
	{
		RED_FATAL_ASSERT( requestedPosition <= m_buffer.Size() , "Out Of Bound Seek request." );
		return requestedPosition;
	}

	Uint64 PackageReadWriteStream::OnRead( void * data, Uint64 size, Uint64 position )
	{
		const void * readCursor = m_buffer.Data( static_cast< Uint32 >( position ) );
		red::Memcpy( data, readCursor, size );
		return size;
	}
	
	Uint64 PackageReadWriteStream::OnWrite( const void * data, Uint64 size, Uint64 position )
	{
		RED_FATAL_ASSERT( position + size <= m_buffer.Size(), "Out Of Bound Write request." );

		void * writeCursor = static_cast< Uint8* >( m_buffer.Data( static_cast< Uint32 >( position ) ) );
		red::Memcpy( writeCursor, data, size );
		return size; 
	}

	red::UniquePtr< PackageReadWriteStream > CreatePackageReadWriteStream( const red::BlobSpan & buffer )
	{
		red::UniquePtr< PackageReadWriteStream > stream = red::CreateUniquePtr< PackageReadWriteStream >();
		stream->SetBuffer( buffer );
		return stream;
	}
}
