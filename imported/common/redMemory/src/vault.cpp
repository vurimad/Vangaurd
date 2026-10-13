/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "vault.h"
#include "systemAllocator.h"
#include "poolUtils.h"

namespace red
{
namespace memory
{
#ifdef RED_MEMORY_ENABLE_POOL_INITALIZATION_VALIDATION
	extern bool g_validatePoolInitialization;
#endif

	void InitializePermanentHooks( HookHandler & handler );

	Vault::Vault()
	{
	}

	Vault::~Vault()
	{
		m_poolRegistry.Uninitialize();
		m_threadMonitor.Uninitialize();
	}

	void Vault::Initialize()
	{
		ReporterParameter param = 
		{
			&m_poolRegistry,
			&m_metricsRegistry,
			&m_systemAllocator,
			&m_flexibleAllocator,
			nullptr
		};

		m_reporter.Initialize( param );
		m_defaultSystemOOMHandler.Initialize( &m_reporter );
		m_defaultPoolOOMHandler.Initialize( &m_reporter );

		m_pageAllocator.Initialize( &m_defaultSystemOOMHandler );
		m_systemAllocator.Initialize( &m_pageAllocator, &m_defaultSystemOOMHandler );
		m_flexibleAllocator.Initialize( &m_pageAllocator, &m_defaultSystemOOMHandler );

		DefaultAllocatorParameter defaultAllocParam =
		{
			&m_systemAllocator,
			&m_threadMonitor
		};

		m_defaultAllocator.Initialize( defaultAllocParam );
		m_defaultPoolOOMHandler.SetPoolRegistry( &m_poolRegistry );

		WwiseAllocatorParameter wwiseAllocParam =
		{
			&m_systemAllocator,
			&m_defaultAllocator
		};

		m_wwiseAllocator.Initialize( wwiseAllocParam );

		ICUAllocatorParameter icuAllocParam =
		{
			&m_systemAllocator,
			&m_defaultAllocator
		};

#ifdef RED_PLATFORM_CONSOLE
		m_consoleDebugAllocator.Initialize( &m_defaultAllocator, &m_systemAllocator );
#endif

		m_icuAllocator.Initialize( icuAllocParam );

		m_hookHandler.Initialize();

		m_poolRegistry.Initialize( &m_systemAllocator );
		m_metricsCapture.Initialize( &m_hookHandler, &m_poolRegistry );
		m_memoryAnalyzer.Initialize( &m_hookHandler );

		InitializePermanentHooks( m_hookHandler );
	}

	LocklessSlabAllocator & Vault::GetLocklessSlabAllocator()
	{
		// LocklessSlabAllocator is stored in DefaultAllocator to avoid cache misses while allocating small (<= 512 bytes) memory by DefaultAllocator.
		return m_defaultAllocator.InternalAcquireLocklessSlabAllocator();
	}

	BigSizeAllocator& Vault::GetBigSizeAllocator()
	{
		return m_defaultAllocator.InternalAcquireBigSizeAllocator();
	}

	DefaultAllocator & Vault::GetDefaultAllocator()
	{
		return m_defaultAllocator;
	}

	SystemAllocator & Vault::GetSystemAllocator()
	{
		return m_systemAllocator;
	}

	SystemAllocator & Vault::GetFlexibleAllocator()
	{
		return m_flexibleAllocator;
	}

	SystemPageAllocator & Vault::GetSystemPageAllocator()
	{
		return m_pageAllocator;
	}

	WwiseAllocator & Vault::GetWwiseAllocator()
	{
		return m_wwiseAllocator;
	}

	ICUAllocator & Vault::GetICUAllocator()
	{
		return m_icuAllocator;
	}

#ifdef RED_PLATFORM_CONSOLE
	red::memory::ConsoleDebugAllocator & Vault::GetConsoleDebugAllocator()
	{
		return m_consoleDebugAllocator;
	}
#endif

	ThreadMonitor* Vault::GetThreadMonitor()
	{
		return &m_threadMonitor;
	}

