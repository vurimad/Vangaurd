#include "build.h"
#include "resultBuffer.h"

namespace compression
{
	ResultBuffer::ResultBuffer()
		: m_data( nullptr )
		, m_size( 0 )
	{
	}

	ResultBuffer::ResultBuffer(void* data, const red::Uint64 size, TFreeFunc freeFunc)
		: m_data( data )
		, m_size( size )
		, m_freeFunc( freeFunc )
	{
	}

	ResultBuffer::~ResultBuffer()
	{
		Clear();
	}

	void ResultBuffer::Clear()
	{
		if ( nullptr != m_data )
		{
			// NOTE: the free function must be defined if the data buffer is not empty
			m_freeFunc(m_data);

			m_data = nullptr;
			m_size = 0;
		}
	}

	void ResultBuffer::PatchDataSize( const Uint64 size )
	{
		RED_FATAL_ASSERT( size <= m_size, "Trying to patch result buffer to size larger than existing one (%llu > %llu)", size, m_size );
		m_size = size;
	}

} // compresion