/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderDataApi.h"
#include "renderResource.h"
#include "renderPublicEnums.h"
#include "renderPublicTypes.h"
#include "../../redReflection/include/resourceAsyncReference.h"
#include "dynamicDecal.h"
#include "morphTargetCommon.h"
#include "renderWorldTransform.h"

class IMaterial; // acseparate_materials

// --------------------

enum class RenderDecalNormalsBlendingMode : Uint8;
enum class RenderDecalOrderPriority : Uint8;
enum EMeshChunkFlags : Uint16;
enum EEnvColorGroup : Uint8;
class IRenderEntityGroup;
class CGIDataResource;
class CReflectionProbeDataResource;
class IRenderSkinningData;

/// Type of rendering proxy
enum ERenderProxyType : Uint8
{
	RPT_None,						//!< Invalid type
	RPT_Mesh,						//!< Proxy for mesh ( static, skinned )
	RPT_PointLight,					//!< Proxy for point light
	RPT_SpotLight,					//!< Proxy for spot light
	RPT_AreaLight,					//!< Proxy for area light
	RPT_Particles,					//!< Proxy for particles
	RPT_Foliage,					//!< Proxy for foliage
	RPT_SSDecal,					//!< Proxy for screen-space decals
	RPT_VectorField,				//!< Proxy for vector fields
	RPT_FogVolume,					//!< Proxy for fog volumes
	RPT_GI,							//!< Proxy for GI data sector
	RPT_ReflectionProbe,			//!< Proxy for Reflection Probe
	RPT_MorphTargetMesh,			//!< Proxy for morph target mesh
	RPT_LightVolume,				//!< Proxy for light clipping volumes
	RPT_DynamicDecalSpawner,		//!< Proxy for dynamic decal spawner
	RPT_AutoSpawner,				//!< Proxy representing terrain cell for automatic spawning of meshes (for example grass)
	RPT_LightBlocker,
	RPT_Max
};

// --------------------

namespace rend
{
	enum LightChannelGroup : Uint8;
	enum ContactShadowReciever : Uint8;

	/// Light flickering
	struct RENDER_DATA_API SLightFlickering
	{
		RTTI_DECLARE_TYPE( SLightFlickering );

		Float		m_positionOffset;
		Float		m_flickerStrength;
		Float		m_flickerPeriod;	

		SLightFlickering()
			: m_positionOffset( 0.0f )
			, m_flickerStrength( 0.0f )
			, m_flickerPeriod( 0.2f )		
		{};
	};


	static RED_FORCE_INLINE constexpr Float GetPointLightColorScale()	{ return 1.0f / (4.0f * RED_PI);	}
										  
	static RED_FORCE_INLINE constexpr Float GetSpotLightColorScale()	{ return 1.0f / (4.0f * RED_PI);	}
										  
	static RED_FORCE_INLINE constexpr Float GetAreaLightColorScale()	{ return 1.0f;	}

	static RED_FORCE_INLINE Float GetColorModifier( ERenderProxyType proxyType )
	{
		switch ( proxyType )
		{
		case RPT_AreaLight:
		case RPT_PointLight:	return GetPointLightColorScale();	
		case RPT_SpotLight:		return GetSpotLightColorScale();	

		default: RED_FATAL("Unknown type of light! Please service your light here.");
		}

		return 1.0f;
	}

	static RED_FORCE_INLINE Float GetColorModifier(ELightType lightType)
	{
		switch (lightType)
		{
		case LT_Point:			return GetPointLightColorScale();
		case LT_Area:			return GetPointLightColorScale();
		case LT_Spot:			return GetSpotLightColorScale();

		default: RED_FATAL("Unknown type of light! Please service your light here.");
		}

		return 1.0f;
	}

