/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../redJobs2/include/jobSystem.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redIO/include/redIOAsyncIO.h"

#include "serializationLoader.h"
#include "resourceLoaderThrottlerCrashData.h"
#include "resourceLoaderThrottler.h"

namespace dd
{
	red::CrashData< String > previousSpeed("ResourceLoaderThrottler", "PreviousSpeed");
	red::CrashData< String > currentSpeed("ResourceLoaderThrottler", "CurrentSpeed");
	
	red::CrashData< Uint32 > numTokensAcquired("ResourceLoaderThrottler", "NumThrottleTokensAcquired");
	red::CrashData< Uint32 > softLimit("ResourceLoaderThrottler", "NumThrottleTokensSoftLimit");
}

namespace Config
{
	// #tbd: single player vs multiplayer server
	// And may be better to prefer the second cluster on consoles
	// And if we have cutscenes and non GPU decoded videos during the loading screen, then may also need to adjust this more
	TConfigVar< Int32 > cvTrickleMaxLoadingThreads( "ResourceLoaderThrottler", "TrickleMaxLoadingThreads", 1 );
	TConfigVar< Int32 > cvStreamMaxLoadingThreads( "ResourceLoaderThrottler", "StreamMaxLoadingThreads", 2 );
	TConfigVar< Int32 > cvFloodMinNonLoadingThreads( "ResourceLoaderThrottler", "FloodMinNonLoadingThreads", 2 );

	// NOTE: currently allocated by the throttler using job::PoolJobScope, so limited there.
	// Should probably just return a buffer here using dedicated I/O memory, but if on PC with many throttler threads, then could use too much I/O memory
	TConfigVar< Int32 > cvDedicatedDecompressionMemoryBytes("ResourceLoaderThrottler", "DedicatedDecompressionMemoryBytes", RED_MEGA_BYTE(1));
}

namespace res
{

Uint32 ResourceLoaderThrottler::GetMaxNoDedicatedDecompressionMemory()
{
	return Config::cvDedicatedDecompressionMemoryBytes.Get();
}

const Uint32 c_unlimitedThrottleLimit = ~0U; // unlimited for all intents

namespace helper
{
	static Int32 ClampMaxLoadingThreads( Int32 maxLoadingThreads )
	{
		const Int32 hwConcurrency = job::GetNumDispatcherThreads() + 1;
		RED_FATAL_ASSERT( hwConcurrency > 0 );
		return Clamp<Int32>( maxLoadingThreads, 1, hwConcurrency );
	}