	void Vault::RegisterPool( PoolHandle handle, const PoolParameter & param )
	{
		RED_MEMORY_ASSERT( ::SIsMainThread(), "RegisterPool can only be called on the main thread" );

		m_poolRegistry.Register( handle, param );
		m_metricsRegistry.RegisterPoolMetrics( handle );
	}

	void Vault::SetPoolBudget( PoolHandle handle, const char* name, u64 budget )
	{
		m_poolRegistry.SetupPoolBudget( handle, name, budget );
	}

	u64 Vault::GetPoolBudget( PoolHandle handle ) const
	{
		return m_poolRegistry.GetPoolBudget( handle );
	}

	void Vault::RegisterAllocatorMetricsProcessor(
		PoolHandle poolHandle,
		ProxyTypeId allocatorId,
		void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
		void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) )
	{
		m_poolRegistry.RegisterAllocatorMetricsProcessor( poolHandle, allocatorId, metricsSerializer, metricsDeserializer );
	}

	u64 Vault::GetPoolTotalBytesAllocated( PoolHandle handle ) const
	{
		return m_metricsRegistry.GetPoolTotalBytesAllocated( handle );
	}

	const char * Vault::GetPoolName( PoolHandle handle ) const
	{
		return m_poolRegistry.GetPoolName( handle );
	}

	void Vault::DisableContributeToParentMetrics( PoolHandle handle )
	{
		m_poolRegistry.DisableContributeToParentMetrics( handle );
	}

	Bool Vault::GetContributeToParentMetrics( PoolHandle handle ) const
	{
		return m_poolRegistry.GetContributeToParentMetrics( handle );
	}

	void Vault::SetMirroredPool( PoolHandle handle )
	{
		m_poolRegistry.SetMirroredPool( handle );
		m_metricsRegistry.SetMirroredPool( handle );
	}

	bool Vault::IsPoolRegistered( PoolHandle handle ) const
	{
		return m_poolRegistry.IsPoolRegistered( handle );
	}

	u32 Vault::GetPoolCount() const
	{
		return m_poolRegistry.GetPoolCount();
	}

	u64 Vault::GetTotalBytesAllocated() const
	{
		return m_poolRegistry.GetTotalBytesAllocated();
	}

