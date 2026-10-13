/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageReadStream.h"

namespace red
{
	PackageReadStream::PackageReadStream()
	{}

	PackageReadStream::~PackageReadStream()
	{}

	void PackageReadStream::SetBuffer( const red::BlobView & blobView )
	{
		m_data = blobView;
		Seek( 0 );
	}

	const void * PackageReadStream::GetReadCursor() const
	{
		return m_data.Data( static_cast< Uint32 >( GetPosition() ) );
	}

	Uint64 PackageReadStream::OnWrite( const void * data, Uint64 size, Uint64 position )
	{
		return 0;
	}
	
	Uint64 PackageReadStream::OnRead( void * data, Uint64 size, Uint64 position )
	{
		const void * readCursor = m_data.Data( static_cast< Uint32 >( position ) );
		red::Memcpy( data, readCursor, size );
		return size;
	}
	
	Uint64 PackageReadStream::OnSeek( Uint64 positionRequest )
	{
		return positionRequest;
	}
	
	Uint32 PackageReadStream::GetBufferSize() const
	{
		return m_data.Size();
	}

	red::UniquePtr< PackageReadStream > CreateReadStream( const red::BlobView & blobView )
	{
		red::UniquePtr< PackageReadStream > stream = red::CreateUniquePtr< PackageReadStream >();
		stream->SetBuffer( blobView );
		return stream;
	}
}
