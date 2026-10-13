/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileSystemResourceDepot.h"
#include "serializationAsyncSourceImpl.h"
#include "resourceDepotUtils.h"
#include "directoryListingCache.h"

#include "../../redSystem/include/crc.h"
#include "../../redFileSystem/include/system.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/fileUtils.h"
#include "../../redReflection/include/serializationAsyncSource.h"
#include "../../redIO/include/redIOAsyncIO.h"
#include "../../redJobs2/include/jobRunner.h"

namespace res
{
namespace prv
{

//------------------------------------------------------------------------------

FileSystemResourceDepot::FileSystemResourceDepot()
	: m_debugName( "Default" )
	, m_dlcRootPaths( red::PoolEngine() )
{
}

FileSystemResourceDepot::~FileSystemResourceDepot()
{
	if ( m_directoryCache && !m_directoryCachePath.Empty() )
	{
		m_directoryCache->Save( m_directoryCachePath );
	}
}

void FileSystemResourceDepot::SetDebugName( const String& name )
{
	m_debugName = name;
}

const char* FileSystemResourceDepot::GetDebugName() const
{
	return m_debugName.AsChar();
}

void FileSystemResourceDepot::LoadDirectoryListingCache( const red::AbsolutePath& cachePath )
{
	m_directoryCachePath = cachePath;
	m_directoryCache = red::CreateUniquePtr< res::prv::DirectoryListingCache >();
	if ( !m_directoryCache->Load( m_directoryCachePath ) )
	{
		RED_LOG_WARNING( "ResourceDepot(%hs): Could not load directory listing cache '%hs'",  GetDebugName(), cachePath.AsChar() );
	}
}

//------------------------------------------------------------------------------

red::SharedPtr<serialization::IAsyncSource> FileSystemResourceDepot::CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params )
{
	auto absolutePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );

	serialization::AsyncSourcePtr result;
	io::TFileHandle fh = io::GAsyncIO.OpenFile( absolutePath.AsChar(), io::eAsyncFlag_TryCloseFileWhenNotUsed | io::eAsyncFlag_DeferredOpen );
	if ( fh != io::INVALID_FILE_HANDLE )
	{
		result = serialization::CreateFileAsyncSource( resourcePath, fh );
		io::GAsyncIO.ReleaseFile( fh ); // close additional reference, now the AsyncSource has the reference to the file
	}

	return result;
}

red::SharedPtr<IFile> FileSystemResourceDepot::CreateResourceWriter( const res::ResourcePath resourcePath )
{
	red::SharedPtr< IFile > result;

	auto absolutePath = TranslateWritableResourcePath( resourcePath.ToStringView() );
	if ( !absolutePath.Empty() )
	{
		result = GFileManager->CreateFileWriter( absolutePath, FOF_AbsolutePath );
	}

	return result;
}

red::SharedPtr<IFile> FileSystemResourceDepot::CreateResourceReader( const res::ResourcePath resourcePath, bool followLinks )
{
	red::SharedPtr< IFile > result;

	red::AbsolutePath absolutePath;
	if ( followLinks )
	{
		absolutePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );
	}
	else
	{
		absolutePath = TranslateWritableResourcePath( resourcePath.ToStringView() );
	}
	if ( !absolutePath.Empty() )
	{
		result = GFileManager->CreateFileReader( absolutePath, FOF_AbsolutePath );
	}

	return result;
}


Bool FileSystemResourceDepot::RemoveResource( const res::ResourcePath path )
{
	auto fullFilePath = TranslateResourcePath( path );
	if ( !fullFilePath.Empty() )
	{
		return GFileManager->DeleteFile( fullFilePath );
	}
	return false;
}

bool FileSystemResourceDepot::ResourceExists( const res::ResourcePath resourcePath ) const
{
	return !TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() ).Empty();
}

bool FileSystemResourceDepot::ResourceIsReadOnly( const res::ResourcePath resourcePath ) const
{
	auto fullFilePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );
	if ( !fullFilePath.Empty() )
	{
		return GFileManager->IsFileReadOnly( fullFilePath );
	}
	return false;
}

Uint64 FileSystemResourceDepot::GetResourceTimestamp( const res::ResourcePath resourcePath ) const
{
	// ctremblay: Optimization here: First try to get time stamp. If fail, check for link file, etc...  
	Uint64 timestamp = 0ull;
	auto fullFilePath = TranslateResourcePath( resourcePath );
	if( !fullFilePath.Empty() ) 
	{
		timestamp = GFileManager->GetFileTimeStamp( fullFilePath );
		if( timestamp == 0ull )
		{
			fullFilePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );
			if( !fullFilePath.Empty() ) 
			{
				timestamp = GFileManager->GetFileTimeStamp( fullFilePath );
			}
		}
	}

	return timestamp;
}

res::ResourcePath FileSystemResourceDepot::ResolveResourcePath( const res::ResourcePath resourcePath ) const
{
	auto absolutePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );
	if ( !absolutePath.Empty() )
	{
		return TranslateAbsolutePath( absolutePath );
	}
	return {};
}

bool FileSystemResourceDepot::ResourceIsLink( const res::ResourcePath resourcePath ) const
{
	red::AbsolutePath fullPath = TranslateResourcePath( resourcePath );

	red::StringView pathExt = red::paths::GetExtension( resourcePath.ToStringView() );
	if ( pathExt == "link" )
	{
		return GFileManager->FileExist( fullPath );
	}

	if ( !GFileManager->FileExist( fullPath ) )
	{
		fullPath.AppendFilePath( ".link" );
		return GFileManager->FileExist( fullPath );
	}

	return false;
}

