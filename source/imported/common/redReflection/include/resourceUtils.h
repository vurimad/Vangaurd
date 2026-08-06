/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once
#include "handle.h"
#include "resourceLoader.h"
#include "resourceToken.h"

/// LEGACY API

namespace red { class AbsolutePath; }

#ifdef LoadResource
	#undef LoadResource
#endif

// Load resource
template< class T >
THandle< T > LoadResource_DEPRECATED( const red::String& depotFileName )
{
	auto path = res::ResourcePath::Build( depotFileName );
	auto token = GResourceLoader->IssueLoadingRequest( path );
	return Cast< T >( token->WaitUntilLoaded() );
}

namespace res
{
	extern RED_REFLECTION_API red::AbsolutePath GetThumbnailPath( const ResourcePath& resourcePath, CName appearanceName );
	extern RED_REFLECTION_API red::AbsolutePath GetBackupPath( const ResourcePath& resourcePath );
	extern RED_REFLECTION_API red::AbsolutePath GetUserDefinedFilterFilePath( const String& filename );
	extern RED_REFLECTION_API red::AbsolutePath GetPreDefinedFilterFilePath( const String& filename );
	extern RED_REFLECTION_API red::AbsolutePath GetDebugFilterDescriptionsFilePath( const String& filename );
}

