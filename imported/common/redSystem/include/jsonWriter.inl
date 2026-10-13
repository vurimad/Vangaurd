/**
 * Copyright (c) 2019 CD PROJEKT RED. All Rights Reserved.
 */
#pragma once

namespace red
{
	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE JsonWritter<CH, BufferSize, StreamClass>::JsonWritter( StreamClass& sc ) : StackStringWriter< CH, BufferSize, StreamClass >( sc )
	{

	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::Begin()
	{
		RED_ASSERT( m_ident == 0 );
		BeginObject();
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::End()
	{
		RED_ASSERT( m_ident == 1 );
		EndObject();
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::BeginObject()
	{
		BeginBlock( '{' );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	void red::JsonWritter<CH, BufferSize, StreamClass>::BeginObject( const AnsiChar* key )
	{
		AddKey( key );
		BeginObject();
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::EndObject()
	{
		EndBlock( '}' );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::BeginArray()
	{
		BeginBlock( '[' );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	void red::JsonWritter<CH, BufferSize, StreamClass>::BeginArray( const AnsiChar* key )
	{
		AddKey( key );
		BeginArray();
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::EndArray()
	{
		EndBlock( ']' );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddKey( const AnsiChar* name )
	{
		RED_ASSERT( ValidateName( name ), "'%hs' is not valid Json name", name );

		NextElement();
		this->Appendf( "\"%hs\":", name );
		m_needsComa = false;
		m_needsSpace = true;
		m_needsNewLine = false;
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValue( const AnsiChar* value )
	{
		NextElement();
		WriteString( value );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	STATIC_CHECK_USE_DECL
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValueF( STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
	{
		AnsiChar buffer[ 1024 ];
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = VSNPrintF( buffer, RED_ARRAY_COUNT( buffer ), format, arglist );
		va_end( arglist );

		AddValue( buffer );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValue( std::nullptr_t )
	{
		NextElement();
		this->Append( "null" );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValue( Bool value )
	{
		NextElement();
		this->Appendf( "%hs", value ? "true" : "false" );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValue( Uint32 value )
	{
		NextElement();
		this->Appendf( "%u", value );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::AddValue( Float value )
	{
		NextElement();
		this->Appendf( "%f", value );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	void red::JsonWritter<CH, BufferSize, StreamClass>::AddKeyValueF( const AnsiChar* key, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
	{
		AddKey( key );

		AnsiChar buffer[ 1024 ];
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = VSNPrintF( buffer, RED_ARRAY_COUNT( buffer ), format, arglist );
		va_end( arglist );

		AddValue( buffer );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::WriteIdent()
	{
		for( Uint32 i = 0; i < m_ident; ++i )
		{
			this->Put( '\t' );
		}
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::NextElement()
	{
		if( m_needsComa )
		{
			this->Put( ',' );
		}

		if( m_needsSpace )
		{
			this->Put( ' ' );
		}

		if( m_needsNewLine )
		{
			this->Append( "\r\n" );
			WriteIdent();
		}

		m_needsComa = true;
		m_needsSpace = false;
		m_needsNewLine = true;
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::BeginBlock( AnsiChar blockChar )
	{
		NextElement();
		this->Put( blockChar );

		++m_ident;
		m_needsComa = false;
		m_needsSpace = false;
		m_needsNewLine = true;
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::EndBlock( AnsiChar blockChar )
	{
		RED_ASSERT( m_ident > 0 );
		--m_ident;
		if( !m_needsComa )
		{
			m_needsNewLine = false;
			m_needsSpace = true;
		}
		m_needsComa = false;

		NextElement();
		this->Put( blockChar );

		m_needsComa = true;
		m_needsSpace = false;
		m_needsNewLine = true;
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE void JsonWritter<CH, BufferSize, StreamClass>::WriteString( const AnsiChar* value )
	{
		this->Put( '\"' );

		if( value )
		{
			while( *value )
			{
				switch( *value )
				{
				case '\\':
				case '"':
					this->Put( '\\' );
					this->Put( *value );
					break;
				case '\b':
					this->Append( "\\b" );
					break;
				case '\t':
					this->Append( "\\t" );
					break;
				case '\n':
					this->Append( "\\n" );
					break;
				case '\f':
					this->Append( "\\f" );
					break;
				case '\r':
					this->Append( "\\r" );
					break;
				default:
					if( *value < ' ' || ( Uint8 )*value >= ( Uint8 )0x7F )
					{
						this->Appendf( "\\u%04X", ( Uint32 )( Uint8 )*value );
					}
					else
					{
						this->Put( *value );
					}
				}

				++value;
			}
		}

		this->Put( '\"' );
	}

	template< typename CH, Uint32 BufferSize, typename StreamClass >
	RED_INLINE Bool JsonWritter<CH, BufferSize, StreamClass>::ValidateName( const AnsiChar* name )
	{
		if( !name )
		{
			return false;
		}

		if( !*name )
		{
			return false;
		}

		while( *name )
		{
			if( *name < ' ' || ( Uint8 )*name >= ( Uint8 )0x7F )
			{
				return false;
			}
			++name;
		}

		return true;
	}
}