	static RED_FORCE_INLINE Box GetLightBoundingBox( const Transform& transform, ELightType lightType, Float radius, Float length )
	{
		Box box = Box::EMPTY();
		switch ( lightType )
		{
		case LT_Spot: // TODO: consider a tighter bbox if they become an issue
		case LT_Point:
			return Box( transform.GetPosition3(), radius );
		case LT_Area:
			box.AddPoint( transform.GetPosition3() + transform.GetOrientation() * Vector3( length / 2.0f, 0.0f, 0.0f ) );
			box.AddPoint( transform.GetPosition3() - transform.GetOrientation() * Vector3( length / 2.0f, 0.0f, 0.0f ) );
			box.Extrude( radius );
			return box;
		default: 
			RED_FATAL( "Unknown type of light! Please service your light here." );
			return box;
		}
	}

	template <typename SetupInfo>
	RED_FORCE_INLINE Float EstimateStreamingDistance( const SetupInfo* info, const math::Box& bbox, Float streamingFactor = 5.0f )
	{
		if( info )
			return info->m_streamingDistance;
		else
			return Max( bbox.CalcSize().Mag3() * streamingFactor, 4.0f );
	}
};

// --------------------

struct RENDER_DATA_API RenderProxyComponentInitData
{
	RenderProxyComponentInitData();
	virtual ~RenderProxyComponentInitData();

	Bool IsDrawableComponentInitData() const
	{
		const Bool isDrawable = RPT_SSDecal == m_type || RPT_Particles == m_type || RPT_Mesh == m_type || RPT_MorphTargetMesh == m_type;
		const Bool isNonDrawable = RPT_None == m_type || RPT_SpotLight == m_type || RPT_PointLight == m_type  || RPT_AreaLight == m_type || RPT_VectorField == m_type || 
			RPT_FogVolume == m_type || RPT_LightVolume == m_type || RPT_DynamicDecalSpawner == m_type || RPT_AutoSpawner == m_type || RPT_LightBlocker == m_type;
		RED_FATAL_ASSERT( !(isDrawable && isNonDrawable) && !(!isDrawable && !isNonDrawable), "Skipped something?" );
		return isDrawable;
	}

	RED_FORCE_INLINE Bool IsMeshTypeComponentInitData() const
	{
		return RPT_Mesh == m_type || RPT_MorphTargetMesh == m_type;
	}


	rend::RenderProxyTransform		m_localToWorld;
	Box								m_boundingBox;
	ERenderProxyType				m_type : 8;
	RenderSceneLayerMask			m_sceneLayerMask : 8;
	Uint8							m_constructorValidation;
};

struct RENDER_DATA_API RenderProxyLightComponentInitData : RenderProxyComponentInitData
{
	Vector3							m_secondaryReferencePoint;
	Float							m_radius;
	ELightUnit						m_unit;
	Float							m_intensity;
	Float							m_rayTracingIntensityScale;
	Float							m_EV;
	Color							m_colorGammaRGB;
	Float							m_colorTemperature;
	Float							m_autoHideDistance;				// auto hide distance for the primary reference point (m_localToWorld)
	Float							m_secondaryAutoHideDistance;	// auto hide distance for the secondary reference point (m_secondaryReferencePoint)
	TRenderPtr<IRenderResource>		m_projectionTexture;
	const rend::SLightFlickering*	m_flickering;
	Float							m_shadowFadeDistance;
	Float							m_shadowFadeRange;
	Float							m_shadowRadius;
	ELightShadowSoftnessMode		m_shadowSoftnessMode;
	Uint32							m_occlusionId;
	Uint16							m_iesProfileIdx;
	Uint16							m_channels;
	Uint8							m_group;
	Uint8							m_colorGroupSaturation;
	Uint8							m_getDynamicShadowsFaceMask;
	Int8							m_roughnessBias;
	EEnvColorGroup					m_envColorGroup;
	rend::ContactShadowReciever     m_contactShadows;
	Uint8							m_portalAngleCutoff;
	Bool                            m_enableLocalShadows : 1;
	Bool							m_enableLocalShadowsForceStaticsOnly : 1;
	Bool							m_useSecondaryReferencePoint : 1;	// enables visibility calculation using 2 reference points (and 2 auto hide distances)
	Bool							m_allowDistantLight : 1;
	Bool							m_hasTransformParent : 1;
	Bool							m_useSceneDiffuse : 1;
	Bool							m_useSceneSpecular : 1;
	Bool							m_useInGI : 1;
	Bool							m_useInFog : 1;
	Bool							m_directional : 1;
	Bool							m_clampAttenuation : 1;
	Bool							m_inverseSquareFallof : 1;
	Bool							m_useInEnvProbe : 1;
	Bool							m_useInTransparents : 1;
	Bool							m_useInParticles : 1;
	Bool							m_component : 1;
	
