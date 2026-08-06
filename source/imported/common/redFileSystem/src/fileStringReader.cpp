/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../redContainers/include/string/string.h"
#include "fileStringReader.h"
#include "file.h"

namespace red
{

CAnsiStringFileReader::CAnsiStringFileReader()
	: m_buffer( nullptr )
	, m_end( nullptr )
	, m_pos( nullptr )
	, m_line( 1 )
{}

CAnsiStringFileReader::CAnsiStringFileReader( IFile* file )
	: m_buffer( nullptr )
	, m_end( nullptr )
	, m_pos( nullptr )
	, m_line( 1 )
{
	if ( file )
	{
		static_assert( sizeof(AnsiChar) == 1, "AnsiChar size is not 1 byte ?!" );

		const Uint32 length = (const Uint32) file->GetSize();
		m_buffer = (AnsiChar*) RED_ALLOCATE( red::PoolEngine, (length+1) );
		file->Seek( 0 );
		file->Serialize( m_buffer, length );
		m_buffer[ length ] = 0;

		m_end = m_buffer + length;
		m_pos = m_buffer;
	}
}

CAnsiStringFileReader::CAnsiStringFileReader( const String& string )
	: m_buffer( nullptr )
	, m_end( nullptr )
	, m_pos( nullptr )
	, m_line( 1 )
{
	if ( !string.Empty() )
	{
		static_assert( sizeof(AnsiChar) == 1, "AnsiChar size is not 1 byte ?!" );

		const Uint32 length = string.Length();
		m_buffer = (AnsiChar*) RED_ALLOCATE( red::PoolEngine, ( length+1 ) );
		red::Memcpy( m_buffer, string.Data(), length );
		m_buffer[ length ] = 0;

		m_end = m_buffer + length;
		m_pos = m_buffer;
	}
}

CAnsiStringFileReader::~CAnsiStringFileReader()
{
	if ( m_buffer )
	{
		RED_FREE( red::PoolEngine, m_buffer );
		m_buffer = nullptr;
	}

	m_end = nullptr;
	m_pos = nullptr;
}

Bool CAnsiStringFileReader::ParseKeyword( const AnsiChar* keyword, const Uint32 length /*= (Uint32)-1*/, const Bool allowLineBreak /*= true*/ )
{
	if ( !SkipWhitespaces( allowLineBreak ) )
		return false;

	const Uint32 compareLength = Min< Uint32 >( length, (const Uint32) red::Strlen(keyword) );
	if ( 0 == red::Strcmp( m_pos, keyword, compareLength ) )
	{
		m_pos += compareLength;
		return true;
	}

	return false;
}

Bool CAnsiStringFileReader::ParseNumber( String& outNumber, const Bool allowLineBreak /*= true*/ )
{
	if ( !SkipWhitespaces( allowLineBreak ) )
		return false;

	// extract numerical value
	const AnsiChar* read = m_pos;
	if ( isdigit(*read) || *read == '-' )
	{
		// skip the sign
		if (*read == '-' )
			read+=1;

		// parse the numerical part
		Bool hasDot = false;
		while ( read < m_end )
		{
			// decimal point
			if ( *read == '.' )
			{
				if ( hasDot )
					break;
				hasDot = true;
				++read;
				continue;
			}

			// we only support digits
			if ( !isdigit( *read ) )
				break;

			// continue
			++read;
		}

		// optional 'f' ending
		if ( *read == 'f' )
			++read;

		// assemble final number
		RED_ASSERT( read > m_pos, "Unexpected null token" );
		outNumber = String( m_pos, (Uint32)(read - m_pos), red::PoolEngine());
		m_pos = read;

		return true;
	}

	// no number parsed
	return false;
}

Bool CAnsiStringFileReader::ParseIdent( String& outIdent, const Bool allowLineBreak /*= true*/ )
{
	if ( !SkipWhitespaces( allowLineBreak ) )
		return false;

	// extract ident characters
	const AnsiChar* read = m_pos;
	if ( *read == '_' || isalnum(*read) )
	{
		// Grab normal text
		while ( (read < m_end) && ( *read == '_' || *read == '/' || *read == '(' || *read == ')' || *read == ',' || isalnum(*read) ) )
		{
			++read;
		}

		// create output string
		RED_ASSERT( read > m_pos, "Unexpected null token" );
		outIdent = String( m_pos, (Uint32)(read - m_pos), red::PoolEngine() );
		m_pos = read;

		return true;
	}

	// no valid identifier
	return false;
}

Bool CAnsiStringFileReader::ParseIdentCustom( String& outIdent, const red::FixedSizeFunction< Bool( const AnsiChar c ) >& cmpFunc, const Bool allowLineBreak /*= true */ )
{
	if( !SkipWhitespaces( allowLineBreak ) )
		return false;
	
	// extract ident characters
	const AnsiChar* read = m_pos;
	if( *read == '_' || isalnum( *read ) )
	{
		// Grab normal text
		while( ( read < m_end ) && ( isalnum( *read ) || cmpFunc( *read ) ) )
		{
			++read;
		}

		// create output string
		RED_ASSERT( read > m_pos, "Unexpected null token" );
		outIdent = String( m_pos, (Uint32)( read - m_pos ), red::PoolEngine());
		m_pos = read;

		return true;
	}

	// no valid identifier
	return false;
}

Bool CAnsiStringFileReader::ParseString( String& outString, const Bool allowLineBreak /*= true*/ )
{
	if ( !SkipWhitespaces( allowLineBreak ) )
		return false;

	// extract from quotes
	const AnsiChar* read = m_pos;
	if ( *m_pos == '\'' || *m_pos == '\"' )
	{
		const AnsiChar delim = *read++;

		// scan until we find matching quote
		while ( read < m_end )
		{
			if ( *read == delim )
				break;

			++read;
		}

		// output only valid strings
		if ( *read == delim )
		{
			outString = String( m_pos+1, (Uint32)(read-m_pos)-1, red::PoolEngine());
			m_pos = read+1; // move past the quote
			return true;
		}
	}
	else
	{
		// normal string - parse till we find a white space
		while ( read < m_end )
		{
			if ( *read <= ' ' )
				break;

			++read;
		}

		// output string
		RED_ASSERT( read > m_pos, "Unexpected null token" );
		outString = String( m_pos, (Uint32)(read-m_pos), red::PoolEngine());
		m_pos = read;
		return true;
	}

	// no valid string found
	return false;
}

Bool CAnsiStringFileReader::ParseStringBetweenSymbols( String& outString, const AnsiChar startSymbol, const AnsiChar endSymbol, const Bool ignoreWhitespaces /*= true*/, const Bool allowLineBreak /*= true */ )
{
	if( !SkipWhitespaces( allowLineBreak ) )
		return false;

	const AnsiChar* read = m_pos;
	if( *read == startSymbol )
	{
		++read;

		if( ignoreWhitespaces && *read == ' ' )
		{
			++read;
		}

		const AnsiChar* firstToExtract = read;
		Uint16 size = 0;

		while( *read != endSymbol )
		{
			++read;
			++size;
		}

		const AnsiChar* temp = read;
		if( ignoreWhitespaces && *--temp == ' ' )
		{
			--size;
		}

		RED_ASSERT( read > m_pos, "Unexpected null token" );
		outString = String( firstToExtract, size, red::PoolEngine());
		m_pos = read + 1;
		return true;
	}
	else
	{
		return false;
	}
}

Bool CAnsiStringFileReader::ParseToken( String& outToken, const Bool allowLineBreak /*= true*/ )
{
	if ( !SkipWhitespaces( allowLineBreak ) )
		return false;

	// stuff in quotes is always parsed as string
	if ( *m_pos == '\'' || *m_pos == '\"' )
		return ParseString( outToken, allowLineBreak );

	// number ?
	if ( ParseNumber( outToken, allowLineBreak ) )
		return true;

	// try to parse as an ident
	if ( ParseIdent( outToken, allowLineBreak ) )
		return true;

	// parse as a single character token
	outToken = String( m_pos, 1, red::PoolEngine());
	m_pos += 1;
	return true;	
}

void CAnsiStringFileReader::SkipCurrentLine()
{
	while ( m_pos < m_end )
	{
		if ( *m_pos++ == '\n' )
		{
			m_line += 1;
			break;
		}
	}
}

Uint32 CAnsiStringFileReader::GetLine() const
{
	return m_line;
}

Bool CAnsiStringFileReader::EndOfFile() const
{
	return (m_pos >= m_end);
}

Bool CAnsiStringFileReader::SkipWhitespaces( const Bool allowLineBreak )
{
	while ( (m_pos < m_end) && (*m_pos <= ' ') )
	{
		// count lines
		if ( *m_pos == '\n' )
		{
			if ( !allowLineBreak )
				return false;

			m_line += 1;
		}

		++m_pos;
	}

	return (m_pos != m_end);
}

} // Red