/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderWeaponPlaneParams.h"

RTTI_BEGIN_TYPE( SWeaponPlaneParams )
RTTI_PROPERTY( m_weaponNearPlaneCM ).editable();
RTTI_PROPERTY( m_blurIntensity ).editable();
RTTI_END_TYPE( )