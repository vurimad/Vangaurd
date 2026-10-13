/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "hookPoolValidator.h"
#include "vault.h"
#include "../include/hookTypes.h"

namespace red
{
namespace memory
{
namespace
{
	static HookHandle s_memoryPoolValidatorHandle = 0;
}
	void PreAllocatePoolValidatorCallback( HookPreParameter & param, void* )
	{
		u32 & size = *param.size;
		Block & block = *param.block;

		if( size )
		{
			// allocate or reallocate with size > 0
			size += 8;
		}

		if( block.address )
		{
			// Free function called, or Reallocate with size 0.
			block.size -= 8;
			const u64 handle = *reinterpret_cast< u64* >( block.address + block.size );
			RED_MEMORY_ASSERT( handle == param.proxy.poolHandle, "Block was allocated via %hs but freed with %hs.", 
				GetPoolName( static_cast< PoolHandle >( handle) ), 
				GetPoolName( param.proxy.poolHandle ) );
			RED_UNUSED( handle );
		}
	}
	
	void PostAllocatePoolValidatorCallback( HookPostParameter & param, void* )
	{
		Block & output = *param.outputBlock;

		if( output.address )
		{
			// Simple allocate function, or reallocate with
			output.size -= 8;
			*reinterpret_cast< u64* >( output.address + output.size ) = param.proxy.poolHandle;
		}
	}

	void EnablePoolValidator()
	{
		const HookCreationParameter param = 
		{
			PreAllocatePoolValidatorCallback,
			PostAllocatePoolValidatorCallback,
			nullptr,
			HookType::HookType_Pool_Validation
		};

		s_memoryPoolValidatorHandle = AcquireVault().CreateHook( param );
	}
	
	
	void DisablePoolValidator()
	{
		AcquireVault().RemoveHook( s_memoryPoolValidatorHandle );
	}
}
}
