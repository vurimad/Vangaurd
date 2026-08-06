/**
* Copyright (c) 2007 CDProjekt Red, Inc. All Rights Reserved.
*
*/

#pragma once

#ifdef RED_PLATFORM_ORBIS
#	include <ctype.h>
#endif
#include "redCoreApi.h"

static const Uint64 HASH64_BASE = 0xCBF29CE484222325; // FNV64_Prime

// Calculate Hash of buffer
REDCORE_API Uint64 ACalcBufferHash64Merge( const void* buffer, size_t sizeInBytes, Uint64 previousHash );
