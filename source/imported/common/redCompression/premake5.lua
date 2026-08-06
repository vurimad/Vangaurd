---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redCompression

-- files redCompression

local redCompression_files = {
	[""] = {
		"src/build.cpp",
		"src/build.h",
		"src/redCompressionInternal.h",
		"include/redCompressionApi.h",
		"include/redCompressionPublic.h" },
	["third-party/lz4"] = {
		"src/lz4/lz4.c",
		"src/lz4/lz4hc.c",
		"src/lz4/lz4.h",
		"src/lz4/lz4hc.h" },
	["third-party/zlib"] = {
		"src/zlib/adler32.c",
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
		"src/zlib/crc32.h",
		"src/zlib/deflate.h",
		"src/zlib/inffast.h",
		"src/zlib/inffixed.h",
		"src/zlib/inflate.h",
		"src/zlib/inftrees.h",
		"src/zlib/trees.h",
		"src/zlib/zconf.h",
		"src/zlib/zlib.h",
		"src/zlib/zutil.h",
		"src/zlib/zconf.h.in" },
	["third-party/doboz"] = {
		"src/doboz/compressor.cpp",
		"src/doboz/decompressor.cpp",
		"src/doboz/dictionary.cpp",
		"src/doboz/common.h",
		"src/doboz/compressor.h",
		"src/doboz/decompressor.h",
		"src/doboz/dictionary.h" },
	["third-party/snappy"] = {
		"src/snappy/snappy.cc",
		"src/snappy/snappy-c.cc",
		"src/snappy/snappy-sinksource.cc",
		"src/snappy/snappy-stubs-internal.cc",
		"src/snappy/snappy.h",
		"src/snappy/snappy-c.h",
		"src/snappy/snappy-internal.h",
		"src/snappy/snappy-sinksource.h",
		"src/snappy/snappy-stubs-internal.h",
		"src/snappy/snappy-stubs-public.h" },
	["compression"] = {
		"src/compression.cpp",
		"src/resultBuffer.cpp",
		"include/compression.h",
		"include/resultBuffer.h" },
	["compression/wrappers"] = {
		"src/wrapperDoboz.cpp",
		"src/wrapperZlib.cpp",
		"src/wrapperSnappy.cpp",
		"src/wrapperLZ4.cpp",
		"src/wrapperKraken.cpp",
		"src/wrapperNoCompression.cpp",
		"src/wrapperDoboz.h",
		"src/wrapperZlib.h",
		"src/wrapperSnappy.h",
		"src/wrapperLZ4.h",
		"src/wrapperKraken.h",
		"src/wrapperNoCompression.h" }
	}

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redCompression"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redCompression"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include", "../../../external/oodle/include" }

	defines { "RED_MODULE_redCompression", "RED_EXPORT_redCompression" }
	links { "redMemory", "redSystem" }
	dependson { "redMemory", "redSystem" }

	vpaths(redCompression_files)
	files(flatten_file_groups(redCompression_files))

	filter "files:src/doboz/*.cpp"
		enablepch "Off"
	filter "files:src/lz4/*.c"
		enablepch "Off"
	filter "files:src/snappy/*.cc"
		enablepch "Off"
	filter "files:src/zlib/*.c"
		enablepch "Off"
