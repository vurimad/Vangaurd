/**
 * Copyright c 2013 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "resaveCommandlet.h"

#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/serializationAsyncSource.h"
#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/serializationBinaryStructureMapper.h"
#include "../../../common/redReflection/include/packageCompiler.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/serializationAsyncSourceImpl.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"
#include "../../backendData/include/fileLinkMap.h"
#include "../../backendData/include/linkResolver.h"
#include "../../backendData/include/dependencyScanner.h"
#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/redJobs2/include/jobSystem.h"


RTTI_BEGIN_TYPE( ResaveCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

ResaveCommandlet::ResaveCommandlet()
	: ResaveCommandlet( RED_NAME_CONSTEXPR( "resave" ) )
{
}

ResaveCommandlet::ResaveCommandlet( CName name )
	: ICommandlet( name )
	, m_sourceDepot( commandlet::GetEditorDepot() )
	, m_outputDepot( nullptr )
	, m_force( false )
	, m_relink( true )
	, m_dryrun( false )
	, m_multithreaded( false )
	, m_skipLoadImports( true )
	, m_skipPostLoad( true )
{
}

const char* ResaveCommandlet::GetOneLiner() const
{
	return "Resave/Upgrade data formats to latest versions";
}

void ResaveCommandlet::PrintHelp() const
{
	/*
	Parameters:

	-file=<resource path> (<resource path> ...)
		Specify individual files to resave

	-dir=<resource dir path> (<resource dir path> ...)
		Specify directories to resave, scanned recursively

	-all
		Resave all resources in the depot

	-glob=<pattern> (<pattern> ...)
		Specify a pattern to filter files with, applies to entries specified with above options

	-outputDepot=<depot path>
		Path to flattened output depot so files are not overridden in place

	-outputlist=<file>
		Write processed resources to an output list, files are written with absolute path

	-force
		Force resaving of all specified files

	-norelink
		Do not perform a relink while resaving
	
	-dry-run
		Perform a dry run of the resave which will only output the list of files processed

	-strict
		Fail the commandlet if it failed to process any resources

	-multithreaded / -mt
		Runs all resource processing in parallel. Speeds up resave. Should be used for going with trivial/same type resources

	-loadImports
	-postLoad
		Perform loading of imports or post load of the resource, normally these are skipped
	*/
}

bool ResaveCommandlet::ParseResourcePaths( const red::CommandLine& options )
{
	// collect resources
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

	// remove resources whose path do not include the glob pattern (there might be multiple patterns specified)
	if ( options.HasOption( "glob" ) )
	{
		red::DynArray< String > patterns = options.GetValues( "glob" );

		if ( patterns.Empty() )
		{
			RED_LOG_ERROR( "'glob' pattern(s) not specified, please remove -glob or specify patterns." );
			return false;
		}

		auto endIter = std::remove_if( m_resources.Begin(), m_resources.End(), [ &patterns ]( const res::ResourcePath& path )
		{
			return std::none_of( patterns.Begin(), patterns.End(), [ &path ]( const String& pattern )
			{
				return red::GlobMatch( path.ToStringView(), pattern );
			} );
		} );

		m_resources.Remove( endIter, m_resources.End() );

		if ( m_resources.Empty() )
		{
			RED_LOG_ERROR( "'glob' pattern(s) filtered out all resources." );
			return false;
		}
	}

	return true;
}

bool ResaveCommandlet::ParseAbsolutePaths( const red::CommandLine& options )
{
	String str;
	if ( options.GetFirstOption( "absoluteFile", str ) )
	{
		m_absoluteFile = red::AbsolutePath::ParseFilePath( str );
		return true;
	}

	return false;
}

bool ResaveCommandlet::ParseOptions( const red::CommandLine& options )
{

	String str;
	if ( options.GetFirstOption( "outputDepot", str ) )
	{
		auto outputDepotPath = red::AbsolutePath::ParseDirPath( str );
		m_ownedOutputDepot = red::CreateUniquePtr< res::prv::SingleTreeResourceDepot >( outputDepotPath );
		m_outputDepot = m_ownedOutputDepot.Get();
	}
	else
	{
		m_outputDepot = m_sourceDepot;
	}

	if ( options.GetFirstOption( "outputlist", str ) )
	{
		m_outputListingFile = red::AbsolutePath::ParseFilePath( str );
	}

	m_force = options.HasOption( "force" );
	m_relink = ! options.HasOption( "norelink" );
	m_dryrun = options.HasOption( "dry-run" );
	m_strict = options.HasOption( "strict" );
	m_multithreaded = options.HasOption( "multithreaded" ) || options.HasOption( "mt" );

	m_skipLoadImports = ! options.HasOption( "loadImports" );
	m_skipPostLoad = ! options.HasOption( "postLoad" );

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

	return true;
}

