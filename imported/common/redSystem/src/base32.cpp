/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "base32.h"

namespace red
{
	static inline Uint32 fastCeilDiv(Uint32 n, Uint32 d)
	{
		return (n + (d - 1)) / d;
	}

	Bool Base32Encode(const void* __restrict input, Uint32 inputLength, char* __restrict output, Uint32 outputSize, char paddingChar)
	{
		const char encodingTable[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

		const Uint32 maxOutputSize = fastCeilDiv(inputLength, 5) * 8 + 1;
		if (RED_UNLIKELY(outputSize < maxOutputSize))
		{
			return false;
		}

		const Uint8* ptr = static_cast<const Uint8*>(input);
		Uint32 sizeLeft = inputLength;
		while (sizeLeft >= 5)
		{
			Uint64 packedBits = 0;
			Uint32 bitPosition = 5 * 8 - 8;
			for (Uint32 i = 0; i < 5; ++i, bitPosition -= 8)
				packedBits += static_cast<Uint64>(ptr[i]) << bitPosition;

			for (Uint32 i = 0; i < 8; ++i, packedBits <<= 5)
				*output++ = encodingTable[static_cast<Uint32>((packedBits >> (40 - 5)) & 0x1Fu)];

			ptr += 5;
			sizeLeft -= 5;
		}

		// Can only be (inputSize % 5) bytes left, so at most 4 bytes.
		if (sizeLeft > 0)
		{
			// Pack leftover bytes into one 32-bit word to simplify encoding.
			Uint32 leftover = 0;
			Uint32 bitPosition = 32 - 8;
			for (Uint32 i = 0; i < sizeLeft; ++i, bitPosition -= 8)
				leftover += static_cast<Uint32>(ptr[i]) << bitPosition;

			Uint32 leftoverSize = fastCeilDiv(sizeLeft * 8, 5u);
			for (Uint32 i = 0; i < leftoverSize; ++i, leftover <<= 5)
				*output++ = encodingTable[leftover >> (32 - 5)];

			Uint32 paddingSize = 8u - leftoverSize;
			if (paddingChar != '\0')
				for (Uint32 i = 0; i < paddingSize; ++i)
					*output++ = paddingChar;
		}

		*output++ = '\0';
		return true;
	}
} // red