	u64 Vault::GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren ) const
	{
		return m_poolRegistry.GetTotalBytesAllocated( handle, includeChildren );
	}

	u32 Vault::GetTotalAllocationCount() const
	{
		return m_metricsRegistry.GetTotalAllocationCount();
	}

	void Vault::GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics )
	{
		m_metricsRegistry.GetRuntimePoolMetrics( handle, poolMetrics );
	}

	void Vault::AddAllocateMetric( PoolHandle handle, const Block & block )
	{
#ifdef RED_MEMORY_ENABLE_POOL_INITALIZATION_VALIDATION
		RED_ASSERT( !g_validatePoolInitialization || m_poolRegistry.GetPoolName( handle ), "Unnamed memory pool due to missing initialization. Call `RED_INITIALIZE_MEMORY_POOL( ... )` for the pool." );
#endif
		m_metricsRegistry.OnAllocate( handle, block );
	}

	void Vault::AddFreeMetric( PoolHandle handle,const Block & block )
	{
		m_metricsRegistry.OnDeallocate( handle, block );
	}
	
	void Vault::AddReallocateMetric( PoolHandle handle,const Block & input, const Block & output )
	{
		m_metricsRegistry.OnReallocate( handle, input, output );
	}

	void Vault::ResetMetrics( PoolHandle handle )
	{
		m_metricsRegistry.ResetMetrics( handle );
	}

	void Vault::PrepareMetricsForNextFrame()
	{
		m_metricsRegistry.PrepareMetricsForNextFrame();
	}

	void Vault::HandlePoolOOM( PoolHandle handle, ProxyTypeId allocatorId, u32 size, u32 alignment )
	{
		m_defaultPoolOOMHandler.HandlePoolAllocateFailure( handle, allocatorId, size, alignment );
	}

	void Vault::HandleProxyOOM( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment )
	{
		m_defaultSystemOOMHandler.HandleProxyAllocateFailure( proxyId, proxy, size, alignment );
	}

	Bool Vault::IsHandlingOOM() const
	{
		return m_defaultSystemOOMHandler.IsHandlingOOM() || m_defaultPoolOOMHandler.IsHandlingOOM();
	}

	OOMHandlerController& Vault::AcquireOOMHandlerController()
	{
		return m_oomHandlerController;
	}

	SystemOOMHandler& Vault::AcquireSystemOOMHandler()
	{
		return m_defaultSystemOOMHandler;
	}

	HookHandle Vault::CreateHook( const HookCreationParameter & param  )
	{
		return m_hookHandler.Create( param );
	}

	void Vault::RemoveHook( HookHandle handle )
	{
		m_hookHandler.Remove( handle );
	}

	void Vault::ProcessPreHooks( HookPreParameter & param, u32 disabledHooks )
	{
		m_hookHandler.ProcessPreHooks( param, disabledHooks );
	}

	void Vault::ProcessPostHooks( HookPostParameter & param, u32 disabledHooks )
	{
		m_hookHandler.ProcessPostHooks( param, disabledHooks );
	}

	void Vault::LogMemoryReport()
	{
		m_reporter.WriteReportToLog();
	}

	void Vault::LogMemoryReportToTxt()
	{
		m_reporter.WriteReportToTxt();
	}

	void Vault::LogMemoryReportToJson()
	{
		m_reporter.WriteReportToJson();
	}

	void Vault::LogMemoryReportToTxt_CustomFile( const char* filePath )
	{
		m_reporter.WriteReportToTxt_CustomFile( filePath );
	}

	void Vault::SetAutoReportInterval(Float autoReportInterval )
	{
		m_autoDumpInterval = autoReportInterval;
	}

	void Vault::SetAutoReportFormat(const char* format)
	{
		if( !red::StrcmpNC( format, "json" ) )
		{
			m_autoDumpFormat = DumpFormat::JSON;
		}
		else if( !red::StrcmpNC( format, "txt" ) )
		{
			m_autoDumpFormat = DumpFormat::TXT;
		}
		else
		{
			RED_LOG_WARNING( "memory::Vault: Failed to set auto report format, reason: unsupported format string '%s'", format );
		}
	}

	void Vault::SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIMetricsToJson )
	{
		m_reporter.SetDumpRTTIToJsonFunction( dumpRTTIMetricsToJson );
	}

	PoolRegistry& Vault::AcquirePoolRegistry()
	{
		return m_poolRegistry;
	}

	void Vault::StartMemoryCapture( const char * filename ) 
	{
		m_metricsCapture.Start( filename );
	}

	void Vault::StartMemoryCapture( u32 bufferSize, OutOfProfilerMemoryCallback outOfProfilerMemoryCallback ) 
	{
		m_metricsCapture.Start( bufferSize, std::move( outOfProfilerMemoryCallback ) );
	}

	void Vault::StopMemoryCapture()
	{
		m_metricsCapture.Stop();
	}

	void Vault::WriteCurrentFrameTick()
	{
		m_metricsCapture.WriteCurrentFrameTick();
	}

	void Vault::ResetMemoryCapture()
	{
		m_metricsCapture.Reset();
	}

	red::memory::Stream& Vault::GetMemoryCaptureStream()
	{
		return m_metricsCapture.GetStream();
	}

	void Vault::SerializeAllocatorMetrics( Serializer & serializer, Deserializer & deserializer, const char * poolName )
	{
		m_poolRegistry.SerializeAllocatorMetrics( serializer, deserializer, poolName );
	}

	void Vault::SerializePoolMetrics( Serializer & serializer )
	{
		m_poolRegistry.SerializePoolMetrics( serializer );
	}

	void Vault::TryDoAutoMemReport(Float dt)
	{
		if (m_autoDumpInterval > 0.f)
		{
			if (m_timeToNextAutoReport <= 0.f)
			{
				switch ( m_autoDumpFormat )
				{
				case DumpFormat::TXT:
					m_reporter.WriteReportToTxt();
					break;
				case DumpFormat::JSON:
					m_reporter.WriteReportToJson();
					break;
				}
				m_timeToNextAutoReport = m_autoDumpInterval;
			}
			else
			{
				m_timeToNextAutoReport -= dt;
			}
		}
	}

	void Vault::VisitPoolInfos(const PoolInfoVisitor& visitor, const PoolHandle poolHandle) const
	{
		m_poolRegistry.VisitPoolInfos( visitor, poolHandle );
	}

	void Vault::ValidateAllPoolBudget()
	{
		m_poolRegistry.ValidateAllPoolBudget();
	}

	void Vault::Debug_SetPlayTime(Float time)
	{
		m_reporter.SetPlayTime(time);
	}

	void Vault::Debug_SetTrackedQuest(const char* questName)
	{
		m_reporter.SetTrackedQuest(questName);
	}

	void Vault::Debug_SetCmdLine(const char* cmdLine)
	{
		m_reporter.SetCmdLine(cmdLine);
	}

	void Vault::Debug_SetLocation(const char* location)
	{
		m_reporter.SetLocation(location);
	}

	struct VaultProxy
	{
		VaultProxy()
		{
			instance.Initialize();
		}

		Vault instance;
	};

