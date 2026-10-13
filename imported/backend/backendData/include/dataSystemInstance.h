/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataSystemContext.h"

namespace data
{

// Builder and owner class that can create contexts for the cooker and generator systems.
// This is used in commandlets to reduce the amount of shared code needed to configure these
// systems and it allows a simple way to use them in the same commandlet (e.g. the assemble-data commandlet)
class BACKEND_DATA_API SystemInstance : private SystemContext
{
public:
	SystemInstance();
	~SystemInstance();

	SystemInstance( SystemInstance&& );
	SystemInstance( const SystemInstance& ) = delete;
	SystemInstance& operator = ( const SystemInstance& ) = delete;

	// Initialises the source depot and related classes using the built in editor systems
	void InitSourceDepot();

	// Initialises the source depot and related classes using another already inititalised instance
	void InitSourceDepotFromInstance( const SystemInstance& other );

	// Initialises the cooked depot and dependency database based on the given paths
	void InitCookedDepot( const red::AbsolutePath& depotPath, const red::AbsolutePath& cachePath );

	// Initialises the cooked depot as an alias of the source depot
	// NOTE: Use this only when running generators, not when cooking data!
	void InitSourceAsCookedDepot();

	// Initialises the data cache and marks this as a client instance
	void InitDataCache( const red::String& cacheSettings );

	// Shuts down the systems, saving caches and databases
	void Shutdown();
	void Shutdown( job::Counter& counter );

	// Sets the target platform enumeration.
	void SetTargetPlatform( data::TargetPlatform platform );

	// Initialises flags and features for determining if its running on the server or client
	void SetServerConfiguration( bool isServer, bool isWorker, const red::AbsolutePath& taskListPath );

	// Sets the force flag
	void SetOption_Forced( bool forced );

	// Sets the collect debug info flag
	void SetOption_CollectDebugInfo( bool collectDebugInfo );

	// Helper function to wait until all initialisation has completed
	// Equivalent to job::FlushCounter( GetInitCounter() )
	void WaitUntilInitialised() const;

	// Returns the counter that signals all the initialisation jobs have completed
	const job::Counter& GetInitCounter() const;

	// Finalise initialisation and create a system context to be used in doing data processing
	data::SystemContext& CreateContext();

	// Returns the base path into which the debug dependencencies should be dumped to
	red::AbsolutePath DefaultDumpDebugDependenciesPath() const;

	// Returns the default system dependency path. This should only be used in commandlets
	// which support the cooking, generating, assembling loop of going to a standard place
	static red::AbsolutePath DefaultDepsDatabasePath();

	// Save the ResourcePathCache to disk in the specified directory
	static void SaveResourcePathCacheFile( const red::AbsolutePath& basePath );

	const red::AbsolutePath& GetSourceCachePath() const;
	const red::AbsolutePath& GetCookedCachePath() const;

private:
	red::UniquePtr< data::ErrorHandler > m_ownedErrorHandler;

	red::UniquePtr< dependency::Database > m_ownedDepsDatabase;
	red::UniquePtr< dependency::Cache > m_ownedSourceDepsCache;
	red::UniquePtr< dependency::Cache > m_ownedCookedDepsCache;

	red::UniquePtr< data::ResourceDepot > m_ownedCookedDepot;
	red::UniquePtr< data::ResourceLoader > m_ownedCookedLoader;

	red::UniquePtr< resdb::Database > m_ownedSourceDepotDatabase;

#if !defined( VANGUARD_ASSET_HOST )
		red::UniquePtr< data::Cache > m_ownedDataCache;

		red::UniquePtr< data::WorkerTaskAccumulator > m_ownedWorkerTaskAccumulator;

		red::AbsolutePath m_outputWorkerTaskListPath;
#endif

	red::AbsolutePath m_sourceCachePath;
	red::AbsolutePath m_cookedCachePath;
	red::AbsolutePath m_depsDatabasePath;
	red::AbsolutePath m_sourceDepsCachePath;
	red::AbsolutePath m_cookedDepsCachePath;

	job::Counter m_counter;
};

} // data
