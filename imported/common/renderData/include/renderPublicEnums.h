/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once


/// Rendering mode
enum ERenderingMode : Int32
{
	RM_Shaded,
	RM_Shaded_NoAmbient,
	RM_ShadedMax = RM_Shaded_NoAmbient,
	RM_HitProxies,
	RM_GBufferOnly,
	RM_SafeMode,
	RM_OverlayOnly,
	RM_TodvisBake
};

RTTI_DECLARE_ENUM( ERenderingMode );

enum EFramePurpose : Int32
{
	// Normal scenes goes here
	FP_Normal,
	FP_NormalSubscene,

	// Marker that splits normals and custom frame purposes
	FP_NormalMax,

	// Special modes here
	FP_Blank,
	FP_ScreenShot,
	FP_DeferredScreenshot,	// Grab is deferred one frame.
	FP_EnvProbe,
	FP_GlobalIllumination,
	FP_GlobalIlluminationBackfaces,
	FP_CascadesBaking
};

// --------------------

namespace FramePurpose
{
	static inline Bool IsNormal( EFramePurpose purpose ) { return purpose < FP_NormalMax; }
	static inline Bool IsScreenshot( EFramePurpose purpose ) { return purpose == FP_ScreenShot || purpose == FP_DeferredScreenshot; }
	static inline Bool IsBaking( EFramePurpose purpose ) { return purpose == FP_EnvProbe || purpose == FP_GlobalIllumination || purpose == FP_GlobalIlluminationBackfaces || purpose == FP_CascadesBaking; }
}

//********************************************************************************************************************************//
//********************************************************************************************************************************//
// These stream indices should match what is specified for the mesh vertex layouts in gpuWrapApiVertexDeclarations.h and mesh.cpp
// 
// To access these streams in a shader, its FXMeta has to provide the enum value.
// For example, to access Custom0, the FXMeta needs <Stream Type="RMS_Custom0"/> in <StreamsToBind>

enum ERenderMeshStreams : Int32
{
	RMS_PositionSkinning		= RED_FLAG( 0 ),
	RMS_TexCoords				= RED_FLAG( 1 ),
	RMS_TangentFrame			= RED_FLAG( 2 ),
	RMS_Extended				= RED_FLAG( 3 ),

	// For custom rendering requirements...
	RMS_Custom0					= RED_FLAG( 4 ),

	RMS_NUM_STREAMS				= 5,

	RMS_BindAll					= 0xFF,				//!< Bind all used streams. Maximum of 8 are allowed by GpuApi, so 0xFF is enough.
};
RTTI_DECLARE_ENUM( ERenderMeshStreams );

// --------------------

enum EFoliageVisualisationMode : Uint32
{
	VISUALISE_NONE,
	VISUALISE_GRASSINSTANCES,
	VISUALISE_TREEINSTANCES,
	VISUALISE_GRASSLAYERS,
};

// --------------------

// List of Display modes
enum EDisplayMode : Uint32
{
	DISPLAY_LDR,
	DISPLAY_HDR10,
	DISPLAY_DOLBY_VISION
};

RTTI_DECLARE_ENUM(EDisplayMode);

// List of supported sink color primaries
enum EColorPrimary : Uint32
{
	PRIM_REC709,
	PRIM_DCIP3,
	PRIM_BT2020
};

RTTI_DECLARE_ENUM(EColorPrimary);

// --------------------

enum EColorMappingFunction : Uint8
{
	CMF_Linear,
	CMF_sRGB,
	CMF_ArriLogC,

	CMF_MAX,
};

RTTI_DECLARE_ENUM(EColorMappingFunction);

// --------------------

enum ESSAOQualityLevel : Uint32
{
	SSAOQUALITY_VeryLow,
	SSAOQUALITY_Low,
	SSAOQUALITY_Medium,
	SSAOQUALITY_High,
	SSAOQUALITY_VeryHigh
};

RTTI_DECLARE_ENUM(ESSAOQualityLevel);

// --------------------

/// Visibility result
enum ERenderVisibilityResult : Uint32
{
	RVR_NotTested,				//!< We do not have valid results yet...
	RVR_NotVisible,				//!< Object is not visible in main view and in shadow view
	RVR_PartialyVisible,		//!< Object is not directly visible but it's shadow may be
	RVR_Visible,				//!< Object is directly visible in main view
};

// --------------------

