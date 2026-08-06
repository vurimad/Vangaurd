/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "frameAllocatorUtils.h"
#include "../include/systemAllocator.h"

namespace red
{
namespace memory
{

	void ValidateInitializationParameter( const FrameAllocatorParameter & parameter )
	{
		RED_UNUSED( parameter );
		RED_MEMORY_ASSERT( parameter.systemAllocator, "Allocator need access to SystemAllocator." );
		RED_MEMORY_ASSERT( parameter.frameBlockSize, "Frame block size cannot be 0" );
		RED_MEMORY_ASSERT( RoundUp( parameter.frameBlockSize, static_cast< u32 >( parameter.systemAllocator->GetPageSize() ) ) == parameter.frameBlockSize, "Frame block size must be round up to page size" );
		RED_MEMORY_ASSERT( parameter.numberOfFrames, "Number of frames cannot be 0");
		RED_MEMORY_ASSERT( parameter.flags, "No flags provided" );
	}

}
}