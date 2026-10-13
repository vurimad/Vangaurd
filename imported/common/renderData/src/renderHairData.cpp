/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderHairData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( HairProfileGradientEntry, rend );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_value ).editable().range( 0.f, 1.f );
	RTTI_PROPERTY( m_color ).editable();
RTTI_END_TYPE();
