/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_HOOKS_H_
#define _RED_MEMORY_INCLUDE_HOOKS_H_

#include "hookTypes.h"

namespace red
{
namespace memory
{
	RED_MEMORY_API HookHandle CreateHook( const HookCreationParameter & param );
	RED_MEMORY_API void RemoveHook( HookHandle handle );
}
}

#endif
