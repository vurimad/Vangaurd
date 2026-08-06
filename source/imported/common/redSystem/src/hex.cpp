/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "hex.h"

namespace red
{

Bool HexEncode( const void * __restrict input, Uint32 inputLength, char * __restrict output, Uint32 outputSize )
{
	const Uint32 maxOutputSize = HexEncodedSize( inputLength ) + 1;
	if ( RED_UNLIKELY( outputSize < maxOutputSize ) )
	{
		return false;
	}

	static constexpr char encodingTable[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

	const Uint8* inputPtr = static_cast< const Uint8* >( input );
	for ( Uint32 i = 0; i < inputLength; ++i )
	{
		const Uint8 leftNibble = (inputPtr[i] & 0xF0) >> 4;
		const Uint8 rightNibble = (inputPtr[i] & 0x0F);
		*output++ = encodingTable[ leftNibble ];
		*output++ = encodingTable[ rightNibble ];
	}
	*output++ = '\0';

	return true;
}

// Assumes that the incoming character is in the valid range
static constexpr Uint8 HexDecodeChar( const char c )
{
	const bool isNum = ( c & 0b01000000 ) == 0;
	return ( c & 0b00001111 ) + ( isNum ? 0 : 0b00001001 );
}

Bool HexDecode( const char * __restrict input, Uint32 inputLength, void * __restrict output, Uint32 outputSize )
{
	if ( RED_UNLIKELY( outputSize < HexDecodedSize( inputLength ) ) || RED_UNLIKELY( ( inputLength & 1 ) != 0 ) )
	{
		return false;
	}

	Uint8* outputPtr = static_cast< Uint8* >( output );
	for ( Uint32 i = 0; i < inputLength; i += 2 )
	{
		const Uint8 leftNibble = HexDecodeChar( *input++ );
		const Uint8 rightNibble = HexDecodeChar( *input++ );
		*outputPtr++ = ( leftNibble << 4 ) | rightNibble;
	}

	return true;
}

} // red
