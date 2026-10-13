/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderPublicEnums.h"

RTTI_BEGIN_ENUM( ERenderingMode )
{
	RTTI_ENUM_OPTION( RM_HitProxies );
	RTTI_ENUM_OPTION( RM_Shaded );
	RTTI_ENUM_OPTION( RM_Shaded_NoAmbient );
	RTTI_ENUM_OPTION( RM_GBufferOnly );
	RTTI_ENUM_OPTION( RM_SafeMode );
	RTTI_ENUM_OPTION( RM_OverlayOnly );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ERenderMeshStreams );
{
	RTTI_ENUM_OPTION( RMS_PositionSkinning );
	RTTI_ENUM_OPTION( RMS_TexCoords );
	RTTI_ENUM_OPTION( RMS_TangentFrame );
	RTTI_ENUM_OPTION( RMS_Extended );
	RTTI_ENUM_OPTION( RMS_Custom0 );
	RTTI_ENUM_OPTION( RMS_BindAll );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ERenderObjectType )
{
	// Some static types
	RTTI_ENUM_OPTION( ROT_Static	);
	RTTI_ENUM_OPTION( ROT_Terrain	);
	RTTI_ENUM_OPTION( ROT_Road		);

	// Some dynamic types
	RTTI_ENUM_OPTION( ROT_Skinned );
	RTTI_ENUM_OPTION( ROT_Character );
	RTTI_ENUM_OPTION( ROT_Foliage );
	RTTI_ENUM_OPTION( ROT_Grass );
	RTTI_ENUM_OPTION( ROT_Vehicle );
	RTTI_ENUM_OPTION( ROT_Weapon );
	RTTI_ENUM_OPTION( ROT_Particle );
	RTTI_ENUM_OPTION( ROT_Enemy );

	RTTI_ENUM_OPTION( ROT_CustomCharacter1	);
	RTTI_ENUM_OPTION( ROT_CustomCharacter2	);
	RTTI_ENUM_OPTION( ROT_CustomCharacter3	);
	RTTI_ENUM_OPTION( ROT_MainPlayer		);
	RTTI_ENUM_OPTION( ROT_NoAO				);
	RTTI_ENUM_OPTION( ROT_NoLighting		);
	RTTI_ENUM_OPTION( ROT_NoTXAA			);
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EDecalRenderMode )
{
	RTTI_ENUM_OPTION( DRM_AllStatic		);
	RTTI_ENUM_OPTION( DRM_ObjectType	);
	RTTI_ENUM_OPTION( DRM_AllDynamic	);
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ERenderMaterialType )
{
	RTTI_ENUM_OPTION( RMT_Standard );
	RTTI_ENUM_OPTION( RMT_Subsurface );
	RTTI_ENUM_OPTION( RMT_Cloth );
	RTTI_ENUM_OPTION( RMT_Eye );
	RTTI_ENUM_OPTION( RMT_Hair );
	RTTI_ENUM_OPTION( RMT_Foliage );
};
RTTI_END_ENUM();


