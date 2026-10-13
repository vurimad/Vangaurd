/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderingWindow.h"
#include "renderingWindowCamera.h"

#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/engine/include/renderEngineViewport.h"
#include "../../../common/engine/include/tickInfo.h"
#include "../../../common/engine/include/externalMessage.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/input/include/inputSystemMessages.h"
#include "../../../common/input/include/inputContext.h"
#include "../../../common/renderData/include/viewport.h"
#include "previewPanelUtils.h"

namespace Config
{
	TConfigVar< Int32 > cvEditorCameraAxesLength( "Editor/Preview", "CameraAxesLength", 40 );
}

namespace tools
{
	UpdateContext::UpdateContext( Float timeDelta, const tick::Info& tickInfo )
		: m_timeDelta( timeDelta )
		, m_tickInfo( tickInfo )
	{}

	RenderContext::RenderContext( CRenderFrameInfo& frameInfo, CRenderCameraFrameInfo& cameraInfo, const class CRenderCamera* camera )
		: m_frameInfo( frameInfo )
		, m_cameraInfo( cameraInfo )
		, m_camera( camera )
	{}


	RenderWindowFrame::RenderWindowFrame( const UpdateContext* updateCtx, RenderContext& renderCtx )
		: m_updateCtx( updateCtx )
		, m_renderCtx( renderCtx )
	{}

	Uint64 g_cameraID = 0;

	RenderingWindow::RenderingWindow( rend::EngineViewportPtr viewport, RenderWindowCloseFunc closeFunc )
		: m_externalCloseFunc( closeFunc )
		, m_viewport( viewport )
		, m_totalTime( 0.0 )
		, m_skippedTime( 0.0 )
		, m_frameCount( 0 )
		, m_cameraID( g_cameraID++ )
		, m_isActiveWindow( true )
		, m_isBufferedFrameSubmitted( false )
		, m_selectionResult( RED_NEW( rend::SelectionResult )( red::PoolBackend() ) )
	{
		m_camera = red::CreateUniquePtr<RenderingWindowCamera>( m_cameraChangedNotifier );

#ifdef RED_USE_IMGUI
		m_viewport->ImGuiInitialize();
#endif
	}

	RenderingWindow::~RenderingWindow()
	{
		m_isBufferedFrameSubmitted = false;

		// unregister
		if ( m_externalCloseFunc )
			m_externalCloseFunc( this );

		// close viewport
		m_viewport.Reset();
	}

	SCameraRenderViewport RenderingWindow::BuildCameraRenderViewport( const CRenderFrameInfo &frameInfo ) const
	{
		const Uint32 w = Max<Uint32>( 1, frameInfo.GetViewportWidth() );
		const Uint32 h = Max<Uint32>( 1, frameInfo.GetViewportHeight() );
		return SCameraRenderViewport( 0, Rect( 0, w, 0, h ), Rect( 0, w, 0, h ) );
	}

	Bool RenderingWindow::IsActiveWindow() const
	{
		return m_isActiveWindow;
	}

	void RenderingWindow::BindExternalProcessFunction( RenderWindowProcessFunc func )
	{
		m_externalProcessFunc = func;
	}

	void RenderingWindow::BindExternalInputFunction( RenderWindowInputFunc func )
	{
		m_externalInputFunc = func;
	}

