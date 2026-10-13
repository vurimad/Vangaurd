/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// True if we are closing
REDCORE_API Bool IsClosingMode();
REDCORE_API void SetClosingMode();

// True if we are running game engine
REDCORE_API Bool IsGameMode();
REDCORE_API void SetGameMode();

REDCORE_API Bool IsEditorMode();
REDCORE_API void SetEditorMode();

// Checks if this app is running in headless mode (no rendering, no audio etc.)
REDCORE_API Bool IsHeadlessMode();


