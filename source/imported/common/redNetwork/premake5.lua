---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redNetwork

-- files redNetwork

local redNetwork_files = {
	["rawNetwork"] = {
		"include/rawPipeManager.h",
		"include/rawTcpManager.h",
		"include/rawManager.h",
		"include/packetNetworkUtils.h",
		"include/packetNetworkInterfaces.h",
		"src/rawPipeManager.cpp",
		"src/rawTcpManager.cpp",
		"src/packetNetworkUtils.cpp" },
	["base"] = {
		"include/packet.h",
		"include/address.h",
		"include/socket.h",
		"include/namedPipe.h",
		"src/address.cpp",
		"src/packet.cpp",
		"src/socket.cpp",
		"src/namedPipe.cpp" },
	["system"] = {
		"include/network.h",
		"src/network.cpp",
		"include/channel.h",
		"include/manager.h",
		"src/channel.cpp",
		"src/manager.cpp" },
	[""] = {
		"include/redNetworkPublic.h",
		"include/redNetworkApi.h",
		"src/redNetworkInternal.h",
		"src/build.h",
		"src/build.cpp" },
	["packetNetwork"] = {
		"include/packetNetworkManager.h",
		"include/packetNetworkPacketHeader.h",
		"include/packetNetworkPacketParser.h",
		"src/packetNetworkSendingDispositor.h",
		"src/packetNetworkPacketParser.cpp",
		"src/packetNetworkManager.cpp" },
	["utility"] = {
		"include/ping.h",
		"src/ping.cpp" },
	["platforms"] = {
		"include/platformOrbis.h",
		"include/platformWindows.h",
		"include/platformLinux.h",
		"src/platformWindows.cpp" }
	}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redNetwork"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redNetwork"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "include" }

	defines { "RED_MODULE_redNetwork", "RED_EXPORT_redNetwork" }
	links { "redContainers", "redMemory", "redSystem" }
	dependson { "redContainers", "redMemory", "redSystem" }

	vpaths(redNetwork_files)
	files(flatten_file_groups(redNetwork_files))
	
	filter "system:linux"
		files {
			"src/platformLinux.cpp"
		}
		excludes {
			"src/platformWindows.cpp",
			"src/rawPipeManager.cpp"
		}

	filter {}
