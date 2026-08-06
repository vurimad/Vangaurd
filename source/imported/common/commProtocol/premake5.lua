---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals commProtocol

commProtocol_location = '../../../temp/' .. _ACTION .. '/common/commProtocol'

-- files commProtocol

commProtocol_files = {
	["src/protocols"] = {
		"src/test.proto",
		"src/interop.proto",
		"src/services.proto",
		"src/utility.proto",
		"src/scriptDatabase.proto",
		"src/scripts.proto",
		"src/private.cpp"
		},
	["include"] = {
		"include/commProtocolPublic.h" },
	["include/common"] = {
		"include/protoTypes.h",
		"include/protoStaticString.h" },
	["src"] = {
		"src/build.h",
		"src/build.cpp",
		"src/commProtocolInit.cpp" },
	["include/serialization"] = {
		"include/protoWriter.h",
		"include/protoReader.h" },
	["include/message"] = {
		"include/message.h" },
	["src/message"] = {
		"src/message.cpp" },
	["NEW_FILES"] = {
		"include/commProtocolApi.h" }
	}

-- configs commProtocol

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "commProtocol"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/commProtocol"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include", "gen" }

	defines
	{
		"RED_MODULE_commProtocol",
		"RED_EXPORT_commProtocol"
	}

	links
	{
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	dependson
	{
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	vpaths(commProtocol_files)
	files(flatten_file_groups(commProtocol_files))
	files { "gen/protocol.h" }
