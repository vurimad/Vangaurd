/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "worldShadowConfig.h"

//******************************************************************************************************//
//******************************************************************************************************//

RTTI_BEGIN_TYPE( ShadowCascadeConfig );
{
	RTTI_PROPERTY( m_range ).editable();
	RTTI_PROPERTY( m_filterSize ).editable();
	RTTI_PROPERTY( m_blendRange ).editable();
	RTTI_PROPERTY( m_biasOffset ).editable();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( ContactShadowsConfig );
{
	RTTI_PROPERTY( m_range ).editable();
	RTTI_PROPERTY( m_rangeLimit ).editable();
	RTTI_PROPERTY( m_screenEdgeFadeRange ).editable();
	RTTI_PROPERTY( m_distanceFadeLimit ).editable();
	RTTI_PROPERTY( m_distanceFadeRange ).editable();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( FoliageShadowConfig );
{
	RTTI_PROPERTY( m_foliageShadowCascadeGradient ).editable().range( 0.0f, 2.0f );
	RTTI_PROPERTY( m_foliageShadowCascadeFilterScale ).editable().range( 0.0f, 4.0f );
	RTTI_PROPERTY( m_foliageShadowCascadeGradientDistanceRange ).editable().range( 10.0f, 60.0f );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( WorldShadowConfig );
{
	RTTI_PROPERTY( m_contactShadows ).editable();

	RTTI_PROPERTY( m_distantShadowsNumLevels ).editable().range( 2, 4 );
	RTTI_PROPERTY( m_distantShadowsBaseLevelRadius ).editable();

	RTTI_PROPERTY( m_foliageShadowConfig ).editable();
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( WorldLightingConfig );
{
	RTTI_PROPERTY( m_lightAttenuationClamp ).editable().range( 1, 128 );
}
RTTI_END_TYPE();

//******************************************************************************************************//
//******************************************************************************************************//

ContactShadowsConfig::ContactShadowsConfig()
	: m_range( 0.05f )
	, m_rangeLimit( 0.075f )
	, m_screenEdgeFadeRange( 0.15f )
	, m_distanceFadeLimit( 3.0f )
	, m_distanceFadeRange( 1.0f )
{
}

ContactShadowsConfig ContactShadowsConfig::operator*( const Float t ) const
{
	ContactShadowsConfig ret;

	ret.m_range = m_range * t;
	ret.m_rangeLimit = m_rangeLimit * t;
	ret.m_screenEdgeFadeRange = m_screenEdgeFadeRange * t;
	ret.m_distanceFadeLimit = m_distanceFadeLimit * t;
	ret.m_distanceFadeRange = m_distanceFadeRange * t;

	return ret;
}
ContactShadowsConfig ContactShadowsConfig::operator+( const ContactShadowsConfig& t ) const
{
	ContactShadowsConfig ret;

	ret.m_range = m_range + t.m_range;
	ret.m_rangeLimit = m_rangeLimit + t.m_rangeLimit;
	ret.m_screenEdgeFadeRange = m_screenEdgeFadeRange + t.m_screenEdgeFadeRange;
	ret.m_distanceFadeLimit = m_distanceFadeLimit + t.m_distanceFadeLimit;
	ret.m_distanceFadeRange = m_distanceFadeRange + t.m_distanceFadeRange;

	return ret;
}

//******************************************************************************************************//
//******************************************************************************************************//

FoliageShadowConfig::FoliageShadowConfig()
	: m_foliageShadowCascadeGradient( 0.1f )
	, m_foliageShadowCascadeFilterScale( 0.1f )
	, m_foliageShadowCascadeGradientDistanceRange( 50.0f )
{

}

//******************************************************************************************************//
//******************************************************************************************************//

WorldShadowConfig::WorldShadowConfig()
	: m_distantShadowsNumLevels( 3 )
	, m_distantShadowsBaseLevelRadius( 250.0f )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

WorldLightingConfig::WorldLightingConfig()
	: m_lightAttenuationClamp( 96.0f )
{

}

//******************************************************************************************************//
//******************************************************************************************************//