//-----------------------------------------------------------------------------

// Kind of bad way of checking the header and the dependencies
// should have an object which keeps the file open for all the checks so that 
// we don't have to keep reopening it and can reuse it for the resave itself

bool ResaveCommandlet::NeedsResave( const res::ResourcePath& path )
{
	auto reader = m_sourceDepot->CreateResourceReader( path, false );

	serialization::FileTables::Header header;
	if ( reader->GetSize() < sizeof( header ) )
	{
		return false;
	}

	reader->Serialize( &header, sizeof( header ) );
	if ( reader->HasErrors() )
	{
		return false;
	}

	return ( VER_MINIMAL <= header.m_version && header.m_version < VER_CURRENT );
}

bool ResaveCommandlet::NeedsRelink( const res::ResourcePath& path )
{
	RED_FATAL_ASSERT( m_relink && m_linkMap );

	red::DynArray< res::ResourcePath > dependencies;
	if ( ! dependency::ScanResource( m_sourceDepot, path, dependencies ) )
	{
		// Error scanning dependencies, leave file alone
		return false;
	}

	if ( dependencies.Empty() )
	{
		// No dependencies, all good
		return false;
	}

	static auto linkFileList = m_linkMap->Mapping().GetKeys();
	return res::ContainsCommonPath( dependencies, linkFileList );
}

//------------------------------------------------------------------------------

// NOTE: Two different ways of loading resources for resaving
// We will need to use option one if we want to process files specified by absolute paths
// Option two can be used when only dealing with depot resources

// Loads the resource using the lowest level code that supports both JSON and Binary resources
bool ResaveCommandlet::LoadResource_DeconstructedMethod( const res::ResourcePath& path, THandle< CResource >& resource )
{
	auto reader = m_sourceDepot->CreateResourceReader( path, true );
	if ( !reader )
	{
		RED_LOG_ERROR( "Resave: Could not open %hs for reading", path.ToDebugString() );
		return false;
	}

	red::Blob buffer;
	if ( !red::LoadFileToBuffer( *reader, buffer ) )
	{
		RED_LOG_ERROR( "Resave: Error reading %hs to memory", path.ToDebugString() );
		return false;
	}

	serialization::AsyncSourcePtr memorySource = serialization::CreateMemoryAsyncSource( buffer.Release() );
	if ( !memorySource )
	{
		RED_LOG_ERROR( "Resave: Error creating memory async source for %hs", path.ToDebugString() );
		return false;
	}

	const auto extension = red::utils::ExtractExtension( path.ToStringView() );
	auto loader = serialization::ILoader::CreateLoader( extension.AsChar() );
	if ( !loader )
	{
		RED_LOG_ERROR( "Resave: Could not create Loader for %hs with extension %hs", path.ToDebugString(), extension.AsChar() );
		return false;
	}

	serialization::LoadingContext context;
	context.m_skipImports = m_skipLoadImports;
	context.m_skipPostLoad = m_skipPostLoad;
	context.m_postLoadFlags.skipUploadRenderData = true;

	struct ResultPacket
	{
		serialization::LoadingResult result;
		red::ManualResetEvent finished;
	} resultPacket;

	serialization::ILoader::CallbackContext callback{};
	callback.userData = &resultPacket;
	callback.callback = []( const serialization::LoadingResult& result, const res::ResourceTokenWeakHandle& /*unused*/, const serialization::LoadingContext&, job::CompletionDeferral&& /*unused*/, void* userData )
	{
		auto resultPacket = reinterpret_cast< ResultPacket* >( userData );
		resultPacket->result = result;
		resultPacket->finished.SetEvent();
	};
	loader->LoadAsyncWithCallback( memorySource, context, callback, job::CompletionDeferral() );

	resultPacket.finished.Wait();	// Do blocking load

	if ( resultPacket.result.IsFailed() || resultPacket.result.m_loadedRootObjects.Empty() )
	{
		RED_LOG_ERROR( "Resave: Failed to load resource in %hs", path.ToDebugString() );
		return false;
	}

	resource = Cast< CResource >( resultPacket.result.m_loadedRootObjects.Front() );
	if ( !resource )
	{
		RED_LOG_ERROR( "Resave: Failed to cast serializable object to CResource in %hs", path.ToDebugString() );
		return false;
	}	

	return true;
}

