#pragma once
#include "renderWindImpulseShape.h"
#include "renderPublicEnums.h"


//**********************************************************************************************//
//**********************************************************************************************//

namespace renderDev
{

	enum class SurfelView
	{
		ALBEDO,
		NORMAL,
		SHADOWS,
		CLOSEST_PROBE,
		EMISSIVE,
		LIGHTING,
		BOUNCE,
		INSIDE,
		SHADOW,
	};

	enum class GIProbeView
	{
		RADIANCE,
		SKY_VISIBILITY,
		ENV_ID,
		FLAG_0,
		FLAG_1,
		FLAG_2,
		CURRENT_ID,
	};

	enum GISectorStatus : Uint8
	{
		FAIL,
		SUCCESS,
		EXPERIMENTAL_BAKE,
		LOCAL_BAKE,
		LOCAL_EXPERIMENTAL_BAKE,
		EMPTY,
		MISSING
	};

	enum GIProbeToBrickView : Uint32
	{
		COLOR, 
		WEIGHT
	};

	enum class EnvProbeView
	{
		RADIANCE,
		ALBEDO,
		NORMAL,
		ROUGHNESS,
		METALNESS,
		EMISSIVE,
		SKY_MASK,
	};

	enum TXAADebugMode
	{
		TXAA_NoDebug,
		TXAA_ShowHistoryBlendFactor,
	};

	//**********************************************************************************************//

	struct RENDER_DATA_API SConfig
	{
		red::Atomic<SurfelView>		m_surfelView;
		red::Atomic<GIProbeView>	m_giProbeView;
		red::Atomic<EnvProbeView>	m_envProbeView;
		
		struct SConfigOverride 
		{
			Bool m_enabled { false }; 
			RED_INLINE Bool IsEnabled() const { return m_enabled; };
		};

		struct GIOverride : public SConfigOverride
		{
			Int32			m_id { 0 };
			Int32			m_faceId { 0 };
			Int32			m_res { 1 };
			// Weights
			Float			m_weightDistanceShadow{ 1.086f };
			Float			m_weightDistanceProbe{ 0.0f };
			Float			m_weightNormal{ 0.0f };
			Float			m_weightBias{ 0.0f };

			// Acceptance threshold
			Float			m_threshold{ 0.163f };
			Int32			m_volMin{ 0 };
			Int32			m_volMax{ 100 };

			Int32			m_maxSteps{ 16 };
			Bool			m_showSteps{ false };
			Bool			m_displayDebug{ false };
#ifndef RED_CONFIGURATION_FINAL
			GISectorStatus		m_currentSectorStatus = GISectorStatus::MISSING; // assume we are missing one by default
			Int32				m_probeToBrickView = GIProbeToBrickView::COLOR;
			
			red::String		m_currentSectorError;
			Uint64			m_timestamp = 0;

			Bool			m_hideGIInvisible{ false };
			Bool			m_hideGITransparent{ false };
			Bool			m_suspendCaptureClosestProbe{ false };
			Bool			m_showInteriorPixels{ false };
			Int32			m_closestProbeID{ 0 };
			Int32			m_closestProbeFaceID{ -1 };
			Int32			m_closestProbeIO{ -1 };
			Int32			m_closestProbeChannel{ -1 };
			Vector3			m_closestProbePos;
			
			Float			m_freeTetras{ 0.0f };
			Float			m_wastedTetras{ 0.0f };

			red::RWSpinLock				m_giSectorStringsLock;
			red::DynArray<red::String>	m_giSectorStrings{ red::PoolDebug() };
#endif // !RED_CONFIGURATION_FINAL
		} GlobalIllumination;

		struct DRSConfig : public SConfigOverride
		{
			Int32 m_targetFPS = 30;
			Float m_minHeightPercentage = 0.5f;
			Float m_maxHeightPercentage = 1.0f;
		} DRS;

		struct DebugPreviewConfig : public SConfigOverride
		{
			Bool			m_showSpecularAO{false};
			Bool			m_showShadowMaskContacts{false};
		} DebugPreview;

		struct FoliageOverride : public SConfigOverride
		{
			Float	m_translucencyScale { 1.0f };
		} Foliage;