	RenderProxyLightComponentInitData();
	~RenderProxyLightComponentInitData();
};

struct RENDER_DATA_API RenderProxySpotLightComponentInitData : RenderProxyLightComponentInitData
{
	Float							m_innerAngle;
	Float							m_outerAngle;
	Float							m_shadowAngle;
	Float							m_softness;

	RenderProxySpotLightComponentInitData()
		: m_innerAngle( 30.0f )
		, m_outerAngle( 45.0f )
		, m_shadowAngle( 45.0f )
		, m_softness( 1.0f )
	{}

};

struct RENDER_DATA_API RenderProxyPointLightComponentInitData : RenderProxyLightComponentInitData
{
	RenderProxyPointLightComponentInitData()
	{}
};

struct RENDER_DATA_API RenderProxyAreaLightComponentInitData : RenderProxyLightComponentInitData
{
	Float							m_sourceRadius;
	Float							m_capsuleLength;
	Float							m_innerAngle;
	Float							m_outerAngle;
	Float							m_shadowAngle;
	Float							m_softness;
	EAreaLightShape					m_areaShape;
	Bool							m_areaTwoSided;
	Bool							m_spotCapsule;
	
	RenderProxyAreaLightComponentInitData()
		: m_areaShape( EAreaLightShape::ALS_Capsule )
		, m_areaTwoSided( true )
		, m_spotCapsule( false )
		, m_sourceRadius( 0.05f )
		, m_capsuleLength( 0.1f )
		, m_innerAngle( 30.0f )
		, m_outerAngle( 45.0f )
		, m_shadowAngle( 45.0f )
		, m_softness( 1.0f )
	{}

};

namespace envUtils
{
	enum class NeighborMode : Uint8;
	enum class ReflectionProbeAmbientContributionMode : Uint8;
}

struct RENDER_DATA_API RenderProxyDrawableComponentInitData : RenderProxyComponentInitData
{
	ERenderingPlane			m_renderingPlane : 8;
	Float					m_autoHideDistanceNear;
	Float					m_autoHideDistanceFar;
	Float					m_secondaryAutoHideDistance;
	Float					m_motionBlurMultiplier;
	Bool					m_isVisible : 1;
	Bool					m_isDynamicGeometryComponent : 1;
	Bool					m_isForceNoAutohide : 1;
	
	RenderProxyDrawableComponentInitData()
		: m_isVisible( true )
		, m_isDynamicGeometryComponent( false )
		, m_isForceNoAutohide( false )
		, m_autoHideDistanceNear( 0.0f )
		, m_autoHideDistanceFar( 75.0f )
		, m_secondaryAutoHideDistance( 75.0f )
		, m_motionBlurMultiplier( 1.0f )
		, m_renderingPlane( RPl_Scene )
	{ }
};

struct RENDER_DATA_API RenderProxyDecalComponentInitData : RenderProxyDrawableComponentInitData
{
	HDRColor m_diffuseColorScale = HDRColor( 1.0f, 1.0f, 1.0f, 1.0f );
	Vector4 m_effectScale;
	TRenderPtr<IRenderResource> m_materialRenderResource;
	Float m_normalThreshold;
	Float m_roughnessScale;
	Float m_alpha;
	Int32 m_frameOverride;
	Uint16 m_orderNo;
	EDecalRenderMode m_decalRenderMode;
	ERenderObjectType m_surfaceType;
	RenderDecalNormalsBlendingMode m_normalsBlendingMode;
	Bool m_horizontalFlip : 1;
	Bool m_verticalFlip : 1;
	Bool m_isStretchingEnabled : 1;
	Bool m_isNormalTresholdEnabled : 1;
	Bool m_shouldCollectWithRayTracing : 1;

