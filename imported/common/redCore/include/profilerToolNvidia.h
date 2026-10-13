/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "profiler.h"


class REDCORE_API NvtxProfilerTool
{
public:
#ifdef USE_NVIDIA_PROFILER
	void Init( const Uint32 mem ) {}
	void Shutdown() {}
	void Start() {}
	void Stop() {}

	RED_INLINE void NextFrame( red::ProfilerFrameType frameType ) { RED_UNUSED( frameType ); }
	RED_INLINE void Update() {}

	void StartBlock( red::InstrumentationObject* block, const char* scopeName );
	void StopBlock( red::InstrumentationObject* block, const char* scopeName );
#endif
};


