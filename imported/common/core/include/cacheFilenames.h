/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// ------------------------------------------------------------------------------
# define CACHE_FILE_EXTENSION					".cache"
// ------------------------------------------------------------------------------

# define CACHE_CONFIGURATION_SUFFIX_FINAL		"_final"
# define CACHE_CONFIGURATION_SUFFIX_NONFINAL	""

#ifdef RED_CONFIGURATION_FINAL
	# define CACHE_CONFIGURATION_SUFFIX			CACHE_CONFIGURATION_SUFFIX_FINAL
#else
	# define CACHE_CONFIGURATION_SUFFIX			CACHE_CONFIGURATION_SUFFIX_NONFINAL
#endif // RED_CONFIGURATION_FINAL
// ------------------------------------------------------------------------------
# define STATIC_SHADER_CACHE_NAME				"staticshader"
# define SHADER_CACHE_NAME						"shader"
// ------------------------------------------------------------------------------
# define CACHE_PLATFORM_SUFFIX_PC				""
# define CACHE_PLATFORM_SUFFIX_PS4				"ps4"
# define CACHE_PLATFORM_SUFFIX_XBOXONE			"xboxone"
// ------------------------------------------------------------------------------

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
	# define CACHE_PLATFORM_SUFFIX				CACHE_PLATFORM_SUFFIX_PC
#elif defined( RED_PLATFORM_ORBIS )
	# define CACHE_PLATFORM_SUFFIX				CACHE_PLATFORM_SUFFIX_PS4
#elif defined( RED_PLATFORM_DURANGO )
	# define CACHE_PLATFORM_SUFFIX				CACHE_PLATFORM_SUFFIX_XBOXONE
#else
# error Unsupported platform!
#endif

# define STATIC_SHADER_CACHE_FILENAME			STATIC_SHADER_CACHE_NAME CACHE_PLATFORM_SUFFIX CACHE_CONFIGURATION_SUFFIX CACHE_FILE_EXTENSION
# define SHADER_CACHE_FILENAME					SHADER_CACHE_NAME CACHE_PLATFORM_SUFFIX CACHE_CONFIGURATION_SUFFIX CACHE_FILE_EXTENSION