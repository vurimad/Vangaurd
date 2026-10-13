/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataTask.h"

#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/engine/include/engineWatchdog.h"
#endif
#include "../../../common/redSystem/include/stopWatch.h"

#include "dependencyDatabase.h"
#include "dependencyCache.h"
#include "dataCache.h"
#include "dataErrorHandler.h"
#include "dataTypesImpl.h"
#include "dataTaskLog.h"

#include <numeric>

namespace data
{

Task::Task( const res::ResourcePath& resourcePath, SystemContext& system )
	: m_system( system )
	, m_path( resourcePath )
	, m_taskContext( m_system, m_path, m_outputCollector )
	, m_buildCounter()
	, m_buildDeferral( m_buildCounter.CreateDeferral() )
	, m_storedDeps( nullptr )
	, m_previousDeps( nullptr )
	, m_state( State::INITIAL )
	, m_cacheStrategy( CacheStrategy::GET_NEVER )
	, m_buildSettings( 0u )
	, m_isolationGroup( 0u )
	, m_buildReason( 0u )
{
}

Task::~Task()
{
}

//------------------------------------------------------------------------------

bool Task::IsResourceUpToDate( const dependency::ResourceInfo& prevInfo, const dependency::ResourceInfo& curInfo ) const
{
	const bool sameResource = prevInfo.GetPath() == curInfo.GetPath();
	const bool exists = curInfo.GetTimeStamp() != 0ull;
	const bool sameTimestamp = prevInfo.GetTimeStamp() == curInfo.GetTimeStamp();
	const bool sameHash = prevInfo.GetHash() == curInfo.GetHash();

	// Order from most likely to least likely to be false
	return ( sameTimestamp || sameHash ) && exists && sameResource;
}

bool Task::IsDependencyUpToDate( const dependency::Dependency& prevInfo, const dependency::Dependency& curInfo ) const
{
	const bool exists = curInfo.GetTimeStamp() != 0ull;
	const bool existed = prevInfo.GetTimeStamp() != 0ull;
	const bool sameType = prevInfo.GetClass()->GetName() == curInfo.GetClass()->GetName();
	const bool sameResource = prevInfo.GetPathHash() == curInfo.GetPathHash();
	const bool sameHash = prevInfo.GetHash() == curInfo.GetHash();

	if ( !exists )
	{
		const dependency::Requirement require = curInfo.GetRequirementLevel();
		if ( require == dependency::Requirement::Required )
		{
			return false;
		}
	}

	// Order from most likely to least likely to be false
	return sameHash && exists == existed && sameResource && sameType;
}

bool Task::IsOutputUpToDate( const dependency::OutputFileInfo& prevInfo, const dependency::OutputFileInfo& curInfo ) const
{
	const bool sameResource = prevInfo.path == curInfo.path;
	const bool exists = curInfo.timestamp != 0ull;
	const bool sameTimestamp = prevInfo.timestamp == curInfo.timestamp;
	const bool sameHash = prevInfo.hash == curInfo.hash;

	// Order from most likely to least likely to be false
	return ( sameTimestamp || sameHash ) && exists && sameResource;
}

bool Task::IsDependencyValid( const dependency::Dependency& depInfo ) const
{
	if ( !depInfo.IsValid() )
	{
		const dependency::Requirement require = depInfo.GetRequirementLevel();
		if ( require == dependency::Requirement::Optional )
		{
			ErrorHandler().ReportWarning( "Optional resource '%hs' not found!", depInfo.GetPathString().Data() );
		}
		else if ( require == dependency::Requirement::Normal )
		{
			ErrorHandler().ReportError( "Resource '%hs' not found!", depInfo.GetPathString().Data() );
		}
		else if ( require == dependency::Requirement::Required )
		{
			ErrorHandler().ReportError( "Required resource '%hs' not found!", depInfo.GetPathString().Data() );
			return false;
		}
	}

	return true;
}

bool IsSourceDependency( const dependency::DependencyPtr& dep )
{
	return !dep->IsGenerated();
}

bool IsRescanDependency( const dependency::DependencyPtr& dep )
{
	return IsSourceDependency( dep ) && dep->RescanOnChanged();
}

bool IsBuiltDependency( const dependency::DependencyPtr& dep )
{
	return dep->IsGenerated();
}

bool IsChildDependency( const dependency::DependencyPtr& dep )
{
	return dep->IsGenerated() && dep->IsChild();
}

void Task::LookupSourceDependencies( job::Builder& builder )
{
	dependency::DependencyList& dependencies = m_currentDeps.GetDependencies();

	builder.DispatchParallelForJob( "DataBuild/LookupSourceDependencies", { dependencies.Size() }, [&system = m_system, &dependencies]( Uint32 index, const job::RunContext& )
	{
		dependency::DependencyPtr& dep = dependencies[ index ];
		if ( IsSourceDependency( dep ) )
		{
			dep->Update( system );
		}
	} );
}

void Task::LookupBuiltDependencies( job::Builder& builder )
{
	dependency::DependencyList& dependencies = m_currentDeps.GetDependencies();

	builder.DispatchParallelForJob( "DataBuild/LookupBuiltDependencies", { dependencies.Size() }, [&system = m_system, &dependencies]( Uint32 index, const job::RunContext& )
	{
		dependency::DependencyPtr& dep = dependencies[ index ];
		if ( IsBuiltDependency( dep ) )
		{
			dep->Update( system );
		}
	} );
}

void Task::LookupOutputList( job::Builder& builder )
{
	dependency::OutputFileList& outputs = m_currentDeps.GetOutputList();

	builder.DispatchParallelForJob( "DataBuild/LookupOutputs", { outputs.Size() }, [this, &outputs]( Uint32 index, const job::RunContext& )
	{
		auto& output = outputs[index];
		auto& depsCache = m_system.GetDepsCache( output.depot );
		depsCache.Lookup( output );
	} );
}


bool Task::AllSourceDependenciesUpToDate()
{
	RED_FATAL_ASSERT( m_previousDeps != nullptr );
	dependency::DependencyList previous = dependency::FilterDependencyList( m_previousDeps->GetDependencies(), IsSourceDependency );
	dependency::DependencyList current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsSourceDependency );

