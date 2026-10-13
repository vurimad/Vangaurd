/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/redReflection/include/serializationBinaryStructureMapper.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/packageCompiler.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/fileLinkMap.h"
#include "../../backendData/include/linkResolver.h"
#include "dependencyMap.h"

class RelinkCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( RelinkCommandlet );

public:

	RelinkCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	bool ParseOptions( const red::CommandLine& options );

	bool ProcessResource( const res::ResourcePath& path );

	red::DynArray< res::ResourcePath > m_resources;

	red::AbsolutePath m_outputResourceListFile;
	red::AbsolutePath m_outputAbsoluteFileListFile;
	bool m_dryrun;
	bool m_skipPostLoad;
	bool m_skipLoadImports;

	res::ResourceEditorDepot* m_editorDepot;
	commandlet::FileLinkMap m_linkMap;
	commandlet::DependencyMap m_dependencyMap;

	commandlet::EngineInstance m_engineInstance;
};

RTTI_BEGIN_TYPE( RelinkCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

RelinkCommandlet::RelinkCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "relink" ) )
	, m_editorDepot( commandlet::GetEditorDepot() )
	, m_dependencyMap( m_editorDepot, commandlet::DependencyOrder::BY_HASH )
	, m_dryrun( false )
	, m_skipPostLoad( true )
	, m_skipLoadImports( true )
{
}

const char* RelinkCommandlet::GetOneLiner() const
{
	return "Update resource references in resources for moved or renamed resources";
}

void RelinkCommandlet::PrintHelp() const
{
}

bool RelinkCommandlet::ParseOptions( const red::CommandLine& options )
{
	m_resources = commandlet::ParseResourcePathsFromCommandLine( *m_editorDepot, options );
	if ( m_resources.Empty() )
	{
		RED_LOG_ERROR( "No resources specified, please use -all, -dir=<path>, or -file=<path>" );
		return false;
	}

	String param;
	if ( options.GetFirstOption( "outputResourceList", param ) )
	{
		m_outputResourceListFile = red::AbsolutePath::ParseFilePath( param );
	}

	if ( options.GetFirstOption( "outputAbsoluteFileList", param ) )
	{
		m_outputAbsoluteFileListFile = red::AbsolutePath::ParseFilePath( param );
	}

	m_dryrun = options.HasOption( "dryrun" );
	m_skipPostLoad = !options.HasOption( "postLoad" );
	m_skipLoadImports = !options.HasOption( "loadImports" );

	return true;
}

Bool RelinkCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void RelinkCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

//------------------------------------------------------------------------------

bool RelinkCommandlet::ProcessResource( const res::ResourcePath& path )
{
	const auto extension = red::paths::ExtractExtension( path.ToStringView() );

	THandle< CResource > resource;

	// Load Resource
	{
		res::IssueLoadingRequestParameter parameter;
		parameter.path = path;
		parameter.skipPostLoad = m_skipPostLoad;
		parameter.skipImports = m_skipLoadImports;
		parameter.postLoadFlags.skipUploadRenderData = true;
		auto token = GResourceLoader->IssueLoadingRequest( parameter );
		resource = token->WaitUntilLoaded();
		if ( !resource )
		{
			RED_LOG_ERROR( "Failed to load resource in '%hs'", path.ToDebugString() );
			return false;
		}

		// Make sure all buffers are read so that the async source isn't needed anymore
		serialization::DeferredDataBufferLoader bufferLoader;
		bufferLoader.Run( resource );
	}

	// Save Resource
	{
		auto filePtr = m_editorDepot->CreateResourceWriter( path );
		if ( !filePtr )
		{
			RED_LOG_ERROR( "Cannot create file writer for '%hs'", path.ToDebugString() );
			return false;
		}

		auto saver = serialization::ISaver::CreateSaver( extension.AsChar() );
		if ( !saver )
		{
			RED_LOG_ERROR( "Cannot create saver object for extension '%hs'", extension.AsChar() );
			return false;
		}

		serialization::SavingContext context{ resource };
		if ( !saver->SaveObjects( *filePtr, context ) )
		{
			RED_LOG_ERROR( "Failed to save objects into the file" );
			return false;
		}
	}

	resource->UnmarkModified();

	return true;
}

//------------------------------------------------------------------------------

bool RelinkCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseOptions( options ) )
	{
		return false;
	}

	// Scan the depot file system and find information about all of the files
	if ( !m_linkMap.Initialise( *m_editorDepot ) )
	{
		return false;
	}

	auto linkFileList = m_linkMap.Mapping().GetKeys();
	if ( linkFileList.Empty() )
	{
		RED_LOG_ERROR( "No link files detected in the Depot!" );
		return true;
	}
	std::sort( linkFileList.Begin(), linkFileList.End() );

	commandlet::FileLinkResolver linkResolver{ m_linkMap };
	serialization::StructureMapper::SetPathResolver( &linkResolver );
	red::PackageCompiler::Internal_SetPathResolver( &linkResolver );

	const Uint32 totalResources = m_resources.Size();
	Uint32 currentResource = 0;
	for ( const auto& path : m_resources )
	{
		currentResource++;
		RED_LOG( "Status: Processing resource %hs %u/%u", path.ToDebugString(), currentResource, totalResources );

		m_dependencyMap.ScanResourceRecursive( path );
		const auto* entry = m_dependencyMap.Get( path );
		const auto dependencies = entry->Dependencies();
		if ( dependencies.Empty() )
		{
			RED_LOG_WARNING( "No dependencies found in file %hs", path.ToDebugString() );
			continue;
		}

		// Since the relinking is a forceful resave operation (because we need to fix the links on save)
		// then we only want to do it for resources which contain paths that reference resources
		// that are now represented by link files
		if ( !res::ContainsCommonPath( linkFileList, dependencies ) )
		{
			RED_LOG_WARNING( "Resource %hs contains no paths which are link files", path.ToDebugString() );
			continue;
		}

		if ( !m_dryrun )
		{
			ProcessResource( path );
		}

		if ( !m_outputResourceListFile.Empty() )
		{
			String outputStr = path.ToString();
			outputStr += "\r\n";

			red::AppendStringToFile( m_outputResourceListFile, outputStr );
		}

		if ( !m_outputAbsoluteFileListFile.Empty() )
		{
			auto absPath = m_editorDepot->TranslateResourcePath( path );
			String outputStr = absPath.AsString();
			outputStr += "\r\n";

			red::AppendStringToFile( m_outputAbsoluteFileListFile, outputStr );
		}
	}

	serialization::StructureMapper::SetPathResolver( nullptr );
	red::PackageCompiler::Internal_SetPathResolver( nullptr );

	return true;
}
