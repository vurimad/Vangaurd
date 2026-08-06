/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "scriptUtils.h"

#ifndef RED_CONFIGURATION_FINAL

#include "scriptDataFormat.h"
#include "scriptDataLoaderPrivate.h"
#include "../../redContainers/include/dynArray.h"
#include "../../redFileSystem/include/fileSys.h"

red::DynArray< res::ResourcePath > GetResourcePathsFromScriptsBlob( const red::AbsolutePath& scriptBlobPath )
{
	auto file = GFileManager->CreateFileReader( scriptBlobPath, FOF_AbsolutePath );
	if ( !file )
	{
		RED_LOG_ERROR( "DependencyUtils: Could not load script blob from %hs", scriptBlobPath.AsChar() );
		return { red::PoolBackend() };
}

	CScriptDataFormat data;
	CScriptDataLoader scriptLoader( data );
	if ( !scriptLoader.Load( *file ) )
	{
		RED_LOG_ERROR( "DependencyUtils: Error reading script blob %hs", scriptBlobPath.AsChar() );
		return { red::PoolBackend() };
	}

	return scriptLoader.GetResourcePaths();
}

#else
RED_NO_EMPTY_FILE();
#endif