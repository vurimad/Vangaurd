#include "build.h"
#include "thumbnailRendering.h"
#include "environmentPresetsManager.h"

#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/renderData/include/renderScene.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../common/renderData/include/renderFrameInfo.h"
#include "../../common/engine/include/renderFrame.h"
#include "../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../common/worldEnvironment/include/envAreaParameters.h"
#include "../../common/worldEnvironment/include/environmentUtils.h"

namespace res
{
	//////////////////////////////////////////////////////////////////////////
	// ThumbnailRenderer
	//////////////////////////////////////////////////////////////////////////
	void ThumbnailPresentation::Reset()
	{
		forward = WorldTransform::IDENTITY().GetForward();
		position = Vector4::ZEROS();
		fov = DEFAULT_CAMERA_FOV;
	}

	Bool ThumbnailPresentation::IsOrthographic() const
	{
		return std::numeric_limits<Float>::epsilon() > fov;
	}

	Quaternion ThumbnailPresentation::GetCameraRotation() const
	{
		return LookAt( forward, up );
	}

	Quaternion ThumbnailPresentation::LookAt( const Vector3 &forward, const Vector3 &desiredUp )
	{
		const Vector3& worldUp = WorldTransform::IDENTITY().GetUp();
		const Vector3& worldForward = WorldTransform::IDENTITY().GetForward();
		const Vector4 direction = Vector3( forward ).Normalized();
		
		const Float dot = worldForward.Dot( direction );
		Vector3 rotAxis = worldForward.Cross( direction ).Normalized();
		if ( rotAxis.SquareMag() < std::numeric_limits<Float>().epsilon() )
		{
			rotAxis = worldUp;
		}
		const Quaternion toDir{ rotAxis, MAcos( dot ) };
		const Vector3 rotUp = toDir.Transform( worldUp ).Normalized();
		const Vector3 projUp = (desiredUp - forward * desiredUp.Dot( forward )).Normalized();
		const Float twist = MAcos(rotUp.Dot( projUp ));
		const float clockwise = 0 <= forward.Cross(projUp).Dot( rotUp ) ? -1.f : 1.f;
		return toDir * Quaternion( worldForward, clockwise*twist );
	}

	void ThumbnailPresentation::SetupLookAtRegionCamera( const Box &region, ThumbnailPresentation &inOutCamera, bool maxRegion )
	{
		const Vector4& lookAt = region.CalcCenter();
		const WorldTransform wt = WorldTransform::EIDENTITY;
		const Float fov = inOutCamera.IsOrthographic() ? ThumbnailPresentation::DEFAULT_CAMERA_FOV : inOutCamera.fov;
		const Float tanHalfFOV = MTan( DEG2RAD( fov * 0.5f ) );
		const Vector4 size = region.CalcSize();
		const Float zoom = maxRegion ? Max( size.X, Max( size.Y, size.Z ) ) : size.Mag3();
		const Float distance = 0.6f * zoom / tanHalfFOV;
		const Vector4 pos = lookAt - (inOutCamera.forward * distance);
		inOutCamera.position = pos;
		inOutCamera.zoom = inOutCamera.IsOrthographic() ? zoom : 1.f;
	}

	//////////////////////////////////////////////////////////////////////////
	// ThumbnailRenderer
	//////////////////////////////////////////////////////////////////////////
	ThumbnailRenderer::ThumbnailRenderer( Uint32 width, Uint32 height )
		: m_width( width )
		, m_height( height )
		, m_useAlphaCut( true )
		, m_clipArea( math::Rect::EMPTY() )
	{}

	ThumbnailRenderer::~ThumbnailRenderer()
	{}

