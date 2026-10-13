/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderFrameInfo.h"
#include "renderer.h"
#include "renderDrawBuffer.h"
#include "captureHighResolutionScreenshot.h"
#include "meshEnum.h"
#include "viewport.h"
#include "../../../../engine/shaders/include/include_hud.fx"
#include "renderSettingsVars.h"
#include "../../redJobs2/include/jobBuilder.h"

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

CRenderFrameSetup::CRenderFrameSetup()
	: m_viewport( nullptr )
	, m_viewportWidth( 0 )
	, m_viewportHeight( 0 )
	, m_renderingMode( RM_Shaded )
	, m_framePurpose( FP_Normal )
	, m_present( false )
{}

CRenderFrameSetup::CRenderFrameSetup( Uint32 viewportWidth, Uint32 viewportHeight )
	: m_viewport( nullptr )
	, m_viewportWidth( viewportWidth )
	, m_viewportHeight( viewportHeight )
	, m_renderingMode( RM_Shaded )
	, m_framePurpose( FP_Normal )
	, m_present( false )
{}

CRenderFrameSetup::CRenderFrameSetup( IViewport* viewport, ERenderingMode mode )
	: m_viewport( viewport )
	, m_viewportWidth( viewport ? viewport->GetWidth() : 0 )
	, m_viewportHeight( viewport ? viewport->GetHeight() : 0 )
	, m_renderingMode( mode)
	, m_framePurpose( FP_Normal )
	, m_present( true )
{}

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

CRenderCameraFrameInfo::CRenderCameraFrameInfo( TDebugDrawer debugDrawer, const rend::DebugFilterMask& renderingMask /*= rend::GetDefaultFilters()*/ )
	: m_renderingMask( renderingMask )
	, m_clearColor( Color::BLACK() )
	, m_overrideClearColor( false )
	, m_depthCutDistance( 0.0f )
	, m_sceneLayer( RenderSceneLayer::Default )
	, m_debugDrawer( debugDrawer )
	, m_allowedFeatureFlags( AllowedRenderFeatureSet_DefaultOnScreen() )
	, m_renderScene( nullptr )
	, m_subPixelOffset( RED_FLT_MAX, RED_FLT_MAX )
	, m_subPixelIndex( 0 )
	, m_fovMultiplier( 1.0f )
	, m_generateMips( false )
	, m_useOcclusionCulling( true )
	, m_cullingBox(Box::EMPTY())
#ifndef RED_CONFIGURATION_FINAL
	, m_lightChannelsOccBuffPreviewRequested( false )
	, m_freezeShadowmaps( false )
	, m_showLightsWithShadowmapsFrusta( false )
	, m_shadowSliceShowVisibleTargetsMask( 0 )
#endif
{
	if ( !m_debugDrawer )
	{
		static TDebugDrawer sharedNullDrawer( rend::IDebugDrawer::CreateNullDrawer() );
		m_debugDrawer = sharedNullDrawer;
	}

	red::Memzero( &m_renderCameraDOF, sizeof( m_renderCameraDOF ) );
	red::Memzero( &m_frustum, sizeof( m_frustum ) );
}

CRenderCameraFrameInfo::CRenderCameraFrameInfo( const CRenderCameraFrameInfo& ) = default;
CRenderCameraFrameInfo::CRenderCameraFrameInfo( CRenderCameraFrameInfo&& ) = default;

CRenderCameraFrameInfo& CRenderCameraFrameInfo::operator=( const CRenderCameraFrameInfo& ) = default;
CRenderCameraFrameInfo& CRenderCameraFrameInfo::operator=( CRenderCameraFrameInfo&& ) = default;

CRenderCameraFrameInfo::~CRenderCameraFrameInfo() = default;


void CRenderCameraFrameInfo::SetRenderCamera( const CRenderCamera::CreationContext & creationContext )
{
	m_renderCamera.Set( creationContext );

	if ( m_renderCamera.GetWeaponPlaneDefinition() )
	{
		m_cameraRenderingPlaneDef[ RPl_Weapon ] = m_renderCamera.GetWeaponPlaneDefinitionShrPtr();
	}
}

