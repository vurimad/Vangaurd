/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "findResourceCommandlet.h"

#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"


RTTI_BEGIN_ABSTRACT_TYPE( FindResourceCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

FindResourceCommandlet::FindResourceCommandlet()
	: FindResourceCommandlet( RED_NAME_CONSTEXPR( "find-resource" ) )
{
}

FindResourceCommandlet::FindResourceCommandlet( CName name )
	: ICommandlet( name )
	, m_sourceDepot( commandlet::GetEditorDepot() )
{
}

const char* FindResourceCommandlet::GetOneLiner() const
{
	return "Find for resources that met a specific criteria";
}

void FindResourceCommandlet::PrintHelp() const
{
	fprintf( stdout, R"(FindResourceCommandlet

Parameters:

-file=<resource path> (<resource path> ...)
	Specify individual files to look for

-dir=<resource dir path> (<resource dir path> ...)
	Specify directories to look for, scanned recursively

-all
	Look for resources in the whole depot

-glob=<pattern> (<pattern> ...)
	Specify a pattern to filter files with, applies to entries specified with above options

-outputlist=<file>
	Write processed resources to an output list, files are written with absolute path
)" );
}

bool FindResourceCommandlet::ParseResourcePaths( const red::CommandLine& options )
{
	m_resources.Clear();
	if ( commandlet::HasResourcePathsCommandLineOption( options ) )
	{
		m_resources = commandlet::ParseResourcePathsFromCommandLine( *commandlet::GetEditorDepot(), options );
	}
	else if ( options.HasOption( "glob" ) )
	{
		// If we have a glob pattern specified but no other file filter then apply it to the whole depot
		m_resources = commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "" );
	}
	if ( m_resources.Empty() )
	{
		RED_LOG_ERROR( "No resources specified, please use -all, -dir=<path>, -file=<path>, or -glob=<pattern>" );
		return false;
	}

	String str;
	if ( options.GetFirstOption( "glob", str ) )
	{
		auto endIter = std::remove_if( m_resources.Begin(), m_resources.End(), 
			[pattern = str]( const res::ResourcePath& path )
		{
			return !red::GlobMatch( path.ToStringView(), pattern );
		} );
		m_resources.Remove( endIter, m_resources.End() );

		if ( m_resources.Empty() )
		{
			RED_LOG_ERROR( "Pattern '%hs' filtered out all resources", str.AsChar() );
			return false;
		}
	}

	return true;
}

bool FindResourceCommandlet::ParseAbsolutePaths( const red::CommandLine& options )
{
	String str;
	if ( options.GetFirstOption( "absoluteFile", str ) )
	{
		m_absoluteFile = red::AbsolutePath::ParseFilePath( str );
		return true;
	}

	return false;
}

bool FindResourceCommandlet::ParseOptions( const red::CommandLine& options )
{
	if ( options.HasOption( "absoluteFile" ) )
	{
		if ( !ParseAbsolutePaths( options ) )
		{
			return false;
		}
	}
	else
	{
		if ( !ParseResourcePaths( options ) )
		{
			return false;
		}
	}

	String str;

	if ( options.GetFirstOption( "outputlist", str ) )
	{
		m_outputListingFile = red::AbsolutePath::ParseFilePath( str );
	}

	return true;
}

bool FindResourceCommandlet::LoadResource( const res::ResourcePath& path, THandle< CResource >& resource )
{
	res::IssueLoadingRequestParameter param{ path };
	ApplyResourceLoadingParams( param );

	auto token = GResourceLoader->IssueLoadingRequest( param );
	if ( token->HasFailed() )
	{
		RED_LOG_ERROR( "FindResourceCommandlet: Could not load %hs for reading", path.ToDebugString() );
		return false;
	}

	resource = token->WaitUntilLoaded();
	if ( !resource || token->HasFailed() )
	{
		RED_LOG_ERROR( "FindResourceCommandlet: Failed to load %hs", path.ToDebugString() );
		return false;
	}

	return true;
}

void FindResourceCommandlet::ReportMatch( const res::ResourcePath& path )
{
	fprintf( stdout, "Found matching resource: %hs\n", path.ToString().AsChar() );

	if ( !m_outputListingFile.Empty() )
	{
		//auto absPath = m_sourceDepot->TranslateResourcePath( path );
		String outputStr = path.ToString();
		outputStr += "\r\n";

		red::AppendStringToFile( m_outputListingFile, outputStr );
	}
}

bool FindResourceCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseOptions( options ) )
	{
		return false;
	}

	if ( !GFileManager->FileExist( m_outputListingFile ) )
	{
		red::SaveStringToFile( m_outputListingFile, "" );
	}

	// filter out unsupported extensions
	auto endIter = std::remove_if( m_resources.Begin(), m_resources.End(), [this] ( const res::ResourcePath& path )
	{
		return !SupportsResourcePath( path );
	} );
	m_resources.Remove( endIter, m_resources.End() );

	const Uint32 totalResources = m_resources.Size();
	Uint32 currentResource = 0;
	for ( const auto& path : m_resources )
	{
		currentResource++;
		RED_LOG( "FindResourceCommandlet: Processing %hs %u/%u", path.ToDebugString(), currentResource, totalResources );

		if ( !IsResource( path ) || IsTextResource( path ) )
		{
			RED_LOG_WARNING( "FindResourceCommandlet: Resource %hs cannot be processed", path.ToDebugString() );
			continue;
		}

		THandle< CResource > resource;

		if ( LoadResource( path, resource ) )
		{
			if ( CheckResource( *resource ) )
			{
				ReportMatch( path );
			}
		}
	}

	return true;
}

Bool FindResourceCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void FindResourceCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void FindResourceCommandlet::ApplyResourceLoadingParams( res::IssueLoadingRequestParameter& param ) const
{
	param.skipImports = true;
	param.skipPostLoad = true;
	param.postLoadFlags.skipUploadRenderData = true;
}

bool FindResourceCommandlet::SupportsResourcePath( const res::ResourcePath& ) const
{
	return true;
}