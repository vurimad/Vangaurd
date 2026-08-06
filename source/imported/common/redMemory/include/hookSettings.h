/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_HOOK_SETTINGS_H_
#define _RED_MEMORY_HOOK_SETTINGS_H_

namespace red
{
namespace memory
{
	// Enable or disable which Hook should be enabled at launch. 
	// Changing any value here will not trigger rebuild, just a relink per apps.
	// DO NOT SUBMIT THIS FILE. Keep you change local.

	const u32 c_markAllocateMemoryPattern = 0;
	const u32 c_markReallocateMemoryPattern = 0;
	const u32 c_markFreeMemoryPattern = 0xfe;

	const bool c_enableAllocateMemoryMarking = true;
	
#ifndef RED_CONFIGURATION_FINAL
	
	const bool c_enableFreeMemoryMarking = true;

#else 

	const bool c_enableFreeMemoryMarking = false;

#endif

	const bool c_enableOverrunDetection = false;
	
	const bool c_enablePoolValidation = false;
}
}

#endif