	return std::equal( previous.Begin(), previous.End(), current.Begin(), current.End(),
		[this]( const dependency::DependencyPtr& left, const dependency::DependencyPtr& right ) -> bool 
	{
		return IsDependencyUpToDate( *left, *right );
	} );
}

bool Task::AllRescanDependenciesUpToDate()
{
	RED_FATAL_ASSERT( m_previousDeps != nullptr );
	dependency::DependencyList previous = dependency::FilterDependencyList( m_previousDeps->GetDependencies(), IsRescanDependency );
	dependency::DependencyList current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsRescanDependency );

	return std::equal( previous.Begin(), previous.End(), current.Begin(), current.End(),
		[this]( const dependency::DependencyPtr& left, const dependency::DependencyPtr& right ) -> bool 
	{
		return IsDependencyUpToDate( *left, *right );
	} );
}

bool Task::AllBuiltDependenciesUpToDate()
{
	RED_FATAL_ASSERT( m_previousDeps != nullptr );
	dependency::DependencyList previous = dependency::FilterDependencyList( m_previousDeps->GetDependencies(), IsBuiltDependency );
	dependency::DependencyList current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsBuiltDependency );

	return std::equal( previous.Begin(), previous.End(), current.Begin(), current.End(),
		[this]( const dependency::DependencyPtr& left, const dependency::DependencyPtr& right ) -> bool 
	{
		return IsDependencyUpToDate( *left, *right );
	} );
}

bool Task::AllOutputsUpToDate()
{
	RED_FATAL_ASSERT( m_previousDeps != nullptr );

	const dependency::OutputFileList& previous = m_previousDeps->GetOutputList();
	const dependency::OutputFileList& current = m_currentDeps.GetOutputList();

	return std::equal( previous.Begin(), previous.End(), current.Begin(), current.End(),
		[this]( const dependency::OutputFileInfo& left, const dependency::OutputFileInfo& right ) -> bool
	{
		return IsOutputUpToDate( left, right );
	} );

	return false;
}

bool Task::AllSourceDependenciesExist()
{
	dependency::DependencyList current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsSourceDependency );

	return std::all_of( current.Begin(), current.End(), [this]( const dependency::DependencyPtr& item ) -> bool
	{
		return IsDependencyValid( *item );
	} );
}

bool Task::AllBuiltDependenciesExist()
{
	dependency::DependencyList current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsBuiltDependency );

	return std::all_of( current.Begin(), current.End(), [this]( const dependency::DependencyPtr& item ) -> bool
	{
		return IsDependencyValid( *item );
	} );
}

bool Task::CanDownloadFromDataCache() const
{
	bool hasCache = m_system.GetDataCache() != nullptr;
	bool isNotForced = ! m_system.IsForcedBuild();
	bool shouldGet = m_cacheStrategy != CacheStrategy::GET_NEVER;
	return hasCache && shouldGet && isNotForced;
}

bool Task::CanBuildLocally() const
{
	if ( m_system.IsServerBuild() )
	{
		return ( m_buildSettings & data::BuildSettings::SERVER ) != 0;
	}
	else
	{
		return ( m_buildSettings & data::BuildSettings::CLIENT ) != 0;
	}
}

//------------------------------------------------------------------------------
// Code for resolving the tool and dependencies
//

void Task::InitialiseTask( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: Initialise '%hs'", m_path.GetHash(), m_path.ToDebugString() );
	RED_FATAL_ASSERT( m_state == State::INITIAL );

	m_currentDeps.SetPath( m_path );
	m_currentDeps.SetPlatform( m_system.TargetPlatform() );

	Metrics_StartToolInit();
	bool result = InitialiseTool( builder );
	Metrics_StopToolInit();
	if ( !result )
	{
		Fail( FailReason::NO_TOOL );
		return;
	}

	builder.DispatchJob( "DataBuild/Initialise", [this]( const job::RunContext& )
	{
		if ( m_system.IsForcedBuild() )
		{
			RED_LOG_TRACE( "Task[%016llX] BuildReason - Forced!", m_path.GetHash() );
			MarkForBuild( BuildReason::FORCED );
			m_state = State::NEEDS_UPDATED_DEPS;
		}
		else
		{
			m_state = State::GET_STORED_DEPS;
		}
	} );
}