		struct BrdfOverride : public SConfigOverride
		{
			Int32	m_brdfType{ 0 };
		} Brdf;

		struct TimeOfDayOverride : public SConfigOverride
		{
			Float	m_timeOfDay{12.0f};
		} TimeOfDay;

		struct ExposureOverride : public SConfigOverride
		{
			Uint32	m_iso { 100 };
			Float	m_shutterTime { 125.0f };
			Float	m_fStop { 8.0f };
		} Exposure;

		struct CameraSpyOn : public SConfigOverride
		{
			Bool largePreviews{ false };
		} CameraSpy;

		struct ShadowLightsQueueOverride : public SConfigOverride
		{
			int	m_perFrameCapacity { 3 };
			// Will be updated on the way. No need to include shadows stuff here I think.
			int	m_maxShadowSlices { 1 }; 
		} ShadowLightsQueue;

		struct DOFOverride : public SConfigOverride
		{
			Float	m_farFocus{ 5.0f };
			Float	m_nearFocus{ 3.0f };
			Float	m_farBlur{ 8.0f };
			Float	m_nearBlur{ 0.0001f };
			Float	m_dofIntensity{ 1.0f };
			Bool	m_useSquareBokeh{ false };
			Bool	m_enableDebugPreview{ false };
			Bool	m_useNearPlane{ true };
			Bool	m_useFarPlane{ true };
		} DOF;

		struct WeaponPlaneBlurOverride : public SConfigOverride
		{
			Float	m_weaponNearPlaneCM{ 10.0f };	//controls the end point of weapon
			Float	m_weaponFarPlaneCM{ 50.0f };	//controls the nearer (start) point of weapon
			Float	m_vignetteIntensity{ 0.5f };
			Float	m_vignetteRadius{ 0.6f };
			Float	m_vignetteCircular{ 0.0f };
			Float	m_blurIntensity{ 1.0f };
			Bool	m_weaponDebugPreview{ false };
		} WeaponPlaneBlur;

		struct CascadesTimeOfDayVisibilityOverride : public SConfigOverride
		{
			Bool m_onOff { true };
		} CascadesTODVis;

		struct LocalShadowsOverride : public SConfigOverride
		{
			Bool	m_pushLightChannelsToOcclusionBuffer { false };
			Bool	m_freezeShadowmaps { false };
			Bool	m_showFrusta { false };
			// Unfortunately ImGui does not accept address from a bitfield so I cannot do a mask.
			Bool	m_showShadowSliceVisibleBBoxes[ 10 ] = { false, false, false , false , false , false , false , false , false , false };
		} LocalShadows;

		struct WindImpulseVolumeOverride : public SConfigOverride
		{
			Bool  m_enableDebugShapeAtCameraPos{ false };
			Bool  m_snapOffsetAlongCameraForwardToXYPlane{ false };
			Bool  m_snapCameraPosToXYPlane{ false };
			Bool  m_freezeShapePosition{ false };
			rend::WindShapeAnchorPointVert m_anchorPointVert{ rend::WindShapeAnchorPointVert::AP_CENTER };
			rend::WindShapeAnchorPointHorz m_anchorPointHorz{ rend::WindShapeAnchorPointHorz::AP_CENTER };
			rend::WindShapeAnchorPointDepth m_anchorPointDepth{ rend::WindShapeAnchorPointDepth::AP_CENTER };
			Float m_offsetAlongCameraForward{ 4.0f };
			Bool  m_overrideShapesParams{ false };
			Float m_shapeRadius{ 1.0f };
			Float m_shapeHeight{ 0.0f };
			Float m_bendIntensity{ 1.0f };
			Float m_lifeTime{ 2.0f };
			Float m_centralDifferenceDenom{ 2.f };
			Float m_k{ 0.2f };
			Float m_v{ 0.73f };
			Float m_velocityLinDecay{ 0.001f };
			Float m_densityLinDecay{ 0.0254 };
			Float m_maxVelocity{ 8.f };
			Float m_minDensity{ 0.0f };
			Float m_maxDensity{ 1.6f };
			Float m_dt{ 0.016f };
		} WindImpulseVolume;

