/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../backendEngine/include/commandletUtils.h"

#include "../../backendData/include/fileLinkMap.h"

class ShowLinksCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ShowLinksCommandlet );

public:

	ShowLinksCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	res::ResourceEditorDepot* m_editorDepot;

	commandlet::FileLinkMap m_linkMap;
};

RTTI_BEGIN_TYPE( ShowLinksCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ShowLinksCommandlet::ShowLinksCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "show-links" ) )
	, m_editorDepot( commandlet::GetEditorDepot() )
{
}

const char* ShowLinksCommandlet::GetOneLiner() const
{
	return "Update resource references in resources for moved or renamed resources";
}

void ShowLinksCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------

namespace show_links_helper
{
	const char* GetErrorString( commandlet::FileLinkMap::ErrorType errorType )
	{
		using ErrorType = commandlet::FileLinkMap::ErrorType;
		switch ( errorType )
		{
		case ErrorType::GENERAL_ERROR:
			return "General / IO Error";
		case ErrorType::BOTH_RES_AND_LINK_EXIST:
			return "Both resource and link file exist";
		case ErrorType::CIRCULAR_LINK_CHAIN:
			return "Circular Link Cycle";
		case ErrorType::BROKEN_LINK:
			return "Broken Link";
		case ErrorType::LINK_TO_LINK:
			return "Link to Link";
		case ErrorType::INVALID_PATH:
			return "Invalid Path";
		case ErrorType::MAX_LINK_CHAIN:
			return "Maximum Link Chain";
		case ErrorType::MISSING_TARGET:
			return "Missing Link Target";
		default:
			return "UNRECOGNISED ERROR";
		};
	}

	String ComposePathTrace( const commandlet::FileLinkMap::ErrorEntry& error )
	{
		String result = "Trace: (in order of links followed)\n";
		Uint32 i = 0;
		do
		{
			const auto& path = error.previousPaths[i];
			red::StringView resPath = path.ToStringView();
			String linkPath = resPath.ToString() + ".link";
			result += String::Printf( "  %2u) %hs (%hs)\n", i, linkPath.AsChar(), resPath.Data() );
		}
		while ( ++i < error.previousPaths.Size() );
		return result;
	}

	String ComposeErrorMessage( const commandlet::FileLinkMap::ErrorEntry& error )
	{
		String result;

		const String errorPathStr = error.linkPath.ToString();
		const String originalPathStr = error.originalPath.ToString();

		using ErrorType = commandlet::FileLinkMap::ErrorType;
		switch ( error.type )
		{
		case ErrorType::GENERAL_ERROR:
			result = String::Printf( "Error reading file %hs\n", errorPathStr.AsChar() );
			break;

		case ErrorType::BOTH_RES_AND_LINK_EXIST:
		{
			result = String::Printf( "Both resource %hs and link %hs exist.\n", originalPathStr.AsChar(), errorPathStr.AsChar() );
			break;
		}
		case ErrorType::CIRCULAR_LINK_CHAIN:
		{
			result = String::Printf( "Circular link chain found at %hs\nCircular chain contains:\n", errorPathStr.AsChar() );
			for ( const auto& path : error.previousPaths )
			{
				String pathStr = path.ToString();
				result += String::Printf( "\t%hs.link (%hs)\n", pathStr.AsChar(), pathStr.AsChar() );
			}
			break;
		}
		case ErrorType::MAX_LINK_CHAIN:
		{
			result = String::Printf( "Link chain found that exceeds maximum allowed for %hs\nChain contains:\n", errorPathStr.AsChar() );
			for ( const auto& path : error.previousPaths )
			{
				String linkPath = path.ToString() + ".link";
				result += String::Printf( "\t%hs (%hs)\n", linkPath.AsChar(), path.ToString().AsChar() );
			}
			break;
		}
		case ErrorType::MISSING_TARGET:
		{
			red::String resPath = red::paths::RemoveExtension( errorPathStr ).ToString();
			result = String::Printf( "Missing target %hs found from %hs\n", resPath.AsChar(), originalPathStr.AsChar() );
			result += ComposePathTrace( error );
			break;
		}
		case ErrorType::BROKEN_LINK:
		{
			red::String resPath = red::paths::RemoveExtension( errorPathStr ).ToString();
			result = String::Printf( "Broken link found to %hs from %hs\n", resPath.AsChar(), originalPathStr.AsChar() );
			result += ComposePathTrace( error );
			break;
		}
		case ErrorType::INVALID_PATH:
		{
			result = String::Printf( "Linked resource %hs does not exist in this depot.\n", errorPathStr.AsChar() );
			result += ComposePathTrace( error );
			break;
		}
		case ErrorType::LINK_TO_LINK:
		{
			result = String::Printf( "Linking to link file %hs from:\n", errorPathStr.AsChar() );
			result += ComposePathTrace( error );
		}
		};

		return result;
	}


enum Report : Uint32
{
	SUMMARY							= RED_FLAG(0),
	LINKS							= RED_FLAG(1),

