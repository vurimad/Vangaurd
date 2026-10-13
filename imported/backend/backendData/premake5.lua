---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals backendCooking

backendData_location = '../../../temp/' .. _ACTION .. '/backend/backendData'

-- files backendCooking

backendData_files = {
	[""] = {
		"include/backendDataApi.h",
		"include/backendDataPublic.h",
		"src/backendDataInit.cpp",
		"src/build.cpp",
		"src/build.h",
	},
	["General"] = {
		"include/dataContext.h",
		"include/dataErrorHandler.h",
		"include/dataErrorHandlerImpl.h",
		"include/dataGlobalCacheReference.h",
		"include/dataPlatform.h",
		"include/dataResourceDepotWrapper.h",
		"include/dataResult.h",
		"include/dataSystemContext.h",
		"include/dataSystemInstance.h",
		"include/dataTask.h",
		"include/dataTaskContext.h",
		"include/dataTaskMetrics.h",
		"include/dataTaskReporter.h",
		"include/dataTaskSystem.h",
		"include/dataTypes.h",
		"include/dataTypesImpl.h",
		"include/memoryReportHelper.h",
		"src/dataContext.cpp",
		"src/dataErrorHandler.cpp",
		"src/dataErrorHandlerImpl.cpp",
		"src/dataGlobalCacheReference.cpp",
		"src/dataPlatform.cpp",
		"src/dataSystemContext.cpp",
		"src/dataSystemInstance.cpp",
		"src/dataTask.cpp",
		"src/dataTaskContext.cpp",
		"src/dataTaskLog.h",
		"src/dataTaskMetrics.cpp",
		"src/dataTaskReporter.cpp",
		"src/dataTaskSystem.cpp",
		"src/dataTypes.cpp",
		"src/memoryReportHelper.cpp",
	},
	["Data Cache"] = {
		"include/dataCache.h",
		"include/dataCacheServer.h",
		"src/dataCache.cpp",
		"src/dataCacheDebugServer.cpp",
		"src/dataCacheDebugServer.h",
		"src/dataCacheWebServiceDepsServer.cpp",
		"src/dataCacheWebServiceDepsServer.h",
		"src/dataCacheHttpFileServer.cpp",
		"src/dataCacheHttpFileServer.h",
		"src/dataCachePostgresDepsServer.cpp",
		"src/dataCachePostgresDepsServer.h",
		"src/dataCacheServer.cpp",
		"src/dataCacheWindowsFileServer.cpp",
		"src/dataCacheWindowsFileServer.h",
	},
	["Cooker"] = {
		"include/cooker.h",
		"include/cookerTask.h",
		"src/cooker.cpp",
		"src/cookerTask.cpp",
	},
	["Dependency"] = {
		"include/dependencyBinaryDatabase.h",
		"include/dependencyCache.h",
		"include/dependencyCollector.h",
		"include/dependencyDatabase.h",
		"include/dependencyHash.h",
		"include/dependencyLooseFileDatabase.h",
		"include/dependencyOutputCollector.h",
		"include/dependencyOutputDepotWrapper.h",
		"include/dependencyReader.h",
		"include/dependencyScanner.h",
		"include/dependencyScannerImpl.h",
		"include/dependencyTypes.h",
		"include/dependencyTypesImpl.h",
		"include/dependencyUtils.h",
		"include/dependencyWriter.h",
		"include/directoryDependency.h",
		"include/resourceDependency.h",
		"src/dependencyBinaryDatabase.cpp",
		"src/dependencyBinaryFileFormat.hpp",
		"src/dependencyBinaryReader.cpp",
		"src/dependencyBinaryReader.h",
		"src/dependencyBinaryWriter.cpp",
		"src/dependencyBinaryWriter.h",
		"src/dependencyCache.cpp",
		"src/dependencyCollector.cpp",
		"src/dependencyDatabase.cpp",
		"src/dependencyHash.cpp",
		"src/dependencyJsonReader.cpp",
		"src/dependencyJsonReader.h",
		"src/dependencyJsonWriter.cpp",
		"src/dependencyJsonWriter.h",
		"src/dependencyLooseFileDatabase.cpp",
		"src/dependencyOutputCollector.cpp",
		"src/dependencyOutputDepotWrapper.cpp",
		"src/dependencyScanner.cpp",
		"src/dependencyScannerImpl.cpp",
		"src/dependencyTypes.cpp",
		"src/dependencyTypesImpl.cpp",
		"src/dependencyUtils.cpp",
		"src/directoryDependency.cpp",
		"src/resourceDependency.cpp",
	},
	["Generator"] = {
		"include/generator.h",
		"include/generatorTask.h",
		"src/generator.cpp",
		"src/generatorTask.cpp",
	},
	["Commandlet"] = {
		"include/commandletTaskReporter.h",
		"include/dataCommandletSettings.h",
		"include/dataWorkerTaskAccumulator.h",
		"src/commandletTaskReporter.cpp",
		"src/cookCommandlet.cpp",
		"src/dataCommandletSettings.cpp",
		"src/dataWorkerTaskAccumulator.cpp",
		"src/debugDependencyCommandlets.cpp",
		"src/dumpDependencyCacheCommandlet.cpp",
		"src/dumpDependencyDatabaseCommandlet.cpp",
		"src/dumpDirectoryListingCache.cpp",
		"src/extractInplaceResourceCommandlet.cpp",
		"src/generatorCommandlet.cpp",
	},
	["Links"] = {
		"include/fileLinkMap.h",
		"include/linkResolver.h",
		"src/fileLinkMap.cpp",
		"src/linkResolver.cpp",	
		"src/backendLinks.natvis",
	},
	["Resource Database"] = {
		"include/resourceDatabase.h",
		"src/fileSystemFilter.cpp",
		"src/fileSystemFilter.h",
		"src/resourceDatabase.cpp",
	},
	["PostgreSQL"] = {
		"include/postgresWrapper.h",
		"src/postgresWrapper.cpp",
	},
}


-- configs backendWorld

project "backendData"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(backendData_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs {
		"include",
		"../../../external/curl/include",
	}
		
	uses { 
		"backendEngine",
		"postgresql",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(backendData_files)

	filter "platforms:Durango or ORBIS"
		flags { "ExcludeFromBuild" }
	filter "system:Windows"
		files(seq(backendData_files))
