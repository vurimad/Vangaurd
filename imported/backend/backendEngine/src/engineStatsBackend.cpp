/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/engine/include/baseEngine.h"
#include "../../../common/engine/src/profilerControlCenter.h"
#include "../../../common/redMemory/include/sharedPtr.h"

#include "../../../common/interop/include/interopBase.h"
#include "../../../common/interop/include/interopInterpreter.h"

#include "engineStatsBackend.h"

using namespace tools;

RTTI_BEGIN_TYPE_IN_NAMESPACE(MemoryStatsData, interopHelper)
	RTTI_PARENT_TYPE(ISerializable);
	RTTI_DESCRIPTIVE_NAME(MemoryStatsData);
	//red::memory::RuntimeMetrics
	RTTI_PROPERTY(m_engineTick);
	RTTI_PROPERTY(totalPhysicalMemory).setName("totalPhysicalMemory");
	RTTI_PROPERTY(availablePhysicalMemory).setName("availablePhysicalMemory");
	RTTI_PROPERTY(cpuBytesAllocated).setName("cpuBytesAllocated");
	RTTI_PROPERTY(gpuBytesAllocated).setName("gpuBytesAllocated");
	RTTI_PROPERTY(cpuAllocationCount).setName("cpuAllocationCount");
	RTTI_PROPERTY(gpuAllocationCount).setName("gpuAllocationCount");
	//job::RuntimeIOMemoryMetrics
	RTTI_PROPERTY(m_totalBytesAllocated);
	RTTI_PROPERTY(m_peakTotalBytesAllocated);
	RTTI_PROPERTY(m_maxRequestBytes);
	RTTI_PROPERTY(m_systemMemoryConsumed);
	RTTI_PROPERTY(m_numberGuardBytes);
	RTTI_PROPERTY(m_lastTimeDelta);
RTTI_END_TYPE()


namespace tools
{
	namespace interopHelper
	{
		static String FormatBytes( Uint64 numBytes )
		{
			const Uint64 KB = 1024;
			const Uint64 MB = KB * 1024;
			const Uint64 GB = MB * 1024;
			if ( numBytes >= GB )
				return String::Printf( "%.2f GB", numBytes / Float(GB) );
			else if ( numBytes >= MB )
				return String::Printf( "%.2f MB", numBytes / Float(MB) );
			else if ( numBytes >= KB )
				return String::Printf( "%.2f KB", numBytes / Float(KB) );
			return String::Printf( "%llu B", numBytes );
		}

		MemoryStatsData::MemoryStatsData()
			: ISerializable()
			, red::memory::RuntimeMetrics()
			, m_totalBytesAllocated( 0 )
			, m_peakTotalBytesAllocated(0)
			, m_maxRequestBytes(0)
			, m_systemMemoryConsumed(0)
			, m_numberGuardBytes(0)
			, m_engineTick( 0 )
			, m_lastTimeDelta( 0.f )
		{	
		}

		MemoryStats::MemoryStats()
			: m_notifierEnabled( true )
			, m_fullNotifierEnabled( false )
			, m_cachedAvailableMemory( 0 )
			, m_cachedGpuMemory( 0 )
			, m_cachedCpuMemory( 0 )
		{
		}

		void MemoryStats::Update( Uint64 _engineTick )
		{
			// #tbd: finer grain notifications
			if ( m_notifierEnabled || m_fullNotifierEnabled)
			{
				red::memory::GetRuntimeMetrics( m_data );
				//job::GetIOMemoryRuntimeMetrics( m_data );
				if (m_fullNotifierEnabled)
				{
					m_data.m_lastTimeDelta = GEngine->GetLastTimeDelta();
					m_data.m_engineTick = _engineTick;
				}

				if (m_notifierEnabled)
				{
					if (m_data.availablePhysicalMemory != m_cachedAvailableMemory ||
						m_data.gpuBytesAllocated != m_cachedGpuMemory ||
						m_data.cpuBytesAllocated != m_cachedCpuMemory)
					{
						m_cachedAvailableMemory = m_data.availablePhysicalMemory;
						m_cachedGpuMemory = m_data.gpuBytesAllocated;
						m_cachedCpuMemory = m_data.cpuBytesAllocated;

						m_notifier.Notify();
					}
				}

				if (m_fullNotifierEnabled)
				{
					m_notifier.Notify(m_data);
				}
			}
		}

