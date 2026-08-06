/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "base64.h"

namespace red
{
	Bool Base64Encode( const void* __restrict input, Uint32 inputLength, char* __restrict output, Uint32 outputSize )
	{
		constexpr char encodingTable[ 65 ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		const Uint32 maxOutputSize = Base64EncodedSize( inputLength ) + 1;
		if ( RED_UNLIKELY(outputSize < maxOutputSize) )
		{
			return false;
		}

		const Uint8* ptr = static_cast< const Uint8* >( input );
		Uint32 sizeLeft = inputLength;
		while (sizeLeft >= 3)
		{
			*output++ = encodingTable[ ( ptr[ 0 ] & 0xFC ) >> 2 ];
			*output++ = encodingTable[ ( ptr[ 0 ] & 0x03 ) << 4 | ( ptr[ 1 ] & 0xF0 ) >> 4 ];
			*output++ = encodingTable[ ( ptr[ 1 ] & 0x0F ) << 2 | ( ptr[ 2 ] & 0xC0 ) >> 6 ];
			*output++ = encodingTable[ ptr[ 2 ] & 0x3F ];
			ptr += 3;
			sizeLeft -= 3;
		}

		// Can only be (inputSize % 3) bytes left
		if (sizeLeft > 0)
		{
			if (sizeLeft == 2)
			{
				*output++ = encodingTable[ ( ptr[ 0 ] & 0xFC ) >> 2 ];
				*output++ = encodingTable[ ( ptr[ 0 ] & 0x03 ) << 4 | ( ptr[ 1 ] & 0xF0 ) >> 4 ];
				*output++ = encodingTable[ ( ptr[ 1 ] & 0x0F ) << 2 ];
			}
			else
			{
				*output++ = encodingTable[ ( ptr[ 0 ] & 0xFC ) >> 2 ];
				*output++ = encodingTable[ ( ptr[ 0 ] & 0x03 ) << 4 ];
				*output++ = '=';
			}
			*output++ = '=';
		}

		*output++ = '\0';
		return true;
	}

	Bool Base64Decode( const char* __restrict input, Uint32 inputLength, void* __restrict output, Uint32 outputSize )
	{
		// ABCDE FGHIJ KLMNO PQRST UVWXY Zabcd efghi jklmn opqrs tuvwx yz012 34567 89+/

		// Omit first 32 ASCII elements from table
		constexpr Uint8 decodingTable[ 96 ] = {
			 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63, // +/
			52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0, // 0123456789 =
			 0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // ABCDEFGHIJKLMNO
			15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0, // PQRSTUVWXYZ
			 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // abcdefghijklmno
			41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0, // pqrstuvwxyz
		};

		const Uint32 maxOutputSize = Base64DecodedSize( inputLength );
		if ( RED_UNLIKELY(outputSize < maxOutputSize) )
		{
			return false;
		}

		Uint8* ptr = reinterpret_cast< Uint8* >( output );
		Uint32 sizeLeft = inputLength;
		while ( sizeLeft >= 4 )
		{
			Uint8 a = decodingTable[ ( input[0] & 0x7F ) - 32 ];
			Uint8 b = decodingTable[ ( input[1] & 0x7F ) - 32 ];
			Uint8 c = decodingTable[ ( input[2] & 0x7F ) - 32 ];
			Uint8 d = decodingTable[ ( input[3] & 0x7F ) - 32 ];

			*ptr++ = a << 2 | b >> 4;
			*ptr++ = b << 4 | c >> 2;
			*ptr++ = c << 6 | d;

			input += 4;
			sizeLeft -= 4;
		}

		return true;
	}
}
