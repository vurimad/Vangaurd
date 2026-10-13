/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "profilerToolNvidia.h"


#ifdef USE_NVIDIA_PROFILER

#include "../../../external/nvToolsExt/include/nvToolsExt.h"

void NvtxProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	nvtxRangePushA( scopeName );
}

void NvtxProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	RED_UNUSED( scopeName );
	nvtxRangePop();
}


#else
	RED_NO_EMPTY_FILE();
#endif