---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals commChannel

commChannel_location = '../../../temp/' .. _ACTION .. '/common/commChannel'

-- files commChannel

commChannel_files = {
	[""] = {
		"src/build.h",
		"src/build.cpp",
		"src/commChannelInternal.h",
		"src/commChannelInit.cpp" },
	["channel"] = {
		"include/channel.h" },
	["channel/connection"] = {
		"include/channelConnectionImpl.h",
		"src/channelConnectionImpl.cpp" },
	["channel/factory"] = {
		"include/channelFactory.h",
		"src/channelFactory.cpp" },
	["channel/proto"] = {
		"include/protoChannel.h",
		"include/protoChannelImpl.h",
		"src/protoChannelImpl.cpp",
		"src/protoTargetImpl.h",
		"src/protoTargetImpl.cpp" },
	["channel/proto/serialization"] = {
		"src/protoMessage.h",
		"src/protoReaderBinary.h",
		"src/protoReaderBinary.cpp",
		"src/protoWriterBinary.cpp",
		"src/protoSerializationCommon.h" },
	["channel/proto/dispatcher"] = {
		"include/protoMessageDispatcher.h",
		"src/protoMessageDispatcher.cpp" },
	["channel/raw"] = {
		"include/rawChannel.h",
		"include/rawChannelImpl.h",
		"src/rawChannelImpl.cpp"
	},
	["network"] = {
		"include/networkConnection.h",
		"src/networkConnection.cpp"
	},
	["api"] = {
		"include/commChannelApi.h",
		"include/commChannelPublic.h" }
	}

-- configs commChannel

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "commChannel"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/commChannel"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include", "../commProtocol/gen" }

	defines
	{
		"RED_MODULE_commChannel",
		"RED_EXPORT_commChannel"
	}

	links
	{
		"commProtocol",
		"redNetwork",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	dependson
	{
		"commProtocol",
		"redNetwork",
		"redContainers",
		"redMemory",
		"redMath",
		"redSystem"
	}

	vpaths(commChannel_files)
	files(flatten_file_groups(commChannel_files))