#ifdef RED_COMPILER_MSC
	RED_DISABLE_WARNING_MSC( 4074 )
#pragma init_seg( compiler )
#endif

	static VaultProxy s_vault RED_STATIC_PRIORITY( 101 );

	PoolStorage StaticPoolStorage< red::memory::PoolRoot >::storage =
	{
		MakeAllocatorStorage( &AcquireNullAllocator(), PoolRoot::GetHandle() ),
		0,
		0,
		nullptr,
		PoolRoot::GetHandle(),
		PoolRoot::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::memory::PoolCPU >::storage =
	{
		MakeAllocatorStorage( &AcquireNullAllocator(), PoolCPU::GetHandle() ),
		0,
		0,
		nullptr,
		PoolCPU::GetHandle(),
		PoolCPU::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::memory::PoolGPU >::storage =
	{
		MakeAllocatorStorage( &AcquireNullAllocator(), PoolGPU::GetHandle() ),
		0,
		0,
		nullptr,
		PoolGPU::GetHandle(),
		PoolGPU::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::memory::PoolFlexible >::storage =
	{
		MakeAllocatorStorage( &AcquireFlexibleSystemAllocator(), PoolFlexible::GetHandle() ),
		0,
		0,
		nullptr,
		PoolFlexible::GetHandle(),
		PoolFlexible::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::PoolDefault >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolDefault::GetHandle() ),
		0,
		0,
		nullptr,
		PoolDefault::GetHandle(),
		PoolDefault::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::PoolLegacyOperator >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolLegacyOperator::GetHandle() ),
		0,
		0,
		nullptr,
		PoolLegacyOperator::GetHandle(),
		PoolLegacyOperator::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::PoolEngine >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolEngine::GetHandle() ),
		0,
		0,
		nullptr,
		PoolEngine::GetHandle(),
		PoolEngine::AllocatorType::TypeId
	};

	PoolStorage StaticPoolStorage< red::PoolRefCount >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolRefCount::GetHandle() ),
		0,
		0,
		nullptr,
		PoolRefCount::GetHandle(),
		PoolRefCount::AllocatorType::TypeId
	};

#ifdef RED_PLATFORM_CONSOLE
	PoolStorage StaticPoolStorage< red::PoolDebug >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetConsoleDebugAllocator(), PoolDebug::GetHandle() ),
		0,
		0,
		nullptr,
		PoolDebug::GetHandle(),
		PoolDebug::AllocatorType::TypeId
	};

#else
	PoolStorage StaticPoolStorage< red::PoolDebug >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolDebug::GetHandle() ),
		0,
		0,
		nullptr,
		PoolDebug::GetHandle(),
		PoolDebug::AllocatorType::TypeId
	};
#endif

	PoolStorage StaticPoolStorage< red::PoolBackend >::storage =
	{
		MakeAllocatorStorage( &AcquireVault().GetDefaultAllocator(), PoolBackend::GetHandle() ),
		0,
		0,
		nullptr,
		PoolBackend::GetHandle(),
		PoolBackend::AllocatorType::TypeId
	};

	Vault & AcquireVault()
	{
		return s_vault.instance;
	}
}
}
