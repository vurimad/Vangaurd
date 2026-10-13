/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "jobMemoryPools.h"
#include "jobRunner.h"

namespace job
{

struct RunContext;

enum class Fence
{
	None,
	Full,
};

// A builder is a temporary helper used to create new jobs and link their dependencies safely.
// It's intended to be used scoped to a function on a single thread.
//
// Conceptually, each builder has its own local queue where its jobs are run in dispatch order.
// To run jobs in order, you can think of a "fence" inserted between each job.
//
// In order to run jobs in parallel, you need to use builder.DispatchJob< job::Fence::None >() or builder.DispatchJob*_NoFence() functions.
// With no fence, there's no ordering between dispatched jobs.
//
// However, you then have to call builder.DispatchFenceExplicitly() afterwards, before you call the default DispatchJob() functions again, or the builder destructs.
// Otherwise you'll get an assert because the builder doesn't try to second guess your intentions.
class REDJOBS2_API RED_NODISCARD Builder : red::NonCopyable
{
	RED_USE_MEMORY_POOL(red::PoolEngine);

	using PoolJobs = PoolJobs2Data;

public:
	// Create a new builder without any previous dependencies.
	explicit Builder(ScheduleParam param = ScheduleParam(), const void* debugUserData = nullptr);

	// Create a builder that continues the currently running job.
	// This way you can create so-called continuation jobs: fork from a parent job, and the parent job won't be
	// considered finished until the forked child job is also finished.
	//
	// You can create multiple such builders if you want to fork multiple times.
	// But often this is used for the case where you need to create more jobs from a running job because
	// you didn't know in advance what jobs to create, or if that parent job acquires some resource, then it's easier
	// to use it in the child job than pass around some shared pointer ahead of time.
	explicit Builder(const RunContext& runContext);

	// If the builder was created with a RunContext and ExtractWaitCounter() wasn't already called,
	// then the destructor does the final continuation linking to the parent job.
	// That is, the builder should be temporary and not kept alive beyond the scope it's needed.
	~Builder();

	Builder(Builder&&) = delete;

	Builder& operator=(Builder&&) = delete;

	// Used to insert a fence after having used builder.DispatchJob< job::Fence::None >() or builder.DispatchJob*_NoFence() functions.
	// Harmless if called redundantly.
	void DispatchFenceExplicitly();

	// Queues a sync point, waiting for a counter to reach zero.
	void DispatchWait(const Counter& externalWaitCounter);

	// Invalidates the builder; only use when no longer needed.
	// Extracts a counter from this builder. The counter can then be used as a dependency to wait for all jobs to finish that were dispatched by this builder.
	// e.g., it could be the argument to another builder's DispatchWait().
	Counter ExtractWaitCounter();

