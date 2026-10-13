/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once
#include "../../../common/engine/include/baseEngine.h"


namespace commandlet
{
	// Simple wrapper class for initialising the engine for commandlets that need it
	class BACKEND_ENGINE_API EngineInstance
	{
	public:
		EngineInstance();
		EngineInstance(EngineInstance&& other);
		~EngineInstance();

		bool Initialise();
		bool InitialiseWithSoundSystem();
		void Shutdown();

	private:
		EngineInstance(const EngineInstance&) = delete;
		EngineInstance& operator=(EngineInstance&) = delete;
		EngineInstance& operator=(EngineInstance&&) = delete;

		bool LoadBackendGatheredResources();

		red::UniquePtr< CBaseEngine > m_engine;
	};

} // namespace commandlet