// Loads the resource using the ResourceLoader and could potentially be faster
bool ResaveCommandlet::LoadResource_ResourceLoaderMethod( const res::ResourcePath& path, THandle< CResource >& resource )
{
	res::IssueLoadingRequestParameter param{ path };
	param.skipImports = m_skipLoadImports;
	param.skipPostLoad = m_skipPostLoad;
	param.postLoadFlags.skipUploadRenderData = true;

	auto token = GResourceLoader->IssueLoadingRequest( param );
	if ( token->HasFailed() )
	{
		RED_LOG_ERROR( "Resave: Could not load %hs for reading", path.ToDebugString() );
		return false;
	}

	resource = token->WaitUntilLoaded();
	if ( !resource || token->HasFailed() )
	{
		RED_LOG_ERROR( "Resave: Failed to load %hs", path.ToDebugString() );
		return false;
	}

	// Make sure all buffers are read so that the async source isn't needed anymore
	serialization::DeferredDataBufferLoader bufferLoader;
	bufferLoader.Run( resource );

	return true;
}

//------------------------------------------------------------------------------

bool ResaveCommandlet::SaveResource( const res::ResourcePath& path, const THandle< CResource >& resource )
{
	auto writer = m_outputDepot->CreateResourceWriter( path );
	if ( !writer )
	{
		RED_LOG_ERROR( "Resave: Could not open %hs for writing", path.ToDebugString() );
		return false;
	}

	const auto extension = red::utils::ExtractExtension( path.ToStringView() );
	auto saver = serialization::ISaver::CreateSaver( extension.AsChar() );
	if ( !saver )
	{
		RED_LOG_ERROR( "Resave: Could not create saver for file %hs with extension %hs", path.ToDebugString(), extension.AsChar() );
		return false;
	}

	serialization::SavingContext context{ resource };
	context.m_gpuSize = resource->GetGPUSize();
	if ( !saver->SaveObjects( *writer, context ) )
	{
		RED_LOG_ERROR( "Resave: Error saving objects to %hs", path.ToDebugString() );
		return false;
	}

	if ( writer->HasErrors() )
	{
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------

void ResaveCommandlet::ReportSuccess( const res::ResourcePath& path )
{
	fprintf( stdout, "Succeeded: %hs\n", path.ToString().AsChar() );

	if ( !m_outputListingFile.Empty() )
	{
		auto absPath = m_sourceDepot->TranslateResourcePath( path );
		String outputStr = absPath.AsString();
		outputStr += "\r\n";

		red::AppendStringToFile( m_outputListingFile, outputStr );
	}
}

void ResaveCommandlet::ReportFailure( const res::ResourcePath& path )
{
	m_failedResource.Increment();

	fprintf( stdout, "Failed: %hs\n", path.ToString().AsChar() );
}

Bool ResaveCommandlet::CheckResource( const res::ResourcePath& path )
{
	if( !IsResource( path ) || IsTextResource( path ) )
	{
		RED_LOG_WARNING( "Resave: Resource %hs cannot be resaved", path.ToDebugString() );
		return false;
	}

	const bool needsResave = NeedsResave( path );
	const bool needsRelink = m_relink && NeedsRelink( path );
	if( !(m_force || needsResave || needsRelink) )
	{
		RED_LOG_TRACE( "Resave: Resource %hs does not need resaving", path.ToDebugString() );
		return false;
	}

	if( m_dryrun )
	{
		ReportSuccess( path );
		return false;
	}

	if( needsResave )
	{
		RED_LOG_TRACE( "Resave: Resource %hs needs resaving", path.ToDebugString() );
	}
	else if( needsRelink )
	{
		RED_LOG_TRACE( "Resave: Resource %hs needs relinking", path.ToDebugString() );
	}

	return true;
}

bool ResaveCommandlet::ProcessResource( const res::ResourcePath& path )
{
	THandle< CResource > resource;

	if( !LoadResource_ResourceLoaderMethod( path, resource ) )
	{
		return false;
	}

	if( !OnResourcePreSave( *resource ) )
	{
		return false;
	}

	if( !SaveResource( path, resource ) )
	{
		return false;
	}

	return true;
}

Bool ResaveCommandlet::SpawnLoadingJob( const res::ResourcePath& path )
{
	res::IssueLoadingRequestParameter param{ path };
	param.skipImports = m_skipLoadImports;
	param.skipPostLoad = m_skipPostLoad;
	param.postLoadFlags.skipUploadRenderData = true;

	// Try to load it
	auto token = GResourceLoader->IssueLoadingRequest( param );
	if( token->HasFailed() )
	{
		RED_LOG_ERROR( "Resave: Could not load %hs for reading", path.ToDebugString() );
		return false;
	}

	// After it loaded, starts processing
	job::DispatchJob<red::PoolBackend>( "ProcessResource", token->GetWaitCounter(), m_accumCounter, [path, token, this]( const job::RunContext& runCtx )
	{
		Bool success = false;

		if( auto resource = token->GetResource() )
		{
			m_currentResource.Increment();
			RED_LOG( "Resave: Processing resource %hs %u/%u", path.ToDebugString(), m_currentResource.GetValue(), m_resources.Size() );

			// Make sure all buffers are read so that the async source isn't needed anymore
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( resource );

			if( OnResourcePreSave( *resource ) )
			{
				success = SaveResource( path, resource );
			}
		}

		if( success )
		{
			ReportSuccess( path );
		}
		else
		{
			ReportFailure( path );
		}

		// Pick up new resource to process
		PopResourceToLoad();

	} );

	// Job was spawned, we are good
	return true;
}

Bool ResaveCommandlet::PopResourceToLoad( )
{
	do 
	{
		// Tries to get next resource path from the list
		Uint32 idx = m_popResource.PostIncrement();

		// Exit loop, when reached end of the queue
		if( idx >= m_resources.Size() )
			return false;

		auto path = m_resources[idx];

		if( CheckResource( path ) )
		{
			// All good so far. Try to spawn job for it...
			if( SpawnLoadingJob( path ) )
				break;
		}

		// Naah.. keep going with next one
	} while ( 1 ) ;

	return true;
}

bool ResaveCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseOptions( options ) )
	{
		return false;
	}

	if ( !m_outputListingFile.Empty() && !GFileManager->FileExist( m_outputListingFile ) )
	{
		red::SaveStringToFile( m_outputListingFile, "" );
	}

	commandlet::FileLinkMap linkMap;
	commandlet::FileLinkResolver linkResolver{ linkMap };

	if ( m_relink )
	{
		m_linkMap = &linkMap;
		m_linkMap->Initialise( *m_sourceDepot );
		m_linkResolver = &linkResolver;
		serialization::StructureMapper::SetPathResolver( m_linkResolver );
		red::PackageCompiler::Internal_SetPathResolver( m_linkResolver );
	}

	if( m_dryrun || !m_multithreaded )
	{
		for( const auto& path : m_resources )
		{
			m_currentResource.Increment();
			RED_LOG( "Resave: Processing resource %hs %u/%u", path.ToDebugString(), m_currentResource.GetValue(), m_resources.Size() );

			if( CheckResource( path ) )
			{
				if( ProcessResource( path ) )
				{
					ReportSuccess( path );
				}
				else
				{
					ReportFailure( path );
				}
			}
		}
	}
	else if( m_multithreaded )
	{
		// To throttle number of in-flight processing resources, we limit first batch of spawned jobs to some number. 
		// More jobs will be spawned after finishing processing more and more new resources
		const Uint32 MAX_NUM_CONCURRENT_RESOURCES = job::GetNumDispatcherThreads();

		for( Uint32 i = 0; i < MAX_NUM_CONCURRENT_RESOURCES; ++i )
		{
			// Pick up new resource to process
			if( !PopResourceToLoad() )
				break;
		}

		// Wait until it finishes all
		job::FlushCounter( m_accumCounter );
	}

	if ( m_relink )
	{
		red::PackageCompiler::Internal_SetPathResolver( nullptr );
		serialization::StructureMapper::SetPathResolver( nullptr );
		m_linkResolver = nullptr;
		m_linkMap = nullptr;
	}

	if ( m_strict && m_failedResource.GetValue() > 0 )
	{
		return false;
	}

	return true;
}

Bool ResaveCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void ResaveCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

bool ResaveCommandlet::OnResourcePreSave( CResource& resource )
{
	return true;
}

bool ResaveCommandlet::IgnoreFile( const red::AbsolutePath & path )
{
	// TODO: Move knowledge of resource extensions and if they are built
	// somewhere else and reference them from here

	red::StringView extension = red::paths::GetExtension( path );
	Uint32 extHash = red::CalculateHash32( extension.Data() );
	switch ( extHash )
	{
	case red::CalculateHash32( "streamingworld" ):
	case red::CalculateHash32( "streamingarea" ):
	case red::CalculateHash32( "streamingsector" ):
	case red::CalculateHash32( "mappins" ):
	case red::CalculateHash32( "areas" ):
	case red::CalculateHash32( "devices" ):
	case red::CalculateHash32( "psrep" ):
		return true;
	};
	return false;
}
