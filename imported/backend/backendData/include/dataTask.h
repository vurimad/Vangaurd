/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redJobs2/include/jobCounterOwner.h"

#include "dataSystemContext.h"
#include "dataTaskContext.h"
#include "dependencyTypes.h"
#include "dependencyTypesImpl.h"
#include "dependencyCollector.h"
#include "dependencyOutputCollector.h"
#include "dataTaskMetrics.h"

namespace red
{
	struct InstrumentationObject;
}

namespace job
{
	class Builder;
}

namespace data
{

enum class State : Uint32
{
	INITIAL,				// Initial state of the task
	GET_STORED_DEPS,		// Get stored dependencies from the deps database
	CHECK_STORED_DEPS,		// Checks the stored dependencies
	NEEDS_UPDATED_DEPS,		// Refresh dependencies from tool
	CHECK_UPDATED_DEPS,		// Check updated dependencies
	WAITING_FOR_DEPS,		// Waiting for dependencies to be built
	CHECK_BUILT_DEPS,		// Check built dependencies are up to date
	CHECK_OUTPUT,			// Check output is up to date
	DOWNLOAD_OUTPUT,		// Download built output from server cache
	BUILD_OUTPUT,			// Build output
	UPDATE_METADATA,		// Update metadata
	CLEAN_OUTPUT,			// Clean up after building
	SUCCESSFUL,				// Successful end state
	FAILED,					// Failure end state
};

// NOTE: Using full bit fields instead of RED_FLAG here so it is easier to see which
// things contributed to the logged reason why a resource was built
enum BuildReason : Uint32
{
	SOURCE_DEPS_CHANGED			= 0x00000001,		// Source dependencies have changed
	BUILT_DEPS_CHANGED			= 0x00000002,		// Built dependencies have changed

	RESOURCE_INVALIDATED		= 0x00000010,		// The resource has been invalidated and must be rebuilt
	OUTPUTS_INVALIDATED			= 0x00000020,		// Other outputs have been invalidated and must be rebuilt

	TOOL_CHANGED				= 0x00000100,		// The tool has changed, either name, version
	FILE_VERSION_CHANGED		= 0x00000200,		// Serialisation File version changed

	FIRST_TIME					= 0x00001000,		// First time this resource is being built
	FORCED						= 0x00002000,		// Forced building of the resource
};

enum class FailReason : Uint32
{
	NONE,
	NO_TOOL,						// No valid generator or cooker found to process this task
	BUILD_FAILED,					// Failed to build with the cooker or generator
	DOWNLOAD_FAILED,				// Failed to get all required files from the data cache
	CANNOT_BUILD,					// No valid build settings exist for this task
	DEPENDENCY_COLLECT_FAILED,		// Failed to collect dependencies from cooker or generator
	DEPENDENCY_CYCLE,				// Part of a cyclic dependency chain
	SOURCE_MISSING,					// Required source dependency missing
	BUILT_MISSING,					// Required built dependency missing
};

const char* GetFailReasonText( FailReason reason );

class Task;
using TaskUniquePtr = red::UniquePtr< data::Task >;
using TaskSharedPtr = red::SharedPtr< data::Task >;
using TaskWeakPtr = red::WeakPtr< data::Task >;

// Encapsulates the entire process of building a resource in the data build system
// This includes resolving dependencies, building the resource, and updating metadata
class BACKEND_DATA_API Task
{
	RED_USE_MEMORY_POOL( PoolDataSystem );

public:
	Task( const res::ResourcePath& path, SystemContext& system );
	virtual ~Task();

	Task( const Task& ) = delete;
	Task( Task&& other ) = delete;
	Task& operator=( const Task& ) = delete;
	Task& operator=( Task&& ) = delete;

	job::Counter CreateResolveJobs();
	void CreateBuildJobs( const job::Counter& dependencyWaitCounter );

	const res::ResourcePath& GetPath() const; 

	red::Set< res::ResourcePath > GetBuiltDependencies() const;

	red::DynArray< res::ResourcePath > GetSourceOutputs() const;

	const job::Counter& GetBuildTasksCounter() const;

	State GetState() const;

	bool Finished() const;
	bool Succeeded() const;
	bool Failed() const;

	void Fail( FailReason reason );
	FailReason GetFailureReason() const;
	Uint32 GetBuildReason() const;

	void SkipBuildJobs();

	bool UsesCache() const;

	bool IsIsolated() const;
	Uint32 GetIsolationGroup() const;

	static void Metrics_DumpHeader( metrics::Writer& writer );
	void Metrics_Dump( metrics::Writer& writer ) const;
	const metrics::TaskMetrics& GetMetrics() const;
	const dependency::ResourceInfo& Internal_GetDependencyResourceInfo() const;

protected:

	virtual bool InitialiseTool( job::Builder& builder ) = 0;
	virtual bool GetDependenciesFromTool( job::Builder& builder ) = 0;
	virtual bool BuildWithTool( job::Builder& builder ) = 0;

