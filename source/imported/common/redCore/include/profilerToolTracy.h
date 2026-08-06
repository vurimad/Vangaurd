/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "profiler.h"


class REDCORE_API TracyProfilerTool
{
public:
#ifdef USE_TRACY_PROFILER
	void Init(const Uint32 mem);
	void Shutdown();
	void Start();
	void Stop();

	void NextFrame(red::ProfilerFrameType frameType);
	void Update();

	void StartBlock( red::InstrumentationObject* block, const char* scopeName );
	void StopBlock( red::InstrumentationObject* block, const char* scopeName );
#endif
};