	RenderingWindow::RenderInfo RenderingWindow::UpdateAndRender( const Float engineTimeDelta, const tick::Info& tickInfo )
	{
		PC_SCOPE( RenderingWindow_UpdateAndRender );

#ifdef RED_USE_IMGUI
		m_viewport->ImGuiNewFrame( tickInfo.m_realTimeDelta );
#endif

		// time accumulator
		const Double doubleTimeDelta = (double)engineTimeDelta;
		m_totalTime += doubleTimeDelta;
		
		// check if we should skip this frame
		const Float timeDelta = static_cast<Float>(doubleTimeDelta + m_skippedTime);
		m_skippedTime = 0.0;

		// cache aspect ratio for the next lastFrameData update
		{
			PC_SCOPE( RenderingWindow_Camera );
			m_camera->SetLastFrameDataAspectRatio( m_viewport->GetAspectRatio() );
			// integrate camera movement within this frame
			// TODO: slow mo
			m_camera->Integrate( timeDelta );
		}

		// local debug update
		{
			PC_SCOPE( RenderingWindow_UpdateDebug );
			m_viewport->UpdateDebug( timeDelta );
		}
		
		// create new rendering frame using the specified camera and viewport
		// TODO: rendering mode and show flags should be copied from the rendering context (they should be removed from the viewport)
		// TODO: instant adaptation, instant dissolve ? what to do with that ?
		CRenderFrameInfo frameInfo;
		CRenderCameraFrameInfo cameraInfo;
		m_viewport->BeginFrame( frameInfo, cameraInfo );
		frameInfo.SetEngineTime( (float) m_totalTime );
		frameInfo.SetCleanEngineTime( (float) m_totalTime );
		const SCameraRenderViewport cameraRenderViewport = BuildCameraRenderViewport( frameInfo );

		// basic debug fragments
		{
			PC_SCOPE( RenderingWindow_RenderDebug );
			RenderInternalDebugFragments( cameraInfo.GetDebugDrawer() );
			RenderPerAreaDebugFragments( cameraInfo.GetDebugDrawer(), *m_camera, cameraRenderViewport.m_renderArea );
		}

		CRenderCamera renderCamera;
		{
			PC_SCOPE( RenderingWindow_PrepareCamera );
			m_camera->PrepareCamera( m_viewport->GetAspectRatio(), renderCamera );
			cameraInfo.SetRenderCamera( renderCamera );
			cameraInfo.m_frustum.InitFromCamera( renderCamera.GetWorldToScreen() );
		}

		RenderCameraDOFData renderCameraDOFData;
		{
			PC_SCOPE( RenderingWindow_PrepareCameraDOFData );
			m_camera->PrepareCameraDOFData( renderCameraDOFData );
			cameraInfo.m_renderCameraDOF = renderCameraDOFData;
		}

		if ( m_externalProcessFunc )
		{
			PC_SCOPE( RenderingWindow_ExternalProcess );

			RenderContext renderCtx = 
			{
				frameInfo,
				cameraInfo,
				&renderCamera
			};

			UpdateContext updateCtx =
			{
				timeDelta,
				tickInfo
			};

			RenderWindowFrame frame =
			{
				&updateCtx,
				renderCtx
			};

			m_externalProcessFunc( frame );
			// m_externalProcessFunc can change m_camera state so we need reapply changes to CRenderCamera and CRenderCameraFrameInfo
			{
				PC_SCOPE( RenderingWindow_PrepareCamera );
				m_camera->PrepareCamera( m_viewport->GetAspectRatio(), renderCamera );
				cameraInfo.SetRenderCamera( renderCamera );
				cameraInfo.m_frustum.InitFromCamera( renderCamera.GetWorldToScreen() );
			}
		}

		auto scene = frameInfo.GetScenePtr();

		// Pass camera to renderer
		if ( scene )
		{
			GRender->GetCommands()->RegisterCamera( scene, renderCamera, cameraRenderViewport, m_cameraID );
			GRender->GetCommands()->UpdateCameraData( scene, m_cameraID, cameraInfo );
			frameInfo.IssueCamera( m_cameraID );
		}


#ifdef RED_USE_IMGUI
		m_viewport->ImGuizmoUpdate( cameraInfo.GetRenderCamera() );
		m_viewport->DrawImGui( frameInfo.m_systemOnRenderDebug, cameraInfo.GetDebugDrawer() );
		m_viewport->ImGuiEndFrame( engineTimeDelta );
#endif

		RenderInfo result;
		result.viewport = m_viewport;
		result.frameInfo = std::move(frameInfo);
		result.cameraInfo = std::move(cameraInfo);
		return result;
	}