RTTI_BEGIN_ENUM( EEnvManagerModifier );
{
	RTTI_ENUM_OPTION( EMM_None );
	RTTI_ENUM_OPTION( EMM_WireframeSolid );
	RTTI_ENUM_OPTION( EMM_WireframeSeethrough );
	RTTI_ENUM_OPTION( EMM_Overdraw );
	RTTI_ENUM_OPTION( EMM_OverdrawSeethrough );
	RTTI_ENUM_OPTION( EMM_ParticleOverdraw );
	RTTI_ENUM_OPTION( EMM_ParticleNumLights );
	RTTI_ENUM_OPTION( EMM_DecalOverdraw );
	RTTI_ENUM_OPTION( EMM_LightOverdraw );
	RTTI_ENUM_OPTION( EMM_SceneReferredColor );
	RTTI_ENUM_OPTION( EMM_DisplayReferredColor );
	RTTI_ENUM_OPTION( EMM_GlobalIllumination );
	RTTI_ENUM_OPTION( EMM_SurfaceMaterialID );
	RTTI_ENUM_OPTION( EMM_SurfaceObjectID );
	RTTI_ENUM_OPTION( EMM_SurfaceBaseColor );
	RTTI_ENUM_OPTION( EMM_SurfaceAlbedo );
	RTTI_ENUM_OPTION( EMM_SurfaceSpecularity );
	RTTI_ENUM_OPTION( EMM_SurfaceMetalness );
	RTTI_ENUM_OPTION( EMM_SurfaceRoughness );
	RTTI_ENUM_OPTION( EMM_SurfaceEmissive );
	RTTI_ENUM_OPTION( EMM_SurfaceTranslucency );
	RTTI_ENUM_OPTION( EMM_SurfaceNormalsWorldSpace );
	RTTI_ENUM_OPTION( EMM_SurfaceNormalsViewSpace );
	RTTI_ENUM_OPTION( EMM_SurfaceHairDirection );
	RTTI_ENUM_OPTION( EMM_SurfaceHairID );
	RTTI_ENUM_OPTION( EMM_SurfaceLightBlockerIntensity );	
	RTTI_ENUM_OPTION( EMM_GBuffer1RGB );
	RTTI_ENUM_OPTION( EMM_GBuffer1A );
	RTTI_ENUM_OPTION( EMM_ConeAODir );
	RTTI_ENUM_OPTION( EMM_ConeAOAngle );
	RTTI_ENUM_OPTION( EMM_VelocityBuffer );
	RTTI_ENUM_OPTION( EMM_Depth );
	RTTI_ENUM_OPTION( EMM_UvDensity );
	RTTI_ENUM_OPTION( EMM_ToneMappingLuminance );
	RTTI_ENUM_OPTION( EMM_ToneMappingThresholds );
	RTTI_ENUM_OPTION( EMM_LuminanceSpotMeter );
	RTTI_ENUM_OPTION( EMM_IlluminanceMeter );
	RTTI_ENUM_OPTION( EMM_DiffuseLight );
	RTTI_ENUM_OPTION( EMM_SpecularLight );
	RTTI_ENUM_OPTION( EMM_ClayView );
	RTTI_ENUM_OPTION( EMM_PureGreyscaleView );
	RTTI_ENUM_OPTION( EMM_PureWhiteView );
	RTTI_ENUM_OPTION( EMM_PureReflectionView );
	RTTI_ENUM_OPTION( EMM_PureGreyReflectionView );
	RTTI_ENUM_OPTION( EMM_Cascades );
	RTTI_ENUM_OPTION( EMM_MaskShadow );
	RTTI_ENUM_OPTION( EMM_MaskSSAO );
	RTTI_ENUM_OPTION( EMM_MaskTXAA );
	RTTI_ENUM_OPTION( EMM_MaskDistortion );
	RTTI_ENUM_OPTION( EMM_SurfaceCacheID );
	RTTI_ENUM_OPTION( EMM_SurfaceCacheResolution );
	RTTI_ENUM_OPTION( EMM_LightChannels );
	RTTI_ENUM_OPTION( EMM_DebugHitProxies );
	RTTI_ENUM_OPTION( EMM_DebugShadowsMode );
	RTTI_ENUM_OPTION( EMM_RayTracingDebug );
	RTTI_ENUM_OPTION( EMM_SSRResults );
	RTTI_ENUM_OPTION( EMM_SSRFade );
	RTTI_ENUM_OPTION( EMM_DepthOfFieldCoC );
	RTTI_ENUM_OPTION( EMM_MultilayeredMode );
	RTTI_ENUM_OPTION( EMM_MultilayeredProxy );
	RTTI_ENUM_OPTION( EMM_MultilayeredUniqueMasks );
	RTTI_ENUM_OPTION( EMM_MultilayeredMaskWeight );
	RTTI_ENUM_OPTION( EMM_LocalShadowsVariance );
	RTTI_ENUM_OPTION( EMM_LocalShadowsRangesOverlapDynamicsOnly );
	RTTI_ENUM_OPTION( EMM_LocalShadowsRangesOverlapStaticsOnly );
	RTTI_ENUM_OPTION( EMM_LODColoring );
	RTTI_ENUM_OPTION( EMM_TodvisRuntimePreview );
	RTTI_ENUM_OPTION( EMM_TodvisBakePreview );
	RTTI_ENUM_OPTION( EMM_RainMask );
	RTTI_ENUM_OPTION( EMM_VolFogDensity );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EEnvColorGroup );
{
	RTTI_ENUM_OPTION( ECG_Default );
	RTTI_ENUM_OPTION( ECG_Sky );
	RTTI_ENUM_OPTION( ECG_Group0 );
	RTTI_ENUM_OPTION( ECG_Group1 );
	RTTI_ENUM_OPTION( ECG_Group2 );
	RTTI_ENUM_OPTION( ECG_Group3 );
	RTTI_ENUM_OPTION( ECG_Group4 );
	RTTI_ENUM_OPTION( ECG_Group5 );
	RTTI_ENUM_OPTION( ECG_Group6 );
	RTTI_ENUM_OPTION( ECG_Group7 );
	RTTI_ENUM_OPTION( ECG_Group8 );
	RTTI_ENUM_OPTION( ECG_Group9 );
	RTTI_ENUM_OPTION( ECG_Group10 );
	RTTI_ENUM_OPTION( ECG_Group11 );
	RTTI_ENUM_OPTION( ECG_Group12 );
	RTTI_ENUM_OPTION( ECG_Group13 );
	RTTI_ENUM_OPTION( ECG_Group14 );
	RTTI_ENUM_OPTION( ECG_Group15 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EEmitterGroup );
{
	RTTI_ENUM_OPTION( EG_Default );
	RTTI_ENUM_OPTION( EG_Group0 );
	RTTI_ENUM_OPTION( EG_Group1 );
	RTTI_ENUM_OPTION( EG_Group2 );
	RTTI_ENUM_OPTION( EG_Group3 );
	RTTI_ENUM_OPTION( EG_Group4 );
	RTTI_ENUM_OPTION( EG_Group5 );
	RTTI_ENUM_OPTION( EG_Group6 );
	RTTI_ENUM_OPTION( EG_Group7 );
	RTTI_ENUM_OPTION( EG_Group8 );
	RTTI_ENUM_OPTION( EG_Group9 );
	RTTI_ENUM_OPTION( EG_Group10 );
	RTTI_ENUM_OPTION( EG_Group11 );
	RTTI_ENUM_OPTION( EG_Group12 );
	RTTI_ENUM_OPTION( EG_Group13 );
	RTTI_ENUM_OPTION( EG_Group14 );
	RTTI_ENUM_OPTION( EG_Group15 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ERenderingPlane );
{
	RTTI_ENUM_OPTION( RPl_Scene );
	RTTI_ENUM_OPTION( RPl_Background );
	RTTI_ENUM_OPTION( RPl_Weapon );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( RenderSceneLayer );
{
	RTTI_ENUM_OPTION( Default );
	RTTI_ENUM_OPTION( Cyberspace );
	RTTI_ENUM_OPTION( WorldMap );
}
RTTI_END_ENUM();

static_assert( Uint32( RenderSceneLayer::MAX ) == 3, "Number of render scene layers has changed. Don't forget to update mask" );

RTTI_BEGIN_BITFIELD( RenderSceneLayerMask, 1 /*byte*/ );
{
	RTTI_BITFIELD_OPTION( Default );
	RTTI_BITFIELD_OPTION( Cyberspace );
	RTTI_BITFIELD_OPTION( WorldMap );
}
RTTI_END_BITFIELD();

RTTI_BEGIN_ENUM( ECustomMaterialParam );
{
	RTTI_ENUM_OPTION( ECMP_CustomParam0 );
	RTTI_ENUM_OPTION( ECMP_CustomParam1 );
	RTTI_ENUM_OPTION( ECMP_CustomParam2 );
	RTTI_ENUM_OPTION( ECMP_CustomParam3 );
	RTTI_ENUM_OPTION( ECMP_CustomParam4 );
	RTTI_ENUM_OPTION( ECMP_CustomParam5 );
	RTTI_ENUM_OPTION( ECMP_CustomParam6 );
}
RTTI_END_ENUM();
