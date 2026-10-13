/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "mathColor.h"
#include "stringParser.h"

namespace red
{
	Bool CheckFormatString(const Color& val, const char* formatToCheck)
	{
		// check custom
		const char* format = GetFormatString(val);
		if (formatToCheck)
		{
			// old c-style format
			const char* checkFormat = std::strchr(formatToCheck, '%');
			if (checkFormat)
			{
				return true;
			}
		}
		return false;
	}

	// custom Color ToBuffer()
	// supported:
	// rgba: [255,255,255,255] (default)
	// argb: [255,255,255,255]
	// 
	// todo:
	// hsv etc..
	Bool ToBuffer(char* buffer, const Uint32 bufferLen, const Color& val, Int32& written, const char* formatString)
	{
		if (buffer && bufferLen)
		{
			if (formatString && (red::CheckFormatString(val, formatString) || red::prv::CheckNewFormatString(val, formatString)))
			{
				// rgba
				if (std::strstr(formatString, "rgba"))
				{
					written += red::SNPrintFUnsafe(buffer, bufferLen, "[%u,%u,%u,%u]", val.R, val.G, val.B, val.A);
				}

				// argb
				else if (std::strstr(formatString, "argb"))
				{
					written += red::SNPrintFUnsafe(buffer, bufferLen, "[%u,%u,%u,%u]", val.A, val.R, val.G, val.B);
				}

				// etc..

				// default
				else
				{
					written += red::SNPrintFUnsafe(buffer, bufferLen, red::GetFormatString(val), val.R, val.G, val.B, val.A);
				}

				return true;
			}
		}
		return false;
	}

}

template<>
const Bool ToString<Color>( red::String& outTxt, const Color& val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[512];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

template<>
const Bool FromString<Color>( const String& txt, Color& outVal )
{
	const char* stream = txt.AsChar();
	return GParseColor( stream, outVal );
}