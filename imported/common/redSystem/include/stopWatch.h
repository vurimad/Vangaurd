/**
* Copyright (c) 2013-16 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include "utility.h"
#include "clock.h"


namespace red
{
	// StopWatch aka CTimerCounter
	class StopWatch : public NonCopyable
	{
	public:
		RED_INLINE StopWatch()
		{
			Reset();
		}

		RED_INLINE void Reset()
		{
			m_startTime = Clock::GetInstance().GetTimer().GetSeconds();
		}

		// returns time delta in [s]
		RED_INLINE Double GetDelta() const
		{
			return Clock::GetInstance().GetTimer().GetSeconds() - m_startTime;
		}

		// returns time delta in [s] as float
		RED_INLINE Float GetDeltaF() const
		{
			return static_cast<Float>( GetDelta() );
		}

		// returns time delta in [ms]
		RED_INLINE Double GetDeltaMS() const
		{
			return (Clock::GetInstance().GetTimer().GetSeconds() - m_startTime)*1000.0;
		}

		// returns the start time
		RED_INLINE Double GetStartTime() const
		{
			return m_startTime;
		}

	protected:
		Double m_startTime;
	};

	// Scoped Stop Watch
	class ScopedStopWatch : public StopWatch
	{
	public:
		RED_INLINE ScopedStopWatch( Double& result )
			: m_result( result )
		{
		}

		RED_INLINE ~ScopedStopWatch()
		{
			m_result = GetDelta();
		}

	private:
		Double& m_result;
	};

	class NullScopedStopWatch
	{
	public:
		RED_INLINE NullScopedStopWatch( Double& result ) { RED_UNUSED( result ); }
	};

#ifdef RED_LOGGING_ENABLED
	using ScopedStopWatchForLoggingTime = ScopedStopWatch;
#else
	using ScopedStopWatchForLoggingTime = NullScopedStopWatch;
#endif
}
