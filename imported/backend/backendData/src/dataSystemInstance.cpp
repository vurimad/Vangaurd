/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataTypesImpl.h"
#include "dataSystemInstance.h"

#include "dependencyDatabase.h"
#include "dependencyUtils.h"
#include "dependencyCache.h"
#include "dataErrorHandlerImpl.h"
#include "dataGlobalCacheReference.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "dataCache.h"
#include "dataWorkerTaskAccumulator.h"
#endif

#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/directoryListingCache.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourcePathCache.h"

#include "../../../common/redJobs2/include/jobRunner.h"

namespace data
{

SystemInstance::SystemInstance()
	: SystemContext()
{
	m_ownedErrorHandler = red::CreateUniquePtr< data::GlobalErrorHandler >();
	m_errorHandler = m_ownedErrorHandler.Get();
}

SystemInstance::~SystemInstance()
{
}

SystemInstance::SystemInstance( SystemInstance&& ) = default;

void SystemInstance::InitSourceDepot()
{
	// Initialise default source depot and loader
	m_sourceDepot = res::GetEditorDepot();
	RED_FATAL_ASSERT( m_sourceDepot != nullptr );

	m_sourceLoader = GResourceLoader;
	RED_FATAL_ASSERT( m_sourceLoader != nullptr );

	m_sourceCachePath = GFileManager->GetCacheDirectory();

	// Initialise resource database for source depot which will speed up dependency lookups
	m_ownedSourceDepotDatabase = red::CreateUniquePtr< resdb::Database >();
	m_sourceDepotDatabase = m_ownedSourceDepotDatabase.Get();

	const red::AbsolutePath sourceDatabasePath = m_sourceCachePath.AddFilePath( resdb::Database::GetDefaultFilename() );

	job::Counter initDatabaseCounter;
	m_ownedSourceDepotDatabase->Initialise( *m_sourceDepot, sourceDatabasePath, initDatabaseCounter );

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/OnDatabaseInitialised", initDatabaseCounter, m_counter, [this]( const job::RunContext& )
	{
		m_ownedSourceDepotDatabase->InitFileSystemWatcher();
	} );

	// Initialise dependency cache for source depot
	m_ownedSourceDepsCache = red::CreateUniquePtr< dependency::Cache >( m_sourceDepot );
	m_sourceDepsCache = m_ownedSourceDepsCache.Get();

	m_sourceDepsCachePath = m_sourceCachePath.AddFilePath( dependency::Cache::GetDefaultFilename() );

	job::Counter noWait;
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/InitSourceDepot", noWait, m_counter, [this]( const job::RunContext& )
	{
		if ( !m_sourceDepsCache->Load( m_sourceDepsCachePath ) )
		{
			RED_LOG_WARNING( "Could not load the source dependency file hash cache '%hs', hashes will be recalculated!", m_sourceDepsCachePath.AsChar() );
		}
	} );

}

void SystemInstance::InitSourceDepotFromInstance( const SystemInstance& other )
{
	m_sourceCachePath = other.m_sourceCachePath;

	m_sourceDepot = other.m_sourceDepot;
	RED_FATAL_ASSERT( m_sourceDepot != nullptr );

	m_sourceLoader = other.m_sourceLoader;
	RED_FATAL_ASSERT( m_sourceLoader != nullptr );

	m_sourceDepotDatabase = other.m_sourceDepotDatabase;
	RED_FATAL_ASSERT( m_sourceDepotDatabase != nullptr );
	
	m_sourceDepsCache = other.m_sourceDepsCache;
	RED_FATAL_ASSERT( m_sourceDepsCache != nullptr );
}

