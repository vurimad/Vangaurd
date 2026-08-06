---
-- Copyright (C)2018 CD Projekt Red. All Rights Reserved.
---

-- files redJobs2

local redJobs2_files = {
	[""] = {
		"include/redJobs2Api.h",
		"include/redJobs2Public.h",
		"include/jobBuilder.h",
		"include/jobCounter.h",
		"include/jobCounterFunctions.h",
		"include/jobDecl.h",
		"include/jobPriority.h",
		"include/jobSystem.h",
		"include/jobDeferral.h",
		"include/jobRunner.h",
		"include/jobStackTrace.h",
		"include/jobCounterOwner.h",
		"include/jobDispatcherInitParam.h",
		"src/documentation.cpp",
	},
	["prv"] = {
		"src/build.h",
		"src/build.cpp",
		"src/redJobs2Internal.h",
		"src/jobBuilder.cpp",
		"src/jobCounterFunctions.cpp",
		"src/jobDispatcher.h",
		"src/jobDispatcher.cpp",
		"src/jobDispatcherEntries.h",
		"src/jobDispatcherThread.h",
		"src/jobDispatcherThread.cpp",
		"src/jobDispatcherQueue.h",
		"src/jobDispatcherQueue.hpp",
		"include/jobDispatcherRefCountMask.h",
		"src/jobDispatcherRefCountMask.cpp",
		"include/jobDispatcherCounterValue.h",
		"src/jobDispatcherCounterValue.cpp",
		"include/jobMemoryPools.h",
		"include/jobScopeMemoryAllocator.h",
		"src/jobScopeMemoryAllocator.cpp",
		"src/jobMemoryPools.cpp",
		"src/jobSystem.cpp",
		"src/jobDeferral.cpp",
		"src/jobLockFreeQueueMPMC.h",
		"src/redJobs2Internal.h",
		"src/jobDebugger.h",
		"src/jobDebugger.cpp",
		"src/jobDebuggerStackTraceCache.h",
		"src/jobDebuggerStackTraceCache.cpp",
		"src/jobDispatcherSemaphore.h",
		"src/jobDispatcherSemaphore.cpp",
		"src/jobStackTrace.cpp",
		"src/jobCounterOwner.cpp",
		"src/jobDispatcherInitParam.cpp",
		"include/jobRunner.inl",
		"include/jobShim.h",
		"src/redJobs2.natvis",
	},
}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redJobs2"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redJobs2"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	defines
	{
		"RED_MODULE_redJobs2",
		"RED_EXPORT_redJobs2"
	}

	links
	{
		"redCore",
		"redIO",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	dependson
	{
		"redCore",
		"redIO",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	vpaths(redJobs2_files)
	files(flatten_file_groups(redJobs2_files))

