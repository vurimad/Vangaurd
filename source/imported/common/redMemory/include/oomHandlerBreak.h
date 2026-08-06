/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OUT_OF_MEMORY_HANDLER_BREAK_H_
#define _RED_MEMORY_OUT_OF_MEMORY_HANDLER_BREAK_H_

#include "oomHandler.h"

namespace red
{
namespace memory
{
	class Reporter;

	class PoolOOMHandlerBreak final : public PoolOOMHandler
	{
	public:
		PoolOOMHandlerBreak();
		~PoolOOMHandlerBreak();

		void Initialize( const Reporter * reporter );

	private:
		virtual void OnHandlePoolAllocateFailure( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) override;

		const Reporter * m_reporter;
	};

}
}

#endif
