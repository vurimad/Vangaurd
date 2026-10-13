/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderProxyInitData.h"
#include "renderSkinningData.h"
#include "renderLightBase.h"
#include "decalParams.h"


RTTI_BEGIN_ENUM(EColorPrimary);
{
	RTTI_ENUM_OPTION( PRIM_REC709 );
	RTTI_ENUM_OPTION( PRIM_DCIP3 );
	RTTI_ENUM_OPTION( PRIM_BT2020 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM(EColorMappingFunction);
{
	RTTI_ENUM_OPTION( CMF_Linear );
	RTTI_ENUM_OPTION( CMF_sRGB );
	RTTI_ENUM_OPTION( CMF_ArriLogC );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM(ESSAOQualityLevel);
{
	RTTI_ENUM_OPTION( SSAOQUALITY_VeryLow );
	RTTI_ENUM_OPTION( SSAOQUALITY_Low );
	RTTI_ENUM_OPTION( SSAOQUALITY_Medium );
	RTTI_ENUM_OPTION( SSAOQUALITY_High );
	RTTI_ENUM_OPTION( SSAOQUALITY_VeryHigh );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ELightType );
{
	RTTI_ENUM_OPTION( LT_Point );
	RTTI_ENUM_OPTION( LT_Spot );
	RTTI_ENUM_OPTION( LT_Area );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM(ELightUnit);
{
	RTTI_ENUM_OPTION( LU_Lumen );
	RTTI_ENUM_OPTION( LU_Watt );
	RTTI_ENUM_OPTION( LU_Lux );
	RTTI_ENUM_OPTION( LU_Nit );
	RTTI_ENUM_OPTION( LU_EV100 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EAreaLightShape );
{
	RTTI_ENUM_OPTION( ALS_Sphere );
	RTTI_ENUM_OPTION( ALS_Capsule );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ELightShadowCastingMode );
{
	RTTI_ENUM_OPTION( LSCM_None );
	RTTI_ENUM_OPTION( LSCM_Normal );
	RTTI_ENUM_OPTION( LSCM_OnlyDynamic );
	RTTI_ENUM_OPTION( LSCM_OnlyStatic );
	RTTI_ENUM_OPTION( LSCM_NormalAndContact );
	RTTI_ENUM_OPTION( LSCM_OnlyContact );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ELightShadowSoftnessMode );
{
	RTTI_ENUM_OPTION( LSSM_ExtraSoft );
	RTTI_ENUM_OPTION( LSSM_Soft );
	RTTI_ENUM_OPTION( LSSM_Default );
	RTTI_ENUM_OPTION( LSSM_Sharp );
	RTTI_ENUM_OPTION( LSSM_ExtraSharp );
}
RTTI_END_ENUM();


RTTI_BEGIN_TYPE_IN_NAMESPACE( SLightFlickering, rend );
{
	RTTI_PROPERTY( m_positionOffset ).instanceEditable();
	RTTI_PROPERTY( m_flickerStrength ).instanceEditable();
	RTTI_PROPERTY( m_flickerPeriod ).instanceEditable().range( 0.05f, RED_FLT_MAX, true );	// Lowe bottom limit without slider thing
}
RTTI_END_TYPE();


//////////////////////////////////////////////////////////////////////////////////////////////////


RenderProxyComponentInitData::RenderProxyComponentInitData()
	: m_type ( RPT_None )
	, m_constructorValidation( 0x55 )
	, m_localToWorld( rend::RenderProxyTransform::IDENTITY() )
	, m_sceneLayerMask( RenderSceneLayerMask::Default )
{
}

RenderProxyComponentInitData::~RenderProxyComponentInitData() = default;


//////////////////////////////////////////////////////////////////////////////////////////////////
RenderProxyLightComponentInitData::RenderProxyLightComponentInitData()
	: m_radius( 0.0f )
	, m_unit( LU_Lumen )
	, m_intensity( 0.0f )
	, m_rayTracingIntensityScale( 1.0f )
	, m_EV( 0.0f )
	, m_colorGammaRGB( Color::WHITE() )
	, m_colorTemperature( -1 )
	, m_envColorGroup( ECG_Default )
	, m_autoHideDistance( 20.0f )
	, m_shadowFadeRange( 10.0f )
	, m_shadowSoftnessMode( ELightShadowSoftnessMode::LSSM_Default )
	, m_secondaryAutoHideDistance( -1.0f )
	, m_shadowRadius( -1.0f )
	, m_iesProfileIdx( 0 )
	, m_flickering( nullptr )
	, m_useInGI( true )
	, m_useInFog( true )
	, m_useSceneDiffuse( true )
	, m_useSceneSpecular( true )
	, m_roughnessBias( 0 )
	, m_directional( false )
	, m_clampAttenuation( false )
	, m_inverseSquareFallof( true )
	, m_allowDistantLight( true )
	, m_useInTransparents( true )
	, m_useInParticles( true )
	, m_hasTransformParent( false )
	, m_occlusionId( 0 )
	, m_getDynamicShadowsFaceMask( 0 )
	, m_enableLocalShadows( false )
	, m_contactShadows( rend::CSR_None )
	, m_useSecondaryReferencePoint( false )
	, m_component( false )
	, m_shadowFadeDistance( 0.0f )
	, m_projectionTexture( nullptr )
	, m_channels( 0 )
	, m_group(0)
	, m_colorGroupSaturation(100)
	, m_portalAngleCutoff(0)
{

}

RenderProxyLightComponentInitData::~RenderProxyLightComponentInitData()
{

}

//////////////////////////////////////////////////////////////////////////////////////////////////

RenderProxyParticleComponentInitData::Emitter::Emitter()
	: m_isEnabled( false )
	, m_emitterResource( nullptr )
	, m_material( nullptr )
{
}

RenderProxyParticleComponentInitData::Emitter::~Emitter()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////

RenderProxyMeshComponentInitData::RenderProxyMeshComponentInitData()
	: m_renderMesh( nullptr )
	, m_materialSetup{ nullptr }
	, m_componentFriendlyName( "<Mesh>" )
	, m_numVirtualInstances( 0 )
	//	, m_lodDistanceScale( 1.f )
	, m_forcedLODLevel( -1 )
	, m_chunkMask( UINT64_MAX )
	, m_LODLevelScales( UINT32_MAX )
	, m_setupsNum( 0 )
	, m_constrainAutoHideDistanceToTerrainHeightMap( false )
	, m_isCharacterShadowFallback( false )
	, m_hasMeshResource( false )
	, m_meshIsPrefabProxy( false )
	, m_meshCanMergeIntoGlobalShadowMesh( false )
	, m_useSecondaryReferencePoint( false )
	, m_isCastingDynamicDistantShadow( false )
	, m_isCastingGlobalShadows( true )
	, m_isCastingLocalShadows( false )
	, m_isCastingShadowsEvenIfNotVisible( false )
	, m_forceStatic( false )
	, m_isTwoSided( false )
	, m_initAsHidden( false )
	, m_isDissolveVisible( true )
	, m_isTerrain( false )
	, m_isClusteredProxy( false )
	, m_isGeneratedByVegetationSystem( false )
	, m_materialOverride( false )
#ifndef RED_CONFIGURATION_FINAL
	, m_isGIVisible( false )
	, m_isGITransparent( false )
#endif // RED_CONFIGURATION_FINAL
	, m_HACK_isMaterialPriorityBumped( false )
	, m_skipDynamicDecals( false )
	, m_isCloth( false )
	, m_windImpulseEnabled( true )
	, m_removeFromRainMap( true )
	, m_calcLodDistFromBboxEdge( false )
	, m_numClothLODs( 0 )
	, m_customCameraTarget( ECustomCameraTarget::ECCTV_All )
	, m_skinningData( nullptr )
	, m_additionalMeshChunkRenderMask( (EMeshChunkFlags)0 )
	, m_destructionRegions ( 0 )
	, m_todVisMask( 0 )
	, m_mirrorCullingBox(Vector4::ZEROS(), Vector4::ZEROS(), Vector4::ZEROS(), Vector4::ZEROS())
	, m_garmentMorphOffsetScales( rend::PoolRenderProxy() )
	, m_garmentMorphOffsetScaleHashes( rend::PoolRenderProxy() )
{

}

RenderProxyMeshComponentInitData::~RenderProxyMeshComponentInitData()
{

}

//////////////////////////////////////////////////////////////////////////////////////////////////

 RenderProxyMorphTargetSkinnedMeshComponentInitData::RenderProxyMorphTargetSkinnedMeshComponentInitData()
 {
 }
 RenderProxyMorphTargetSkinnedMeshComponentInitData::~RenderProxyMorphTargetSkinnedMeshComponentInitData()
 {
 }

//////////////////////////////////////////////////////////////////////////////////////////////////

RenderProxyDecalComponentInitData::RenderProxyDecalComponentInitData()
	: m_alpha( 1.0f )
	, m_effectScale( Vector4::ONES() )
	, m_normalThreshold( 0.0f )
	, m_roughnessScale( 1.0f )
	, m_orderNo( 0 )
	, m_horizontalFlip( false )
	, m_verticalFlip( false )
	, m_isStretchingEnabled( false )
	, m_isNormalTresholdEnabled( true )
	, m_normalsBlendingMode( RenderDecalNormalsBlendingMode::AlphaBlending )
	, m_materialRenderResource( nullptr )
	, m_frameOverride( -1 )
	, m_surfaceType( ERenderObjectType::ROT_Static )
	, m_decalRenderMode( EDecalRenderMode::DRM_AllStatic )
	, m_shouldCollectWithRayTracing( true )
{

}

RenderProxyDecalComponentInitData::~RenderProxyDecalComponentInitData()
{

}

//////////////////////////////////////////////////////////////////////////

RenderProxyDynamicDecalSpawnerComponentInitData::RenderProxyDynamicDecalSpawnerComponentInitData()
	: m_materialRenderResource( nullptr )
	, m_alpha( 1.0f )
{
}

RenderProxyDynamicDecalSpawnerComponentInitData::~RenderProxyDynamicDecalSpawnerComponentInitData()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////

RenderProxyLightChannelComponentInitData::RenderProxyLightChannelComponentInitData()
	: m_channels( 1 )
	, m_vertices{ rend::PoolRenderProxy() }
	, m_indices{ rend::PoolRenderProxy() }
	, m_group( rend::LCG_LightClippingVolume )
	, m_component( false )
{}

//////////////////////////////////////////////////////////////////////////////////////////////////

RenderProxyInitInfo::~RenderProxyInitInfo()
{
#ifndef RED_CONFIGURATION_FINAL
	RED_FATAL_ASSERT( m_initInfoStateCheck, "Invalid init info. Don't memset it" );
#endif
}
