/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "vault.h"
#include "../include/hookTypes.h"

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

	bool operator==( const HookProxyParameter & left, const HookProxyParameter & right )
	{
		return left.poolHandle == right.poolHandle
			&& left.id == right.id
			&& left.address == right.address;
	}

	bool operator==( const HookPreParameter & left, const HookPreParameter & right )
	{
		return left.block == right.block
			&& left.size == right.size
			&& left.proxy == right.proxy;
	}

	bool operator==( const HookPostParameter & left, const HookPostParameter & right )
	{
		return left.inputBlock == right.inputBlock
			&& left.outputBlock == right.outputBlock
			&& left.proxy == right.proxy;
	}
}
}
