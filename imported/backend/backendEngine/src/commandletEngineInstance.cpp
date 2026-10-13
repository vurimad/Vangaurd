/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "commandletEngineInstance.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/engine/include/baseEngine.h"
#include "../../../common/sound/include/soundSystem.h"
#include "../../common/engine/include/baseEngineInitParams.h"


namespace commandlet
{
	EngineInstance::EngineInstance()
	{
	}

	EngineInstance::~EngineInstance()
	{
		if ( m_engine )
		{
			RED_FATAL_ASSERT( GEngine == nullptr, "Missing call to Shutdown()" );
		}
	}

	EngineInstance::EngineInstance( EngineInstance&& other )
		: m_engine( std::move( other.m_engine ) )
	{
	}

	bool EngineInstance::Initialise()
	{
		if ( !GEngine )
		{
			EngineInitParams params;
			params.m_scriptsSilentCompilation = true;
			params.m_scriptsSilentValidation = true;
			params.m_scriptVersion = script::ScriptVersion_Debug;
			params.m_gatheredResourceSilentValidation = true;
			params.m_audioConfig = RED_NAME_CONSTEXPR( "Commandlet" );
			params.m_cmdlet = true;

			m_engine = red::CreateUniquePtr< CBaseEngine >();
			auto counter = m_engine->InitializeAsync( params );
			job::FlushCounter( counter );

			if ( m_engine->GetState() != BES_Initialized )
			{
				RED_LOG_ERROR( "Commandlet: Unable to initialize engine" );
				return false;
			}
			
			GEngine = m_engine.Get();

			// NOTE: Need to load the backend resources here as well
			RED_VERIFY( res::prv::LoadBackendGatheredResources() );
		}

		return true;
	}

	bool EngineInstance::InitialiseWithSoundSystem()
	{
		if ( !GEngine )
		{
			EngineInitParams params;
			params.m_scriptsSilentCompilation = true;
			params.m_scriptsSilentValidation = true;
			params.m_scriptVersion = script::ScriptVersion_Debug;
			params.m_gatheredResourceSilentValidation = true;
			params.m_audioConfig = RED_NAME_CONSTEXPR( "Commandlet" );
			params.m_cmdlet = true;

			SoundSystemConfig audioInitConfig = SoundSystemConfig::EditorConfig();
			if ( params.m_audioConfig )
			{
				audioInitConfig = SoundSystemConfig::GetConfig( params.m_audioConfig );
			}
			audio::SoundSystem::PreInit( audioInitConfig );

			m_engine = red::CreateUniquePtr< CBaseEngine >();
			auto counter = m_engine->InitializeAsync( params );
			job::FlushCounter( counter );

			if ( m_engine->GetState() != BES_Initialized )
			{
				RED_LOG_ERROR( "Commandlet: Unable to initialize engine" );
				return false;
			}

			GEngine = m_engine.Get();

			if ( !m_engine->InitializeSoundSystem() )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Engine, "Engine: Failed to initialize sound system" );
				return false;
			}

			m_engine->FinalizeSoundSystemInit();

			// NOTE: Need to load the backend resources here as well
			RED_VERIFY( res::prv::LoadBackendGatheredResources() );
		}

		return true;
	}

	void EngineInstance::Shutdown()
	{
		if ( m_engine )
		{
			if ( m_engine.Get() == GEngine )
			{
				GEngine = nullptr;
			}

			m_engine->Shutdown();
			m_engine.Reset();
		}
	}

} // namespace commandlet
