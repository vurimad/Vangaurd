/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "hookUtils.h"
#include "vault.h"
#include "hookHandler.h"
#include "hookMarkBlock.h"
#include "hookSettings.h"
#include "hookOverrunDetector.h"
#include "hookPoolValidator.h"

RED_DISABLE_WARNING_MSC( 4127 ) //  warning C4127: conditional expression is constant

namespace red
{
namespace memory
{
namespace internal
{
	void ProcessPreHooks( HookPreParameter & param, u32 disabledHooks )
	{
		AcquireVault().ProcessPreHooks( param, disabledHooks );
	}

	void ProcessPostHooks( HookPostParameter & param, u32 disabledHooks )
	{
		AcquireVault().ProcessPostHooks( param, disabledHooks );
	}
}

	void InitializePermanentHooks( HookHandler & handler )
	{
		RED_UNUSED( handler );

#ifdef RED_MEMORY_ENABLE_HOOKS

		static_assert( !( c_enableOverrunDetection && c_enablePoolValidation ), "Pool Validator and Overrun Detector cannot run at same time." );

		if( c_enableAllocateMemoryMarking || c_enableFreeMemoryMarking )
		{
			HookCreationParameter param = 
			{
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
				c_enableFreeMemoryMarking ? MarkFreeBlock : nullptr,
#else
				nullptr, // Debug Allocator can't know correct block size when freeing.
#endif
				c_enableAllocateMemoryMarking ? MarkAllocatedBlock : nullptr,
				nullptr,
				HookType::HookType_Memory_Marking
			};

			handler.Create( param );
		}

		if( c_enableOverrunDetection )
		{
			const HookCreationParameter param = 
			{
				PreAllocateOverrunDetectorCallback,
				PostAllocateOverrunDetectorCallback,
				nullptr,
				HookType::HookType_Overrun_Detection
			};

			handler.Create( param );
		}

		if( c_enablePoolValidation )
		{
			const HookCreationParameter param = 
			{
				PreAllocatePoolValidatorCallback,
				PostAllocatePoolValidatorCallback,
				nullptr,
				HookType::HookType_Pool_Validation
			};

			handler.Create( param ); 
		}
#endif
	}
}
}
