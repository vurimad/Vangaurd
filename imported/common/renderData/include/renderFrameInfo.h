/**
* Copyright (c) 2007-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderCamera.h"
#include "envParameters.h"
#include "environmentGameParams.h"
#include "drawer.h"
#include "debugFilterMask.h"
#include "renderCamera.h"
#include "renderFeatureFlags.h"
#include "../../redReflection/include/frustum.h"
#include "../../redImage/include/image.h"

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

namespace rend
{
	class IDebugDrawer;
	class SelectionResult;
	class DepthResult;
	class ColorResult;
	class Color10Result;
	class HdrResult;
	class DrawBuffer;
	using DrawBufferPtr = red::SharedPtr<DrawBuffer>;
	class IColorCapture;
	class CCaptureHighResolutionScreenshot;
	class GIVolumeResult;

namespace SelectionResultType
{
	struct GPUBufferPixel;
}

} // rend

namespace fx
{
	struct CompositionShaderParams;
}

class IRenderScene;
class DynamicTexture;
struct ImDrawData;

// Debug preview settings
struct RENDER_DATA_API SDebugPreviewSettings
{
	SDebugPreviewSettings ()
	{
		Reset();
	}

	void Reset()
	{
		m_pointedPixelX = -1;
		m_pointedPixelY = -1;
		m_zoomOffsetX = 0;
		m_zoomOffsetY = 0;
		m_zoomDiv = 1;
	}

	Bool IsZoomEnabled() const
	{
		return m_zoomDiv > 1 || 0 != m_zoomOffsetX || 0 != m_zoomOffsetY;
	}

	Int32	m_pointedPixelX;
	Int32	m_pointedPixelY;
	Float	m_zoomOffsetX;
	Float	m_zoomOffsetY;
	Float	m_zoomDiv;
};

struct RENDER_DATA_API RenderCameraDOFData
{
	Float	m_dofIntensity{ 0.0f };
	Float	m_dofNearBlur{ 0.0f };
	Float	m_dofNearFocus{ 0.0f };
	Float	m_dofFarBlur{ 0.0f };
	Float	m_dofFarFocus{ 0.0f };
	Bool	m_useNearPlane{ true };
	Bool	m_useFarPlane{ true };

	// For now both are accessible only from ImGui panel.
	Bool	m_enableSquareBokeh{ false };
	Bool	m_enableDebugPreview{ false };
};

// structure with ImGui drawing commands
#ifdef RED_USE_IMGUI
struct RENDER_DATA_API ImGuiDrawCommandList
{
	struct Command
	{
		TRenderPtr< IRenderResource > texture;
		Rect clippingRect;
		Uint32 indexCount;
		Uint32 indexBufferOffset;
	};

	red::DynArray< DebugVertexUV > vertices{ rend::PoolImGui() };
	red::DynArray< Uint32 > indices{ rend::PoolImGui() };
	red::DynArray< Command > commands{ rend::PoolImGui() };
};
#endif


struct RENDER_DATA_API WaterData
{
	Float						m_waterDepth{ -1.0f };
	Float						m_waterLevel{ -10000.0f };
	Float						m_waterSize;
	Vector4						m_waterChopiness;
	Matrix						m_waterMatrix;
	TRenderPtr<IRenderResource>	m_waterFFT{ nullptr };
};


/// Helper class for setting up render frame
/// THOSE SETTINGS PASSED HERE WILL NEVER CHANGE once frame info was created
class RENDER_DATA_API CRenderFrameSetup
{
public:
	IViewport*						m_viewport;						// Target viewport ( not needed if rendering surfaces given )
	Uint32							m_viewportWidth;				// Viewport width
	Uint32							m_viewportHeight;				// Viewport height
	
	ERenderingMode					m_renderingMode;				// Rendering mode
	EFramePurpose					m_framePurpose;

	Bool							m_present;						// Swap to viewport's front buffer (only viewport)

	String							m_windowContextName;
public:
	CRenderFrameSetup();
	CRenderFrameSetup( Uint32 viewportWidth, Uint32 viewportHeight );
	CRenderFrameSetup( IViewport* viewport, ERenderingMode mode = RM_Shaded );
};

class RENDER_DATA_API CRenderCameraFrameInfo
{
public:

	typedef red::SharedPtr< rend::IDebugDrawer > TDebugDrawer;

public:

	Vector2									m_resolutionScale;				// Resolution scale
	math::Color								m_clearColor;
	Bool									m_overrideClearColor;			// enable custom background color
	Float									m_depthCutDistance;
	red::SharedPtr< SRenderingPlaneDef >	m_cameraRenderingPlaneDef[ ERenderingPlane::RPl_Max ];
	RenderCameraDOFData						m_renderCameraDOF;
	RenderSceneLayer						m_sceneLayer;

	TDebugDrawer							m_debugDrawer;					// debug geometry to render
	CFrustum								m_frustum;

	// Which features are allowed to be used when rendering this camera. The actual features used still depends on
	// current graphics settings, it's not possible to force something on with this that would otherwise be off. It's
	// strictly to turn off features that aren't wanted.
	RenderFeatureSet						m_allowedFeatureFlags;

	RenderFeatureSet						m_featureFlags;					// Used internally by renderer

	IRenderScene*							m_renderScene;					// If nullptr, only debug fragments will be drawn

	Vector2									m_subPixelOffset;
	Uint32									m_subPixelIndex;

	Float									m_fovMultiplier;

	Bool									m_generateMips;					// Should a mip chain be generated for the final render? Can be useful for remote cameras
	Bool									m_applyCascadesTODVis { true };	// Always apply by default. Kept here for ability to trigger it off for comparison purposes.
	WaterData								m_waterData;

	Bool									m_useOcclusionCulling { true };			// Should this camera apply culling via software occlusion rasterizer.

	Box										m_cullingBox;

#ifndef RED_CONFIGURATION_FINAL
	// NOTE: Control requested by Lighting Team. Most probably temporary until they pick parameters they like.
	Float									m_ssrMaxRoughnesOfFullInfluence{ -1.0f };
	Float									m_ssrMinRoughnessForProbesFullInfluence{ -1.0f };
	Float									m_ssrBackwardRaysFadeStart{ -1.0f };
	Float									m_ssrBackwardRaysFadeEnd{ -1.0f };

	Bool									m_lightChannelsOccBuffPreviewRequested;
	Bool									m_freezeShadowmaps;
	Bool									m_showLightsWithShadowmapsFrusta;
	Uint16									m_shadowSliceShowVisibleTargetsMask;
#endif

public:
	CRenderCameraFrameInfo( TDebugDrawer debugDrawer = TDebugDrawer(), const rend::DebugFilterMask& renderingMask = rend::GetDefaultGameFilters() );

	CRenderCameraFrameInfo( const CRenderCameraFrameInfo& );
	CRenderCameraFrameInfo( CRenderCameraFrameInfo&& );

	CRenderCameraFrameInfo& operator=( const CRenderCameraFrameInfo& );
	CRenderCameraFrameInfo& operator=( CRenderCameraFrameInfo&& );

	~CRenderCameraFrameInfo();

public:

	void SetRenderCamera( const CRenderCamera::CreationContext& creationContext );
	void SetRenderCamera( const CRenderCamera& renderCamera );
	const CRenderCamera& GetRenderCamera() const;

	RED_FORCE_INLINE Bool HasFeature( const ERenderFeatureFlags f ) const { return m_featureFlags.Get( f ); }
	RED_FORCE_INLINE const RenderFeatureSet& GetFeatures() const { return m_featureFlags; }

	//! Get rendering filters
	RED_FORCE_INLINE const rend::DebugFilterMask& GetRenderingMask() const { return m_renderingMask; }

	//! Get the debug drawer
	RED_FORCE_INLINE rend::IDebugDrawer& GetDebugDrawer() const { return *m_debugDrawer; }

	RED_FORCE_INLINE TDebugDrawer GetDebugDrawerPtr() const { return m_debugDrawer; }

	RED_FORCE_INLINE Float GetFOVMultiplier() const { return m_fovMultiplier; }

	const SRenderingPlaneDef* GetRenderingPlaneDef( ERenderingPlane plane ) const;
	void SetRenderPlaneDefinition( ERenderingPlane plane, const SRenderingPlaneDef& newPlaneDefinition );

	RED_FORCE_INLINE Bool HasSubpixelOffset() const { return m_subPixelOffset.X != RED_FLT_MAX && m_subPixelOffset.Y != RED_FLT_MAX; };

	void DumpDebugDrawer_Async();

private:

	rend::DebugFilterMask			m_renderingMask;				// Rendering debug mask
	CRenderCamera					m_renderCamera;

};

class WorldShadowConfig;

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

class RENDER_DATA_API CRenderFrameGrab
{
protected:

	Uint32 m_viewportWidth;
	Uint32 m_viewportHeight;

public:

	Bool m_multilayerSelection;
	Rect m_selectionArea;				// Position of selection area
	red::SharedPtr<rend::SelectionResult> m_selectionResult;			// Output selection buffer in case of a selection map rendering
	red::SharedPtr<red::DynArray<Uint8>, rend::PoolImageProcessing> m_selectionCaptureData;
	red::SharedPtr<Uint32, rend::PoolImageProcessing> m_multilayerSelectionCaptureSizeData;
	red::SharedPtr<red::DynArray<rend::SelectionResultType::GPUBufferPixel>, rend::PoolImageProcessing> m_multilayerSelectionCaptureData;

	Rect m_depthArea;	// Position of depth area
	red::SharedPtr<rend::DepthResult> m_depthResult;	// Output depth buffer

	red::SharedPtr<rend::IColorCapture> m_colorCaptureInterface;		
	red::SharedPtr<rend::ColorResult> m_colorResult;				// Output color result
	red::SharedPtr<rend::HdrResult> m_colorResultHDR;
	red::SharedPtr<rend::ColorResult> m_gBufferAlbedo8888Result;
	red::SharedPtr<rend::Color10Result> m_gBufferAlbedo1010102Result;
	red::SharedPtr<rend::Color10Result> m_gBufferNormalsResult;
	red::SharedPtr<rend::GIVolumeResult> m_giVolumeResult;  // Used for GI baking process only
	red::SharedPtr<rend::HdrResult> m_backgroundDump;
	red::SharedPtr<rend::ColorResult> m_gBufferMaterialResult;
	red::SharedPtr<rend::CCaptureHighResolutionScreenshot> m_highResoulutionScreenshotInterface;

	// - - - - - - - - - - - - - - - - - - - - 

	CRenderFrameGrab();
	CRenderFrameGrab( const CRenderFrameGrab& );
	CRenderFrameGrab( CRenderFrameGrab&& );

	CRenderFrameGrab& operator=( const CRenderFrameGrab& );
	CRenderFrameGrab& operator=( CRenderFrameGrab&& );

	~CRenderFrameGrab();

	// - - - - - - - - - - - - - - - - - - - - 

	//! Get viewport width
	RED_FORCE_INLINE Uint32 GetViewportWidth() const { return m_viewportWidth; }

	//! Get viewport height
	RED_FORCE_INLINE Uint32 GetViewportHeight() const { return m_viewportHeight; }

	// - - - - - - - - - - - - - - - - - - - - 

};


struct DebugHeatmapSetup
{
	enum Mode : Uint8
	{
		Mode_Volume,	// render as fully raymarched 3D texture
		Mode_Sphere,	// render as sphere shell (cross section only)
		Mode_Plane,		// render as plane (cross section only)		
	};

	TRenderPtr<IRenderResource> texture;

	Box box;		// used by Mode_Volume
	Sphere sphere;	// used by Mode_Sphere
	Plane plane;	// used by Mode_Plane

	Float density = 0.01f;
	Float alpha = 0.5f;
	Mode mode = Mode_Volume;
	Bool clipToGeometry = false;
};

// HDR calibration screen will get injected before tonemapping
struct CalibrationScreenInfo
{
	TRenderPtr<IRenderResource> texture;
	Rect fromTextureRect;
	Rect toRect;
};

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//
/// Information about frame to render
/// NOTE: this is not a thread safe class on it's own, all access to it should be protected
class RENDER_DATA_API CRenderFrameInfo : public CRenderFrameGrab, red::NonCopyable
{
	RED_USE_MEMORY_POOL( rend::PoolFrameInfo );
public:

	//! Get shadow distance
	RED_INLINE Float GetCascadeDistance( Uint32 cascadeIndex ) const 
	{
		const auto& sh = m_worldRenderSettings.m_shadows;
		RED_ASSERT( cascadeIndex < m_requestedNumCascades ); 
		RED_ASSERT( !(cascadeIndex > 0 && sh.m_cascade[cascadeIndex-1].m_range >= sh.m_cascade[cascadeIndex].m_range) );
		return sh.m_cascade[cascadeIndex].m_range;
	}

	//! Get assigned render scene
	RED_FORCE_INLINE IRenderScene* GetScene() const { return m_scene.Get(); }
	RED_FORCE_INLINE IRenderScene* GetOriginalScene() const { return m_originalScene.Get(); }
	RED_FORCE_INLINE IRenderScene* GetNonInteractiveScene() const { return m_nonInteractiveScene.Get(); }

	RED_FORCE_INLINE const TRenderPtr<IRenderScene>& GetScenePtr() const { return m_scene; }
	RED_FORCE_INLINE const TRenderPtr<IRenderScene>& GetOriginalScenePtr() const { return m_originalScene; }
	RED_FORCE_INLINE const TRenderPtr<IRenderScene>& GetNonInteractiveScenePtr() const { return m_nonInteractiveScene; }

	// check HDR mode based on viewport settings and frame purpose
	GpuWrapApi::eHDRMode GetHDRMode() const;

	//! If the viewport is our target get it viewport
	RED_FORCE_INLINE IViewport* GetViewport() const { return m_viewport; }

	RED_FORCE_INLINE const String& GetWindowContextName() const { return m_windowContextName; }

	//! Get selected rendering mode
	RED_FORCE_INLINE const ERenderingMode GetRenderingMode() const { return m_renderingMode; }
	RED_FORCE_INLINE void SetRenderingMode( ERenderingMode val ) { m_renderingMode = val; }

	RED_FORCE_INLINE const EFramePurpose GetFramePurpose() const { return m_framePurpose; }

	RED_FORCE_INLINE void SetFramePurpose( EFramePurpose newPurpose ) { m_framePurpose = newPurpose; }

	// Issue new camera. Camera with existing name will be overwritten
	RED_INLINE void IssueCamera( Uint64 hashName ) { red::alg::PushBackUnique( m_cameras, hashName ); }

	void ProcessStagingSelectionData() const;

public:
	CRenderFrameInfo( CRenderFrameInfo&& );
	explicit CRenderFrameInfo( const CRenderFrameSetup& setup = CRenderFrameSetup() );
	~CRenderFrameInfo();

	CRenderFrameInfo& operator=( CRenderFrameInfo&& );

	// Called when frame setup is finished. It will do data checks and validation
	void FinishConfigSetup();

	// Initialize frame shadow info from world shadow info
	void SetShadowConfig( const WorldShadowConfig& params );
	
	// Initialize frame lighting info from world lighting info
	void SetLightingConfig( const WorldLightingConfig& params );

	// Set day point environment parameters
	void SetDayPointEnvParams( const DayPointEnvironmentParams& params );

	// Set game environment parameters
	void SetDisplaySettingsParams( const CEnvDisplaySettingsParams& params );

	// Set base lighting
	void SetBaseLightingParams( const CRenderBaseLightParams& params );
		
	// Set speedtree parameters
	void SetSpeedTreeParams( const FoliageSettings& params );

	// Set world render settings
	void SetWorldRenderSettings( const WorldRenderSettings& params );

	// Set world time at which this frame was generated
	void SetFrameTime( Float frameWorldTime );

	// Set true engine time at which this frame was generated
	void SetEngineTime( Float engineTime );

	// Set delta time for last frame
	void SetLastFrameDT( Float dt );

	// Set clean engine time at which this frame was generated
	void SetCleanEngineTime( Float cleanEngineTime );

	// Set game time
	void SetGameTime( Float gameTime, Uint32 gameDays );

	// Set time of day
	void SetTimeOfDay( Float timeOfDay );

	// Set previous time of day
	void SetPrevTimeOfDay( Float prevTimeOfday );

	// Set simulation time (engine/game time with time dilation)
	void SetSimulationTime( Float simulationTime );

	// Set previous simulation time (engine/game time with time dilation)
	void SetPrevSimulationTime( Float prevSimulationTime );

	// Set accumulated wind ( mainly for shader use )
	void SetAccumulatedWind( Vector2 accumulatedWind );

	// Attach rendering scene
	void SetScene( const TRenderPtr<IRenderScene>& scene );
	void SetNonInteractiveScene( const TRenderPtr<IRenderScene>& scene );

public:	
	Box								m_worldMaxBounds;				// Maximum bounds of the world. Might not be tight-fitting

	red::DynArray<Uint64>						m_cameras{ rend::PoolFrameInfo() };
	Bool										m_present;						// Should we present the frame
	CRenderCameraFrameInfo::TDebugDrawer		m_nonCameraDebugDrawer;
	rend::DrawBufferPtr				m_composition;

	CalibrationScreenInfo			m_calibrationScreenInfo;

	Float							m_worldTime;					// Current world time at the moment this frame was generated
	Float							m_frameTime;					// Frame time for shader constants
	Float							m_engineTime;					// True engine time
	Float							m_cleanEngineTime;				// Clean engine time (zeroed with each game session start)
	Float							m_lastFrameDT;					// Delta time for last frame
	Float							m_gameTime;						// Game time - #TODO: make sure it converts to time of day range
	Uint32							m_gameDays;						// Game days
	Float							m_timeOfDay;					// Time of day [0, 24]
	Float							m_prevTimeOfDay;
	Float							m_simulationTime;
	Float							m_prevSimulationTime;
	Float							m_particleTimeMultiplier;		// 0 - stop simulation, 1 - run simulation

	Vector2							m_accumulatedWindOffset;

	SDebugPreviewSettings			m_debugPreviewSettings;			// Debug preview settings

	DayPointEnvironmentParams		m_envParametersDayPoint;		// World environment parameters including wind / weather conditions

	CEnvDisplaySettingsParams		m_displaySettings;
	Vector4							m_backgroundTextureColorScale;	// Scaling of the color for texture background

	Vector3							m_cameraReferencePosition;		// Main camera view position

	FoliageSettings					m_speedTreeParameters;			// Speed tree parameters
	CRenderBaseLightParams			m_baseLightingParameters;		// Parameters of base scene lighting
	WorldRenderSettings				m_worldRenderSettings;			// World render settings

	Uint32							m_requestedNumCascades;			// Number of shadow cascades for global shadows ( at least one )

	Uint32							m_distantShadowsRequestedNumLevels;
	Float							m_distantShadowsBaseLevelRadius;

    Bool                            m_enableDebugMeshColoring = false;
	Bool							m_systemOnRenderDebug = true;
	Bool							m_enableVisibleMeshesCollection = false;
	Bool							m_isTimeShiftGlitchPrevention = false;
	Bool							m_allowMeshDissolve = true;
	
	Bool							m_stalledFrameForDebug = false; // Is this a frame to render some debug information because the normal scene frame is otherwise stalled

	Bool							m_allowGameSaveThumbnail = true;

#ifdef RED_USE_IMGUI
	red::DynArray< ImGuiDrawCommandList > m_imGuiDrawCommandList { rend::PoolImGui() };
#endif

#ifndef RED_CONFIGURATION_FINAL
	DebugHeatmapSetup				m_debugHeatmap;
#endif

	// #TODO: This, m_present, and CRenderFrameGrab could all be encapsulated inside the DynamicTexture mechanism...
	// have a special DynamicTexture representing a viewport/swapchain. Have another than captures multiple planes and
	// allows CPU readback. And anything else we might want.
	TRenderPtr<IRenderResource> m_dynamicTextureTargetFinal;
	TRenderPtr<IRenderResource> m_dynamicTextureTargetGBuffer0;
	TRenderPtr<IRenderResource> m_dynamicTextureTargetGBuffer1;
	TRenderPtr<IRenderResource> m_dynamicTextureTargetDepth;
	TRenderPtr<IRenderResource> m_dynamicTextureTargetParticles; // hack for 3D map / cyberspace, because we don't have proper "compute materials"

	red::UniquePtr< fx::CompositionShaderParams > m_compositionShaderParamsOverride;

private:
	IViewport*						m_viewport;						// Target viewport ( not needed if rendering surfaces given )
	
	ERenderingMode					m_renderingMode;				// Rendering mode
	EFramePurpose					m_framePurpose;

	TRenderPtr<IRenderScene>		m_scene;						// scene to render
	TRenderPtr<IRenderScene>		m_originalScene; // ctremblay: Scene Preview override the render scene, but update jobs are pushing to original one. I need to flush state also!
	TRenderPtr<IRenderScene>		m_nonInteractiveScene; // scene for update during non-interactive rendering, m_scene should be nullptr if this is set

	String							m_windowContextName; //Copy of from EngineViewport::m_windowContextName
};

RENDER_DATA_API void GatherScenesForUpdate( red::DynArray<TRenderPtr<IRenderScene>>* scenes, const CRenderFrameInfo& frameInfo );

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//
