/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/hooks.h"
#include "vault.h"

namespace red
{
namespace memory
{
	HookHandle CreateHook( const HookCreationParameter & param )
	{
		return AcquireVault().CreateHook( param );
	}

	void RemoveHook( HookHandle handle )
	{
		AcquireVault().RemoveHook( handle );
	}
}
}