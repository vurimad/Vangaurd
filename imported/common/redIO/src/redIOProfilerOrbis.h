/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_PLATFORM_ORBIS ) && defined( USE_PROFILER )

#include <perf.h>

#include "../../redSystem/include/readWriteSpinLock.h"
#include "redIOProfilerVisitor.h"

namespace io
{
namespace orbis
{

struct ProfilerSetup
{
	Uint32 traceBufferSize{ 0 };
};

class Profiler : red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	explicit Profiler( const ProfilerSetup& setup );
	~Profiler();

	struct ResultInfo
	{
		Uint64 traceStartedTimestamp{ 0 };
		Bool validTrace{ false };
		Bool aioWriteBufferFull{ false };
		Bool bio2WriteBufferFull{ false };
	};

	void StartTrace();	
	ResultInfo StopTrace();
	void VisitTrace( IProfileStreamVisitor& visitor );

	void ProfileIOWorkerBegin( Uint32 priority );
	void ProfileIOWorkerEnd( Uint32 priority );

	void ProfileDecompressStart( const char* resourcePath );
	void ProfileDecompressEnd( const char* resourcePath );
	void ProfileMarkEvent( const char* eventName );

private:
	ResultInfo StopTrace_NoLock();

	struct PerfTraceInfo
	{
		Int32 aioTraceID{ -1 };
		void* aioTraceBuffer{ nullptr };

		Int32 bio2TraceID{ -1 };
		void* bio2TraceBuffer{ nullptr };
	};

	enum TraceState : Uint8
	{
		TS_Clear,
		TS_InProgress,
		TS_Stopped,
	};

	TraceState GetTraceStateAtomicRelaxed() const { return const_cast<volatile TraceState&>( m_traceState ); }

	mutable red::SpinLock m_traceLock;
	
	red::DynArray< DecompressionEvent > m_decompressionEvents{ red::PoolDebug() };
	mutable red::SpinLock m_decompressionEventsLock;

	red::StaticArray< red::DynArray< IOWorkerCPUEvent >, 3 > m_ioWorkerCPUEvents;
	mutable red::RWSpinLock m_ioWorkerCPUEventsLock;

	red::DynArray< MarkEvent > m_markEvents{ red::PoolDebug() };
	mutable red::SpinLock m_markEventsLock;

	PerfTraceInfo InitPerfTrace( const ProfilerSetup& setup );

	ProfilerSetup m_setup;
	PerfTraceInfo m_info;

	ScePerfTraceInfo m_aioInfo;
	ScePerfTraceInfo m_bio2Info;
	Uint64 m_traceStartTimestamp;

	TraceState m_traceState;
};

} // orbis
} // io

#endif
