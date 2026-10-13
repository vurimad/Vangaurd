/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewPanel.h"
#include "renderingWindowCamera.h"
#include "previewEditMode.h"
#include "previewDragDropHandler.h"
#include "previewPanelSharedData.h"
#include "screenshotUtilities.h"
#include "resourceBackend.h"
#include "previewShortcutInputHandler.h"
#include "resourceEditor.h"
#include "previewEnvironmentManager.h"

#include "../../../common/engine/include/renderEngineViewport.h"

#include "../../../common/input/include/inputContext.h"

#include "../../../common/redReflection/include/serializableMap.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../../common/resourceMaterial/include/materialCompilationManager.h"

#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/renderData/include/viewport.h"

#include "../../../common/renderBackend/include/materialLibrary.h"

#include "../../../common/shaderCache/include/shaderCacheManager.h"

#include "../../../common/world/include/worldRuntimeScene.h"

#include "../../../common/renderData/include/renderSelectionResult.h"

#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resource/include/jsonSerializer.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( CameraClipboardCoordinates, tools );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_entries );
	RTTI_PROPERTY( m_prefixes );
RTTI_END_TYPE();

using red::DynArray;

namespace Config
{
	TConfigVar< Bool > cvHdrEnable( "Editor/HDR", "Enabled", false, eConsoleVarFlag_Save );
	TConfigVar< Float > cvHdrMaxBrightness( "Editor/HDR", "MaxBrightness", 500.0f, 50.0f, 5000.0f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvHdrPaperWhite( "Editor/HDR", "PaperWhite", 100.0f, 50.0f, 5000.0f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvHdrTonemappingMidpoint( "Editor/HDR", "TonemappingMidpoint", 2.0f, 0.0f, 100.0f, eConsoleVarFlag_Save );

	/// The slider move amount per mouse pixel move.
	TConfigVar< Float > cvEnvCubeRotationSpeed( "Editor/RenderingWindow/Env", "EnvCubeRotationSpeed", 0.1f, 0.01f, 10.f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvTimeOfDayRotationSpeed( "Editor/RenderingWindow/Env", "TimeOfDayRotationSpeed", 0.05f, 0.01f, 0.05f, eConsoleVarFlag_Save );

	TConfigVar< Bool > cvInvertCameraOrbitXAxis( "Editor/RenderingWindow/Camera", "InvertCameraOrbitXAxis", false, eConsoleVarFlag_Save );
	TConfigVar< Bool > cvInvertCameraOrbitYAxis( "Editor/RenderingWindow/Camera", "InvertCameraOrbitYAxis", false, eConsoleVarFlag_Save );
}

namespace tools
{
	static red::Atomic< Uint32 > GPreviewPanelIDAllocator(1);
	const char* SEnvModeConfigGroup = "envMode";
	const char* SInitialEnvModeConfigKey = "initialMode";

	const char* c_presetConfigGroup = "RenderPreset";
	const char* c_presetConfigKey = "Value";

	const char PreviewPanel::sm_envBasePath[] = "engine\\preview_panel\\preview_panel_env_base.env";

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// Generic input handler (capture mode input) for handling mouse slider.
	/// Uses mouse moves left/right to update some scalar value.
	class MouseSliderInputHandler : public IPreviewInputHandler
	{
	public:
		MouseSliderInputHandler( PreviewPanel* panel )
			: m_panel( panel )
		{
		}

		virtual void OnUpdateValue( Int32 sliderDelta ) = 0;

		virtual Bool OnMouseUp( const InputMouseButton& evt ) override
		{
			if ( evt.m_button == RMB )
			{
				m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_SHOW );
				return false;   // we are done
			}
			return true;          // keep us alive
		}

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			OnUpdateValue( evt.m_dx );

			m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_HIDE );

			return true;        // keep us active
		}

		virtual void OnCanceled() override
		{
			m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_SHOW );
		}

	protected:
		PreviewPanel* m_panel;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// Input handler (capture mode input) for handling mouse slider env cube rotation.
	class EnvCubeMouseRotationInputHandler : public MouseSliderInputHandler
	{
	public:
		EnvCubeMouseRotationInputHandler( PreviewPanel* panel, EnvironmentManager& envManager )
			: MouseSliderInputHandler( panel )
			, m_envManager( envManager )
		{
		}

		virtual void OnUpdateValue( Int32 sliderDelta ) override
		{
			Float curAngle = m_panel->GetEnvProbeYawAngle();
			curAngle += sliderDelta * Config::cvEnvCubeRotationSpeed.Get();
			if ( curAngle < 0.0f )
				curAngle += 360.0f;
			if ( curAngle > 360.0f )
				curAngle -= 360.0f;
			m_panel->SetEnvProbeYawAngle( curAngle );
		}

	private:
		EnvironmentManager& m_envManager;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	/// Input handler (capture mode input) for handling mouse slider time of day rotation.
	class TimeOfDayMouseRotationInputHandler : public MouseSliderInputHandler
	{
	public:
		TimeOfDayMouseRotationInputHandler( PreviewPanel* panel, EnvironmentManager& envManager )
			: MouseSliderInputHandler( panel )
			, m_envManager( envManager )
		{
		}

		virtual void OnUpdateValue( Int32 sliderDelta ) override
		{
			Float timeOfDay = m_panel->GetTimeOfDay();
			timeOfDay -= sliderDelta * Config::cvTimeOfDayRotationSpeed.Get();
			if (timeOfDay < 0.0f)
				timeOfDay += 24.0f;
			if (timeOfDay > 24.0f)
				timeOfDay -= 24.0f;
			m_panel->SetTimeOfDay( timeOfDay );
		}

	private:
		EnvironmentManager& m_envManager;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	namespace
	{
		void RecompileLoadedMaterials()
		{
#ifndef NO_RUNTIME_MATERIAL_COMPILATION
			RED_LOG_WARNING( "Recompiling shaders for material templates..." );
			GShaderCache->CacheDirectoriesCRC();
			const Uint64 currentIncludesCRC = GShaderCache->GetCurrentIncludesCRC();
			const Uint64 cachedIncludesCRC = GShaderCache->GetCachedIncludesCRC();
			const Bool includesMatch = cachedIncludesCRC == currentIncludesCRC;

			// Reload shaders
			for ( const auto& it : GSerializableMap->OfType< CMaterialTemplate >() )
			{
				const THandle< CMaterialTemplate > matTempl = it.ToHandle();
				if ( !matTempl )
				{
					continue;
				}
				for ( const auto& technique : matTempl->GetTechniques() )
				{
					for ( const auto& pass : technique.m_passes )
					{
						for ( Uint32 target = MSH_First; target < MSH_MAX; ++target )
						{
							red::AbsolutePath sourcePath;
							if ( GetMaterialLibrary()->GetShaderSourceAbsolutePath( matTempl->GetName(), pass.m_stagePassNameRegular, (EMaterialShaderTarget)target, sourcePath ) )
							{
								red::DateTime dt = GFileManager->GetFileTime( sourcePath );
								matTempl->SetTimeStamp( pass.m_stagePassNameRegular, (EMaterialShaderTarget)target, dt );
							}
						}
					}
				}


				matTempl->Recompile( includesMatch );
			}

			GetMaterialCompilationManager().Run( includesMatch );
			RED_LOG_WARNING( "Recompiling shaders for material templates done (async compilation might still continue...)." );
#endif // NO_RUNTIME_MATERIAL_COMPILATION
		}
	}

	PreviewPanel::CameraOrbitInputHandler::CameraOrbitInputHandler( PreviewPanel* panel, const Vector3& targetPoint )
		: m_panel( panel )
		, m_targetPoint( targetPoint )
		, m_rotationX( 0.0f )
		, m_rotationY( 0.0 )
	{
		RED_FATAL_ASSERT( m_panel );

		const RenderingWindowCamera* camera = m_panel->GetViewport()->GetCamera();
		m_baseCameraPosition = camera->GetPosition();
		m_baseCameraRotation = camera->GetRotation();
	}

	Bool PreviewPanel::CameraOrbitInputHandler::OnMouseUp( const InputMouseButton& evt )
	{
		if ( evt.m_button == RMB )
		{
			m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_SHOW );
			return false; // we are done
		}

		return true; // keep us alive
	}

	Bool PreviewPanel::CameraOrbitInputHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		if ( evt.m_rawDx == 0.0f && evt.m_rawDy == 0.0f )
		{
			return true;
		}

		Float rawX = evt.m_rawDx;
		Float rawY = evt.m_rawDy;

		if ( Config::cvInvertCameraOrbitXAxis.Get() )
		{
			rawX *= -1.0f;
		}

		if ( Config::cvInvertCameraOrbitYAxis.Get() )
		{
			rawY *= -1.0f;
		}

		// use raw input, like RenderingWindowCamera does
		m_rotationX += RenderingWindowCamera::GetMouseRotationScaleX() * rawX;
		m_rotationY += RenderingWindowCamera::GetMouseRotationScaleY() * rawY;

		// normalize angles
		while ( m_rotationX > 360.0f )
			m_rotationX -= 360.0f;

		while ( m_rotationX < 0.0f )
			m_rotationX += 360.0f;