	RenderProxyDecalComponentInitData();
	~RenderProxyDecalComponentInitData();
};

struct RENDER_DATA_API RenderProxyDynamicDecalSpawnerComponentInitData : RenderProxyDrawableComponentInitData
{
	TRenderPtr< IRenderResource >	m_materialRenderResource;
	Float							m_alpha;
	SDynamicDecalInitInfo			m_dynamicDecalInitInfo;

	RenderProxyDynamicDecalSpawnerComponentInitData();
	~RenderProxyDynamicDecalSpawnerComponentInitData();
};

struct RENDER_DATA_API RenderProxyParticleComponentInitData : RenderProxyDrawableComponentInitData
{
	struct Emitter
	{
		Bool					m_isEnabled;
		TRenderPtr<IRenderResource>	m_emitterResource;
		TRenderPtr<IRenderResource>	m_material;

		Emitter();
		~Emitter();
	};

	Emitter					m_systemEmitters[15];
	Box						m_systemGetBoundingBox;
	Float					m_systemGetPrewarmingTime;
	Uint32					m_systemGetRenderingPlane;				// actodo: make this a type
	Uint32					m_systemNumEmitters;
	Float					m_systemGetAutoHideDistance;
	Float					m_systemGetAutoHideRange;
	Float					m_systemFadeoutRange;
	Bool					m_hasParticleSystem : 1;
	Bool					m_systemIsVisibleThroughWalls : 1;
	Bool					m_systemIsBoundingBoxStatic : 1;
	Bool					m_triggerSimulation : 1;
	Bool					m_useCustomTimeDilation : 1;
#ifndef RED_CONFIGURATION_FINAL
	res::ResourcePath		m_resourcePath;
#endif

	RenderProxyParticleComponentInitData()
		: m_hasParticleSystem( false )
		, m_systemGetPrewarmingTime( 0.0f )
		, m_systemGetRenderingPlane( 0 )
		, m_systemIsVisibleThroughWalls( false )
		, m_systemIsBoundingBoxStatic( true )
		, m_systemGetBoundingBox( Box::EMPTY() )
		, m_systemNumEmitters( 0 )
		, m_systemGetAutoHideDistance( 20.0f )
		, m_systemGetAutoHideRange( 20.0f )
		, m_systemFadeoutRange( 10.0f )
		, m_triggerSimulation( false )
		, m_useCustomTimeDilation( false )
	{}

};

struct RENDER_DATA_API RenderProxyGIComponentInitData : RenderProxyDrawableComponentInitData
{
	red::ArraySpan<const Uint8>	m_dataBuffer;
	Vector3					m_location;

#ifndef RED_CONFIGURATION_FINAL
	res::ResourcePath		m_giBufferPath;
#endif // RED_CONFIGURATION_FINAL

	RenderProxyGIComponentInitData()
		: m_location{0,0,0}
	{}

	// Used to keep refcount of the CGIDataResource so the object is live until the end of the lambda
	THandle<CGIDataResource> m_dataResPtr;
};

struct RENDER_DATA_API RenderProxyLightChannelComponentInitData : RenderProxyDrawableComponentInitData
{
	red::DynArray< Vector3 >			m_vertices;
	red::DynArray< Uint16 >				m_indices;
	Uint8								m_channels;
	Bool								m_component;
	rend::LightChannelGroup				m_group;

	// - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -

	/// This is used to determine if given point is in light channel, with leaving the renderer clueless 
	/// about the actual engine-side implementations - lambda holds weak ptr to the instance.

	red::Function<Bool( const Vector4&, Float )> m_isPointInVolumeCallback;

	// - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -  - - - - - - - - -

	RenderProxyLightChannelComponentInitData();
};

