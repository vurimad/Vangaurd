/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "blob.h"

#include "../../../common/redSystem/include/utility.h"
#include "../../../common/redMemory/include/pool.h"

namespace red
{
	//-----------------------------------------------------------------------------
	// BlobSpan

	void BlobSpan::CheckRange( Uint32 offset, Uint32 size ) const
	{
		RED_FATAL_ASSERT( m_data != nullptr, "Cannot access data from a null pointer" );
		RED_FATAL_ASSERT( offset + size <= m_size,
			"Attempting to access data past the end of the buffer, offset %u and size %u is not within %u", offset, size, m_size );
	}

	//-----------------------------------------------------------------------------
	// BlobView

	void BlobView::CheckRange( Uint32 offset, Uint32 size ) const
	{
		RED_FATAL_ASSERT( m_data != nullptr, "Cannot access data from a null pointer" );
		RED_FATAL_ASSERT( offset + size <= m_size,
			"Attempting to access data past the end of the buffer, offset %u and size %u is not within %u", offset, size, m_size );
	}

	//-----------------------------------------------------------------------------
	// Blob

	Blob::Blob()
		: m_buffer( MakeEmptyUniqueBuffer( red::PoolEngine::GetInstance(), 1 ) )
	{
	}

	Blob::Blob( Uint32 size, Uint32 align )
		: m_buffer( CreateUniqueBuffer< red::PoolEngine >( size, align ) )
	{
	}

	void* Blob::Data( Uint32 offset )
	{
		RED_FATAL_ASSERT( offset < Size(), 
			"Attempting to access data outside the valid range, offset %u size %u", offset, Size() );
		return static_cast<Uint8*>( Data() ) + offset;
	}

	const void* Blob::Data( Uint32 offset ) const
	{
		RED_FATAL_ASSERT( offset < Size(), 
			"Attempting to access data outside the valid range, offset %u size %u", offset, Size() );
		return static_cast<const Uint8*>( Data() ) + offset;
	}

	void Blob::Resize( Uint32 size )
	{
		m_buffer.Reallocate( size );
	}

	red::UniqueBuffer Blob::Release()
	{
		// Releasing the buffer for someone else to own
		return std::move( m_buffer );
	}

	void Blob::CheckRange( Uint32 offset, Uint32 size ) const
	{
		RED_FATAL_ASSERT( m_buffer, "Cannot access data from a null pointer" );
		RED_FATAL_ASSERT( offset + size <= m_buffer.GetSize(),
			"Attempting to access data past the end of the buffer, offset %u and size %u is not within %u", offset, size, m_buffer.GetSize() );
	}

} // namespace red
