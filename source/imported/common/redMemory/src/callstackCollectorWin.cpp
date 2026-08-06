/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "callstackCollectorConstant.h"
#include "callstackCollectorWin.h"
#include "../include/utils.h"

namespace red
{
namespace memory
{
	void CallstackCollectorWin::GetCallstack( u64* callstack, u32& depth, u32& size, u16& mask, u32& hash )
	{
		void * frames[c_callstackMaxDepth] = { 0 };
		DWORD callstackHash = 0;
		const WORD framesCaptured = RtlCaptureStackBackTrace(
			c_callstackFrameToSkip,
			c_callstackMaxDepth,
			frames,
			&callstackHash
		);

		if ( framesCaptured )
		{
			callstack[ 0 ] = AddressOf( frames[ 0 ] );
			mask = 1;
			size += sizeof( u64 );
		}

		for ( WORD index = 1; index != framesCaptured; ++index )
		{
			const auto address = AddressOf( frames[ index ] );

			callstack[ index ] = address;

			const i64 diff = address - callstack[ 0 ];
			if ( diff > c_maxI32 || diff < c_minI32 )
			{
				mask |= 1 << index;
				size += sizeof( u64 );
			}
			else
			{
				size += sizeof( i32 );
			}
		}

		hash = callstackHash;
		depth = framesCaptured;
	}
}
}