/**
* Copyright (c) 2014-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// declarations
	class DebugServerInternalPlugin : public red::DebugServerPlugin
	{
		// common
		virtual Bool Init() override final;
		virtual Bool ShutDown() override final;

		// life-time
		virtual void GameStarted() override final;
		virtual void GameStopped() override final;
		virtual void Update() override final;
	};
}