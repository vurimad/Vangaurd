---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redImage

redImage_location = '../../../temp/' .. _ACTION .. '/common/redImage'

-- files redImage

redImage_files = {
	["third-party/png"] = {
		"src/png/png.c",
		"src/png/pngerror.c",
		"src/png/pngget.c",
		"src/png/pngmem.c",
		"src/png/pngpread.c",
		"src/png/pngread.c",
		"src/png/pngrio.c",
		"src/png/pngrtran.c",
		"src/png/pngrutil.c",
		"src/png/pngset.c",
		"src/png/pngtrans.c",
		"src/png/pngwio.c",
		"src/png/pngwrite.c",
		"src/png/pngwtran.c",
		"src/png/pngwutil.c",
		"src/png/png.h",
		"src/png/pngconf.h",
		"src/png/pngdebug.h",
		"src/png/pnginfo.h",
		"src/png/pnglibconf.h",
		"src/png/pngpriv.h",
		"src/png/pngstruct.h" },
	["interface"] = {
		"src/imageAccess.cpp",
		"src/image.cpp",
		"include/image.h",
		"include/imageAccess.h",
		"include/imageFilters.h",
		"include/imageFormat.h",
		"include/imageIterator.h",
		"include/imageUtils.h",
		"include/imageAccess.inl",
		"include/imageFormat.inl",
		"include/imageIterator.inl",
		"include/imageUtils.inl" },
	["interface/readers/"] = {
		"include/imageMemoryReader.h",
	},
	["internal/formats"] = {
		"src/imageSaver.cpp",
		"src/imageLoader.cpp",
		"src/imageFormats.cpp",
		"include/imageSaver.h",
		"include/imageLoader.h",
		"include/imageSaverGeneric.h",
		"src/imageSaverGeneric.cpp" },
	["internal/formats/png"] = {
		"src/imageLoaderPNG.cpp",
		"src/imageSaverPNG.cpp",
		"src/imageLoaderPNG.h",
		"src/imageSaverPNG.h" },
	["internal/utils"] = {
		"src/imageUtilsChannels.cpp",
		"src/imageUtilsPacking.cpp",
		"src/imageUtilsBoxDownscale.cpp",
		"src/imageUtilsGuassDownscale.cpp",
		"src/imageUtilsResampleBicubic.cpp",
		"src/imageUtilsHistogramProcessor.cpp",
		"src/imageUtilsFlip.cpp",
		"src/imageUtilsEquirectangular2Cube.cpp" },
	["internal/memory"] = {
		"src/imageMemory.cpp",
		"src/imageMemory.h" },
	["third-party/zlib"] = {
		"src/zlib/adler32.c",
		"src/build.cpp",
		"src/zlib/compress.c",
		"src/zlib/crc32.c",
		"src/zlib/deflate.c",
		"src/zlib/infback.c",
		"src/zlib/inffast.c",
		"src/zlib/inflate.c",
		"src/zlib/inftrees.c",
		"src/zlib/trees.c",
		"src/zlib/uncompr.c",
		"src/zlib/zutil.c",
		"src/build.h" },
	["zlib"] = {
		"src/zlib/crc32.h",
		"src/zlib/deflate.h",
		"src/zlib/inffast.h",
		"src/zlib/inffixed.h",
		"src/zlib/inflate.h",
		"src/zlib/inftrees.h",
		"src/zlib/trees.h",
		"src/zlib/zconf.h",
		"src/zlib/zconf.h.in",
		"src/zlib/zlib.h",
		"src/zlib/zlib.map",
		"src/zlib/zutil.h" },
	[""] = {
		"include/redImagePublic.h",
		"include/redImageApi.h",
		"src/redImageInternal.h" },
	}

-- configs redImage

project "redImage"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(redImage_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	uses { 
		"redCompression",
		"redContainers",
		"redFileSystem",
		"redMath"
	}

	vpaths(redImage_files)

	files(seq(redImage_files))

	filter "files:src/png/*.c"
		sndbs "OFF"
		flags { "NoPCH" }
	filter "files:src/zlib/*.c"
		flags { "NoPCH" }
