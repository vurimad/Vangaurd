/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */
#ifndef _RED_MEMORY_HOOK_POOL_VALIDATOR_H_
#define _RED_MEMORY_HOOK_POOL_VALIDATOR_H_

namespace red
{
namespace memory
{
	struct HookPreParameter;
	struct HookPostParameter;

	void PreAllocatePoolValidatorCallback( HookPreParameter & param, void* );
	void PostAllocatePoolValidatorCallback( HookPostParameter & param, void* );

	// DO NOT ACTIVATE AT RUNTIME. UNIT TEST ONLY
	RED_MEMORY_API void EnablePoolValidator(); 
	RED_MEMORY_API void DisablePoolValidator();
}
}

#endif
