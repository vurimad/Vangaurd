---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redCore

-- files redCore

local redCore_files = {
	[""] = {
		"include/absolutePath.h",
		"include/absolutePath.hpp",
		"include/commandline.h",
		"include/corePool.h",
		"include/crt.h",
		"include/globalModeInfo.h",
		"include/names.h",
		"include/names.hpp",
		"include/redCoreApi.h",
		"include/redCorePublic.h",
		"include/settings.h",
		"include/singleton.h",
		"include/worldGlobalNodeIDUtils.h",
		"include/worldNodeNameUtils.h",
		"src/absolutePath.cpp",
		"src/commandline.cpp",
		"src/corePool.cpp",
		"src/crt.cpp",
		"src/globalModeInfo.cpp",
		"src/redCoreInternal.h",
		"src/worldGlobalNodeIDUtils.cpp",
		"src/worldNodeNameUtils.cpp",
	},
	["Debug Server"] = {
		"src/debugServerHelpers.cpp",
		"src/debugServerInternalCommands.cpp",
		"src/debugServerInternalCommands.h",
		"src/debugServerInternalPlugin.cpp",
		"src/debugServerInternalPlugin.h",
		"src/debugServerManager.cpp",
		"src/debugServerPlugin.cpp",
		"include/debugServerHelpers.h",
		"include/debugServerManager.h",
		"include/debugServerPlugin.hpp",
		"include/debugServerPlugin.h",
	},
	["Profiler"] = {
		"src/profiler.cpp",
		"src/profilerManager.cpp",
		"src/profilerChannels.cpp",
		"src/redProfiler.cpp",
		"src/profilerFileWriter.cpp",
		"src/profilerBlockFileWriter.cpp",
		"src/instrumentationObject.cpp",
		"include/profiler.h",
		"include/profilerManager.h",
		"include/profilerTypes.h",
		"include/profilerConfiguration.h",
		"include/profilerChannels.h",
		"include/instrumentationObject.h",
		"include/profilerBlockFileWriter.h",
		"include/profilerFileWriter.h",
	},
	["Profiler/Tools"] = {
		"src/profilerToolRed.cpp",
		"src/profilerToolRedInGame.cpp",
		"src/profilerToolNvidia.cpp",
		"src/profilerToolTracy.cpp",
		"src/profilerToolPix.cpp",
		"src/profilerToolRazor.cpp",
		"src/profilerToolVTune.cpp",
		"include/profilerToolPix.h",
		"include/profilerToolRed.h",
		"include/profilerToolRedInGame.h",
		"include/profilerToolRedInGame.inl",
		"include/profilerToolNvidia.h",
		"include/profilerToolTracy.h",
		"include/profilerToolVTune.h",
		"include/profilerToolRazor.h",
	},
	["Profiler/Extensions/IO"] = {
		"src/ioProfiler.cpp",
		"include/ioProfiler.h",
		"src/ioQueueProfiler.cpp",
		"include/ioQueueProfiler.h",
	},
	["Profiler/Extensions/Memory"] = {
		"src/profilerExtMemory.cpp",
		"include/profilerExtMemory.h",
	},
	["private"] = {
		"src/build.cpp",
		"src/build.h",
		"src/nameRegistry.cpp",
		"src/nameRegistry.h",
		"src/names.cpp",
		"src/tweakDBIDRegistry.cpp",
		"src/tweakDBIDRegistry.h",
		"src/singleton.cpp",
	},
	["tools"] = {
		"src/red_core.natvis",
		"src/red_core.natstepfilter",
	},
	["misc/process"] = {
		"include/processRunner.h",
		"include/processTime.h",
		"src/processRunner.cpp",
		"src/processTime.cpp",
	},
	["tweakDB"] = {
		"include/tweakDBID.h",
		"include/tweakDBID.hpp",
		"include/tweakDBIDHash.h",
		"include/tweakDBIDHash.hpp",
		"src/tweakDBID.cpp",
	},
	["localization"] = {
		"include/languageRegion.h",
		"src/languageRegion.cpp",
	}
}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redCore"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redCore"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs {
		"include",
		"../../../external/WinPixEventRuntime/include",
		"../../../external/ittnotify/include",
		"../../../external/nvToolsExt/include"
	}

	defines { "RED_MODULE_redCore", "RED_EXPORT_redCore" }
	links {
		"redCompression",
		"redNetwork",
		"redMath",
		"redIO",
		"redContainers",
		"redMemory",
		"redSystem"
	}
	dependson { "redCompression", "redNetwork", "redMath", "redIO", "redContainers", "redMemory", "redSystem" }

	vpaths(redCore_files)
	files(flatten_file_groups(redCore_files))
