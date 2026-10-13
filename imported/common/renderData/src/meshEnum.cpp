/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshEnum.h"

RTTI_BEGIN_BITFIELD( EMeshChunkRenderMask, 2 /*bytes*/ );
	RTTI_BITFIELD_OPTION( MCR_Scene );
	RTTI_BITFIELD_OPTION( MCR_Cascade1 );
	RTTI_BITFIELD_OPTION( MCR_Cascade2 );
	RTTI_BITFIELD_OPTION( MCR_Cascade3 );
	RTTI_BITFIELD_OPTION( MCR_Cascade4 );
	RTTI_BITFIELD_OPTION( MCR_DistantShadows );
	RTTI_BITFIELD_OPTION( MCR_LocalShadows );
	RTTI_BITFIELD_OPTION( MCR_IsTwoSided );
	RTTI_BITFIELD_OPTION( MCR_IsRayTracedEmissive );
	RTTI_BITFIELD_OPTION( MCR_PrefabProxy );
	RTTI_BITFIELD_OPTION( MCR_Cascades );
RTTI_END_BITFIELD();

RTTI_BEGIN_BITFIELD( EMeshChunkFlags, 2 /*bytes*/ );
RTTI_BITFIELD_OPTION( MCF_RenderInScene );
RTTI_BITFIELD_OPTION( MCF_RenderInShadows );
RTTI_BITFIELD_OPTION( MCF_IsTwoSided );
RTTI_BITFIELD_OPTION( MCF_IsRayTracedEmissive );
RTTI_BITFIELD_OPTION( MCF_IsPrefabProxy );
RTTI_END_BITFIELD();

RTTI_BEGIN_ENUM( EMeshShadowImportanceBias );
	RTTI_ENUM_OPTION( MSIB_EvenLessImportant );
	RTTI_ENUM_OPTION( MSIB_LessImportant );
	RTTI_ENUM_OPTION( MSIB_Default );
	RTTI_ENUM_OPTION( MSIB_MoreImportant );
	RTTI_ENUM_OPTION( MSIB_EvenMoreImportant );
RTTI_END_ENUM();


