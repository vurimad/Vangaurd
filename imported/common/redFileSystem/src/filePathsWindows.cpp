/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "filePaths.h"
#include "../../redContainers/include/string/stringUtils.h"

//------------------------------------------------------------------------------

namespace red
{
namespace paths
{
namespace prv
{

static const char g_companyName[] = "CD Projekt Red";
static const char g_gameName[] = "Cyberpunk 2077";

const char* GetCompanyName()
{
	return g_companyName;
}

const char* GetGameName()
{
	return g_gameName;
}

} // prv
} // paths
} // red

//------------------------------------------------------------------------------

#ifdef RED_PLATFORM_WINPC

#include <shlobj.h>
#include <winioctl.h>

#pragma comment (lib, "Winmm.lib")

namespace red
{
namespace paths
{

namespace helper
{

static red::AbsolutePath GetExecutableDirectory()
{
	UniChar moduleFileName[ c_maxPath ];
	UniChar moduleFullPath[ c_maxPath ];
	::GetModuleFileNameW( nullptr, moduleFileName, c_maxPath );
	::GetFullPathNameW( moduleFileName, c_maxPath, moduleFullPath, nullptr);

	char buffer[ c_maxPath ];
	RED_VERIFY( red::FileSystemStringToEngineString( moduleFullPath, buffer, c_maxPath ) );

	const red::StringView modulePath( &buffer[0] );
	const red::StringView resultPath = red::paths::GetParentPath( modulePath );
	return red::AbsolutePath::CreateDirPath( resultPath );
}

static red::AbsolutePath GetBaseDirectory()
{
	UniChar moduleFileName[ c_maxPath ];
	UniChar moduleFullPath[ c_maxPath ];
	::GetModuleFileNameW( nullptr, moduleFileName, c_maxPath );
	::GetFullPathNameW( moduleFileName, c_maxPath, moduleFullPath, nullptr);

	char buffer[ c_maxPath ];
	RED_VERIFY( red::FileSystemStringToEngineString( moduleFullPath, buffer, c_maxPath ) );
	const red::StringView modulePath( &buffer[0] );

	// modulePath is "%root%\\bin\\%platform%-%config%\\file.exe"
	// so here strip off the file name first
	red::StringView resultPath = red::paths::GetParentPath( modulePath );

	{
		const auto splitPath = red::paths::SplitPath( resultPath );
		if ( splitPath.Size() > 2 && splitPath[ splitPath.Size() - 2 ] == "bin" )
		{
			// Strip two directories
			resultPath = red::paths::GetParentPath( red::paths::GetParentPath( resultPath ) );
		}
	}

	return red::AbsolutePath::CreateDirPath( resultPath );
}

// Get the User Profile directory for the game
static red::AbsolutePath GetUserProfileDirectory()
{
	red::AbsolutePath result;

	PWSTR pathStr = nullptr;
	HRESULT pathResult = ::SHGetKnownFolderPath( FOLDERID_Profile, 0, nullptr, &pathStr );
	RED_FATAL_ASSERT( pathResult == S_OK, "No Profile folder" );
	if ( pathResult == S_OK )
	{
		result = red::AbsolutePath::CreateDirPath( Utf16String( pathStr ) );
	}
	::CoTaskMemFree( pathStr );

	return result;
}

static red::AbsolutePath GetUserDocumentsDirectory()
{
	red::AbsolutePath result;

	PWSTR pathStr = nullptr;
	HRESULT pathResult = ::SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &pathStr );
	RED_FATAL_ASSERT( pathResult == S_OK, "No My Documents folder" );
	if ( pathResult == S_OK )
	{
		result = red::AbsolutePath::CreateDirPath( Utf16String( pathStr ) );
	}
	::CoTaskMemFree( pathStr );

