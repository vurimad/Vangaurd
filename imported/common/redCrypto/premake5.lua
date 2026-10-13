---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redCrypto

redCrypto_location = '../../../temp/' .. _ACTION .. '/common/redCrypto'

-- files redCrypto

redCrypto_files = {
	[""] = {
		"src/build.cpp",
		"src/build.h",
		"include/redCryptoApi.h",
		"include/redCryptoPublic.h",
		"src/redCryptoInternal.h" },
	["sha256"] = {
		"src/sha256.cpp",
		"src/sha256prv.cpp",
		"include/sha256.h",
		"src/sha256prv.h" },
	["sha1"] = {
		"src/sha1.cpp",
		"src/sha1prv.cpp",
		"include/sha1.h",
		"src/sha1prv.h" },
	["md5"] = {
		"src/md5.cpp",
		"src/md5prv.cpp",
		"include/md5.h",
		"src/md5prv.h" }
	}

-- configs redCrypto

project "redCrypto"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(redCrypto_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { "./", "src", "include" }

	uses "redMemory"

	vpaths(redCrypto_files)

	files(seq(redCrypto_files))