	void RenderingWindow::HandleInput( input::IInputContext& input )
	{
		PC_SCOPE( RenderingWindow_HandleInput );

		// engine viewport debug (console, debug pages)
#ifdef RED_USE_IMGUI
		m_viewport->ProcessInputInternal( input );
#endif
		m_viewport->ProcessDebugInput( input );

		// process external 
		if ( m_externalInputFunc )
        {
            const Bool was_any_input_handled = m_externalInputFunc( input );
			if( !was_any_input_handled  )
            {
                m_camera->ProcessInput( input );
            }
        }

        input.FlushConsumedEvents();
	}

	void RenderingWindow::HandleMessage( const gsm::ExternalMessage& msg )
	{
		if( msg.m_name == input::Messages::WindowLostFocus || msg.m_name == input::Messages::WindowGainedFocus )
		{
			Uint64 windowHandle = FromStringDirect<Uint64>(msg.m_params);
			if( m_viewport->GetViewportID() == windowHandle )
			{
				m_isActiveWindow = ( msg.m_name == input::Messages::WindowGainedFocus );
				m_windowActivationNotifier.Notify( m_isActiveWindow );
			}
		}
	}

	void RenderingWindow::SetFocalArea( const Box& area )
	{
		m_camera->SetFocalArea( area );
	}

	void RenderingWindow::FocusCameraOn( bool snap )
	{
		m_camera->FocusOn( snap );
	}

	void RenderingWindow::SetupFreeCamera( const Vector3& position, const EulerAngles& rotation )
	{
		m_camera->ResetToFreeCamera( position, rotation );
	}

	void RenderingWindow::SetupOrbitCamera( const Vector3& orbitPosition, const EulerAngles& rotation, const Float distance )
	{
		m_camera->ResetToOrbitCamera( orbitPosition, rotation, distance );
	}

	void RenderingWindow::SetCustomCameraTransform( const Transform& transform )
	{
		m_camera->SetCustomTransform( transform );
	}

	void RenderingWindow::ResetToDefaultCameraTransform()
	{
		m_camera->ResetToDefaultTransform();
	}

	void RenderingWindow::SetCustomFov(Float fov)
	{
		m_camera->SetCustomFov(fov);
	}

	void RenderingWindow::ResetToDefaultFov()
	{
		m_camera->ResetToDefaultFov();
	}

	void RenderingWindow::SetCustomDof( const RenderCameraDOFData& renderCameraDOF )
	{
		m_camera->SetCustomDof( renderCameraDOF );
	}

	void RenderingWindow::ResetToDefaultDof()
	{
		m_camera->ResetToDefaultDof();
	}

	void RenderingWindow::CalcCamera( CRenderCamera& outCamera ) const
	{
		m_camera->PrepareCamera( m_viewport->GetAspectRatio(), outCamera );
	}

	void RenderingWindow::SetProjectionMode( const EProjectionMode mode )
	{
		m_camera->SetProjectionMode( mode );
	}

	Vector3 RenderingWindow::GetCameraPosition() const
	{
		return m_camera->GetPosition();
	}

	EulerAngles RenderingWindow::GetCameraRotation() const
	{
		return m_camera->GetRotation();
	}

	Vector3 RenderingWindow::GetCameraStreamingPosition() const
	{
		return m_camera->GetStreamingReferencePosition();
	}

	Uint32 RenderingWindow::GetWidth() const
	{
		return m_viewport->GetWidth();
	}

	Uint32 RenderingWindow::GetHeight() const
	{
		return m_viewport->GetHeight();
	}

	Uint64 RenderingWindow::GetViewportID() const
	{
		return m_viewport->GetViewportID();
	}

