/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#ifndef RED_CONFIGURATION_FINAL
#define CONTINUOUS_SCREENSHOT_HACK
#endif

namespace red
{

class REDSYSTEM_API Timer
{
public:
	Timer()
	#ifdef CONTINUOUS_SCREENSHOT_HACK
		: IsEnabledTimeHack( false )
		, TimeHackBaseTime( 0.0 )
		, TimeHackCorrection( 0.0 )
		, ScreenshotFramerate( 30.0f )
	#endif
	{
		LARGE_INTEGER frequency;
		::QueryPerformanceFrequency( &frequency );

		m_ticksPerSecond = frequency.QuadPart;
		m_secondsPerTick = 1.0 / m_ticksPerSecond;

		Reset();
	}

	RED_FORCE_INLINE Double GetFrequency() const
	{
		return static_cast< Double >( m_ticksPerSecond );
	}

	RED_FORCE_INLINE void GetFrequency( Uint64& freq ) const
	{
		freq = m_ticksPerSecond;
	}

	RED_FORCE_INLINE static Uint64 GetTicks()
	{
#ifdef RED_PLATFORM_DURANGO
		return __rdtsc();
#else
		LARGE_INTEGER counter;
		::QueryPerformanceCounter(&counter);

		return counter.QuadPart;
#endif
	}

	RED_FORCE_INLINE static void GetTicks( Uint64& time )
	{
		time = GetTicks();
	}

	RED_INLINE Double GetSeconds() const
	{
#ifdef CONTINUOUS_SCREENSHOT_HACK
		if ( IsEnabledTimeHack )
		{
			return TimeHackBaseTime;
		}
#endif
		Double time = ( GetTicks() * m_secondsPerTick ) + m_startSecondsNegated;

#ifdef CONTINUOUS_SCREENSHOT_HACK
		return time - TimeHackCorrection;
#else
		return time;
#endif
	}

	RED_FORCE_INLINE void GetSeconds( Double& seconds ) const
	{
		seconds = GetSeconds();
	}

	RED_INLINE void Reset()
	{
		m_startSecondsNegated = 0;
		m_startSecondsNegated = -GetSeconds();
	}

private:
	Uint64					m_ticksPerSecond;
	Double					m_secondsPerTick;
	Double					m_startSecondsNegated;

#ifdef CONTINUOUS_SCREENSHOT_HACK
	Bool IsEnabledTimeHack;
	Double TimeHackBaseTime;
	Double TimeHackCorrection;
	Double ScreenshotFramerate;

public:
	RED_FORCE_INLINE Bool IsTimeHackEnabled() const { return IsEnabledTimeHack; }
	void EnableGameTimeHack();
	void DisableGameTimeHack();
	void NextFrameGameTimeHack();
	void SetScreenshotFramerate( Double framerate );
#endif
};

} // namespace red
