/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{

class Timer
{
public:
	Timer()
		: m_startSecondsNegated( 0.0 )
	{
		m_ticksPerSecond = sceKernelGetTscFrequency();
		m_secondsPerTick = 1.0 / m_ticksPerSecond;

		Reset();
	}

	RED_FORCE_INLINE static Uint64 GetTicks()
	{
		return sceKernelReadTsc();
	}

	RED_FORCE_INLINE static void GetTicks( Uint64& time )
	{
		time = GetTicks();
	}

	RED_FORCE_INLINE Double GetFrequency() const
	{
		return static_cast< Double >( m_ticksPerSecond );
	}

	RED_FORCE_INLINE void GetFrequency( Uint64& freq ) const
	{
		freq = m_ticksPerSecond;
	}

	RED_INLINE Double GetSeconds() const
	{
		return ( GetTicks() * m_secondsPerTick ) + m_startSecondsNegated;
	}

	RED_FORCE_INLINE void GetSeconds( Double& seconds ) const
	{
		seconds = GetSeconds();
	}

	RED_FORCE_INLINE void Reset()
	{
		m_startSecondsNegated = 0;
		m_startSecondsNegated = -GetSeconds();
	}

private:

	Uint64 m_ticksPerSecond;
	Double m_secondsPerTick;
	Double m_startSecondsNegated;
};

}
