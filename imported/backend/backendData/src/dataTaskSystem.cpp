/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataTaskSystem.h"
#include "dataSystemInstance.h"
#include "dataTaskReporter.h"

#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/redSystem/include/stopWatch.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/engine/include/engineWatchdog.h"
#endif

#include "dataTaskLog.h"

// For metrics
#include "cooker.h"
#include "generator.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"

namespace data
{

//------------------------------------------------------------------------------

TaskSystemMetrics::TaskSystemMetrics()
	: resolveTime( Duration::zero() )
	, buildTime( Duration::zero() )
	, resourceCount( 0u )
{
}

//------------------------------------------------------------------------------

Uint32 TaskSystem::DefaultMaxConcurrentTasks()
{
	return job::GetNumDispatcherThreads() * 2;
}

TaskSystem::TaskSystem( SystemContext& systemContext, TaskFactoryFunction taskFactory, TaskReporter& reporter )
	: TaskSystem( systemContext, taskFactory, reporter, DefaultMaxConcurrentTasks() )
{
}

TaskSystem::TaskSystem( SystemContext& systemContext, TaskFactoryFunction taskFactory, TaskReporter& reporter, Uint32 maxConcurrentTasks )
	: m_context( systemContext )
	, m_taskFactory( taskFactory )
	, m_reporter( reporter )
	, m_maxConcurrentTasks( maxConcurrentTasks )
{
	RED_FATAL_ASSERT( m_taskFactory );
}

TaskSystem::~TaskSystem()
{
}

void TaskSystem::Build( const red::DynArray< res::ResourcePath >& resourcePaths )
{
	m_metrics.EmplaceBack();
	ResolveCurrentTasks( resourcePaths );
	BuildCurrentTasks();
}

const red::DynArray< res::ResourcePath >& TaskSystem::GetBuiltSourceResources() const
{
	return m_sourceOutputs;
}

red::DynArray< res::ResourcePath > TaskSystem::GetCacheUsingResources() const
{
	red::DynArray< res::ResourcePath > result;
	result.Reserve( m_allTasksInOrder.Size() );
	for ( const auto& task : m_allTasksInOrder )
	{
		if ( task->UsesCache() )
		{
			result.PushBack( task->GetPath() );
		}
	}
	return result;
}

red::ArraySpan< const TaskSharedPtr > TaskSystem::Internal_GetAllTasksInOrder() const
{
	return m_allTasksInOrder;
}

TaskUniquePtr TaskSystem::CreateTask( const res::ResourcePath& resourcePath )
{
	return m_taskFactory( resourcePath, m_context );
}

red::DynArray< TaskSharedPtr > TaskSystem::CreateTasks( const red::ArraySpan< const res::ResourcePath > resourcePaths )
{
	red::DynArray< TaskSharedPtr > result;
	result.Reserve( resourcePaths.Size() );
	for ( const auto& resourcePath : resourcePaths )
	{
		data::TaskSharedPtr dataTaskPtr = CreateTask( resourcePath );
		if ( dataTaskPtr )
		{
			m_resourceTaskMap[ resourcePath ] = dataTaskPtr;
			result.PushBack( dataTaskPtr );
		}
		else
		{
			// TODO: Error could not create build task for %hs
		}
	}
	return result;
}

void TaskSystem::ResolveCurrentTasks( const red::DynArray< res::ResourcePath >& resourcePaths )
{
	PC_SCOPE( ResolveCurrentTasks );
	metrics::StopWatch timer;

	m_reporter.UpdateSystemStatus( String::Printf( "Stage 1: Resolving dependencies for %u resources", resourcePaths.Size() ) );

	// maps from a resource to all of the resources it is dependent upon (ones that need to be complete before this resource is done)
	red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > forwardDependencies;

	{
		// NOTE: Using DynArrays which we sort manually here instead of Sets because it is significantly faster
		// because we know at a higher level when arrays are sorted and how we merge them together
		red::DynArray< res::ResourcePath > resources = resourcePaths;
		red::alg::RemoveDuplicates( resources );

		// Take all existing processed resources as visited
		red::DynArray< res::ResourcePath > visitedResources = m_dependencies.GetKeys();
		red::alg::RemoveDuplicates( visitedResources );

		// Remove already visisted resources from being considered again
		{
			auto iter = red::alg::RemoveIfIn( resources.Begin(), resources.End(), visitedResources.Begin(), visitedResources.End() );
			resources.Remove( iter, resources.End() );
		}

		Uint32 index = 1;
		while ( !resources.Empty() )
		{
			// Create and resolve tasks
			red::DynArray< data::TaskSharedPtr > resolveTasks = CreateTasks( resources );

			ResolveTasks( resolveTasks, index );

			// Add resolved task resources to visited resources list
			{
				Uint32 prevSize = visitedResources.Size();
				visitedResources.PushBack( resources );
				std::inplace_merge( visitedResources.Begin(), visitedResources.Begin() + prevSize, visitedResources.End() );
			}

			// Collate all dependencies discovered that need to be built
			red::DynArray< res::ResourcePath > allDependencies;
			allDependencies.Reserve( 65536 ); // Reserve 1MB worth of space for dependency paths
			for ( const auto& task : resolveTasks )
			{
				auto builtDependencies = task->GetBuiltDependencies();
				forwardDependencies.Insert( task->GetPath(), builtDependencies );
				allDependencies.PushBack( builtDependencies.Elements() );
			}

			// Remove visited dependencies for next iteration
			{
				std::sort( allDependencies.Begin(), allDependencies.End() );
				auto iter = std::unique( allDependencies.Begin(), allDependencies.End() );
				iter = red::alg::RemoveIfIn( allDependencies.Begin(), iter, visitedResources.Begin(), visitedResources.End() );
				allDependencies.Remove( iter, allDependencies.End() );
				resources = std::move( allDependencies );
			}
		}
	}

	// Remove dependencies that have already been completed
	// NOTE: This includes previously completed tasks as well as tasks that failed to resolve
	{
		auto IsTaskComplete = [this]( const res::ResourcePath& path ) -> bool
		{
			auto task = m_resourceTaskMap.Find( path );
			RED_FATAL_ASSERT( task != m_resourceTaskMap.End() );
			return task.Value()->Finished();
		};

		for ( auto& fwdDep : forwardDependencies )
		{
			fwdDep.Value().RemoveIf( IsTaskComplete );
		}
	}

	// Copy resolved dependencies to main list
	// NOTE: We do this after we remove completed dependencies so we only try to depend on tasks that will have something to execute
	m_dependencies.Reserve( m_dependencies.Size() + forwardDependencies.Size() );
	for ( const auto& dep : forwardDependencies )
	{
		m_dependencies.Insert( dep.Key(), dep.Value() );
	}

	// Do a topological sort of the tasks by the dependencies using Kahn's algorithm
	m_tasks.Reserve( forwardDependencies.Size() );
	{
		PC_SCOPE( ResolveCurrentTasks_TopologicalSort );

		// maps from a resource to all resources dependent on it (resources with are waiting on this one to complete)
		red::HashMap< res::ResourcePath, red::Set< res::ResourcePath > > reverseDependencies;

		// Initial stage forming the list of leaf nodes
		// Creates a reverse dependencies lookup here to help remove forward dependencies in main stage of algorithm
		red::DynArray< res::ResourcePath > leafResources;
		auto iter = forwardDependencies.Begin(), end = forwardDependencies.End();
		while ( iter != end )
		{
			if ( iter.Value().Empty() )
			{
				leafResources.PushBack( iter.Key() );
				auto result = forwardDependencies.Remove( iter );
				RED_FATAL_ASSERT( result.IsSuccessful() );
				iter = result.Iterator();
			}
			else
			{
				for ( const auto& path : iter.Value() )
				{
					reverseDependencies[ path ].Insert( iter.Key() );
				}

				++iter;
			}
		}

		// Main stage of sorting
		while ( !leafResources.Empty() )
		{
			const res::ResourcePath path = leafResources.PopBack();

			auto iter = m_resourceTaskMap.Find( path );
			RED_FATAL_ASSERT( iter != m_resourceTaskMap.End() );
			m_tasks.PushBack( iter.Value() );

			auto& revDeps = reverseDependencies[ path ];
			for ( auto& revDep : revDeps )
			{
				auto& fwdDeps = forwardDependencies[ revDep ];
				fwdDeps.Remove( path );
				if ( fwdDeps.Empty() )
				{
					leafResources.PushBack( revDep );
					forwardDependencies.Remove( revDep );
				}
			}
			reverseDependencies.Remove( path );
		}

		if ( !forwardDependencies.Empty() )
		{
			// ERROR: Cycle found in dependency chain!
			// Flagging these tasks as failed and adding them into the list for statistics and error reporting at the end
			for ( const auto& path : forwardDependencies.GetKeys() )
			{
				auto iter = m_resourceTaskMap.Find( path );
				RED_FATAL_ASSERT( iter != m_resourceTaskMap.End() );
				const TaskSharedPtr& task = iter.Value();
				task->Fail( FailReason::DEPENDENCY_CYCLE );
				task->SkipBuildJobs();
				m_tasks.PushBack( task );
			}
		}
	}

	m_metrics.Back().resolveTime = timer.Get< TaskSystemMetrics::Duration >();
	m_metrics.Back().resourceCount = m_tasks.Size();
	RED_LOG( "DataTaskSystem: Resolving %u dependencies for %u resources took %u ms", m_tasks.Size(), resourcePaths.Size(), m_metrics.Back().resolveTime.count() );

	// TODO: Move this somewhere else as we are skipping resolve progress (which could take a while for some resources)
	m_reporter.SetTasksCount( m_tasks.Size() );
}

struct TaskSystem::ResolveScheduleContext
{
	const red::ArraySpan< data::TaskSharedPtr > tasks;
	const Uint32 startIndex;
	red::Atomic< Uint32 > nextIndex;
	job::Counter mainCounter;
};

void TaskSystem::ScheduleResolveTask( ResolveScheduleContext& context )
{
	Uint32 taskIndex = context.nextIndex.PostIncrement();
	if ( taskIndex >= context.tasks.Size() )
	{
		return;
	}

	const Uint32 displayIndex = context.startIndex + taskIndex;
	auto& task = context.tasks[ taskIndex ];

	job::Builder builder;

	builder.DispatchJob( "DataBuildSystem/ResolveStart", [this, displayIndex, path = task->GetPath()]( const job::RunContext& )
	{
		RED_LOG_TRACE( "Task[%016llX] Started Resolve", path.GetHash() );
		m_reporter.UpdateTaskProgress( displayIndex, String::Printf( "%6u - %hs", displayIndex, path.ToDebugString() ) );
	} );

	builder.DispatchWait( task->CreateResolveJobs() );

	builder.DispatchJob( "DataBuildSystem/ResolveEnd", [this, &context, displayIndex, path = task->GetPath()]( const job::RunContext& )
	{
		RED_LOG_TRACE( "Task[%016llX] Finished Resolve", path.GetHash() );
		m_reporter.ClearTaskProgress( displayIndex );
		ScheduleResolveTask( context );
	} );

	context.mainCounter += builder.ExtractWaitCounter();
}

void TaskSystem::ResolveTasks( const red::ArraySpan< data::TaskSharedPtr > tasks, Uint32& index )
{
	metrics::StopWatch timer;
	const Uint32 startIndex = index;

	ResolveScheduleContext context{ tasks, startIndex, {}, job::Counter() };

	const Uint32 startingTaskCount = Min( m_maxConcurrentTasks, tasks.Size() );
	for ( Uint32 i = 0; i < startingTaskCount; ++i )
	{
		ScheduleResolveTask( context );
	}

	job::FlushCounter( context.mainCounter );

	index += context.nextIndex.GetValue();
	RED_LOG( "DataTaskSystem: Resolving Tasks %u to %u took %u ms", startIndex, index - 1, timer.Get< TaskSystemMetrics::Duration >().count() );
}

struct TaskSystem::BuildScheduleContext
{
	const red::ArraySpan< data::TaskSharedPtr > tasks;
	const Uint32 startIndex;
	job::Counter mainCounter;

