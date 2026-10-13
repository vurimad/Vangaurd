/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SLAB_CONSTANT_H_
#define _RED_MEMORY_SLAB_CONSTANT_H_

namespace red
{
namespace memory
{
	const u32 c_slabMinAllocSize = 8;
	const u32 c_slabMaxAllocSize = 512;
	const u32 c_slabDefaultAlignment = 8;
#ifdef RED_PLATFORM_CONSOLE
	const u32 c_slabMaxCount = 16;
	const u32 c_slabSize = 32 * 1024;
#else
	const u32 c_slabMaxCount = 32;
	const u32 c_slabSize = 64 * 1024;
#endif
	

	const u8 c_slabUnitTestAllocFiller = 0xa1;
	const u8 c_slabUnitTestFreeFiller = 0xf1;
}
}

#endif
