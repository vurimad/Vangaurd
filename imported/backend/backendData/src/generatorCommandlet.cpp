/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
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

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"
#include "../../backendEngine/include/streamingManagerInstance.h"
#include "../../backendEngine/include/interactiveCommandletCommunicationHandler.h"

#include "dataSystemInstance.h"
#include "dataTask.h"
#include "dataPlatform.h"
#include "dataTaskSystem.h"
#include "commandletTaskReporter.h"
#include "dataCommandletSettings.h"
#include "dependencyCollector.h"
#include "generatorTask.h"
#include "dependencyUtils.h"
#include "dataGlobalCacheReference.h"

class GeneratorCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( GeneratorCommandlet );

public:
	GeneratorCommandlet();
	~GeneratorCommandlet();

	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );

	red::DynArray< res::ResourcePath > m_resources;

	commandlet::EngineInstance m_engineInstance;
	res::StreamingManagerInstance m_streamingInstance;
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE( GeneratorCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

struct GeneratorCommandlet::Settings : public data::CommandletSettings
{
	red::AbsolutePath exportCacheDepsPath;
};

//------------------------------------------------------------------------------

GeneratorCommandlet::GeneratorCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "generate" ) )
{
}

GeneratorCommandlet::~GeneratorCommandlet()
{
}

const char* GeneratorCommandlet::GetOneLiner() const
{
	return "Ensure that a generated resource is up to date";
}

void GeneratorCommandlet::PrintHelp() const
{
}

Bool GeneratorCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && m_streamingInstance.Initialize();
}

void GeneratorCommandlet::TeardownEnvironment()
{
	m_renderInstance.Shutdown(); // Unconditionally shut down rendering instance
	m_streamingInstance.Shutdown();
	m_engineInstance.Shutdown();
}

//------------------------------------------------------------------------------

bool GeneratorCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	if ( !settings.Parse( commandline, data::PLATFORM_NOT_REQUIRED ) )
	{
		return false;
	}

	if ( commandline.HasOption( "file" ) )
	{
		auto files = commandline.GetValues( "file" );
		for ( const auto& file : files )
		{
			m_resources.PushBack( res::ResourcePath::Build( file ) );
		}
	}
	else if ( commandline.HasOption( "filelist" ) )
	{
		auto listingFiles = commandline.GetValues( "filelist" );
		for ( const auto& listingFile : listingFiles )
		{
			auto listingFilePath = red::AbsolutePath::ParseFilePath( listingFile );
			if ( !listingFilePath.Empty() )
			{
				m_resources.PushBack( commandlet::ReadResourcePathsFromListingFile( listingFilePath ) );
			}
		}
	}

	if ( m_resources.Empty() )
	{
		return false;
	}

	String param;
	if ( commandline.GetFirstOption( "exportDeps", param ) )
	{
		settings.exportCacheDepsPath = red::AbsolutePath::ParseDirPath( param );
	}

	if ( commandline.HasOption( "enableRender" ) )
	{
		m_renderInstance.Initialise();
	}

	return true;
}

//------------------------------------------------------------------------------

Bool GeneratorCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	red::InteractiveCommandletCommunicationHandler communicationHandler{ options };
	bool isRunningInteractively = communicationHandler.IsRunning();

	red::Stages stages = communicationHandler.DeclareStages( { "Initialize Data Task System", "Generate data", "Save build information" } );

	commandlet::CommandletTaskReporter reporter{ communicationHandler };
	reporter.OpenOutputDevice( isRunningInteractively );
	reporter.UpdateSystemStatus( "Initializing Data Task System" );

	data::SystemInstance instance;
	instance.InitSourceDepot();
	if ( settings.outputDepotPath.Empty() )
	{
		instance.InitSourceAsCookedDepot();
	}
	else
	{
		instance.InitCookedDepot( settings.outputDepotPath, settings.outputCachePath );
	}
	instance.InitDataCache( settings.dataCacheConfig );
	instance.SetTargetPlatform( settings.platform );
	instance.SetServerConfiguration( settings.serverBuild, settings.serverWorker, settings.workerTaskListPath );
	instance.SetOption_Forced( settings.forced );
	instance.SetOption_CollectDebugInfo( settings.dumpDependencies );
	instance.WaitUntilInitialised();

	data::GlobalCacheManager globalCaches;
	globalCaches.InitAllGlobalCaches();

	stages.FinishCurrentStage();

	data::SystemContext& systemContext = instance.CreateContext();
	data::TaskSystem taskSystem{ systemContext, &generator::CreateTask, reporter, settings.maxConcurrentTasks };

	taskSystem.Build( m_resources );

	stages.FinishCurrentStage();

	if ( !settings.exportCacheDepsPath.Empty() )
	{
		red::StopWatch timer;

		reporter.UpdateSystemStatus( "Exporting json dependencies" );

		red::DynArray< res::ResourcePath > cacheUsingResources = taskSystem.GetCacheUsingResources();
		GFileManager->CreatePath( settings.exportCacheDepsPath );
		dependency::prv::ExportDependenciesToJson( systemContext.DependencyDatabase(), settings.exportCacheDepsPath, cacheUsingResources );

		RED_LOG( "Exporting json dependencies for server cache took %0.3lf ms", timer.GetDeltaMS() );
	}

	if ( settings.dumpDependencies )
	{
		red::StopWatch timer;

		reporter.UpdateSystemStatus( "Dumping Debug Dependency information" );

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

		reporter.UpdateSystemStatus( "Dumping metrics" );

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

	reporter.UpdateSystemStatus( "Saving build information" );

	globalCaches.ShutAllGlobalCaches();
	instance.Shutdown();

	stages.FinishCurrentStage();

	reporter.UpdateSystemStatus( "Finished!" );
	reporter.CloseOutputDevice();

	return true;
}
