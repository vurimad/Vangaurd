/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/


//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../redMemory/include/poolRoot.h"
#include "../../redMemory/include/redMemoryPublic.h"


//////////////////////////////////////////////////////////////////////////
// implementation
CAnsiToUnicode::~CAnsiToUnicode()
{
	if (m_buf && m_buf != m_staticBuf)
	{
		RED_FREE( red::PoolEngine, m_buf );
	}
}

// todo:
void CAnsiToUnicode::convert(const AnsiChar* src)
{
	if ( src == nullptr )
	{
		m_buf = m_staticBuf;
		m_buf[ 0 ] = 0;
		return;
	}

	const size_t len = red::Strlen(src) + 1;

	if (len > MAX_STATIC_CONV_LEN)
	{
		m_buf = reinterpret_cast<UniChar*>( RED_ALLOCATE( red::PoolEngine, sizeof(UniChar) * len) );
	}
	else 
	{
		m_buf = m_staticBuf;
	}

#if defined( RED_COMPILER_MSC )
	mbstate_t mbst = { 0 };
#elif defined( RED_COMPILER_CLANG )
	mbstate_t mbst; // has a ctor for C++
#else
#	error Unsupported compiler
#endif

#if defined( RED_PLATFORM_LINUX )
	 size_t result = mbsrtowcs(m_buf, &src, len, &mbst );
	 // mbsrtowcs_s: if no null character was written to dst after len wide characters were written, then L'\0' is stored in dst[len], which means len+1 total wide characters are written
	 m_buf[ len - 1 ] = L'\0';
	 RED_ERROR( result != (size_t)(-1), "Failed to convert string %hs to Unicode", src );
#else
	size_t resultSize = 0;
	errno_t result = mbsrtowcs_s( &resultSize, m_buf, len, &src, len, &mbst );
	RED_ERROR( result == 0, "Failed to convert string %hs to Unicode", src );
#endif
	
	
}

CUnicodeToAnsi::~CUnicodeToAnsi()
{
	if (m_buf && m_buf != m_staticBuf)
	{
		RED_FREE( red::PoolEngine, m_buf );
	}
}

// todo:
void CUnicodeToAnsi::convert( const UniChar* src )
{
	if ( src == nullptr )
	{
		m_buf = m_staticBuf;
		m_buf[ 0 ] = 0;
		return;
	}

	size_t len = 0;
#if defined( RED_COMPILER_MSC )
	mbstate_t mbst = { 0 };
#elif defined( RED_COMPILER_CLANG )
	mbstate_t mbst; // has a ctor for C++
#endif

#if defined( RED_PLATFORM_LINUX )
	size_t result = wcsrtombs( nullptr, &src, 0, &mbst );
	len = result;
	Bool failed = result == (size_t)(-1);
#else
	errno_t result = wcsrtombs_s( &len, nullptr, 0, &src, 0, &mbst );
	Bool failed = result != 0;
#endif
	if ( failed )
	{
		m_buf = m_staticBuf;
		m_buf[ 0 ] = 0;
		return;
	}
	len += 1;

	if (len > MAX_STATIC_CONV_LEN)
	{
		m_buf = reinterpret_cast<AnsiChar*>( RED_ALLOCATE( red::PoolEngine, sizeof(AnsiChar) * len) );
	}
	else 
	{
		m_buf = m_staticBuf;
	}

#if defined( RED_PLATFORM_LINUX )
	result = wcsrtombs( m_buf, &src, len, &mbst );
	// if the conversion stops without writing a null character, 
	// the function will store '\0' in the next byte in dst, which may be dst[len] or dst[dstsz]
	m_buf[ len - 1 ] = '\0';

	RED_ERROR( result != (size_t)(-1), "Failed to convert Unicode string to ANSI, converted '%hs'", m_buf );
#else
	size_t resultSize = 0;
	result = wcsrtombs_s( &resultSize, m_buf, len, &src, len, &mbst );

	RED_ERROR( result == 0, "Failed to convert Unicode string to ANSI, converted '%hs'", m_buf );
#endif
}
