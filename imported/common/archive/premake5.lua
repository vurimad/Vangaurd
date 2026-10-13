---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals archive

archive_location = '../../../temp/' .. _ACTION .. '/common/archive'

-- files archive

archive_files = {
	["archive"] = {
		"include/common.h",
		"include/fileId.h",
		"include/fileIOImpl.h",
		"include/fileIOInterfaces.h",
		"include/fileMetadata.h",
		"include/fileTable.h",
		"include/instance.h",
		"src/fileFormat.h",
		"src/fileId.cpp",
		"src/fileIOImpl.cpp",
		"src/fileMetadata.cpp",
		"src/fileTable.cpp",
		"src/instance.cpp",
		"src/utils.cpp",
		"src/utils.h",
	},
	["support"] = {
		"include/inspector.h",
		"src/inspector.cpp",
	},
	[""] = {
		"include/archiveApi.h",
		"include/archivePublic.h",
		"src/archiveInit.cpp",
		"src/archiveInternal.h",
		"src/build.cpp",
		"src/build.h"
	}
}

-- configs archive

project "archive"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(archive_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "include" }

	uses {
		"redCompression",
		"redCore",
		"redReflection",
	}
	
	vpaths(archive_files)

	files(seq(archive_files))
