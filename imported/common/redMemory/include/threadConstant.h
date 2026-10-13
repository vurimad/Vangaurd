/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_THREAD_CONSTANT_H_
#define _RED_MEMORY_THREAD_CONSTANT_H_

namespace red
{
namespace memory
{
#ifdef RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION

	const u32 c_maxThreadCount = 64;

#else

	const u32 c_maxThreadCount = 16;

#endif

	const size_t c_maxThreadNameLength = 32;
}
}

#endif