	void ThumbnailRenderer::Render( red::SharedPtr<rend::ColorResult>& colorResult, const ThumbnailPresentation& presentation, RenderFunc func )
	{
		if ( m_width == 0 || m_height == 0 )
		{
			RED_LOG_WARNING( "Generation failed" );
			return;
		}

		// camera parameters
		const Float cameraAspect = static_cast<Float>(m_width) / static_cast<Float>(m_height);

		// prepare camera
		CRenderCamera::CreationContext context;
		{
			context.position = WorldPosition(presentation.position);
			context.rotation = presentation.GetCameraRotation();
			context.fov = presentation.fov;
			context.zoom = presentation.zoom;
			context.aspect = cameraAspect;
		}
		CRenderCamera frameCamera( context );

		red::SharedPtr<rend::DepthResult> dephResult( RED_NEW( rend::DepthResult )( red::PoolBackend() ) );
		
		CRenderFrameSetup setup( m_width, m_height );
		
		const bool disableBackFaceCulling = presentation.IsOrthographic();
		setup.m_framePurpose = disableBackFaceCulling ? FP_CascadesBaking : FP_Normal;

		CRenderFrameInfo frameInfo( setup );
		CRenderCameraFrameInfo cameraInfo;
		frameInfo.SetEngineTime( 0.0f );
		frameInfo.SetCleanEngineTime( 0.0f );
		frameInfo.m_colorResult = colorResult;
		frameInfo.m_allowMeshDissolve		= false;

		if( m_useAlphaCut )
		{
			frameInfo.m_depthResult	= dephResult;
			frameInfo.m_depthArea	= Rect( 0, m_width, 0, m_height );
		}

		// insert custom rendering
		if ( func )
		{
			func( frameInfo );
		}

		// perform rendering
		{
			const Uint64 cameraName = 666;

			frameInfo.IssueCamera( cameraName );

			auto scene = frameInfo.GetScenePtr();

			GRender->GetCommands()->RegisterCamera( scene, frameCamera, SCameraRenderViewport::Fullscreen(), cameraName );
			GRender->GetCommands()->UpdateCameraData( scene, cameraName, cameraInfo );

			GRender->GetCommands()->FrameTick( red::ArraySpan<TRenderPtr<IRenderScene>>( &scene, 1 ) );

			auto frame = TRenderPtr<CRenderFrame>( RED_NEW( CRenderFrame )( std::forward< CRenderFrameInfo >( frameInfo ) ) );

			GRender->GetCommands()->RenderScene( frame );
			GRender->GetCommands()->UnregisterCamera( scene, cameraName );
			GRender->Flush();
		}

		const Uint32 W = colorResult->GetWidth();
		const Uint32 H = colorResult->GetHeight();

		if( m_useAlphaCut )
		{
			const Float* depth = &dephResult->GetData()[0];
			const Float transparent = *depth; // HACK, depth keeps changing between reverseProjection and normal, lets just assume the first pixel is transparent.

			struct RGBA { Uint8 r, g, b, a; } *rgba = (RGBA*)colorResult->GetWriteData();

			for( Uint32 i = 0, endI = colorResult->GetWidth() * colorResult->GetHeight(); i < endI; ++i, ++depth, ++rgba )
			{
				rgba->a = *depth == transparent ? 0 : 255;
			}
		}

		if( !m_clipArea.IsEmpty() )
		{
			struct RGBA { Uint8 r, g, b, a; } *rgba = (RGBA*)colorResult->GetWriteData();

			for( Uint32 y = 0; y < H; ++y )
			{
				for( Uint32 x = 0; x < W; ++x, ++rgba )
				{
					if( !m_clipArea.Contains( math::Point{ (Int32)x, (Int32)y } ) )
					{
						rgba->a = 0;
					}
				}
			}
		}

		RED_FATAL_ASSERT( m_width == colorResult->GetWidth() );
		RED_FATAL_ASSERT( m_height == colorResult->GetHeight() );
	}

	//////////////////////////////////////////////////////////////////////////
	// ThumbnailRenderingManager
	//////////////////////////////////////////////////////////////////////////
	static const String s_defaultEnvPreset( "gray" );

	ThumbnailRenderingManager::ThumbnailRenderingManager( Uint32 size )
		: m_renderer( red::CreateUniquePtr< ThumbnailRenderer >( size, size ) )
		, m_scene( GRender->CreateScene( IRenderScene::Type::THUMBNAIL ) )
		, m_renderProxy( nullptr )
		, m_presetName( s_defaultEnvPreset )
		, m_onlyOverlayMode( false )
	{
	}

	ThumbnailRenderingManager::~ThumbnailRenderingManager()
	{
		m_renderer.Reset();

		ReleaseRenderProxy();
	}

	void ThumbnailRenderingManager::ReleaseRenderProxy()
	{
		if ( m_renderProxy )
		{
			if ( m_scene )
				GRender->GetCommands()->RemoveProxyFromScene( m_scene, m_renderProxy );

			m_renderProxy = nullptr;
		}
	}

	void ThumbnailRenderingManager::Render( red::SharedPtr<rend::ColorResult>& colorResult, tools::EnvironmentPresetsManager& envPresetManager )
	{
		m_renderer->Render( colorResult, m_presentation,
			[this, &envPresetManager]( CRenderFrameInfo& frame )
		{
			OnRender( frame, envPresetManager );
		} );
	}

	void ThumbnailRenderingManager::SetupProxy( RenderProxyComponentInitData& initData )
	{
		m_renderer->SetAlphaCut( true );

		if ( !m_scene )
			return;

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &initData;

		// create proxy
		if ( auto newProxy = GRender->CreateProxy( initInfo ) )
		{
			m_renderProxy = newProxy;
			GRender->GetCommands()->AddProxyToScene( m_scene, m_renderProxy );
		}
	}

