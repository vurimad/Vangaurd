---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals core

core_location = '../../../temp/' .. _ACTION .. '/common/core'

-- files core

core_files = {

	["api"] = {
		"src/build.cpp",
		"src/coreInit.cpp",
		"src/build.h",
		"include/coreInternal.h",
		"include/corePublic.h",
		"include/coreApi.h",
	},

	-- HTTP

	["http/http"] = {
		"src/httpHeader.cpp",
		"src/httpRequestParser.cpp",
		"src/httpServer.cpp",
		"src/httpResponseSender.cpp",
		"src/httpResponseData.cpp",
		"include/httpHeader.h",
		"include/httpRequestParser.h",
		"include/httpServer.h",
		"include/httpResponseSender.h",
		"include/httpResponseData.h",
		"include/httpRequestHandler.h",
	},
	["http/basics"] = {
		"src/basicURL.cpp",
		"src/basicDataBlob.cpp",
		"include/basicURL.h",
		"include/basicDataBlob.h",
	},
	["http/html"] = {
		"src/htmlDocument.cpp",
		"include/htmlDocument.h",
	},
	["http/websockets"] = {
		"include/webSocketFrame.h",
		"include/webSocketFrameParser.h",
		"include/webSocketEndpoint.h",
		"src/webSocketEndpoint.cpp",
		"src/webSocketFrame.cpp",
		"src/webSocketFrameParser.cpp"
	},

	-- MISC

	["message"] = {
		"include/message.h",
		"include/messageLocation.h",
		"include/messageToken.h",
		"include/messageResourcePathWrapper.h",
		"src/message.cpp",
		"src/messageLocation.cpp",
		"src/messageToken.cpp",
		"src/messageResourcePathWrapper.cpp",
	},
	["misc/allocators"] = {
		"include/allocators.h",
		"src/allocatorsInit.cpp",
	},
	["misc/allocators/telemetryAllocator"] = {
		"include/telemetryAllocator.h",
		"src/telemetryAllocator.cpp",
		"src/lockingMutexDynamicTlsfAllocator.h",
		"src/lockingMutexDynamicTlsfAllocator.cpp",
	},
	["misc/containers/stringUtils"] = {
		"src/stringConversion.cpp",
		"include/stringConversion.h",
		"include/codeChunk.h",
	},
	["misc/containers/bufferUtils"] = {
		"src/dataBufferUtils.cpp",
		"include/dataBufferUtils.h",
	},
	["misc/instancing"] = {
		"src/instanceBuffer.cpp",
		"src/instanceDataLayout.cpp",
		"src/instanceDataLayoutCompiler.cpp",
		"src/instanceVar.cpp",
		"include/instanceBuffer.h",
		"include/instanceDataLayout.h",
		"include/instanceDataLayoutCompiler.h",
		"include/instanceVar.h" },
	["misc/commandlets"] = {
		"src/commandlet.cpp",
		"src/commandletRunner.cpp",
		"src/processStatusReporter.cpp",
		"include/commandlet.h",
		"include/commandletRunner.h",
		"include/processStatusReporter.h"
	},
	["misc/memoryBufferFile"] = {
		"include/memoryBufferFile.h",
		"src/memoryBufferFile.cpp",
	},
	["misc/tags"] = {
		"src/tagList.cpp",
		"src/tagSystem.cpp",
		"include/tagList.h",
		"include/tagList.inl",
		"include/tagSystem.h",
	},
	["misc/system/error"] = {
		"src/dataError.cpp",
		"include/dataError.h" },
	["misc/system/clipboard"] = {
		"src/clipboardBase.cpp",
		"include/clipboardBase.h" },
	["misc/algorithm"] = {	
		"src/polygonTriangulation.cpp",
		"include/polygonTriangulation.h" },
	["misc/algorithm/crc"] = {
		"src/hashHelper.cpp",
		"include/hashHelper.h" },
	["misc/fileSystem/XboxOne"] = {
		"src/xvcPackage.cpp",
		"include/xvcPackage.h" },
	["misc/fileSystem/priorities"] = {
		"src/ioTagResolver.cpp",
		"include/ioTags.h",
		"include/ioTagResolver.h" },
	["misc/debug"] = {
		"src/debugPageHandler.cpp",
		"src/debugPageHTMLDoc.cpp",
		"src/debugPageServer.cpp",
		"include/debugPageHandler.h",
		"include/debugPageHTMLDoc.h",
		"include/debugPageServer.h" },
	["misc/settings"] = {
		"include/settings.h",
		"include/standardSettings.h",
	},
	["misc/smartObject"] = {
		"include/refCountPointer.h" },
	["misc/content"] = {
		"include/cacheFilenames.h" },
	["misc/log"] = {
		"include/loggerTcpSink.h",
		"src/loggerTcpSink.cpp"
	},
	["misc/messageHandler"] = {
		"include/ipcMessageHandler.h",
		"src/ipcMessageHandler.cpp",
		"include/namedPipeMessageHandler.h",

		"src/namedPipeMessageHandler.cpp",
		"include/tcpMessageHandler.h",
		"src/tcpMessageHandler.cpp"
	},
	["misc/memoryBudgets"] = {
		"include/memoryBudgets.h",
		"src/memoryBudgets.cpp",
	},
	["misc/aiStuff"] = {
		"include/generalInterface.h",
		"include/generalInterfaceContainer.h",
	},
	["misc/grid"] = {
		"include/gridUtils.h",
		"src/gridUtils.cpp",
	},
	["misc/platform"] = {
		"include/platform.h"
	}
}

-- configs core

project "core"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(core_location)
	
	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs	{
		"./",
		"src",
		"include"
	}

	uses {
		"redCrypto",
		"resource",
		--"gog",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(core_files)

	files(seq(core_files))

	filter "platforms:Durango"
		excludes {
			"src/interactiveCommandlet.cpp",
			"include/namedPipeMessageHandler.h",
			"src/namedPipeMessageHandler.cpp"
		}

	filter "platforms:ORBIS"
		excludes {
			"src/interactiveCommandlet.cpp",
			"include/namedPipeMessageHandler.h",
			"src/namedPipeMessageHandler.cpp"
		}
		
	filter "system:Linux"
		excludes {
			"src/interactiveCommandlet.cpp",
			"include/namedPipeMessageHandler.h",
			"src/namedPipeMessageHandler.cpp"
		}

