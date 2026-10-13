/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../include/string/stringBuilder.h"

namespace red
{
	template<typename T>
	const Float StringBuilder<T>::GROWTH_FACTOR = 1.5f;

	template<typename T>
	StringBuilder<T>::StringBuilder()
		: m_buf(m_inlineBuf)
		, m_capacity( INLINE_BUFFER_SIZE )
		, m_length( 0 )
	{
		writeNullTerminator();
	}

	template<typename T>
	StringBuilder<T>::~StringBuilder()
	{
		if ( m_buf != m_inlineBuf )
		{
			RED_FREE( red::PoolEngine, m_buf );
		}
	}

	template<typename T>
	void StringBuilder<T>::Append( const TChar* str, Uint32 len )
	{
		RED_FATAL_ASSERT( red::Strlen( str ) >= len, "Copied characters contain a null character" );
		const Uint32 requiredCapacity = m_length + len + 1;
		EnsureCapacity( requiredCapacity );

		TChar* offsetBuf = m_buf + m_length;
		red::Memcpy( offsetBuf, str, sizeof(char) * len );
		m_length += len;
		writeNullTerminator();
	}

	template<typename T>
	void StringBuilder<T>::Prepend( const TChar* str, Uint32 len )
	{
		RED_FATAL_ASSERT( red::Strlen( str ) >= len, "Copied characters contain a null character" );
		const Uint32 requiredCapacity = m_length + len + 1;
		EnsureCapacity( requiredCapacity );

		red::Memmove( m_buf + len, m_buf, sizeof( char ) * m_length );
		red::Memcpy( m_buf, str, sizeof( char ) * len );

		m_length += len;
		writeNullTerminator();
	}

	template<typename T>
	void StringBuilder<T>::Appendf( STATIC_CHECK_PRINTF_MSC const TChar* fmt, ... )
	{
		RED_FATAL_ASSERT( fmt, "" );

		// #fixme note: can't rely on our VSNPrintf wrapper returning the required buffer size on different platforms
		Int32 result = -1;
		for ( ;; )
		{
			// Get buf each time, since it may point to different memory
			TChar* offsetBuf = m_buf + m_length;
			const Int32 bufsz = static_cast< Int32 >( m_capacity - m_length );

			va_list vargs;
			va_start( vargs, fmt );
			result = red::VSNPrintF( offsetBuf, bufsz, fmt, vargs );
			va_end( vargs );
			
			// red::VSNPrintF could return -1 (truncated or error) or >= bufsz (truncated) depending on the platform
			// #fixme: no good way of detecting an error using our wrappers
			if ( result >= 0 && result < bufsz )
			{
				break;
			}
			EnsureCapacity();
		}
		
		RED_FATAL_ASSERT( result >= 0, "" );
		m_length += result;
		
		// VSNPrint appends null terminator itself
		assertNullTerminator();
	}

	template<typename T>
	RED_FORCE_INLINE void StringBuilder<T>::EnsureCapacity()
	{
		const Uint32 requiredCapacity = static_cast< Uint32 >( m_capacity * GROWTH_FACTOR );
		EnsureCapacity( requiredCapacity );
	}

	template<typename T>
	void StringBuilder<T>::EnsureCapacity( Uint32 requiredCapacity )
	{
		if ( requiredCapacity > m_capacity )
		{
			const Uint32 temp = static_cast< Uint32 >( m_capacity * GROWTH_FACTOR ); 
			const Uint32 newCapacity = temp >= requiredCapacity ? temp : static_cast< Uint32 >( requiredCapacity * GROWTH_FACTOR );
			if ( m_buf != m_inlineBuf )
			{
				m_buf = static_cast< TChar* >( RED_REALLOCATE( red::PoolEngine, m_buf, sizeof(TChar) * newCapacity ) );
			}
			else
			{
				m_buf = static_cast< TChar* >( RED_ALLOCATE( red::PoolEngine, sizeof(TChar) * newCapacity ) );
				red::Memcpy( m_buf, m_inlineBuf, sizeof(TChar) * (m_length+1) ); // copy null terminator
			}

			m_capacity = newCapacity;
		}

		assertCapacity();
	}
}

// explicit template instantiation
template class RED_CONTAINERS_API red::StringBuilder<String>;
