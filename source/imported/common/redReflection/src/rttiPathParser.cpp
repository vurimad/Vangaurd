/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiPathParser.h"
#include "rttiAccessPath.h"

namespace rtti
{
	PathParser::PathParser( const AccessPath& path )
		: m_localCopyBuffer( path.ToString() ) // COPY
	{
		m_pos = m_localCopyBuffer.AsChar();
		m_end = m_pos + m_localCopyBuffer.Length();
	}

	PathParser::PathParser( const String& rawPath )
		: m_localCopyBuffer( rawPath ) // COPY
	{
		m_pos = m_localCopyBuffer.AsChar();
		m_end = m_pos + m_localCopyBuffer.Length();
	}

	PathParser::PathParser( const AccessPath* pathNoCopy )
	{
		m_pos = pathNoCopy ? pathNoCopy->ToString().AsChar() : "";
		m_end = m_pos + red::Strlen( m_pos );
	}

	PathParser::PathParser( const AnsiChar* rawPathNoCopy )
	{
		m_pos = rawPathNoCopy ? rawPathNoCopy : "";
		m_end = m_pos + red::Strlen( m_pos );
	}

	const Bool PathParser::EatName( CName& outName )
	{
		// skip potential whitespaces
		while ( m_pos < m_end )
		{
			if (*m_pos > ' ' )
				break;

			++m_pos;
		}

		// end of stream
		if ( m_pos >= m_end )
			return false;

		// it's an array index NOT a property name
		if ( *m_pos == '[' )
			return false;

		// #fixme: could parse more strictly, since could allow name..name
		// skip over initial dot in a case like [3].prop, otherwise will parse as empty prop name.
		if ( *m_pos == '.' )
			++m_pos;
		if ( m_pos >= m_end )
			return false;

		// extract property name
		const auto* start = m_pos;
		while ( m_pos < m_end )
		{
			// parse till next child element or array index
			if ( *m_pos == '[' || *m_pos == '.' )
				break;

			++m_pos;
		}

		AnsiChar nameBuf[ 128 ];

		// get length of the ident
		const Uint32 identLength = (Uint32)(m_pos - start);
		RED_FATAL_ASSERT( identLength < RED_ARRAY_COUNT_U32(nameBuf), "Identifier is to long" );

		// extract string
		red::Memcpy( nameBuf, start, identLength );
		nameBuf[ identLength ] = 0;

		// output name
		outName = RED_NAME( nameBuf );
		return true;
	}

	const Bool PathParser::EatIndex( Int32& outArrayIndex )
	{
		// skip potential whitespaces
		while ( m_pos < m_end )
		{
			if (*m_pos > ' ' )
				break;

			++m_pos;
		}

		// end of stream
		if ( m_pos >= m_end )
			return false;

		// it's NOT an array index NOT a property name
		if ( *m_pos != '[' )
			return false;

		// skip the starting bracket
		m_pos += 1;

		// extract index name
		const auto* start = m_pos;
		while ( m_pos < m_end )
		{
			// parse till next child element or array index
			if ( *m_pos == ']' )
				break;
			++m_pos;
		}

		// we reached end of stream - there was no closing bracket
		if ( m_pos >= m_end )
			return false;

		// extract data
		AnsiChar numberBuf[ 16 ];

		// get length of the value
		const Uint32 numberLength = (Uint32)(m_pos - start);
		RED_FATAL_ASSERT( numberLength < RED_ARRAY_COUNT_U32(numberBuf), "Number is to long" );

		// exctract string
		red::Memcpy( numberBuf, start, numberLength );
		numberBuf[ numberLength ] = 0;

		// skip the ']'
		m_pos += 1;

		// get the number
		outArrayIndex = atoi( numberBuf );
		return true;
	}

	AccessPath PathParser::GetUneatenPath() const
	{
		if ( m_pos >= m_end )
			return AccessPath(); // just in case

		return AccessPath( m_pos );
	}

} // rtti