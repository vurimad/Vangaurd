---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

local redSystem_files = {
	["include/time"] = {
		"include/clock.h",
		"include/stopWatch.h",
		"include/timer.h",
		"include/timerWindows.h",
		"include/timerOrbis.h",
		"include/timerLinux.h",
		"include/profileTimer.h",
	},
	["src/time"] = {
		"src/clock.cpp",
		"src/timerWindows.cpp",
		"src/timerOrbis.cpp",
		"src/timerLinux.cpp",
	},
	["src/crt"] = {
		"src/crt.cpp",
	},
	["include/types"] = {
		"include/types.h",
		"include/typetraits.h",
		"include/systemTypesFormatters.h",
		"include/scopedPtr.h",
		"include/scopedPtr.hpp",
	},
	["src/types"] = {
		"src/systemTypesFormatters.cpp",
	},
	["include/hashing"] = {
		"include/crc.h",
		"include/guid.h",
		"include/hash.h",
		"include/hash.hpp",
	},
	["src/hashing"] = {
		"src/crc.cpp",
		"src/guid.cpp",
	},
	["include/debugging"] = {
		"include/unitTestMode.h",
		"include/assert.h",
		"include/dbgUtils.h",
		"include/errorHandler.h",
		"include/crashData.h",
		"include/crashData.inl",
		"include/crashDataRegistration.h",
		"include/crashDataStorage.h",
		"include/crashDataTypeAdapter.h",
	},		
	["src/debugging"] = {
		"src/unitTestMode.cpp",
		"src/assert.cpp",
		"src/crashDataRegistration.cpp",
		"src/crashDataStorage.cpp",
		"src/errorHandlerImplCrashDumpData.cpp",
		"src/errorHandler.cpp",
		"src/errorHandlerImplHooks.h",
		"src/errorHandlerImplAttachments.h",
		"src/errorHandlerImplAttachments.cpp",
		"src/errorHandlerImplErrorHooksOrbis.h",
		"src/errorHandlerImplErrorHooksOrbis.cpp",
		"src/errorHandlerImplErrorHooksLinux.h",
		"src/errorHandlerImplErrorHooksLinux.cpp",
		"src/errorHandlerImplErrorHooksWin32.h",
		"src/errorHandlerImplErrorHooksWin32.cpp",
		"src/errorHandlerImplWin32.cpp",
		"src/errorHandlerImplMessages.cpp",
		"src/errorHandlerImplMessages.h",
		"src/errorHandlerImplCrashDumpData.h",
		"src/errorReporterWin32.cpp",
		"src/errorReporterWin32.h",
		"src/errorReporterIPCWin32.cpp",
		"src/errorReporterIPCWin32.h",
		"src/dbgUtilsCommon.cpp",
		"src/dbgUtilsWin32.cpp",
		"src/dbgUtilsLinux.cpp",
		"src/dbgUtilsPS4.cpp",
		"src/dbgUtilsXboxOne.cpp",
		"src/sce_callstack.h",
		"src/sce_callstack.cpp",
	},
	["include/encoding"] = {
		"include/base32.h",
		"include/base64.h",
		"include/hex.h",
	},
	["src/encoding"] = {
		"src/base32.cpp",
		"src/base64.cpp",
		"src/hex.cpp",
	},
	["include/threads/impl/PS4"] = {
		"include/redThreadsThreadOrbisAPI.h",
		"include/redThreadsThreadOrbisAPI.inl",
		"include/redThreadsAtomicOrbisAPI.inl",
	},
	["src/threads/impl/PS4"] = {
		"src/redThreadsThreadOrbisAPI.cpp",
	},
	["include/threads/impl/Win32"] = {
		"include/redThreadsThreadWinAPI.h",
		"include/redThreadsThreadWinAPI.inl",
		"include/redThreadsAtomicWinAPI.inl",
	},
	["src/threads/impl/Win32"] = {
		"src/redThreadsThreadWinAPI.cpp",
	},
	["include/threads/impl/Linux"] = {
		"include/redThreadsThreadLinuxAPI.h",
		"include/redThreadsThreadLinuxAPI.inl",
		"include/redThreadsAtomicLinuxAPI.inl",
	},
	["src/threads/impl/Linux"] = {
		"src/redThreadsThreadLinuxAPI.cpp",
	},
	["include/threads"] = {
		"include/readWriteSpinLock.h",
		"include/redThreadsAtomic.h",
		"include/redThreadsRedSystem.h",
		"include/redThreadsThread.h",
		"include/redThreadsPlatform.h",
		"include/redThreadsCommon.h",
		"include/threads.h",
		"include/readWriteSpinLock.inl",
		"include/redThreadsAtomic.inl",
		"include/redThreadsThread.inl",
		"src/readWriteSpinLock.cpp",
	},
	["src/threads"] = {
		"src/redThreadsThread.cpp",
	},
	["include/process"] = {
		"include/processUtility.h",
	},
	["src/process"] = {
		"src/processUtility.cpp",
	},
	["include/module"] = {
		"include/module.h",
	},
	["src/log"] = {
		"src/loggerLocklessQueue.h",
		"src/loggerLocklessQueue.hpp",
		"src/logger.h",
		"src/loggerWorker.h",
		"src/logMessage.cpp",
		"src/logger.cpp",
		"src/loggerSink.cpp",
		"src/loggerWorker.cpp",
		"src/log.cpp",
		"src/loggerFileSink.cpp",
		"src/internalDataError.cpp",
		"src/dataErrorAction.cpp",
	},
	["src/file"] = {
		"src/file.cpp",
	},
	["include/file"] = {
		"include/file.h",
	},
	["src"] = {
		"src/build.h",
		"src/build.cpp",
		"src/ruid.cpp",
		"src/systemAssert.h",
	},
	["include/log"] = {
		"include/loggerSink.h",
		"include/log.h",
		"include/logMessage.h",
		"include/loggerFileSink.h",
		"include/abstractDataError.h",
		"include/dataError.h",
		"include/internalDataError.h",
		"include/internalDataError.hpp",
		"include/dummyDataError.h",
		"include/dataErrorAction.h",
	},
	["include"] = {
		"include/crt.h",
		"include/crt.hpp",
		"include/vPrintf.h",
		"include/redSystemPublic.h",
		"include/redSystemApi.h",
		"include/ruid.h",
		"include/ruid.inl",
		"include/enums.h",
		"include/jsonWriter.h",
		"include/jsonWriter.inl",
		"include/stringWriter.h",
		"include/stringWriter.inl",
	},
	["include/settings"] = {
		"include/architecture.h",
		"include/compilerExtensions.h",
		"include/os.h",
		"include/settings.h",
	},
	["include/utils"] = {
		"include/bitUtils.h",
		"include/formatMacros.h",
		"include/platformUtilsWin32.h",
		"include/swapBytes.h",
		"include/utility.h",
		"include/utility.hpp",
		"include/refCount.h",
		"include/refCount.hpp",
		"include/optional.h",
		"include/fixedSizeFunction.h",
		"include/fixedSizeFunction.hpp",
		"include/functionUtils.h",
		"include/functionUtils.hpp"
	},
	["src/utils"] = {
		"src/platformUtilsWin32.cpp",
		"src/optional.hpp"
	},
	["src/applicationErrorDumper"] = {
		"src/applicationErrorDumper.cpp",
	},
	["include/applicationErrorDumper"] = {
		"include/applicationErrorDumper.h"
	},
}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do
			table.insert(result, file)
		end
	end
	return result
