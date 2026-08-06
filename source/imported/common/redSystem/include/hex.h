/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{
// Calculate the encoded size of the data, not inluding the null terminator
constexpr Uint32 REDSYSTEM_API HexEncodedSize( Uint32 inputLength ) { return inputLength * 2; }

// Encodes the data given by the input pointer and length and stores the result in the buffer represented by
// the output pointer and size. Terminates the output buffer with a null terminator so that it is a valid string.
// Returns false if not enough bytes were provided in the output buffer to store the encoded string and null terminating character
Bool REDSYSTEM_API HexEncode( const void* __restrict input, Uint32 inputLength, char* __restrict output, Uint32 outputSize );

// Calculate the decoded size of the data, giving the longest possible decoded string length given the input length
// Not including the character required for the null terminator
constexpr Uint32 REDSYSTEM_API HexDecodedSize( Uint32 inputLength ) { return inputLength / 2; }

// Decodes the string given by the input pointer and length and stores the result in the buffer reprensented
// by the output pointer and size. The input is assumed to be a hex encoded string which does not contain
// any characters outside "0-9A-Fa-f".
// Returns false if not enough bytes were provided in the output buffer to store the decoded value.
Bool REDSYSTEM_API HexDecode( const char* __restrict input, Uint32 inputLength, void* __restrict output, Uint32 outputSize );

} // red
