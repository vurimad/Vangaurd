/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// implementation taken from https://codereview.stackexchange.com/questions/163872/md5-implementation-in-c11

namespace red
{
	template < typename TElement > class DynArray;
	class String;
}

#include "../../redContainers/include/staticArray.h"

namespace crypto
{
	struct MD5;

	namespace md5
	{
		struct MD5_Context
		{
			MD5& m_hash;

			red::StaticArray<Uint32, 16> m_array;
			red::StaticArray<Uint32, 16>::iterator m_arrayFirst;
		};

		Uint32 LeftRotate( Uint32 x, Uint32 c );
		void ResetArray( MD5_Context& ctx );
		void BytesToArray( MD5_Context& ctx, const Uint8*& first, red::StaticArray<Uint32, 16>::iterator arrayLast );
		void TrueBitToArray( MD5_Context& ctx, const Uint8*& first, std::ptrdiff_t chunkLength );
		void ZerosToArray( MD5_Context& ctx, red::StaticArray<Uint32, 16>::iterator arrayLast );
		void OriginalLengthBitsToArray( MD5_Context& ctx, Uint64 originalLengthBits );
		void HashChunk( MD5_Context& ctx );
		void Update( MD5_Context& ctx, const void* buffer, const red::Uint64 size );
	};
}