void SystemInstance::InitCookedDepot( const red::AbsolutePath& depotPath, const red::AbsolutePath& cachePath )
{
	// Create cooked depot and loader
	GFileManager->CreatePath( depotPath ); // Make sure depot path exists so we don't assert

	m_cookedCachePath = cachePath;

	auto cookedDepot = red::CreateUniquePtr< res::prv::SingleTreeResourceDepot >( depotPath );
	cookedDepot->SetDebugName( "Cooked" );

	job::Counter noWait;

	auto directoryListingCachePath = m_cookedCachePath.AddFilePath( res::prv::DirectoryListingCache::GetDefaultFilename() );
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/InitCookedDepot/DirectoryListingCache", noWait, m_counter, [fileSystemDepot = cookedDepot.Get(), directoryListingCachePath]( const job::RunContext& )
	{
		fileSystemDepot->LoadDirectoryListingCache( directoryListingCachePath );
	} );

	m_ownedCookedDepot = std::move( cookedDepot );
	m_cookedDepot = m_ownedCookedDepot.Get();
	RED_FATAL_ASSERT( m_cookedDepot != nullptr );

	m_ownedCookedLoader = res::CreateResourceLoader( m_cookedDepot, true );
	m_cookedLoader = m_ownedCookedLoader.Get();
	RED_FATAL_ASSERT( m_cookedLoader != nullptr );

	// Initialise dependency database
	m_depsDatabasePath = dependency::Database::ComputeDefaultDatabasePath( m_cookedCachePath );
	m_ownedDepsDatabase = dependency::Database::Create( m_depsDatabasePath, 0 );
	m_depsDatabase = m_ownedDepsDatabase.Get();

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/InitCookedDepot/Dependencies", noWait, m_counter, [this]( const job::RunContext& )
	{
		if ( !m_depsDatabase->Load() )
		{
			RED_LOG_ERROR( "Failed to load the dependency database from '%hs'", m_depsDatabasePath.AsChar() );
			m_depsDatabase->Clear();
		}
	} );

	// Initialise dependency cache for cooked depot
	m_ownedCookedDepsCache = red::CreateUniquePtr< dependency::Cache >( m_cookedDepot );
	m_cookedDepsCache = m_ownedCookedDepsCache.Get();

	m_cookedDepsCachePath = m_cookedCachePath.AddFilePath( dependency::Cache::GetDefaultFilename() );
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/InitCookedDepot/DepsCache", noWait, m_counter, [this]( const job::RunContext& )
	{
		if ( !m_cookedDepsCache->Load( m_cookedDepsCachePath ) )
		{
			RED_LOG_WARNING( "Could not load the cooked dependency file hash cache '%hs', hashes will be recalculated!", m_cookedDepsCachePath.AsChar() );
		}
	} );
}

void SystemInstance::InitSourceAsCookedDepot()
{
	// Initialise default source depot and loader as cooked depot versions
	m_cookedCachePath = m_sourceCachePath;

	m_cookedDepot = res::GetEditorDepot();
	RED_FATAL_ASSERT( m_cookedDepot != nullptr );

	m_cookedLoader = GResourceLoader;
	RED_FATAL_ASSERT( m_cookedLoader != nullptr );

	m_depsDatabasePath = DefaultDepsDatabasePath();
	m_ownedDepsDatabase = dependency::Database::Create( m_depsDatabasePath, 0 );
	m_depsDatabase = m_ownedDepsDatabase.Get();

	job::Counter noWait;
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/InitSourceAsCookedDepot/Dependencies", noWait, m_counter, [this]( const job::RunContext& )
	{
		if ( !m_depsDatabase->Load() )
		{
			RED_LOG_ERROR( "Failed to load the dependency database from '%hs'", m_depsDatabasePath.AsChar() );
			m_depsDatabase->Clear();
		}
	} );

	m_cookedDepsCache = m_ownedSourceDepsCache.Get();
	m_cookedDepsCachePath = m_sourceDepsCachePath;
}

void SystemInstance::InitDataCache( const red::String& cacheSettings )
{
#if defined( VANGUARD_ASSET_HOST )
	if ( !cacheSettings.Empty() )
	{
		RED_LOG_ERROR( "SystemInstance: data-cache connections are not available in VanguardAssetHost" );
	}
#else
	if ( cacheSettings.Empty() )
	{
		RED_LOG( "DataCache: No settings provided, skipping!" );
		return;
	}

	auto onConnected = [this, cacheSettings]( red::UniquePtr< data::Cache >&& dataCache )
	{
		m_ownedDataCache = std::move( dataCache );
		if ( m_ownedDataCache )
		{
			m_dataCache = m_ownedDataCache.Get();
			RED_LOG( "DataCache: Connected to data cache %hs", cacheSettings.AsChar() );
		}
		else
		{
			RED_LOG_ERROR( "DataCache: Failed to connect to the data cache" );
		}
	};

	data::Cache::Connect( cacheSettings, m_counter, red::Function< data::Cache::ConnectCallback >( red::PoolBackend(), onConnected ) );
#endif
}

