/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OUT_OF_MEMORY_HANDLER_IGNORE_H_
#define _RED_MEMORY_OUT_OF_MEMORY_HANDLER_IGNORE_H_

#include "oomHandler.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API PoolOOMHandlerIgnore final : public PoolOOMHandler
	{
	public:

		PoolOOMHandlerIgnore();
		~PoolOOMHandlerIgnore();

	private:

		virtual void OnHandlePoolAllocateFailure( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) override;
	};

}
}

#endif
