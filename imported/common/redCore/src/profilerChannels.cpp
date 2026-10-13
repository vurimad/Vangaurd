/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "profilerChannels.h"

RED_NO_EMPTY_FILE();

REDCORE_API const char* GetProfilerBlockChannelName( EProfilerBlockChannel channel )
{
	switch( channel )
	{
	case PBC_NONE:			return "None";
	case PBC_JOB:			return "Job";
	case PBC_IO:			return "IO";
	case PBC_PHYSX:			return "PhysX";
	case PBC_RENDER:		return "Render";
	case PBC_ANIMATION:		return "Animation";
	case PBC_AUDIO:			return "Audio";
	case PBC_STREAMING:		return "Streaming";
	case PBC_UI:			return "UI";
	case PBC_SPAWNING:		return "Spawning";
	case PBC_RUNTIMESYSTEM:	return "RuntimeSystem";
	case PBC_LOADINGFENCE:	return "LoadingFence";
	case PBC_SCRIPTS:		return "Scripts";
	case PBC_BUCKET:		return "Bucket";
	default:
		break;
	}

	return "<Unknown>";
}