	//---
	template< Fence fence = Fence::Full, typename TFunc, size_t N >
	void DispatchJob(StringLiteralConstRef< N > debugName, TFunc&& func)
	{
		::job::DispatchJob< PoolJobs >(debugName, m_waitForZeroCounter, m_accumulateCounter, std::forward<TFunc>(func));
		DoFence(fence);
	}
	template< typename TFunc, size_t N >
	void DispatchJobAfterWait_NoFence(const Counter& externalWaitCounter, StringLiteralConstRef< N > debugName, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchJob< PoolJobs >(debugName, combinedWaitForZeroCounter, m_accumulateCounter, std::forward<TFunc>(func));
		DoFence(Fence::None);
	}
	//---
	template< Fence fence = Fence::Full, typename TFunc, size_t N >
	void DispatchParallelForJob(StringLiteralConstRef< N > debugName, ImmediateValue numElements, TFunc&& func)
	{
		::job::DispatchParallelForJob< PoolJobs >(debugName, m_waitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func));
		DoFence(fence);
	}
	template< Fence fence = Fence::Full, typename TNumElementsFunc, typename TFunc, size_t N >
	void DispatchParallelForJob(StringLiteralConstRef< N > debugName, TNumElementsFunc&& numElementsFunc, TFunc&& func)
	{
		::job::DispatchParallelForJob< PoolJobs >(debugName, m_waitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func));
		DoFence(fence);
	}
	template< typename TFunc, size_t N >
	void DispatchParallelForJobAfterWait_NoFence(const Counter& externalWaitCounter, StringLiteralConstRef< N > debugName, ImmediateValue numElements, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJob< PoolJobs >(debugName, combinedWaitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func));
		DoFence(Fence::None);
	}
	template< typename TNumElementsFunc, typename TFunc, size_t N >
	void DispatchParallelForJobAfterWait_NoFence(const Counter& externalWaitCounter, StringLiteralConstRef< N > debugName, TNumElementsFunc&& numElementsFunc, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJob< PoolJobs >(debugName, combinedWaitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func));
		DoFence(Fence::None);
	}
	//---
	template< Fence fence = Fence::Full, typename TFunc, typename TEpilogueFunc, size_t N >
	void DispatchParallelForJobWithEpilogue(StringLiteralConstRef< N > debugName, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(debugName, m_waitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(fence);
	}
	template< Fence fence = Fence::Full, typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc, size_t N >
	void DispatchParallelForJobWithEpilogue(StringLiteralConstRef< N > debugName, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(debugName, m_waitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(fence);
	}
	template< typename TFunc, typename TEpilogueFunc, size_t N >
	void DispatchParallelForJobWithEpilogueAfterWait_NoFence(const Counter& externalWaitCounter, StringLiteralConstRef< N > debugName, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(debugName, combinedWaitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(Fence::None);
	}
	template< typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc, size_t N >
	void DispatchParallelForJobWithEpilogueAfterWait_NoFence(const Counter& externalWaitCounter, StringLiteralConstRef< N > debugName, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(debugName, combinedWaitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(Fence::None);
	}

	// Special cases

	template< JobHint hint, Fence fence = Fence::Full, typename TFunc, size_t N >
	void DispatchJobWithHint( StringLiteralConstRef< N > debugName, TFunc&& func )
	{
		::job::ext::DispatchJobWithHint< hint, PoolJobs >( debugName, m_waitForZeroCounter, m_accumulateCounter, std::forward<TFunc>( func ) );
		DoFence( fence );
	}

	template< Fence fence = Fence::Full, typename TFunc, typename TEpilogueFunc, size_t N >
	void DispatchParallelForJobWithEpilogueWithBatchSize( StringLiteralConstRef< N > debugName, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc, Uint32 maxBatchSize )
	{
		::job::ext::DispatchParallelForJobWithEpilogueWithBatchSize< PoolJobs >( debugName, m_waitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ), maxBatchSize );
		DoFence( fence );
	}

	template< Fence fence = Fence::Full, typename TFunc, typename TEpilogueFunc >
	void DispatchParallelForJobWithEpilogueWithBatchSize(
		red::InstrumentationObject& instrumentationObject,
		ImmediateValue numElements,
		TFunc&& func,
		TEpilogueFunc&& epilogueFunc,
		Uint32 maxBatchSize )
	{
		::job::ext::DispatchParallelForJobWithEpilogueWithBatchSize< PoolJobs >(
			instrumentationObject,
			m_waitForZeroCounter,
			m_accumulateCounter,
			numElements,
			std::forward< TFunc >( func ),
			std::forward< TEpilogueFunc >( epilogueFunc ),
			maxBatchSize );
		DoFence( fence );
	}

	// Custom instrumentation object overrides

	//---
	template< Fence fence = Fence::Full, typename TFunc >
	void DispatchJob(red::InstrumentationObject& instrumentationObject, TFunc&& func)
	{
		::job::DispatchJob< PoolJobs >(instrumentationObject, m_waitForZeroCounter, m_accumulateCounter, std::forward<TFunc>(func));
		DoFence(fence);
	}
	template< typename TFunc >
	void DispatchJobAfterWait_NoFence(const Counter& externalWaitCounter, red::InstrumentationObject& instrumentationObject, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchJob< PoolJobs >(instrumentationObject, combinedWaitForZeroCounter, m_accumulateCounter, std::forward<TFunc>(func));
		DoFence(Fence::None);
	}
	//---
	template< Fence fence = Fence::Full, typename TFunc >
	void DispatchParallelForJob(red::InstrumentationObject& instrumentationObject, ImmediateValue numElements, TFunc&& func)
	{
		::job::DispatchParallelForJob< PoolJobs >(instrumentationObject, m_waitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func));
		DoFence(fence);
	}
	template< Fence fence = Fence::Full, typename TNumElementsFunc, typename TFunc >
	void DispatchParallelForJob(red::InstrumentationObject& instrumentationObject, TNumElementsFunc&& numElementsFunc, TFunc&& func)
	{
		::job::DispatchParallelForJob< PoolJobs >(instrumentationObject, m_waitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func));
		DoFence(fence);
	}
	template< typename TFunc >
	void DispatchParallelForJobAfterWait_NoFence(const Counter& externalWaitCounter, red::InstrumentationObject& instrumentationObject, ImmediateValue numElements, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJob< PoolJobs >(instrumentationObject, combinedWaitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func));
		DoFence(Fence::None);
	}
	template< typename TNumElementsFunc, typename TFunc >
	void DispatchParallelForJobAfterWait_NoFence(const Counter& externalWaitCounter, red::InstrumentationObject& instrumentationObject, TNumElementsFunc&& numElementsFunc, TFunc&& func)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJob< PoolJobs >(instrumentationObject, combinedWaitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func));
		DoFence(Fence::None);
	}
	//---
	template< Fence fence = Fence::Full, typename TFunc, typename TEpilogueFunc >
	void DispatchParallelForJobWithEpilogue(red::InstrumentationObject& instrumentationObject, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(instrumentationObject, m_waitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(fence);
	}
	template< Fence fence = Fence::Full, typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc >
	void DispatchParallelForJobWithEpilogue(red::InstrumentationObject& instrumentationObject, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(instrumentationObject, m_waitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(fence);
	}
	template< typename TFunc, typename TEpilogueFunc >
	void DispatchParallelForJobWithEpilogueAfterWait_NoFence(const Counter& externalWaitCounter, red::InstrumentationObject& instrumentationObject, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(instrumentationObject, combinedWaitForZeroCounter, m_accumulateCounter, numElements, std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(Fence::None);
	}
	template< typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc >
	void DispatchParallelForJobWithEpilogueAfterWait_NoFence(const Counter& externalWaitCounter, red::InstrumentationObject& instrumentationObject, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc)
	{
		Counter combinedWaitForZeroCounter{ { m_waitForZeroCounter.Internal_GetPriority(), m_waitForZeroCounter.Internal_GetAffinity() }, m_debugUserData };
		combinedWaitForZeroCounter += m_waitForZeroCounter;
		combinedWaitForZeroCounter += externalWaitCounter;

		::job::DispatchParallelForJobWithEpilogue< PoolJobs >(instrumentationObject, combinedWaitForZeroCounter, m_accumulateCounter, std::forward< TNumElementsFunc >(numElementsFunc), std::forward< TFunc >(func), std::forward< TEpilogueFunc >(epilogueFunc));
		DoFence(Fence::None);
	}

private:
	// Should be fully optimized out since Fence is known at compile-time
	RED_FORCE_INLINE void DoFence(Fence fence)
	{
		switch (fence)
		{
		case Fence::None:
		{
#ifdef RED_ASSERTS_ENABLED
			m_debugNeedsExplicitFence = true;
#endif
		}
		break;
		case Fence::Full:
		{
			RED_FATAL_ASSERT(!m_debugNeedsExplicitFence, "DispatchFenceExplicitly() should have been called after using Fence::None");
			DispatchFenceExplicitly();
		}
		break;
		default:
			break;
		}
	}

	void GuardThread()
	{
		RED_FATAL_ASSERT(m_threadOwner == red::ThreadId::CurrentThread());
	}

	void Sync_NoGuard();
	void FinalSync_NoGuard();

	const char* GetDebugName() const { return m_debugName; }
	const char* m_debugName;
	const void* m_debugUserData;

	// NOTE: this data is owned by the job system,
	// but we can destruct the Builder without caring.
	Counter m_waitForZeroCounter;
	Counter m_accumulateCounter;
	Counter* m_continuationCounter; // Not owned
	ScheduleParam m_param;
	red::ThreadId m_threadOwner;
	Bool m_isExtracted;
	Bool m_debugNeedsExplicitFence;
};

}
