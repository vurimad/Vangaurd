/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "playback.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( PlaybackState, tools );
	RTTI_ENUM_OPTION( Stopped );
	RTTI_ENUM_OPTION( Playing );
	RTTI_ENUM_OPTION( Paused );
	RTTI_ENUM_OPTION( Edit );
RTTI_END_ENUM();
