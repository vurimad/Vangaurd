/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	/// Encode sequence of bytes into base32 string.
	/// Using RFC-4648 standard, uses only characters 'A'-'Z' + '2'-'7', by default uses '=' character for padding.
	/// You can supply some other padding character, or set it to '\0'' to strip padding entirely.
	/// Output size should be at least 1 + ceil( inputSize / 5 ) * 8
	Bool REDSYSTEM_API Base32Encode(const void* __restrict input, Uint32 inputLength, char* __restrict output, Uint32 outputSize, char paddingChar = '=');
	/// Calculate the size required for padded encoded base32 string.
	/// It does not count zero-terminator character, so if you want zero-terminated string add +1 to this result.
	constexpr Uint32 REDSYSTEM_API Base32EncodedSize(Uint32 inputLength) { return ((inputLength + 4u) / 5u) * 8u; }
}