	return result;
}

static red::AbsolutePath GetUserPicturesDirectory()
{
	red::AbsolutePath result;

	PWSTR pathStr = nullptr;
	HRESULT pathResult = ::SHGetKnownFolderPath( FOLDERID_Pictures, 0, nullptr, &pathStr );
	RED_FATAL_ASSERT( pathResult == S_OK, "No My Pictures folder" );
	if( pathResult == S_OK )
	{
		result = red::AbsolutePath::CreateDirPath( Utf16String( pathStr ) );
	}
	::CoTaskMemFree( pathStr );

	return result;
}

// Get the User Local AppData directory for the game
static red::AbsolutePath GetLocalAppDataDirectory()
{
	red::AbsolutePath result;

	PWSTR pathStr = nullptr;
	HRESULT pathResult = ::SHGetKnownFolderPath( FOLDERID_LocalAppData, 0, nullptr, &pathStr );
	RED_FATAL_ASSERT( pathResult == S_OK, "No User Local AppData folder" );
	if ( pathResult == S_OK )
	{
		result = red::AbsolutePath::CreateDirPath( Utf16String( pathStr ) );
	}
	::CoTaskMemFree( pathStr );

	return result;
}

// Get the User Remote AppData directory for the game
static red::AbsolutePath GetRoamingAppDataDirectory()
{
	red::AbsolutePath result;

	PWSTR pathStr = nullptr;
	HRESULT pathResult = ::SHGetKnownFolderPath( FOLDERID_RoamingAppData, 0, nullptr, &pathStr );
	RED_FATAL_ASSERT( pathResult == S_OK, "No User Roaming AppData folder" );
	if ( pathResult == S_OK )
	{
		result = red::AbsolutePath::CreateDirPath( Utf16String( pathStr ) );
	}
	::CoTaskMemFree( pathStr );

	return result;
}

static red::AbsolutePath GetTempDirectory()
{
	wchar_t path[ MAX_PATH ];
	GetTempPathW( MAX_PATH, path );
	return red::AbsolutePath::CreateDirPath( Utf16String( path ) );
}

} // helper

const red::AbsolutePath& GetExecutableDirectory()
{
	static const auto path = helper::GetExecutableDirectory();
	return path;
}

const red::AbsolutePath& GetRootDirectory()
{
	static const auto path = helper::GetBaseDirectory();
	return path;
}

const red::AbsolutePath& GetLocalAppDataDirectory()
{
	static const auto path = helper::GetLocalAppDataDirectory().AppendDirPath( prv::GetCompanyName() ).AppendDirPath( prv::GetGameName() );
	return path;
}

const red::AbsolutePath& GetUserDirectory()
{
	static const auto path = helper::GetUserDocumentsDirectory().AppendDirPath( prv::GetCompanyName() ).AppendDirPath( prv::GetGameName() );
	return path;
}

const red::AbsolutePath& GetUserCacheDirectory()
{
	static const auto path = helper::GetLocalAppDataDirectory().AppendDirPath( prv::GetCompanyName() ).AppendDirPath( prv::GetGameName() ).AppendDirPath( "cache" );
	return path;
}

const red::AbsolutePath& GetUserEditorDirectory()
{
#if defined( NO_EDITOR ) || defined( RED_CONFIGURATION_FINAL )
	RED_FATAL( "Not Implemented!" );
	static const red::AbsolutePath path;
#else
	static const auto path = helper::GetRoamingAppDataDirectory().AppendDirPath( prv::GetCompanyName() ).AppendDirPath( "Red Engine" );
#endif
	return path;
}

const red::AbsolutePath& GetSaveDirectory()
{
	static const auto path = helper::GetUserProfileDirectory().AppendDirPath( "Saved Games" ).AppendDirPath( prv::GetCompanyName() ).AppendDirPath( prv::GetGameName() );
	return path;
}

const red::AbsolutePath& GetScreenshotDirectory()
{
	static const auto path = helper::GetUserPicturesDirectory().AppendDirPath( prv::GetGameName() );
	return path;
}

const red::AbsolutePath& GetTempDirectory()
{
	static const auto path = helper::GetTempDirectory();
	return path;
}

} // paths
} // red

#endif // RED_PLATFORM_WINPC