void CRenderCameraFrameInfo::SetRenderCamera( const CRenderCamera & renderCamera )
{
	m_renderCamera = renderCamera;

	if ( m_renderCamera.GetWeaponPlaneDefinition() )
	{
		m_cameraRenderingPlaneDef[ RPl_Weapon ] = m_renderCamera.GetWeaponPlaneDefinitionShrPtr();
	}
}

const CRenderCamera & CRenderCameraFrameInfo::GetRenderCamera() const
{
	return m_renderCamera;
}

const SRenderingPlaneDef* CRenderCameraFrameInfo::GetRenderingPlaneDef( ERenderingPlane plane ) const
{
	return  m_cameraRenderingPlaneDef[plane].Get();
}

void CRenderCameraFrameInfo::SetRenderPlaneDefinition( ERenderingPlane plane, const SRenderingPlaneDef& newPlaneDefinition )
{
	if ( m_cameraRenderingPlaneDef[plane] == nullptr )
	{
		m_cameraRenderingPlaneDef[plane] = red::CreateSharedPtr< SRenderingPlaneDef >( newPlaneDefinition );
	}
	else
	{
		*( m_cameraRenderingPlaneDef[plane] ) = newPlaneDefinition;
	}
}

void CRenderCameraFrameInfo::DumpDebugDrawer_Async()
{
	job::Builder builder( job::Priority::RenderPath );
	builder.DispatchJob( "DestroyDebugDrawer", [debugDrawer = std::move( m_debugDrawer )]( const job::RunContext& )
	{
		// nothing to do, lambda destructor will destroy shared ptr
	} );
}

void CRenderFrameInfo::ProcessStagingSelectionData() const
{
	if ( m_selectionCaptureData && !m_selectionCaptureData->Empty() )
	{
		// prepare selected objects collection
		const Int32 w = m_selectionArea.Width();
		const Int32 h = m_selectionArea.Height();
		const Uint32 numPixels = w * h;

		for ( Uint32 pixel_i = 0; pixel_i < numPixels; ++pixel_i )
		{
			Color color = *( Color* ) ( m_selectionCaptureData->TypedData() + sizeof( Color ) * pixel_i );

			auto objId = IRenderSelectable::DecodeID( color ); // decode the colors back into the selection ID
			if ( objId != 0 )
			{
				const Int32 pixelPosX = m_selectionArea.m_left + ( pixel_i % w );
				const Int32 pixelPosY = m_selectionArea.m_top + ( pixel_i / w );
				m_selectionResult->AddSelectedObjectInfo( pixelPosX, pixelPosY, objId, 0.0f );
			}
		}

		m_selectionCaptureData->Clear();
	}

	// process multilayer selection
	if ( m_multilayerSelectionCaptureData && !m_multilayerSelectionCaptureData->Empty() )
	{
		Uint32 numPixels = *m_multilayerSelectionCaptureSizeData;

		if ( numPixels > m_multilayerSelectionCaptureData->Size() )
		{
			// handle append buffer overflow
			numPixels = m_multilayerSelectionCaptureData->Size();
		}

		for ( Uint32 i = 0; i < numPixels; ++i )
		{
			const rend::SelectionResultType::GPUBufferPixel& pixel = ( *m_multilayerSelectionCaptureData )[ i ];
			m_selectionResult->AddSelectedObjectInfo( pixel );
		}

		m_multilayerSelectionCaptureData->Clear();
	}

	if ( m_selectionResult )
	{
		m_selectionResult->ProcessData();
		m_selectionResult->Complete();
	}
}

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

CRenderFrameInfo::CRenderFrameInfo(const CRenderFrameSetup& setup)
	: m_viewport(setup.m_viewport)
	, m_renderingMode(setup.m_renderingMode)
	, m_framePurpose(setup.m_framePurpose)
	, m_present(setup.m_present)
	, m_worldMaxBounds(Box::EMPTY())
	, m_worldTime(0)
	, m_engineTime(0)
	, m_cleanEngineTime(0)
	, m_gameTime(0)
	, m_timeOfDay(12.0f)
	, m_gameDays(0)
	, m_particleTimeMultiplier(1.f)
	, m_backgroundTextureColorScale(1.0, 1.0, 1.0, 1.0)
	, m_frameTime(0)
	, m_requestedNumCascades(2)
	, m_lastFrameDT(1.0f / 30.0f)
	, m_windowContextName(setup.m_windowContextName)
	, m_cameraReferencePosition(Vector3::ZEROS())
{
	m_viewportWidth		= setup.m_viewportWidth;
	m_viewportHeight	= setup.m_viewportHeight;

	RED_FATAL_ASSERT( !(m_viewport && (m_viewportWidth != m_viewport->GetWidth() || m_viewportHeight != m_viewport->GetHeight())), "Viewport size not consistent with provided viewport" );

	m_frameTime = static_cast< Float >( red::Clock::GetInstance().GetTimer().GetSeconds() );

	m_distantShadowsRequestedNumLevels = 3;
	m_distantShadowsBaseLevelRadius = 250.0f;
}

