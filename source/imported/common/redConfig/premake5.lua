---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redConfig

redConfig_location = '../../../temp/' .. _ACTION .. '/common/redConfig/'

-- files redConfig

redConfig_files = {
	["api"] = {
		"include/redConfigApi.h",
		"include/redConfigPublic.h",
		"include/redConfigPool.h",
		"src/redConfigInternal.h",
		"src/redConfigInit.cpp",
		"src/build.h",
		"src/build.cpp",
		"src/redConfigPool.cpp"
	},
	["config"] = {
		"src/configVar.cpp",
		"src/configVarRegistry.cpp",
		"src/configVarStorage.cpp",
		"src/configVarSystem.cpp",
		"src/configVarHierarchy.cpp",
		"include/configVarRegistry.h",
		"include/configVarStorage.h",
		"include/configVarSystem.h",
		"include/configVarHierarchy.h",
		"include/configVar.h",
	},
	["inGameConfig"] = {
		"include/documentation.h",
		"include/inGameConfigFileUtils.h",
		"include/inGameConfigGroup.h",
		"include/inGameConfigReader.h",
		"include/inGameConfigRegistry.h",
		"include/inGameConfigSystem.h",
		"include/inGameConfigUtils.h",
		"include/inGameConfigVar.h",
		"include/inGameConfigVar.hpp",
		"include/inGameConfigVarListener.h",
		"include/inGameConfigWriter.h",
		"src/inGameConfigGroup.cpp",
		"src/inGameConfigReader.cpp",
		"src/inGameConfigRegistry.cpp",
		"src/inGameConfigSystem.cpp",
		"src/inGameConfigUtils.cpp",
		"src/inGameConfigVar.cpp",
		"src/inGameConfigWriter.cpp",
	}
}

-- configs redConfig

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redConfig"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redConfig"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	defines
	{
		"RED_MODULE_redConfig",
		"RED_EXPORT_redConfig",
		"_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING"
	}

	links
	{
		"redFileSystem",
		"redCore",
		"redCompression",
		"redIO",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	dependson
	{
		"redFileSystem",
		"redCore",
		"redCompression",
		"redIO",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	vpaths(redConfig_files)
	files(flatten_file_groups(redConfig_files))