	static Int32 GetFloodMaxLoadingThreads()
	{
		const Int32 hwConcurrency = job::GetNumDispatcherThreads() + 1;
		RED_FATAL_ASSERT( hwConcurrency > 0 );

		Int32 minNonLoadingThreads = Config::cvFloodMinNonLoadingThreads.Get();
		if ( minNonLoadingThreads < 0 )
		{
			minNonLoadingThreads = 0;
		}

		const Int32 maxLoadingThreads = hwConcurrency - minNonLoadingThreads;
		return Clamp< Int32 >( maxLoadingThreads, 1, hwConcurrency );
	}
}

const char* GetThrottleReasonForDebugText(ThrottleReasonForDebug reason)
{
	switch (reason)
	{
	case ThrottleReasonForDebug::Invalid: return "Invalid";
	case ThrottleReasonForDebug::BinaryLoader_Decompress: return "BinaryLoader/Decompress";
	case ThrottleReasonForDebug::BinaryLoader_LoadObjects: return "BinaryLoader/LoadObjects";
	case ThrottleReasonForDebug::BinaryLoader_PostLoad: return "BinaryLoader/PostLoad";
	case ThrottleReasonForDebug::DeferredDataBuffer_Decompress: return "DeferredDataBuffer/Decompress";
	case ThrottleReasonForDebug::TextLoader_Decompress: return "TextLoader/Decompress";
	default:
		break;
	}
	return "<Uknown Reason>";
}

const char* GetThrottleLoadingSpeedNameDebugText(ThrottleLoadingSpeed speed)
{
	switch (speed)
	{
	case ThrottleLoadingSpeed::Suspend: return "Suspend";
	case ThrottleLoadingSpeed::Trickle: return "Trickle";
	case ThrottleLoadingSpeed::Stream: return "Stream";
	case ThrottleLoadingSpeed::Flood: return "Flood";
	case ThrottleLoadingSpeed::Unlimited: return "Unlimited";
	default:
		break;
	}
	return "<Unknown speed>";
}

Uint32 ResourceLoaderThrottler::SoftLimits::GetSoftLimit_NoLock(ThrottleLoadingSpeed speed ) const
{
	const_cast<SoftLimits*>( this )->LazyInitSoftLimits_NoLock();
	return m_numThrottleTokensSoftLimits[ static_cast<Uint32>( speed ) ];
}

void ResourceLoaderThrottler::SoftLimits::LazyInitSoftLimits_NoLock()
{
	// Lazy init because the job system isn't initialized at the time the resource loader is created
	if ( !m_isLazyInitializedSoftLimits )
	{
		m_isLazyInitializedSoftLimits = true;
		m_numThrottleTokensSoftLimits[ static_cast<Uint32>( ThrottleLoadingSpeed::Suspend ) ] = 0;
		m_numThrottleTokensSoftLimits[ static_cast<Uint32>( ThrottleLoadingSpeed::Trickle ) ] = helper::ClampMaxLoadingThreads( Config::cvTrickleMaxLoadingThreads.Get() );
		m_numThrottleTokensSoftLimits[ static_cast<Uint32>( ThrottleLoadingSpeed::Stream ) ] = helper::ClampMaxLoadingThreads( Config::cvStreamMaxLoadingThreads.Get() );
		m_numThrottleTokensSoftLimits[ static_cast<Uint32>( ThrottleLoadingSpeed::Flood ) ] = helper::GetFloodMaxLoadingThreads();
		m_numThrottleTokensSoftLimits[ static_cast<Uint32>( ThrottleLoadingSpeed::Unlimited ) ] = c_unlimitedThrottleLimit;
	}
}

ResourceLoaderThrottler::ResourceLoaderThrottler()
	: m_numThreadTokensAcquired( 0 )
	, m_loadingSpeed(ThrottleLoadingSpeed::Unlimited )
	, m_throttleContexts( io::eAsyncPriority_COUNT, red::PoolEngine() )
	, m_reservedDecompressionMemoryBuffers( red::PoolEngine() )
{
}

ResourceLoaderThrottler::~ResourceLoaderThrottler()
{
	if (m_danglingJobsCounterAllDispatchThreads)
	{
		job::FlushCounter(*m_danglingJobsCounterAllDispatchThreads);
	}
#ifdef USE_RESOURCE_THROTTLER_THREADS
	if (m_danglingJobsCounterResourceThrottlerThreads)
	{
		job::FlushCounter(*m_danglingJobsCounterResourceThrottlerThreads);
	}
#endif
}

void ResourceLoaderThrottler::SetLoadingSpeed(ThrottleLoadingSpeed speed )
{
	// Lock so other code can have a consistent speed while under the lock
	RED_SCOPE_LOCK( m_throttleContextsLock );

	if (m_loadingSpeed != speed)
	{
		dd::previousSpeed.Set(GetThrottleLoadingSpeedNameDebugText(m_loadingSpeed));
		dd::currentSpeed.Set(GetThrottleLoadingSpeedNameDebugText(speed));

		m_loadingSpeed = speed;
	}
}

ThrottleLoadingSpeed ResourceLoaderThrottler::GetLoadingSpeed() const
{
	RED_SCOPE_SHARED_LOCK( m_throttleContextsLock );
	return m_loadingSpeed;
}

static Bool thread_local g_tls_reentrancyGuard = false;

void ResourceLoaderThrottler::Run(CallbackFn* callback, void* userData, Bool isPostLoad)
{
	//job::FatalAssertIfDispatcherThread();

#ifdef RED_ASSERTS_ENABLED
	RED_FATAL_ASSERT(!g_tls_reentrancyGuard);
	red::ScopedFlag< Bool > guard{ g_tls_reentrancyGuard = true, false };
#endif

	RED_FATAL_ASSERT(callback);

	ThrottleContext context;
	context.callback = callback;
	context.userData = userData;

	Bool tryUseNewThread = false;
	{
		RED_SCOPE_LOCK( m_throttleContextsLock );

		if (!m_danglingJobsCounterAllDispatchThreads)
		{
			m_danglingJobsCounterAllDispatchThreads = red::CreateUniquePtr<job::Counter>(job::Priority::Latent);
		}
#ifdef USE_RESOURCE_THROTTLER_THREADS
		if (!m_danglingJobsCounterResourceThrottlerThreads)
		{
			job::ScheduleParam params = { job::Priority::Latent, job::Affinity::ResourceThrottler };
			m_danglingJobsCounterResourceThrottlerThreads = red::CreateUniquePtr<job::Counter>(params);
		}
#endif
		if (!m_isInitializedReservedDecompressionMemoryBuffers)
		{
			m_isInitializedReservedDecompressionMemoryBuffers = true;

			for (Uint32 i = 0; i < c_numReservervedBuffers; ++i)
			{
				auto buffer = red::CreateUniqueBuffer<red::PoolResource>(GetMaxNoDedicatedDecompressionMemory(), 1);
				RED_FATAL_ASSERT(buffer);

				m_reservedDecompressionMemoryBuffers.PushBack(std::move(buffer));
			}
		}



		const Uint32 softLimit = m_softLimits.GetSoftLimit_NoLock( m_loadingSpeed );
		dd::softLimit.Set(softLimit);

		if (isPostLoad)
		{
			m_postLoadContexts.PushBack(context);
		}
		else
		{
			m_throttleContexts.PushBack(context);
		}

		if ( m_numThreadTokensAcquired + 1 <= softLimit)
		{
			m_numThreadTokensAcquired += 1;
			tryUseNewThread = true;
		}

		dd::numTokensAcquired.Set(m_numThreadTokensAcquired);
	}

	if (tryUseNewThread)
	{
		job::JobDecl jobDecl;
		jobDecl.jobFunc = RunThrottledStatic;
		jobDecl.jobData = this;
		jobDecl.instrumentationObject = &m_instrumentationObject;

		job::Counter dummyWait{ job::Priority::Latent };

#ifdef USE_RESOURCE_THROTTLER_THREADS
		// Only use the rtThreads when we are in normal streaming mode (i.e. in game)
		// During FLOOD or Unlimited loading we utilise ALL dispatch threads as usual
		if ( m_loadingSpeed == ThrottleLoadingSpeed::Stream || m_loadingSpeed == ThrottleLoadingSpeed::Trickle )
		{
			// Run these as low priority threads in the background to soak up free CPU time
			job::RunJob(jobDecl, dummyWait, *m_danglingJobsCounterResourceThrottlerThreads);
		}
		else
#endif
			job::RunJob(jobDecl, dummyWait, *m_danglingJobsCounterAllDispatchThreads);
	}
}

void ResourceLoaderThrottler::RunThrottledStatic(void* jobData, const job::RunContext& runContext)
{
	auto* thisPtr = static_cast<ResourceLoaderThrottler*>(jobData);
	thisPtr->RunThrottled(runContext);
}

void ResourceLoaderThrottler::RunThrottled(const job::RunContext& runContext)
{
	red::UniqueBuffer decompressionMemory;
	Bool isReservedDecompressionMemoryBuffer = false;

	{
		RED_SCOPE_LOCK(m_throttleContextsLock);

		if (!m_reservedDecompressionMemoryBuffers.Empty())
		{
			decompressionMemory = m_reservedDecompressionMemoryBuffers.PopBack();
			isReservedDecompressionMemoryBuffer = true;
		}
		else
		{
			// #TBD?? should only be on PC or console while loading and allowed to have more throttlers
			decompressionMemory = red::CreateUniqueBuffer<red::PoolResource>(GetMaxNoDedicatedDecompressionMemory(), 1);
			RED_FATAL_ASSERT(decompressionMemory);
		}
	}

	RED_FATAL_ASSERT(decompressionMemory);

	Bool isDedicatedPostLoadProcessor = false;

	for (;;)
	{
		Bool isNextContextValid = false;
		ThrottleContext nextContext;
		{
			RED_SCOPE_LOCK(m_throttleContextsLock);

			const Uint32 softLimit = m_softLimits.GetSoftLimit_NoLock(m_loadingSpeed);
			if ( (m_throttleContexts.Empty() && m_postLoadContexts.Empty()) || (m_numThreadTokensAcquired > softLimit))
			{
				RED_FATAL_ASSERT(m_numThreadTokensAcquired > 0);
				m_numThreadTokensAcquired -= 1;

				if (isDedicatedPostLoadProcessor)
				{
					isDedicatedPostLoadProcessor = false;
					m_hasDedicatedPostLoadProcessor = false;
				}

				break;
			}

			if (!m_postLoadContexts.Empty())
			{
				if (!m_hasDedicatedPostLoadProcessor)
				{
					isDedicatedPostLoadProcessor = true;
					m_hasDedicatedPostLoadProcessor = true;
				}

				if (isDedicatedPostLoadProcessor || m_throttleContexts.Empty())
				{
					nextContext = m_postLoadContexts.Front();
					m_postLoadContexts.PopFront();

					isNextContextValid = true;
				}
			}

			if (!isNextContextValid)
			{
				RED_FATAL_ASSERT(!m_throttleContexts.Empty(), "Should have broken out of loop above if no more work available");
				nextContext = m_throttleContexts.Front();
				m_throttleContexts.PopFront();

				isNextContextValid = true;
			}
		}
		
		RED_FATAL_ASSERT(isNextContextValid);
		(*nextContext.callback)(nextContext.userData, red::MakeBlobSpan(decompressionMemory));
	}

	if (isReservedDecompressionMemoryBuffer)
	{
		RED_SCOPE_LOCK(m_throttleContextsLock);

		m_reservedDecompressionMemoryBuffers.PushBack(std::move(decompressionMemory));		
	}
}

void ResourceLoaderThrottler::GetThrottleMetrics( Metrics& outMetrics ) const
{
	RED_SCOPE_SHARED_LOCK( m_throttleContextsLock );

	outMetrics.loadingSpeed = m_loadingSpeed;
	outMetrics.numThrottleTokensSoftLimit = m_softLimits.GetSoftLimit_NoLock( m_loadingSpeed );
	outMetrics.numTokensAcquired = m_numThreadTokensAcquired;
}

}