		const Vector3 cameraVector = m_baseCameraPosition - m_targetPoint;

		// calculate rotation quaternions
		Vector4 yRotationAxis;
		m_baseCameraRotation.ToAngleVectors( nullptr, &yRotationAxis, nullptr );
		const Quaternion yRotationQuat( yRotationAxis, DEG2RAD( m_rotationY ) );
		const Quaternion xRotationQuat( Vector3::EZ(), DEG2RAD( m_rotationX ) );

		// rotate the original camera vector
		const Vector3 rotatedCameraVector = ( xRotationQuat * yRotationQuat ).Transform( cameraVector );

		// update camera position & rotation
		const Vector3 newCameraPosition = m_targetPoint + rotatedCameraVector;
		const EulerAngles newCameraRotation = m_baseCameraRotation + EulerAngles( 0.0f, m_rotationY, m_rotationX );
		m_panel->GetViewport()->GetCamera()->ResetToFreeCamera( newCameraPosition, newCameraRotation );

		m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_HIDE );

		return true; // keep us active
	}

	void PreviewPanel::CameraOrbitInputHandler::OnCanceled()
	{
		m_panel->RequestMouseCursorShowHide( PreviewPanel::MOUSE_CURSOR_SHOW );
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

	static Float GetZoomTime()
	{
		return EngineTime::GetNow();
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	PreviewPanel::SZoomState::SZoomState ()
	{
		Reset();
	}

	void PreviewPanel::SZoomState::Reset()
	{
		m_div			= 1;
		m_offsetX		= 0;
		m_offsetY		= 0;
	}

	bool PreviewPanel::SZoomState::operator==( const SZoomState &other ) const
	{
		return m_div == other.m_div && m_offsetX == other.m_offsetX && m_offsetY == other.m_offsetY;
	}
	
	PreviewPanel::SZoomData::SZoomData ()
		: m_startTime ( -1 )
	{}

	void PreviewPanel::SZoomData::Reset()
	{
		m_startTime = -1000;
		m_startState.Reset();
		m_targetState.Reset();
	}

	void PreviewPanel::SZoomData::SetZoomTarget( Float currentTime, Float transitionStartTime, const SZoomState &newTarget )
	{
		m_startState = GetState( currentTime );
		m_targetState = newTarget;
		m_targetState.m_div = Max( 1.f, m_targetState.m_div );
		m_startTime = transitionStartTime;
	}

	void PreviewPanel::SZoomData::TransformCoords( Float currentTime, Int32 x, Int32 y, Int32 &outX, Int32 &outY ) const
	{
		const SZoomState state = GetState( currentTime );
		outX = (Int32)(x / state.m_div + state.m_offsetX);
		outY = (Int32)(y / state.m_div + state.m_offsetY);
	}

	PreviewPanel::SZoomState PreviewPanel::SZoomData::GetState( Float time ) const
	{
		const Float transitionDuration = 0.125f;
		const Float endTime = m_startTime + transitionDuration;

		if ( time <= m_startTime )
		{
			return m_startState;
		}

		if ( time >= endTime )
		{
			return m_targetState;
		}
		
		SZoomState currState;
		const Float t = Clamp( (time - m_startTime) / Max( 0.0001f, transitionDuration ), 0.f, 1.f );
		currState.m_div = 1.f / math::Lerp( t, 1.f / m_startState.m_div, 1.f / m_targetState.m_div ); 
		currState.m_offsetX = math::Lerp( t, m_startState.m_offsetX, m_targetState.m_offsetX ); 
		currState.m_offsetY = math::Lerp( t, m_startState.m_offsetY, m_targetState.m_offsetY );
		return currState;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

	PreviewPanel::PreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState, const SetupInfo& setupInfo)
		: m_sharedState(sharedState)
		, m_viewport( renderingWindow )
		, m_disableTonemapping( true )
		, m_panelID( GPreviewPanelIDAllocator.Increment() )
		, m_lastMouseX( -1 )
		, m_lastMouseY( -1 )
		, m_mouseTotalTravel( 0 )
		, m_envManagerCollection( red::CreateUniquePtr< ObjectCollection >( sharedState->GetActionHistory() ) )
		, m_screenshotDataCollection( red::CreateUniquePtr< ObjectCollection >( sharedState->GetActionHistory() ) )
		, m_screenshotData( CreateHandle< rend::SingleScreenShotData >() )
		, m_screenshotBatchCollection( red::CreateUniquePtr< ObjectCollection >( sharedState->GetActionHistory() ) )
		, m_screenshotBatchData( CreateHandle< rend::ScreenshotBatchData >() )
		, m_gridGeneratorCollection( red::CreateUniquePtr< ObjectCollection >( sharedState->GetActionHistory() ) )
		, m_gridGeneratorData( CreateHandle< rend::GridGeneratorData >() )
		, m_envManager( CreateHandle< EnvironmentManager >( sharedState->GetResourceBackend().GetEnvironmentPresetsManager() ) )
		, m_defaultEnvMode( setupInfo.m_defaultEnvMode )
		, m_requestMouseCursorHide( false )
		, m_requestMouseCursorShow( false )
		, m_inputListeners{ red::PoolBackend() }
		, m_useExtenedDynamicRangeSky( false )
		, m_shortcutManager( red::CreateSharedPtr< ShortcutManager >() )
		, m_motionBlurEnabled( true )
		, m_screenshotCoordsDump(false)
		, m_screenshotCoordsResource(res::ResourcePath())
	{
		const String& editorTypeName = sharedState->GetEditor().GetEditorTypeName();
		m_contextName = RED_NAME( String::Printf( "%hsPreviewPanel", editorTypeName.AsChar() ) );

		m_envManagerCollection->SetObject( m_envManager.Get() );
		m_envManagerCollection->SetModifyResource( false );

		m_screenshotDataCollection->SetObject( m_screenshotData.Get() );
		m_screenshotBatchCollection->SetObject( m_screenshotBatchData.Get() );
		m_gridGeneratorCollection->SetObject( m_gridGeneratorData.Get() );

		// register in the shared data
		m_sharedState->NotifyPanelCreated(this);

		// connect hooks that will propagate the update from the window to this preview panel
		m_viewport->BindExternalInputFunction( [this](input::IInputContext& inputData) { return OnInput( inputData ); } );
		m_viewport->BindExternalProcessFunction( [this]( RenderWindowFrame& frame ) { UpdateAndRender( frame ); } );

		// setup typical camera
		m_viewport->SetupFreeCamera( Vector3(0,-10,0), EulerAngles(0,0,0) );

		m_envManager->OnEnvModeChanged = [this]( const String& newMode )
		{
			GetSharedData()->SetConfigValue( ConfigScope::Editor, SEnvModeConfigGroup, SInitialEnvModeConfigKey, newMode );
		};

		InitializeBaseEnvironment();

		m_shortcutInputHandler = red::CreateUniquePtr< PreviewShortcutInputHandler >( m_panelID );
		m_shortcutManager->Register( m_shortcutInputHandler.Get() );
		m_shortcutManager->PushContext( m_contextName, ShortcutContextMode::Inherit );
		AttachInputListener( m_shortcutInputHandler.Get() );
		RegisterShortcuts();

		m_shortcutManager->InitializeInterop( sharedState->GetResourceBackend().GetShortcutBackend(), m_panelID );
		auto token = GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( sm_envBasePath ) );
		token->WaitUntilLoaded();
		if (token->IsLoaded())
		{
			if (token->GetResource())
			{
				m_defaultEnvironmentDefinition = Cast< world::EnvironmentDefinition >( token->GetResource() );
			}
		}
	}

	PreviewPanel::~PreviewPanel()
	{
		m_shortcutManager->UnregisterInterop( m_panelID );

		// make sure all input handlers unregister themselves
		ProcessInputHandlerChange( false );

		m_sharedState->NotifyPanelDestroyed(this);
	}

	void PreviewPanel::InitializeBaseEnvironment()
	{
		String initialEnvName = m_defaultEnvMode;
		GetSharedData()->GetConfigValue( ConfigScope::Editor, SEnvModeConfigGroup, SInitialEnvModeConfigKey, initialEnvName );
		GetEnvironmentManager()->SetEnvPreviewPreset( initialEnvName );
	}

	void PreviewPanel::InitializeEnvironmentInfo( CRenderFrameInfo& info )
	{
		if (m_defaultEnvironmentDefinition)
		{
			world::envUtils::CreateBaseSet( info, m_defaultEnvironmentDefinition, 12.00f );
			world::envUtils::FinalizeBaseSet( info );
		}
	}

	void PreviewPanel::PerformOverrides( CRenderFrameInfo & info )
	{
		GetEnvironmentManager()->PerformOverrides( info );
	}

	void PreviewPanel::SetTimeOfDay( Float timeOfDay )
	{
	}

	void PreviewPanel::SetEnvProbeYawAngle( Float angle )
	{
		m_envManager->SetEnvProbeYawAngle( angle );
	}

	Float PreviewPanel::GetTimeOfDay()
	{
		return 12.0f;
	}

	Float PreviewPanel::GetEnvProbeYawAngle()
	{
		return m_envManager->GetEnvProbeYawAngle();
	}

	void PreviewPanel::RegisterShortcuts()
	{
		// Context menu.
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_RightMouse );

			ShortcutBuilder contextMenu = ShortcutBuilder()
				.IdName( "idPreviewContexMenu" )
				.Binding( binding )
				.Callback( [ this ]( const KeyEventData& data ) { return OnShowContextMenu( m_lastMouseX, m_lastMouseY ); } )
				.Name( "A context menu" );

			m_shortcutManager->Register( contextMenu );
		}

		// Env cube rotation using mouse.
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_V ).Key( EInputKey::IK_RightMouse, EAction::Down );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				AttachInputHandler( red::CreateUniquePtr< EnvCubeMouseRotationInputHandler >( this, *GetEnvironmentManager().Get() ) );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idRotateEnvCubeShortcut" ).Binding( binding ).Callback( callback ).Name( "Rotate env cube" );

			m_shortcutManager->Register( shortcut );
		}

		// Time of day rotation using mouse.
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_V ).Key( EInputKey::IK_RightMouse, EAction::Down ).Modifier( EInputKey::IK_Shift );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				AttachInputHandler( red::CreateUniquePtr< TimeOfDayMouseRotationInputHandler >( this, *GetEnvironmentManager().Get() ) );

				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idRotateTimeOfDayShortcut" ).Binding( binding ).Callback( callback ).Name( "Rotate time of day" );

			m_shortcutManager->Register( shortcut );
		}

		// Jump to
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_J );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				return CameraJumpTo( m_lastMouseX, m_lastMouseY );
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idJumpToCursor" ).Binding( binding ).Callback( callback ).Name( "Jump to cursor" ).Description( "Teleport camera to a point under the cursor" );

			m_shortcutManager->Register( shortcut );
		}

		//TODO: fix crash in wwise when using this...looks like it happens at unexpected times like when typing in the screenplay CYB-84074

		// Reload soundbanks
		/*{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_B );

			ShortcutCallback callback = [ this ]( const KeyEventData& )
			{
				GEngine->GetSoundSystem()->ReloadSoundBanks();
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "reloadSoundbanks" )
				.Binding( binding )
				.Callback( callback )
				
				.Name( "Reload Soundbanks" )
				.Description( "Reloads the wwise soundbanks" );

			//m_shortcutManager.Register( shortcut );
		}*/

		// Orbiting around point under cursor
		{
			ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_Z ).Key( EInputKey::IK_RightMouse, EAction::Down );

			ShortcutCallback callback = [ this ]( const KeyEventData& data )
			{
				Vector3 target;
				if ( !GetPointAt( GetMouseX(), GetMouseY(), target ) )
				{
					return false;
				}

				AttachInputHandler( red::CreateUniquePtr< CameraOrbitInputHandler >( this, target ) );
				return true;
			};

			ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idOrbitAroundTarget" ).Binding( binding ).Callback( callback ).Name( "Target orbit mode" ).Description( "Rotate camera around point under the cursor" );

			m_shortcutManager->Register( shortcut );
		}

	}

	Bool PreviewPanel::OnShowContextMenu( Int32 mouseX, Int32 mouseY )
	{
		const Int32 maxMouseMoveDelta = 5;

		if ( m_mouseTotalTravel < maxMouseMoveDelta )
		{
			String xStr, yStr;

			ToString( xStr, mouseX );
			ToString( yStr, mouseY );

			m_contextMenuNotifier.Notify( xStr + "," + yStr );
		}

		// do not consume
		return false;
	}

	void PreviewPanel::FocusOnBounds( const Box& area, const Bool snap /*= false*/ )
	{
		m_viewport->SetFocalArea( area );
		m_viewport->FocusCameraOn( snap );
	}
	
	void PreviewPanel::ZoomOut( Bool snap )
	{
		const Box bounds = GetSceneBounds();
		if ( !bounds.IsEmpty() )
		{
			FocusOnBounds( bounds, snap );
		}
	}

	void PreviewPanel::UpdateAndRender( RenderWindowFrame& frame )
	{
		OnFrameBegin( frame.m_renderCtx );

		if ( frame.m_updateCtx )
		{
			OnUpdate( *frame.m_updateCtx );
		}

		OnRender( frame.m_renderCtx );
	}

	void PreviewPanel::OnFrameBegin( RenderContext& ctx )
	{
	
	}

	void PreviewPanel::OnUpdate(const UpdateContext& ctx)
	{
		// apply HDR settings
		if ( const ViewportHandle& renderingViewport = m_viewport->GetEngineViewport()->GetRenderingViewport() )
		{
			renderingViewport->RequestHDRMode( Config::cvHdrEnable.Get() ? GpuWrapApi::HDRMODE_HDR10_PQ : GpuWrapApi::HDRMODE_None );
			renderingViewport->RequestHDRMaxBrightness( Config::cvHdrMaxBrightness.Get() );
			renderingViewport->RequestHDRPaperWhite( Config::cvHdrPaperWhite.Get() );
			renderingViewport->RequestHDRTonemappingMidpoint( Config::cvHdrTonemappingMidpoint.Get() );
		}

		// update input handler
		if (m_inputHandler)
			ProcessInputHandlerChange(m_inputHandler->OnUpdate(ctx.m_timeDelta));

		if ( m_inputCommand )
		{
			m_inputCommand->OnUpdate( ctx.m_timeDelta );
		}

		// update drag&drop logic
		if (m_dragDropHandler)
			m_dragDropHandler->OnUpdate(ctx.m_timeDelta);

		// update all of the input listeners
		for (auto it : m_inputListeners)
			it->OnUpdate(ctx.m_timeDelta);

		// update the shared state, only once per frame
		if (m_sharedState)
			m_sharedState->Update(ctx);

		UpdateScreenshotBatcher(ctx);
	}
	void PreviewPanel::UpdateScreenshotBatcher( const UpdateContext& ctx )
	{
		if ( !m_isRunningScreenshotBatch )
		{
			return;
		}

		if ( !m_screenshotCoordsDump )
		{
			if ( !m_batchedScreenshotsData.Empty() )
			{
				BatchedScreenshotData& data = m_batchedScreenshotsData.Back();

				const CameraLocation& newLocation = data.m_cameraLocation;
				const CameraLocation prevLocation = GetCamera()->GetCameraLocation();

				if ( prevLocation != newLocation )
				{
					GetCamera()->SetCameraLocation( newLocation );
					return;
				}

				Bool isStreaming = m_sharedState->HasStreaming() && m_sharedState->IsStreaming();
				if ( !isStreaming )
				{
					if ( !m_screenshotTaken && data.m_delayTime < m_screenshotBatchData->m_delayTime / 2 )
					{
						m_screenshotData->m_filePrefix = data.m_prefixStr;
						TakeFullScreenshot( *m_screenshotData.Get() );
						m_screenshotTaken = true;
					}

					if ( data.m_delayTime > 0.0f )
					{
						data.m_delayTime -= ctx.m_timeDelta;
					}
					else
					{
						m_batchedScreenshotsData.PopBack();
						m_screenshotTaken = false;
					}
				}
			}
			else
			{
				StopScreenshotBatch();
			}
		}
		// start coords dump
		else
		{
			Uint32 coordsDumped = 0;
			for ( Uint32 i = 0; i < m_batchedScreenshotsData.Size(); ++i )
			{
				Float& delayTime = m_batchedScreenshotsData[ i ].m_delayTime;
				if ( delayTime > 0.0f )
				{
					delayTime -= ctx.m_timeDelta;
					if ( delayTime <= 0.0f )
					{
						m_batchedScreenshotsData[ i ].m_cameraLocation = GetCamera()->GetCameraLocation();
						coordsDumped += 1;
						RED_LOG( "Camera coords stored: %d of %d ", coordsDumped, m_batchedScreenshotsData.Size() );
					}
					break;
				}
				else
				{
					++coordsDumped;
				}
			}
			// all coords stored, write to disk
			if ( coordsDumped == m_batchedScreenshotsData.Size() && !m_screenshotCoordsResource.IsEmpty() )
			{
				THandle< tools::CameraClipboardCoordinates > resList = red::CreateUniquePtr< tools::CameraClipboardCoordinates>();
				for ( const auto& data : m_batchedScreenshotsData )
				{
					resList->m_entries.PushBack( ToStringDirect( data.m_cameraLocation ) );
				}
				serialization::json::JsonSerializer serializer( resList );

				red::String jsonText;
				serializer.Serialize( jsonText );

				auto file = res::GetEditorDepot()->CreateResourceWriter( m_screenshotCoordsResource );
				file->Serialize( jsonText.AsChar(), jsonText.Length() );

				StopScreenshotBatch();
			}
		}
	}

	void PreviewPanel::GenerateGridJson()
	{
		auto coordinates = CreateHandle< CameraClipboardCoordinates >();

		for ( Uint32 y = 0; y < m_gridGeneratorData->m_numberOfYSteps; ++y )
		{
			for ( Uint32 x = 0; x < m_gridGeneratorData->m_numberOfXSteps; ++x )
			{
				CameraLocation cameraLocation;
				cameraLocation.m_position = m_gridGeneratorData->m_startingPosition + Vector3( x * m_gridGeneratorData->m_xStep, y * m_gridGeneratorData->m_yStep, 0 );
				cameraLocation.m_rotation = m_gridGeneratorData->m_rotation;
				cameraLocation.m_projectionMode = EProjectionMode::PM_OrthoMinusZ;
				cameraLocation.m_mode = EMode::Free;
				cameraLocation.m_orbitDistance = m_gridGeneratorData->m_orbitDistance;
				cameraLocation.m_zoom = m_gridGeneratorData->m_zoom;

				coordinates->m_entries.PushBack( ToStringDirect( cameraLocation ) );

				const Uint32 numberOfDigits = Uint32( log10( m_gridGeneratorData->m_numberOfXSteps * m_gridGeneratorData->m_numberOfYSteps ) );
				const String format = String::Printf( "%%0%dd", numberOfDigits );
				coordinates->m_prefixes.PushBack( String::Printf( format.AsChar(), x + m_gridGeneratorData->m_numberOfXSteps * ( m_gridGeneratorData->m_numberOfYSteps - 1 - y ) ) );
			}
		}

		serialization::json::JsonSerializer serializer( coordinates );

		String jsonText;
		serializer.Serialize( jsonText );

		res::IBackendResourceDepot* depot = GetSharedData()->GetResourceBackend().GetResourceDepot();
		res::ResourcePath resPath = depot->TranslateAbsolutePath( m_screenshotBatchData->m_batchPositionsPath );

		auto file = res::GetEditorDepot()->CreateResourceWriter( resPath );
		file->Serialize( jsonText.AsChar(), jsonText.Length() );
	}

	void PreviewPanel::OnEditModeChanged()
	{
		// reset IO between edit modes
		CancelInputHandler();
		CancelActiveDragDropOperation();
	}

	void PreviewPanel::RenderDebugFragments( rend::IDebugDrawer& dd )
	{
	}

	void PreviewPanel::RenderDebugOvelay( rend::IDebugDrawer& dd )
	{
	}

	void PreviewPanel::OnRender( RenderContext& ctx )
	{
		// Fill frame info reference position
		{
			ctx.m_frameInfo.m_cameraReferencePosition = ctx.m_cameraInfo.GetRenderCamera().GetPosition().AsVector3();
		}

		// Set environment data to frame info
		InitializeEnvironmentInfo( ctx.m_frameInfo );
		PerformOverrides( ctx.m_frameInfo );

		if ( !m_motionBlurEnabled )
		{
			ctx.m_frameInfo.m_worldRenderSettings.m_blurs.m_motionBlurStrength = 0;
		}
		
		if ( GetSharedData()->GetRenderScene() )
		{
			
			GRender->GetCommands()->UseExtendedDynamicRangeSky( GetSharedData()->GetRenderScene(), GetEnvironmentManager()->IsEnvPreviewPresetSelected() );
		}

		// setup debug preview settings
		{
			const SZoomState zoomState = m_zoomData.GetState( GetZoomTime() );

			SDebugPreviewSettings &debugSett = ctx.m_frameInfo.m_debugPreviewSettings;
			
			Float resScaleX, resScaleY;
			GRender->GetResolutionScale( resScaleX, resScaleY );
			debugSett.m_pointedPixelX = Int32( m_lastMouseX * resScaleX );
			debugSett.m_pointedPixelY = Int32( m_lastMouseY * resScaleY );
			debugSett.m_zoomDiv = zoomState.m_div;
			debugSett.m_zoomOffsetX = zoomState.m_offsetX;
			debugSett.m_zoomOffsetY = zoomState.m_offsetY;
		}

		// debug fragments
		RenderDebugFragments( ctx.m_cameraInfo.GetDebugDrawer() );

		// add the rendering mode
		if (m_sharedState)
			m_sharedState->Render(ctx.m_frameInfo, ctx.m_cameraInfo);

		// render all input listeners 
		if( ctx.m_camera )
		{
			for ( auto it : m_inputListeners )
				it->OnRender( *ctx.m_camera, ctx.m_cameraInfo.GetDebugDrawer() );
		}

		// render drag&drop object
		if ( m_dragDropHandler )
			m_dragDropHandler->OnRender( *ctx.m_camera, ctx.m_cameraInfo.GetDebugDrawer() );

		// input handler interface is rendered AFTER normal stuff
		if ( m_inputHandler && ctx.m_camera )
			m_inputHandler->OnRender( *ctx.m_camera, ctx.m_cameraInfo.GetDebugDrawer() );

		if ( m_inputCommand && ctx.m_camera )
		{
			m_inputCommand->OnRender( *ctx.m_camera, ctx.m_cameraInfo.GetDebugDrawer() );
		}

		// add world to the rendering frame
		if (m_sharedState)
			ctx.m_frameInfo.SetScene(m_sharedState->GetRenderScene());
	}

	Bool PreviewPanel::OnInput( input::IInputContext& ctx )
	{
		// backend drag&drop processing
		bool consumed = HandleLocalDragAndDrop( ctx );

		m_shortcutInputHandler->UpdatePressedModifiers( ctx );

		// normal processing
		for ( auto& it : ctx.GetBufferedInputData() )
		{
			if ( it.m_type == input::BufferedInputEvent::Type::Raw )
			{
				const auto raw = it.m_raw;

				if ( raw.m_key == IK_LeftMouse )
				{
					if( raw.IsClick() && HandleMouseClick(LMB, raw.m_action == IACT_DoubleClick, raw.m_x, raw.m_y) )
					{
						it.Consume();
                        consumed = true;
					}
					else if( raw.IsButton() && HandleMouseButton( LMB, raw.m_action == IACT_Press, raw.m_x, raw.m_y ) )
					{
						it.Consume();
                        consumed = true;
					}
				}
				else if ( raw.m_key == IK_MiddleMouse )
				{
					if( raw.IsClick() && HandleMouseClick(MMB, raw.m_action == IACT_DoubleClick, raw.m_x, raw.m_y) )
					{
						it.Consume();
                        consumed = true;
					}
					else if( raw.IsButton() && HandleMouseButton( MMB, raw.m_action == IACT_Press, raw.m_x, raw.m_y ) )
					{
						it.Consume();
                        consumed = true;
					}
				}
				else if ( raw.m_key == IK_RightMouse )
				{
					if( raw.IsClick() && HandleMouseClick(RMB, raw.m_action == IACT_DoubleClick, raw.m_x, raw.m_y) )
					{
						it.Consume();
                        consumed = true;
					}
					else if( raw.IsClick() && HandleMouseClick( RMB, raw.m_action == IACT_Click, raw.m_x, raw.m_y ) )
					{
						it.Consume();
                        consumed = true;
					}
					else if ( raw.IsButton() && HandleMouseButton( RMB, raw.m_action == IACT_Press, raw.m_x, raw.m_y ) )
					{
						it.Consume();
                        consumed = true;
					}
				}			

				// mouse movement
				else if ( raw.m_key == IK_MouseX )
				{
					if ( HandleMouseMovement( raw.m_x, raw.m_y, raw.m_data, 0.0f ) )
					{
						it.Consume();
                        consumed = true;
					}
				}
				else if ( raw.m_key == IK_MouseY )
				{
					if ( HandleMouseMovement( raw.m_x, raw.m_y, 0.0f, raw.m_data ) )
					{
						it.Consume();
                        consumed = true;
					}
				}

				// mouse hovering (mouse movement over unfocused window)
				else if ( raw.m_key == IK_MouseHover )
				{
					if ( HandleMouseMovement( raw.m_x, raw.m_y, 0.0f, 0.0f ) )
					{
						it.Consume();
                        consumed = true;
					}
				}

				// key press // release
				else if ( raw.m_action == IACT_Press )
				{
					if ( HandleKeyPress( raw.m_key ) )
					{
						it.Consume();
                        consumed = true;
					}
				}
				else if ( raw.m_action == IACT_Release )
				{
					if ( HandleKeyRelease( raw.m_key ) )
					{
						it.Consume();
                        consumed = true;
					}
				}

				// mouse wheel
				else if ( raw.m_key == IK_MouseZ && raw.m_action == IACT_Axis )
				{
					if ( HandleMouseWheel( raw.m_data, raw.m_x, raw.m_y ) )
					{
						it.Consume();
                        consumed = true;
					}
				}
			}
		}

		// Handle delayed cursor show/hide requests.
		if ( m_requestMouseCursorHide | m_requestMouseCursorShow )     // Optimization, we do not access InputContext unless we actually want to change something.
		{
			// Hiding and showing cursor cause mouse operations to fail if two or more editor are open at the same time
			// This code will be uncommented once we fix Hide/ShowCursor
// 			Bool isMouseCursorVisible = ctx.IsCursorVisible();
// 
// 			if ( m_requestMouseCursorHide & isMouseCursorVisible )
// 				ctx.HideCursor( "MeshPreviewPanel cursor show-hide request" );
// 			if ( m_requestMouseCursorShow & !isMouseCursorVisible )
// 				ctx.ShowCursor( "MeshPreviewPanel cursor show-hide request" );
// 
// 			m_requestMouseCursorHide = m_requestMouseCursorShow = false;
		}

        return consumed;
	}

	void PreviewPanel::OnEditorMessage(const String& message, const void* userData)
	{
		// nothing
	}

	void PreviewPanel::RenderSelectionMap( const Rect& selectionRect, red::SharedPtr<rend::SelectionResult>& selectionResult, Bool buffered, Bool isMultiLayer ) const
	{
		m_viewport->RenderSelectionMap( selectionRect, selectionResult, buffered, isMultiLayer );
	}

	void PreviewPanel::RenderDepthMap( const Rect& viewportRect, red::SharedPtr<rend::DepthResult>& depthResult, red::SharedPtr<rend::Color10Result> normalResult ) const
	{
		m_viewport->RenderDepthMap( viewportRect, depthResult, normalResult );
	}

	Bool PreviewPanel::GetPointAt( Uint32 x, Uint32 y, Vector3& outPoint ) const
	{
		const Rect viewportRect( x, x + 1, y, y + 1 );

		auto depthMap = red::CreateSharedPtr< rend::DepthResult >( red::PoolBackend() );
		m_viewport->RenderDepthMap( viewportRect, depthMap );

		// Get the world space position from the depth map
		const Float maxZ = 8000.0f; // TODO no depth limit
		if ( depthMap && depthMap->CalcWorldSpacePoint( x, y, outPoint, maxZ ) )
		{
			return true;
		}

		return false;
	}

	Bool PreviewPanel::CameraJumpTo( Uint32 x, Uint32 y )
	{
		Vector3 depthWorldPoint;
		if ( !GetPointAt( x, y, depthWorldPoint ) )
		{
			return false;
		}

		const Box focusBounds( depthWorldPoint, 2.0f );
		FocusOnBounds( focusBounds );
		return true;
	}

	void PreviewPanel::SetupEditModeViewport( EditModeViewport& vpt ) const
	{
		// physical size of the viewport
		vpt.m_viewportWidth = m_viewport->GetWidth();
		vpt.m_viewportHeight = m_viewport->GetHeight();

		// current camera
		m_viewport->CalcCamera( vpt.m_camera );
	}

	Bool PreviewPanel::IsMouseCursorOverPreviewPanel() const
	{
		POINT p;
		::GetCursorPos( &p );

		HWND window = (HWND)m_viewport->GetEngineViewport()->GetViewportID();

		RECT rect;
		::GetWindowRect( window, &rect );

		if ( p.x >= rect.left && p.x < rect.right && p.y >= rect.top && p.y < rect.bottom )
			return true;

		return false;
	}

	void PreviewPanel::SelectObject( const EditorObjectIDPath& )
	{
	}

	void PreviewPanel::OnGainFocus()
	{
		m_sharedState->OnGainFocus();
	}

	void PreviewPanel::OnLoseFocus()
	{
		m_sharedState->OnLoseFocus();
	}

	Bool PreviewPanel::HandleZoom( const Bool zoomOut, const Int32 x, const Int32 y )
	{
		RED_ASSERT( m_viewport );
		if ( m_viewport )
		{
			const Int32 zoomShiftOffset = zoomOut ? -1 : 1;
			const Float zoomTime = GetZoomTime();
					
			RED_ASSERT( m_zoomData.m_targetState.m_div >= 1.f );
			const SZoomState oldState = m_zoomData.GetState( zoomTime );
			const Int32 oldZoomShift = MLog2( (Int32)(Max( 1.f, m_zoomData.m_targetState.m_div ) + 0.1f) );
			const Int32 newZoomShift = Clamp<Int32>( oldZoomShift + zoomShiftOffset, 0, 6 );

			if ( newZoomShift != oldZoomShift )
			{
				SZoomState newState;
				if ( newZoomShift > 0 )
				{
					const Int32 clickPosX = (Int32)((x / oldState.m_div) + oldState.m_offsetX);
					const Int32 clickPosY = (Int32)((y / oldState.m_div) + oldState.m_offsetY);
					newState.m_div = powf( 2.f, (Float)newZoomShift );
					newState.m_offsetX = clickPosX - (x / newState.m_div);
					newState.m_offsetY = clickPosY - (y / newState.m_div);
				}

				m_zoomData.SetZoomTarget( zoomTime, zoomTime, newState );
			}
		}

		return true;
	}

	void PreviewPanel::NotifyInputListeners( EInputKey key, Bool isDown )
	{
		for ( auto it : m_inputListeners )
		{
			it->OnKeyEvent( key, isDown );
		}
	}

	void PreviewPanel::NotifyInputListeners( EMouseButton button, Bool isDown )
	{
		switch ( button )
		{
		case LMB:
			NotifyInputListeners( EInputKey::IK_LeftMouse, isDown );
			break;

		case RMB:
			NotifyInputListeners( EInputKey::IK_RightMouse, isDown );
			break;

		case MMB:
			NotifyInputListeners( EInputKey::IK_MiddleMouse, isDown );
			break;
		}
	}

	Bool PreviewPanel::HandleMouseButton( const EMouseButton button, const Bool state, const Int32 x, const Int32 y )
	{
		// TODO: It not the best place for that (InputSystem could be better)
		// update mouse button mask
		if ( state )
		{
			m_mouseButtonMask |= button;
			::SetCapture( (HWND)m_viewport->GetEngineViewport()->GetViewportID() );
		}
		else
		{
			m_mouseButtonMask &= ~button;

			if ( m_mouseButtonMask == 0 )
			{
				::ReleaseCapture();
			}
		}

		// pass to edit mode
		{
			// transform coords by zoom
			Int32 zoomedX, zoomedY;
			m_zoomData.TransformCoords( GetZoomTime(), x, y, zoomedX, zoomedY );

			EditModeViewport vpt;
			SetupEditModeViewport( vpt );

			if ( state )
			{
				const InputMouseButton evt( button, m_mouseButtonMask, zoomedX, zoomedY, vpt, state );
				if ( HandleDirectMouseDown( evt ) )
					return true;
			}
			else
			{
				const InputMouseButton evt( button, m_mouseButtonMask, zoomedX, zoomedY, vpt, state );
				if ( HandleDirectMouseUp( evt ) )
				{
					const InputMouseMove fakeEvt( m_mouseButtonMask, zoomedX, zoomedY, 0, 0, 0.0f, 0.0f, m_viewport->GetViewportID(), vpt );
					HandleDirectMouseMoved( fakeEvt );

					return true;
				}
			}
		}

		// not handled by default
		return false;
	}

	Bool PreviewPanel::HandleMouseClick( const EMouseButton button, const Bool dbl, const Int32 x, const Int32 y )
	{
		// pass to edit mode
		{
			// transform coords by zoom
			Int32 zoomedX, zoomedY;
			m_zoomData.TransformCoords( GetZoomTime(), x, y, zoomedX, zoomedY );

			EditModeViewport vpt;
			SetupEditModeViewport( vpt );

			const InputMouseClick evt( button, button, zoomedX, zoomedY, vpt, dbl );

			if ( dbl )
			{
				if ( HandleDirectMouseDoubleClick( evt ) )
					return true;
			}
			else
			{
				if ( HandleDirectMouseClick( evt ) )
					return true;
			}
		}

		return false;
	}

	Bool PreviewPanel::HandleMouseMovement( const Int32 x, const Int32 y, const Float rawDx, const Float rawDy )
	{
		// transform coords by zoom
		Int32 zoomedX, zoomedY;
		m_zoomData.TransformCoords( GetZoomTime(), x, y, zoomedX, zoomedY );

		EditModeViewport vpt;
		SetupEditModeViewport( vpt );

		const InputMouseMove evt( m_mouseButtonMask, zoomedX, zoomedY, m_lastMouseX - x, m_lastMouseY - y, rawDx, rawDy, m_viewport->GetViewportID(), vpt );
		if ( HandleDirectMouseMoved( evt ) )
			return true;

		// not handled by default
		return false;
	}

	Bool PreviewPanel::HandleMouseWheel( const Float delta, const Int32 x, const Int32 y )
	{
		EditModeViewport vpt;
		SetupEditModeViewport( vpt );

		const InputMouseWheel evt( delta, m_mouseButtonMask, x, y, vpt );
		if ( HandleDirectMouseWheel( evt ) )
		{
			return true;
		}
		return false;
	}

	Bool PreviewPanel::HandleKeyPress( const EInputKey key )
	{
		// pass to edit mode
		const InputKeyEvent evt( key );
		if ( HandleDirectKeyDown( evt ) )
			return true;

		Bool shiftDown = false;
		if ( GetKeyState( VK_SHIFT ) & 0x8000 )
		{
			shiftDown = true;
		}

		// zoom ( - / = )
		if ( shiftDown && ( key == IK_Equals || key == IK_Minus ) )
		{
			const Bool isZoomOut = key == IK_Minus;
			if ( HandleZoom( isZoomOut, m_lastMouseX, m_lastMouseY ) )
			{
				return true;
			}
		}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
		if( key == IK_F11 )
		{
			GRender->EnqueueReloadSimpleShaders();
		}
		else if ( key == IK_F9 )
		{
			RecompileLoadedMaterials();
		}
#endif	//NO_RUNTIME_MATERIAL_COMPILATION

		// not handled by default
		return false;	
	}

	Bool PreviewPanel::HandleKeyRelease( const EInputKey key )
	{
		const InputKeyEvent evt( key );
		if ( HandleDirectKeyUp( evt ) )
			return true;

		// not handled by default
		return false;
	}

	Bool PreviewPanel::HandleLocalDragAndDrop( input::IInputContext& ctx )
	{
		// look for drag&drop events in the stream
        bool consumed = false;
		for ( auto& evt : ctx.GetBufferedInputData() )
		{
			if ( evt.m_type == input::BufferedInputEvent::Type::DragDrop )
			{
				const auto& dd = evt.m_dragDrop;
				switch ( dd.m_action )
				{
					case input::BufferedInputEvent::DragDropAction::Enter:
					{
						if ( HandleDragEnter( "text", dd.m_text, dd.m_x, dd.m_y ) )
                        {
							evt.Consume();
                            consumed = true;
                        }
						break;
					}

					case input::BufferedInputEvent::DragDropAction::Leave:
					{
						HandleDragLeave( dd.m_text );
						evt.Consume();
                        consumed = true;
						break;
					}

					case input::BufferedInputEvent::DragDropAction::Over:
					{
						HandleDragOver( dd.m_text, dd.m_x, dd.m_y );
						evt.Consume();
                        consumed = true;
						break;
					}


					case input::BufferedInputEvent::DragDropAction::Drop:
					{
						if ( HandleDrop( dd.m_text, dd.m_x, dd.m_y ) )
                        {
							evt.Consume();
                            consumed = true;
                        }
						break;
					}

					default:
						RED_ASSERT( false, "Invalid drag&drop action" );
				}

			}
		}

        return consumed;
	}

	Bool PreviewPanel::HandleDragEnter( const String& formats, const String& data, Int32 x, Int32 y )
	{
		// transform coords by zoom
		m_zoomData.TransformCoords( GetZoomTime(), x, y, x, y );

		EditModeViewport vpt;
		SetupEditModeViewport( vpt );

		const InputMouseMove evt( LMB, x, y, 0, 0, 0.0f, 0.0f, m_viewport->GetViewportID(), vpt );
		return HandleDirectDragEnter( data, evt );
	}

	Bool PreviewPanel::HandleDragOver( const String& data, Int32 x, Int32 y )
	{
		// transform coords by zoom
		m_zoomData.TransformCoords( GetZoomTime(), x, y, x, y );

		EditModeViewport vpt;
		SetupEditModeViewport( vpt );

		const InputMouseMove evt( LMB, x, y, 0, 0, 0.0f, 0.0f, m_viewport->GetViewportID(), vpt );
		return HandleDirectDragOver( evt );
	}

	void PreviewPanel::HandleDragLeave( const String& data )
	{
		HandleDirectDragLeave();
	}

	Bool PreviewPanel::HandleDrop( const String& data, Int32 x, Int32 y )
	{
		// transform coords by zoom
		m_zoomData.TransformCoords( GetZoomTime(), x, y, x, y );
		EditModeViewport vpt;
		SetupEditModeViewport( vpt );

		const InputMouseButton evt( RMB, 0, x, y, vpt, false );
		return HandleDirectDragDrop( evt );
	}

	void PreviewPanel::SetupInterop( PreviewPanel* panel, ::interop::utils::Object& op )
	{
		// the window
		op.RegisterObject( "window", [panel](interop::IContext& ctx)
		{
			return panel->m_viewport->CreateInterop();
		});

		op.RegisterValueRO< String >( "selectionInfo", [panel]()
		{ 
			const Vector3 cameraPosition = panel->m_viewport->GetCameraPosition();

			PreviewPanelSharedData::SelectionInfo info;

			if( panel->GetSharedData()->CalculateSelectionInfo( cameraPosition, info ) )
			{
				return String::Printf( "Distance: %.2f ( %d %s )", info.m_minDistance, info.m_numItems, info.m_numItems > 1 ? "nodes" : "node" );
			}

			return String( "No selection" );
		} );

		// env settings
		op.RegisterObject( "envSettings", panel->m_envManagerCollection->GetRootInterop() );

		op.RegisterObject( "screenshotData", panel->m_screenshotDataCollection->GetRootInterop() );
		op.RegisterFunctor< THandle< rend::SingleScreenShotData > >( "getScreenshotData", [panel] ( interop::IContext& ctx )
		{
			return panel->m_screenshotData;
		} );

		op.RegisterFunctor< void, THandle< rend::SingleScreenShotData > >( "setScreenshotData", [panel] ( interop::IContext& ctx, const THandle< rend::SingleScreenShotData >& data )
		{
			panel->m_screenshotData = data;
			panel->m_screenshotDataCollection->SetObject( panel->m_screenshotData.Get() );
		} );

		op.RegisterObject( "screenshotBatchData", panel->m_screenshotBatchCollection->GetRootInterop() );

		op.RegisterObject( "gridGeneratorData", panel->m_gridGeneratorCollection->GetRootInterop() );

		// Notification about rick-click menu
		op.RegisterNotifier( "showEditorContextMenu", panel->m_contextMenuNotifier );

		// An object was dragged onto the preview
		op.RegisterFunctor<Bool, String, Uint32, Uint32>("onDragEnter", 
			[panel](interop::IContext& ctx, const String& data, const Uint32& x, const Uint32& y ) -> Bool
		{ 
			return panel->HandleDragEnter( "", data, x, y );
		});

		// An object is being dragged over the preview
		op.RegisterFunctor<void, String, Uint32, Uint32>("onDragOver", 
			[panel](interop::IContext& ctx, const String& data, const Uint32& x, const Uint32& y )
		{ 
			panel->HandleDragOver( data, x, y );
		});

		// Dragged object left the preview
		op.RegisterFunctor<void, String>("onDragLeave", 
			[panel](interop::IContext& ctx, const String& data )
		{ 
			panel->HandleDragLeave( data );
		});

		// Dragged object wad dropped onto the preview
		op.RegisterFunctor<Bool, String, Uint32, Uint32>("onDrop", 
			[panel](interop::IContext& ctx, const String& data, const Uint32& x, const Uint32& y ) -> Bool
		{ 
			return panel->HandleDrop( data, x, y );
		});

		op.RegisterFunctor< String, Uint32, Uint32, String, Uint32 >( "takeScreenshot",
			[ panel ]( interop::IContext& ctx, Uint32 width, Uint32 height, String label, Uint32 flags )
		{
			return panel->TakeScreenshot( width, height, label, flags );
		});

		op.RegisterFunctor<void>( "startScreenshotBatch", [ panel ]( interop::IContext& ctx )
		{
			const red::AbsolutePath batchPath = panel->m_screenshotBatchData->m_batchPositionsPath;
			const Float delayTime = panel->m_screenshotBatchData->m_delayTime;
			const Uint32 dumpCoordsCount = panel->m_screenshotBatchData->m_numberOfCoordinatesToDump;

			res::IBackendResourceDepot* depot = panel->GetSharedData()->GetResourceBackend().GetResourceDepot();
			res::ResourcePath resPath = depot->TranslateAbsolutePath( batchPath );
			res::ResourceTokenHandle loadingToken = GResourceLoader->IssueLoadingRequest( resPath );
			loadingToken->WaitUntilLoaded();

			red::DynArray< std::pair< CameraLocation, String > > tempArray;
			if ( dumpCoordsCount == 0 )
			{
				THandle< JsonResource > jsonResource = Cast< JsonResource >( loadingToken->GetResource() );
				if ( !jsonResource )
				{
					RED_LOG_ERROR( "Unable to read file: %hs", resPath.ToDebugString() );
				}
				else if ( THandle< CameraClipboardCoordinates > resList = jsonResource->GetRoot< CameraClipboardCoordinates >() )
				{
					const Uint32 coordsCount = resList->m_entries.Size();
					if ( coordsCount > 0 )
					{
						for ( Int32 i = ( coordsCount - 1 ); i >= 0; --i )
						{
							const String& prefix = resList->m_prefixes.Size() > (Uint32)i ? resList->m_prefixes[ i ] : String::EMPTY();
							tempArray.PushBack( std::make_pair( FromStringDirect< CameraLocation >( resList->m_entries[ i ] ), prefix ) );
						}
						panel->TakeBatchFullScreenshot( *panel->m_screenshotData, tempArray, delayTime );
					}
				}
			}
			else
			{
				panel->TakeBatchFullScreenshot( *panel->m_screenshotData, tempArray, delayTime, dumpCoordsCount, resPath );
			}
		} );

		op.RegisterFunctor<void>( "stopScreenshotBatch", [ panel ]( interop::IContext& ctx )
		{
			panel->StopScreenshotBatch();
		} );

		op.RegisterValueRO<Bool>( "isBatchRunning", &panel->m_isRunningScreenshotBatch, &panel->m_runningScreenshotBatchNotifier );

		op.RegisterFunctor<void>( "takeFullScreenshot", [ panel ]( interop::IContext& ctx )
		{
			panel->TakeFullScreenshot( *panel->m_screenshotData );
		} );

		op.RegisterNotifier( "_onScreenshotTaken", panel->m_screenshotTakenNotifier );

		op.RegisterFunctor<void>( "generateGridJson", [ panel ]( interop::IContext& ctx )
		{
			panel->GenerateGridJson();
		} );

		// Zoom out to see the whole stuff
		op.RegisterFunctor<Bool>( "zoomOut",
			[panel]( interop::IContext& ctx )
		{
			panel->ZoomOut( false );
			return true;
		} );

		op.RegisterFunctor<void>( "toggleOrbit", [ panel ]( interop::IContext& )
		{
			const Bool orbitEnabled = panel->IsOrbitCameraEnabled();
			panel->EnableOrbitCamera( !orbitEnabled );
		} );

		op.RegisterValue<Bool>( "orbit", [ panel ]( interop::IContext&, Bool& outValue )
		{
			outValue = panel->IsOrbitCameraEnabled();
		},
		[ panel ]( interop::IContext&, const Bool& newValue )
		{
			panel->EnableOrbitCamera( newValue );
		}, &panel->m_orbitNotifier );

		op.RegisterValue< Bool >( "motionBlurEnabled",
		[ panel ]( interop::IContext&, Bool& outValue )
		{
			outValue = panel->m_motionBlurEnabled;
		},
		[ panel ]( interop::IContext&, const Bool& newValue )
		{
			panel->m_motionBlurEnabled = newValue;
		} );

		// TEMP - until the front end size gets fixed we need to expose all interop stuff from the shared data directly in the preview panel
		panel->GetSharedData()->ExposeInterop( op );
	}

	String PreviewPanel::TakeScreenshot( Uint32 width, Uint32 height, const String& label, Uint32 flags )
	{
		auto result = red::CreateSharedPtr<rend::ColorResult>( red::PoolBackend() );

		CRenderFrameInfo frameInfo;
		CRenderCamera renderCamera;
		InitializeScreenshotFrame( frameInfo, renderCamera, width, height );
		frameInfo.m_colorResult = result;

		if ( flags & RED_FLAG( ScreenshotFlags::DisableMotionBlur ) )
		{
			frameInfo.m_worldRenderSettings.m_blurs.m_motionBlurStrength = 0;
		}

		const SCameraRenderViewport cameraRenderViewport = SCameraRenderViewport( 0, Rect( 0, width, 0, height ), Rect( 0, width, 0, height ) );
		utilities::Screenshot::RenderFrame( frameInfo, renderCamera, cameraRenderViewport );

		img::ImagePtr image = utilities::Screenshot::ToImagePtr( *result, width, height );

		red::AbsolutePath path = GFileManager->GenerateTemporaryFilePath( label );

		path.AppendFilePath( ".png" );

		if ( utilities::Screenshot::Save( path, image ) )
		{
			return path.AsString();
		}

		return String::EMPTY();
	}

	void PreviewPanel::InitializeScreenshotFrame( CRenderFrameInfo& info, CRenderCamera& renderCamera, Uint32 width, Uint32 height )
	{
		CRenderFrameSetup frameSetup( width, height );

		const auto& scene = GetSharedData()->GetRenderScene();
		const Float aspectRatio = width / static_cast<Float>( height );

		info = CRenderFrameInfo( frameSetup );
		info.SetScene( scene );
		info.SetEngineTime( 0.0f );
		info.SetCleanEngineTime( 0.0f );

	    GetEnvironmentManager()->PerformOverrides( info );

		m_viewport->GetCamera()->PrepareCamera( aspectRatio, renderCamera );
	}

	void PreviewPanel::TakeFullScreenshot( const rend::SingleScreenShotData& screenshotData, Bool allowDebugDraw )
	{
		GetSharedData()->ForceNoProxies( true );

		rend::SingleScreenShotData params = screenshotData;

		red::DateTime currentTime;
		red::Clock::GetInstance().GetLocalTime( currentTime );

		if ( red::paths::ExtractExtension( params.m_outputPath ).Empty() )
		{
			const String fileName = String::Printf( "%hsframe%d_%d", screenshotData.m_filePrefix.AsChar(), currentTime.GetDateRaw(), currentTime.GetTimeRaw() );
			params.m_outputPath.AppendFilePath( fileName );
		}

		params.m_includeDebugOverlay = allowDebugDraw;
		params.m_onCreatedCallback = [ this ]( const red::String& savePath, bool created ) -> void {
			if ( created )
			{
				m_screenshotTakenNotifier.Notify( savePath );
			}
		};
		m_viewport->GetEngineViewport()->TakeFullScreenshot( params );

		GetSharedData()->ForceNoProxies( false );
	}

	void PreviewPanel::TakeBatchFullScreenshot( const rend::SingleScreenShotData& screenshotData,
		const red::DynArray< std::pair< CameraLocation, String > >& coords, const Float delayTime, const Uint32 dumpCoordsCount, res::ResourcePath resPath )
	{
		if ( dumpCoordsCount > 0 )
		{
			for ( Uint32 i = 0; i < dumpCoordsCount; ++i )
			{
				m_batchedScreenshotsData.PushBack( { {}, delayTime, String::EMPTY() } );
				m_screenshotCoordsDump = true;
				m_screenshotCoordsResource = resPath;
			}
		}
		else
		{
			for ( const auto& cameraLocationWithPrefix : coords )
			{
				m_batchedScreenshotsData.PushBack( { cameraLocationWithPrefix.first, delayTime, cameraLocationWithPrefix.second } );
			}
			// prepare for the first shoot
			GetCamera()->SetCameraLocation( m_batchedScreenshotsData.Back().m_cameraLocation );
		}

		m_isRunningScreenshotBatch = true;
		m_runningScreenshotBatchNotifier.Notify( m_isRunningScreenshotBatch );
	}
	
	void PreviewPanel::StopScreenshotBatch()
	{
		m_batchedScreenshotsData.Clear();
		m_screenshotCoordsDump = false;
		m_screenshotCoordsResource = res::ResourcePath();

		m_isRunningScreenshotBatch = false;
		m_runningScreenshotBatchNotifier.Notify( m_isRunningScreenshotBatch );
	}

	///---

	Bool PreviewPanel::HandleDirectKeyDown( const InputKeyEvent& evt )
	{
		NotifyInputListeners( evt.m_key, true );

		// always pass the transient key events to the listeners for preview, they are not allowed to eat the event
		if ( IsModifierKey( evt.m_key ) )
		{
			for ( auto it : m_inputListeners )
				it->OnModifierKeyDown( evt.m_key );
		}

		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			// special case for the ESC key that cancels ALL input handlers, ALWAYS - this is universal behavior
			if ( evt.m_key == IK_Escape )
			{
				CancelInputHandler();
				CancelCommand();

				return true;
			}

			m_inputCommand->OnKeyDown( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnKeyDown( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnKeyDown( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		if ( m_inputHandler )
		{
			// special case for the ESC key that cancels ALL input handlers, ALWAYS - this is universal behavior
			if ( evt.m_key == IK_Escape )
			{
				CancelInputHandler();
			}
			else
			{
				ProcessInputHandlerChange( m_inputHandler->OnKeyDown( evt ) );
			}

            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if( it->OnKeyDown( *this, evt ) )
                return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectKeyUp( const InputKeyEvent& evt )
	{
		NotifyInputListeners( evt.m_key, false );

		// always pass the transient key events to the listeners for preview, they are not allowed to eat the event
		if ( IsModifierKey(evt.m_key) )
		{
			for ( auto it : m_inputListeners )
				it->OnModifierKeyUp( evt.m_key );
		}

		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			// special case for the ESC key that cancels ALL input handlers, ALWAYS - this is universal behavior
			if ( evt.m_key == IK_Escape )
			{
				CancelInputHandler();
				CancelCommand();

				return true;
			}

			// special case for the ESC key that cancels ALL input handlers, ALWAYS - this is universal behavior
			if ( evt.m_key == IK_Enter )
			{
				FinishCommand();

				return true;
			}

			m_inputCommand->OnKeyUp( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnKeyUp( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnKeyUp( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnKeyUp( evt ) );
			return false;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnKeyUp( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectMouseDown( const InputMouseButton& evt )
	{
		NotifyInputListeners( evt.m_button, true );

		// cancel drag&drop
		CancelActiveDragDropOperation();

		if ( evt.m_mask )
		{
			m_mouseTotalTravel = 0;
		}

		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			m_inputCommand->OnMouseDown( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseDown( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseDown( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseDown( evt ) );
            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnMouseDown( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectMouseUp( const InputMouseButton& evt )
	{
		NotifyInputListeners( evt.m_button, false );

		// cancel drag&drop
		CancelActiveDragDropOperation();

		if ( evt.m_mask )
		{
			m_mouseTotalTravel = 0;
		}

		// all input is consumed by input handler
		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			m_inputCommand->OnMouseUp( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseUp( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseUp( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseUp( evt ) );
            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnMouseUp( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectMouseClick( const InputMouseClick& evt )
	{
		// cancel drag&drop
		CancelActiveDragDropOperation();

		if ( evt.m_mask )
		{
			m_mouseTotalTravel = 0;
		}

		// all input is consumed by input handler
		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			m_inputCommand->OnMouseClick( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseClick( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseClick( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseClick( evt ) );
            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnMouseClick( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectMouseDoubleClick( const InputMouseClick& evt )
	{
		// cancel drag&drop
		CancelActiveDragDropOperation();

		if ( evt.m_mask )
		{
			m_mouseTotalTravel = 0;
		}

		// all input is consumed by input handler
		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			m_inputCommand->OnMouseDoubleClick( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseDoubleClick( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseDoubleClick( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseDoubleClick( evt ) );
            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnMouseDoubleClick( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectMouseMoved( const InputMouseMove& evt )
	{
		if ( evt.m_mask )
		{
			if ( evt.m_dx || evt.m_dy )
			{
				m_mouseTotalTravel += 1; // hack
			}
		}

		// all input is consumed by input handler
		IPreviewCommand::InputResult inputResult;
		if ( m_inputCommand )
		{
			// store last mouse position
			m_lastMouseX = evt.m_x;
			m_lastMouseY = evt.m_y;

			m_inputCommand->OnMouseMoved( evt, inputResult );

			if ( !inputResult.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !inputResult.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseMoved( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( inputResult.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseMoved( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return inputResult.m_handled;
		}
		
		Bool result = false;

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseMoved( evt ) );
            result = true;
		}
		else
		{
			// pass to the listener stack and if something processes it than eat the input
			for ( auto it : m_inputListeners )
			{
				if ( it->OnMouseMoved( *this, evt ) )
				{
					result = true;
					break;
				}
			}
		}

		// store last mouse position
		m_lastMouseX = evt.m_x;
		m_lastMouseY = evt.m_y;

		// event was not handled by this edit mode and can continue it's journey
		return result;
	}

	Bool PreviewPanel::HandleDirectMouseWheel( const InputMouseWheel& evt )
	{
		// all input is consumed by input handler
		IPreviewCommand::InputResult result;
		if ( m_inputCommand )
		{
			m_inputCommand->OnMouseWheel( evt, result );

			if ( !result.m_keepLive )
			{
				CancelCommand();
			}
			else if ( !result.m_handled )
			{
				if ( m_inputHandler )
				{
					Bool result = m_inputHandler->OnMouseWheel( evt );
					ProcessInputHandlerChange( result );

					return result;
				}
				else
				{
					if ( result.m_processGizmoManager )
					{
						for ( auto it : m_inputListeners )
						{
							if ( it->GetName() == RED_NAME_CONSTEXPR_NOREG( "GizmoManager" ) )
							{
								if ( it->OnMouseWheel( *this, evt ) )
								{
									return true;
								}
							}
						}
					}
				}
			}

			return result.m_handled;
		}

		// all input is consumed by input handler
		if ( m_inputHandler )
		{
			ProcessInputHandlerChange( m_inputHandler->OnMouseWheel( evt ) );
            return true;
		}

		// pass to the listener stack and if something processes it than eat the input
		for ( auto it : m_inputListeners )
		{
			if ( it->OnMouseWheel( *this, evt ) )
				return true;
		}

		// event was not handled by this edit mode and can continue it's journey
		return false;
	}

	Bool PreviewPanel::HandleDirectDragEnter( const String& data, const InputMouseMove& evt )
	{
		// go through input listeners and check if we can start Drag&Drop operation with given data
		for ( auto* ptr : m_inputListeners )
		{
			auto* handler = ptr->OnDragEnter( *this, data, evt );
			if ( handler )
			{
				// cancel all other input modes
				CancelInputHandler();
				CancelActiveDragDropOperation();

				// start drag&drop
				m_dragDropHandler.Reset( handler );
				return true;
			}
		}

		// format not accepted
		return false;
	}

	Bool PreviewPanel::HandleDirectDragOver( const InputMouseMove& evt )
	{
		if ( m_dragDropHandler )
        {
			m_dragDropHandler->OnMouseMoved( evt );
            return true;
        }

        return false;
	}

	void PreviewPanel::HandleDirectDragLeave()
	{
		for ( auto* ptr : m_inputListeners )
			ptr->OnDragLeave();

		if ( m_dragDropHandler )
		{
			m_dragDropHandler->OnCanceled();
			m_dragDropHandler.Reset();
		}
	}

	Bool PreviewPanel::HandleDirectDragDrop( const InputMouseButton& evt )
	{
		for ( auto* ptr : m_inputListeners )
			ptr->OnDragDrop();

		if ( m_dragDropHandler )
		{
			Bool res = m_dragDropHandler->OnFinished( evt );
			m_dragDropHandler.Reset();
			return res;
		}
		return false;
	}

	red::SharedPtr< world::RuntimeScene > PreviewPanel::GetRuntimeScene()
	{
		return nullptr;
	}

	void PreviewPanel::CancelInputHandler()
	{
		if ( m_inputHandler )
		{
			// exit action
			m_inputHandler->OnCanceled();

			// notify listeners
			for ( auto listener : m_inputListeners )
				listener->OnInputHandlerRemoved( *this, m_inputHandler.Get(), true );

			m_inputHandler.Reset();
		}
	}

	void PreviewPanel::AttachInputHandler( red::UniquePtr< IPreviewInputHandler > handler )
	{
		CancelInputHandler();
		m_inputHandler = std::move( handler );
	}

	void PreviewPanel::CancelCommand()
	{
		if ( m_inputCommand )
		{
			// exit action
			m_inputCommand->OnCanceled();
			m_inputCommand.Reset();
		}
	}

	void PreviewPanel::FinishCommand()
	{
		if ( m_inputCommand )
		{
			// exit action
			m_inputCommand->OnFinished();
			m_inputCommand.Reset();
		}
	}

	void PreviewPanel::SetActiveCommand( red::SharedPtr< IPreviewCommand > command )
	{
		CancelCommand();
		m_inputCommand = std::move( command );
	}

	Bool PreviewPanel::IsCommandActive( IPreviewCommand* command ) const
	{
		return m_inputCommand.Get() == command;
	}

	void PreviewPanel::ProcessInputHandlerChange( Bool keepAlive )
	{
		if ( !keepAlive )
		{
			// notify listeners
			if ( m_inputHandler.Get() )
			{
				for ( auto listener : m_inputListeners )
				{
					listener->OnInputHandlerRemoved( *this, m_inputHandler.Get(), false );
				}
			}

			// close current handler
			m_inputHandler = nullptr;
		}
	}

	void PreviewPanel::AttachInputListener( IPreviewInputListener* listener, Bool atTheBack /*= true*/ )
	{
		RED_ASSERT( !m_inputListeners.Exist( listener ), "Input listener already registered" );

		if ( listener && !m_inputListeners.Exist( listener ) )
		{
			if ( atTheBack )
			{
				m_inputListeners.PushBack( listener );
			}
			else
			{
				m_inputListeners.InsertAt( 0, listener );
			}
		}
	}

	void PreviewPanel::DetachInputListener( IPreviewInputListener* listener )
	{
		CancelActiveDragDropOperation();
		m_inputListeners.Remove( listener );
	}

	void PreviewPanel::RequestMouseCursorShowHide( MouseCursorShowHideRequest request )
	{
		m_requestMouseCursorShow = ( request == MOUSE_CURSOR_SHOW );
		m_requestMouseCursorHide = ( request == MOUSE_CURSOR_HIDE );
	}

	void PreviewPanel::CancelActiveDragDropOperation()
	{
		if ( m_dragDropHandler )
		{
			m_dragDropHandler->OnCanceled();
			m_dragDropHandler.Reset();
		}
	}

	Bool PreviewPanel::IsOrbitCameraEnabled() const
	{
		return GetViewport()->GetCamera()->IsOrbiting();
	}

	void PreviewPanel::EnableOrbitCamera( Bool enable )
	{
		EnableOrbitCameraInternal( enable, Vector3::ZEROS() );
	}

	void PreviewPanel::EnableOrbitCameraInternal( Bool enable, const Vector3 orbitCenter )
	{
        if( IsOrbitCameraEnabled() == enable )
            return;

		RenderingWindowCamera* camera = GetViewport()->GetCamera();
		EulerAngles curCameraOrientation = camera->GetRotation();
		Vector3 curCameraPosition = camera->GetPosition();

		if ( enable )
		{	// We cannot keep current position and orientation at the same time, orbit mode is more restricted.
			Float orbitDistance = math::Max( (orbitCenter - curCameraPosition).Mag(), 0.01f );
			GetViewport()->SetupOrbitCamera( orbitCenter, curCameraOrientation, orbitDistance );
		}
		else
		{	// We keep the same position/rotation when switching to free camera.
			Vector4 forward;
			curCameraOrientation.ToAngleVectors( &forward, nullptr, nullptr );
			GetViewport()->SetupFreeCamera( curCameraPosition - forward.AsVector3() * camera->GetOrbitDistance(), curCameraOrientation );
		}

		m_orbitNotifier.Notify();
	}

	Vector3 PreviewPanel::GetCameraPos() const
	{
		return GetViewport()->GetCameraStreamingPosition();
	}
}