void Task::CheckStoredToolInfo()
{
	RED_LOG_TRACE( "Task[%016llX] Step: CheckStoredToolInfo", m_path.GetHash() );
	RED_FATAL_ASSERT( m_previousDeps != nullptr );

	const auto& previousTool = m_previousDeps->GetToolInfo();
	const auto& currentTool = m_currentDeps.GetToolInfo();

	if ( previousTool.name != currentTool.name )
	{
		ErrorHandler().ReportError( "Mismatch in build tool name detected! Expected '%hs' but was '%hs'",
			previousTool.name.AsChar(), currentTool.name.AsChar() );

		// Using a different generator therefore dependencies are invalid
		m_previousDeps = nullptr;
		RED_LOG_TRACE( "Task[%016llX] BuildReason - Build tool changed (Different tool)", m_path.GetHash() );
		MarkForBuild( BuildReason::TOOL_CHANGED );
	}

	if ( previousTool.version != currentTool.version )
	{
		// Different generator version therefore need to refresh the dependencies
		// so that that any changes to the generated dependencies can be picked up
		m_previousDeps = nullptr;
		RED_LOG_TRACE( "Task[%016llX] BuildReason - Build tool changed (Version different)", m_path.GetHash() );
		MarkForBuild( BuildReason::TOOL_CHANGED );
	}

	if ( previousTool.fileVersion != currentTool.fileVersion )
	{
		// Different RTTI / file structure version, just need to reprocess
		RED_LOG_TRACE( "Task[%016llX] BuildReason - Build tool changed (File version different)", m_path.GetHash() );
		MarkForBuild( BuildReason::FILE_VERSION_CHANGED );
	}
}


void Task::GetStoredDependencies()
{
	RED_LOG_TRACE( "Task[%016llX] Step: GetStoredDependencies", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::GET_STORED_DEPS );

	m_storedDeps = m_previousDeps = m_system.DependencyDatabase().GetResourceInfo( m_path );
	if ( m_previousDeps )
	{
		CheckStoredToolInfo();
	}

	// Retest the stored dependency info in case it was cleared when checking the tool dependency
	if ( m_previousDeps )
	{
		// Copy over the dependencies from the database into the current working set
		m_currentDeps.CopyDependencies( *m_previousDeps );

		m_state = State::CHECK_STORED_DEPS;
	}
	else
	{
		RED_LOG_TRACE( "Task[%016llX] BuildReason - First Time / No stored dependencies", m_path.GetHash() );
		MarkForBuild( BuildReason::FIRST_TIME );
		m_state = State::NEEDS_UPDATED_DEPS;
	}
}

void Task::CheckStoredDependencies( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: CheckStoredDependencies", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::CHECK_STORED_DEPS );

	LookupSourceDependencies( builder );

	builder.DispatchJob( "DataBuild/CheckStoredDependencies/AllUpToDate", [this]( const job::RunContext& )
	{
		if ( !AllSourceDependenciesUpToDate() )
		{
			RED_LOG_TRACE( "Task[%016llX] BuildReason - Source dependencies out of date", m_path.GetHash() );
			MarkForBuild( BuildReason::SOURCE_DEPS_CHANGED );

			if ( !AllSourceDependenciesExist() )
			{
				Fail( FailReason::SOURCE_MISSING );
				return;
			}
		}

		if ( AllRescanDependenciesUpToDate() )
		{
			m_state = State::WAITING_FOR_DEPS;
		}
		else
		{
			m_state = State::NEEDS_UPDATED_DEPS;
		}
	} );
}

void Task::UpdateDependencies( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: UpdateDependencies", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::NEEDS_UPDATED_DEPS );

	Metrics_StartToolCollectDeps();
	bool result = GetDependenciesFromTool( builder );
	Metrics_StopToolCollectDeps();
	if ( !result )
	{
		Fail( FailReason::DEPENDENCY_COLLECT_FAILED );
		return;
	}

	builder.DispatchJob( "DataBuild/UpdateDependencies/ResolveFromTool", [this]( const job::RunContext& )
	{
		// Check error handler so we can detect failures during job execution
		if ( ErrorHandler().HasFailed() )
		{
			Fail( FailReason::DEPENDENCY_COLLECT_FAILED );
		}
		else
		{
			m_currentDeps.SetDependencies( m_depsCollector );

			m_state = State::CHECK_UPDATED_DEPS;
		}
	} );
}

void Task::CheckUpdatedDependencies( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: CheckUpdatedDependencies", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::CHECK_UPDATED_DEPS );

	LookupSourceDependencies( builder );

	builder.DispatchJob( "DataBuild/UpdateDependencies/SetState", [this]( const job::RunContext& )
	{
		if ( AllSourceDependenciesExist() )
		{
			m_state = State::WAITING_FOR_DEPS;
		}
		else
		{
			Fail( FailReason::SOURCE_MISSING );
		}
	} );
}


//------------------------------------------------------------------------------
// Code for checking the built dependencies and building the output
//

