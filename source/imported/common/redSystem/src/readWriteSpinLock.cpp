/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "readWriteSpinLock.h"

// uncomment to enable periodic spin lock stats dumping
//#define RED_PROFILE_SPIN_LOCK

namespace red
{
	static const Uint32 c_rwSpinLockMaxSpinBeforeYield = 512;
#if defined(RED_PLATFORM_DURANGO)
	// Tad: Stupidly big number as we only want to resort to a Sleep when we really
	// are in a live-lock situation. A 1ms stall is better than a 1 second stall !
	static const Uint32 c_rwSpinLockMaxSpinBeforeSleep = 6553600;
#else
	static const Uint32 c_rwSpinLockMaxSpinBeforeSleep = 16384;
#endif

#ifdef RED_PROFILE_SPIN_LOCK
	static const Uint32 g_numSamplesForHistogramDump = 10000000;
	RED_ALIGN( 64 ) static red::Atomic<Uint32> g_numSamples;
	RED_ALIGN( 64 ) static red::Atomic<Uint32> g_longAcquireCounters[ 9 ];

	static void CollectSpinLockHistogramSample( double t )
	{
		if ( t < 1.0e-8 )		g_longAcquireCounters[ 0 ].Increment();
		else if ( t < 1.0e-7 )	g_longAcquireCounters[ 1 ].Increment();
		else if ( t < 1.0e-6 )	g_longAcquireCounters[ 2 ].Increment();
		else if ( t < 1.0e-5 )	g_longAcquireCounters[ 3 ].Increment();
		else if ( t < 1.0e-4 )	g_longAcquireCounters[ 4 ].Increment();
		else if ( t < 1.0e-3 )	g_longAcquireCounters[ 5 ].Increment();
		else if ( t < 1.0e-2 )	g_longAcquireCounters[ 6 ].Increment();
		else if ( t < 1.0e-1 )	g_longAcquireCounters[ 7 ].Increment();
		else					g_longAcquireCounters[ 7 ].Increment();

		const Uint32 totalNumSamples = g_numSamples.Increment();
		if ( totalNumSamples == g_numSamplesForHistogramDump )
		{
			g_numSamples.SetValue( 0 );

			const Uint32 num0 = g_longAcquireCounters[ 0 ].Exchange( 0 );
			const Uint32 num1 = g_longAcquireCounters[ 1 ].Exchange( 0 );
			const Uint32 num2 = g_longAcquireCounters[ 2 ].Exchange( 0 );
			const Uint32 num3 = g_longAcquireCounters[ 3 ].Exchange( 0 );
			const Uint32 num4 = g_longAcquireCounters[ 4 ].Exchange( 0 );
			const Uint32 num5 = g_longAcquireCounters[ 5 ].Exchange( 0 );
			const Uint32 num6 = g_longAcquireCounters[ 6 ].Exchange( 0 );
			const Uint32 num7 = g_longAcquireCounters[ 7 ].Exchange( 0 );
			const Uint32 num8 = g_longAcquireCounters[ 8 ].Exchange( 0 );

			RED_LOG_INFO( "RWSpinLock::Acquire(Shared) time histogram:" );
			RED_LOG_INFO( "<10ns   %12u (%.3f%%)", num0, 100.0f * num0 / totalNumSamples );
			RED_LOG_INFO( "<100ns  %12u (%.3f%%)", num1, 100.0f * num1 / totalNumSamples );
			RED_LOG_INFO( "<1us    %12u (%.3f%%)", num2, 100.0f * num2 / totalNumSamples );
			RED_LOG_INFO( "<10us   %12u (%.3f%%)", num3, 100.0f * num3 / totalNumSamples );
			RED_LOG_INFO( "<100us  %12u (%.3f%%)", num4, 100.0f * num4 / totalNumSamples );
			RED_LOG_INFO( "<1ms    %12u (%.3f%%)", num5, 100.0f * num5 / totalNumSamples );
			RED_LOG_INFO( "<10ms   %12u (%.3f%%)", num6, 100.0f * num6 / totalNumSamples );
			RED_LOG_INFO( "<100ms  %12u (%.3f%%)", num7, 100.0f * num7 / totalNumSamples );
			RED_LOG_INFO( ">=100ms %12u (%.3f%%)", num8, 100.0f * num8 / totalNumSamples );
		}
	}
#endif // RED_PROFILE_SPIN_LOCK

