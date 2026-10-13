/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace red
{
	namespace MultiplayerSetup
	{
		// Initializes multiplayer setup
		RED_REFLECTION_API void Initialize();

		// Checks if multiplayer setup was initialized
		RED_REFLECTION_API Bool IsInitialized();

		// Checks if this app is a (multiplayer) server
		RED_REFLECTION_API Bool IsServer();
		// Checks if this app is a (multiplayer) client
		RED_REFLECTION_API Bool IsClient();
		// Checks if this app is a (multiplayer) client or server
		RED_REFLECTION_API Bool IsMultiplayer();

		// Checks if this app is running in "booth mode"
		RED_REFLECTION_API Bool IsBoothMode();
		// Checks if there is playtest specified
		RED_REFLECTION_API Bool IsPlaytest();
		// Gets current playtest name; returns empty string if there is no playtest set up
		RED_REFLECTION_API const String& GetPlaytestName();

#ifdef RED_PLATFORM_WINPC
		// Checks if the game is supposed to use "server_windows" data
		RED_REFLECTION_API Bool ShouldUseWindowsServerData();
#endif
	}
}