	void RenderingWindow::SetViewportAspectRatio( const EAspectRatio aspectRatio )
	{
		m_viewport->SetAspectRatio( aspectRatio );

		red::HashMap< String, EAspectRatio > ratios;
		ratios.Insert( "default", EAspectRatio::FR_NONE );
		ratios.Insert( "21:9", EAspectRatio::FR_21_9 );
		ratios.Insert( "16:9", EAspectRatio::FR_16_9 );
		ratios.Insert( "16:10", EAspectRatio::FR_16_10 );
		ratios.Insert( "4:3", EAspectRatio::FR_4_3 );

		m_aspectRatioNotifier.Notify( ratios.FindValue( aspectRatio ).Key() );
	}

	void RenderingWindow::EnableRadialMenu( Bool enable )
	{
		m_viewport->EnableRadialMenu( enable );
	}

	Bool RenderingWindow::IsRadialMenuEnabled() const
	{
		return m_viewport->IsRadialMenuEnabled();
	}

	void RenderingWindow::RenderSelectionMap( const Rect& selectionRect, red::SharedPtr<rend::SelectionResult>& selectionResult, Bool buffered, Bool isMultiLayer )
	{
		// create new rendering frame using the specified camera and viewport
		// TODO: rendering mode and show flags should be copied from the rendering context (they should be removed from the viewport)
		// TODO: instant adaptation, instant dissolve ? what to do with that ?
		CRenderFrameInfo frameInfo;
		CRenderCameraFrameInfo cameraInfo;
		m_viewport->BeginFrame( frameInfo, cameraInfo, /*selection*/ true, /*present*/ false );
		frameInfo.SetEngineTime( (float) m_totalTime );
		frameInfo.SetCleanEngineTime( (float) m_totalTime );
		const SCameraRenderViewport cameraRenderViewport = BuildCameraRenderViewport( frameInfo );

		// setup selection
		frameInfo.m_selectionArea.m_left = Clamp< Int32 >( selectionRect.m_left, 0, m_viewport->GetWidth() );
		frameInfo.m_selectionArea.m_top = Clamp< Int32 >( selectionRect.m_top, 0, m_viewport->GetHeight());
		frameInfo.m_selectionArea.m_right = Clamp< Int32 >( selectionRect.m_right, 0, m_viewport->GetWidth() );
		frameInfo.m_selectionArea.m_bottom = Clamp< Int32 >( selectionRect.m_bottom, 0, m_viewport->GetHeight() );
		frameInfo.m_systemOnRenderDebug = false;
		frameInfo.m_multilayerSelection = isMultiLayer;

		Uint32 nextFrameIndex = 0;
 		if ( buffered )
 		{
 			// when buffering is enabled, we write to internal buffer
 			frameInfo.m_selectionResult = m_selectionResult;
 		}
 		else
		{
			// when buffering is disabled we can write the result directly to the output structure
			frameInfo.m_selectionResult = selectionResult;
			selectionResult->Clear();
		}

		// nothing to render
		if ( frameInfo.m_selectionArea.IsEmpty() )
		{
			return;
		}

		// compute rendering camera
		CRenderCamera frameCamera;
		m_camera->PrepareCamera( m_viewport->GetAspectRatio(), frameCamera );
		cameraInfo.SetRenderCamera(frameCamera);
		cameraInfo.m_frustum.InitFromCamera(frameCamera.GetWorldToScreen());

		if ( m_externalProcessFunc )
		{
			PC_SCOPE(RenderingWindow_ExternalProcess);

			RenderContext renderCtx =
			{
				frameInfo,
				cameraInfo,
				&frameCamera
			};

			RenderWindowFrame frame =
			{
				nullptr,
				renderCtx
			};

			m_externalProcessFunc( frame );
		}

		auto scene = frameInfo.GetScenePtr();
		if ( !scene )
		{
			return;
		}

		// Pass camera to renderer
		GRender->GetCommands()->RegisterCamera( scene, frameCamera, cameraRenderViewport, m_cameraID );
		GRender->GetCommands()->UpdateCameraData( scene, m_cameraID, cameraInfo );

		frameInfo.IssueCamera( m_cameraID ); 

		Bool isPrevBufferedSelectionMapReady = false;
		// Flush any previous pending fence
		if ( m_isBufferedFrameSubmitted )
		{
			m_isBufferedFrameSubmitted = false;
			GRender->GetCommands()->FlushPreviousFrameCommandsProcessing();
			isPrevBufferedSelectionMapReady = true;
		}

		// create rendering frame using the frame info
		m_viewport->SubmitFrame( frameInfo, cameraInfo );

		if ( buffered )
		{
			if ( isPrevBufferedSelectionMapReady )
			{
				RED_ASSERT( m_selectionResult->IsValid(), "Selection result is not valid. Render command not executed?" );
				if ( m_selectionResult->IsValid() )
				{
					selectionResult = std::move( m_selectionResult );
				}
			}

			m_isBufferedFrameSubmitted = true;
		}
		else /* !buffered */
		{
			// flush and sync rendering
			// this ensures that the frame was rendered and the data is ready for read out
			GRender->GetCommands()->FlushPreviousFrameCommandsProcessing();
			m_isBufferedFrameSubmitted = false;
			// the readout of the buffer is done synchronously at the end of frame rendering inside the renderer itself

			// 24.05.2019 WCzarny - I'm commenting this out, becuause it causes a crash on taking a screenshot in the editor
			//RED_FATAL_ASSERT( selectionResult->IsValid(), "Selection result is not valid. Render command not executed?" );
		}
	}

