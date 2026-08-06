/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_VAULT_H_
#define _RED_MEMORY_VAULT_H_

#include "defaultAllocator.h"
#include "poolRegistry.h"
#include "metricsRegistry.h"
#include "threadMonitor.h"
#include "systemOOMHandlerBreak.h"
#include "oomHandlerBreak.h"
#include "oomHandlerController.h"
#include "reporter.h"
#include "hookHandler.h"
#include "systemAllocatorType.h"
#include "systemPageAllocatorType.h"
#include "metricsCapture.h"
#include "memoryAnalyzer.h"
#include "wwiseAllocator.h"
#include "icuAllocator.h"

#ifdef RED_PLATFORM_CONSOLE
	#include "consoleDebugAllocator.h"
#endif
#include "../include/metricsUtils.h"

namespace red
{
namespace memory
{
	// This is the static storage of memory system. 
	// To keep memory localize as much as possible, all needed class are stored here, and not randomly everywhere in static memory
	// It also glued everything together. This class is never referenced by any allocator or system.
	// It is never directly access by anything, and only access indirectly by client.
	class Vault 
	{
	public:

		Vault();
		~Vault();

		void Initialize();

		LocklessSlabAllocator & GetLocklessSlabAllocator();
		BigSizeAllocator& GetBigSizeAllocator();
		DefaultAllocator & GetDefaultAllocator();
		SystemAllocator & GetSystemAllocator();
		SystemAllocator & GetFlexibleAllocator();
		SystemPageAllocator & GetSystemPageAllocator();
		WwiseAllocator & GetWwiseAllocator();
		ICUAllocator & GetICUAllocator();
#ifdef RED_PLATFORM_CONSOLE
		ConsoleDebugAllocator & GetConsoleDebugAllocator();
#endif

		ThreadMonitor* GetThreadMonitor();

		void RegisterPool( PoolHandle handle, const PoolParameter & param );
		void SetPoolBudget( PoolHandle handle, const char* name, u64 budget );
		u64 GetPoolBudget( PoolHandle handle ) const;

		void RegisterAllocatorMetricsProcessor(
			PoolHandle poolHandle,
			ProxyTypeId allocatorId,
			void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
			void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) );

		u64 GetPoolTotalBytesAllocated( PoolHandle handle ) const;
		const char * GetPoolName( PoolHandle handle ) const;

		void DisableContributeToParentMetrics( PoolHandle handle );
		Bool GetContributeToParentMetrics( PoolHandle handle ) const;

		void SetMirroredPool( PoolHandle handle );

		bool IsPoolRegistered( PoolHandle handle ) const;
		u32 GetPoolCount() const;
		void VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle = PoolRoot::GetHandle() ) const;
		void ValidateAllPoolBudget();

		u64 GetTotalBytesAllocated() const;
		u64 GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren = true ) const;
		u32 GetTotalAllocationCount() const;
		void GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics );

		void AddAllocateMetric( PoolHandle handle, const Block & block );
		void AddFreeMetric( PoolHandle handle,const Block & block );
		void AddReallocateMetric( PoolHandle handle,const Block & input, const Block & output );
		void ResetMetrics( PoolHandle handle );
		void PrepareMetricsForNextFrame();

		HookHandle CreateHook( const HookCreationParameter & param  );
		void RemoveHook( HookHandle handle );
		void ProcessPreHooks( HookPreParameter & param, u32 disabledHooks );
		void ProcessPostHooks( HookPostParameter & param, u32 disabledHooks );
		void HandlePoolOOM( PoolHandle handle, ProxyTypeId allocatorId, u32 size, u32 alignment );
		void HandleProxyOOM( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment );
		Bool IsHandlingOOM() const;
		OOMHandlerController& AcquireOOMHandlerController();
		SystemOOMHandler& AcquireSystemOOMHandler();

		void LogMemoryReport();
		void LogMemoryReportToTxt();
		void LogMemoryReportToJson();
		void LogMemoryReportToTxt_CustomFile( const char* filePath );
		void SetAutoReportInterval( Float autoReportInterval );
		void SetAutoReportFormat( const char* format );
		void SetDumpRTTIToJsonFunction( DumpRTTIMetricsToJson dumpRTTIMetricsToJson );
		PoolRegistry& AcquirePoolRegistry();

		void StartMemoryCapture( const char * filename );
		void StartMemoryCapture( u32 bufferSize, OutOfProfilerMemoryCallback outOfProfilerMemoryCallback = []{} );
		void StopMemoryCapture();
		void WriteCurrentFrameTick();
		void ResetMemoryCapture();
		red::memory::Stream& GetMemoryCaptureStream();

		void SerializeAllocatorMetrics( Serializer & serializer, Deserializer & deserializer, const char * poolName );
		void SerializePoolMetrics( Serializer & serializer );

		void TryDoAutoMemReport( Float dt );
	private:

		Vault( const Vault & );
		const Vault & operator=( const Vault & );

		DefaultAllocator m_defaultAllocator;
#ifdef RED_PLATFORM_CONSOLE
		ConsoleDebugAllocator m_consoleDebugAllocator;
#endif

		HookHandler m_hookHandler;
		PoolRegistry m_poolRegistry;
		MetricsRegistry m_metricsRegistry;

		PlatformSystemAllocator m_systemAllocator;
		PlatformFlexibleAllocator m_flexibleAllocator;
		PlatformSystemPageAllocator m_pageAllocator;
		WwiseAllocator m_wwiseAllocator;
		ICUAllocator m_icuAllocator;

		ThreadMonitor m_threadMonitor;
		Reporter m_reporter;
		OOMHandlerController m_oomHandlerController;
		SystemOOMHandlerBreak m_defaultSystemOOMHandler;
		PoolOOMHandlerBreak m_defaultPoolOOMHandler;

		MetricsCapture m_metricsCapture;
		MemoryAnalyzer m_memoryAnalyzer;

		enum class DumpFormat
		{
			TXT,
			JSON
		};

		Float m_autoDumpInterval = 0.f;
		Float m_timeToNextAutoReport = 0.f;
		DumpFormat m_autoDumpFormat = DumpFormat::TXT;
public:
	void Debug_SetPlayTime(Float time);
	void Debug_SetTrackedQuest(const char* questName);
	void Debug_SetCmdLine(const char* cmdLine);

	void Debug_SetLocation(const char* location);
	};

	Vault & AcquireVault();
}
}

#endif
