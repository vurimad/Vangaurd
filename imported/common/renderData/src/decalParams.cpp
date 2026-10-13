/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "decalParams.h"

RTTI_BEGIN_ENUM( RenderDecalNormalsBlendingMode )
	RTTI_ENUM_OPTION( AlphaBlending );
	RTTI_ENUM_OPTION( Reorient );
RTTI_END_ENUM()

RTTI_BEGIN_ENUM( RenderDecalOrderPriority )
	RTTI_ENUM_OPTION( Priority0 );
	RTTI_ENUM_OPTION( Priority1 );
	RTTI_ENUM_OPTION( Priority2 );
	RTTI_ENUM_OPTION( Priority3 );
RTTI_END_ENUM()