void Task::CheckBuiltDependencies( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: CheckBuiltDependencies", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::CHECK_BUILT_DEPS );

	// Check generated dependencies
	LookupBuiltDependencies( builder );

	builder.DispatchJob( "DataBuild/ComputePrerequisitesHash", [this]( const job::RunContext& )
	{
		m_currentDeps.ComputePrerequisitesHash();
	} );

	builder.DispatchJob( "DataBuild/CheckBuiltDependencies/Check", [this]( const job::RunContext& )
	{
		if ( !AllBuiltDependenciesExist() )
		{
			Fail( FailReason::BUILT_MISSING );
			return;
		}

		bool upToDate = false;
		if ( m_previousDeps != nullptr )
		{
			upToDate = AllBuiltDependenciesUpToDate();
		}
		else
		{
			const auto& currentDeps = m_currentDeps.GetDependencies();
			upToDate = std::none_of( currentDeps.Begin(), currentDeps.End(), IsBuiltDependency );
		}

		if ( !upToDate )
		{
			RED_LOG_TRACE( "Task[%016llX] BuildReason - Built dependencies out of date", m_path.GetHash() );
			MarkForBuild( BuildReason::BUILT_DEPS_CHANGED );
		}

		m_state = State::CHECK_OUTPUT;
	} );
}

void Task::CheckOutput( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: CheckOutputs", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::CHECK_OUTPUT );

	if ( m_previousDeps != nullptr )
	{
		// Check output resource
		if ( ! m_currentDeps.Update( m_system ) )
		{
			RED_LOG_TRACE( "Task[%016llX] BuildReason - Output missing", m_path.GetHash() );
			MarkForBuild( BuildReason::RESOURCE_INVALIDATED );
		}
		else if ( ! IsResourceUpToDate( *m_previousDeps, m_currentDeps ) )
		{
			RED_LOG_TRACE( "Task[%016llX] BuildReason - Output invalidated", m_path.GetHash() );
			MarkForBuild( BuildReason::RESOURCE_INVALIDATED );
		}

		// Check additional outputs
		m_currentDeps.CopyOutputList( *m_previousDeps );

		LookupOutputList( builder );

		builder.DispatchJob( "DataBuild/CheckOutput/CheckOutputs", [this]( const job::RunContext& )
		{
			if ( ! AllOutputsUpToDate() )
			{
				RED_LOG_TRACE( "Task[%016llX] BuildReason - Outputs invalidated or missing", m_path.GetHash() );
				MarkForBuild( BuildReason::OUTPUTS_INVALIDATED );
			}
		} );
	}

	builder.DispatchJob( "DataBuild/CheckOutput/SetState", [this]( const job::RunContext& )
	{
		if ( m_buildReason == 0u )
		{
			if ( m_cacheStrategy == CacheStrategy::GET_LATEST && CanDownloadFromDataCache() )
			{
				RED_LOG_TRACE( "Task[%016llX] No building necessary, but attempting download of latest", m_path.GetHash() );
				m_state = State::DOWNLOAD_OUTPUT;
			}
			else
			{
				RED_LOG_TRACE( "Task[%016llX] No building necessary, resource up to date!", m_path.GetHash() );
				m_state = State::SUCCESSFUL;
			}
		}
		else
		{
			RED_LOG_TRACE( "Task[%016llX] Building %hs because %08X", m_path.GetHash(), m_path.ToDebugString(), m_buildReason );
			if ( CanDownloadFromDataCache() )
			{
				m_state = State::DOWNLOAD_OUTPUT;
			}
			else if ( CanBuildLocally() )
			{
				m_state = State::BUILD_OUTPUT;
			}
			else
			{
				Fail( FailReason::CANNOT_BUILD );
			}
		}
	} );
}

namespace prv
{

#if !defined( VANGUARD_ASSET_HOST )
struct DownloadContext
{
	dependency::OutputFileList outputList;
	red::DynArray< data::Cache::Result > resultList;
};
#endif

}

