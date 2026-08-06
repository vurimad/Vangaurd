/**
 * Copyright (c) 2019 CD PROJEKT RED. All Rights Reserved.
 */
#pragma once

#include "stringWriter.h"

namespace red
{
	template< typename CH, Uint32 BufferSize, typename StreamClass = StringWriterStream::ConstantSizeStream< CH > >
	class JsonWritter : public StackStringWriter< CH, BufferSize, StreamClass >
	{
	public:
		JsonWritter( StreamClass& sc = StreamClass::GetInstance() );

		void Begin();
		void End();

		void BeginObject();
		void BeginObject( const AnsiChar* key );
		void EndObject();

		void BeginArray();
		void BeginArray( const AnsiChar* key );
		void EndArray();

		void AddKey( const AnsiChar* key );

		void AddValue( const AnsiChar* value );
		void AddValueF( STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... );
		void AddValue( std::nullptr_t );
		void AddValue( Bool value );
		void AddValue( Uint32 value );
		void AddValue( Float value );

		template< typename T >
		RED_INLINE void AddKeyValue( const AnsiChar* key, const T& value )
		{
			AddKey( key );
			AddValue( value );
		}
		void AddKeyValueF( const AnsiChar* key, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... );

	private:
		void WriteIdent();
		void NextElement();
		void BeginBlock( AnsiChar blockChar );
		void EndBlock( AnsiChar blockChar );
		void WriteString( const AnsiChar* value );

		Bool ValidateName( const AnsiChar* name );

		Uint32 m_ident = 0;
		Bool m_needsComa = false;
		Bool m_needsSpace = false;
		Bool m_needsNewLine = false;
	};

}

#include "jsonWriter.inl"
