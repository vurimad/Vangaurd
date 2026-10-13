/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "callstackCollectorConstant.h"
#include "callstackCollectorLinux.h"
#include "../include/utils.h"

namespace red
{
	namespace memory
	{
		struct StackFrame
		{
			uintptr_t nextFrame;
			uintptr_t functionReturnAddress;
			uintptr_t unknown;
		};

		u32 CaptureStackBackTrace( u32 framesToSkip, u32 maxDepth, uintptr_t* frames, u32& hash )
		{
			StackFrame* stackFrame = reinterpret_cast< StackFrame* >( __builtin_frame_address( 0 ) );

			while ( stackFrame && framesToSkip )
			{
				stackFrame = reinterpret_cast< StackFrame* >( stackFrame->nextFrame );
				--framesToSkip;
			}

			u32 depth = 0;
			while ( stackFrame && depth < maxDepth )
			{
				frames[depth] = stackFrame->functionReturnAddress;

				const u64 address = frames[depth];
				if ( hash == 0 )
				{
					hash = red::CalculateHash32( &address, sizeof( address ) );
				}
				else
				{
					hash = red::CombineHashes32( hash, red::CalculateHash32( &address, sizeof( address ) ) );
				}

				++depth;
				stackFrame = reinterpret_cast< StackFrame* >( stackFrame->nextFrame );
			}

			return depth > 0 ? depth - 1 : 0;
		}


		void CallstackCollectorLinux::GetCallstack( u64* callstack, u32& depth, u32& size, u16& mask, u32& hash )
		{
			uintptr_t frames[c_callstackMaxDepth] = { 0 };
			depth = CaptureStackBackTrace( c_callstackFrameToSkip, c_callstackMaxDepth, frames, hash );

			if ( depth )
			{
				callstack[0] = frames[0];
				mask = 1;
				size += sizeof( u64 );
			}

			for ( u32 index = 1; index < depth; ++index )
			{
				const auto address = frames[index];

				RED_MEMORY_ASSERT( address != 0, "Invalid frame address" );

				callstack[index] = address;

				const i64 diff = address - callstack[0];
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
		}
	}
}