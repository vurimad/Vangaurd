/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "filePaths.h"

#ifdef RED_PLATFORM_ORBIS

namespace red
{
namespace paths
{

const red::AbsolutePath& GetExecutableDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/app0/" );
	return path;
}

const red::AbsolutePath& GetRootDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/app0/" );
	return path;
}

const red::AbsolutePath& GetSaveDirectory()
{
	static const auto path = GetRootDirectory().AddDirPath( "sav" );
	return path;
}

const red::AbsolutePath& GetUserDirectory()
{
	// NOTE: This isn't actually what we want here, we should assert, but some rendering stuff needs a valid path here
	return GetTempDirectory();
}

const red::AbsolutePath& GetUserCacheDirectory()
{
	static const red::AbsolutePath dummyPath;
	RED_FATAL( "Not Implemented!" );
	return dummyPath;
}

const red::AbsolutePath& GetUserEditorDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/host/%APPDATA%" ).AppendDirPath( prv::GetCompanyName() ).AppendDirPath( "Red Engine" );
	return path;
}

const red::AbsolutePath& GetScreenshotDirectory()
{
	static const red::AbsolutePath dummyPath;
	RED_FATAL( "Not Implemented!" );
	return dummyPath;
}

const red::AbsolutePath& GetTempDirectory()
{
	static const auto path = red::AbsolutePath::CreateDirPath( "/data/temp/" );
	return path;
}

} // paths
} // red

#endif // RED_PLATFORM_ORBIS