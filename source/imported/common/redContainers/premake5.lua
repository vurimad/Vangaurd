---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

local redContainers_files = {
	["common"] = {
		"include/algorithms.h",
		"include/checkedIterator.h",
		"include/checkedIterator.hpp",
		"include/containerOpResult.h",
		"include/containersCommon.h",
		"include/hash.h",
		"include/indexRange.h",
		"include/pair.h",
		"include/policies.h",
		"src/containersCommon.cpp",
	},
	["array"] = {
		"include/arrayImplUtils.h",
		"include/arrayImplUtils.hpp",
		"include/arrayIterator.h",
		"include/arrayIterator.hpp",
		"include/arraySpan.h",
		"include/arraySpan.hpp",
		"include/arraySpanIterator.h",
		"include/arraySpanIterator.hpp",		
		"include/arrayWrapper.h",
		"include/arrayWrapper.hpp",
		"include/dynArray.h",
		"include/dynArray.hpp",
		"include/dynArrayAccessor.h",
		"include/dynArrayAccessor.hpp",
		"include/fixedArray.h",
		"include/fixedArray.hpp",
		"include/sortedArray.h",
		"include/sortedArray.hpp",
		"include/staticArray.h",
		"include/staticArray.hpp",
		"include/staticArrayAccessor.h",
		"src/staticArrayAccessor.cpp",
	},
	["buffer"] = {
		"include/dynamicBuffer.h",
		"include/dynamicBuffer.hpp",
		"include/fixedBuffer.h",
		"src/dynamicBuffer.cpp",
	},
	["blob"] = {
		"include/blob.h",
		"include/blob.hpp",
		"src/blob.cpp",
	},
	[""] = {
		"include/redContainersApi.h",
		"include/redContainersPublic.h",
		"src/build.h",
		"src/build.cpp",
		"src/redContainersInternal.h",
	},
	["types"] = {
		"include/containersTypesFormatters.h",
		"src/containersTypesFormatters.cpp"
	},
	["string"] = {
		"include/string/string.h",
		"include/string/stringBuffer.h",
		"include/string/stringBuilder.h",
		"include/string/stringLocale.h",
		"include/string/stringPrinter.h",
		"include/string/stringUtils.h",
		"include/string/stringView.h",
		"include/string/stringView.hpp",
		"include/string/tokenizer.h",
		"src/string/string.cpp",
		"src/string/stringBuilder.cpp",
		"src/string/stringLocale.cpp",
		"src/string/stringUtils.cpp",
		"src/string/stringView.cpp",
		"src/string/tokenizer.cpp",
	},
	["ustring"] = {
		"include/ustring/ustring.h",
		"include/ustring/ustringUtil.h",
		"include/ustring/utf16String.h",
		"include/ustring/utf8String.h",
		"include/ustring/utfHelpers.h",
		"include/ustring/utypes.h",
		"src/ustring/ustringUtil.cpp",
		"src/ustring/utf16String.cpp",
		"src/ustring/utf8String.cpp",
		"src/ustring/utfHelpers.cpp",
	},
	["string/utils"] = {
		"include/fundamentalStringConversion.h",
		"include/fundamentalStringParser.h",
		"src/fundamentalStringConversion.cpp",
		"src/fundamentalStringParser.cpp",
	},
	["map"] = {
		"include/hashMap.h",
		"include/hashMap.hpp",
		"include/map.h",
		"include/map.hpp",
	},
	["set"] = {
		"include/hashSet.h",
		"include/hashSet.hpp",
		"include/set.h",
		"include/set.hpp",
	},
	["bit"] = {
		"include/bitSet.h",
		"include/bitSet.hpp",
		"include/bitSetCommon.h",
		"include/bitSetCommon.hpp",
		"include/bitSetDynamic.h",
		"include/bitSetDynamic.hpp",
		"include/bitSetAtomicLatch.h",
		"include/bitSetAtomicLatch.hpp"	},
	["id"] = {
		"include/idAllocator.h",
		"include/idAllocator.hpp",
		"include/lruIdPool.h",
		"src/idAllocator.cpp",
	},
	["queue"] = {
		"include/lockFreeQueue.h",
		"include/lockFreeQueue.hpp",
		"include/priQueue.h",
		"include/priQueue.hpp",
		"include/queue.h",
		"include/queue.hpp",
	},		
	["other"] = {
		"include/circularBuffer.h",
		"include/circularBuffer.hpp",
		"include/heap.h",
		"include/heap.hpp",
		"include/indexAllocator.h",
		"include/intrusiveList.h",
		"include/intrusiveList.hpp",
        "include/minSet.h",
		"include/packedArray.h",
		"include/packedArray.hpp",
		"include/pool.h",
		"include/pool.hpp",
		"src/indexAllocator.cpp",
		"src/pool.cpp",
	},
	["tools"] = {
		"src/red_containers.natvis",
		"src/red_containers.natstepfilter",
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

project "redContainers"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redContainers"

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs {
		"src",	-- needed for including build.h from files in string and ustring directories
		"include",
	}

	defines
	{
		"RED_MODULE_redContainers",
		"RED_EXPORT_redContainers"
	}

	links
	{
		"redMemory",
		"redMath",
		"redSystem"
	}

	dependson
	{
		"redMemory",
		"redMath",
		"redSystem"
	}

	vpaths(redContainers_files)
	files(flatten_file_groups(redContainers_files))
