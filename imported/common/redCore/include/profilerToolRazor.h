/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "profiler.h"


class RazorProfilerTool
{
public:
#ifdef USE_RAZOR_PROFILER

	static void StartCPUCapture();
	static void StopCPUCapture();

	void Init( const Uint32 mem ) {}
	void Shutdown() {}
	void Start() {}
	void Stop() {}

	void NextFrame( red::ProfilerFrameType frameType );
	RED_INLINE void Update() {}

	void StartBlock( red::InstrumentationObject* block, const char* scopeName );
	void StopBlock( red::InstrumentationObject* block, const char* scopeName );

	//void Message( CProfilerBlock* block, const char* msg );
	#endif
};