	void RenderingWindow::RenderDepthMap( const Rect& viewportRect, red::SharedPtr<rend::DepthResult>& depthResult, red::SharedPtr<rend::Color10Result> normalResult )
	{
		// create new rendering frame using the specified camera and viewport
		// TODO: rendering mode and show flags should be copied from the rendering context (they should be removed from the viewport)
		// TODO: instant adaptation, instant dissolve ? what to do with that ?
		CRenderFrameInfo frameInfo;
		CRenderCameraFrameInfo cameraInfo;
		m_viewport->BeginFrame( frameInfo, cameraInfo, /*selection*/ false, /*present*/ false );
		frameInfo.SetEngineTime( (float) m_totalTime );
		frameInfo.SetCleanEngineTime( (float) m_totalTime );
		const SCameraRenderViewport cameraRenderViewport = BuildCameraRenderViewport( frameInfo );

		// setup selection
		frameInfo.m_depthArea.m_left = Clamp< Int32 >( viewportRect.m_left, 0, m_viewport->GetWidth()  );
		frameInfo.m_depthArea.m_top = Clamp< Int32 >( viewportRect.m_top, 0, m_viewport->GetHeight() );
		frameInfo.m_depthArea.m_right = Clamp< Int32 >( viewportRect.m_right, 0, m_viewport->GetWidth()  );
		frameInfo.m_depthArea.m_bottom = Clamp< Int32 >( viewportRect.m_bottom, 0, m_viewport->GetHeight() );
		frameInfo.m_depthResult = depthResult;
		frameInfo.m_gBufferNormalsResult = normalResult;
		frameInfo.m_systemOnRenderDebug = false;

		// nothing to render
		if ( frameInfo.m_depthArea.IsEmpty() )
		{
			return;
		}

		Uint64 camName = 0;

		// compute rendering camera
		CRenderCamera frameCamera;
		m_camera->PrepareCamera( m_viewport->GetAspectRatio(), frameCamera );
		cameraInfo.SetRenderCamera(frameCamera);
		cameraInfo.m_frustum.InitFromCamera(frameCamera.GetWorldToScreen());

		if (m_externalProcessFunc)
		{
			PC_SCOPE(RenderingWindow_ExternalProcess);

			RenderContext renderCtx =
			{
				frameInfo,
				cameraInfo,
				&frameCamera
			};

			RenderWindowFrame frame =
			{
				nullptr,
				renderCtx
			};

			m_externalProcessFunc( frame );
		}

		auto scene = frameInfo.GetScenePtr();

		// Pass camera to renderer
		GRender->GetCommands()->RegisterCamera( scene, frameCamera, cameraRenderViewport, camName );
		GRender->GetCommands()->UpdateCameraData( scene, camName, cameraInfo );

		frameInfo.IssueCamera( camName++ );

		// create rendering frame using the frame info
		{	
			m_viewport->SubmitFrame( frameInfo, cameraInfo );

			// flush and sync rendering
			// this ensures that the frame was rendered and the data is ready for read out
			// the readout of the buffer is done synchronously at the end of frame rendering inside the renderer itself
			m_viewport->FlushFrame();
		}
	}

