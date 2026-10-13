/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_STREAM_H_
#define _RED_MEMORY_STREAM_H_

#include "../include/poolUtils.h"
#include "../../redSystem/include/utility.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API Stream : NonCopyable
	{
	public:

		RED_USE_MEMORY_POOL( PoolDebug );

		Stream();
		virtual ~Stream();

		u32 DataAvailableToWrite() const;
		u32 DataAvailableToRead() const;

		Bool WriteBuffer( const void * buffer, u32 size );
		Bool ReadBuffer( void * buffer, u32 size, u32 & readSize );

		void* GetReadData() const;
		void SeekReadIndex( u32 size );

	private:

		virtual u32 OnDataAvailableToWrite() const = 0;
		virtual u32 OnDataAvailableToRead() const = 0;
		virtual Bool OnWriteBuffer( const void * buffer, u32 size ) = 0;
		virtual Bool OnReadBuffer( void * buffer, u32 size, u32 & readSize ) = 0;
		virtual void* OnGetReadData() const = 0;
		virtual void OnSeekReadIndex( u32 size ) = 0;
	};

	class RED_MEMORY_API StreamView
	{
	public:

		StreamView();
		StreamView( Stream* stream );
		StreamView& operator=( Stream* stream );

		u32 DataAvailableToWrite() const;
		u32 DataAvailableToRead() const;

		Bool WriteBuffer( const void * buffer, u32 size );
		Bool ReadBuffer( void * buffer, u32 size, u32 & readSize );

		void MoveReadIndex( u32 size );

		void Reset();

	private:

		class NullStream final : public Stream
		{
		private:
			virtual u32 OnDataAvailableToWrite() const override;
			virtual u32 OnDataAvailableToRead() const override;
			virtual Bool OnWriteBuffer( const void * buffer, u32 size ) override;
			virtual Bool OnReadBuffer( void * buffer, u32 size, u32 & readSize ) override;
			virtual void* OnGetReadData() const override;
			virtual void OnSeekReadIndex( u32 size ) override;
		};

		NullStream m_nullStream;
		Stream* m_stream;
	};
}
}

#endif