		void MemoryStats::SetupInterop( MemoryStats* ptr, ::interop::utils::Object& op )
		{
			op.RegisterValue( "_enabled", &ptr->m_notifierEnabled, &ptr->m_notifier );

			op.RegisterFunctor<Uint64, Bool, Bool>("enable_notifier", [ptr](interop::IContext& ctx, const Bool& _enableNormal, const Bool& _enableFull)
			{
				ptr->m_notifierEnabled = _enableNormal;
				ptr->m_fullNotifierEnabled = _enableFull;
				return GEngine->GetCurrentEngineTick();
			});


			op.RegisterNotifier( "_onChanged",  ptr->m_notifier );

			// cpu/gpu
			op.RegisterValueRO( "gpu_total", ptr->m_data.gpuBytesAllocated );
			op.RegisterValueRO( "cpu_total", ptr->m_data.cpuBytesAllocated );
			op.RegisterValueRO( "available", ptr->m_data.availablePhysicalMemory );

			// io
			op.RegisterValueRO( "io_total", ptr->m_data.m_totalBytesAllocated );
			op.RegisterValueRO( "io_peak",	ptr->m_data.m_peakTotalBytesAllocated );
			op.RegisterValueRO( "io_max",	ptr->m_data.m_maxRequestBytes );
			op.RegisterValueRO( "io_sys",	ptr->m_data.m_systemMemoryConsumed );
			op.RegisterValueRO( "io_guard", ptr->m_data.m_numberGuardBytes );

			op.RegisterValueRO< String >( "statsInfo", [ptr]() { 
				return String::Printf( "available: %.2f, CPU: %.2f, GPU: %.2f",
					ptr->m_data.availablePhysicalMemory / ( 1024.f * 1024.f ),
					ptr->m_data.cpuBytesAllocated / ( 1024.f * 1024.f ),
					ptr->m_data.gpuBytesAllocated / ( 1024.f * 1024.f ) );
			});
		}

		///---

		TimeStats::TimeStats()
			: m_notifierEnabled( true )
			, m_cachedFps( 0 )
			, m_cachedMinFps( 0 )
		{}

		void TimeStats::Update(Uint64 _engineTick)
		{
			// #tbd: finer grain notifications
			if ( m_notifierEnabled )
			{
				Int32 currFps = (Int32)GEngine->GetLastTickRate();
				Int32 currMinFps = (Int32)GEngine->GetMinTickRate();

				if ( currFps != m_cachedFps || currMinFps != m_cachedMinFps )
				{
					m_cachedFps = currFps;
					m_cachedMinFps = currMinFps;
					m_notifier.Notify();
				}
			}
		}

		void TimeStats::SetupInterop( TimeStats* ptr, ::interop::utils::Object& op )
		{
			op.RegisterValue( "_enabled", &ptr->m_notifierEnabled, &ptr->m_notifier );
			op.RegisterNotifier( "_onChanged",  ptr->m_notifier );

			op.RegisterValueRO<Int32>( "fps", [ptr]() { return ptr->m_cachedFps; } );
			op.RegisterValueRO<Int32>( "fps_min", [ptr]() { return ptr->m_cachedMinFps; } );
		}


#ifdef USE_PROFILER
		///--- Profiler
		
		ProfilerStats::ProfilerStats()
		{}

		void ProfilerStats::SetupInterop( ProfilerStats* ptr, ::interop::utils::Object& op )
		{
			op.RegisterValueRO<Bool>( "init", [ptr]() { return ptr->m_init; } );
			op.RegisterValueRO<Bool>( "running", [ptr]() { return ptr->m_running; } );
			op.RegisterValueRO<Float>( "bufferFilled", [ptr]() { return ptr->m_bufferFilled; } );
			op.RegisterValueRO<Uint64>( "pid", [ptr]() { return red::ProfilerControlCenter::GetProfilerPID(); } );

			op.RegisterValueRO< String >( "engineinfo", [ptr]()
			{
				return String::Printf( "Init: %u Running: %u Buffer: %0.1f%%", PROFILER_IsInitialized(), PROFILER_IsRecording(), PROFILER_GetBufferUsage( red::ProfilerToolSlot::PTS_REDPROFILER )*100.0f );
			} );
		}
#endif

	} // interopHelper

	///---

	EngineStatsBackend::EngineStatsBackend()
	{}

	EngineStatsBackend::~EngineStatsBackend()
	{}

	void EngineStatsBackend::Update()
	{
		Uint64 engineTick = GEngine->GetCurrentEngineTick();
		m_memoryStats.Update( engineTick );
		m_timeStats.Update( engineTick );
	}

	void EngineStatsBackend::SetupInterop( EngineStatsBackend* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "memory",
			[ptr](interop::IContext& ctx)
		{
			return ptr->m_memoryStats.CreateInterop();
		} );

		op.RegisterObject( "time",
			[ptr](interop::IContext& ctx)
		{
			return ptr->m_timeStats.CreateInterop();
		} );

#ifdef USE_PROFILER
		op.RegisterObject( "profiler",
			[ptr](interop::IContext& ctx)
		{
			return ptr->m_profilerStats.CreateInterop();
		} );
#endif
	}
}
