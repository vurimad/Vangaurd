/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_CALLSTACK_COLLECTOR_CONSTANT_H_
#define _RED_MEMORY_CALLSTACK_COLLECTOR_CONSTANT_H_

namespace red
{
namespace memory
{
	const u32 c_callstackMaxDepth = 16;

#if defined( RED_CONFIGURATION_RELEASE ) || defined( RED_CONFIGURATION_FINAL )
	const u32 c_callstackFrameToSkip = 6;
#else
	const u32 c_callstackFrameToSkip = 8;
#endif


	const i64 c_maxI32 = std::numeric_limits< i32 >::max();
	const i64 c_minI32 = std::numeric_limits< i32 >::min();
}
}

#endif