/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/util.h" // For ECookingPlatform

namespace data
{

// Target platform enumeration that is stored inside dependencies and used in the data build system
enum class TargetPlatform : Uint32
{
	NONE,
	PC,
	XBOX1,
	PS4,
	WINDOWS_SERVER,
	LINUX_SERVER,
};

static_assert( ECookingPlatform::PLATFORM_PC == static_cast< Uint32 >( TargetPlatform::PC ), "Platforms must match" );
static_assert( ECookingPlatform::PLATFORM_XboxOne == static_cast< Uint32 >( TargetPlatform::XBOX1 ), "Platforms must match" );
static_assert( ECookingPlatform::PLATFORM_PS4 == static_cast< Uint32 >( TargetPlatform::PS4 ), "Platforms must match" );
static_assert( ECookingPlatform::PLATFORM_WindowsServer == static_cast< Uint32 >( TargetPlatform::WINDOWS_SERVER ), "Platforms must match" );
static_assert( ECookingPlatform::PLATFORM_LinuxServer == static_cast< Uint32 >( TargetPlatform::LINUX_SERVER ), "Platforms must match" );

// Returns a user-friendly string for the current target platform
BACKEND_DATA_API const char* CharFromTargetPlatform( TargetPlatform platform );

// Converts from the user-friendly string for the current platform into the enumeration
BACKEND_DATA_API TargetPlatform TargetPlatformFromChar( const char* str );

// Parses a string to determine the target platform, handles more cacses than the conversion functions above
BACKEND_DATA_API TargetPlatform ParseTargetPlatfrom( const char* str );

constexpr TargetPlatform TargetPlatformFromCookingPlatform( ECookingPlatform platform )
{
	return static_cast< TargetPlatform >( platform );
}

constexpr ECookingPlatform CookingPlatformFromTargetPlatform( TargetPlatform platform )
{
	return static_cast< ECookingPlatform >( platform );
}

// Returns the default cooked depot path (the root of the cooked depot tree) for the given target platform
BACKEND_DATA_API red::AbsolutePath DefaultCookedDepotPath( TargetPlatform platform );

// Returns the default cooked cache path (the directory where various caches are kept) for the given target platform
BACKEND_DATA_API red::AbsolutePath DefaultCookedCachePath( TargetPlatform platform );

// Returns the default base directory where the archives are kept for the given target platform
BACKEND_DATA_API red::AbsolutePath DefaultArchivesRootPath( TargetPlatform platform );

} // data
