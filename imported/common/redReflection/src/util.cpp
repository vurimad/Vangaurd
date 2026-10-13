/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "util.h"
#include "enumBuilder.h"

RTTI_BEGIN_ENUM( EComparisonType );
	RTTI_ENUM_OPTION( Greater );
	RTTI_ENUM_OPTION( GreaterOrEqual );
	RTTI_ENUM_OPTION( Equal );
	RTTI_ENUM_OPTION( NotEqual );
	RTTI_ENUM_OPTION( Less );
	RTTI_ENUM_OPTION( LessOrEqual );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ECookingPlatform );
	RTTI_ENUM_OPTION( PLATFORM_None );
	RTTI_ENUM_OPTION( PLATFORM_PC );
	RTTI_ENUM_OPTION( PLATFORM_XboxOne );
	RTTI_ENUM_OPTION( PLATFORM_PS4 );
	RTTI_ENUM_OPTION( PLATFORM_WindowsServer );
	RTTI_ENUM_OPTION( PLATFORM_LinuxServer );
RTTI_END_ENUM();