void SystemInstance::Shutdown()
{
	job::Counter counter;
	Shutdown( counter );
	job::FlushCounter( counter );
}

void SystemInstance::Shutdown( job::Counter& counter )
{
	job::Counter noWait;

	if ( m_ownedSourceDepotDatabase )
	{
		m_ownedSourceDepotDatabase->ShutFileSystemWatcher();
	}

	m_dataCache = nullptr;
#if !defined( VANGUARD_ASSET_HOST )
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/DataCache", noWait, counter, [this]( const job::RunContext& )
	{
		if ( m_ownedDataCache )
		{
			m_ownedDataCache->Shutdown();
			m_ownedDataCache = nullptr;
		}
	} );
#endif

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/SourceDepotDatabase", noWait, counter, [this]( const job::RunContext& )
	{
		m_sourceDepotDatabase = nullptr;

		if ( m_ownedSourceDepotDatabase )
		{
			m_ownedSourceDepotDatabase->Shutdown();
			m_ownedSourceDepotDatabase = nullptr;
		}
	} );

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/DependencyDatabase", noWait, counter, [this]( const job::RunContext& )
	{
		m_depsDatabase = nullptr;

		if ( m_ownedDepsDatabase )
		{
			if ( m_ownedDepsDatabase->HasChanged() )
			{
				if ( !m_ownedDepsDatabase->Save() )
				{
					RED_LOG_ERROR( "Failed to save the dependency database to '%hs'", m_depsDatabasePath.AsChar() );
				}
			}

			m_ownedDepsDatabase = nullptr;
		}
	} );

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/SourceDepsCache", noWait, counter, [this]( const job::RunContext& )
	{
		m_sourceDepsCache = nullptr;

		if ( m_ownedSourceDepsCache )
		{
			if ( !m_sourceDepsCachePath.Empty() )
			{
				if ( !m_ownedSourceDepsCache->Save( m_sourceDepsCachePath ) )
				{
					RED_LOG_WARNING( "Could not save the source dependency file hash cache '%hs'", m_sourceDepsCachePath.AsChar() );
				}
			}

			m_ownedSourceDepsCache = nullptr;
		}
	} );

	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/CookedDepsCache", noWait, counter, [this]( const job::RunContext& )
	{
		m_cookedDepsCache = nullptr;

		if ( m_ownedCookedDepsCache )
		{
			if ( !m_cookedDepsCachePath.Empty() && m_cookedDepsCachePath != m_sourceDepsCachePath )
			{
				if ( !m_ownedCookedDepsCache->Save( m_cookedDepsCachePath ) )
				{
					RED_LOG_WARNING( "Could not save the cooked dependency file hash cache '%hs'", m_cookedDepsCachePath.AsChar() );
				}
			}
		}
	} );

	m_workerTaskAccumulator = nullptr;
#if !defined( VANGUARD_ASSET_HOST )
	job::DispatchJob<red::PoolBackend>( "DataSystemInstance/Shutdown/WorkerTaskAccumulator", noWait, counter, [this]( const job::RunContext& )
	{
		if ( m_isServer && !m_isWorker && m_ownedWorkerTaskAccumulator )
		{
			if ( !m_ownedWorkerTaskAccumulator->SaveList( m_outputWorkerTaskListPath ) )
			{
				RED_LOG_ERROR( "Failed to save worker task list to '%hs'", m_outputWorkerTaskListPath.AsChar() );
			}
		}
	} );
#endif
}

void SystemInstance::SetTargetPlatform( data::TargetPlatform platform )
{
	m_targetPlatform = platform;
}

void SystemInstance::SetOption_Forced( bool forced )
{
	m_force = forced;
}

