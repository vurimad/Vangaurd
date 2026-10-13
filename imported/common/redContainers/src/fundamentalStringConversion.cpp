/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "containersTypesFormatters.h"
#include "fundamentalStringConversion.h"
#include "fundamentalStringParser.h"
#include "../../redSystem/include/guid.h"
#include "../../redSystem/include/systemTypesFormatters.h"


//////////////////////////////////////////////////////////////////////////
// string
const Bool ToString( red::String& outTxt, const red::String& val, const char* customFormat )
{
	if ( !customFormat )
	{
		outTxt = val;
		return true;
	}

	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& txt, red::String& outVal )
{
	outVal = txt;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// utf16 string
const Bool ToString( red::String& outTxt, const red::Utf16String& val, const char* customFormat )
{
	if ( !customFormat )
	{
		outTxt.Set( UNICODE_TO_ANSI( val.AsChar() ) );
		return true;
	}

	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& txt, red::Utf16String& outVal )
{
	outVal = red::Utf16String( ANSI_TO_UNICODE( txt.AsChar() ) );
	return true;
}
 
//////////////////////////////////////////////////////////////////////////
// Bool
const Bool ToString( red::String& outTxt, const Bool val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Bool& outVal )
{
	if ( text.EqualsNC("true") )
	{
		outVal = true;
		return true;
	}
	else if ( text.EqualsNC("false") )
	{
		outVal = false;
		return true;
	}
	else
	{
		// try the numerical 0/1 value
		const auto* str = text.AsChar();
		Int64 val;
		if ( GParseInteger( str, val ) )
		{
			outVal = (val != 0);
			return true;
		}
	}

	// not parsed
	return false;
}


//////////////////////////////////////////////////////////////////////////
// Int8
const Bool ToString( red::String& outTxt, const Int8 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& txt, Int8& outVal )
{
	Int64 val = 0;
	const auto* str = txt.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val < std::numeric_limits<Int8>::min() || val > std::numeric_limits<Int8>::max() )
		return false;

	outVal = (Int8) val;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Int16
const Bool ToString( red::String& outTxt, const Int16 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Int16& outVal )
{
	Int64 val = 0;
	const auto* str = text.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val < std::numeric_limits<Int16>::min() || val > std::numeric_limits<Int16>::max() )
		return false;

	outVal = (Int16) val;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Int32
const Bool ToString( red::String& outTxt, const Int32 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Int32& outVal )
{
	Int64 val = 0;
	const auto* str = text.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val < std::numeric_limits<Int32>::min() || val > std::numeric_limits<Int32>::max() )
		return false;

	outVal = (Int32) val;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Int64
const Bool ToString( red::String& outTxt, const Int64 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Int64& outVal )
{
	const auto* str = text.AsChar();
	return GParseInteger( str, outVal );
}

//////////////////////////////////////////////////////////////////////////
// Uint8
const Bool ToString( red::String& outTxt, const Uint8 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Uint8& outVal )
{
	Uint64 val = 0;
	const auto* str = text.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val > std::numeric_limits<Uint8>::max() )
		return false;

	outVal = (Uint8) val;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Uint16
const Bool ToString( red::String& outTxt, const Uint16 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Uint16& outVal )
{
	Uint64 val = 0;
	const auto* str = text.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val > std::numeric_limits<Uint16>::max() )
		return false;

	outVal = (Uint16) val;
	return true;

}

//////////////////////////////////////////////////////////////////////////
// Uint32
const Bool ToString( red::String& outTxt, const Uint32 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Uint32& outVal )
{
	Uint64 val = 0;
	const auto* str = text.AsChar();
	if ( !GParseInteger( str, val ) )
		return false;

	if ( val > std::numeric_limits<Uint32>::max() )
		return false;

	outVal = (Uint32) val;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Uint64
const Bool ToString( red::String& outTxt, const Uint64 val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Uint64& outVal )
{
	const auto* str = text.AsChar();
	return GParseInteger( str, outVal );
}

//////////////////////////////////////////////////////////////////////////
// Float
const Bool ToString( red::String& outTxt, const Float val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

// Float : Max decimal precision
const Bool ToStringMaxPrecision( red::String& outTxt, const Float val )
{
	char formattedBuffer[ 512 ];
	Int32 written = red::SNPrintFSafe( formattedBuffer, sizeof( formattedBuffer ), "%.64f", val );
	if ( !written )
	{
		return false;
	}
	while ( formattedBuffer[ written - 1 ] == '0' )
	{
		--written;
	}
	formattedBuffer[ written ] = 0;
	outTxt.Set( formattedBuffer, written );
	return true;
}

// Float : Mantisa / Exponent / Sign
const Bool ToStringMantisaExponentSign( red::String& outTxt, const Float val )
{
	typedef union {
		float f;
		struct {
			unsigned int mantisa : 23;
			unsigned int exponent : 8;
			unsigned int sign : 1;
		} parts;
	} float_cast;
	const float_cast fc = { val };

	char formattedBuffer[ 512 ];
	const Int32 written = red::SNPrintFSafe( formattedBuffer, sizeof( formattedBuffer ), "<%u:%u:%u>", fc.parts.sign, fc.parts.mantisa, fc.parts.exponent );
	if ( !written )
	{
		return false;
	}

	outTxt.Set( formattedBuffer, written );
	return true;
}

const Bool FromString( const red::String& text, Float& outVal )
{
	const auto* str = text.AsChar();
	return GParseFloat( str, outVal );
}

//////////////////////////////////////////////////////////////////////////
// Double
const Bool ToString( red::String& outTxt, const Double val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, Double& outVal )
{
	const auto* str = text.AsChar();
	return GParseDouble( str, outVal );
}

//////////////////////////////////////////////////////////////////////////
// GUID
const Bool ToString( red::String& outTxt, const red::GUID& val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& text, red::GUID& outVal )
{
	Uint32 partA=0, partB=0, partC=0, partD=0;;
	const auto* str = text.AsChar();
	if ( GParseHex( str, partA ) )
	{
		if ( GParseKeyword(str, "-") && GParseHex( str, partB ) )
		{
			if ( GParseKeyword(str, "-") && GParseHex( str, partC ) )
			{
				if ( GParseKeyword(str, "-") && GParseHex( str, partD ) )
				{
					outVal = red::GUID(partA, partB, partC, partD);
					return true;
				}
			}
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// RUID
const Bool ToString( red::String& outTxt, const red::RUID& val, const char* customFormat /*= nullptr */ )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if (red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ))
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& txt, red::RUID& outVal )
{
	return outVal.FromString( txt.AsChar() );
}

const Bool ToString( red::String& outTxt, const red::RUIDRef& val, const char* customFormat /*= nullptr */ )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if (red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ))
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const red::String& txt, red::RUIDRef& outVal )
{
	return outVal.FromString( txt.AsChar() );
}

//////////////////////////////////////////////////////////////////////////
// DynArray<Uint8>
const Bool ToString( String& outTxt, const red::DynArray<Uint8>& val, const char* customFormat )
{
	outTxt.Resize( val.Size() );
	red::Memcpy( outTxt.Data(), val.Data(), val.Size() /* * sizeof( Uint8 ) */ );
	return true;
}

const Bool FromString( const String& txt, red::DynArray<Uint8>& outVal )
{
	outVal.Resize( txt.Length() );
	red::Memcpy( outVal.Data(), txt.Data(), txt.Length() /* * sizeof( AnsiChar ) */ );
	return true;
}