	void ThumbnailRenderingManager::SetupTexture( const CBitmapTexture* texture )
	{
		m_onlyOverlayMode = true;

		if ( !texture->GetImage() )
		{
			return;
		}

		auto texSize = texture->GetImage()->GetSize();

		{
			math::Rect clipArea;

			Int32 rW = m_renderer->GetWidth();
			Int32 rH = m_renderer->GetHeight();

			Float ratio = Min( rW / Float(texSize.m_width), rH / Float(texSize.m_height) );

			Int32 clipW = (Int32)(texSize.m_width * ratio);
			Int32 clipH = (Int32)(texSize.m_height * ratio);

			clipArea.m_left = ( rW - clipW ) / 2;	clipArea.m_right	= clipArea.m_left + clipW;
			clipArea.m_top	= ( rH - clipH ) / 2;	clipArea.m_bottom	= clipArea.m_top  + clipH;

			m_renderer->SetClipArea( clipArea );
		}

		SetEnvironmentPreset( String::EMPTY() );
		Uint8 numChannels = texSize.m_channels;
		Uint8 channelMask = (1 << numChannels ) - 1;

		IRenderCommandHandler::DebugTexturePreviewDesc previewDesc;

		previewDesc.numChannels		= texSize.m_channels;
		previewDesc.channelMask		= channelMask;

		GRender->GetCommands()->SendDebugTexturePreview( m_scene, texture->GetRenderResource(), previewDesc );
	}

	void ThumbnailRenderingManager::SetupCamera( const ThumbnailPresentation& presentation )
	{
		RED_FATAL_ASSERT( !std::isnan( presentation.position.X ) && std::isfinite( presentation.position.X ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.position.Y ) && std::isfinite( presentation.position.Y ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.position.Z ) && std::isfinite( presentation.position.Z ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.forward.X ) && std::isfinite( presentation.forward.X ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.forward.Y ) && std::isfinite( presentation.forward.Y ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.forward.Z ) && std::isfinite( presentation.forward.Z ) );
		RED_FATAL_ASSERT( !std::isnan( presentation.fov ) && std::isfinite( presentation.fov ) );

		m_presentation = presentation;
	}

	void ThumbnailRenderingManager::SetEnvironmentPreset( const String& presetName )
	{
		m_renderer->SetAlphaCut( false );
		m_presetName = presetName;
	}

	void ThumbnailRenderingManager::OnRender( CRenderFrameInfo& frame, tools::EnvironmentPresetsManager& envPresetManager )
	{
		SetupEnvironment( frame, envPresetManager );
		frame.SetScene( m_scene );
	}

	void ThumbnailRenderingManager::SetupEnvironment( CRenderFrameInfo& frame, tools::EnvironmentPresetsManager& envPresetManager ) const
	{
		{
			frame.SetRenderingMode(m_onlyOverlayMode ? RM_OverlayOnly : RM_Shaded );
		}

		DayPointEnvironmentParams& envDayPointParams = frame.m_envParametersDayPoint;
		tools::EnvironmentPresetsManager::SPreviewPreset preset;

		if ( envPresetManager.GetEnvModePreset( m_presetName, preset ) )
		{
			// Setup override cubemap
			envDayPointParams.m_forcedEnvProbeDataSource	= preset.m_cubemap ? preset.m_cubemap->GetRenderResource() : nullptr;

			// Setup light direction from HDR metadata
			frame.m_baseLightingParameters.m_lightDirection = preset.m_lightDir;
			frame.m_baseLightingParameters.m_lightColor		= preset.m_lightColor;
		}
		else
		{
			envDayPointParams.m_forcedEnvProbeDataSource = nullptr;
		}

		EulerAngles angles( 0, 0, 0 );
		envDayPointParams.m_forcedEnvProbeRotation = angles.ToMatrix();

		world::envUtils::CreateBaseSet( frame, nullptr, 12.0f );

		// Override light direction
		EulerAngles lightBaseAngles( 0.f, 30.f, 0.f );
		const Vector4 sunDirection			= BuildCustomSunDirection( lightBaseAngles );

		// light parameters
		CRenderBaseLightParams& baseLightParams = frame.m_baseLightingParameters;
		baseLightParams.m_sunDirection		= sunDirection;
		baseLightParams.m_moonDirection		= sunDirection;
		baseLightParams.m_lightDirection	= sunDirection;

		// auto& ambientParams = frame.m_worldRenderSettings.m_ambientOverride;
		// ambientParams.m_enabled = true;
		// for( auto& it : ambientParams.m_color ) it = HDRColor::WHITE();

		world::envUtils::FinalizeBaseSet( frame );
	}

	Vector4 ThumbnailRenderingManager::BuildCustomSunDirection( const EulerAngles& lightBaseAngles ) const
	{
		EulerAngles angles( 0, 0, 0 );
		const Float lightRotation = 60.0f;

		angles += lightBaseAngles;
		angles.Yaw += lightRotation;

		return -angles.ToMatrix().TransformVector( -Vector4::EY() );
	}
}