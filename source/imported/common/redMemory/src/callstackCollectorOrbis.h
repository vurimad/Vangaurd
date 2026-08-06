/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_CALLSTACK_COLLECTOR_ORBIS_H_
#define _RED_MEMORY_CALLSTACK_COLLECTOR_ORBIS_H_

namespace red
{
namespace memory
{
	class CallstackCollectorOrbis
	{
	public:
		void GetCallstack( u64* callstack, u32& depth, u32& size, u16& mask, u32& hash );
	};
}
}

#endif