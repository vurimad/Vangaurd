/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers 
#include "../redContainersApi.h"
#include "../../../redSystem/include/types.h"


static constexpr size_t MAX_STATIC_CONV_LEN = 256;

RED_WARNING_PUSH()
RED_DISABLE_WARNING_MSC( 4996 )


class RED_CONTAINERS_API CAnsiToUnicode
{
	UniChar		m_staticBuf[MAX_STATIC_CONV_LEN];
	UniChar*	m_buf;

public:
	explicit CAnsiToUnicode( const AnsiChar* src )
		: m_buf(0)
	{
		convert(src);
	}

	~CAnsiToUnicode();

	void convert(const AnsiChar* src);

	operator UniChar* () const
	{
		return m_buf;
	}
};

class RED_CONTAINERS_API CUnicodeToAnsi
{
	AnsiChar	m_staticBuf[MAX_STATIC_CONV_LEN];
	AnsiChar*	m_buf;

public:
	explicit CUnicodeToAnsi( const UniChar* src )
		: m_buf(0)
	{
		convert(src);
	}

	~CUnicodeToAnsi();

	void convert( const UniChar* src );

	operator AnsiChar* () const
	{
		return m_buf;
	}
};

#define ANSI_TO_UNICODE(str)	static_cast<UniChar*>( CAnsiToUnicode( str ) )
#define UNICODE_TO_ANSI(str)	static_cast<AnsiChar*>( CUnicodeToAnsi( str ) )


RED_INLINE Bool IsUpper( Char c )
{
	return c >= 'A' && c <= 'Z';
}

RED_INLINE Bool IsLower( Char c )
{
	return c >= 'a' && c <= 'z';
}

RED_INLINE Bool IsNumber( Char c )
{
	return c >= '0' && c <= '9';
}

////////////////////////////////////////////////////

RED_WARNING_POP()

////////////////////////////////////////////////////