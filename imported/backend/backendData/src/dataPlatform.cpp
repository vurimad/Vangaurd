/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataPlatform.h"
#include "../../../common/app/include/appPath.h"

//------------------------------------------------------------------------------

RTTI_BEGIN_ENUM_IN_NAMESPACE( TargetPlatform, data );
RTTI_ENUM_OPTION( NONE );
RTTI_ENUM_OPTION( PC );
RTTI_ENUM_OPTION( XBOX1 );
RTTI_ENUM_OPTION( PS4 );
RTTI_ENUM_OPTION( WINDOWS_SERVER );
RTTI_ENUM_OPTION( LINUX_SERVER );
RTTI_END_ENUM();

namespace data
{

const char* CharFromTargetPlatform( TargetPlatform platform )
{
	switch ( platform )
	{
	case TargetPlatform::PC: return "PC";
	case TargetPlatform::XBOX1: return "XboxOne";
	case TargetPlatform::PS4: return "PS4";
	case TargetPlatform::WINDOWS_SERVER: return "WindowsServer";
	case TargetPlatform::LINUX_SERVER: return "LinuxServer";
	case TargetPlatform::NONE:
	default:
		return "None";
	};
}

TargetPlatform TargetPlatformFromChar( const char* str )
{
	if ( red::StrcmpNC( str, "PC" ) == 0 )
	{
		return TargetPlatform::PC;
	}
	else if ( red::StrcmpNC( str, "XboxOne" ) == 0 )
	{
		return TargetPlatform::XBOX1;
	}
	else if ( red::StrcmpNC( str, "PS4" ) == 0 )
	{
		return TargetPlatform::PS4;
	}
	else if ( red::StrcmpNC( str, "WindowsServer" ) == 0 )
	{
		return TargetPlatform::WINDOWS_SERVER;
	}
	else if ( red::StrcmpNC( str, "LinuxServer" ) == 0 ||
		      red::StrcmpNC( str, "Server" ) == 0 )  // temporary backwards compatibility
	{
		return TargetPlatform::LINUX_SERVER;
	}
	return TargetPlatform::NONE;
}

TargetPlatform ParseTargetPlatfrom( const char* str )
{
	if ( red::StrcmpNC( str, "WindowsServer" ) == 0 || red::StrcmpNC( str, "server_windows" ) == 0 )
	{
		return TargetPlatform::WINDOWS_SERVER;
	}
	else if ( red::StrcmpNC( str, "LinuxServer" ) == 0 || red::StrcmpNC( str, "server_linux" ) == 0 ||
		      red::StrcmpNC( str, "Server" ) == 0 )  // temporary backwards compatibility
	{
		return TargetPlatform::LINUX_SERVER;
	}
	else if ( red::StrcmpNC( str, "PC" ) == 0 || red::StrcmpNC( str, "X64" ) == 0 )
	{
		return TargetPlatform::PC;
	}
	else if ( red::StrcmpNC( str, "XboxOne" ) == 0 || red::StrcmpNC( str, "xb1" ) == 0 || red::StrcmpNC( str, "xbox" ) == 0 )
	{
		return TargetPlatform::XBOX1;
	}
	else if ( red::StrcmpNC( str, "PS4" ) == 0 || red::StrcmpNC( str, "ps" ) == 0 )
	{
		return TargetPlatform::PS4;
	}
	// Not sure about these two, if they should be included or not
	else if ( red::StrcmpNC( str, "Durango" ) == 0 )
	{
		return TargetPlatform::XBOX1;
	}
	else if ( red::StrcmpNC( str, "Orbis" ) == 0 )
	{
		return TargetPlatform::PS4;
	}
	return TargetPlatform::NONE;
}

red::AbsolutePath DefaultCookedDepotPath( TargetPlatform platform )
{
	return app::paths::DefaultCookedDepotPathForPlatform( CookingPlatformFromTargetPlatform( platform ) );
}

red::AbsolutePath DefaultCookedCachePath( TargetPlatform platform )
{
	return app::paths::DefaultCookedCachePathForPlatform( CookingPlatformFromTargetPlatform( platform ) );
}

red::AbsolutePath DefaultArchivesRootPath( TargetPlatform platform )
{
	return app::paths::DefaultArchivePathForPlatform( CookingPlatformFromTargetPlatform( platform ) );
}

} // data
