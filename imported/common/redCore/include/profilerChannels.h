/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redCoreApi.h"

// Note: order matters here, should go from generic to more or equally specific
// When there are multiple channels, the highest one can be chosen as representative
// Note2: Has to reflect the count and order of colors in instrumentationObject.cpp
enum EProfilerBlockChannel : Uint32
{
	PBC_NONE = 0,
	PBC_JOB,	
	PBC_IO,
	PBC_PHYSX,
	PBC_RENDER,
	PBC_ANIMATION,
	PBC_AUDIO,
	PBC_STREAMING,
	PBC_UI,
	PBC_SPAWNING,
	PBC_RUNTIMESYSTEM,
	PBC_LOADINGFENCE,
	PBC_SCRIPTS,
	PBC_BUCKET,
	PBC_COUNT,
};

extern REDCORE_API const char* GetProfilerBlockChannelName( EProfilerBlockChannel channel );