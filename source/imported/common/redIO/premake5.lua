---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redIO

-- files redIO

local redIO_files = {
	[""] = {
		"include/redIOApi.h",
		"include/redIOAsyncIO.h",
		"include/redIOStats.h",
		"include/redIOAsyncReadToken.h",
		"include/redIOCommon.h",
		"include/redIOFile.h",
		"include/redIOPublic.h",
		"include/redIOAsyncFileHandleCache.h",
		"include/redIOProfilerInterface.h",
		"include/redIOSystemFileWinAPI.h",
		"include/redIOSystemFileLinuxAPI.h",
		"include/redIOSystemFileOrbisAPI.h",
		"src/redIOAsyncReadToken.cpp",
		"src/redIO.natvis",
	},
	["internal"] = {
		"src/build.h",
		"src/build.cpp",
		"src/redIOInternal.h",
		"src/redIO.cpp",
		"src/redIOAsyncOp.h",
		"src/redIOAsyncFileHandleCache.cpp",
		"src/redIOAsyncIO.cpp",
		"src/redIOWorkerGeneric.h",
		"src/redIOWorkerGeneric.cpp",
		"src/redIOWorkerOrbis.h",
		"src/redIOWorkerOrbis.cpp",
		"src/redIOWorkerWin32.h",
		"src/redIOWorkerWin32.cpp",
		"src/redIOFile.cpp",
		"src/redIOSystemFileOrbisAPI.cpp",
		"src/redIOSystemFileWinAPI.cpp",
		"src/redIOSystemFileLinuxAPI.cpp",
		"src/redIOProfilerInterface.cpp",
		"src/redIOSettings.h",
		"src/redIONullProfiler.h",
		"src/redIOMemory.h",
		"src/redIOMemory.cpp",
		"src/redIOProfilerOrbis.cpp",
		"src/redIOProfilerOrbis.h",
		"src/redIOProfilerVisitor.cpp",
		"src/redIOProfilerVisitor.h",
		"src/redIOTraceWriterOrbis.cpp",
		"src/redIOTraceWriterOrbis.h",
		"src/serializationMemoryAllocator.cpp",
		"src/serializationMemoryAllocator.h",
	}
}
local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redIO"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redIO"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	defines { "RED_MODULE_redIO", "RED_EXPORT_redIO" }
	links { "redContainers", "redMemory", "redSystem" }
	dependson { "redContainers", "redMemory", "redSystem" }

	vpaths(redIO_files)
	files(flatten_file_groups(redIO_files))

	filter "system:windows"
		excludes {
			"src/redIOProfilerOrbis.cpp",
			"src/redIOSystemFileLinuxAPI.cpp",
			"src/redIOSystemFileOrbisAPI.cpp",
			"src/redIOTraceWriterOrbis.cpp",
			"src/redIOWorkerOrbis.cpp"
		}

	filter "system:linux"
		excludes {
			"src/redIOProfilerOrbis.cpp",
			"src/redIOSystemFileOrbisAPI.cpp",
			"src/redIOSystemFileWinAPI.cpp",
			"src/redIOTraceWriterOrbis.cpp",
			"src/redIOWorkerOrbis.cpp",
			"src/redIOWorkerWin32.cpp"
		}

	filter {}

