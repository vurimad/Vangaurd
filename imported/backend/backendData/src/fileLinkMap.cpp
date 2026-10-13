/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileLinkMap.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceDepotUtils.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/dependencyScannerImpl.h"

namespace commandlet
{

FileLinkMap::FileLinkMap()
	: m_depot( nullptr )
{
}

FileLinkMap::FileLinkMap( FileLinkMap&& other ) = default;
FileLinkMap& FileLinkMap::operator=( FileLinkMap&& other ) = default;
FileLinkMap::~FileLinkMap() = default;


bool FileLinkMap::Initialise( res::IBackendResourceDepot& depot )
{
	PC_SCOPE( FileLinkMap_Initialise );

	m_depot = &depot;

	red::DynArray< res::ResourcePath > allResources = commandlet::ScanResourceDepotDirectories( depot, "" );
	InitialiseFileLists( allResources );

	return true;
}

bool FileLinkMap::Initialise( res::IBackendResourceDepot& depot, red::DynArray< res::ResourcePath >& allResources )
{
	PC_SCOPE( FileLinkMap_Initialise );

	m_depot = &depot;

	InitialiseFileLists( allResources );

	return true;
}

void FileLinkMap::InitialiseFileLists( red::DynArray< res::ResourcePath >& allResources )
{
	auto isNotLinkFile = []( const res::ResourcePath& path ) -> bool
	{
		return red::paths::GetExtension( path.ToStringView() ) != "link";
	};

	// Split into two sections, with link files at the back
	auto linkFilesBegin = std::partition( allResources.Begin(), allResources.End(), isNotLinkFile );

	// Copy non-link resources into final destination
	m_resources.Resize( static_cast<Uint32>( std::distance( allResources.Begin(), linkFilesBegin ) ) );
	std::copy( allResources.Begin(), linkFilesBegin, m_resources.Begin() );
	std::sort( m_resources.Begin(), m_resources.End() );

	// Copy link resources into final destination
	m_links.Resize( static_cast<Uint32>( std::distance( linkFilesBegin, allResources.End() ) ) );
	std::copy( linkFilesBegin, allResources.End(), m_links.Begin() );
	std::sort( m_links.Begin(), m_links.End() );

	// For every link file, find its ultimate resource file
	m_linkFileMap.Reserve( m_links.Size() );
	for ( const auto& path : m_links )
	{
		ResolveLinkFile( path );
	}

	// Create a reverse link mapping
	for ( const auto& linkEntry : m_linkFileMap )
	{
		const auto& linkPath = linkEntry.Key();
		const auto& resPath = linkEntry.Value();
		m_resourceLinkMap[ resPath ].InsertUnsorted( linkPath );
	}
	for ( const auto& resEntry : m_resourceLinkMap )
	{
		resEntry.Value().MakeClean();
	}

	// Sort errors by resource path string order
	auto stringOrder = []( const ErrorEntry& left, const ErrorEntry& right ) -> bool
	{
		return left.linkPath.ToStringView() < right.linkPath.ToStringView();
	};

	std::stable_sort( m_errorResources.Begin(), m_errorResources.End(), stringOrder );
}

void FileLinkMap::ResolveLinkFile( const res::ResourcePath& path )
{
	// Compute the original resource path for this resource
	res::ResourcePath originalResourcePath = res::ResourcePath::Build( red::paths::RemoveExtension( path.ToStringView() ) );

	if ( std::binary_search( m_resources.Begin(), m_resources.End(), originalResourcePath ) )
	{
		// Found a resource and link file with the same base name that exist
		m_errorResources.PushBack( { path, originalResourcePath, ErrorType::BOTH_RES_AND_LINK_EXIST, { originalResourcePath } } );
		// No point continuing here as the actual resource will be found not the link file
		return;
	}

	// Could have been created by another link file in a chain
	if ( m_linkFileMap.KeyExist( originalResourcePath ) )
		return;

	red::DynArray< res::ResourcePath > previousPaths;
	previousPaths.PushBack( originalResourcePath );
	ResolveSingleLink( path, originalResourcePath, path, previousPaths );
}

void FileLinkMap::ResolveSingleLink( const res::ResourcePath& linkPath, const res::ResourcePath& originalPath, const res::ResourcePath& originalLinkPath, red::DynArray< res::ResourcePath >& previousPaths )
{
	// 1.) First try to resolve the link file itself, and check that it exists and contains valid data
	red::AbsolutePath absoluteLinkPath = m_depot->TranslateResourcePath( linkPath );
	if ( absoluteLinkPath.Empty() )
	{
		// Resource path is not mapped into this depot
		m_errorResources.PushBack( { linkPath, originalPath, ErrorType::INVALID_PATH, previousPaths } );
		return;
	}

	if ( !GFileManager->FileExist( absoluteLinkPath ) )
	{
		// Neither the resource or the link file exist, therefore mark the original link file as being in error
		m_errorResources.PushBack( { originalLinkPath, originalPath, ErrorType::MISSING_TARGET, previousPaths } );

		// Maintain a list of missing resources that link files point to
		const auto& missingResourcePath = previousPaths.Back();
		for ( const auto& prevPath : previousPaths )
		{
			m_missingResourcesMap[ missingResourcePath ].Insert( prevPath );
		}
		return;
	}

	String resourceStr;
	if ( !red::LoadFileToString( absoluteLinkPath, resourceStr ) )
	{
		// IO or other error preventing the link file from being read
		m_errorResources.PushBack( { linkPath, originalPath, ErrorType::GENERAL_ERROR, previousPaths } );
		return;
	}

	if ( resourceStr.Length() > res::ResourcePath::MAX_LENGTH || resourceStr.Length() < 4 || !dependency::prv::LooksLikeResourcePath( resourceStr ) )
	{
		// Invalid contents of a link file
		m_errorResources.PushBack( { linkPath, originalPath, ErrorType::BROKEN_LINK, previousPaths } );
		return;
	}

	res::ResourcePath resourcePath = res::ResourcePath::Build( resourceStr );
	if ( red::paths::GetExtension( resourcePath.ToStringView() ) == "link" )
	{
		// The linked to resource is a .link file, therefore the engine will not load it
		m_errorResources.PushBack( { linkPath, originalPath, ErrorType::LINK_TO_LINK, previousPaths } );
		return;
	}

	// 2.) Try to find the resource that the link file was pointing to
	// We have a list of resources already therefore just try and find it in this list rather than on disk
	if ( std::binary_search( m_resources.Begin(), m_resources.End(), resourcePath ) )
	{
		// If found then direct all previous link paths to this resource
		for ( const auto& prevPath : previousPaths )
		{
			m_linkFileMap.Insert( prevPath, resourcePath );
		}
		return;
	}

	// 3.) Try and find the linked-to resource path in our link file mapping
	// This means that we have already traversed this link chain therefore we can just
	// resolve all of our links to point to the same resource
	auto foundIter = m_linkFileMap.Find( resourcePath );
	if ( foundIter != m_linkFileMap.End() )
	{
		for ( const auto& prevPath : previousPaths )
		{
			m_linkFileMap.Insert( prevPath, foundIter.Value() );
		}
		return;
	}

	// 4.) Check that the linked to resource path is valid enough
	// So that we can attempt to find its link file and recurse down the chain
	if ( previousPaths.Exist( resourcePath ) )
	{
		m_errorResources.PushBack( { linkPath, originalPath, ErrorType::CIRCULAR_LINK_CHAIN, previousPaths } );
		return;
	}

	if ( previousPaths.Size() == res::MAX_LINK_FILE_RECURSION )
	{
		m_errorResources.PushBack( { originalLinkPath, originalPath, ErrorType::MAX_LINK_CHAIN, previousPaths } );
	}

	previousPaths.PushBack( resourcePath );

	String linkPathStr = resourcePath.ToString() + ".link";
	res::ResourcePath linkResourcePath = res::ResourcePath::Build( linkPathStr );
	ResolveSingleLink( linkResourcePath, originalPath, originalLinkPath, previousPaths );
}

} // commandlet
