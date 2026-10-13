/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../backendData/include/fileLinkMap.h"
#include "../../backendData/include/resourceDatabase.h"

class ShowRedundantLinksCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ShowRedundantLinksCommandlet );

public:

	ShowRedundantLinksCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	commandlet::FileLinkMap m_linkMap;
	resdb::Database m_database;
};

RTTI_BEGIN_TYPE( ShowRedundantLinksCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ShowRedundantLinksCommandlet::ShowRedundantLinksCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "show-redundant-links" ) )
{
}

const char* ShowRedundantLinksCommandlet::GetOneLiner() const
{
	return "Show link files which are now redundant and can be safely deleted";
}

void ShowRedundantLinksCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------

namespace redundant_links_helper
{

// Redundant links are:
// 1) no longer referenced by anyone
// 2) refer to resources that no longer exist
// 3) refer to a link file
// 4) represent a link file themselves (.link.link)
// 5) exist next to the file they are representing (both a.res and a.res.link exist)
// 6) form a circular link chain
// 7) have more than the maximum number of links in a chain to reach a valid resource


enum class Reason : Uint16
{
	UnknownError = 0,
	Unreferenced,
	TargetMissing,
	TargetIsLink,
	LinkDotLink,
	ShadowedLink,
	CircularChain,
	ChainTooLong,
	InvalidLink,
};

using LinkMapErrorType = commandlet::FileLinkMap::ErrorType;

Reason GetReason( LinkMapErrorType errorType )
{
	switch ( errorType )
	{
	case LinkMapErrorType::BOTH_RES_AND_LINK_EXIST:
		return Reason::ShadowedLink;
	case LinkMapErrorType::CIRCULAR_LINK_CHAIN:
		return Reason::CircularChain;
	case LinkMapErrorType::MISSING_TARGET:
		return Reason::TargetMissing;
	case LinkMapErrorType::BROKEN_LINK:
		return Reason::InvalidLink;
	case LinkMapErrorType::INVALID_PATH:
		return Reason::InvalidLink;
	case LinkMapErrorType::LINK_TO_LINK:
		return Reason::LinkDotLink;
	case LinkMapErrorType::MAX_LINK_CHAIN:
		return Reason::ChainTooLong;
	case LinkMapErrorType::GENERAL_ERROR:
	default:
		return Reason::UnknownError;
	}
}

const char* GetReasonString( Reason reason )
{
	switch ( reason )
	{
	case Reason::Unreferenced:
		return "Link is no longer referenced";
	case Reason::TargetMissing:
		return "Link target is missing";
	case Reason::TargetIsLink:
		return "Link target is a link file";
	case Reason::LinkDotLink:
		return "Is a link.link file";
	case Reason::ShadowedLink:
		return "Both link and original file exist";
	case Reason::CircularChain:
		return "Link forms a circular chain of links";
	case Reason::ChainTooLong:
		return "Link forms chain that exceeds maximum";
	case Reason::InvalidLink:
		return "Invalid target of link file";
	case Reason::UnknownError:
	default:
		return "Unknown";
	}
}

struct RedundantPathEntry
{
	res::ResourcePath path;
	Reason reason;
};

bool operator == ( const RedundantPathEntry& left, const RedundantPathEntry& right )
{
	return left.path == right.path;
}

bool operator < ( const RedundantPathEntry& left, const RedundantPathEntry& right )
{
	return left.path < right.path;
}

bool operator == ( const RedundantPathEntry& left, const res::ResourcePath& right )
{
	return left.path == right;
}

bool operator == ( const res::ResourcePath& left, const RedundantPathEntry& right )
{
	return left == right.path;
}

bool operator < ( const RedundantPathEntry& left, const res::ResourcePath& right )
{
	return left.path < right;
}

bool operator < ( const res::ResourcePath& left, const RedundantPathEntry& right )
{
	return left < right.path;
}


struct PotentialLinkEntry
{
	res::ResourcePath linkPath;
	red::DynArray< res::ResourcePath > reversePaths;
};

red::DynArray< RedundantPathEntry > GetRedundantLinks( const commandlet::FileLinkMap& linkMap, const resdb::Database& database )
{
	red::DynArray< RedundantPathEntry > result;
	result.Reserve( linkMap.Links().Size() );

	// Any link with an error should be automatically deleted
	// Handles all but case 1
	const auto& errorLinks = linkMap.Errors();
	for ( const auto& entry : errorLinks )
	{
		result.PushBack( { entry.linkPath, GetReason( entry.type ) } );
	}

	// Stores the links that have references to them
	red::DynArray< PotentialLinkEntry > remainingLinks;

	for ( const auto& linkPath : linkMap.Links() )
	{
		res::ResourcePath originalPath = res::ResourcePath::Build( red::paths::RemoveExtension( linkPath.ToStringView() ) );
		auto reversePaths = resdb::Database::ExtractFromPaths( database.GetBackwardLinks( originalPath ) );
		if ( reversePaths.Empty() )
		{
			result.PushBack( { linkPath, Reason::Unreferenced } );
		}
		else
		{
			if ( std::all_of( reversePaths.Begin(), reversePaths.End(), IsLinkResource ) )
			{
				remainingLinks.PushBack( { linkPath, reversePaths } );
			}
		}
	}

	red::alg::RemoveDuplicates( result );

	RED_LOG( "RedundantLinks: %u found in first phase, %u potential left to inspect", result.Size(), remainingLinks.Size() );

	// Loop through the remaining links until there are none left
	while ( !remainingLinks.Empty() )
	{
		for ( auto& entry : remainingLinks )
		{
			auto& reversePaths = entry.reversePaths;
			auto end = std::remove_if( reversePaths.Begin(), reversePaths.End(), [&result]( const res::ResourcePath& reversePath ) -> bool
			{
				return std::binary_search( result.Begin(), result.End(), reversePath );
			} );
			reversePaths.Remove( end, entry.reversePaths.End() );
		}

		auto end = std::partition( remainingLinks.Begin(), remainingLinks.End(), []( const PotentialLinkEntry& entry ) -> bool 
		{
			return !entry.reversePaths.Empty();
		} );
		if ( end == remainingLinks.End() )
		{
			RED_LOG( "RedundantLinks: %u links remain valid", remainingLinks.Size() );
			break;
		}

		RED_LOG( "RedundantLinks: Found %u links in phase", static_cast< Uint32 >( std::distance( end, remainingLinks.End() ) ) );

		for ( auto iter = end; iter != remainingLinks.End(); ++iter )
		{
			result.PushBack( { iter->linkPath, Reason::Unreferenced } );
		}
		red::alg::RemoveDuplicates( result );
		remainingLinks.Remove( end, remainingLinks.End() );
	}

	return result;
}

} // redundant_links_helper
using namespace redundant_links_helper;

