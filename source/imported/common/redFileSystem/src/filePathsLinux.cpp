/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "filePaths.h"

#ifdef RED_PLATFORM_LINUX

#include <sys/types.h>
#include <pwd.h>

namespace red
{
namespace paths
{

namespace helper
{

static red::AbsolutePath GetUserDirectory()
{
	// Taken from https://stackoverflow.com/questions/2910377/get-home-directory-in-linux
	// Note: A best guess because I cannot compile or test this
	return red::AbsolutePath::CreateDirPath( getenv( "HOME" ) );
}

} // helper


const red::AbsolutePath& GetExecutableDirectory()
{
	// TODO: Fix this
	static const auto path = red::AbsolutePath::CreateDirPath( "/" );
	return path;
}

const red::AbsolutePath& GetRootDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/" );
	return path;
}

const red::AbsolutePath& GetUserDirectory()
{
	static const auto path = helper::GetUserDirectory().AppendDirPath( prv::GetCompanyName() ).AppendDirPath( prv::GetGameName() );
	return path;
}

const red::AbsolutePath& GetUserCacheDirectory()
{
	static const auto path = GetUserDirectory().AddDirPath( "cache" );
	return path;
}

const red::AbsolutePath& GetSaveDirectory()
{
	static const auto path = GetUserDirectory().AddDirPath( "saves" );
	return path;
}

const red::AbsolutePath& GetUserEditorDirectory()
{
	static const auto path = GetUserDirectory().AddDirPath( "editor" );
	return path;
}

const red::AbsolutePath& GetScreenshotDirectory()
{
	static const auto path = GetUserDirectory().AddDirPath( "screenshots" );
	return path;
}

const red::AbsolutePath& GetTempDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/tmp/" );
	return path;
}

} // paths
} // red

#endif // RED_PLATFORM_LINUX
