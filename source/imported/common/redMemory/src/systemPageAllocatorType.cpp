/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemPageAllocatorType.h"
#include "vault.h"

namespace red
{
namespace memory
{
	SystemPageAllocator & AcquireSystemPageAllocator()
	{
		return AcquireVault().GetSystemPageAllocator();
	}
}
}