---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redMemory

redMemory_location = '../../../temp/' .. _ACTION .. '/common/redMemory'

-- files redMemory

redMemory_files = {
	["src/utility"] = {
		"src/intrusiveList.h",
		"src/intrusiveList.hpp",
		"src/macroUtils.h",
		"src/assert.h",
		"src/proxy.h",
		"src/callstack.h",
		"src/objectUtils.h",
		"src/objectUtils.hpp",
		"src/intrusiveList.cpp",
		"src/utils.cpp",
		"src/callstack.cpp",
		"src/callstackCollectorConstant.h",
		"src/callstackCollector.h",
		"src/callstackCollectorWin.h",
		"src/callstackCollectorWin.cpp",
		"src/callstackCollectorLinux.h",
		"src/callstackCollectorLinux.cpp",
		"src/callstackCollectorOrbis.h",
		"src/callstackCollectorOrbis.cpp" },
	["src/allocators/system/platform"] = {
		"src/systemAllocatorDurango.h",
		"src/systemAllocatorOrbis.h",
		"src/systemAllocatorWin.h",
		"src/systemAllocatorLinux.h",
		"src/flexibleSystemAllocator.h",
		"src/systemPageAllocatorWin.h",
		"src/systemPageAllocatorLinux.h",
		"src/systemPageAllocatorDurango.h",
		"src/systemPageAllocatorOrbis.h",
		"src/systemAllocatorDurango.cpp",
		"src/systemAllocatorOrbis.cpp",
		"src/systemAllocatorWin.cpp",
		"src/systemAllocatorLinux.cpp",
		"src/flexibleSystemAllocator.cpp",
		"src/systemPageAllocatorWin.cpp",
		"src/systemPageAllocatorLinux.cpp",
		"src/systemPageAllocatorDurango.cpp",
		"src/systemPageAllocatorOrbis.cpp" },
	["src/allocators/TLSF"] = {
		"src/dynamicTlsfAllocator.h",
		"src/staticTlsfAllocator.h",
		"src/lockingDynamicTlsfAllocator.h",
		"src/dynamicTlsfAllocator.cpp",
		"src/staticTlsfAllocator.cpp",
		"src/lockingDynamicTlsfAllocator.cpp" },
	["src/allocators/TLSF/private"] = {
		"src/tlsfAllocator.h",
		"src/tlsfBlock.h",
		"src/tlsfConstant.h",
		"src/tlsfBlock.hpp",
		"src/tlsfAllocator.cpp",
		"src/tlsfBlock.cpp" },
	["src/allocators/SLAB/private"] = {
		"src/slabAllocator.h",
		"src/slabConstant.h",
		"src/slabHeader.h",
		"src/slabChunk.h",
		"src/slabList.h",
		"src/slabChunkAllocatorInterface.h",
		"src/slabMetrics.h",
		"src/slabHeader.hpp",
		"src/slabAllocator.cpp",
		"src/slabChunk.cpp",
		"src/slabHeader.cpp" },
	["src/allocators/SLAB"] = {
		"src/locklessSlabAllocator.h",
		"src/locklessSlabAllocator.hpp",
		"src/locklessSlabAllocator.cpp" },
	["src/allocators/stack"] = {
		"src/staticStackAllocator.h",
		"src/dynamicStackAllocator.h",
		"src/staticStackAllocator.cpp",
		"src/dynamicStackAllocator.cpp" },
	["src/allocators/stack/private"] = {
		"src/stackAllocator.h",
		"src/stackAllocator.cpp" },
	["src/allocators/system"] = {
		"src/systemPageAllocator.h",
		"src/systemAllocatorType.h",
		"src/systemPageAllocatorType.h",
		"src/systemAllocator.cpp",
		"src/systemBlock.cpp",
		"src/virtualRange.cpp",
		"src/systemPageAllocator.cpp",
		"src/systemAllocatorType.cpp",
		"src/systemPageAllocatorType.cpp" },
	["src/allocators/default"] = {
		"src/defaultAllocator.h",
		"src/defaultAllocator.cpp" },
	["src/allocators/linear"] = {
		"src/linearAllocator.h",
		"src/linearAllocator.cpp",
		"src/locklessStaticLinearAllocator.h",
		"src/locklessStaticLinearAllocator.cpp",
		"src/dynamicLinearAllocator.h",
		"src/dynamicLinearAllocator.cpp",
		"src/unsafeDynamicLinearAllocator.h",
		"src/unsafeDynamicLinearAllocator.cpp",
	},
	["src/allocators/circular"] = {
		"src/circularAllocator.h",
		"src/circularAllocator.cpp" },
	["src/allocators/bigSize"] = {
		"src/bigSizeAllocator.h",
		"src/bigSizeAllocator.cpp" },
	["src/allocators"] = {
		"src/allocator.h",
		"src/block.cpp" },
	["src/metrics"] = {
		"src/metricsRegistry.h",
		"src/allocatorMetrics.h",
		"src/metricsRegistry.hpp",
		"src/metricsUtils.h",
		"src/poolMetrics.h",
		"src/metricsCapture.h",
		"src/abstractMetricsCapture.h",
		"src/metricsCaptureWin.h",
		"src/metricsCaptureLinux.h",
		"src/metricsCaptureOrbis.h",
		"src/metricsCaptureDurango.h",
		"src/metricsRegistry.cpp",
		"src/allocatorMetrics.cpp",
		"src/metricsUtils.cpp",
		"src/poolMetrics.cpp",
		"src/abstractMetricsCapture.cpp",
		"src/metricsCaptureWin.cpp",
		"src/metricsCaptureLinux.cpp",
		"src/metricsCaptureOrbis.cpp",
		"src/metricsCaptureDurango.cpp",
		"src/allocatorMetricsSerializerRegistry.h",
		"src/allocatorMetricsSerializerRegistry.cpp",
		"src/allocatorMetricsLogger.cpp",
		"src/memoryAnalyzer.h",
		"src/memoryAnalyzerOrbis.h",
		"src/memoryAnalyzerOrbis.cpp",
		"src/memoryAnalyzerUtils.cpp" },
	["src/utility/threads"] = {
		"src/spinLock.h",
		"src/spinLock.cpp",
		"src/mutex.h",
		"src/scopedLock.hpp",
		"src/scopedLock.h",
		"src/threadIdProvider.cpp",
		"src/threadMonitor.cpp",
		"src/mutex.cpp" },
	["src/utility/hooks"] = {
		"src/hook.h",
		"src/hookHandler.h",
		"src/hookPool.h",
		"src/hookHandler.hpp",
		"src/hookMarkBlock.h",
		"src/hookUtils.h",
		"src/hookUtils.hpp",
		"src/hookOverrunDetector.h",
		"src/hookPoolValidator.h",
		"src/hook.cpp",
		"src/hookHandler.cpp",
		"src/hookPool.cpp",
		"src/hookTypes.cpp",
		"src/hookMarkBlock.cpp",
		"src/hookUtils.cpp",
		"src/hookOverrunDetector.cpp",
		"src/hookPoolValidator.cpp" },
	["src/allocators/fixedSize"] = {
		"src/locklessStaticFixedSizeAllocator.h",
		"src/dynamicFixedSizeAllocator.h",
		"src/locklessStaticFixedSizeAllocator.cpp",
		"src/dynamicFixedSizeAllocator.cpp" },
	["src/pool"] = {
		"src/pool.h",
		"src/poolRegistry.h",
		"src/poolConstant.h",
		"src/pool.hpp",
		"src/poolDeclaration.h",
		"src/poolStorage.h",
		"src/poolStorage.hpp",
		"src/poolRegistry.cpp",
		"src/poolUtils.cpp",
		"src/pool.cpp",
		"src/poolRoot.cpp" },
	["src/vault"] = {
		"src/vault.h",
		"src/vault.cpp" },
	["src/allocators/fixedSize/private"] = {
		"src/locklessFixedSizeAllocator.h",
		"src/locklessFixedSizeAllocator.cpp" },
	["src/utility/oom"] = {
		"src/systemOOMHandler.cpp",
		"src/systemOOMHandlerBreak.cpp",
		"src/oomHandler.cpp",
		"src/oomHandlerBreak.cpp",
		"src/oomHandlerIgnore.cpp",
		"src/oomHandlerController.h",
		"src/oomHandlerController.cpp" },
	["src/allocators/debug"] = {
		"src/debugAllocator.h",
		"src/debugAllocatorConstant.h",
		"src/debugAllocator.cpp",
		"src/consoleDebugAllocator.h",
		"src/consoleDebugAllocator.cpp"},
	["src/operators"] = {
		"src/functions.h",
		"src/functions.hpp",
		"include/operatorsLegacy.h",
		"src/operatorsLegacy.cpp" },
	["src/allocators/system/platform/private"] = {
		"src/systemAllocatorOrbisHelper.h",
		"src/systemAllocatorOrbisHelper.cpp" },
	["src/allocators/gpu"] = {
		"src/legacyGpuAllocator.h",
		"src/legacyGpuAllocatorRegion.h",
		"src/legacyGpuAllocator.cpp",
		"src/gpuAllocator.h",
		"src/gpuAllocator.cpp",
		"src/gpuAllocatorConstant.h" },
	["src/allocators/gpu/platform"] = {
		"src/gpuAllocatorOrbis.h",
		"src/gpuAllocatorOrbis.cpp"
	},
	["src/utility/unitTest"] = {
		"src/poolUnitTest.h",
		"src/poolUnitTest.cpp" },
	["src/operators/private"] = {
		"src/operatorsInternal.h",
		"src/operatorsInternal.hpp" },
	["src/allocators/null"] = {
		"src/nullAllocator.h",
		"src/nullAllocator.cpp" },
	["src/allocators/frame"] = {
		"src/frameAllocatorUtils.h",
		"src/frameAllocatorUtils.cpp",
		"src/frameAllocator.h",
		"src/frameAllocator.cpp",
		"src/locklessFrameAllocator.h",
		"src/locklessFrameAllocator.cpp",
		"src/locklessDebugBanFrameAllocator.h",
		"src/locklessDebugBanFrameAllocator.cpp",
		"src/frameAllocatorWithFallback.h",
		"src/frameAllocatorWithFallback.cpp" },
	["src/allocators/buddy"] = {
		"src/buddyAllocator.h",
		"src/buddyAllocator.cpp",
		"src/buddyAllocatorUtils.h",
		"src/buddyAllocatorUtils.cpp",
		"src/staticBuddyAllocator.h",
		"src/staticBuddyAllocator.cpp",
		"src/dynamicBuddyAllocator.h",
		"src/dynamicBuddyAllocator.cpp",
		"src/lockingDynamicBuddyAllocator.h",
		"src/lockingDynamicBuddyAllocator.cpp"
	},
	["src/allocators/thirdparty"] = {
		"src/wwiseAllocator.cpp",
		"src/icuAllocator.cpp"
	},
	["include/smartObjects/private"] = {
		"include/uniquePtrStorage.h",
		"include/uniquePtrStorage.hpp",
		"include/nonAtomicSharedStorage.h",
		"include/nonAtomicSharedStorage.hpp",
		"include/atomicSharedStorage.h",
		"include/atomicSharedStorage.hpp",
		"include/intrusiveSharedStorage.h",
		"include/intrusiveSharedStorage.hpp",
		"include/sharedStorageAttorney.hpp",
		"include/sharedStorageAttorney.h",
		"include/weakStorage.h",
		"include/weakStorage.hpp",
		"include/sharedStorage.hpp",
		"include/sharedStorage.h" },
	["include"] = {
		"include/redMemoryApi.h",
		"include/types.h",
		"include/operators.h",
		"include/redMemoryPublic.h",
		"include/redMemoryInternal.h" },
	["src"] = {
		"src/build.h",
		"src/build.cpp",
		"src/documentation.cpp",
		"src/redMemoryInit.cpp" },
	["include/pool"] = {
		"include/poolTypes.h",
		"include/poolRoot.h",
		"include/pool.h",
		"include/gameMemoryPools.h" },
	["include/allocators"] = {
		"include/defaultAllocator.h",
		"include/gpuAllocator.h",
		"include/flags.h",
		"include/fixedSizeAllocator.h",
		"include/tlsfAllocator.h",
		"include/linearAllocator.h",
		"include/unsafeDynamicLinearAllocator.h",
		"include/stackAllocator.h",
		"include/systemAllocator.h",
		"include/virtualRange.h",
		"include/systemBlock.h",
		"include/virtualRange.hpp",
		"include/frameAllocator.h",
		"include/circularAllocator.h",
		"include/slabAllocator.h",
		"include/bigSizeAllocator.h",
		"include/wwiseAllocator.h",
		"include/buddyAllocator.h",
		"include/icuAllocator.h",
		},
	["include/utility"] = {
		"include/block.h",
		"include/block.hpp",
		"include/metricsUtils.h",
		"include/reportUtils.h",
		"include/utils.h",
		"include/utils.hpp",
		"include/poolUtils.h",
		"include/poolUtils.hpp",
		"include/metricsUtils.hpp",
		"include/proxyTypeId.h",
		"include/proxyTypeId.hpp",
		"include/memoryAnalyzerUtils.h",
		"include/simpleArray.h",
		"include/simpleArray.hpp" },
	["include/utility/serialization"] = {
		"include/stream.h",
		"include/streamUtils.h",
		"include/memoryStream.h",
		"include/serializer.h",
		"include/serializer.hpp",
		"include/deserializer.h",
		"include/deserializer.hpp" },
	["include/utility/threads"] = {
		"include/threadConstant.h",
		"include/threadIdProvider.h",
		"include/threadIdProvider.hpp",
		"include/threadMonitor.h"
	},
	["include/smartObjects"] = {
		"include/atomicSharedPtr.h",
		"include/atomicWeakPtr.h",
		"include/intrusivePtr.h",
		"include/sharedPtr.h",
		"include/uniqueBuffer.h",
		"include/uniquePtr.h",
		"include/weakPtr.h",
		"include/atomicSharedPtr.hpp",
		"include/sharedPtr.hpp",
		"include/uniqueBuffer.hpp",
		"include/uniquePtr.hpp",
		"include/sharedFromThis.h",
		"include/atomicSharedFromThis.h",
		"include/atomicSharedFromThis.hpp",
		"include/nonAtomicSharedPtr.h",
		"include/nonAtomicSharedPtr.hpp",
		"include/nonAtomicSharedFromThis.h",
		"include/nonAtomicSharedFromThis.hpp",
		"include/nonAtomicWeakPtr.h",
		"include/sharedPtrUtils.h",
		"include/sharedPtrUtils.hpp",
	},
	["include/function"] = {
		"include/function.h",
		"include/function.hpp"
	},
	["include/utility/oom"] = {
		"include/systemOOMHandler.h",
		"include/systemOOMHandlerBreak.h",
		"include/oomHandler.h",
		"include/oomHandlerBreak.h",
		"include/oomHandlerIgnore.h" },
	["include/utility/hooks"] = {
		"include/hookType.h",
		"include/hookTypes.h" },
	["include/settings"] = {
		"include/settings.h",
		"include/hookSettings.h" },
	["include/metrics"] = {
		"include/allocatorMetricsSerializer.h",
		"include/allocatorMetricsSerializer.hpp",
		"include/allocatorIdentifiersSerializer.h",
		"include/allocatorIdentifiersSerializer.hpp",
		"include/allocatorMetricsLogger.h" },
	["src/utility/serialization"] = {
		"src/deserializer.cpp",
		"src/serializer.cpp",
		"src/stream.cpp",
		"src/streamUtils.cpp",
		"src/memoryStream.h",
		"src/memoryStream.cpp" },
	["src/utility/serialization/platform"] = {
		"src/fileStreamWin.h",
		"src/fileStreamWin.cpp" },
	["src/utility/reporting"] = {
		"src/reporter.h",
		"src/notificationHandler.h",
		"src/reporter.cpp",
		"src/reportUtils.cpp",
		"src/notificationHandler.cpp" },
	["src/smartObjects"] = {
		"src/nonAtomicSharedStorage.cpp",
		"src/atomicSharedStorage.cpp",
		"src/uniqueBuffer.cpp" },
	["tools"] = {
		"src/setup_debug_allocator.bat",
		"src/red_memory.natvis",
		"src/red_memory.natstepfilter" }
	}