	void RWSpinLock::AcquireShared()
	{
#ifdef RED_PROFILE_SPIN_LOCK
		red::Timer timer;
#endif // RED_PROFILE_SPIN_LOCK

		profiler::SyncPrepare( this );
		{
#if defined(USE_NATIVE_RWLOCK)
			AcquireSRWLockShared( &m_SRWLock );
#else
			Uint32 spinCount = 0;
			for (;;)
			{
				Int8 expected = atomic::FetchValue8( &m_lock );
				if ( expected != WriteLockValue )
				{
					Int8 desired = 1 + expected;
					if ( atomic::CompareExchange8( &m_lock, desired, expected ) == expected )
					{
						break;
					}
				}

				YieldThread( spinCount );
			}
#endif			
		}
		profiler::SyncAcquired( this );

#ifdef RED_PROFILE_SPIN_LOCK
		CollectSpinLockHistogramSample( timer.GetSeconds() );
#endif // RED_PROFILE_SPIN_LOCK
	}

	void RWSpinLock::Acquire()
	{
#ifdef RED_PROFILE_SPIN_LOCK
		red::Timer timer;
#endif // RED_PROFILE_SPIN_LOCK

		profiler::SyncPrepare( this );
		{
#if defined(USE_NATIVE_RWLOCK)
			AcquireSRWLockExclusive( &m_SRWLock );
#else
			Uint32 spinCount = 0;
			for (;;)
			{
				// read first, because compare-exchange always writes, even if the condition is not met
				if ( atomic::FetchValue8( &m_lock ) == UnlockValue )
				{
					if ( atomic::CompareExchange8( &m_lock, WriteLockValue, UnlockValue ) == UnlockValue )
					{
						break;
					}
				}

				YieldThread( spinCount );
			}
#endif
		}
		profiler::SyncAcquired( this );

#ifdef RED_PROFILE_SPIN_LOCK
		CollectSpinLockHistogramSample( timer.GetSeconds() );
#endif // RED_PROFILE_SPIN_LOCK
	}

	bool RWSpinLock::TryAcquire()
	{
		profiler::SyncPrepare( this );
		{
#if defined(USE_NATIVE_RWLOCK)
			if ( TryAcquireSRWLockExclusive( &m_SRWLock ) != 0 )
			{
				profiler::SyncAcquired( this );
				return true;
			}
#else
			if ( atomic::CompareExchange8( &m_lock, WriteLockValue, UnlockValue ) == UnlockValue )
			{
				profiler::SyncAcquired( this );
				return true;
			}
#endif
		}
		profiler::SyncCancel( this );
		return false;
	}

	bool RWSpinLock::TryAcquireShared()
	{
		profiler::SyncPrepare(this);
		{
#if defined(USE_NATIVE_RWLOCK)
			if ( TryAcquireSRWLockShared( &m_SRWLock ) != 0 )
			{
				profiler::SyncAcquired(this);
				return true;
			}
#else
			Int8 expected = atomic::FetchValue8(&m_lock);
			if (expected != WriteLockValue)
			{
				Int8 desired = 1 + expected;
				if (atomic::CompareExchange8(&m_lock, desired, expected) == expected)
				{
					profiler::SyncAcquired(this);
					return true;
				}
			}
#endif
		}
		profiler::SyncCancel(this);
		return false;
	}

#if !defined(USE_NATIVE_RWLOCK)
	void RWSpinLock::YieldThread( Uint32& spinCount )
	{
		spinCount++;

		if ( spinCount >= c_rwSpinLockMaxSpinBeforeSleep )
		{
#ifdef RED_PLATFORM_ORBIS
			SleepOnCurrentThread( 1 );
#elif defined(RED_PLATFORM_DURANGO)
			::Sleep(1);
#endif
			spinCount = 0;
		}
		else if ( spinCount % c_rwSpinLockMaxSpinBeforeYield == 0 )
		{
			YieldCurrentThread();
		}
	}
#endif

} // namespace red
