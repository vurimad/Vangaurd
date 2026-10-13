/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/stream.h"

namespace red
{
namespace memory
{
	Stream::Stream()
	{}

	Stream::~Stream()
	{}

	u32 Stream::DataAvailableToWrite() const
	{
		return OnDataAvailableToWrite();
	}

	u32 Stream::DataAvailableToRead() const
	{
		return OnDataAvailableToRead();
	}

	Bool Stream::WriteBuffer( const void * buffer, u32 size )
	{
		return OnWriteBuffer( buffer, size );
	}

	Bool Stream::ReadBuffer(void * buffer, u32 size, u32 & readSize)
	{
		return OnReadBuffer( buffer, size, readSize );
	}

	void* Stream::GetReadData() const
	{
		return OnGetReadData();
	}

	void Stream::SeekReadIndex( u32 size )
	{
		OnSeekReadIndex( size );
	}

	StreamView::StreamView()
		: m_stream( &m_nullStream )
	{}

	StreamView::StreamView( Stream* stream )
		: m_stream( stream )
	{}

	StreamView& StreamView::operator=( Stream* stream )
	{
		if ( std::addressof( m_stream ) != std::addressof( stream ) )
		{
			m_stream = stream;
		}

		return *this;
	}

	u32 StreamView::DataAvailableToWrite() const
	{
		return m_stream->DataAvailableToWrite();
	}

	u32 StreamView::DataAvailableToRead() const
	{
		return m_stream->DataAvailableToRead();
	}

	Bool StreamView::WriteBuffer( const void * buffer, u32 size )
	{
		return m_stream->WriteBuffer( buffer, size );
	}

	Bool StreamView::ReadBuffer( void * buffer, u32 size, u32 & readSize )
	{
		return m_stream->ReadBuffer( buffer, size, readSize );
	}

	void StreamView::MoveReadIndex( u32 size )
	{
		return m_stream->SeekReadIndex( size );
	}

	void StreamView::Reset()
	{
		m_stream = &m_nullStream;
	}

	u32 StreamView::NullStream::OnDataAvailableToWrite() const
	{
		return 0;
	}

	u32 StreamView::NullStream::OnDataAvailableToRead() const
	{
		return 0;
	}

	Bool StreamView::NullStream::OnWriteBuffer( const void * , u32  )
	{
		return false;
	}

	Bool StreamView::NullStream::OnReadBuffer( void * , u32 , u32 &  )
	{
		return false;
	}

	void* StreamView::NullStream::OnGetReadData() const
	{
		return nullptr;
	}

	void StreamView::NullStream::OnSeekReadIndex( u32 )
	{
	}
}
}
