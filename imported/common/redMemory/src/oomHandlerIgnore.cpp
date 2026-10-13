/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "oomHandlerIgnore.h"

namespace red
{
namespace memory
{
	PoolOOMHandlerIgnore::PoolOOMHandlerIgnore()
	{}

	PoolOOMHandlerIgnore::~PoolOOMHandlerIgnore()
	{}

	void PoolOOMHandlerIgnore::OnHandlePoolAllocateFailure( const char *, const char *, u32 , u32  )
	{
		// Nothing to do, move along. User is handling null ptr that will be returned.
	}
}
}
