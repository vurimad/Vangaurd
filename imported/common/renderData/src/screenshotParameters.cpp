/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "screenshotParameters.h"

RTTI_BEGIN_ENUM( ESaveFormat )
{
	RTTI_ENUM_OPTION( SF_PNG );
	RTTI_ENUM_OPTION( SF_EXR );
	RTTI_ENUM_OPTION( SF_PNG_AND_EXR );
}
RTTI_END_ENUM();

