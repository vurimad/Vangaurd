---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redFileSystem

redFileSystem_location = '../../../temp/' .. _ACTION .. '/common/redFileSystem/'

-- files redFileSystem

redFileSystem_files = {
    ["api"] = {
        "include/redFileSystemApi.h",
        "include/redFileSystemPublic.h",
        "src/redFileSystemInternal.h",
        "src/redFileSystemInit.cpp",
        "src/build.h",
        "src/build.cpp",
	},
	["general"] = {
		"include/compressedNumSerializer.h",
		"include/file.h",
		"include/fileFormat.h",
		"include/filePaths.h",
		"include/fileSkipableBlock.h",
		"include/fileStringReader.h",
		"include/fileStringWriter.h",
		"include/fileSys.h",
		"include/fileSys.inl",
		"include/fileUtils.h",
		"include/fileVersionList.h",
		"include/memoryFileReader.h",
		"include/memoryFileWriter.h",
		"include/nullFile.h",
		"src/compressedNumSerializer.cpp",
		"src/file.cpp",
		"src/fileFormat.cpp",
		"src/filePathsWindows.cpp",
		"src/filePathsOrbis.cpp",
		"src/filePathsDurango.cpp",
		"src/filePathsLinux.cpp",
		"src/fileSkipableBlock.cpp",
		"src/fileStringReader.cpp",
		"src/fileSys.cpp",
		"src/fileUtils.cpp",
		"src/memoryFileReader.cpp",
		"src/memoryFileWriter.cpp",
		"src/nullFile.cpp",
	},
	["raw"] = {
		"src/rawFileReader.cpp",
		"src/rawFileWriter.cpp",
		"include/rawFileWriter.h",
		"include/rawFileReader.h",
	},
	["buffered"] = {
		"src/bufferedReader.cpp",
		"src/bufferedWriter.cpp",
		"src/bufferedTempFile.cpp",
		"src/bufferedTempFile.h",
		"include/bufferedWriter.h",
		"include/bufferedReader.h",
	},
	["compressed"] = {
		"include/chunkedLZ4FileReader.h",
		"include/chunkedLZ4FileWriter.h",
		"src/chunkedLZ4FileReader.cpp",
		"src/chunkedLZ4FileWriter.cpp",
		"src/chunkedLZ4Utils.h"
	},
	["fileSync"] = {
		"include/fileSyncFileManager.h",
		"include/fileSyncService.h",
		"src/fileSyncFileManager.cpp",
		"src/fileSyncService.cpp",
	},
	["system"] = {
		"include/system.h",
		"src/systemOrbis.cpp",
		"src/systemWin32.cpp",
		"src/systemLinux.cpp",
	},
}

-- configs redFileSystem

project "redFileSystem"
    filter "platforms:not x64_DLL"
        kind "StaticLib"
    filter "platforms:x64_DLL"
        kind "SharedLib"
    filter {}

    language "C++"

    location(redFileSystem_location)

    pchheader "build.h"
    pchsource "src/build.cpp"

    includedirs "include"

    uses {
		"redCore",
		"redCompression",
		"udt",
		"lz4",
		"FileSync"
		}

    vpaths(redFileSystem_files)

    files(seq(redFileSystem_files))
