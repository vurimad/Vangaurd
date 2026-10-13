/**
* Copyright (c) 2012-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fundamentalStringParser.h"
#include <limits.h>

Bool GParseWhitespaces( const AnsiChar*& stream )
{
	if ( !stream )
	{
		return false;
	}

	// Eat white spaces
	while ( *stream && *stream <= ' ' )
	{
		stream++;
	}

	// 
	return *stream != 0;
}

Bool GParseIdentifier( const AnsiChar*& stream, String& value )
{	
	String outputVal;

	if ( stream && GParseWhitespaces( stream ) )
	{
		if( *stream == '_' || iswalpha(*stream) )
		{
			// Grab normal text
			while( *stream && ( *stream == '_' || iswalnum(*stream) ) )
			{
				AnsiChar chars[2] = { *stream, 0 };
				outputVal += chars;
				stream++;				
			}
		}
		if( outputVal.Length() )
		{
			// Return grabbed string
			value = outputVal;
			return true;
		}
	}

	return false;
}

Bool GParseKeyword( const AnsiChar*& stream, const AnsiChar* keyword )
{
	if ( stream && GParseWhitespaces( stream ) )
	{
		const Int32 N = static_cast< Int32 >( red::Strlen( keyword ) );
		if ( 0 == red::StrcmpNC( stream, keyword, N ) )
		{
			// Matched, move pointer
			stream += N;
			return true;
		}
	}

	// Not matched
	return false;
}

Bool GParseString( const AnsiChar*& stream, String& value )
{	
	String outputVal;

	if ( stream && GParseWhitespaces( stream ) )
	{
		if ( *stream == '\"' )
		{
			// Grab quoted text
			stream++;
			while ( *stream && *stream != '\"' )
			{
				outputVal += *stream;
				stream++;
			}

			if (*stream == '\"')
				stream++;
		}
		else
		{
			// Grab normal text
			while ( *stream && *stream > ' ' )
			{
				outputVal += *stream;
				stream++;				
			}
		}
	}
	else
	{
		return false;
	}

	// Return grabbed string
	value = outputVal;
	return true;
}

static Bool IsAlphaNum( Char ch )
{
	if ( ch >= '0' && ch <= '9') return true;
	if ( ch >= 'A' && ch <= 'Z') return true;
	if ( ch >= 'a' && ch <= 'z') return true;
	if ( ch == '_' ) return true;
	return false;
}

Bool GParseToken( const AnsiChar*& stream, String& token )
{
	String outputVal;

	const AnsiChar* original = stream;
	if ( GParseWhitespaces( stream ) )
	{
		// Starts with '\"'
		if ( *stream == '\"' )
		{
			// Skip
			stream++;

			// Extract chars
			while ( *stream && *stream != '\"' )
			{
				AnsiChar chars[2] = { *stream, 0 };
				outputVal += chars;
				stream++;				
			}

			// Skip ending \"
			if ( *stream == '\"' )
			{
				stream++;
				token = outputVal;
				return true;
			}

			// Not parsed
			stream = original;
			return false;
		}

		// Grab normal text - alpha numerical only
		while ( IsAlphaNum( *stream ) )
		{
			AnsiChar chars[2] = { *stream, 0 };
			outputVal += chars;
			stream++;				
		}

		// No token was parsed
		if ( outputVal.Empty() )
		{
			stream = original;
			return false;
		}

		// Parsed
		token = outputVal;
		return true;
	}

	// Not grabbed
	return false;
}

static Bool IsIntNum( Uint32 index, Char ch )
{
	if ( ch >= '0' && ch <= '9' ) return true;
	if ( (ch == '+' || ch == '-') && (index == 0) ) return true;
	return false;
}

static Bool IsHex( Char ch )
{
	if ( ch >= '0' && ch <= '9' ) return true;
	if ( ch >= 'A' && ch <= 'F' ) return true;
	if ( ch >= 'a' && ch <= 'f' ) return true;
	return false;
}

Bool GParseInteger( const AnsiChar*& stream, Int64& value )
{
	const AnsiChar* original = stream;
	if ( original && GParseWhitespaces( stream ) )
	{
		if( stream[0] == '0' && stream[1] == 'x' )
		{
			stream += 2;
			Uint64 hexValue = value;
			Bool ret = GParseHex( stream, hexValue );
			value = hexValue;
			if( !ret ) stream = original;
			return ret;
		}

		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsIntNum( numChars, *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;
			return red::StringToInt( value, intTxt, nullptr, red::BaseTen );
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseInteger( const AnsiChar*& stream, Int32& value )
{
	const AnsiChar* original = stream;
	if ( original && GParseWhitespaces( stream ) )
	{
		if( stream[0] == '0' && stream[1] == 'x' )
		{
			stream += 2;
			Uint32 hexValue = value;
			Bool ret = GParseHex( stream, hexValue );
			value = hexValue;
			if( !ret ) stream = original;
			return ret;
		}

		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsIntNum( numChars, *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;

			return red::StringToInt( value, intTxt, nullptr, red::BaseTen );
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseInteger( const AnsiChar*& stream, Uint32& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		if( stream[0] == '0' && stream[1] == 'x' )
		{
			stream += 2;
			Bool ret = GParseHex( stream, value );
			if( !ret ) stream = original;
			return ret;
		}

		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsIntNum( numChars, *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;
			return red::StringToInt( value, intTxt, nullptr, red::BaseTen );
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseInteger( const AnsiChar*& stream, Uint64& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		if( stream[0] == '0' && stream[1] == 'x' )
		{
			stream += 2;
			Bool ret = GParseHex( stream, value );
			if( !ret ) stream = original;
			return ret;
		}

		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsIntNum( numChars, *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;
#ifdef RED_PLATFORM_WINPC
			value = _strtoui64( intTxt, NULL, red::BaseTen );
			if ( value == _UI64_MAX )
			{
				if ( errno == ERANGE )
					return false;
			}
#else
			value = strtoull( intTxt, NULL, red::BaseTen );
			if ( value == ULLONG_MAX )
			{
				if ( errno == ERANGE )
					return false;
			}
#endif
			return true;
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseHex( const AnsiChar*& stream, Uint64& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsHex( *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;
			return red::StringToInt( value, intTxt, nullptr, red::BaseSixteen );
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseHex( const AnsiChar*& stream, Uint32& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		const Uint32 maxTxtLen = 64;
		AnsiChar intTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsHex( *stream ) )
		{
			intTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			intTxt[ numChars ] = 0;

			Uint64 temp = strtoul( intTxt, NULL, 16 );
			if ( temp == ULONG_MAX )
			{
				if ( errno == ERANGE )
					return false;
			}
			if ( temp > std::numeric_limits<Uint32>::max() )       // On PS4 long is 64bit...
				return false;

			value = (Uint32) temp;
			return true;
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseBool( const AnsiChar*& stream, Bool& value )
{
	if ( stream && GParseWhitespaces( stream ) )
	{
		// True
		if ( red::StrcmpNC( stream, "true", 4 ) == 0 )
		{
			value = true;
			stream += 4;
			return true;
		}

		// False
		if ( red::StrcmpNC( stream, "false", 5 ) == 0 )
		{
			value = false;
			stream += 5;
			return true;
		}

		// Parse as number
		Int32 numericValue = 0;
		if ( GParseInteger( stream, numericValue ) )
		{
			value = (numericValue != 0);
			return true;
		}
	}

	// Not parsed
	return false;
}


static Bool IsFloatNum( Uint32 index, Char ch )
{
	if ( ch == '.' ) return true;
	if ( ch >= '0' && ch <= '9' ) return true;
	if ( ch == 'f' && (index>0) ) return true;
	if ( ch == 'e' && (index>0) ) return true;
	if ( ch == 'E' && (index>0) ) return true;
	if ( ch == '+' ) return true;
	if ( ch == '-' ) return true;
	return false;
}

Bool GParseFloat( const AnsiChar*& stream, Float& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		const Uint32 maxTxtLen = 64;
		AnsiChar floatTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsFloatNum( numChars, *stream ) )
		{
			floatTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			floatTxt[ numChars ] = 0;
			value = (Float)red::StringToDouble( floatTxt );
			return true;
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseDouble( const AnsiChar*& stream, Double& value )
{
	const AnsiChar* original = stream;
	if ( stream && GParseWhitespaces( stream ) )
	{
		const Uint32 maxTxtLen = 128;
		AnsiChar floatTxt[ maxTxtLen + 1 ];
		Uint32 numChars = 0;

		// Grab float number text
		while ( IsFloatNum( numChars, *stream ) )
		{
			floatTxt[ numChars++ ] = *stream++;

			// Overflow
			if ( numChars == maxTxtLen )
			{
				stream = original;
				return false;
			}
		}

		// Convert
		if ( numChars )
		{
			floatTxt[ numChars ] = 0;
			value = red::StringToDouble( floatTxt );
			return true;
		}
	}

	// Not parsed, restore
	stream = original;
	return false;
}

Bool GParseInt8( const AnsiChar*& stream, Int8& value )
{
	const auto* start = stream;

	Int64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal < std::numeric_limits<Int8>::min() || bigVal > std::numeric_limits<Int8>::max() )
	{
		stream = start;
		return false;
	}

	value = (Int8)bigVal;
	return true;
}

Bool GParseInt16( const AnsiChar*& stream, Int16& value )
{
	const auto* start = stream;

	Int64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal < std::numeric_limits<Int16>::min() || bigVal > std::numeric_limits<Int16>::max() )
	{
		stream = start;
		return false;
	}

	value = (Int16)bigVal;
	return true;

}

Bool GParseInt32( const AnsiChar*& stream, Int32& value )
{
	const auto* start = stream;

	Int64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal < std::numeric_limits<Int32>::min() || bigVal > std::numeric_limits<Int32>::max() )
	{
		stream = start;
		return false;
	}

	value = (Int32)bigVal;
	return true;
}

Bool GParseInt64( const AnsiChar*& stream, Int64& value )
{
	return GParseInteger( stream, value );
}

Bool GParseUint8( const AnsiChar*& stream, Uint8& value )
{
	const auto* start = stream;

	Uint64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal > std::numeric_limits<Uint8>::max() )
	{
		stream = start;
		return false;
	}

	value = (Uint8)bigVal;
	return true;
}

Bool GParseUint16( const AnsiChar*& stream, Uint16& value )
{
	const auto* start = stream;

	Uint64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal > std::numeric_limits<Uint16>::max() )
	{
		stream = start;
		return false;
	}

	value = (Uint16)bigVal;
	return true;
}

Bool GParseUint32( const AnsiChar*& stream, Uint32& value )
{
	const auto* start = stream;

	Uint64 bigVal = 0;
	if ( !GParseInteger( stream, bigVal ) )
		return false;

	if ( bigVal > std::numeric_limits<Uint32>::max() )
	{
		stream = start;
		return false;
	}

	value = (Uint32)bigVal;
	return true;
}

Bool GParseUint64( const AnsiChar*& stream, Uint64& value )
{
	return GParseInteger( stream, value );
}

Bool GParseEscapedString(const AnsiChar*& stream, String& value)
{
	String outputVal;

	GParseWhitespaces(stream);

	if (*stream == '\"')
	{
		stream++;

		while (*stream && *stream != '\"')
		{
			if (*stream == '\\')
			{
				stream++;

				const auto code = *stream++;
				if (code == 'n')
				{
					outputVal += "\n";
				}
				else if (code == 't')
				{
					outputVal += "\t";
				}
				else if (code == '\\')
				{
					outputVal += "\\";
				}
				else if (code == '\'')
				{
					outputVal += "\'";
				}
				else if (code == '\"')
				{
					outputVal += "\"";
				}
			}
			else
			{
				outputVal += *stream;
				stream++;
			}
		}

		if (*stream == '\"')
			stream++;
	}
	else
	{
		Uint32 stackCount = 0;
		Bool insideString = false;

		while (*stream && ( insideString || *stream > ' ') )
		{
			if (*stream == '\"' && outputVal.Back() != '\\')
			{
				insideString = !insideString;
			}

			if (*stream == '{' || *stream == '[' || *stream == '(')
				stackCount += 1;

			if (*stream == '}' || *stream == ']' || *stream == ')')
			{
				if (stackCount == 0)
					break;
				stackCount -= 1;
			}

			if (*stream == ',' && stackCount == 0)
				break;

			outputVal += *stream;
			stream++;
		}
	}

	// Return grabbed string
	value = outputVal;
	return true;
}