CRenderFrameInfo::CRenderFrameInfo( CRenderFrameInfo&& ) = default;

CRenderFrameInfo::~CRenderFrameInfo()
{
}

CRenderFrameInfo& CRenderFrameInfo::operator=( CRenderFrameInfo&& ) = default;

GpuWrapApi::eHDRMode CRenderFrameInfo::GetHDRMode() const
{
	if ( GetFramePurpose() == FP_Normal )
	{
		if ( GetViewport() )
		{
			return GetViewport()->GetHDRMode();
		}
	}

	return GpuWrapApi::HDRMODE_None;
}

void CRenderFrameInfo::FinishConfigSetup()
{
	// Same values to clamp kernel size 
	{
		// [KN] Values are not random. They are kinda in-pair to average penumbra from sun when having shadows a meter from object.
		constexpr Float minKernelSize[] ={ 0.02f, 0.04f, 0.08f, 0.16f };
		constexpr Float maxKernelSize[] ={ 0.08f, 0.08f, 0.16f, 0.32f };

		for( Uint32 i = 0; i < MAX_CASCADES; ++i )
		{
			ShadowCascadeConfig& dst		= m_worldRenderSettings.m_shadows.m_cascade[i];

			dst.m_filterSize	= Clamp<Float>( dst.m_filterSize, minKernelSize[i], maxKernelSize[i] );
			dst.m_biasOffset	= Config::cvCascadeDepthBias[i].Get();
			dst.m_range			= Config::cvCascadeRange[i].Get();
		}
	}
}

void CRenderFrameInfo::SetLightingConfig( const WorldLightingConfig& params )
{
	m_worldRenderSettings.m_lightSettings.m_lightBrighthnessClamp = Max<Float>( params.m_lightAttenuationClamp, 1.0f );
}

void CRenderFrameInfo::SetShadowConfig( const WorldShadowConfig& params )
{
	// cascades
	m_requestedNumCascades = MAX_CASCADES;

	m_speedTreeParameters.m_foliageShadowCascadeGradient				= params.m_foliageShadowConfig.m_foliageShadowCascadeGradient;
	m_speedTreeParameters.m_foliageShadowCascadeFilterScale				= params.m_foliageShadowConfig.m_foliageShadowCascadeFilterScale;
	m_speedTreeParameters.m_foliageShadowCascadeGradientDistanceRange	= params.m_foliageShadowConfig.m_foliageShadowCascadeGradientDistanceRange;

	m_distantShadowsRequestedNumLevels			= Clamp<Uint32>( params.m_distantShadowsNumLevels, 0, MAX_DISTANT_SHADOWS_LEVELS );
	m_distantShadowsBaseLevelRadius				= params.m_distantShadowsBaseLevelRadius;

	m_worldRenderSettings.m_contactShadows		= params.m_contactShadows;
}

void CRenderFrameInfo::SetGameTime( Float gameTime, Uint32 gameDays )
{ 
	m_gameTime = gameTime; 
	m_gameDays = gameDays;
}

void CRenderFrameInfo::SetTimeOfDay(Float timeOfDay)
{
	m_timeOfDay = timeOfDay;
}

void CRenderFrameInfo::SetPrevTimeOfDay( Float prevTimeOfDay )
{
	m_prevTimeOfDay = prevTimeOfDay;
}

void CRenderFrameInfo::SetSimulationTime( Float simulationTime )
{
	m_simulationTime = simulationTime;
}

void CRenderFrameInfo::SetPrevSimulationTime( Float prevSimulationTime )
{
	m_prevSimulationTime = prevSimulationTime;
}

