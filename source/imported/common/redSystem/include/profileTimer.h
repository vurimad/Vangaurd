#pragma once

// #fixme: red::StopWatch precision and that continuous screenshot hack

#include "clock.h"

namespace red
{

//#tbd: StopWatch timer, but this has better precision, performance, and no "continuous screenshot hack" junk
class REDSYSTEM_API ProfileTimer
{
public:
	RED_FORCE_INLINE
	static Uint64 GetTicks()
	{
		return red::Clock::GetInstance().GetTimer().GetTicks();
	}

	RED_FORCE_INLINE
	static Uint64 GetNsec(Uint64 deltaTicks)
	{
		const Uint64 deltaUsec = deltaTicks * 1000000000;
		Uint64 freq = 1;
		red::Clock::GetInstance().GetTimer().GetFrequency(freq);
		return deltaUsec / freq;
	}

	RED_FORCE_INLINE
	static Uint64 GetUsec( Uint64 deltaTicks )
	{
		const Uint64 deltaUsec = deltaTicks * 1000000;
		Uint64 freq = 1;
		red::Clock::GetInstance().GetTimer().GetFrequency( freq );
		return deltaUsec / freq;
	}

	RED_FORCE_INLINE
	static Uint64 GetMsec( Uint64 deltaTicks )
	{
		const Uint64 deltaMsec = deltaTicks * 1000;
		Uint64 freq = 1;
		red::Clock::GetInstance().GetTimer().GetFrequency( freq );
		return deltaMsec / freq;
	}

	RED_FORCE_INLINE
	static Uint64 GetSec( Uint64 deltaTicks )
	{
		const Uint64 deltaSec = deltaTicks;
		Uint64 freq = 1;
		red::Clock::GetInstance().GetTimer().GetFrequency( freq );
		return deltaSec / freq;
	}

	RED_FORCE_INLINE
	static Uint64 GetDeltaNsec(Uint64 endTicks, Uint64 startTicks)
	{
		return GetNsec(endTicks - startTicks);
	}

	RED_FORCE_INLINE
	static Uint64 GetDeltaUsec( Uint64 endTicks, Uint64 startTicks )
	{
		return GetUsec( endTicks - startTicks );
	}
	
	RED_FORCE_INLINE
	static Uint64 GetDeltaMsec( Uint64 endTicks, Uint64 startTicks )
	{
		return GetMsec( endTicks - startTicks );
	}

	RED_FORCE_INLINE
	static Uint64 GetDeltaSec( Uint64 endTicks, Uint64 startTicks )
	{	
		return GetSec( endTicks - startTicks );
	}

public:
	RED_FORCE_INLINE
	ProfileTimer()
	{
		Reset();
	}

	ProfileTimer& operator=(const ProfileTimer&) = delete;
	ProfileTimer( const ProfileTimer& ) = delete;

	RED_FORCE_INLINE
	void Reset()
	{
		m_startTicks = red::Clock::GetInstance().GetTimer().GetTicks();
	}

	RED_FORCE_INLINE
	Uint64 GetDeltaNsec() const
	{
		const Uint64 endTick = red::Clock::GetInstance().GetTimer().GetTicks();
		return ProfileTimer::GetDeltaNsec(endTick, m_startTicks);
	}

	RED_FORCE_INLINE
	Uint64 GetDeltaUsec() const
	{
		const Uint64 endTick = red::Clock::GetInstance().GetTimer().GetTicks();
		return ProfileTimer::GetDeltaUsec( endTick, m_startTicks );
	}

	RED_FORCE_INLINE
	Uint64 GetDeltaMsec() const
	{
		const Uint64 endTick = red::Clock::GetInstance().GetTimer().GetTicks();
		return ProfileTimer::GetDeltaMsec( endTick, m_startTicks );
	}

	RED_FORCE_INLINE
	Uint64 GetDeltaSec() const
	{
		const Uint64 endTick = red::Clock::GetInstance().GetTimer().GetTicks();
		return ProfileTimer::GetDeltaSec( endTick, m_startTicks );
	}

private:
	Uint64 m_startTicks;
};

} // red