/// Environment modifier
enum EEnvManagerModifier : Uint32
{
	EMM_None = 0,
	EMM_WireframeSolid,
	EMM_WireframeSeethrough,
	EMM_Overdraw,
	EMM_OverdrawSeethrough,
	EMM_ParticleOverdraw,
	EMM_ParticleNumLights,
	EMM_DecalOverdraw,
	EMM_LightOverdraw,
	EMM_SceneReferredColor,
	EMM_DisplayReferredColor,
	EMM_GlobalIllumination,
	EMM_SurfaceMaterialID,
	EMM_SurfaceObjectID,
	EMM_SurfaceBaseColor,
	EMM_SurfaceAlbedo,
	EMM_SurfaceSpecularity,
	EMM_SurfaceMetalness,
	EMM_SurfaceRoughness,
	EMM_SurfaceEmissive,
	EMM_SurfaceTranslucency,
	EMM_SurfaceNormalsWorldSpace,
	EMM_SurfaceNormalsViewSpace,
	EMM_SurfaceHairDirection,
	EMM_SurfaceHairID,
	EMM_SurfaceLightBlockerIntensity,
	EMM_GBuffer1RGB,
	EMM_GBuffer1A,
	EMM_ConeAODir,
	EMM_ConeAOAngle,
	EMM_VelocityBuffer,
	EMM_Depth,
	EMM_UvDensity,
	EMM_ToneMappingLuminance,
	EMM_ToneMappingThresholds,
	EMM_LuminanceSpotMeter,
	EMM_IlluminanceMeter,
	EMM_DiffuseLight,
	EMM_SpecularLight,
	EMM_ClayView,
	EMM_PureGreyscaleView,
	EMM_PureWhiteView,
	EMM_PureReflectionView,
	EMM_PureGreyReflectionView,
	EMM_Cascades,
	EMM_MaskShadow,
	EMM_MaskSSAO,
	EMM_MaskTXAA,
	EMM_MaskDistortion,
	EMM_SurfaceCacheID,
	EMM_SurfaceCacheResolution,
	EMM_LightChannels,
	EMM_DebugHitProxies,
	EMM_DebugShadowsMode,
	EMM_RayTracingDebug,
	EMM_SSRResults,
	EMM_SSRFade,
	EMM_DepthOfFieldCoC,
	EMM_MultilayeredMode,
	EMM_MultilayeredProxy,
	EMM_MultilayeredUniqueMasks,
	EMM_MultilayeredMaskWeight,
	EMM_LocalShadowsVariance,
	EMM_LocalShadowsRangesOverlapDynamicsOnly,
	EMM_LocalShadowsRangesOverlapStaticsOnly,
	EMM_LODColoring,
	EMM_TodvisRuntimePreview,
	EMM_TodvisBakePreview,
	EMM_RainMask,
	EMM_VolFogDensity,

	EMM_MAX,
};
RTTI_DECLARE_ENUM( EEnvManagerModifier );

// --------------------

/// Environment color group
enum EEnvColorGroup : Uint8
{
	ECG_Default,		// No env color
	ECG_Sky,			// Sky color
	ECG_Group0,
	ECG_Group1,
	ECG_Group2,
	ECG_Group3,
	ECG_Group4,
	ECG_Group5,
	ECG_Group6,
	ECG_Group7,
	ECG_Group8,
	ECG_Group9,
	ECG_Group10,
	ECG_Group11,
	ECG_Group12,
	ECG_Group13,
	ECG_Group14,
	ECG_Group15,

	ECG_MAX
};

RTTI_DECLARE_ENUM( EEnvColorGroup );

// --------------------

/// Emitter group
enum EEmitterGroup : Uint8
{
	EG_Default,
	EG_Group0,
	EG_Group1,
	EG_Group2,
	EG_Group3,
	EG_Group4,
	EG_Group5,
	EG_Group6,
	EG_Group7,
	EG_Group8,
	EG_Group9,
	EG_Group10,
	EG_Group11,
	EG_Group12,
	EG_Group13,
	EG_Group14,
	EG_Group15,

	EG_MAX
};

RTTI_DECLARE_ENUM( EEmitterGroup );

// --------------------

enum ERenderingPlane : Uint32
{
	RPl_Scene,
	RPl_Background,			// TODO remove this
	RPl_Weapon,
	RPl_HUD,
	RPl_Max,
};

RTTI_DECLARE_ENUM( ERenderingPlane );

// --------------------

enum class RenderSceneLayer : Uint8
{
	Default,
	Cyberspace,
	WorldMap,
	MAX
};
constexpr Uint32 RenderSceneLayer_MAX = Uint32( RenderSceneLayer::MAX );
RTTI_DECLARE_ENUM( RenderSceneLayer );

enum class RenderSceneLayerMask : Uint8
{
	Default		= RED_FLAG( (Uint32)RenderSceneLayer::Default ),
	Cyberspace	= RED_FLAG( (Uint32)RenderSceneLayer::Cyberspace ),
	WorldMap	= RED_FLAG( (Uint32)RenderSceneLayer::WorldMap ),
};
RTTI_DECLARE_BITFIELD( RenderSceneLayerMask );

RED_FORCE_INLINE Bool CheckRenderSceneLayerMask( const RenderSceneLayerMask& mask, const RenderSceneLayer& layer )
{
	RED_FATAL_ASSERT( (Uint32)layer < RenderSceneLayer_MAX, "Invalid render scene layer: %u", (Uint32)layer );
	return ( (Uint32)mask & ( 1 << Uint32( layer ) ) ) != 0;
}

// --------------------

//!! Must match defines in globalConstants.fx !!//
enum ERenderObjectType : Uint8
{
	// STATIC OBJECT TYPES BEGIN (index [0-11])
	ROT_STATIC_FIRST		= 0,
	ROT_Static				= ROT_STATIC_FIRST,
	ROT_Terrain				= 1,
	ROT_Road				= 2,

