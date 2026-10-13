/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "singleTreeResourceDepot.h"

#include "../../../common/redFileSystem/include/fileSys.h"

namespace res
{
namespace prv
{

SingleTreeResourceDepot::SingleTreeResourceDepot( const red::AbsolutePath& rootPath )
	: m_rootPath( rootPath )
{
	RED_FATAL_ASSERT( m_rootPath.IsDirectoryPath(), "Must initialise this depot with a directory path" );
	RED_FATAL_ASSERT( GFileManager->FileExist( m_rootPath ), "Depot root path '%hs' must exist!", m_rootPath.AsChar() );
}

SingleTreeResourceDepot::~SingleTreeResourceDepot()
{
}

void SingleTreeResourceDepot::FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const
{
	red::AbsolutePath fullPath = m_rootPath.AddDirPath( searchDepotPath );
	FileSystemResourceDepot::FindDepotDirectories( fullPath, outDirectoryNames );
}

void SingleTreeResourceDepot::FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const
{
	red::AbsolutePath fullPath = m_rootPath.AddDirPath( searchDepotPath );
	FileSystemResourceDepot::FindDepotFiles( fullPath, pattern, outFileNames );
}

red::AbsolutePath SingleTreeResourceDepot::TranslateResourcePath( const res::ResourcePath resourcePath ) const
{
	red::AbsolutePath result;
	auto path = resourcePath.ToStringView();
	if ( !path.Empty()  )
	{
		result = m_rootPath.AddFilePath( path );
	}
	return result;
}

res::ResourcePath SingleTreeResourceDepot::TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const
{
	red::StringView absPathView = absolutePath.AsStringView();
	red::StringView basePathView = absPathView.SubView( 0, m_rootPath.Length() );
	if ( basePathView == m_rootPath.AsStringView() )
	{
		red::StringView leafPathView = absPathView.SubView( m_rootPath.Length() );
		return res::ResourcePath::Build( leafPathView );
	}
	return res::ResourcePath();
}

red::AbsolutePath SingleTreeResourceDepot::TranslateExistingResourcePath( const red::StringView& path ) const
{
	if ( !path.Empty()  )
	{
		red::AbsolutePath fullPath = m_rootPath.AddFilePath( path );
		if ( GFileManager->FileExist( fullPath ) )
		{
			return fullPath;
		}
	}
	return {};
}

red::AbsolutePath SingleTreeResourceDepot::TranslateWritableResourcePath( const red::StringView& path ) const
{
	red::AbsolutePath result;
	if ( !path.Empty()  )
	{
		result = m_rootPath.AddFilePath( path );
	}
	return result;
}

} // prv
} // res