void Task::DownloadOutput( job::Builder& builder )
{
#if defined( VANGUARD_ASSET_HOST )
	RED_FATAL_ASSERT( m_system.GetDataCache() == nullptr, "VanguardAssetHost does not initialise the remote RED data cache" );
	RED_FATAL( "VanguardAssetHost entered the disabled RED data-cache download path" );
#else
	RED_LOG_TRACE( "Task[%016llX] Step: DownloadOutput", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::DOWNLOAD_OUTPUT );

	auto* cache = m_system.GetDataCache();
	RED_FATAL_ASSERT( cache != nullptr, "Data cache must exist" );

	auto downloadContext = red::CreateSharedPtr< prv::DownloadContext, red::PoolBackend >();

	Cache::Result lookupResult;
	if ( m_cacheStrategy == CacheStrategy::GET_EXACT )
	{
		lookupResult = cache->GetExactOutputList( m_currentDeps.GetPrerequisiteHash(), downloadContext->outputList, builder );
	}
	else if ( m_cacheStrategy == CacheStrategy::GET_LATEST )
	{
		lookupResult = cache->GetLatestOutputList( m_path, downloadContext->outputList, builder );
	}

	builder.DispatchJob( "DataBuild/DownloadOutput/GetOutputs", [this, downloadContext, lookupResult]( const job::RunContext& runContext )
	{
		if ( lookupResult.Failed() )
		{
			// Fall through to the next job and handle state transition there
			RED_LOG( "Task[%016llX] Download: Lookup database failed for prerequisite hash '%hs'.", m_path.GetHash(), m_currentDeps.GetPrerequisiteHash().ToString().AsChar() );
			return;
		}
		auto& fileList = downloadContext->outputList;
		if ( fileList.Empty() )
		{
			RED_LOG( "Task[%016llX] Download: No files found in database for prerequisite hash '%hs'.", m_path.GetHash(), m_currentDeps.GetPrerequisiteHash().ToString().AsChar() );
			return;
		}

		downloadContext->resultList.Resize( downloadContext->outputList.Size() );

		auto* cache = m_system.GetDataCache();

		job::Builder builder{ runContext };
		for ( Uint32 index = 0; index < downloadContext->outputList.Size(); ++index )
		{
			const auto& cacheDep = downloadContext->outputList[ index ];
			auto& resultEntry = downloadContext->resultList[ index ];

			auto& depsCache = m_system.GetDepsCache( cacheDep.depot );

			dependency::OutputFileInfo currentDep = cacheDep;
			if ( depsCache.Lookup( currentDep ) && IsOutputUpToDate( cacheDep, currentDep ) )
			{
				// No need to download if we are already up to date
				RED_LOG( "Task[%016llX] Skipped downloading '%hs' because it was up to date.", m_path.GetHash(), cacheDep.path.ToDebugString() );
				resultEntry.SetValue( true );
			}
			else
			{
				RED_LOG( "Task[%016llX] Downloading '%hs'", m_path.GetHash(), currentDep.path.ToDebugString(), currentDep.path.GetHash() );
				resultEntry = cache->GetResource( cacheDep.path, cacheDep.hash, m_taskContext.GetResourceDepot( cacheDep.depot ), builder );
			}
		}
	} );

	builder.DispatchJob( "DataBuild/DownloadOutput/Check", [this, downloadContext]( const job::RunContext& )
	{
		bool hadDownloads = !downloadContext->outputList.Empty();

		Uint32 downloadedCount = 0;
		for ( Uint32 index = 0; index < downloadContext->resultList.Size(); ++index )
		{
			const auto& result = downloadContext->resultList[ index ];
			const auto& output = downloadContext->outputList[ index ];
			if ( result.Successful() )
			{
				++downloadedCount;
				m_outputCollector.Add( output.path, output.depot );
			}
			else
			{
				RED_LOG_ERROR( "Task[%016llX] Failed to download '%hs' with prerequsite hash %hs", m_path.GetHash(), output.path.ToDebugString(), m_currentDeps.GetPrerequisiteHash().ToString().AsChar() );
			}
		}
		m_metrics.filesDownloaded = downloadedCount;

		bool allSuccessful = downloadedCount == downloadContext->outputList.Size();
		if ( hadDownloads && allSuccessful )
		{
			m_state = State::UPDATE_METADATA;
		}
		else
		{
			// NOTE: For now just clearing the collected outputs should do the trick
			m_outputCollector.Clear();

			// TODO: Should delete files which were downloaded here as we shouldn't leave orphaned files lying around
			// If the build is successful then the built files should override the downloaded ones
			// However if the build fails it will leave orphaned files hanging around

			if ( CanBuildLocally() )
			{
				m_state = State::BUILD_OUTPUT;
			}
			else
			{
				Fail( FailReason::DOWNLOAD_FAILED );
			}
			}
		} );
#endif
}

void Task::BuildOutput( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: BuildOutput", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::BUILD_OUTPUT );

	RED_LOG( "Task[%016llX] DataBuild: Building '%hs' because of %08X", m_path.GetHash(), m_path.ToDebugString(), m_buildReason );

	Metrics_StartToolBuild();
	bool result = BuildWithTool( builder );
	Metrics_StopToolBuild();
	if ( !result )
	{
		Fail( FailReason::BUILD_FAILED );
		return;
	}

	builder.DispatchJob( "DataBuild/BuildOutput/Check", [this]( const job::RunContext& )
	{
		RED_FATAL_ASSERT( m_state == State::BUILD_OUTPUT );

		m_metrics.filesBuilt = m_outputCollector.GetOutputListSize();

		// Check error handler so we can detect failures during job execution
		if ( ErrorHandler().HasFailed() )
		{
			Fail( FailReason::BUILD_FAILED );
		}
		else
		{
			m_state = State::UPDATE_METADATA;
		}
	} );
}

void Task::UpdateMetadata( job::Builder& builder )
{
	RED_LOG_TRACE( "Task[%016llX] Step: UpdateMetadata", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::UPDATE_METADATA );

	// Update output resource
	m_currentDeps.Update( m_system );

	m_currentDeps.SetOutputList( m_outputCollector );

	LookupOutputList( builder );

	builder.DispatchJob( "BuildData/UpdateMetadata/SetState", [this]( const job::RunContext& )
	{
		m_system.DependencyDatabase().SetResourceInfo( m_currentDeps );

		m_state = State::CLEAN_OUTPUT;
	} );
}

