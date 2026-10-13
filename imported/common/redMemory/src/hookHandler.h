/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_HOOK_HANDLER_H_
#define _RED_MEMORY_HOOK_HANDLER_H_

#include "hookPool.h"
#include "scopedLock.h"
#include "spinLock.h"
#include "../include/hookTypes.h"

namespace red
{
namespace memory
{
	class Hook;
	struct Block;

	class RED_MEMORY_API HookHandler
	{
	public:

		HookHandler();
		~HookHandler();

		void Initialize();

		HookHandle Create( const HookCreationParameter & param );
		void Remove( HookHandle handle );

		void ProcessPreHooks( HookPreParameter & param, u32 disabledHooks );
		void ProcessPostHooks( HookPostParameter & param, u32 disabledHooks );

	private:

		void Register( Hook * hook );
		void Unregister( Hook * hook );

		Hook * m_rootHook;
		HookPool m_pool;
	};
}
}

#include "hookHandler.hpp"

#endif
