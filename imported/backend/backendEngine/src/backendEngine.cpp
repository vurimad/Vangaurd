/**
* Copyright (c) 2013-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendEngine.h"
#include "gpuResourceCollector.h"

#include "engineStatsBackend.h"
#include "resourceBackend.h"
#include "resourceEditor.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/redReflection/include/scriptingSystem.h"
#include "../../../common/input/include/inputSystem.h"
#include "../../../common/interop/include/interopInterpreter.h"
#include "../../../common/sound/include/soundSystem.h"
#include "../../../common/interopServices/include/interopService.h"
#include "../../../common/commChannel/include/networkConnection.h"
#include "../../../common/app/include/logging.h"
#include "../../../common/shaderCache/include/shaderCacheManager.h"
#include "../../../common/renderData/include/viewport.h"
#include "../../../common/gameTweakDB/include/tweakDB.h"
#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/inkCore/include/inkSystem.h"
#include "../../../common/localization/include/localizationManager.h"
#include "../../../common/redCore/include/globalModeInfo.h"
#include "../../../common/engine/include/scriptErrorDialog.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../common/engine/include/baseEngineInitParams.h"
#include "../../common/renderData/include/renderer.h"
#include "../../common/redJobs2/include/jobBuilder.h"

//--------------------------------------------------------------

namespace Config
{
	TConfigVar< String > cvDefaultConstructorWorldName( "Engine/Backend", "DefaultConstructorWorld", "BoxDemo", eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
}

//--------------------------------------------------------------

RTTI_BEGIN_TYPE( CBackendEngine );
RTTI_PARENT_TYPE( CBaseEngine );
RTTI_END_TYPE();

CBackendEngine::CBackendEngine()
	: CBaseEngine( false )
	, m_streamingManagerTimer( "StreamingManagerTimer" )
{
	m_detachRequested = false;
	SetEditorMode();
}

CBackendEngine::~CBackendEngine()
{
}

void VerifyStaticShadersStatus( const EngineInitParams& params )
{
	RED_ASSERT( GStaticShaderCache->IsInit(), "Static ShaderCache is not initialized yet." );

	red::THash64 serializedDataHash = GStaticShaderCache->GetHash();
	red::THash64 currentDataHash = IStaticShaderCache::CalculateDirectoryHash( GFileManager->GetEngineRoot().AddDirPath( "shaders" ) );

	if( serializedDataHash != currentDataHash )
	{
		RED_LOG( "Status: Recompiling static shaders..." );

		CDateTime shadersTimestamp( GFileManager->GetFolderTimestamp( GFileManager->GetEngineRoot().AddDirPath( "shaders" ), true ) );
		const CDateTime& serializedTimestamp = GStaticShaderCache->GetTimestamp();

		String serializedTimestampString;
		ToString( serializedTimestampString, serializedTimestamp );
		String currentShadersTimestampString;
		ToString( currentShadersTimestampString, shadersTimestamp );

		RED_LOG_INFO( "Static shaders not up-to-date when starting editor:" );
		RED_LOG_INFO( "  Hashes:     cached [%" RED_PRIu64 "], current [%" RED_PRIu64 "]", serializedDataHash, currentDataHash );
		RED_LOG_INFO( "  Timestamps: cached [%hs], current [%hs]", serializedTimestampString.AsChar(), currentShadersTimestampString.AsChar() );

		// TODO: read info about this from a config file or a messagebox of some kind
		const Bool doesUserWantToRebuildStaticShaders = true;
		if( doesUserWantToRebuildStaticShaders )
		{
			GRender->EnqueueReloadSimpleShaders();

			GRender->FlushReloadSimpleShaders();
		}
	}
}

Bool CBackendEngine::OnRootSystemInitialization( const EngineInitParams& params )
{
	SoundSystemConfig audioInitConfig = SoundSystemConfig::EditorConfig();
	if( params.m_audioConfig )
	{
		audioInitConfig = SoundSystemConfig::GetConfig( params.m_audioConfig );
	}
	audio::SoundSystem::PreInit( audioInitConfig );

	// Initialize base engine
	if( !CBaseEngine::OnRootSystemInitialization( params ) )
		return false;

	RED_FATAL_ASSERT( ! IsHeadless(), "BackendEngine should never be headless" );
	if ( !LoadBackendGatheredResources() )
	{
		return false;
	}

	// Initialize editor debug filters
	InitializeDebugFilters( params.m_renderPreset );

	// Verify if static shader cache is up to date with the data on HDD
	//@todo Add some form of asking user if they want to recompile static shaders locally before running editor
	VerifyStaticShadersStatus( params );

	if( !InitializeInput() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize editor input system" );
		return false;
	}

	// Initialize streaming manager
	if( m_streamingManagerInstance.Initialize() )
	{
		const Float streamingCacheSavePeriod = 30.0f;

		m_streamingManagerTimer.ExecuteEvery( streamingCacheSavePeriod, [this]() {
			m_streamingManagerInstance.Save();
		} );
	}
	else
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize streaming manager" );
		return false;
	}

	// Initialize resource backend
	if( !InitializeResourceBackend() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize resource backend" );
		return false;
	}

	m_configBackend = red::CreateUniquePtr< Config::ConfigBackend >();

	m_statsBackend = red::CreateUniquePtr< tools::EngineStatsBackend >();

	m_profilerBackend = red::CreateUniquePtr< red::ProfilerBackend >();

	m_debuggerBackend = red::CreateUniquePtr< tools::DebuggerBackend >( *m_resourceBackend.Get(), m_networkConnection->GetChannelFactory() );

	m_debugFilterHelper = red::CreateUniquePtr< DebugFilterHelper >();

	m_qualityLevelHelper = red::CreateUniquePtr< QualityLevelHelper >();

	GRender->DisableCacheUsing();
	GRender->CreateGameViewport( "FakeViewport", 0, 0, VWM_Borderless, 0, 0 ); // needed to complete the renderer initialization

	RED_LOG( "Status: Initializing SoundSystem..." );
	if( !InitializeSoundSystem() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize sound system" );
		return false;
	}

	RED_LOG( "Status: Initializing LocalizationManager..." );
	if( !InitializeLocalizationManager() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize the localization Manager" );
		return false;
	}

	if( m_soundSystem )
	{
		m_soundSystem->FinalizeInit( GetLocalizationManager() );
	}

	if( m_inkSystem )
	{
		m_inkSystem->SetMode( ink::InkSystemMode::Backend );
		m_inkSystem->InitializeUISoundSystem();
	}

	if( !InitializeMaterialLibrary() )
	{
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize the materials library" );
		return false;
	}

	game::data::InitTweakDB();
	game::data::GetTweakDB().SetShowErrorDialog( !params.m_tweakDBSilentValidation );

	// PG 25.07.18
	// Load TweakDB. Needed to avoid loading it from custom property editors which can be called not from the main thread.

	if ( params.m_tweakDBForceRaw || params.m_tweakdbBlobPath.Empty() )
	{
		for ( const String& dlc : params.m_tweakDBdlcsTEMP )
		{
			game::data::GetTweakDB().EnableDLC( dlc );
		}

		game::data::GetTweakDB().LoadRawFiles();
	}
	else
	{
		game::data::GetTweakDB().Editor_LoadOptimized( params.m_tweakdbBlobPath );
	}

	// Initialise data error handler, this will resolve resources etc
	auto dataErrorHandler = [this]( const red::String& dataErrorAction ) {
		m_resourceBackend->OnDataError( dataErrorAction );
	};
	app::logging::SetDataErrorHandler( dataErrorHandler );

	AutoOpenEditors();

	// initialized
	// commented out for now, because it breaks debugging game, when asset browser is running and game is run from
	// script editor, script editor will connect to the backend of asset browser and not the game :/
	//IScriptingSystem::GetInstance().InitializeChannel( m_networkConnection->GetChannelFactory() );
	return true;
}

void CBackendEngine::OnRegularSystemInitialization( const EngineInitParams& params, const job::RunContext& context, red::Atomic< Uint32 >& keepRunning )
{
	job::Builder builder{ context };
	builder.DispatchJob( "CBackendEngine/Initialization/BaseInitialization", [this, params, &keepRunning]( const job::RunContext& context )
	{
		if ( !keepRunning.GetValue() )
		{
			return;
		}

		CBaseEngine::OnRegularSystemInitialization( params, context, keepRunning );
	} );

	builder.DispatchJob< job::Fence::None >( "CBackendEngine/Initialization/Interop", [this, params, &keepRunning]( const job::RunContext& context )
	{
		if ( !keepRunning.GetValue() )
		{
			return;
		}

		if( !InitializeInterop() )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize interop" );
			keepRunning.SetValue( 0 );
			return;
		}

		m_interopStartingPort = params.m_interopStartingPort;
		if( !InitializeInteropService( ) )
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize interop service" );
			keepRunning.SetValue( 0 );
		}
	} );

	if ( IsHoudiniEngineAvailable() )
	{
		builder.DispatchJob< job::Fence::None >( "CBackendEngine/Initialization/HoudiniEngine", [this, &keepRunning]( const job::RunContext& context )
		{
			if ( !keepRunning.GetValue() )
			{
				return;
			}

			InitializeHoudiniEngine();
			// clear status bar
			RED_LOG( "Status: " );
		} );
	}

	builder.DispatchFenceExplicitly();
}

Bool CBackendEngine::InitializeServices()
{
	CName servicesClassName = RED_NAME_CONSTEXPR( "servicesGameServicesWin" );
	RED_LOG_INFO( "Core: Game services class: '%hs'", servicesClassName.AsChar() );

	const auto* servicesClass = GetRttiSystem().FindClass( servicesClassName );
	if ( servicesClass )
	{
		m_services = red::UniquePtr< services::GameServicesGalaxy >( servicesClass->CreateObject< services::GameServicesGalaxy >() );
		if ( m_services->Initialize( red::AppType::Tools ) )
		{
			return true;
		}
	}

	RED_LOG_ERROR( "Core: Unable to create game services" );
	return false;
}

void CBackendEngine::ShutdownServices()
{
	if ( m_services )
	{
		m_services->Shutdown();
	}
}

void CBackendEngine::Shutdown()
{
	if( IsHoudiniEngineAvailable() && houdini_legacy::IsSessionValid( GetHoudiniSession() ) )
	{
		if( ShutdownHoudiniEngine() )
		{
			RED_LOG( "Houdinie Engine - Shutdown Success!" );
		}
		else
		{
			RED_LOG_ERROR( "Houdinie Engine - Shutdown FAILURE!" );
		}
	}

	if( m_resourceBackend )
	{
		m_resourceBackend->CloseAllEditors();
	}

	if( GGpuResourceCollector )
	{
		GGpuResourceCollector->Stop();
		RED_DELETE( GGpuResourceCollector );
		GGpuResourceCollector = nullptr;
	}

	m_debuggerBackend.Reset();
	m_resourceBackend.Reset();

	if( m_interopService )
		m_interopService->Shutdown();

	m_interopService.Reset();

	m_streamingManagerTimer.Stop();
	m_streamingManagerInstance.Save();
	m_streamingManagerInstance.Shutdown();

	ShutdownMaterialLibrary();

	game::data::DeinitTweakDB();

	// shutdown base engine
	CBaseEngine::Shutdown();
}

Bool CBackendEngine::IsBackend() const
{
	return true;
}

Bool CBackendEngine::ProcessFrame( const Float scaledEngineTimeDelta, const tick::Info& tickInfo, input::IInputContext& input )
{
	PC_SCOPE( BackendEngine_ProcessFrame );

	{
		PC_SCOPE( BackendEngine_Profiler );
		m_profilerBackend->Update();
	}

	{
		PC_SCOPE( BackendEngine_ProcessFrame_BaseEngine );
		CBaseEngine::ProcessFrame( scaledEngineTimeDelta, tickInfo, input );
	}
	
	{
		PC_SCOPE( BackendEngine_GameServices );
		m_services->Update();
	}

	{
		PC_SCOPE( BackendEngine_SoundSystems );
		GetSoundSystem()->UpdateFromBackend( scaledEngineTimeDelta, tickInfo );
	}

	{
		PC_SCOPE( BackendEngine_ProcessMessages );
		red::DynArray< ExternalMessage > q;
		m_inputSystem->GetMessages( q );
		m_resourceBackend->ProcessMessages( q );
		m_services->GetMessages( q );
	}

	// Update stats before applying new updates
	{
		PC_SCOPE( BackendEngine_StatsBackend );
		m_statsBackend->Update();
	}

	// process input
	{
		PC_SCOPE( BackendEngine_ProcessInput );
		m_resourceBackend->ProcessInput( input );
	}

	// update the resource backend rendering
	{
		// #tbd: does it really need to be in lock step?
		m_resourceBackend->UpdateAndRender( scaledEngineTimeDelta, tickInfo );
	}

	// process editor communication
	{
		PC_SCOPE( BackendEngine_ProcessCommunication );
		ProcessCommunication();
	}

	// Editor notifications
	{
		PC_SCOPE( SendPendingNotifications );
		tools::SerializableValueNotificationDispatcher::GetInstance().SendPendingNotifications();
	}

	// Shutdown engine if it's connected with editor and editor connection is lost
	{
		PC_SCOPE( BackendEngine_CheckEditorConnection );
		CheckEditorConnection();
	}

	// Backend timers
	{
		PC_SCOPE( BackendEngine_FireAllPendingTimers );
		tools::Timer::FireAllPendingTimers();
	}

	// debug filters
	{
		PC_SCOPE( BackedEngine_DebugFilters );

		red::DynArray< String > names{red::PoolFrame()};
		rend::DebugFilterMask::GetFilterMaskMapKeys( names );

		Bool debugFiltersChanged = false;

		for( Uint32 i = 0; i < names.Size(); ++i )
		{
			if( names[ i ] == rend::GetGameFilterName() )
			{
				continue;
			}

			const auto& filterMask = rend::DebugFilterMask::GetFilterMask( names[ i ] );
			auto& savedMask = m_savedDebugFilters[ names[ i ] ];

			if( filterMask != savedMask )
			{
				savedMask = filterMask;
				debugFiltersChanged = true;
			}
		}

		if( debugFiltersChanged )
		{
			m_debugFilterHelper->DebugFilterChangedNotify();
		}
	}

	return !RequestedExit();
}

Bool CBackendEngine::InitializeResourceBackend()
{
	GGpuResourceCollector = RED_NEW( tools::GpuResourceCollector );
	GGpuResourceCollector->Start();

	tools::ResourceBackendSetup setup;
	setup.m_depot = res::GetEditorDepot();
	setup.m_loader = GResourceLoader;
	setup.m_viewportManager = m_viewportManager.Get();
	setup.m_sinks.PushBack( m_inputSystem.Get() );

	m_resourceBackend = red::CreateUniquePtr< tools::ResourceBackend >( setup );

	return true;
}

bool CBackendEngine::LoadBackendGatheredResources()
{
	if ( !res::prv::LoadBackendGatheredResources() )
	{
		const auto& gatheredResourceErrors = res::prv::GetGatheredResourceErrors();
		ScriptErrorDialogData errorDialog;
		errorDialog.m_caption = "Errors loading Gathered Resources!";
		errorDialog.m_errors.PushBack( "Not all gathered resources could be loaded, please check the errors below and get any missing files before trying this operation again. The engine will now crash." );
		errorDialog.m_errors.PushBack( gatheredResourceErrors );
		ShowScriptErrorDialogBox( errorDialog );
		const auto combinedErrorString = red::StrJoin( gatheredResourceErrors, "\r\n" );
		RED_FATAL( "Not all Gathered Resources were loaded!\r\n%hs", combinedErrorString.AsChar() );
		return false;
	}
	return true;
}

Bool CBackendEngine::InitializeInput()
{
	CName class_name = RED_NAME_CONSTEXPR( "inputInputSystemEditor" );

	const auto* class_type = GetRttiSystem().FindClass( class_name );
	RED_FATAL_ASSERT( class_type, "Error: Invalid input system class: [%hs]", class_name.AsChar() );

	m_inputSystem = red::UniquePtr< input::IInputSystem >( class_type->CreateObject< input::IInputSystem >() );
	m_inputSystem->Initialize();
	return m_inputSystem != nullptr;
}

Bool CBackendEngine::InitializeLocalizationManager()
{
	if( GetLocalizationManager() == nullptr )
	{
		RED_LOG_ERROR( "Engine: No localization manager." );
		return false;
	}

	auto& locManager = *GetLocalizationManager();
	locManager.Init();

	auto commandLine = red::CommandLine::Get();
	String param;
	if ( commandLine.GetFirstOption( "language", param ) )
	{
		locManager.SetLanguageGlobal( locManager.ResolveClosestLanguage( param ) );
	}
	if ( commandLine.GetFirstOption( "voLanguage", param ) )
	{
		locManager.SetVoiceLanguage( locManager.ResolveClosestLanguage( param ) );
	}
	if ( commandLine.GetFirstOption( "subtitleLanguage", param ) )
	{
		locManager.SetSubtitleLanguage( locManager.ResolveClosestLanguage( param ) );
	}
	if ( commandLine.GetFirstOption( "textLanguage", param ) )
	{
		locManager.SetTextLanguage( locManager.ResolveClosestLanguage( param ) );
	}

	return true;
}

void CBackendEngine::InitializeDebugFilters( rend::RenderFilterPreset preset )
{
	CBaseEngine::InitializeCommonDefaultDebugFilters( rend::GetDefaultEditorFilters(), preset );
	InitializeDefaultEditorDebugFilters();
}

void CBackendEngine::InitializeDefaultEditorDebugFilters()
{
	auto& editorFilters = rend::GetDefaultEditorFilters();

	rend::DebugFilter::ToggleFilter( editorFilters, "Editor", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window/CameraAxes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window/Grid", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window/ActiveLayer", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window/CameraInfo", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Window/EditModeInfo", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Prefab", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Prefab/BoundingBoxVisualization", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/World", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/World/EditMode", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/World/BlockoutSectorsVisualization", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/World/StreamingSectorsVisualization", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/World/SplineEditing", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/SnapPoint", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Blockout", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Editor/Blockout/Minimap", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Components", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/AnimationErrors", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/ExternalInputsErrors", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/FiredEvents", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/DiscardedEvents", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/TrackErrors", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/BoneErrors", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Editor", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/EntityName", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Locomotion", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/PlayedAnimations", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Profiler", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/LOD", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Skeletons", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Skeletons/BonesAxes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Skeletons/BonesLines", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Animation/Skeletons/BonesNames", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaLightShape", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/ShadowAngleAreaLightShape", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Interiors", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Outline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/OutlineSeethrough", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Selectable", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Sprites", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/AreaShape", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/CollisionArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/CommunityArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/CrowdArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/CrowdNullArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/RestrictMovementArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/SectorSplitArea", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/AmbientArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/Entity", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/EnvArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/NoNotifier", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/Quest", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/LocationArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/InteriorArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/StaticTriggerAreaComponent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/StaticAreaShapeComponent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/WorldBoundary", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/CyberspaceBoundary", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/DynamicEvent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/KillTrigger", true);
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/EffectTrigger", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/VehicleForbiddenArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/PerformanceArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Types/TriggerArea/WaterNullArea", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/AreaShapes/Volume", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Interiors", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Outline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/OutlineSeethrough", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Selectable", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Types", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Types/GeometryShape", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Types/GIShape", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Types/LightChannel", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/GeometryShapes/Volume", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/BinkComponent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/BrokenPrefabProxyMeshes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Cables/SnapPoint", true);
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Cameras", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/CurvePaths", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Decals", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Decals/Sprites", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Decals/MaterialResource", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Decals/Boxes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Entity/EntityResource", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/FogVolumes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/FogVolumes/Sprite", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/FogVolumes/Box", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Foliage/MeshResource", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Game", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/LightBounds", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/LightVolumes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Markers", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Mesh/MeshResource", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Decals", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Effects", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Entities", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Meshes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Particles", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/MissingAssets/Prefabs", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/QuestMarkers", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/QuestMarkers/MainQuest", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/QuestMarkers/SideQuest", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/QuestMarkers/StreetStory", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Prefabs/AllDisabledVariants", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Decorations", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/LaneMarkings", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/MeshGenerationDescriptions", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/ParkingSpots", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/ParkingSpotCollisionBoxes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection/Traffic", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection/Sidewalks", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection/Active", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection/Custom", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Roads/Intersection/CutOffset", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/RotationVisualization", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/RotationVisualization/X", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/RotationVisualization/Y", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/RotationVisualization/Z", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Selectable", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/PatrolSpline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/SpeedSpline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/Spline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/CurvePath", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/TrafficSpline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/RoadSpline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Splines/Types/CableSpline", true);
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/EnvProbes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/GI", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/LightChannels", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/Lights", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/LightsLocalShadowsInfo", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/Particles", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Sprites/Effects", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/Stickers", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VirtualCamera", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent/Axes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent/BoundingBox", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent/Outline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent/Volume", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Components/VehicleChassisComponent/ShowPlayerCollider", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/BrokenNodes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Pivot", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Snap", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Snap/AllPoints", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Snap/PotentialPoints", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Snap/ActivePoints", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/SelectedName2d", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/TerrainNodesSelectionOutline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/DensityVisualization", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/BoundingBoxes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/SecondaryReferencePointInfo", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/ExcludedOnConsole", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Nodes/Conditional", false );	

	rend::DebugFilter::ToggleFilter( editorFilters, "Selection", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Selection/NodeDepth", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Selection/NodeName", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Selection/Bounds", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Proxy/SpawnInfo", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Proxy/ResourceProxyMeshOnly", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Proxy/NodeProxyMeshOnly", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/AsMarker", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Area", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Building", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Decoration", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Generated", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Quest", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Regular", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Road", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Prefabs/Type/Terrain", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Entity", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "AI", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "AI/Workspots/Spots", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "AI/Patrol", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "AI/Patrol/SplineControlPoints", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Population", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Population/Spawners", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Traffic", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Traffic/SlotsThatFinishedMovement", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Traffic/Lanes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Traffic/Splines", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Ink", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/Draw", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/HUD", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/Menu", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/WorldWidgets", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/InteractionPlanes", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Ink/GameUI", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "GameSystem/GPS/DrawInWorld", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "GameSystem/GPS/Debug", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Source", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Source/MeshNode", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Source/OccluderNode", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Source/InstancedOccluderNode", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Source/OccluderComponent", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Occluder", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Occluder/OnlyFlatMeshes", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Occluder/Outline", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Occluder/Volume", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Occlusion/Occluder/XRayMode", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "RuntimeSystem", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "RuntimeSystem/Entity", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "RuntimeSystem/Entity/Workspots", true );

	rend::DebugFilter::ToggleFilter( editorFilters, "Physics", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Physics/Collisions", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Physics/Collisions/NodeBounds", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Physics/Collisions/MissingCollisions", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Physics/DynamicActorRepellingComponent", false );

	rend::DebugFilter::ToggleFilter( editorFilters, "Audio", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/Triggers", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/Triggers/Sfx", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/Triggers/Music", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/Triggers/SoloSelected", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/Triggers/OuterShape", false );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/UseOcclusion", true );
	rend::DebugFilter::ToggleFilter( editorFilters, "Audio/UseDynamicReverb", true );
}

Bool CBackendEngine::IsHoudiniEngineAvailable()
{
	return houdini_legacy::IsHoudiniEngineAvailable();
}

void CBackendEngine::InitializeHoudiniEngine( red::String& msg )
{
	houdini_legacy::InitializeHoudiniEngine( msg, m_houdiniSession );
}

Bool CBackendEngine::StartHoudiniEngine( red::String& msg )
{
	return houdini_legacy::Start( GetHoudiniSession(), msg );
}

Bool CBackendEngine::ShutdownHoudiniEngine()
{
	return houdini_legacy::Close( GetHoudiniSession() );
}

void CBackendEngine::CheckEditorConnection()
{
	if( m_interopService->IsConnectionLost() && false == m_detachRequested)
	{
		RED_LOG( "Engine: Lost connection with editor, exiting engine application." );
		RequestExit( 0 );
	}
}

void CBackendEngine::RequestDetach()
{
	if (false == m_detachRequested)
	{
		m_detachRequested = true;
		RED_LOG_INFO("Detach requested - shutting down logger!");
		RED_LOG_FLUSH_AND_WAIT();


		//app::logging::Shutdown();
	}

	if (false)//m_interopService)
	{
		RED_LOG_DEBUG("%hs()", __FUNCTION__);
		RED_LOG_FLUSH_AND_WAIT();
		m_interopService->Shutdown();
		m_interopService.Reset();
		RED_LOG_DEBUG("after Interop shutdown...");
		RED_LOG_FLUSH_AND_WAIT();

		::Sleep(2 * 1000);

		InitializeInteropService();
		RED_LOG_DEBUG("after Interop initialize...");
		RED_LOG_FLUSH_AND_WAIT();
	}
}

void CBackendEngine::AfterAttach(const red::String& newCommandline)
{

}

void CBackendEngine::ProcessCommunication()
{
	m_interopService->ProcessMessages();
	m_debuggerBackend->ProcessCommunication();
}

red::SharedPtr< interop::utils::Object > CBackendEngine::GetInteropRoot() const
{
	return m_interopRoot;
}

void CBackendEngine::AutoOpenEditors()
{
	// Parse the command line locally
	const red::CommandLine& commandLineArguments = red::CommandLine::Get();

	// auto open resource editor
	{
		String resourceToLoad;
		String interopRootPath( "." );
		if( commandLineArguments.GetFirstOption( "load", resourceToLoad ) )
		{
			const auto path = res::ResourcePath::Build( resourceToLoad );
			if( m_resourceBackend->CreateResourceEditor( path, "EditorXXX" ) )
			{
				for( ;; )
				{
					tick::Info nullTickInfo;
					m_resourceBackend->UpdateAndRender( 0.1f, nullTickInfo );

					auto editor = m_resourceBackend->GetResourceEditorByName( "EditorXXX" );
					if( editor )
					{
						// create the default preview panel, this is to mimic the old behavior
						editor->CreatePreviewPanel( "generic", 0 );

						interopRootPath = ".depot.editors[\"EditorXXX\"].";
						break;
					}
				}
			}
		}

		// execute interop
		if( commandLineArguments.HasOption( "interop" ) )
		{
			auto commands = commandLineArguments.GetValues( "interop" );
			for( const auto& cmd : commands )
			{
				String toRun;
				toRun += interopRootPath;
				toRun += cmd;

				// Execute expression
				::interop::InteropResult result;
				if(::interop::Execute( m_interopRoot, toRun, result ) == false )
				{
					RED_LOG_ERROR( "BackendEngine: Interop running: exception while executing expression (%hs), because of: %hs",
								   toRun.AsChar(), result.m_data.AsChar() );
				}
			}
		}
	}
}
