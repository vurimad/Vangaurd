/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef _WINDOWS
#	include <intrin.h>
#else
#	include <x86intrin.h>
#endif

#include "context.h"
#include "listener.h"
#include "state.h"

unsigned int CountCharacters( const char* token, unsigned int length );

class InternalState : public lexer::native::State
{
public:
	InternalState( const char* source, lexer::native::IListener* emitter )
		: State( source )
		, m_listener( emitter )
	{
	}

	inline void NextLine()
	{
		++m_currentContext.m_line;
		m_lineStart = m_currentContext;
	}

	inline void StoreTokenStart()
	{
		m_tokenStart = m_currentContext;
	}

	inline void StoreSequenceStart( unsigned int charactersToSkip = 0 )
	{
		m_sequenceStart = m_tokenStart;
		if ( charactersToSkip )
		{
			m_sequenceStart.m_byte += charactersToSkip;
			m_sequenceStart.m_character += charactersToSkip;
		}

		m_sectionLineStart = m_lineStart;
	}

	inline void UpdateContext( const char* token, unsigned int tokenByteLength )
	{
		m_currentContext.m_byte += tokenByteLength;
		m_currentContext.m_character += CountCharacters( token, tokenByteLength );
	}

	inline void EmitToken( unsigned int id )
	{
		m_listener->Token( *this, id );
	}

	inline void EmitSequence( unsigned int id )
	{
		m_listener->Sequence( *this, id );
	}

	inline void EmitComment()
	{
		m_listener->Comment( *this );
	}

	inline void EmitError()
	{
		m_listener->Error( *this );
	}

private:
	lexer::native::IListener* m_listener;
};

unsigned int CountUtf8Bytes( const char utf8Byte )
{
	unsigned long index = 0;
	unsigned long mask = (~utf8Byte);

#ifdef _WINDOWS
	::_BitScanReverse( &index, mask );
#else
	// We're counting the bits 
	index = 31 - __lzcnt32( mask );
#endif

	// We have the 0-index Context of the most significant bit, which we need to turn into a count of the number of bits (or 1-index Context)
	// Then reverse the order so that we have a count of the number of bits leading up to the MSB
	return 8 - ( index + 1 );
}

unsigned int CountCharacters( const char* token, unsigned int size )
{
	unsigned int length = 0;
	for ( unsigned int i = 0; i < size; ++i )
	{
		if ( ( token[ i ] & 0x80 ) )
		{
			const unsigned int sizeOfUtf8Character = CountUtf8Bytes( token[ i ] );
			const unsigned int bytesToSkip = sizeOfUtf8Character - 1;

			i += bytesToSkip;
		}

		++length;
	}

	return length;
}