//------------------------------------------------------------------------------

red::AbsolutePath FileSystemResourceDepot::TranslateExistingResourcePath_FollowLinks( const red::StringView& path ) const
{
	red::AbsolutePath result = TranslateExistingResourcePath( path );

	// If we didn't find an existing file then look for a .link file and follow that
	if ( result.Empty() )
	{
		String resourcePath = path.ToString();
		Int32 linksRemaining = MAX_LINK_FILE_RECURSION;
		do
		{
			String linkPath = resourcePath + ".link";
			red::AbsolutePath absoluteLinkPath = TranslateExistingResourcePath( linkPath );
			if ( absoluteLinkPath.Empty() || 
				!red::LoadFileToString( absoluteLinkPath, resourcePath ) )
			{
				break;
			}

#ifdef RESOURCEDEPOT_LOG_LINKS
			RED_LOG_TRACE( "ResourceDepot(%hs): Finding '%hs' with link '%hs' to '%hs'", GetDebugName(), path.ToString().AsChar(), linkPath.AsChar(), resourcePath.AsChar() );
#endif
			result = TranslateExistingResourcePath( resourcePath );
		}
		while ( result.Empty() && --linksRemaining > 0 );

#ifdef RESOURCEDEPOT_LOG_LINKS
		if ( !result.Empty() )
		{
			RED_LOG_INFO( "ResourceDepot(%hs): Finding '%hs' through links to '%hs'", GetDebugName(), path.ToString().AsChar(), resourcePath.AsChar() );
		}
		else
		{
			RED_LOG_ERROR( "ResourceDepot(%hs): Could not find resource '%hs'", GetDebugName(), path.ToString().AsChar() );
		}
#endif
	}

	return result;
}

//------------------------------------------------------------------------------

Uint32 FileSystemResourceDepot::Debug_GetResourceTotalSizeOnDisk( const res::ResourcePath resourcePath ) const
{
	auto absolutePath = TranslateExistingResourcePath_FollowLinks( resourcePath.ToStringView() );
	if ( !absolutePath.Empty() )
	{
		return static_cast<Uint32>( GFileManager->GetFileSize( absolutePath ) );
	}
	return 0;
}

Uint32 FileSystemResourceDepot::Debug_GetResourceObjectSizeOnDisk(const res::ResourcePath resourcePath) const
{
	return 0;
}

namespace
{

void FindDepotItems( const red::AbsolutePath& searchPathPattern, red::DynArray< String >& output, const bool findDirectories )
{
	for ( CSystemFindFile it( searchPathPattern.AsChar() ); it; ++it )
	{
		// Skip meta directory entries
		if ( (red::Strcmp( it.GetFileName(), "." ) == 0) ||
			(red::Strcmp( it.GetFileName(), ".." ) == 0) )
		{
			continue;
		}

		// Process only desired items
		if ( it.IsDirectory() == findDirectories )
		{
			output.PushBack( it.GetFileName() );
		}
	}
}

}

void FileSystemResourceDepot::FindDepotDirectories( const red::AbsolutePath& searchPath, red::DynArray< String >& output ) const
{
	if ( m_directoryCache )
	{
		m_directoryCache->LookupDirectories( searchPath, output );
	}
	else
	{
		red::AbsolutePath searchPatternPath = searchPath.AddFilePath( "*." );
		FindDepotItems( searchPatternPath, output, true );
	}
}

void FileSystemResourceDepot::FindDepotFiles( const red::AbsolutePath& searchPath, const char* pattern, red::DynArray< String >& output ) const
{
	if ( m_directoryCache )
	{
		m_directoryCache->LookupFiles( searchPath, pattern, output );
	}
	else
	{
		red::AbsolutePath searchPatternPath = searchPath;
		if ( pattern )
		{
			searchPatternPath.AppendFilePath( pattern );
		}
		else
		{
			searchPatternPath.AppendFilePath( "*.*" );
		}
		FindDepotItems( searchPatternPath, output, false );
	}
}

//------------------------------------------------------------------------------

const red::DynArray< red::String >& FileSystemResourceDepot::GetDLCRootPaths() const
{
	return m_dlcRootPaths;
}

void FileSystemResourceDepot::SetDLCRootPaths( const red::DynArray< String >& rootPaths )
{
	m_dlcRootPaths.Reserve( rootPaths.Size() );
	for ( const auto& path : m_dlcRootPaths )
	{
		if ( !TranslateExistingResourcePath( path ).Empty() )
		{
			m_dlcRootPaths.PushBack( path );
		}
	}
}

void FileSystemResourceDepot::ScanForDLCRootPaths()
{
	m_dlcRootPaths.Clear();

	// Find the top level ep directories
	red::DynArray< red::String > topLevelPaths{ red::PoolEngine() };
	FindDepotSubDirectories( "", topLevelPaths );
	red::alg::EraseIf( topLevelPaths, []( const red::String& str ) -> bool
	{
		return !str.BeginsWith( "ep" );
	} );

	// Find all dlc subdirectories
	red::DynArray< red::String > dlcPaths{ red::PoolEngine() };
	FindDepotSubDirectories( "dlc\\", dlcPaths );

	// Assign them to our entries
	m_dlcRootPaths.Reserve( topLevelPaths.Size() + dlcPaths.Size() );

	for ( const auto& path : topLevelPaths )
	{
		m_dlcRootPaths.PushBack( path );
	}

	for ( const auto& path : dlcPaths )
	{
		m_dlcRootPaths.PushBack( "dlc\\" + path );
	}
}

//------------------------------------------------------------------------------

} // prv
} // res
