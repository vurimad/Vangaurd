/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "environmentGameParams.h"
#include "renderPublicEnums.h"

RTTI_BEGIN_TYPE( CEnvDisplaySettingsParams );
{
	RTTI_PROPERTY( m_enableInstantAdaptation ).editable();
	RTTI_PROPERTY( m_enableGlobalLightingTrajectory ).editable();
	RTTI_PROPERTY( m_enableEnvProbeInstantUpdate ).editable();
	RTTI_PROPERTY( m_allowEnvProbeUpdate ).editable();
	RTTI_PROPERTY( m_allowBloom ).editable();
	RTTI_PROPERTY( m_allowColorMod ).editable();
	RTTI_PROPERTY( m_allowAntialiasing ).editable();
	RTTI_PROPERTY( m_allowGlobalFog ).editable();
	RTTI_PROPERTY( m_allowDOF ).editable();
	RTTI_PROPERTY( m_allowSSAO ).editable();
	RTTI_PROPERTY( m_allowCloudsShadow ).editable();
	RTTI_PROPERTY( m_allowWaterShader ).editable();
	RTTI_PROPERTY( m_gamma ).editable();
}
RTTI_END_TYPE();

// --------------------------------------------------------------------------

CEnvDisplaySettingsParams::CEnvDisplaySettingsParams ()
	: m_enableInstantAdaptation( false )
	, m_enableGlobalLightingTrajectory( false )
	, m_enableEnvProbeInstantUpdate( false )
	, m_allowEnvProbeUpdate ( true )
	, m_allowBloom( true )
	, m_allowColorMod( true )
	, m_allowAntialiasing( true )
	, m_allowGlobalFog( true )
	, m_allowDOF( true )
	, m_allowSSAO( true )
	, m_allowCloudsShadow( true )
	, m_forceCutsceneDofMode( false )
	, m_allowWaterShader( true )
	, m_gamma( 2.2f )
	, m_disableTonemapping( false )
	, m_displayMode( EMM_None )
{}

Bool CEnvDisplaySettingsParams::IsLightMeterMode() const
{
	return ( m_displayMode == EMM_GlobalIllumination ||
		m_displayMode == EMM_LuminanceSpotMeter ||
		m_displayMode == EMM_IlluminanceMeter ||
		m_displayMode == EMM_ToneMappingLuminance ||
		m_displayMode == EMM_ToneMappingThresholds );
}

Bool CEnvDisplaySettingsParams::HasLightingPass() const
{
	return ( m_displayMode == EMM_None ||
		m_displayMode == EMM_PureGreyReflectionView ||
		m_displayMode == EMM_PureGreyscaleView ||
		m_displayMode == EMM_PureWhiteView ||
		m_displayMode == EMM_PureReflectionView ||
		m_displayMode == EMM_ClayView ||
		m_displayMode == EMM_SceneReferredColor ||
		m_displayMode == EMM_DisplayReferredColor ||
		m_displayMode == EMM_PureGreyReflectionView  ||
		m_displayMode == EMM_SSRResults ||
		m_displayMode == EMM_SSRFade ||
		m_displayMode == EMM_LocalShadowsVariance ||
		m_displayMode == EMM_LocalShadowsRangesOverlapDynamicsOnly ||
		m_displayMode == EMM_LocalShadowsRangesOverlapStaticsOnly );
}

Bool CEnvDisplaySettingsParams::NeedShadows() const
{
	return m_displayMode == EMM_MaskShadow || m_displayMode == EMM_Cascades || m_displayMode == EMM_GlobalIllumination || HasLightingPass();
}

Bool CEnvDisplaySettingsParams::HasDetailedView() const
{
	return m_displayMode == EMM_ConeAOAngle || m_displayMode == EMM_ConeAODir || m_displayMode == EMM_MaskSSAO || NeedShadows();
}

Bool CEnvDisplaySettingsParams::IsMotionVectorsView() const
{
	return m_displayMode == EMM_VelocityBuffer;
}

// --------------------------------------------------------------------------
