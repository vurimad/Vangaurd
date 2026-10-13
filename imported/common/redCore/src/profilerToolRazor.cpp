/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "profilerToolRazor.h"


#ifdef USE_RAZOR_PROFILER

#include <perf.h>

void RazorProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
{
	const uint32_t color = red::SelectMarkerColor( block, scopeName ).ToUint32();
	sceRazorCpuPushMarkerStatic( scopeName, color, 0 );
}

void RazorProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	RED_UNUSED( scopeName );
	sceRazorCpuPopMarker();
}

// #tbd: make bookmarks instead...
void RazorProfilerTool::StartCPUCapture()
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB 
	const Int32 ret = sceRazorCpuStartCapture();
	if (ret != SCE_OK)
	{
		RED_LOG_WARNING("Failed to start CPU capture");
	}
#endif
}

void RazorProfilerTool::StopCPUCapture()
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	sceRazorCpuStopCapture();
#endif
}

void RazorProfilerTool::NextFrame( red::ProfilerFrameType frameType )
{
	if ( frameType == red::ProfilerFrameType::PFT_ENGINE )
	{
		sceRazorCpuSync();
	}
}

#else
	RED_NO_EMPTY_FILE();
#endif