struct RENDER_DATA_API RenderProxyEnvProbeComponentInitData : RenderProxyDrawableComponentInitData
{
	TResAsyncRef< CReflectionProbeDataResource >		m_dataBuffer;
	Vector3												m_edgeScale;
	Vector3												m_size;
	Vector3												m_referenceOffset;
	Vector3												m_fogColor;
	Float												m_fogDensity;
	Float												m_emissiveScale;
	Float												m_skyScale;
	Float												m_heightStreaming;
	Float												m_reflectionDimming;
	Float												m_blendRange;
	Uint16												m_lightChannels;
	Uint16												m_volumeChannels;
	Int8												m_brightnessEVClamp;
	Uint8												m_priority;
	envUtils::NeighborMode								m_neighborMode : 8;
	Bool												m_globalProbe : 1;
	Bool												m_volFogAmbient : 1;
	Bool												m_overrideGI : 1;
	Bool												m_canContributeAmbient : 1;
	Bool												m_allInShadow : 1;
	Bool												m_boxProjection : 1;
	Bool												m_noFadeBlend : 1;
	Bool												m_hideSkyColor : 1;
	Bool												m_subScene : 1;
	Bool												m_componentProbe : 1;

	RenderProxyEnvProbeComponentInitData()
		: m_lightChannels( 0 )
		, m_volumeChannels( 0 )
		, m_globalProbe( false )
		, m_overrideGI( false )
		, m_canContributeAmbient( false )
		, m_allInShadow( false )
		, m_volFogAmbient( true )
		, m_boxProjection( true )
		, m_noFadeBlend( false )
		, m_hideSkyColor( false )
		, m_subScene( false )
		, m_componentProbe( false )
		, m_edgeScale( 0.95f, 0.95f, 0.95f )
		, m_size( 1.0f, 1.0f, 1.0f )
		, m_referenceOffset( 0.0f, 0.0f, 0.0f )
		, m_fogColor( 1.0f, 1.0f, 1.0f )
		, m_fogDensity( 0.0f )
		, m_brightnessEVClamp( 8 )
		, m_emissiveScale( 1.0f )
		, m_reflectionDimming( 1.0f )
		, m_blendRange( 0.2f )
		, m_skyScale( 1.0f )
		, m_priority( 2 )
		, m_heightStreaming( -1 )
		, m_neighborMode( envUtils::NeighborMode( 0 ) )
	{}

};

struct RENDER_DATA_API RenderProxyVectorFieldComponentInitData : RenderProxyComponentInitData
{
	Float		m_autoHideDistance;
	Vector3		m_direction;
};

struct RENDER_DATA_API RenderProxyLightBlockerComponentInitData : RenderProxyComponentInitData
{
	Float		m_radius;
	Float		m_autohideDistance;

	RenderProxyLightBlockerComponentInitData()
		: m_radius( 0.f )
		, m_autohideDistance( 10.f )
	{}
};

struct RENDER_DATA_API RenderProxyFogVolumeComponentInitData : RenderProxyComponentInitData
{
	Float			m_density{1.0f};
	Float			m_blendFalloff{0.0f};
	Float			m_densityFalloff{0.0f};
	Float			m_ambientScale{1.0f};
	Float			m_absorption{-1.0f};
	Color			m_color{ Color::WHITE() };
	Uint16			m_lightChannels{ 0 };
	Uint8			m_priority{5};
	Bool			m_absolute{false};
	Bool			m_applyHeightFalloff{true};
	EEnvColorGroup	m_envColorGroup{ ECG_Default };
};

