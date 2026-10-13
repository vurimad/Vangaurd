/*
 * Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceEditorDepotImpl.h"
#include "resourceDepotUtils.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redSystem/include/stopWatch.h"

namespace res
{

ResourceEditorDepot::ResourceEditorDepot( const DepotInitInfo& initInfo )
	: m_mappings( red::PoolEngine() )
{
	InitializeResourceDepotMappings( initInfo.m_engineDepotRoot, initInfo.m_gameDepotRoot, initInfo.m_customDepotMapping, m_mappings );
}

ResourceEditorDepot::ResourceEditorDepot( const red::DynArray< prv::DepotMapping >& mappings )
	: m_mappings( mappings )
{
}

ResourceEditorDepot::~ResourceEditorDepot()
{
}

void ResourceEditorDepot::FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& outDirectoryNames ) const
{
	red::StopWatch timer;

	if ( searchDepotPath.Empty() )
	{
		for ( const auto& mapping : m_mappings )
		{
			red::StringView relativePath( mapping.mountPath );
			red::StringView pathSegment = relativePath.SubView( 0, relativePath.Find( '\\' ) );
			outDirectoryNames.PushBack( pathSegment.ToString() );
		}
	}
	else
	{
		for ( const auto& mapping : m_mappings )
		{
			if ( searchDepotPath.BeginsWith( mapping.mountPath ) )
			{
				// Trim off the front of the mount path from the depot path
				red::StringView relativePath( searchDepotPath );
				relativePath.TrimFront( mapping.mountPath.Length() );

				// Construct the full disk path for this mount point
				red::AbsolutePath fullPath = mapping.diskPath.AddDirPath( relativePath );

				FileSystemResourceDepot::FindDepotDirectories( fullPath, outDirectoryNames );
			}
			else if ( mapping.mountPath.BeginsWith( searchDepotPath ) )
			{
				// Special case for directories if the searched for path is a sub path of a mount point
				red::StringView relativePath( mapping.mountPath );
				relativePath.TrimFront( searchDepotPath.Length() );
				red::StringView pathSegment = relativePath.SubView( 0, relativePath.Find( '\\' ) );
				outDirectoryNames.PushBack( pathSegment.ToString() );
			}
		}
	}


	red::alg::RemoveDuplicates( outDirectoryNames );

	RED_LOG_TRACE( "Finding directories in Editor Depot. Path: '%hs' -> %u Results, Took: %lf ms", searchDepotPath.AsChar(), outDirectoryNames.Size(), timer.GetDeltaMS() );
}

void ResourceEditorDepot::FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& outFileNames ) const
{
	red::StopWatch timer;

	if ( !searchDepotPath.Empty() )
	{
		for ( const auto& mapping : m_mappings )
		{
			if ( searchDepotPath.BeginsWith( mapping.mountPath ) )
			{
				// Trim off the front of the mount path from the depot path
				red::StringView relativePath( searchDepotPath );
				relativePath.TrimFront( mapping.mountPath.Length() );

				// Construct the full disk path for this mount point
				red::AbsolutePath fullPath = mapping.diskPath.AddDirPath( relativePath );

				FileSystemResourceDepot::FindDepotFiles( fullPath, pattern, outFileNames );
			}
		}
	}

	RED_LOG_TRACE( "Finding files in Editor Depot. Path: '%hs' Pattern: '%hs' -> %u Results, Took: %lf ms", searchDepotPath.AsChar(), pattern, outFileNames.Size(), timer.GetDeltaMS() );
}

red::AbsolutePath ResourceEditorDepot::ComputeAbsolutePath( const red::StringView& path, const prv::DepotMapping& mapping ) const
{
	// Trim off the front of the mount path from the depot path
	red::StringView relativePath( path );
	relativePath.TrimFront( mapping.mountPath.Length() );

	// Construct the full disk path for this mount point
	return red::paths::IsDirectoryPath( path ) ? mapping.diskPath.AddDirPath( relativePath ) : mapping.diskPath.AddFilePath( relativePath );
}

res::ResourcePath ResourceEditorDepot::ComputeResourcePath( const red::StringView& path, const prv::DepotMapping& mapping ) const
{
	red::StringView absolutePath( path );
	absolutePath.TrimFront( mapping.diskPath.Length() );

	// Construct the full mounted resource path for this mount point
	String relativePath = mapping.mountPath;
	relativePath.Append( absolutePath.Data(), absolutePath.Length() );
	return res::ResourcePath::Build( relativePath );
}

red::AbsolutePath ResourceEditorDepot::TranslateResourcePath( const ResourcePath resourcePath ) const
{
	red::AbsolutePath result;

	// NOTE: This just finds the last entry that matches the path
	// it needs more work to find exactly what we want
	const red::StringView path = resourcePath.ToStringView();
	for ( const auto& mapping : m_mappings.Reverse() )
	{
		if ( path.StartsWith( mapping.mountPath ) )
		{
			result = ComputeAbsolutePath( path, mapping );
			break;
		}
	}

	return result;
}

red::AbsolutePath ResourceEditorDepot::ResolveWritableResourcePath( const ResourcePath resourcePath ) const
{
	return TranslateWritableResourcePath( resourcePath.ToStringView() );
}

ResourcePath ResourceEditorDepot::TranslateAbsolutePath( const red::AbsolutePath& absolutePath ) const
{
	res::ResourcePath result;

	const red::StringView path = absolutePath.AsStringView();
	for ( const auto& mapping : m_mappings.Reverse() )
	{
		if ( path.StartsWith( mapping.diskPath.AsStringView() ) )
		{
			result = ComputeResourcePath( path, mapping );
			break;
		}
	}

	return result;
}

Uint32 ResourceEditorDepot::GetResourceLevel( const ResourcePath& resourcePath ) const
{
	const red::StringView path = resourcePath.ToStringView();
	for ( const auto& mapping : m_mappings.Reverse() )
	{
		if ( path.StartsWith( mapping.mountPath ) )
		{
			return mapping.level;
		}
	}
	return std::numeric_limits<Uint32>::max();
}

red::AbsolutePath ResourceEditorDepot::TranslateExistingResourcePath( const red::StringView& path ) const
{
	// For reading we find a file using a reverse order of mappings
	// This means that later mappings override earlier ones.
	red::AbsolutePath result;

	for ( const auto& mapping : m_mappings.Reverse() )
	{
		if ( path.StartsWith( mapping.mountPath ) )
		{
			red::AbsolutePath fullPath = ComputeAbsolutePath( path, mapping );

			// We are checking to see if the file exists because we need to ensure that the 
			// resource system file exists in this mapping on disk.
			// We do this to handle the case when some different paths on disk are aliased 
			// to the same path in the resource system. This should not be a regular occurence
			// but is here to resolve the few cases when this can happen for a good reason.
			if ( GFileManager->FileExist( fullPath ) )
			{
				result = std::move( fullPath );
				break;
			}
		}
	}

	return result;
}

red::AbsolutePath ResourceEditorDepot::TranslateWritableResourcePath( const red::StringView& path ) const
{
	// For finding a file to write to we first check to find an existing file mounted somewhere
	red::AbsolutePath result = TranslateExistingResourcePath( path );

	// If we didn't find an existing file then look for a path where we could write a new file to
	if ( result.Empty() )
	{
		for ( const auto& mapping : m_mappings.Reverse() )
		{
			if ( path.StartsWith( mapping.mountPath ) )
			{
				result = ComputeAbsolutePath( path, mapping );
				break;
			}
		}
	}

	return result;
}

static ResourceEditorDepot* g_editorDepot = nullptr;

ResourceEditorDepot* GetEditorDepot()
{
	return g_editorDepot;
}

void SetEditorDepot( ResourceEditorDepot* depot )
{
	g_editorDepot = depot;
}

} // res
