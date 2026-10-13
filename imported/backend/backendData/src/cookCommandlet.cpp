/*
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/redReflection/include/serializationBinaryStructureMapper.h"
#include "../../../common/redReflection/include/packageCompiler.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"

#include "../../backendData/include/fileLinkMap.h"
#include "../../backendData/include/linkResolver.h"
#include "../../backendData/include/resourceDatabase.h"

#include "dataSystemInstance.h"
#include "dataTask.h"
#include "dataPlatform.h"
#include "dataTaskSystem.h"
#include "commandletTaskReporter.h"
#include "dataCommandletSettings.h"
#include "dependencyCollector.h"
#include "cookerTask.h"
#include "dependencyUtils.h"
#include "dataGlobalCacheReference.h"

class CookCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CookCommandlet );

public:
	CookCommandlet();
	~CookCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );

	red::DynArray< res::ResourcePath > m_resources;

	commandlet::EngineInstance m_engineInstance;
};

RTTI_BEGIN_TYPE( CookCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

CookCommandlet::CookCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "cook" ) )
{
}

CookCommandlet::~CookCommandlet()
{
}

const char* CookCommandlet::GetOneLiner() const
{
	return "Generate cooked data";
}

void CookCommandlet::PrintHelp() const
{
}

bool CookCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void CookCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

//------------------------------------------------------------------------------

struct CookCommandlet::Settings : public data::CommandletSettings
{
	bool relink;

	Settings()
		: relink( true )
	{
	}
};

bool CookCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	if ( !settings.Parse( commandline, data::REQUIRES_VALID_OUTPUT | data::DEFAULT_PLATFORM_PC ) )
	{
		return false;
	}

	if ( commandlet::HasResourcePathsCommandLineOption( commandline ) )
	{
		m_resources = commandlet::ParseResourcePathsFromCommandLine( *commandlet::GetEditorDepot(), commandline, true );
	}
	else if ( commandline.HasOption( "glob" ) )
	{
		m_resources = commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "" );
	}
	if ( m_resources.Empty() )
	{
		RED_LOG_ERROR( "No resources specified, please use -all, -dir=<path>, -file=<path>, or -glob=<pattern>" );
		return false;
	}

	if ( commandline.HasOption( "glob" ) )
	{
		red::DynArray< String > patterns = commandline.GetValues( "glob" );

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

	Uint32 resourcesLimit = 0;
	if ( commandline.GetFirstParam( "limit", resourcesLimit ) && resourcesLimit != 0 )
	{
		if ( m_resources.Size() > resourcesLimit )
		{
			RED_LOG( "Resource limit breached, reducing from %u to %u resources to process", m_resources.Size(), resourcesLimit );
			m_resources.Remove( m_resources.Begin() + resourcesLimit, m_resources.End() );
		}
	}

	if ( commandline.HasOption( "norelink" ) )
	{
		settings.relink = false;
	}

	return true;
}


//------------------------------------------------------------------------------

bool CookCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	red::InteractiveCommandletCommunicationHandler communicationHandler{ options };
	bool isRunningInteractively = communicationHandler.IsRunning();

	commandlet::CommandletTaskReporter reporter{ communicationHandler };
	reporter.OpenOutputDevice( isRunningInteractively );

	data::SystemInstance instance;
	instance.InitSourceDepot();
	instance.InitCookedDepot( settings.outputDepotPath, settings.outputCachePath );
	instance.SetTargetPlatform( settings.platform );
	instance.SetOption_Forced( settings.forced );
	instance.SetOption_CollectDebugInfo( settings.dumpDependencies );
	instance.WaitUntilInitialised();

	data::GlobalCacheManager globalCaches;
	globalCaches.InitAllGlobalCaches();

	data::SystemContext& systemContext = instance.CreateContext();
	data::TaskSystem taskSystem{ systemContext, &cooker::CreateTask, reporter, settings.maxConcurrentTasks };

	auto& sourceDepot = systemContext.SourceDepot();
	commandlet::FileLinkMap linkMap;
	auto allResourcePaths = systemContext.SourceResourceDatabase().GetResourcePaths(); // Note: Copy
	linkMap.Initialise( sourceDepot, allResourcePaths );

	commandlet::FileLinkResolver linkResolver{ linkMap };
	if ( settings.relink )
	{
		serialization::StructureMapper::SetPathResolver( &linkResolver );
		red::PackageCompiler::Internal_SetPathResolver( &linkResolver );
	}

	taskSystem.Build( m_resources );

	serialization::StructureMapper::SetPathResolver( nullptr );
	red::PackageCompiler::Internal_SetPathResolver( nullptr );

	if ( settings.dumpDependencies )
	{
		red::StopWatch timer;

		const auto debugDumpPath = instance.DefaultDumpDebugDependenciesPath();
		if ( ! dependency::prv::DumpDebugInfo( taskSystem, debugDumpPath ) )
		{
			RED_LOG_ERROR( "Failed to output all debug dependency information" );
		}

		RED_LOG( "Dumping debug dependencies took %0.3lf ms", timer.GetDeltaMS() );
	}

	if ( settings.dumpMetrics )
	{
		red::StopWatch timer;

		auto file = GFileManager->CreateFileWriter( settings.dumpMetricsPath, FOF_AbsolutePath );
		if ( file )
		{
			data::metrics::Writer writer( std::move( file ) );

			// Dump Data Task metrics
			taskSystem.Metrics_DumpInfo( writer );
			taskSystem.Metrics_DumpTasks( writer );
		}

		RED_LOG( "Dumping metrics took %0.3lf ms", timer.GetDeltaMS() );
	}

	globalCaches.ShutAllGlobalCaches();
	instance.Shutdown();

	return true;
}
