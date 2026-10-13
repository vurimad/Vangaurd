/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_CALLSTACK_COLLECTOR_H_
#define _RED_MEMORY_CALLSTACK_COLLECTOR_H_

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
#include "callstackCollectorWin.h"
#elif defined( RED_PLATFORM_ORBIS )
#include "callstackCollectorOrbis.h"
#elif defined( RED_PLATFORM_LINUX )
#include "callstackCollectorLinux.h"
#endif

namespace red
{
namespace memory
{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	typedef CallstackCollectorWin CallstackCollector;
#elif defined( RED_PLATFORM_ORBIS )
	typedef CallstackCollectorOrbis CallstackCollector;
#elif defined( RED_PLATFORM_LINUX )
	typedef CallstackCollectorLinux CallstackCollector;
#endif
}
}

#endif