	LINK_MAPPING_SIMPLE				= RED_FLAG(4),
	LINK_MAPPING_DETAILED			= RED_FLAG(5),

	LINK_ERRORS_SIMPLE				= RED_FLAG(8),
	LINK_ERRORS_DETAILED			= RED_FLAG(9),

	MISSING_RESOURCES_SIMPLE		= RED_FLAG(12),
	MISSING_RESOURCES_DETAILED		= RED_FLAG(13),

	DEFAULT = SUMMARY | LINK_MAPPING_SIMPLE | LINK_ERRORS_SIMPLE,
	NONE = 0,
	ALL = 0xFFFFFFFF
};

bool ResourcePathStringOrder( const res::ResourcePath& left, const res::ResourcePath& right )
{
	return left.ToStringView() < right.ToStringView();
}

void PrintSummary( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& resources = linkMap.Resources();
	const auto& links = linkMap.Links();
	const auto& mapping = linkMap.Mapping();
	const auto& errors = linkMap.Errors();

	// Print resource statistics / summary
	fprintf( output, "Resources found: %u\n", resources.Size() );
	fprintf( output, "Links found: %u\n", links.Size() );
	fprintf( output, "Errors found: %u\n", errors.Size() );
}

void PrintLinks( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	// Note copy here
	red::DynArray< res::ResourcePath > links = linkMap.Links();
	std::sort( links.Begin(), links.End(), ResourcePathStringOrder );

	for ( const auto& link : links )
	{
		fprintf( output, "%hs\n", link.ToDebugString() );
	}
}

void PrintLinkMappingsSimple( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& mapping = linkMap.Mapping();

	red::Map< res::ResourcePath, res::ResourcePath > sortedMap;
	for ( const auto link : mapping )
	{
		sortedMap.InsertUnsorted( link.Key(), link.Value() );
	}
	sortedMap.MakeClean();

	for ( const auto link : sortedMap )
	{
		fprintf( output, "%hs -> %hs\n", link.Key().ToDebugString(), link.Value().ToDebugString() );
	}
}

void PrintLinkMappingsDetailed( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& reverseMapping = linkMap.ReverseMapping();

	red::Map< res::ResourcePath, red::Set< res::ResourcePath > > sortedMap;
	for  ( const auto res : reverseMapping )
	{
		sortedMap.InsertUnsorted( res.Key(), res.Value() );
	}
	sortedMap.MakeClean();

	for ( const auto res : sortedMap )
	{
		fprintf( output, "%hs\n", res.Key().ToDebugString() );
		for ( const auto link : res.Value() )
		{
			fprintf( output, "\t%hs\n", link.ToDebugString() );
		}
	}
}

void PrintLinkErrorsSimple( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& errors = linkMap.Errors();

	for ( const auto& error : errors )
	{
		fprintf( output, "%hs - %hs - (%hs)\n", error.originalPath.ToDebugString(), GetErrorString( error.type ), error.linkPath.ToDebugString() );
	}
}

void PrintLinkErrorsDetailed( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& errors = linkMap.Errors();

	for ( const auto& error : errors )
	{
		String message = ComposeErrorMessage( error );
		fprintf( output, "%hs\n", message.AsChar() );
	}
}

void PrintMissingResourcesSimple( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	red::DynArray< res::ResourcePath > missingResources = linkMap.MissingResources();
	std::sort( missingResources.Begin(), missingResources.End(), ResourcePathStringOrder );

	for ( const auto& res : missingResources )
	{
		fprintf( output, "%hs\n", res.ToDebugString() );
	}
}

void PrintMissingResourcesDetailed( FILE* output, const commandlet::FileLinkMap& linkMap )
{
	const auto& missingResourcesMap = linkMap.MissingResourcesMap();

	red::Map< res::ResourcePath, red::Set< res::ResourcePath > > sortedMap;
	for  ( const auto res : missingResourcesMap )
	{
		sortedMap.InsertUnsorted( res.Key(), res.Value() );
	}
	sortedMap.MakeClean();

	for ( const auto res : sortedMap )
	{
		fprintf( output, "%hs\n", res.Key().ToDebugString() );
		for ( const auto link : res.Value() )
		{
			fprintf( output, "\t%hs\n", link.ToDebugString() );
		}
	}
}


} // show_links_helper
using namespace show_links_helper;

