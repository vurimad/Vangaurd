/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderSkinData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureRegionPart, rend );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_innerRegion ).readOnly();
	RTTI_PROPERTY( m_outerRegion ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureRegion, rend );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_name ).readOnly();
	RTTI_PROPERTY( m_isStretch ).readOnly();
	RTTI_PROPERTY( m_regionParts ).readOnly();
RTTI_END_TYPE();
