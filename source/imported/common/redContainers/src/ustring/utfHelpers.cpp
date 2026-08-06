#include "build.h"

#include "ustring/utfHelpers.h"

namespace red {

static void LogInvalidUtf8Sequence( const Uint8* buffer, Uint32 seqLen )
{
	RED_FATAL_ASSERT( seqLen >= 1 && seqLen <= 4 );

	if ( seqLen == 1 )
	{
		RED_LOG_ERROR( "Invalid UTF-8 sequence: %02X", buffer[ 0 ] );
	}
	else if ( seqLen == 2 )
	{
		RED_LOG_ERROR( "Invalid UTF-8 sequence: %02X %02X", buffer[ 0 ], buffer[ 1 ] );
	}
	else if ( seqLen == 3 )
	{
		RED_LOG_ERROR( "Invalid UTF-8 sequence: %02X %02X %02X", buffer[ 0 ], buffer[ 1 ], buffer[ 2 ] );
	}
	else if ( seqLen == 4 )
	{
		RED_LOG_ERROR( "Invalid UTF-8 sequence: %02X %02X %02X %02X", buffer[ 0 ], buffer[ 1 ], buffer[ 2 ], buffer[ 3 ] );
	}
}

// decode single codepoint from a UTF-8 buffer
// will return -1 in case of any decoding error encountered
static CodePoint DecodeUtf8Character( const Uint8* buffer, Uint32 bufferSize, Uint32& outSequenceLength )
{
	RED_ASSERT( buffer != nullptr, "Invalid buffer" );

	// handle null-terminator
	if ( buffer[ 0 ] == 0 )
	{
		outSequenceLength = 0;
		return 0;
	}

	Uint8 c1, c2;
	CodePoint codePoint = 0;

	c1 = buffer[ 0 ];

	// read data from the first byte and determine sequence length
	Uint32 seqlen = 0;
	if ( ( c1 & 0x80 ) == 0 )
	{
		codePoint = c1 & 0x7F;
		seqlen = 1;
	}
	else if ( ( c1 & 0xE0 ) == 0xC0 )
	{
		codePoint = c1 & 0x1F;
		seqlen = 2;
	}
	else if ( ( c1 & 0xF0 ) == 0xE0 )
	{
		codePoint = c1 & 0x0F;
		seqlen = 3;
	}
	else if ( ( c1 & 0xF8 ) == 0xF0 )
	{
		codePoint = c1 & 0x07;
		seqlen = 4;
	}
	else
	{
		RED_LOG_ERROR( "Invalid UTF-8 string, first byte: %02X", (Uint32)c1 );
		return -1;
	}

	if ( seqlen > bufferSize )
	{
		RED_LOG_ERROR( "UTF-8 buffer is too short: expected %u-byte sequence, got %u bytes", seqlen, bufferSize );
		return -1;
	}

	// check if the buffer is long enough
	for ( Uint32 i = 1; i < seqlen; ++i )
	{
		if ( buffer[ i ] == 0 )
		{
			RED_LOG_ERROR( "Invalid UTF-8 string, expected %u-byte sequence, got %u bytes", seqlen, i + 1 );
			return -1;
		}
	}

	// validate sequence bytes
	for ( Uint32 i = 1; i < seqlen; ++i )
	{
		if ( ( buffer[ i ] & 0xC0 ) != 0x80 )
		{
			LogInvalidUtf8Sequence( buffer, seqlen );
			return -1;
		}
	}

	if ( seqlen == 2 )
	{
		if ( c1 < 0xC2 || c1 > 0xDF )
		{
			LogInvalidUtf8Sequence( buffer, seqlen );
			return -1;
		}
	}
	else if ( seqlen == 3 )
	{
		c2 = buffer[ 1 ];

		switch ( c1 )
		{
		case 0xE0:
			if ( c2 < 0xA0 || c2 > 0xBF )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;

		case 0xED:
			if ( c2 < 0x80 || c2 > 0x9F )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;

		default:
			if ( ( c1 < 0xE1 || c1 > 0xEC ) && ( c1 < 0xEE || c1 > 0xEF ) )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;
		}
	}
	else if ( seqlen == 4 )
	{
		c2 = buffer[ 1 ];

		switch ( c1 )
		{
		case 0xF0:
			if ( c2 < 0x90 || c2 > 0xBF )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;

		case 0xF4:
			if ( c2 < 0x80 || c2 > 0x8F )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;

		default:
			if ( c1 < 0xF1 || c1 > 0xF3 )
			{
				LogInvalidUtf8Sequence( buffer, seqlen );
				return -1;
			}
			break;
		}
	}

	// collect codepoint bits from all the bytes
	for ( Uint32 i = 1; i < seqlen; ++i )
	{
		codePoint = ( ( codePoint << 6 ) | (Uint32)( buffer[ i ] & 0x3F ) );
	}

	outSequenceLength = seqlen;
	return codePoint;
}

UtfStringIterator::UtfStringIterator( const char* buffer )
	: m_buffer( reinterpret_cast<const Uint8*>( buffer ) )
	, m_byteOffset( 0 )
	, m_encoding( UTF8 )
{
	size_t len = strlen( buffer );
	len = Min<size_t>( len, UINT32_MAX );
	m_bufferSize = static_cast<Uint32>( len );

	RED_FATAL_ASSERT( buffer, "Invalid string buffer" );
}

CodePoint UtfStringIterator::ParseNextCharacter()
{
	CodePoint result = '?';

	if ( m_encoding == UTF8 )
	{
		const Uint8* buffer = m_buffer + m_byteOffset;

		Uint32 seqLen = 0;
		result = DecodeUtf8Character( buffer, m_bufferSize - m_byteOffset, seqLen );

		// UTF-8 parsing error, discard rest of the characters
		if ( result == -1 )
		{
			m_byteOffset = m_bufferSize;
			result = -1;
		}
		else
		{
			RED_ASSERT( m_byteOffset + seqLen <= m_bufferSize, "Buffer overflow. Indicates bug in DecodeUtf8Character" );
			m_byteOffset += seqLen;
		}
	}
	else if ( m_encoding == UTF32 )
	{
		result = *reinterpret_cast<const CodePoint*>( m_buffer + m_byteOffset );
		m_byteOffset += sizeof( CodePoint );
	}
	else
	{
		RED_FATAL( "Invalid string encoding" );
	}

	return result;
}

Uint32 UtfStringIterator::ComputeLength() const
{
	Uint32 length = 0;

	UtfStringIterator iterator = *this;
	while ( iterator.IsValid() )
	{
		length++;
		iterator.ParseNextCharacter();
	}

	return length;
}

bool ConvertUtf8ToUtf32( const StringView input, DynArray< CodePoint >& output )
{
	UtfStringIterator stringIterator( input );

	while ( stringIterator.IsValid() )
	{
		const CodePoint codePoint = stringIterator.ParseNextCharacter();
		if ( codePoint == -1 )
		{
			return false;
		}

		output.PushBack( codePoint );
	}

	return true;
}

bool ValidateUtf8( const StringView input )
{
	UtfStringIterator stringIterator( input );

	while ( stringIterator.IsValid() )
	{
		const CodePoint codePoint = stringIterator.ParseNextCharacter();
		if ( codePoint == -1 )
		{
			return false;
		}
	}

	return true;
}

} // red