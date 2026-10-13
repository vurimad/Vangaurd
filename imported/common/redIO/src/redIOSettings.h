/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redIOCommon.h"

#if defined( RED_PLATFORM_WINPC )
//# define RED_USE_IOWORKER_WIN32
# define RED_USE_IOWORKER_GENERIC
#elif defined( RED_PLATFORM_DURANGO )
#  define RED_USE_IOWORKER_WIN32
//# define RED_USE_IOWORKER_GENERIC
#elif defined( RED_PLATFORM_ORBIS )
# define RED_USE_IOWORKER_ORBIS
#elif defined( RED_PLATFORM_LINUX )
# define RED_USE_IOWORKER_GENERIC
#else
#error Undefined platform
#endif