-- configs redMemory

project "redMemory"
	filter "platforms:not x64_DLL*"
		kind "StaticLib"
	filter "platforms:x64_DLL*"
		kind "SharedLib"
	filter {}

	filter "configurations:Debug"
		if _OPTIONS["debug-full"] then
			defines { "_SCL_SECURE_NO_WARNINGS" }
		end
	filter {}
	
	language "C++"
	location(redMemory_location)

	warnings "Extra"
	
	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	uses "redSystem" 

	vpaths(redMemory_files)

	files(seq(redMemory_files))

	excludes { "%{wks.location}/../../src/common/redMemory/src/DLL/operatorsLegacyDLL.cpp" }

	-- This will be compiled in each app project now instead
	filter { "action:vs2017", "kind:WindowedApp or ConsoleApp", "platforms:x64" }
		excludes { "%{wks.location}/../../src/common/redMemory/src/operatorsLegacy.cpp" }	
	
	filter "system:Windows"
		excludes {
			"src/flexibleSystemAllocator.cpp",
			"src/systemPageAllocatorDurango.cpp",
			"src/systemPageAllocatorOrbis.cpp",
			"src/systemPageAllocatorLinux.cpp",
			"src/systemAllocatorDurango.cpp",
			"src/systemAllocatorLinux.cpp",
			"src/systemAllocatorOrbis.cpp",
			"src/systemAllocatorOrbisHelper.cpp",
			"src/callstackCollectorOrbis.cpp",
			"src/callstackCollectorLinux.cpp",
			"src/metricsCaptureDurango.cpp",
			"src/metricsCaptureLinux.cpp",
			"src/metricsCaptureOrbis.cpp",
			"src/memoryAnalyzerOrbis.cpp"
		}

	filter "platforms:Durango"
		excludes {
			"src/debugAllocator.cpp",
			"src/fileStreamWin.cpp",
			"src/flexibleSystemAllocator.cpp",
			"src/systemPageAllocatorOrbis.cpp",
			"src/systemPageAllocatorWin.cpp",
			"src/systemPageAllocatorLinux.cpp",
			"src/systemAllocatorOrbis.cpp",
			"src/systemAllocatorOrbisHelper.cpp",
			"src/systemAllocatorWin.cpp",
			"src/systemAllocatorLinux.cpp",
			"src/callstackCollectorOrbis.cpp",
			"src/callstackCollectorLinux.cpp",
			"src/metricsCaptureWin.cpp",
			"src/metricsCaptureLinux.cpp",
			"src/metricsCaptureOrbis.cpp",
			"src/memoryAnalyzerOrbis.cpp"
		}

	filter "platforms:ORBIS"
		excludes {
			"src/debugAllocator.cpp",
			"src/fileStreamWin.cpp",
			"src/operatorsLegacy.cpp",
			"src/systemPageAllocatorDurango.cpp",
			"src/systemPageAllocatorWin.cpp",
			"src/systemPageAllocatorLinux.cpp",
			"src/systemAllocatorDurango.cpp",
			"src/systemAllocatorWin.cpp",
			"src/systemAllocatorLinux.cpp",
			"src/callstackCollectorWin.cpp",
			"src/callstackCollectorLinux.cpp",
			"src/metricsCaptureWin.cpp",
			"src/metricsCaptureLinux.cpp",
			"src/metricsCaptureDurango.cpp"
		}
		
	filter "system:Linux"
		excludes {
			"src/flexibleSystemAllocator.cpp",
			"src/fileStreamWin.cpp",
			"src/operatorsLegacy.cpp",
			"src/systemPageAllocatorOrbis.cpp",
			"src/systemPageAllocatorDurango.cpp",
			"src/systemPageAllocatorWin.cpp",
			"src/systemAllocatorOrbis.cpp",
			"src/systemAllocatorOrbisHelper.cpp",
			"src/systemAllocatorDurango.cpp",
			"src/systemAllocatorWin.cpp",
			"src/callstackCollectorWin.cpp",
			"src/callstackCollectorOrbis.cpp",
			"src/metricsCaptureWin.cpp",
			"src/metricsCaptureDurango.cpp",
			"src/metricsCaptureOrbis.cpp",
			"src/memoryAnalyzerOrbis.cpp"
		}
		
usage "redMemory"
	filter "platforms:Durango"
		links "kernelx.lib"

-- copy *.natstepfilter (https://msdn.microsoft.com/en-us/library/dn457346.aspx) to Visual Studio 2015 user visualizers directory
if _ACTION == 'vs2015' and premake.vstudio.uservisdir ~= nil then
	copy_absolute(script_dir() .. 'src/red_memory.natstepfilter', premake.vstudio.uservisdir .. 'red_memory.natstepfilter')
end
