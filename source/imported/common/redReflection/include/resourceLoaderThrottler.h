/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redContainers/include/circularBuffer.h"
#include "../../redJobs/include/jobValue.h"
#include "../../redIO/include/redIOCommon.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "resourceLoaderTypes.h"

namespace red { struct InstrumentationObject; }
namespace job { struct RunContext; }
namespace serialization { class LoadingContext; }
namespace rtti { class ClassType;  }

namespace res
{
	class ResourceLoaderThrottler;

	enum class ThrottleReasonForDebug : Uint32
	{
		Invalid,
		BinaryLoader_Decompress,
		BinaryLoader_LoadObjects,
		BinaryLoader_PostLoad,
		DeferredDataBuffer_Decompress,
		TextLoader_Decompress,
		MAX,
	};

	enum class ThrottleLoadingSpeed
	{
		Suspend,
		Trickle,
		Stream,
		Flood,
		Unlimited,
		COUNT,
	};

	RED_REFLECTION_API const char* GetThrottleReasonForDebugText(ThrottleReasonForDebug reason);
	RED_REFLECTION_API const char* GetThrottleLoadingSpeedNameDebugText(ThrottleLoadingSpeed speed);
	
	// # ResourceCPUThrottler
	class RED_REFLECTION_API ResourceLoaderThrottler : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:

		static Uint32 GetMaxNoDedicatedDecompressionMemory();


		using CallbackFn = void(void*, const red::BlobSpan&);

		struct Metrics
		{
			struct Throttle
			{
				Uint32 numWaiting{ 0 };
				Uint32 numActive{ 0 };
			};

			ThrottleLoadingSpeed loadingSpeed{ ThrottleLoadingSpeed::Suspend };
			Uint32 numThrottleTokensSoftLimit{ 0 };
			Uint32 numTokensAcquired{ 0 };
			red::StaticArray< Throttle, (Uint32)ThrottleReasonForDebug::MAX> reasonCount{ (Uint32)ThrottleReasonForDebug::MAX };
		};

		ResourceLoaderThrottler();
		~ResourceLoaderThrottler();

		void SetLoadingSpeed(ThrottleLoadingSpeed speed );
		ThrottleLoadingSpeed GetLoadingSpeed() const;

		void Run(CallbackFn* callback, void* userData, Bool isPostLoad = false);
		void GetThrottleMetrics( Metrics& outMetrics ) const;

	private:
		struct ThrottleContext
		{
			CallbackFn* callback{ nullptr };
			void* userData{ nullptr };
		};

		class SoftLimits
		{
		public:
			SoftLimits()
				: m_isLazyInitializedSoftLimits( false )
			{}

			Uint32 GetSoftLimit_NoLock( ThrottleLoadingSpeed speed ) const;

		private:
			void LazyInitSoftLimits_NoLock();
			Uint32 m_numThrottleTokensSoftLimits[ static_cast<Uint32>(ThrottleLoadingSpeed::COUNT ) ];
			Bool m_isLazyInitializedSoftLimits;
		};

		static void RunThrottledStatic(void* jobData, const job::RunContext& runContext);
		void RunThrottled(const job::RunContext& runContext);
	
		red::CircularBuffer< ThrottleContext > m_throttleContexts{ red::PoolEngine() };
		red::CircularBuffer< ThrottleContext > m_postLoadContexts{ red::PoolEngine() };

		SoftLimits m_softLimits;

		mutable red::RWSpinLock m_throttleContextsLock;
		Uint32 m_numThreadTokensAcquired;
		ThrottleLoadingSpeed m_loadingSpeed;

		// #fixme: lazy init because the throttler gets created before the job system is initialized
		red::UniquePtr< job::Counter > m_danglingJobsCounterAllDispatchThreads;
#ifdef USE_RESOURCE_THROTTLER_THREADS
		red::UniquePtr< job::Counter > m_danglingJobsCounterResourceThrottlerThreads;
#endif
		red::InstrumentationObject m_instrumentationObject{ "ResourceThrottler" };

		// NOTE: memory budget taken from reducing io::c_ioAllocatorBudget accordingly
		static const Uint32 c_numReservervedBuffers = 2; // chosen for the number of throttlers during streaming on consoles
		red::DynArray<red::UniqueBuffer> m_reservedDecompressionMemoryBuffers;
		Bool m_isInitializedReservedDecompressionMemoryBuffers{ false };
		Bool m_hasDedicatedPostLoadProcessor{ false };
	};
}