	red::SpinLock guard;
	Uint32 nextIndex;
	Uint32 isolationMask;

	Uint32 NextTask_NoLock();
	Uint32 NextTask();
	Uint32 NextIsolatedTask( const Uint32 completedIndex );
};

Uint32 TaskSystem::BuildScheduleContext::NextTask_NoLock()
{
	Uint32 index = nextIndex;
	if ( index >= tasks.Size() )
	{
		return index;
	}

	auto task = tasks[index];
	while ( task->IsIsolated() )
	{
		const Uint32 taskMask = 1 << task->GetIsolationGroup();
		if ( (isolationMask & taskMask) == 0 )
		{
			isolationMask |= taskMask;
			break;
		}

		++index;
		if ( index >= tasks.Size() )
		{
			break;
		}
		task = tasks[index];
	}

	nextIndex = index + 1;
	return index;
}

Uint32 TaskSystem::BuildScheduleContext::NextTask()
{
	RED_SCOPE_LOCK( guard );
	return NextTask_NoLock();
}

Uint32 TaskSystem::BuildScheduleContext::NextIsolatedTask( const Uint32 completedIndex )
{
	RED_SCOPE_LOCK( guard );
	Uint32 index = completedIndex + 1;
	if ( index >= tasks.Size() )
	{
		return index;
	}

	const Uint32 isolationGroup = tasks[completedIndex]->GetIsolationGroup();
	const Uint32 taskMask = 1 << isolationGroup;

	do
	{
		auto task = tasks[index];
		if ( task->IsIsolated() && task->GetIsolationGroup() == isolationGroup )
		{
			return index;
		}
		++index;
	}
	while ( index < nextIndex && index < tasks.Size() );

	isolationMask &= ~taskMask;

	return NextTask_NoLock();
}

void TaskSystem::ScheduleSpecificBuildTask( BuildScheduleContext& context, Uint32 taskIndex )
{
	if ( taskIndex >= context.tasks.Size() )
	{
		return;
	}

	const Uint32 displayIndex = context.startIndex + taskIndex;
	auto& task = context.tasks[ taskIndex ];

	job::Counter dependencyWaitCounter;
	// All of this tasks dependencies should already have been processed
	for ( const res::ResourcePath& path : m_dependencies[ task->GetPath() ] )
	{
		auto iter = m_resourceTaskMap.Find( path );
		RED_FATAL_ASSERT( iter != m_resourceTaskMap.End() );
		dependencyWaitCounter += iter.Value()->GetBuildTasksCounter();
	}

	job::Counter preBuildCounter;
	job::DispatchJob< red::PoolBackend >( "DataBuildSystem/BuildStart", dependencyWaitCounter, preBuildCounter, [this, displayIndex, path = task->GetPath()]( const job::RunContext& )
	{
		RED_LOG_TRACE( "Task[%016llX] Started Build", path.GetHash() );
		m_reporter.UpdateTaskProgress( displayIndex, String::Printf( "%6u - %hs", displayIndex, path.ToDebugString() ) );
	} );

	task->CreateBuildJobs( preBuildCounter );

	job::DispatchJob< red::PoolBackend >( "DataBuildSystem/BuildEnd", task->GetBuildTasksCounter(), context.mainCounter, [this, &context, taskIndex, displayIndex, path = task->GetPath()]( const job::RunContext& )
	{
		RED_LOG_TRACE( "Task[%016llX] Finished Build", path.GetHash() );

		m_reporter.ClearTaskProgress( displayIndex );

		Uint32 nextTask = context.tasks.Size();
		auto& task = context.tasks[taskIndex];
		if ( task->IsIsolated() )
		{
			nextTask = context.NextIsolatedTask( taskIndex );
		}
		else
		{
			nextTask = context.NextTask();
		}
		ScheduleSpecificBuildTask( context, nextTask );
	} );
}

void TaskSystem::BuildCurrentTasks()
{
	metrics::StopWatch timer;

	m_reporter.UpdateSystemStatus( String::Printf( "Stage 2: Making sure %u resources are up to date", m_tasks.Size() ) );

	BuildScheduleContext context{ m_tasks, 1, job::Counter() };
	context.nextIndex = 0;
	context.isolationMask = 0;

	const Uint32 startingTaskCount = Min( m_maxConcurrentTasks, m_tasks.Size() );
	for ( Uint32 i = 0; i < startingTaskCount; ++i )
	{
		ScheduleSpecificBuildTask( context, context.NextTask() );
	}

	job::FlushCounter( context.mainCounter );

	m_metrics.Back().buildTime = timer.Get< TaskSystemMetrics::Duration >();
	RED_LOG( "DataTaskSystem: Building %u tasks took %u ms", m_tasks.Size(), m_metrics.Back().buildTime.count() );

	// Update source assets list
	m_sourceOutputs.Reserve( m_sourceOutputs.Size() + m_tasks.Size() );
	for ( const auto& task : m_tasks )
	{
		m_sourceOutputs.PushBack( task->GetSourceOutputs() );
	}

	m_allTasksInOrder.PushBack( m_tasks );
	m_tasks.Clear();
}

//------------------------------------------------------------------------------

namespace prv
{
extern red::DynArray< ICooker* > EnumerateCookers();
extern red::DynArray< IGenerator* > EnumerateGenerators();
}

void TaskSystem::Metrics_DumpGlobalInfo( metrics::Writer& writer )
{
	// File Serialisation Version
	String text = String::Printf(
		"Data Task System Metrics\n\n"
		"File Serialization Version: %u\n",
		VER_CURRENT
	);
	writer.Write( text );

	writer.Write( "\nCookers\n" );
	auto cookers = prv::EnumerateCookers();
	for ( const auto cooker : cookers )
	{
		writer.Write( String::Printf( "%hs\t%u\n", cooker->GetName().AsChar(), cooker->GetVersion() ) );
	}

	writer.Write( "\nGenerators\n" );
	auto generators = prv::EnumerateGenerators();
	for ( const auto generator : generators )
	{
		writer.Write( String::Printf( "%hs\t%u\n", generator->GetName().AsChar(), generator->GetVersion() ) );
	}
}

void TaskSystem::Metrics_DumpInfo( metrics::Writer& writer ) const
{
	writer.Write( "\nStage\tResolve Time\tBuild Time\tResource Count\n" );

	Uint32 index = 1;
	for ( const auto& metrics : m_metrics )
	{
		writer.Write( String::Printf( "Stage%u\t%u\t%u\t%u\n", index, metrics.resolveTime.count(), metrics.buildTime.count(), metrics.resourceCount ) );
		++index;
	}

	writer.Write( "\n" );
}

void TaskSystem::Metrics_DumpTasks( metrics::Writer& writer ) const
{
	data::Task::Metrics_DumpHeader( writer );
	for ( const auto& task : m_allTasksInOrder )
	{
		task->Metrics_Dump( writer );
	}
}

} // data
