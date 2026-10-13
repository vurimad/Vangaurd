/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

/// String writing helper
class CStringPrinter
{
public:
	red::DynArray< AnsiChar >		m_text{ red::PoolEngine() };

public:
	RED_INLINE CStringPrinter()
	{
		m_text.PushBack(0);
	}

	// Get string
	RED_INLINE const AnsiChar* AsChar() const
	{
		RED_ASSERT( m_text.Size() );
		return m_text.TypedData();
	}

	// Append string
	RED_INLINE void Append( const AnsiChar* text )
	{
		const Uint32 len = text ? static_cast< Uint32 >( red::Strlen( text ) ) : 0;
		if ( len )
		{
			const Uint32 oldSize = m_text.Size();
			m_text.Grow( len );
			red::Memcpy( &m_text[ oldSize-1 ], text, len+1 );
		}
	}

	// Prepend string
	RED_INLINE void Prepend( const AnsiChar* text )
	{
		const Uint32 len = text ? static_cast< Uint32 >( red::Strlen( text ) ) : 0;
		if ( len )
		{
			const Uint32 oldSize = m_text.Size();
			m_text.Grow( len );
			red::Memmove( &m_text[ len ], &m_text[ 0 ], oldSize );
			red::Memcpy( &m_text[ 0 ], text, len );
		}
	}

	// Print line
	RED_INLINE void Print( STATIC_CHECK_PRINTF_MSC const AnsiChar* text, ... )
	{
		va_list arglist;
		va_start(arglist, text);
		AnsiChar formattedBuf[ 4096 ];
		red::VSPrintF( formattedBuf,  RED_ARRAY_COUNT(formattedBuf), text, arglist ); 
		const Uint32 len = static_cast< Uint32 >( red::Strlen( formattedBuf ) );
		if ( len )
		{
			Append( formattedBuf );
			Append( "\r\n" );
		}
	}
};