//------------------------------------------------------------------------------

bool ShowLinksCommandlet::Execute( const red::CommandLine& options )
{
	FILE* output = stdout;
	String param;
	if ( options.GetFirstOption( "output", param ) )
	{
		output = fopen( param.AsChar(), "w" );
		if ( output == nullptr )
		{
			output = stdout;
		}
	}

	const bool showDetails = options.HasOption( "details" );

	Uint32 reportSections = Report::NONE;
	if ( options.HasOption( "show" ) )
	{
		auto showOptions = options.GetValues( "show" );
		for ( const auto& showOption : showOptions )
		{
			if ( showOption == "all" )
			{
				reportSections = Report::ALL;
				break;
			}

			if ( showOption == "links" )
			{
				reportSections |= Report::LINKS;
			}
			else if ( showOption == "missing" )
			{
				reportSections |= ( showDetails ? Report::MISSING_RESOURCES_DETAILED : Report::MISSING_RESOURCES_SIMPLE );
			}
			else if ( showOption == "mapping" )
			{
				reportSections |= ( showDetails ? Report::LINK_MAPPING_DETAILED : LINK_MAPPING_SIMPLE );
			}
			else if ( showOption == "errors" )
			{
				reportSections |= ( showDetails ? Report::LINK_ERRORS_DETAILED : Report::LINK_ERRORS_SIMPLE );
			}
		}
	}
	else
	{
		reportSections = ( showDetails ? Report::ALL : Report::DEFAULT );
	}

	if ( !m_linkMap.Initialise( *m_editorDepot ) )
	{
		return false;
	}

	if ( ( reportSections & Report::SUMMARY ) != 0 )
	{
		fprintf( output, "Summary:\n" );
		PrintSummary( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::LINKS ) != 0 )
	{
		if ( reportSections != Report::LINKS )
		{
			fprintf( output, "Links:\n" );
		}
		PrintLinks( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::LINK_MAPPING_SIMPLE ) != 0 )
	{
		if ( reportSections != Report::LINK_MAPPING_SIMPLE )
		{
			fprintf( output, "Link Mappings: (Link -> Resource)\n" );
		}
		PrintLinkMappingsSimple( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::LINK_MAPPING_DETAILED ) != 0 )
	{
		if ( reportSections != Report::LINK_MAPPING_DETAILED )
		{
			fprintf( output, "Link Mappings: (Resource: Link Resources...)\n" );
		}
		PrintLinkMappingsDetailed( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::LINK_ERRORS_SIMPLE ) != 0 )
	{
		if ( reportSections != Report::LINK_ERRORS_SIMPLE )
		{
			fprintf( output, "Link Errors: (Resource - Error - (Original link)\n" );
		}
		PrintLinkErrorsSimple( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::LINK_ERRORS_DETAILED ) != 0 )
	{
		if ( reportSections != Report::LINK_ERRORS_DETAILED )
		{
			fprintf( output, "Link Errors:\n" );
		}
		PrintLinkErrorsDetailed( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::MISSING_RESOURCES_SIMPLE ) != 0 )
	{
		if ( reportSections != Report::MISSING_RESOURCES_SIMPLE )
		{
			fprintf( output, "Missing Resources:\n" );
		}
		PrintMissingResourcesSimple( output, m_linkMap );
		fprintf( output, "\n" );
	}

	if ( ( reportSections & Report::MISSING_RESOURCES_DETAILED ) != 0 )
	{
		if ( reportSections != Report::MISSING_RESOURCES_DETAILED )
		{
			fprintf( output, "Missing Resources: (Resource: Links...)\n" );
		}
		PrintMissingResourcesDetailed( output, m_linkMap );
		fprintf( output, "\n" );
	}

	fflush( output );

	if ( output != stdout )
	{
		fclose( output );
	}

	return true;
}
