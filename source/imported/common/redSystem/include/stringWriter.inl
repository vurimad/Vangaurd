/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red {

	namespace StringWriterStream
	{
		template< typename CH >
		ConstantSizeStream<CH>& ConstantSizeStream<CH>::GetInstance()
		{
			static ConstantSizeStream<CH> theInstance;
			return theInstance;
		}

		template< typename CH >
		bool ConstantSizeStream<CH>::Flush( const Bool canFail, const CH* data, const red::Uint32 count )
		{
			// returning false will fail further writing
			return false;
		}

		template< typename CH >
		NullStream<CH>& NullStream<CH>::GetInstance()
		{
			static NullStream<CH> theInstance;
			return theInstance;
		}

		template< typename CH >
		bool NullStream<CH>::Flush( const Bool canFail, const CH* data, const red::Uint32 count )
		{
			// consume all input, do nothing
			return true;
		}

	} // StringWriterStream

	template< typename CH, typename StreamClass >
	RED_INLINE StringWriter<CH, StreamClass>::StringWriter( CH* buffer, const red::Uint32 bufferSize, StreamClass& sc /*= StreamClass()*/ )
		: m_stream( sc )
		, m_max( bufferSize-1 ) // always leave place for terminating zero
		, m_pos( 0 )
		, m_full( false )
		, m_buf( buffer )
	{}

	template< typename CH, typename StreamClass >
	RED_INLINE StringWriter<CH, StreamClass>::~StringWriter()
	{
		Flush();
	}

	template< typename CH, typename StreamClass >
	RED_INLINE void StringWriter<CH, StreamClass>::Reset()
	{
		m_full = false;
		m_pos = 0;
	}

	template< typename CH, typename StreamClass >
	RED_INLINE void StringWriter<CH, StreamClass>::Flush()
	{
		if ( !m_full )
		{
			if ( m_stream.Flush( true, m_buf, m_pos ) )
				m_pos = 0;
		}
	}

	template< typename CH, typename StreamClass >
	RED_INLINE void StringWriter<CH, StreamClass>::FlushWithFail()
	{
		if ( !m_full )
		{
			if ( m_stream.Flush( false, m_buf, m_pos ) )
			{
				m_pos = 0;
			}
			else
			{
				RED_FATAL( "StringWriter overflow!" );
				m_full = true;
			}
		}		
	}

	// put char in the stream
	template< typename CH, typename StreamClass >
	RED_INLINE void StringWriter<CH, StreamClass>::Put( const CH ch )
	{
		if ( m_pos == m_max )
		{
			FlushWithFail();
		}
			
		if ( !m_full && m_pos < m_max )
		{
			m_buf[ m_pos ] = ch;
			++m_pos;
		}
	}

	// append string (or part of it)
	template< typename CH, typename StreamClass >
	RED_INLINE void StringWriter<CH, StreamClass>::Append( const CH* str, const red::Uint32 length /*= -1*/ )
	{
		RED_ASSERT( str != nullptr );

		red::Uint32 pos = length;
		while (pos-- && *str)
			Put(*str++);

		// we ALWAYS have a spare place to put the end terminator
		if ( !m_full )
			m_buf[ m_pos ] = 0;
	}

	// append formated string
	template< typename CH, typename StreamClass >
	STATIC_CHECK_USE_DECL
	RED_INLINE void StringWriter<CH, StreamClass>::Appendf( STATIC_CHECK_PRINTF_MSC const CH* str, ... )
	{
		CH buffer[ FormattedBufferSize ];
		va_list args;

		va_start( args, str );
		red::VSNPrintF( buffer, FormattedBufferSize, str, args );
		va_end( args );

		Append( buffer );
	}

	// append formated string
	template< typename CH, typename StreamClass >
	STATIC_CHECK_USE_DECL
	RED_INLINE void StringWriter<CH, StreamClass>::VAppendf( STATIC_CHECK_PRINTF_MSC const CH* str, va_list args )
	{
		CH buffer[ FormattedBufferSize ];
		red::VSNPrintF( buffer, FormattedBufferSize, str, args );
		Append( buffer );
	}

} // System, Red