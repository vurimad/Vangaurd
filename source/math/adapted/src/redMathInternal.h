/**
 * Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
 */

#pragma once

// this makes sure this header is NOT included directly from outside core
#ifndef RED_MODULE_redMath
#error "Do not include internal headers directly, please use redMathPublic.h"
#endif

// SEE - platform intrinsics
#if defined(RED_PLATFORM_WINPC) || defined(RED_PLATFORM_DURANGO)
#include <intrin.h>
#elif defined(RED_PLATFORM_ORBIS) || defined(RED_PLATFORM_LINUX)
#include <x86intrin.h>
#endif

// SSE
#include <tmmintrin.h>

// General math
#include <utility>
#include <float.h>
#include <cmath>
#include <limits>

#include "redMathPublic.h"

// put additional, private headers here
#ifdef RED_PLATFORM_DURANGO
#pragma comment(lib, "Bcrypt")
#endif