struct RENDER_DATA_API RenderProxyMeshComponentInitData : RenderProxyDrawableComponentInitData
{
	Vector3							m_secondaryReferencePoint;
	Uint32							m_LODLevelScales;
	TRenderPtr<IRenderResource>		m_renderMesh;
	TRenderPtr<IRenderResource>		m_materialSetup;
	TRenderPtr<IRenderResource>		m_materialOverlay;
	TRenderPtr<IRenderResource>		m_materialOverlayParam;
	TRenderPtr<IRenderSkinningData> m_skinningData;
	const AnsiChar*					m_componentFriendlyName;
	OrientedBox						m_mirrorCullingBox;
	// optional garment data begin
	red::DynArray< red::DynArray< Uint8 > > m_garmentMorphOffsetScales;		// Array of chunks, and array of vertex morph offset scales for each chunk.
	red::DynArray< Uint64>					m_garmentMorphOffsetScaleHashes; // Array of precomputed hashes for the m_garmentMorphOffsetScales array. 
	TRenderPtr< IRenderResource >			m_garmentVisibleTriangleIndexBuffer;	// @todo DP: keep texture directly? or at least arrays of Uint8
	// optional garment data end

	Uint16							m_destructionRegions = 0;
	Uint16							m_todVisMask{ 0 }; // Time of day visibility mask for cascades.
	Uint32							m_numVirtualInstances;
	Int32							m_forcedLODLevel; // -1 for automatic LOD (default)
	Int32							m_numClothLODs;
	Uint64							m_chunkMask;
	EMeshChunkFlags					m_additionalMeshChunkRenderMask;
	Uint8							m_setupsNum; // TODO remove
	Uint8							m_objectTypeID{ 0 };
	Uint8							m_order { 0 };
	ECustomCameraTarget				m_customCameraTarget;
	Bool							m_constrainAutoHideDistanceToTerrainHeightMap : 1;
	Bool							m_isCharacterShadowFallback : 1;
	Bool							m_hasMeshResource : 1;
	Bool							m_meshIsPrefabProxy : 1;
	Bool							m_meshCanMergeIntoGlobalShadowMesh : 1;
	Bool							m_useSecondaryReferencePoint : 1;
	Bool							m_forceStatic : 1;
	Bool							m_isCastingDynamicDistantShadow : 1;
	Bool							m_isCastingGlobalShadows : 1;
	Bool							m_isCastingLocalShadows : 1;
	Bool							m_isCastingShadowsEvenIfNotVisible : 1;
	Bool							m_isTwoSided : 1;
	Bool							m_initAsHidden : 1;
    Bool                            m_isDissolveVisible : 1;
	Bool							m_isTerrain : 1;
	Bool							m_isClusteredProxy : 1;
	Bool							m_isGeneratedByVegetationSystem : 1;
	Bool							m_materialOverride : 1;
#ifndef RED_CONFIGURATION_FINAL
	Bool							m_isGIVisible : 1;
	Bool							m_isGITransparent : 1;
#endif // RED_CONFIGURATION_FINAL
	Bool							m_HACK_isMaterialPriorityBumped : 1;
	Bool							m_skipDynamicDecals : 1;
	Bool							m_isCloth : 1;
	Bool							m_windImpulseEnabled : 1;
	Bool							m_removeFromRainMap : 1;
	Bool							m_calcLodDistFromBboxEdge : 1;
	
	RenderProxyMeshComponentInitData();
	~RenderProxyMeshComponentInitData();
};

struct RENDER_DATA_API RenderProxyMorphTargetSkinnedMeshComponentInitData : RenderProxyMeshComponentInitData
{
	red::StaticArray< TRenderPtr< IRenderResource >, MorphTargets::BTT_USED_MAX >	m_baseTexture { MorphTargets::BTT_USED_MAX }; //base texture for corresponding BlendedTextureType

	RenderProxyMorphTargetSkinnedMeshComponentInitData();
	~RenderProxyMorphTargetSkinnedMeshComponentInitData();
};

/// Render proxy initialization info
class RENDER_DATA_API RenderProxyInitInfo
{
public:

	RenderProxyComponentInitData*			m_componentData;		//!< Source component data

	RenderProxyInitInfo();
	~RenderProxyInitInfo();

	//! Extract world space bounding box
	Box ExtractBoundingBox() const;

	//! Extract local to world matrix
	rend::RenderProxyTransform ExtractLocalToWorld() const;

private:
#ifndef RED_CONFIGURATION_FINAL
	Bool m_initInfoStateCheck;
#endif
};
