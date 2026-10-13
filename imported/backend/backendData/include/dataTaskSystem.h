/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataSystemContext.h"
#include "dataTask.h"
#include "dataTaskMetrics.h"

namespace data
{

struct TaskSystemMetrics
{
	using Duration = metrics::Milliseconds;

	Duration resolveTime;
	Duration buildTime;
	Uint32 resourceCount;

	TaskSystemMetrics();
};

class TaskReporter;

class BACKEND_DATA_API TaskSystem
{
	RED_USE_MEMORY_POOL( PoolDataSystem );
public:
	using TaskFactoryFunction = std::function< TaskUniquePtr ( const res::ResourcePath& resourcePath, SystemContext& systemContext ) >;

	TaskSystem( SystemContext& systemContext, TaskFactoryFunction taskFactory, TaskReporter& reporter );
	TaskSystem( SystemContext& systemContext, TaskFactoryFunction taskFactory, TaskReporter& reporter, Uint32 maxConcurrentTasks );
	~TaskSystem();

	// Resolves and builds the resources specified by the given list
	// This will also build all of the dependent resources
	void Build( const red::DynArray< res::ResourcePath >& resourcePaths );

	// Returns a list of all the resources that were built and saved to the source depot
	const red::DynArray< res::ResourcePath >& GetBuiltSourceResources() const;

	// Returns a list of all the resources that have a cache strategy set
	red::DynArray< res::ResourcePath > GetCacheUsingResources() const;

	// Returns a list of all the data tasks that have been executed in this system
	red::ArraySpan< const TaskSharedPtr > Internal_GetAllTasksInOrder() const;

	static void Metrics_DumpGlobalInfo( metrics::Writer& writer );
	void Metrics_DumpInfo( metrics::Writer& writer ) const;
	void Metrics_DumpTasks( metrics::Writer& writer ) const;

	red::ArraySpan< const TaskSharedPtr > GetAllTasksInOrderOfExecution() const;
	red::ArraySpan< const TaskSystemMetrics > GetMetrics() const;

	static Uint32 DefaultMaxConcurrentTasks();

private:
	TaskUniquePtr CreateTask( const res::ResourcePath& resourcePath );
	red::DynArray< TaskSharedPtr > CreateTasks( const red::ArraySpan< const res::ResourcePath > resourcePaths );

	void ResolveTasks( const red::ArraySpan< data::TaskSharedPtr > tasks, Uint32& index );
	void ResolveCurrentTasks( const red::DynArray< res::ResourcePath >& resourcePaths );
	void BuildCurrentTasks();

	struct ResolveScheduleContext;
	void ScheduleResolveTask( ResolveScheduleContext& context );

	struct BuildScheduleContext;
	void ScheduleSpecificBuildTask( BuildScheduleContext& context, Uint32 taskIndex );

	SystemContext& m_context;
	TaskFactoryFunction m_taskFactory;
	TaskReporter& m_reporter;

	// List of tasks to process in the current build phase
	red::DynArray< TaskSharedPtr > m_tasks;

	// List of all tasks processed by this system in the order which they were executed (used for metrics)
	red::DynArray< TaskSharedPtr > m_allTasksInOrder;

	// Map of all resources processed by this system to the task they were processed in (used for looking up processed tasks)
	red::HashMap< res::ResourcePath, TaskSharedPtr > m_resourceTaskMap;

	// Map of all resources processed by this system to the resources they depend on
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > m_dependencies;

	// List of resource files built into the source directory
	red::DynArray< res::ResourcePath > m_sourceOutputs;

	const Uint32 m_maxConcurrentTasks;

	red::DynArray< TaskSystemMetrics > m_metrics;
};

RED_INLINE red::ArraySpan< const TaskSharedPtr > TaskSystem::GetAllTasksInOrderOfExecution() const
{
	return m_allTasksInOrder;
}

RED_INLINE red::ArraySpan< const TaskSystemMetrics > TaskSystem::GetMetrics() const
{
	return m_metrics;
}

} // data
