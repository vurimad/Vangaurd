/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include "../pair.h"
#include "../dynArray.h"
#include "string.h"


class RED_CONTAINERS_API CTokenizer
{
protected:
	typedef red::String::const_iterator				tStringIter;
	typedef std::pair<tStringIter, tStringIter>		tTokenDelims;

	red::String						m_toTokenize;
	red::String						m_delimiters;
	red::DynArray< tTokenDelims >	m_tokenized;

public:
	CTokenizer( const red::String& toTokenize, const red::String& delimiters );

public:
	Uint32 GetNumTokens() const;

	red::String GetToken( Uint32 i ) const;

protected:
	void Tokenize();

	Bool IsDelimiter( char c ) const;
};


// CEnumeratingTokenizer - should be used only locally, as delimiters are not stored in the tokenizer, and there's a limit on size of the tokenized string
template< int _SIZE = 256 >
class CStaticTokenizer
{
protected:
	char		m_toTokenize[ _SIZE ];
	const char*	m_delimiters;
	char*		m_currentToken;

public:
	CStaticTokenizer( const char* toTokenize, const char* delimiters )
	{
		Uint32 length = static_cast< Uint32 >( red::Strlen( toTokenize ) );
		RED_ASSERT( (length+3) < _SIZE );

		m_delimiters = delimiters;
		m_currentToken = nullptr;

		Uint32 readIndex = 0;
		Uint32 writeIndex = 0;
		Bool outsideToken = true;
		for ( ;; )
		{
			if( readIndex == length )
				break;
			char c = toTokenize[ readIndex++ ];
			if( IsDelimiter( c ) == true )
			{
				if( outsideToken == false )
				{
					// was parsing token
					m_toTokenize[ writeIndex++ ] = 0;
					outsideToken = true;
				}
				continue;
			}
			outsideToken = false;
			if( m_currentToken == nullptr )
				m_currentToken = &m_toTokenize[ writeIndex ];
			m_toTokenize[ writeIndex++ ] = c;
		}
		m_toTokenize[ writeIndex++ ] = 0;
		m_toTokenize[ writeIndex++ ] = 0; // 2nd zero for EndTokenizeString
	}

	char* GetNextToken()
	{
		char* result = m_currentToken;
		Uint32 length = result ? static_cast< Uint32 >( red::Strlen( result ) ) : 0;
		if ( length == 0 )
		{
			return nullptr;
		}
		m_currentToken = &m_currentToken[ length + 1 ];
		return result;
	}

protected:

	Bool IsDelimiter( char c ) const
	{
		// lets assume that there are no more than 1-2 delimiters (so no fancy search is required)
		for( int i = 0; m_delimiters[i] != 0; i++ )
		{
			if( m_delimiters[i] == c )
				return true;
		}
		return false;
	}
};
