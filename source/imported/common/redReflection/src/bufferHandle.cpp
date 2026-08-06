/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../redMemory/include/uniqueBuffer.h"

#include "bufferHandle.h"

namespace serialization
{

BufferHandleRO::BufferHandleRO( Uint32 size, Uint32 alignment, const red::memory::Pool& memoryPool )
{
	m_data = red::CreateSharedPtr< red::UniqueBuffer >( red::CreateUniqueBuffer( memoryPool, size, alignment ) );
}

BufferHandleRO::BufferHandleRO( red::UniqueBuffer&& data )
{
	m_data = red::CreateSharedPtr< red::UniqueBuffer >( std::move( data ) );
}

BufferHandleRO::BufferHandleRO( const void* data, Uint32 size, Uint32 alignment, const red::memory::Pool& memoryPool )
{
	auto buf = red::CreateUniqueBuffer( memoryPool, size, alignment );
	if ( size > 0 )
	{
		red::Memcpy( buf.Get(), data, size );
	}
	m_data = red::CreateSharedPtr< red::UniqueBuffer >( std::move( buf ) );
}

BufferHandleRO::BufferHandleRO( std::nullptr_t )
{
}

BufferHandleRO::BufferHandleRO()
{
}

const void* BufferHandleRO::Data() const
{
	// Check for size, since the memory allocator may have given us a valid ptr for zero size
	return m_data && (m_data->GetSize() > 0) ? m_data->Get() : nullptr;
}

Uint32 BufferHandleRO::Size() const
{
	return m_data ? m_data->Size() : 0;
}

Uint32 BufferHandleRO::GetSize() const
{
	return m_data ? m_data->Size() : 0;
}

Uint32 BufferHandleRO::GetAlignment() const
{
	return m_data ? m_data->GetAlignment() : 0;
}

red::BlobSpan BufferHandleRO::GetSpan() const
{
    return red::BlobSpan( m_data->Data(), m_data->Size() );
}

red::BlobView BufferHandleRO::GetView() const
{
    return red::BlobView( m_data->Data(), m_data->Size() );
}

}
