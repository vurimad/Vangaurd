/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "profiler.h"

#ifdef USE_VTUNE_PROFILER
# include "../../../external/ittnotify/include/ittnotify.h"
#endif

class REDCORE_API VTuneProfilerTool
{
public:
#ifdef USE_VTUNE_PROFILER
	VTuneProfilerTool();
	void Init( const Uint32 mem );
	void Shutdown() {}
	void Start() {}
	void Stop() {}

	void InitThread( const char* threadName );
	void InitBlock( red::InstrumentationObject* block, const char* scopeName );

	void EmitGlobalMarker( red::InstrumentationObject* block );
	void NextFrame( red::ProfilerFrameType frameType );
	RED_INLINE void Update() {}

	void StartBlock( red::InstrumentationObject* block, const char* scopeName );
	void StopBlock( red::InstrumentationObject* block, const char* scopeName );
private:
	__itt_domain* m_domains[ PBC_COUNT ];
	__itt_string_handle* m_blockStrings[ PROFILER_MAX_SCOPES ];
	__itt_domain* m_frameDomain;
#endif
};

