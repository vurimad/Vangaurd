/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redCore/include/absolutePath.h"

namespace red
{
namespace paths
{

// Platform specific executable directory
RED_FILESYSTEM_API const red::AbsolutePath& GetExecutableDirectory();

// Platform specific root directory
// Generally this is a directory two levels up from the executable directory, usually in the form of: "%root%\bin\%platform-config%\.exe"
// During development this is more or less the Perforce workspace directory
// For production this will be the directory where the game is installed to
// On consoles this will be different and should not be used directly
RED_FILESYSTEM_API const red::AbsolutePath& GetRootDirectory();

// Platform specific user directory
// On Windows this is the user profile directory with subdirectories named for the company name and game name.
// So this would end up being "%userprofile%\Documents\CD Projekt Red\Cyberpunk 2077\", unless the user has mounted their Documents folder elsewhere
// On consoles this points to a temporary location as user data needs to be stored using specific APIs
RED_FILESYSTEM_API const red::AbsolutePath& GetUserDirectory();

// Platform specific user cache directory
// Should be used for when storing caches for local machine configurations, ones which need to be written during the game runtime
// For other caches that are build during compile/build time, they should use GFileManager->GetCacheDirectory()
// On Windows this will be the user's local appdata directory with the company name and game name as subdirectories
// This would end up being "%localappdata%\CD Projekt Red\Cyberpunk 2077\cache\"
// On consoles this doesn't make sense and will fatally assert
RED_FILESYSTEM_API const red::AbsolutePath& GetUserCacheDirectory();

#ifdef RED_PLATFORM_WINPC
RED_FILESYSTEM_API const red::AbsolutePath& GetLocalAppDataDirectory();
#endif

// Platform specific user editor directory for storing configurations and such
// Should be used to store editor specific content as it points to the user's roaming appdata directory where all current editor configurations reside
// This expands to "%appdata%\CD Projekt Red\Red Engine"
// On consoles this will expand to the host directory whenever possible
RED_FILESYSTEM_API const red::AbsolutePath& GetUserEditorDirectory();

// Called to get the directory saves are stored in
// For Windows this will be stored in the "Saved Games" directory in the user's profile with the company and game names as subdirectories
// This expands to "%userprofile%\Saved Games\CD Projekt Red\Cyberpunk 2077\" unless the user has mounted this directory elsewhere
// On consoles this will return a temp directory with "sav" appended, this should not be used as console specific APIs need to be used instead
RED_FILESYSTEM_API const red::AbsolutePath& GetSaveDirectory();

// Platform specific user screenshot directory
// For Windows this will be "Pictures" directory located in the user's profile with game name as subdirectory
// This expands to "%userprofile%\Pictures\Cyberpunk 2077"
// On consoles this doesn't make sense and will fatally assert
RED_FILESYSTEM_API const red::AbsolutePath& GetScreenshotDirectory();

// Get temporary data directory
// This works on all platforms to get a temporary directory for files, in general it should not be used in final game runtime
RED_FILESYSTEM_API const red::AbsolutePath& GetTempDirectory();

namespace prv
{
const char* GetCompanyName();
const char* GetGameName();
} // prv

} // paths
} // red
