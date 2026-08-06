/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../include/string/tokenizer.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::Distance;
using red::String;
using red::alg::BinarySearch;


CTokenizer::CTokenizer( const String& toTokenize, const String& delimiters ) 
	: m_toTokenize( toTokenize )
	, m_delimiters( delimiters )
	, m_tokenized( red::PoolEngine() ) 
{
	// sort tokens for faster lookup
	std::sort( m_delimiters.Begin(), m_delimiters.End() );	

	Tokenize();
}

Uint32 CTokenizer::GetNumTokens() const
{
	return m_tokenized.Size();
}

String CTokenizer::GetToken( Uint32 i ) const
{
	if ( i >= m_tokenized.Size() )
	{
		return String::EMPTY();
	}

	const tTokenDelims &currToken = m_tokenized[ i ];
	Uint32 tokenBegin = Distance< Uint32 >( m_toTokenize.Begin(), currToken.first );
	Uint32 tokenLength = Distance< Uint32 >( currToken.first, currToken.second );

	return String( m_toTokenize.AsChar() + tokenBegin, tokenLength );
}

Bool CTokenizer::IsDelimiter( char c ) const
{
	return BinarySearch( m_delimiters.Begin(), m_delimiters.End(), c ) != m_delimiters.End();
}

void CTokenizer::Tokenize()
{
	// Do not tokenize empty strings
	if ( m_toTokenize.Empty() )
	{
		return;
	}

	tStringIter currBegin = m_toTokenize.Begin();
	tStringIter currEnd = m_toTokenize.Begin();
	tStringIter *currIter = &currBegin;

	Bool outsideToken = true;
	while( *currIter != m_toTokenize.End() )
	{
		if ( IsDelimiter( *(*currIter) ) ==  outsideToken )
		{
			++(*currIter);
			continue;
		}
		else
		{
			// entering/leaving token
			if ( outsideToken )
			{
				// entering token, switch current iterator
				currEnd = currBegin;
				currIter = &currEnd;			
			}
			else
			{
				// leaving token, emit
				m_tokenized.PushBack( tTokenDelims( currBegin, currEnd ) );

				currBegin = currEnd;
				currIter = &currBegin;
			}
			
			outsideToken = !outsideToken;
		}
	}

	if ( !outsideToken )
	{
		// emit last token
		m_tokenized.PushBack( tTokenDelims( currBegin, currEnd ) );
	}
}