namespace
{

dependency::OutputFileList GetOutputsToDelete( const dependency::OutputFileList& stored, const dependency::OutputFileList& current )
{
	dependency::OutputFileList outputsToDelete;
	outputsToDelete.Reserve( stored.Size() );

	auto storedIter = stored.Begin(), storedEnd = stored.End();
	auto currentIter = current.Begin(), currentEnd = current.End();
	while ( storedIter != storedEnd && currentIter != currentEnd )
	{
		if ( *storedIter < *currentIter )
		{
			// Old file not present in previous version
			outputsToDelete.PushBack( *storedIter );
			++storedIter;
		}
		else if ( *currentIter < *storedIter )
		{
			// New file added to the current version
			++currentIter;
		}
		else // *storedIter == *currentIter
		{
			// Existing output matched in both previous and current version
			++storedIter;
			++currentIter;
		}
	}

	while ( storedIter != storedEnd )
	{
		outputsToDelete.PushBack( *storedIter );
		++storedIter;
	}

	return outputsToDelete;
}

dependency::OutputFileList GetAbandonedDependenciesToDelete( const dependency::DependencyList& stored, const dependency::DependencyList& current, dependency::Database& depsDatabase )
{
	red::DynArray< const dependency::ResourceInfo* > abandonedDeps;
	abandonedDeps.Reserve( stored.Size() );

	auto PushDep = [&abandonedDeps, &depsDatabase]( const dependency::DependencyPtr& dep )
	{
		res::ResourcePath path = dep->GetResourcePath();
		if ( path.IsValid() )
		{
			const auto* resInfo = depsDatabase.GetResourceInfo( path );
			if ( resInfo )
			{
				abandonedDeps.PushBack( resInfo );
			}
		}
	};

	auto storedIter = stored.Begin(), storedEnd = stored.End();
	auto currentIter = current.Begin(), currentEnd = current.End();
	while ( storedIter != storedEnd && currentIter != currentEnd )
	{
		if ( *storedIter < *currentIter )
		{
			// Old dependency not present in stored version
			PushDep( *storedIter );
			++storedIter;
		}
		else if ( *currentIter < *storedIter )
		{
			// New dependency added to the current version
			++currentIter;
		}
		else // *storedIter == *currentIter
		{
			// Existing output matched in both previous and current version
			++storedIter;
			++currentIter;
		}
	}

	while ( storedIter != storedEnd )
	{
		PushDep( *storedIter );
		++storedIter;
	}

	dependency::OutputFileList result;

	Uint32 count = std::accumulate( abandonedDeps.Begin(), abandonedDeps.End(), 0, 
		[]( Uint32 total, const dependency::ResourceInfo* resInfo ) { return total + resInfo->GetOutputList().Size() + 1; } );
	result.Reserve( count );

	for ( const dependency::ResourceInfo* resInfo : abandonedDeps )
	{
		result.PushBack( resInfo->GetOutputResourceInfo() );
		result.PushBack( resInfo->GetOutputList() );
	}

	return result;
}

}

void Task::CleanOutput()
{
	RED_LOG_TRACE( "Task[%016llX] Step: CleanOutput", m_path.GetHash() );
	RED_FATAL_ASSERT( m_state == State::CLEAN_OUTPUT );

	dependency::OutputFileList outputsToDelete;

	if ( m_storedDeps != nullptr )
	{
		// Collect from this tasks previous outputs
		outputsToDelete = GetOutputsToDelete( m_storedDeps->GetOutputList(), m_currentDeps.GetOutputList() );

		// Collect abandoned built dependencies of this task and add their outputs to be deleted
		auto stored = dependency::FilterDependencyList( m_storedDeps->GetDependencies(), IsChildDependency );
		auto current = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsChildDependency );
		outputsToDelete.PushBack( GetAbandonedDependenciesToDelete( stored, current, m_system.DependencyDatabase() ) );
	}

	if ( ! outputsToDelete.Empty() )
	{
		for ( const auto& output : outputsToDelete )
		{
			RED_LOG( "Task[%016llX] Removing old generated file '%hs' from the %hs depot", m_path.GetHash(), 
				output.path.ToDebugString(), ( output.depot == data::DepotLocation::Source ? "source" : "cooked" ) );
			auto& depot = m_system.GetResourceDepot( output.depot );
			depot.RemoveResource( output.path );
		}

		m_metrics.filesDeleted = outputsToDelete.Size();
	}

	m_state = State::SUCCESSFUL;
}

//------------------------------------------------------------------------------
// Main Running functions
//