//------------------------------------------------------------------------------

bool ShowRedundantLinksCommandlet::Execute( const red::CommandLine& options )
{
	commandlet::StandardOutput output{ options };
	commandlet::BackendDepotHolder depotHolder{ options };

	res::IBackendResourceDepot* depot = depotHolder.GetDepot();
	red::AbsolutePath resdbCachePath = depotHolder.GetCachePath();

	resdbCachePath.AppendFilePath( resdb::Database::GetDefaultFilename() );

	const bool printReason = options.HasOption( "reason" );
	const bool printAbsolutePath = options.HasOption( "absolute" );

	m_database.Initialise( *depot, resdbCachePath );

	auto allResources = m_database.GetResourcePaths();
	if ( !m_linkMap.Initialise( *depot, allResources ) )
	{
		return false;
	}

	auto redundantLinks = GetRedundantLinks( m_linkMap, m_database );

	auto stringSorted = []( const RedundantPathEntry& left, const RedundantPathEntry& right ) -> bool
	{
		return res::SortByDirectory( left.path, right.path );
	};
	std::sort( redundantLinks.Begin(), redundantLinks.End(), stringSorted );

	red::AbsolutePath absPath;

	fprintf( output, "Redundant links:\n" );
	for ( const auto& entry : redundantLinks )
	{
		if ( printAbsolutePath )
		{
			absPath = depot->TranslateResourcePath( entry.path );
			fprintf( output, "%hs", absPath.AsChar() );
		}
		else
		{
			fprintf( output, "%hs", entry.path.ToDebugString() );
		}

		if ( printReason )
		{
			fprintf( output, "\t%hs", GetReasonString( entry.reason ) );
		}

		fprintf( output, "\n" );
	}

	m_database.Shutdown();

	return true;
}
