/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// Implements the canonical encoding from section 4 of from RFC 4648.
// Output size should be at least 1 + ceil( inputSize / 3 ) * 4
namespace red
{
	// Calculate the encoded size of the data, not inluding the null terminator
	constexpr Uint32 REDSYSTEM_API Base64EncodedSize( Uint32 inputLength ) { return ( ( inputLength + 2 ) / 3 ) * 4; }

	// Encodes the data given by the input pointer and length and stores the result in the buffer represented by
	// the output pointer and size. Terminates the output buffer with a null terminator so that it is a valid string.
	// Returns false if not enough bytes were provided in the output buffer to store the encoded string and null terminating character
	Bool REDSYSTEM_API Base64Encode( const void* __restrict input, Uint32 inputLength, char* __restrict output, Uint32 outputSize );

	// Calculate the decoded size of the data, giving the longest possible decoded string length given the input length
	// Not including the character required for the null terminator
	constexpr Uint32 REDSYSTEM_API Base64DecodedSize( Uint32 inputLength ) { return ( ( inputLength / 4 ) * 3 ); }

	// Decodes the string given by the input pointer and length and stores the result in the buffer reprensented
	// by the output pointer and size. The input is assumed to be a Base64 encoded string which does not contain
	// any characters outside "A-Za-z0-9+/=".
	// Returns false if not enough bytes were provided in the output buffer to store the decoded value.
	Bool REDSYSTEM_API Base64Decode( const char* __restrict input, Uint32 inputLength, void* __restrict output, Uint32 outputSize );
}

