/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "packageWriteStream.h"

namespace red
{
	const Uint32 c_packageWriteStreamChunkSize = RED_MEGA_BYTE( 1 );

	PackageWriteStream::PackageWriteStream()
	{}

	PackageWriteStream::~PackageWriteStream()
	{}

	void PackageWriteStream::Initialize( Uint32 reservedSize )
	{
		m_buffer = red::CreateUniqueBuffer< red::PoolEngine >( reservedSize, 16 ); // ctremblay: Is this going to be in runtime ? Most likely. Gotta need to select pool.
	}

	void * PackageWriteStream::GetWriteCursor() const
	{
		return static_cast< Uint8* >( m_buffer.Get() ) + GetPosition();
	}

	red::BlobView PackageWriteStream::GetBufferView() const
	{
		return red::BlobView( m_buffer.Get(), GetWriteCursor() );
	}

	red::BlobSpan PackageWriteStream::GetBuffer()
	{
		return red::BlobSpan( m_buffer.Get(), GetWriteCursor() );
	}

	red::BlobView PackageWriteStream::GetRange( Uint64 start, Uint64 end ) const
	{
		return GetBufferView().Range( static_cast< Uint32 >( start ), static_cast< Uint32 >( end - start ) );
	}

	red::BlobSpan PackageWriteStream::GetRange( Uint64 start, Uint64 end )
	{
		return red::BlobSpan( static_cast< Uint8* >( m_buffer.Get() ) + start, end - start );
	}

	red::UniqueBuffer PackageWriteStream::ReleaseBuffer()
	{
		m_buffer.Reallocate( static_cast< Uint32 >( GetPosition() ) ); // ctremblay: Shrinking buffer to minimal size.
		return std::move( m_buffer );
	}

	Uint64 PackageWriteStream::OnSeek( Uint64 requestedPosition )
	{
		if( requestedPosition > m_buffer.GetSize() )
		{
			m_buffer.Reallocate( red::memory::RoundUp( static_cast< Uint32 >( requestedPosition ), c_packageWriteStreamChunkSize ) );
		}

		return requestedPosition;
	}

	Uint64 PackageWriteStream::OnRead( void * data, Uint64 size, Uint64 position )
	{
		return 0;
	}

	Uint64 PackageWriteStream::OnWrite( const void * data, Uint64 size, Uint64 position )
	{
		if( position + size > m_buffer.GetSize() )
		{
			m_buffer.Reallocate( red::memory::RoundUp( static_cast< Uint32 >( size + position ), c_packageWriteStreamChunkSize ) );
		}

		void * writeCursor = static_cast< Uint8* >( m_buffer.Get() ) + position;
		red::Memcpy( writeCursor, data, size );
		return size; 
	}

	red::UniquePtr< PackageWriteStream > CreateWriteStream( Uint32 reservedSize )
	{
		red::UniquePtr< PackageWriteStream > stream = red::CreateUniquePtr< PackageWriteStream >();
		stream->Initialize( reservedSize ) ;
		return stream;
	}
}