void SystemInstance::SetOption_CollectDebugInfo( bool collectDebugInfo )
{
	m_collectDebugInfo = collectDebugInfo;
}

void SystemInstance::SetServerConfiguration( bool isServer, bool isWorker, const red::AbsolutePath& taskListPath )
{
#if defined( VANGUARD_ASSET_HOST )
	if ( isServer || isWorker || !taskListPath.Empty() )
	{
		RED_LOG_ERROR( "SystemInstance: server/worker configuration is not available in VanguardAssetHost" );
	}
	m_isServer = false;
	m_isWorker = false;
	m_workerTaskAccumulator = nullptr;
#else
	m_isServer = isServer;
	m_isWorker = isWorker;
	RED_FATAL_ASSERT( m_isWorker ? m_isServer : true, "If worker is set then server must also be set!" );

	if ( m_isServer && !m_isWorker )
	{
		if ( !taskListPath.IsFilePath() )
		{
			RED_LOG_ERROR( "Invalid path '%hs' specified for output task list, must be a file path", taskListPath.AsChar() );
			return;
		}

		m_outputWorkerTaskListPath = taskListPath;
		m_ownedWorkerTaskAccumulator = red::CreateUniquePtr< data::WorkerTaskAccumulator >();
		m_workerTaskAccumulator = m_ownedWorkerTaskAccumulator.Get();
	}
	else
	{
		if ( !taskListPath.Empty() )
		{
			if ( !m_isServer )
			{
				RED_LOG_ERROR( "Cannot specify a worker task list when running as a client" );
			}
			else if ( m_isWorker )
			{
				RED_LOG_ERROR( "Cannot specify a worker task list when running as a worker" );
			}
		}
	}
#endif
}

void SystemInstance::WaitUntilInitialised() const
{
	job::FlushCounter( m_counter );
}

const job::Counter& SystemInstance::GetInitCounter() const
{
	return m_counter;
}

data::SystemContext& SystemInstance::CreateContext()
{
	RED_FATAL_ASSERT( m_depsDatabase != nullptr );
	RED_FATAL_ASSERT( m_sourceDepsCache != nullptr );
	RED_FATAL_ASSERT( m_sourceDepot != nullptr );
	RED_FATAL_ASSERT( m_sourceLoader != nullptr );
	RED_FATAL_ASSERT( m_cookedDepot != nullptr );
	RED_FATAL_ASSERT( m_cookedLoader != nullptr );
	RED_FATAL_ASSERT( m_cookedDepsCache != nullptr );

	return *this;
}

//------------------------------------------------------------------------------

void SystemInstance::SaveResourcePathCacheFile( const red::AbsolutePath& basePath )
{
	const auto& resourcePathCache = res::prv::GetResourcePathCache();

	const red::AbsolutePath cachePath = basePath.AddFilePath( resourcePathCache.GetDefaultFilename() );
	if ( !resourcePathCache.Save( cachePath ) )
	{
		RED_LOG_ERROR( "SystemInstance: Failed to save Resource Path Cache to '%hs'", cachePath.AsChar() );
	}
}

const red::AbsolutePath& SystemInstance::GetSourceCachePath() const
{
	return m_sourceCachePath;
}

const red::AbsolutePath& SystemInstance::GetCookedCachePath() const
{
	return m_cookedCachePath;
}

//------------------------------------------------------------------------------

red::AbsolutePath SystemInstance::DefaultDumpDebugDependenciesPath() const
{
	red::AbsolutePath dumpPath;
	if ( m_depsDatabasePath.AsStringView().EndsWith( ".binary_deps" ) )
	{
		dumpPath = red::paths::ParentAbsolutePath( m_depsDatabasePath );
	}
	else
	{
		dumpPath = m_depsDatabasePath;
	}
	dumpPath.AppendDirPath( "_debug" );
	return dumpPath;
}

red::AbsolutePath SystemInstance::DefaultDepsDatabasePath()
{
	return dependency::Database::ComputeDefaultDatabasePath( GFileManager->GetCacheDirectory() );
}

//------------------------------------------------------------------------------

} // data