	void RenderingWindow::RenderInternalDebugFragments( rend::IDebugDrawer& dd )
	{
		static rend::DebugFilter DF_Grid( "Editor/Window/Grid" );
		if ( dd.GetMask().Test( DF_Grid ) )
			tools::RenderGrid( dd );	
	}

	void RenderingWindow::RenderPerAreaDebugFragments( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect )
	{
		static rend::DebugFilter DF_CameraInfo( "Editor/Window/CameraInfo" );
		if ( dd.GetMask().Test( DF_CameraInfo ) )
			RenderCameraInfo( dd, cam, areaRect );

		static rend::DebugFilter DF_CameraAxes( "Editor/Window/CameraAxes" );
		if ( dd.GetMask().Test( DF_CameraAxes ) )
			RenderCameraAxes( dd, cam, areaRect );
	}

	void RenderingWindow::RenderCameraAxes( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect )
	{
		rend::FragmentBuilder builder( dd );

		const Int32 scale = Config::cvEditorCameraAxesLength.Get();
		const Int32 bx = areaRect.m_left + 10 + scale;
		const Int32 by = areaRect.m_bottom - 10 - scale;

		// extract camera orientation matrix
		CRenderCamera tempCamera;
		cam.PrepareCamera( 1.0f, tempCamera );
		const auto orientationMatrix = tempCamera.GetWorldToView();

		// project the view axes
		const Vector3 ex = orientationMatrix.TransformVector( Vector3::EX() );
		const Vector3 ey = orientationMatrix.TransformVector( Vector3::EY() );
		const Vector3 ez = orientationMatrix.TransformVector( Vector3::EZ() );

		// render the axes with orthographic projection 
		const Vector2 o( (Float)bx, (Float)by );
		const Vector2 px = o + (Vector2(ex.X, -ex.Y) * (Float)scale); // projection
		const Vector2 py = o + (Vector2(ey.X, -ey.Y) * (Float)scale); // projection
		const Vector2 pz = o + (Vector2(ez.X, -ez.Y) * (Float)scale); // projection
		builder.SetColor( Color::RED() );
		builder.AddFlatLine( o, px );
		builder.SetColor( Color::GREEN() );
		builder.AddFlatLine( o, py );
		builder.SetColor( Color::BLUE() );
		builder.AddFlatLine( o, pz );

		// descriptions
		const Vector2 tx = o + (Vector2(ex.X, -ex.Y) * (Float)scale * 1.2f); // projection
		const Vector2 ty = o + (Vector2(ey.X, -ey.Y) * (Float)scale * 1.2f); // projection
		const Vector2 tz = o + (Vector2(ez.X, -ez.Y) * (Float)scale * 1.2f); // projection
		builder.SetColor( Color::WHITE() );
		builder.SetVerticalTextAlignment(0);
		builder.SetHorizontalTextAlignment(0);
		builder.AddFlatText( (Int32)tx.X, (Int32)tx.Y, "X" );
		builder.AddFlatText( (Int32)ty.X, (Int32)ty.Y, "Y" );
		builder.AddFlatText( (Int32)tz.X, (Int32)tz.Y, "Z" );
	}

