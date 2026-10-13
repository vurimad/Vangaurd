/**
* Copyright (c) 2009 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "engineTime.h"
#include "scriptStackFrame.h"

RTTI_BEGIN_TYPE( EngineTime );
	RTTI_IMPORT_ONLY();
	RTTI_NATIVE_STATIC_FUNCTION( "IsValid", funcIsValid );
	RTTI_NATIVE_STATIC_FUNCTION( "FromFloat", funcFromFloat );
	RTTI_NATIVE_STATIC_FUNCTION( "ToFloat", funcToFloat );
	RTTI_NATIVE_STATIC_FUNCTION( "ToString", funcToString );
RTTI_END_TYPE();

#ifdef RED_PLATFORM_LINUX
# define NANOSEC_PER_SEC 1000000000L
# define ENGINE_TIME_CLOCK CLOCK_MONOTONIC_RAW

void operator-=( struct timespec& a, const struct timespec& b )
{
	if ( a.tv_nsec < b.tv_nsec )
	{
		a.tv_sec -= 1;
		a.tv_nsec += NANOSEC_PER_SEC;
	}

	a.tv_sec -= b.tv_sec;
	a.tv_nsec -= b.tv_nsec;
}

void operator+=( struct timespec& a, const struct timespec& b )
{
	a.tv_sec += b.tv_sec;
	a.tv_nsec += b.tv_nsec;

	if ( a.tv_nsec >= NANOSEC_PER_SEC )
	{
		a.tv_sec += 1;
		a.tv_nsec -= NANOSEC_PER_SEC;
	}
}

namespace EngineTimeHelpers
{
	static struct timespec s_initTime;
}

Int32			EngineTime::s_timerFreq = 0;

#endif // RED_PLATFORM_LINUX

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
LARGE_INTEGER	EngineTime::s_timerFreq		= {0, 0};
#elif defined( RED_PLATFORM_ORBIS )
SceInt32		EngineTime::s_timerFreq = 0;
Int64			EngineTime::s_baseTime = 0;
#endif

Double			EngineTime::s_timerFreqDbl = 0.0;

const EngineTime EngineTime::ZERO;

void EngineTime::Init()
{
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
	QueryPerformanceFrequency(&s_timerFreq);
	s_timerFreqDbl = Double( s_timerFreq.QuadPart );

#elif defined( RED_PLATFORM_ORBIS )
	s_timerFreq = ::sceRtcGetTickResolution();
	RED_ASSERT( s_timerFreq > 0 );
	s_timerFreqDbl = Double( s_timerFreq );

	SceRtcTick tick;
	RED_VERIFY(::sceRtcGetCurrentTick(&tick) == SCE_OK);
	s_baseTime = tick.tick;
#elif defined( RED_PLATFORM_LINUX )
	Int32 status = clock_gettime( ENGINE_TIME_CLOCK, &EngineTimeHelpers::s_initTime );
	RED_ASSERT( status == 0, "clock_gettime failed with errno=%d", errno );
	
	s_timerFreq = NANOSEC_PER_SEC;
	s_timerFreqDbl = Double( s_timerFreq );

#endif
}

EngineTime EngineTime::GetNow()
{
	EngineTime result;
	result.SetNow();
	return result;
}

void EngineTime::SetNow()
{
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
	QueryPerformanceCounter(&m_time);

#elif defined( RED_PLATFORM_ORBIS )
	SceRtcTick tick;
	RED_VERIFY( ::sceRtcGetCurrentTick( &tick ) == SCE_OK );
	RED_FATAL_ASSERT(tick.tick >= s_baseTime, "sceRtcGetCurrentTick backwards in time: %llu vs %llu", tick.tick, s_baseTime);
	m_time = tick.tick - s_baseTime;

#elif defined( RED_PLATFORM_LINUX )
	// Gives almost 300 years uptime until overflow at nanosecond precision
	struct timespec tp;
	clock_gettime( ENGINE_TIME_CLOCK, &tp );
	tp -= EngineTimeHelpers::s_initTime;
	m_time = tp.tv_sec * NANOSEC_PER_SEC + tp.tv_nsec;

#endif
}