/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_HOOK_TYPES_H_
#define _RED_MEMORY_HOOK_TYPES_H_

#include "block.h"
#include "hookType.h"
#include "poolTypes.h"
#include "proxyTypeId.h"

namespace red
{
namespace memory
{
	struct Block;
	struct HookPreParameter;
	struct HookPostParameter;

	typedef u64 HookHandle;

	typedef void (*HookPreCallback)( HookPreParameter &, void* );
	typedef void (*HookPostCallback)( HookPostParameter &, void* );

	struct HookCreationParameter
	{
		HookPreCallback preCallback;
		HookPostCallback postCallback;
		void * userData;
		HookType type;
	};

	struct HookProxyParameter
	{
		PoolHandle poolHandle;
		ProxyTypeId id;
		u64 address;
	};

	struct HookPreParameter
	{
		Block * block;
		u32 * size;
		HookProxyParameter proxy;
	};

	struct HookPostParameter
	{
		Block * inputBlock;
		Block * outputBlock;
		HookProxyParameter proxy;
	};

	RED_MEMORY_API HookHandle CreateHook( const HookCreationParameter & param );
	RED_MEMORY_API void RemoveHook( HookHandle handle );

	// FOR UNIT TEST
	RED_MEMORY_API bool operator==( const HookProxyParameter & left, const HookProxyParameter & right );
	RED_MEMORY_API bool operator==( const HookPreParameter & left, const HookPreParameter & right );
	RED_MEMORY_API bool operator==( const HookPostParameter & left, const HookPostParameter & right );
	
}
}

#endif