job::Counter Task::CreateResolveJobs()
{
	job::Builder builder;

	builder.DispatchJob( "DataBuild/Initial", [this]( const job::RunContext& runContext )
	{
		Metrics_StartResolve();
		if ( m_state == State::INITIAL )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			InitialiseTask( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/GetStoredDeps", [this]( const job::RunContext& )
	{
		Metrics_StopState();
		if ( m_state == State::GET_STORED_DEPS )
		{
			Metrics_StartState();
			GetStoredDependencies();
			Metrics_StopState();
		}
	} );

	builder.DispatchJob( "DataBuild/CheckStoredDeps", [this]( const job::RunContext& runContext )
	{
		if ( m_state == State::CHECK_STORED_DEPS )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			CheckStoredDependencies( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/CollectDeps", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::NEEDS_UPDATED_DEPS )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			UpdateDependencies( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/CheckCollectedDeps", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::CHECK_UPDATED_DEPS )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			CheckUpdatedDependencies( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/ResolveEnd", [this]( const job::RunContext& )
	{
		Metrics_StopState();
		Metrics_StopResolve();
	} );

	return builder.ExtractWaitCounter();
}


void Task::CreateBuildJobs( const job::Counter& dependencyWaitCounter )
{
	job::Builder builder;
	builder.DispatchWait( dependencyWaitCounter );

	builder.DispatchJob( "DataBuild/CheckBuiltDeps", [this]( const job::RunContext& runContext )
	{
		Metrics_StartBuild();

		if ( m_state == State::WAITING_FOR_DEPS )
		{
			m_state = State::CHECK_BUILT_DEPS;
		}

		if ( m_state == State::CHECK_BUILT_DEPS )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			CheckBuiltDependencies( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/CheckOutput", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::CHECK_OUTPUT )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			CheckOutput( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/DownloadOutput", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::DOWNLOAD_OUTPUT )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			DownloadOutput( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/BuildOutput", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::BUILD_OUTPUT )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			BuildOutput( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/UpdateMetadata", [this]( const job::RunContext& runContext )
	{
		Metrics_StopState();
		if ( m_state == State::UPDATE_METADATA )
		{
			Metrics_StartState();
			job::Builder depBuilder{ runContext };
			UpdateMetadata( depBuilder );
		}
	} );

	builder.DispatchJob( "DataBuild/CleanOutput", [this]( const job::RunContext& )
	{
		Metrics_StopState();
		if ( m_state == State::CLEAN_OUTPUT )
		{
			Metrics_StartState();
			CleanOutput();
			Metrics_StopState();
		}

		Metrics_StopBuild();
	} );

	m_buildCounter += builder.ExtractWaitCounter();

	m_buildDeferral.FinishDeferral();
}

void Task::SkipBuildJobs()
{
	// Skip build jobs if the task was failed before the build jobs were even scheduled
	RED_FATAL_ASSERT( Finished(), "Should only skip build jobs if the task had been completed in another way." );
	m_buildDeferral.FinishDeferral();
}

//------------------------------------------------------------------------------

red::Set< res::ResourcePath > Task::GetBuiltDependencies() const
{
	auto builtDependencies = dependency::FilterDependencyList( m_currentDeps.GetDependencies(), IsBuiltDependency );

	red::Set< res::ResourcePath > result;
	result.Reserve( builtDependencies.Size() );

	for ( const auto& dep : builtDependencies )
	{
		result.InsertUnsorted( res::ResourcePath::Build( dep->GetPathString() ) );
	}

	result.MakeClean();

	return result;
}

red::DynArray< res::ResourcePath > Task::GetSourceOutputs() const
{
	const auto& allOutputs = m_currentDeps.GetOutputList();

	red::DynArray< res::ResourcePath > result;
	if ( m_currentDeps.GetOutputDepot() == data::DepotLocation::Source )
	{
		// If we output to the source depot add ourselves as an output as well
		// so that the resource and its dependencies get included in the build correctly
		result.Reserve( allOutputs.Size() + 1 );
		result.PushBackUnchecked( m_currentDeps.GetPath() );
	}
	else
	{
		result.Reserve( allOutputs.Size() );
	}

	for ( const auto& output : allOutputs )
	{
		if ( output.depot == data::DepotLocation::Source )
		{
			result.PushBackUnchecked( output.path );
		}
	}

	return result;
}

const job::Counter& Task::GetBuildTasksCounter() const
{
	return m_buildCounter;
}

void Task::Fail( FailReason reason )
{
	RED_LOG_ERROR( "Task[%016llX] Failed - %hs", m_path.GetHash(), GetFailReasonText( reason ) );
	m_state = State::FAILED;
	m_failReason = reason;
}

void Task::MarkForBuild( BuildReason reason )
{
	m_buildReason |= reason;
}

//------------------------------------------------------------------------------

const char* GetFailReasonText( FailReason reason )
{
	switch ( reason )
	{
	case FailReason::NO_TOOL:
		return "No tool found";
	case FailReason::BUILD_FAILED:
		return "Build with Tool failed";
	case FailReason::DOWNLOAD_FAILED:
		return "Download from data cache failed";
	case FailReason::CANNOT_BUILD:
		return "No valid build settings found";
	case FailReason::DEPENDENCY_COLLECT_FAILED:
		return "Collecting dependencies from tool failed";
	case FailReason::DEPENDENCY_CYCLE:
		return "Found a dependency cycle";
	case FailReason::SOURCE_MISSING:
		return "Source dependency missing";
	case FailReason::BUILT_MISSING:
		return "Built dependency missing";
	case FailReason::NONE:
	default:
		return "";
	};
}

//------------------------------------------------------------------------------

namespace prv
{

struct IsolationGroupLedger
{
	red::DynArray< CName > m_groups;
	red::RWSpinLock m_guard;

	Uint32 Register( CName name )
	{
		Uint32 result = red::INVALID_INDEX;
		{
			RED_SCOPE_SHARED_LOCK( m_guard );
			result = m_groups.GetIndex( name );
		}

		if ( result == red::INVALID_INDEX )
		{
			RED_SCOPE_LOCK( m_guard );
			result = m_groups.GetIndex( name );
			if ( result == red::INVALID_INDEX )
			{
				result = m_groups.Size();
				RED_FATAL_ASSERT( result < 32, "Maximum number of generator isolation groups exceeded!" );
				m_groups.PushBack( name );
			}
		}

		return result;
	}
};

} // prv

Uint32 Task::GetIsolationGroup( const CName name )
{
	static prv::IsolationGroupLedger s_ledger;
	return s_ledger.Register( name );
}

//------------------------------------------------------------------------------

void Task::Metrics_DumpHeader( metrics::Writer& writer )
{
	String subHeader = 
		"Resource Hash\tResource Class\tBuild Reason\t"
		"Tool Name\tTool Version\t"
		"Total\tResolve\tBuild\t"
		"Init\tGet Stored Deps\tCheck Stored Deps\tGet Updated Deps\tCheck Updated Deps\t"
		"Check Built Deps\tCheck Output\tDownload Output\tBuild Output\tUpdate Metadata\tClean Output\t"
		"Tool Init\tTool Collect Deps\tTool Generate\t"
		"Files Downloaded\tFiles Built\tFiles Deleted\t"
		"Resource Path\n";

	writer.Write( subHeader );
}

void Task::Metrics_Dump( data::metrics::Writer& writer ) const
{
	const rtti::ClassType* type = res::GetResourceClassForFile( m_path );
	const char* typeName = (type != nullptr) ? type->GetName().AsChar() : "(no name)";
	String line = String::Printf( "%016llX\t%hs\t%08X\t", m_path.GetHash(), typeName, m_buildReason );

	// ToolInfo
	const auto& toolInfo = m_currentDeps.GetToolInfo();
	line += String::Printf( "%hs\t%u\t", toolInfo.name.AsChar(), toolInfo.version );

	// Metrics
	line += String::Printf( "%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t",
		m_metrics.totalWallClockTime.count(), 
		m_metrics.resolveWallClockTime.count(),
		m_metrics.buildWallClockTime.count(),
		m_metrics.initTime.count(),
		m_metrics.getStoredDepsTime.count(),
		m_metrics.checkStoredDepsTime.count(),
		m_metrics.getUpdatedDepsTime.count(),
		m_metrics.checkUpdatedDepsTime.count(),
		m_metrics.checkBuildDepsTime.count(),
		m_metrics.checkOutputTime.count(),
		m_metrics.downloadOutputTime.count(),
		m_metrics.buildOutputTime.count(),
		m_metrics.updateMetadataTime.count(),
		m_metrics.cleanOutputTime.count(),
		m_metrics.toolInitTime.count(),
		m_metrics.toolCollectDepsTime.count(),
		m_metrics.toolGenerateTime.count(),
		m_metrics.filesDownloaded,
		m_metrics.filesBuilt,
		m_metrics.filesDeleted
		);

	// ResourcePath
	const auto sv = m_path.ToStringView();
	line.Append( sv.Data(), sv.Length() );
	line += "\n";

	writer.Write( line );
}

//------------------------------------------------------------------------------

void Task::Metrics_StartResolve()
{
	m_taskStartTime = m_stageStartTime = metrics::Clock::now();
}

void Task::Metrics_StopResolve()
{
	m_metrics.resolveWallClockTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( metrics::Clock::now() - m_stageStartTime );
}

void Task::Metrics_StartBuild()
{
	m_stageStartTime = metrics::Clock::now();
}

void Task::Metrics_StopBuild()
{
	auto endTime = metrics::Clock::now();
	m_metrics.buildWallClockTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( endTime - m_stageStartTime );
	m_metrics.totalWallClockTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( endTime - m_taskStartTime );
}

void Task::Metrics_StartState()
{
	m_timingState = m_state;
	m_stateStartTime = metrics::Clock::now();
}

void Task::Metrics_StopState()
{
	auto timeTaken = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( metrics::Clock::now() - m_stateStartTime );
	switch ( m_timingState )
	{
	case State::INITIAL: m_metrics.initTime = timeTaken; break;
	case State::GET_STORED_DEPS: m_metrics.getStoredDepsTime = timeTaken; break;
	case State::CHECK_STORED_DEPS: m_metrics.checkStoredDepsTime = timeTaken; break;
	case State::NEEDS_UPDATED_DEPS: m_metrics.getUpdatedDepsTime = timeTaken; break;
	case State::CHECK_UPDATED_DEPS: m_metrics.checkUpdatedDepsTime = timeTaken; break;
	case State::CHECK_BUILT_DEPS: m_metrics.checkBuildDepsTime = timeTaken; break;
	case State::CHECK_OUTPUT: m_metrics.checkOutputTime = timeTaken; break;
	case State::DOWNLOAD_OUTPUT: m_metrics.downloadOutputTime = timeTaken; break;
	case State::BUILD_OUTPUT: m_metrics.buildOutputTime = timeTaken; break;
	case State::UPDATE_METADATA: m_metrics.updateMetadataTime = timeTaken; break;
	case State::CLEAN_OUTPUT: m_metrics.cleanOutputTime = timeTaken; break;
	case State::SUCCESSFUL:
	case State::FAILED:
		break;
	default:
		RED_FATAL( "Unknown state %u was being measured in metrics", m_timingState );
		break;
	};
	m_timingState = State::FAILED; // Set the state to something else to not overwrite the metrics
}

void Task::Metrics_StartToolInit()
{
	m_toolStartTime = metrics::Clock::now();
}

void Task::Metrics_StopToolInit()
{
	m_metrics.toolInitTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( metrics::Clock::now() - m_toolStartTime );
}

void Task::Metrics_StartToolCollectDeps()
{
	m_toolStartTime = metrics::Clock::now();
}

void Task::Metrics_StopToolCollectDeps()
{
	m_metrics.toolCollectDepsTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( metrics::Clock::now() - m_toolStartTime );
}

void Task::Metrics_StartToolBuild()
{
	m_toolStartTime = metrics::Clock::now();
}

void Task::Metrics_StopToolBuild()
{
	m_metrics.toolGenerateTime = std::chrono::duration_cast< metrics::TaskMetrics::Duration >( metrics::Clock::now() - m_toolStartTime );
}

} // data