	// Functions that take a builder do not complete until the jobs complete
	void InitialiseTask( job::Builder& builder );
	void CheckStoredToolInfo();
	void GetStoredDependencies();
	void CheckStoredDependencies( job::Builder& builder );
	void UpdateDependencies( job::Builder& builder );
	void CheckUpdatedDependencies( job::Builder& builder );
	void CheckBuiltDependencies( job::Builder& builder );
	void CheckOutput( job::Builder& builder );
	void BuildOutput( job::Builder& builder );
	void DownloadOutput( job::Builder& builder );
	void UpdateMetadata( job::Builder& builder );
	void CleanOutput();

	bool IsResourceUpToDate( const dependency::ResourceInfo& prevInfo, const dependency::ResourceInfo& curInfo ) const;
	bool IsDependencyUpToDate( const dependency::Dependency& prevInfo, const dependency::Dependency& curInfo ) const;
	bool IsOutputUpToDate( const dependency::OutputFileInfo& prevInfo, const dependency::OutputFileInfo& curInfo ) const;
	bool IsDependencyValid( const dependency::Dependency& depInfo ) const;

	void LookupSourceDependencies( job::Builder& builder );
	void LookupBuiltDependencies( job::Builder& builder );
	void LookupOutputList( job::Builder& builder );

	bool AllSourceDependenciesUpToDate();
	bool AllRescanDependenciesUpToDate();
	bool AllBuiltDependenciesUpToDate();
	bool AllSourceDependenciesExist();
	bool AllBuiltDependenciesExist();
	bool AllOutputsUpToDate();

	bool CanDownloadFromDataCache() const;
	bool CanBuildLocally() const;

	void MarkForBuild( BuildReason reason );

	ErrorHandler& ErrorHandler() const;

	static Uint32 GetIsolationGroup( const CName name );

	SystemContext& m_system;
	const res::ResourcePath m_path;
	TaskContext m_taskContext;

	job::Counter m_buildCounter;
	job::CompletionDeferral m_buildDeferral;

	dependency::Collector m_depsCollector;
	dependency::OutputCollector m_outputCollector;

	const dependency::ResourceInfo* m_storedDeps;	// Dependencies stored in the database
	const dependency::ResourceInfo* m_previousDeps;	// Previous dependencies used to evaluate if it is up to date or not
	dependency::ResourceInfo m_currentDeps;			// Current dependencies during this build

	data::CacheStrategy m_cacheStrategy;
	Uint32 m_buildSettings;
	Uint32 m_isolationGroup;

	Uint32 m_buildReason;
	State m_state;
	FailReason m_failReason;

	metrics::TaskMetrics m_metrics;

	// Variables used in gathering the metrics
	using TimePoint = metrics::Clock::time_point;
	TimePoint m_toolStartTime;
	TimePoint m_taskStartTime;
	TimePoint m_stageStartTime;
	TimePoint m_stateStartTime;
	State m_timingState;

	void Metrics_StartResolve();
	void Metrics_StopResolve();
	void Metrics_StartBuild();
	void Metrics_StopBuild();
	void Metrics_StartState();
	void Metrics_StopState();
	void Metrics_StartToolInit();
	void Metrics_StopToolInit();
	void Metrics_StartToolCollectDeps();
	void Metrics_StopToolCollectDeps();
	void Metrics_StartToolBuild();
	void Metrics_StopToolBuild();
};

//------------------------------------------------------------------------------

RED_INLINE State Task::GetState() const
{
	return m_state;
}

RED_INLINE const res::ResourcePath& Task::GetPath() const
{
	return m_path;
}

RED_INLINE bool Task::Finished() const
{
	return m_state == State::SUCCESSFUL || m_state == State::FAILED;
}

RED_INLINE bool Task::Succeeded() const
{
	return m_state == State::SUCCESSFUL;
}

RED_INLINE bool Task::Failed() const
{
	return m_state == State::FAILED;
}

RED_INLINE FailReason Task::GetFailureReason() const
{
	return m_failReason;
}

RED_INLINE ErrorHandler& Task::ErrorHandler() const
{
	return m_taskContext.ErrorHandler();
}

RED_INLINE bool Task::UsesCache() const
{
	return m_cacheStrategy != data::CacheStrategy::GET_NEVER;
}

RED_INLINE const dependency::ResourceInfo& Task::Internal_GetDependencyResourceInfo() const
{
	return m_currentDeps;
}

RED_INLINE bool Task::IsIsolated() const
{
	return ( m_buildSettings & data::BuildSettings::ISOLATED ) != 0;
}

RED_INLINE Uint32 Task::GetIsolationGroup() const
{
	return m_isolationGroup;
}
	
RED_INLINE const metrics::TaskMetrics& Task::GetMetrics() const
{
	return m_metrics;
}

RED_INLINE Uint32 Task::GetBuildReason() const
{
	return m_buildReason;
}


//------------------------------------------------------------------------------

RED_INLINE bool operator<( const TaskSharedPtr& left, const TaskSharedPtr& right )
{
	return left->GetPath() < right->GetPath();
}

RED_INLINE bool operator<( const TaskSharedPtr& left, const res::ResourcePath& right )
{
	return left->GetPath() < right;
}

RED_INLINE bool operator<( const res::ResourcePath& left, const TaskSharedPtr& right )
{
	return left < right->GetPath();
}

} // data
