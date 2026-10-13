/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "visibilityWorldCommon.h"
#include "visibilityOccluderMeshResource.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( WorldOccluderType, vis );
	RTTI_ENUM_OPTION( Default );
	RTTI_ENUM_OPTION( None );
	RTTI_ENUM_OPTION( Detail );
	RTTI_ENUM_OPTION( MinorInterior );
	RTTI_ENUM_OPTION( MajorInterior );
	RTTI_ENUM_OPTION( Exterior );
RTTI_END_ENUM();