	// additional static object types go here...

	ROT_STATIC_LAST			= 11,
	// STATIC OBJECT TYPES END
	
	// CUSTOM OBJECT TYPES BEGIN (index [12-19])
	ROT_CUSTOM_FIRST		= ROT_STATIC_LAST + 1,
	
	ROT_CustomCharacter1	= ROT_CUSTOM_FIRST,
	ROT_CustomCharacter2	= 13,
	ROT_CustomCharacter3	= 14,
	ROT_MainPlayer			= 15,
	ROT_NoAO				= 16,
	ROT_NoLighting			= 17,	// Special one for removing light tiles from being computed
	ROT_NoTXAA				= 18,	// Special case for reducing TXAA anty aliasing

	// additional custom object types go here...

	ROT_CUSTOM_LAST			= 19,
	// CUSTOM OBJECT TYPES END

	// DYNAMIC OBJECT TYPES BEGIN (index [20-31])
	ROT_DYNAMIC_FIRST		= ROT_CUSTOM_LAST + 1,
	
	ROT_Skinned				= ROT_DYNAMIC_FIRST,
	ROT_Character			= 21,
	ROT_Foliage				= 22,
	ROT_Grass				= 23,
	ROT_Vehicle				= 24,
	ROT_Weapon				= 25,
	ROT_Particle			= 26,
	ROT_Enemy				= 27,

	// additional dynamic object types go here...

	ROT_DYNAMIC_LAST		= 31,

	// Max can be 32
	ROT_Max					= 32,
};

static_assert( ROT_Max <= 32, "Cannot add more object types" );

RTTI_DECLARE_ENUM( ERenderObjectType );

// ----------------------
enum EDecalRenderMode : Uint8
{
	// Some static types
	DRM_AllStatic = 0,
	DRM_ObjectType = 1,
	DRM_AllDynamic = 2,
};

RTTI_DECLARE_ENUM( EDecalRenderMode );

//!! Must match defines in globalConstants.fx !!//
enum ERenderMaterialType : Uint8
{
	RMT_Standard		= 0,
	RMT_Subsurface		= 1,
	RMT_Cloth			= 2,
	RMT_Eye				= 3,
	RMT_Hair			= 4,
	RMT_Foliage			= 5,

	RMT_Unused0,
	RMT_Unused1,

	// Max can be 8
	RMT_Max,
};

static_assert(RMT_Max <= 8, "Cannot add more material types");

RTTI_DECLARE_ENUM( ERenderMaterialType );

// --------------------

/// Render light type

enum ELightType : Uint32
{
	LT_Point,
	LT_Spot,
	LT_Area,
};

RTTI_DECLARE_ENUM( ELightType );

// --------------------

/// Render light unit

enum ELightUnit : Uint32
{
	LU_Lumen,
	LU_Watt,
	LU_Lux,
	LU_Nit,
	LU_EV100,
};

RTTI_DECLARE_ENUM(ELightUnit);

// --------------------

/// Area light shape

enum EAreaLightShape : Uint32
{
	ALS_Sphere,
	ALS_Capsule,

	ALS_Max
};

RTTI_DECLARE_ENUM( EAreaLightShape );

// --------------------

/// Render light parameter
enum ERenderLightParameter : Uint32
{
	RLP_Brightness,
	RLP_Radius,
	RLP_Color,
	RLP_Temperature,
	RLP_AllowDistantFade
};

// --------------------

/// Parameter mask for rendering fragments
enum ERenderFragmentMaterialParameter : Uint32
{
	RFMP_GameplayParameters				= RED_FLAG( 3 ),
	RFMP_FoliageColor					= RED_FLAG( 4 ),
};

// --------------------

enum ELightShadowCastingMode : Uint32
{
	LSCM_None,
	LSCM_Normal,
	LSCM_OnlyDynamic,
	LSCM_OnlyStatic,
	LSCM_NormalAndContact,
	LSCM_OnlyContact,

	LSCM_Max
};

RTTI_DECLARE_ENUM( ELightShadowCastingMode );

enum ELightShadowSoftnessMode : Uint32
{
	LSSM_ExtraSoft,
	LSSM_Soft,
	LSSM_Default,
	LSSM_Sharp,
	LSSM_ExtraSharp,
	LSSM_Max
};

RTTI_DECLARE_ENUM( ELightShadowSoftnessMode );

// --------------------

// Custom material parameter flag
enum ECustomMaterialParam : Uint32
{
	ECMP_CustomParam0 = RED_FLAG(0),
	ECMP_CustomParam1 = RED_FLAG(1),
	ECMP_CustomParam2 = RED_FLAG(2),
	ECMP_CustomParam3 = RED_FLAG(3),
	ECMP_CustomParam4 = RED_FLAG(4),
	ECMP_CustomParam5 = RED_FLAG(5),
	ECMP_CustomParam6 = RED_FLAG(6)
};

// ---------------------

// Excluding collecting to specific camera types 
enum ECustomCameraTarget : Uint8
{
	ECCTV_All,
	ECCTV_OnlyOffscreen,
	ECCTV_OnlyOnscreen
};