end

project "redSystem"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redSystem"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs
	{
		"include",
		"src",
		"../../../external/ittnotify/include"
	}

	defines
	{
		"RED_MODULE_redSystem",
		"RED_EXPORT_redSystem"
	}

	vpaths(redSystem_files)
	files(flatten_file_groups(redSystem_files))

	filter "system:windows"
		excludes
		{
			"src/dbgUtilsLinux.cpp",
			"src/dbgUtilsPS4.cpp",
			"src/dbgUtilsXboxOne.cpp",
			"src/errorHandlerImplErrorHooksLinux.cpp",
			"src/errorHandlerImplErrorHooksOrbis.cpp",
			"src/redThreadsThreadLinuxAPI.cpp",
			"src/redThreadsThreadOrbisAPI.cpp",
			"src/sce_callstack.cpp",
			"src/timerLinux.cpp",
			"src/timerOrbis.cpp"
		}

	filter "system:linux"
		excludes
		{
			"src/dbgUtilsPS4.cpp",
			"src/dbgUtilsWin32.cpp",
			"src/dbgUtilsXboxOne.cpp",
			"src/errorHandlerImplErrorHooksOrbis.cpp",
			"src/errorHandlerImplErrorHooksWin32.cpp",
			"src/errorHandlerImplWin32.cpp",
			"src/errorReporterIPCWin32.cpp",
			"src/errorReporterWin32.cpp",
			"src/platformUtilsWin32.cpp",
			"src/redThreadsThreadOrbisAPI.cpp",
			"src/redThreadsThreadWinAPI.cpp",
			"src/sce_callstack.cpp",
			"src/timerOrbis.cpp",
			"src/timerWindows.cpp"
		}
		links
		{
			"pthread"
		}

	-- Orbis and other console targets deliberately remain in the source groups.
	-- Their filters will be activated when their SDK/toolchain platform
	-- definitions are introduced to the RED Vanguard Premake layer.
	filter {}
