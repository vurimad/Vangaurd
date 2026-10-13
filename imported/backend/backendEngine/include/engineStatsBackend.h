/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redMemory/include/metricsUtils.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/interop/include/interopUtilsManual.h"

// Aggregate of common engine stats that aren't per editor or viewport.
namespace tools
{

namespace interopHelper
{
	class BACKEND_ENGINE_API MemoryStatsData :
		public ISerializable,
		public red::memory::RuntimeMetrics
	{
		RTTI_DECLARE_TYPE(MemoryStatsData);
	public:
		Uint64 m_engineTick;
		Float m_lastTimeDelta;

		// IO metrics - deprecated but here because of properties used in functional tests...
		Uint64 m_totalBytesAllocated;
		Uint64 m_peakTotalBytesAllocated;
		Uint64 m_maxRequestBytes;
		Uint64 m_systemMemoryConsumed;
		Uint64 m_numberGuardBytes;

		MemoryStatsData();
	};

	class BACKEND_ENGINE_API MemoryStats final
	{
		DECLARE_INTEROP(MemoryStats);

	public:
		MemoryStats();
		void Update(Uint64 _engineTick);

	private:
		Uint64						m_cachedAvailableMemory;
		Uint64						m_cachedGpuMemory;
		Uint64						m_cachedCpuMemory;
		Bool						m_notifierEnabled;
		Bool						m_fullNotifierEnabled;

		MemoryStatsData				m_data;

		::interop::InteropNotifier	m_notifier;
	};

	class TimeStats
	{
		DECLARE_INTEROP( TimeStats );

	public:
		TimeStats();
		void Update(Uint64 _engineTick);

	private:
		::interop::InteropNotifier	m_notifier;
		Bool						m_notifierEnabled;

		Int32						m_cachedFps;
		Int32						m_cachedMinFps;
	};

#ifdef USE_PROFILER
	class ProfilerStats
	{
		DECLARE_INTEROP( ProfilerStats );

	public:
		ProfilerStats();
		void Update();

	private:
		::interop::InteropNotifier	m_notifier;
		Bool						m_notifierEnabled;

		Bool						m_init;
		Bool						m_running;
		Float						m_bufferFilled;
	};
#endif

}

///---

class EngineStatsBackend
{
	DECLARE_INTEROP( EngineStatsBackend );
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	EngineStatsBackend();
	virtual ~EngineStatsBackend();

	void Update();

private:
	interopHelper::MemoryStats		m_memoryStats;
	interopHelper::TimeStats		m_timeStats;
#ifdef USE_PROFILER
	interopHelper::ProfilerStats	m_profilerStats;
#endif
};

} // tools