	void RenderingWindow::RenderCameraInfo( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect )
	{
		rend::FragmentBuilder builder( dd );

		Int32 bx = (Int32)builder.GetScreenWidth() - 20;
		Int32 by = (Int32)builder.GetScreenHeight() - 20;

		if ( cam.IsOrbiting() )
		{
			Int32 sizeY = 13;

			builder.SetColor( Color::WHITE() );
			builder.SetVerticalTextAlignment(1);
			builder.SetHorizontalTextAlignment(1);
			builder.AddFlatTextf( bx, by, "Orbit [%1.2f, %1.2f, %1.2f], Pitch: %1.1f, Yaw: %1.1f, Distance: %1.2f",
				cam.GetPosition().X, cam.GetPosition().Y, cam.GetPosition().Z,
				cam.GetRotation().Pitch, cam.GetRotation().Yaw, cam.GetOrbitDistance() );

			by -= sizeY+5;

			if ( cam.GetInputMouseMode() == 1 )
			{
				builder.AddFlatText( bx, by, "Camera mode: Zoom" );
			}
			else if ( cam.GetInputMouseMode() == 2 )
			{
				builder.AddFlatText( bx, by, "Camera mode: Orbit" );
			}
		}
		else if ( cam.IsFree() )
		{
			Int32 sizeY = 13;

			builder.SetColor( Color::WHITE() );
			builder.SetVerticalTextAlignment(1);
			builder.SetHorizontalTextAlignment(1);
			builder.AddFlatTextf( bx, by, "Free [%1.2f, %1.2f, %1.2f], Pitch: %1.1f, Yaw: %1.1f",
				cam.GetPosition().X, cam.GetPosition().Y, cam.GetPosition().Z,
				cam.GetRotation().Pitch, cam.GetRotation().Yaw );

			by -= sizeY+5;

			if ( cam.GetInputMouseMode() == 1 )
			{
				builder.AddFlatText( bx, by, "Camera mode: Move" );
			}
			else if ( cam.GetInputMouseMode() == 2 )
			{
				builder.AddFlatText( bx, by, "Camera mode: Look" );
			}
			else if ( cam.GetInputMouseMode() == 3 )
			{
				builder.AddFlatText( bx, by, "Camera mode: Pan" );
			}
		}
	}

	void RenderingWindow::SetupInterop( RenderingWindow* window, ::interop::utils::Object& op )
	{
		op.RegisterNotifier( "_onCameraChanged", window->m_cameraChangedNotifier );
		op.RegisterNotifier( "_onWindowActivationChanged", window->m_windowActivationNotifier );
		op.RegisterNotifier( "_onAspectRatioChanged", window->m_aspectRatioNotifier );

		// Register camera
		op.RegisterObject( "camera",
			[window]( interop::IContext& ctx )
			{
				if ( window->m_camera )
					return window->m_camera->CreateInterop();
				else
					return interop::ObjectPtr();
			} );

		op.RegisterFunctor<Uint32>( "getWidth", [window]( interop::IContext& ctx ) { return window->GetWidth(); } );
		op.RegisterFunctor<Uint32>( "getHeight", [window]( interop::IContext& ctx ) { return window->GetHeight(); } );

		op.RegisterFunctor< void, String >( "setAspectRatioMode", [ window ]( interop::IContext& ctx, const String& mode ) 
			{
				red::HashMap< String, EAspectRatio > ratios;
				ratios.Insert( "default", EAspectRatio::FR_NONE );
				ratios.Insert( "21:9", EAspectRatio::FR_21_9 );
				ratios.Insert( "16:9", EAspectRatio::FR_16_9 );
				ratios.Insert( "16:10", EAspectRatio::FR_16_10 );
				ratios.Insert( "4:3", EAspectRatio::FR_4_3 );

				EAspectRatio ratio;
				if ( ratios.Find( mode, ratio ) )
				{
					window->SetViewportAspectRatio( ratio );
				}			
			} );
	}
}