		struct ScreeshotParamsOverride : public SConfigOverride
		{
			Int32	m_saveFormat{ -1 };
			Int32	m_resolutionMultiplier{ 1 };
			Int32	m_resolution{ -1 };
			Bool	m_forceLod0{ true };
			Int32	m_horizontalShotsCount360{ 10 };
			Int32	m_verticalAngleStep360{ 36 };
		} ScreenshotParams;

		struct SSRParamsOverride : public SConfigOverride
		{
			// NOTE: Pixels with roughness values between m_maxRoughnesOfFullInfluence and m_minRoughnessForProbesFullInfluence 
			//		 will linearly blend its influences on lighting calculations.

			// Pixels with roughness values from 0.0 to m_maxRoughnesOfFullInfluence, will cause SSR to have full influence in lighting calculations.
			Float m_maxRoughnesOfFullInfluence { 0.6f };
			// Pixels with roughness values from m_minRoughnessForProbesFullInfluence to 1.0, will cause SSR to have no influence - all reflections 
			// in lighting calculations are taken from probes. They early exit the calculations.
			Float m_minRoughnessForProbesFullInfluence { 0.8f };

			// Controls fade out of rays facing camera
			Float m_backwardRaysFadeStart{ 0.4f };
			Float m_backwardRaysFadeEnd{ 0.8f };
		} SSR;

		struct TXAAOverride : public SConfigOverride
		{
			TXAADebugMode m_debugMode{ TXAA_NoDebug };
		} TXAA;

		struct CustomOverride : public SConfigOverride
		{
			red::Atomic<Bool>	m_renderToggle[ 4 ];
		} Custom;

		struct VisibilityBVHOverride : public SConfigOverride
		{
			Bool m_fullOptimalRebuild{ false };
			Float m_bvhObjectsVisualizationRadius{ 512.f };
			Int32 m_bvhLevelToVisualize{ -1 };
		} VisibilityBVH;

		struct MirrorOverride : public SConfigOverride
		{
			Float m_finalResolutionScale{ 1.0f };
			Float m_internalResolutionScale{ 1.0f };
		} Mirror;

		struct ResolutionUpscaling : public SConfigOverride
		{
			Float m_casScalingFactor{ 1.0f };
			Float m_casSharpeningFactor{ 0.0f };
		} ResolutionUpscaling;

		struct SceneLayersOverride : public SConfigOverride
		{
			Bool m_renderInMainView[RenderSceneLayer_MAX] { true, /* rest are zero-initialized */ };
			static_assert( Uint32(RenderSceneLayer::Default) == 0, "Expect Default scene layer to be index 0." );
		} SceneLayers;

		struct MeshProxyCollectFilter : public SConfigOverride
		{
			static constexpr Uint32 MaxNameLength = 256;
			char m_resourceName[ MaxNameLength ] = { 0 };
		} MeshProxyCollectFilter;

		struct CascadesDebug : public SConfigOverride
		{
			Bool m_globalOcclusionEnable { true };
			Bool m_globalOcclusionFreeze { false };
			Bool m_globalOcclusionShowDebugRough { false };
			Bool m_globalOcclusionShowDebugFine { false };
		} CascadesDebug;

#ifdef RED_PLATFORM_CONSOLE
		struct ConsoleSamplers : public SConfigOverride
		{
			Uint8 samplerMipClampRegionSize = 4;
			Uint8 samplerZClampRegionSize = 0;
			Uint8 samplerAnisoThreshold = 2;
			Float samplerAnisoBias = 12.0f / 32.0f;
		} ConsoleSamplers;
#endif

		// - - - - - - - - - - - - - - - - - - - - - - - - 

		SConfig();

		~SConfig();

		// - - - - - - - - - - - - - - - - - - - - - - - - 

	};

	//**********************************************************************************************//

	RENDER_DATA_API Float ApplyDevOverride( Float timeOfDay, SConfig* devConfig );

	class ICameraSpy
	{
	public:
		struct SEntry
		{
			SEntry();

			EFramePurpose framePurpose;
			red::DynArray< TRenderPtr<IRenderResource> > textures = { rend::PoolRenderDebug() };
		};

		virtual void StartSpying() = 0;
		virtual void StopSpying() = 0;
		virtual void GrabReport( red::DynArray< SEntry >& cameras ) = 0;
	};
}