void CRenderFrameInfo::SetAccumulatedWind( Vector2 accumulatedWind )
{
	m_accumulatedWindOffset = accumulatedWind;
}

void CRenderFrameInfo::SetEngineTime( Float engineTime )
{
	m_engineTime = engineTime;
}

void CRenderFrameInfo::SetLastFrameDT( Float dt )
{
	m_lastFrameDT = dt;
}

void CRenderFrameInfo::SetCleanEngineTime( Float cleanEngineTime )
{
	m_cleanEngineTime = cleanEngineTime;
}

void CRenderFrameInfo::SetDayPointEnvParams( const DayPointEnvironmentParams& params )
{
	m_envParametersDayPoint = params;
}

void CRenderFrameInfo::SetDisplaySettingsParams( const CEnvDisplaySettingsParams& params )
{
	m_displaySettings = params;
}

void CRenderFrameInfo::SetBaseLightingParams( const CRenderBaseLightParams& params )
{
	m_baseLightingParameters = params;
}

void CRenderFrameInfo::SetSpeedTreeParams( const FoliageSettings& params )
{
	m_speedTreeParameters = params;
}

void CRenderFrameInfo::SetWorldRenderSettings( const WorldRenderSettings& params )
{
	m_worldRenderSettings = params;
}

void CRenderFrameInfo::SetFrameTime( Float frameWorldTime )
{
	m_worldTime = frameWorldTime;
}

void CRenderFrameInfo::SetScene( const TRenderPtr<IRenderScene>& scene )
{
	if( m_scene )
	{
		m_originalScene = m_scene;
	}

	m_scene = scene;
}

void CRenderFrameInfo::SetNonInteractiveScene( const TRenderPtr<IRenderScene>& scene)
{
	m_nonInteractiveScene = scene;
}

void GatherScenesForUpdate( red::DynArray<TRenderPtr<IRenderScene>>* scenes, const CRenderFrameInfo& frameInfo )
{
	if ( auto& scene = frameInfo.GetScenePtr() )
	{
		scenes->PushBack( scene );
	}

	if ( auto& scene = frameInfo.GetOriginalScenePtr() )
	{
		scenes->PushBack( scene );
	}

	if ( auto& scene = frameInfo.GetNonInteractiveScenePtr() )
	{
		scenes->PushBack( scene );
	}

	red::alg::RemoveDuplicates( *scenes );
}

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//

CRenderFrameGrab::CRenderFrameGrab()
	: m_viewportHeight( 0 )
	, m_viewportWidth( 0 )
	, m_selectionArea( 0, 0, 0, 0 )
	, m_selectionResult( nullptr )
	, m_depthArea( 0, 0, 0, 0 )
	, m_depthResult( nullptr )
	, m_colorResult( nullptr )
	, m_gBufferAlbedo8888Result( nullptr )
	, m_gBufferAlbedo1010102Result( nullptr )
	, m_giVolumeResult( nullptr )
	, m_gBufferNormalsResult( nullptr )
	, m_backgroundDump( nullptr )
	, m_gBufferMaterialResult( nullptr )
	, m_selectionCaptureData( red::CreateSharedPtr< red::DynArray< Uint8 >, rend::PoolImageProcessing >( red::DynArray< Uint8 >{ rend::PoolImageProcessing() } ) )
	, m_multilayerSelection( false )
	, m_multilayerSelectionCaptureSizeData( red::CreateSharedPtr< Uint32, rend::PoolImageProcessing >( 0 ) )
	, m_multilayerSelectionCaptureData( red::CreateSharedPtr< red::DynArray< rend::SelectionResultType::GPUBufferPixel >, rend::PoolImageProcessing >( red::DynArray< rend::SelectionResultType::GPUBufferPixel >( rend::PoolImageProcessing() )  ) )
{
}

CRenderFrameGrab::CRenderFrameGrab( const CRenderFrameGrab& ) = default;

CRenderFrameGrab::CRenderFrameGrab( CRenderFrameGrab&& ) = default;

CRenderFrameGrab& CRenderFrameGrab::operator=( const CRenderFrameGrab& ) = default;

CRenderFrameGrab& CRenderFrameGrab::operator=( CRenderFrameGrab&& ) = default;

CRenderFrameGrab::~CRenderFrameGrab() = default;

//******************************************************************************************************************************************//
//******************************************************************************************************************************************//
