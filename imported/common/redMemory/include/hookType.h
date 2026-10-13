/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_HOOK_TYPE_H_
#define _RED_MEMORY_HOOK_TYPE_H_

namespace red
{
namespace memory
{

	enum HookType : u32
	{
		HookType_None = RED_FLAG( 0 ),
		HookType_Memory_Marking = RED_FLAG( 1 ),
		HookType_Overrun_Detection = RED_FLAG( 2 ),
		HookType_Pool_Validation = RED_FLAG( 3 ),
		HookType_Memory_Profiler = RED_FLAG( 4 ),
#ifdef RED_MEMORY_UNIT_TEST
		HookType_Unit_Test = RED_FLAG( 5 ),
		HookType_All = HookType_Memory_Marking | HookType_Overrun_Detection | HookType_Pool_Validation | HookType_Memory_Profiler | HookType_Unit_Test
#else
		HookType_All = HookType_Memory_Marking | HookType_Overrun_Detection | HookType_Pool_Validation | HookType_Memory_Profiler
#endif
	};

}
}

#endif