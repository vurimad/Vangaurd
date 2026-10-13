/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FRAME_ALLOCATOR_UTILS_H_
#define _RED_MEMORY_FRAME_ALLOCATOR_UTILS_H_

#include "allocatorMetrics.h"
#include "../include/block.h"
#include "../include/virtualRange.h"
#include "../include/systemBlock.h"

namespace red
{
namespace memory
{
	const u8 c_frameAllocatorUnitTestAllocFiller = 0xa5;
	const u8 c_frameAllocatorUnitTestFreeFiller = 0xf5;

	struct RED_ALIGN( 8 ) FrameAllocatorHeader
	{
		u32 size;
		u32 marker;
	};

	const u32 c_frameAllocatorHeaderSize = sizeof( FrameAllocatorHeader );
	const u32 c_frameAllocatorHeaderAlignment = __alignof( FrameAllocatorHeader );
	const u32 c_frameAllocatorHeaderAllocateMarker = 0xBEEFC0DE;
	const u32 c_frameAllocatorHeaderFreeMarker = 0xDEADC0DE;

	class SystemAllocator;
	class Serializer;

	struct FrameAllocatorParameter
	{
		SystemAllocator* systemAllocator;
		u32 frameBlockSize;
		u32 numberOfFrames;
		u32 flags;
	};

	struct FrameAllocatorMetrics
	{
		AllocatorMetrics metrics;
		u64 freeBlockSize;
	};

	void ValidateInitializationParameter( const FrameAllocatorParameter & parameter );
}
}

#endif