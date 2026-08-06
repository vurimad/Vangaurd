/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/poolRoot.h"

// #fixme: should move into redSystem?
#include "../../redCore/include/profilerChannels.h"

#include "jobSystem.h"
#include "jobRunner.inl"
#include "jobCounterFunctions.h"

namespace job
{

struct JobDecl;

template< size_t N >
using StringLiteralConstRef = const char ( & )[ N ];

struct ImmediateValue
{
	Uint32 GetValue() const { return m_value; }

	Uint32 m_value;
};

///---

template< typename TMemoryPool, typename TFunc, size_t N >
RED_INLINE void BuildJob( JobDecl& outJobDecl, StringLiteralConstRef< N > debugName, TFunc&& func )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( const RunContext& ) >() ), TFunc >::value, "Incorrect function signature" );

	using TJobShim = prv::JobShim< TFunc, TMemoryPool >;

	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	prv::InitJobDecl< TMemoryPool, TJobShim >( outJobDecl, s_instrumentationObject, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, typename TFunc, size_t N >
RED_INLINE void BuildParallelForJob( JobDeclParallelFor& outJobDecl, StringLiteralConstRef< N > debugName, Uint32 numElements, TFunc&& func )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( Uint32 index, const RunContext& ) >() ), TFunc >::value, "Incorrect function signature" );

	using TJobShim = prv::ParallelForJobShim< TMemoryPool, TFunc >;

	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	prv::InitJobDeclParallelFor< TMemoryPool, TJobShim >( outJobDecl, s_instrumentationObject, numElements, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, typename TFunc, typename TEpilogueFunc, size_t N >
RED_INLINE void BuildParallelForJobWithEpilogue( JobDeclParallelFor& outJobDecl, StringLiteralConstRef< N > debugName, Uint32 numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( prv::IsFunctionTrait< TEpilogueFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( Uint32 index, const RunContext& ) >() ), TFunc >::value, "Incorrect function signature" );
	static_assert( std::is_assignable< decltype( std::function< void( const RunContext& ) >() ), TEpilogueFunc >::value, "Incorrect epilogue signature" );

	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	using TJobShim = prv::ParallelForJobShim< TMemoryPool, TFunc, TEpilogueFunc >;
	prv::InitJobDeclParallelFor< TMemoryPool, TJobShim >( outJobDecl, s_instrumentationObject, numElements, std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
}

///---

template< typename TMemoryPool, typename TFunc >
RED_INLINE void BuildJob( JobDecl& outJobDecl, red::InstrumentationObject& instrumentationObject, TFunc&& func )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( const RunContext& ) >() ), typename red::RemoveCVRef< TFunc >::Type >::value, "Incorrect function signature" );

	using TJobShim = prv::JobShim< TFunc, TMemoryPool >;

	prv::InitJobDecl< TMemoryPool, TJobShim >( outJobDecl, instrumentationObject, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, typename TFunc >
RED_INLINE void BuildParallelForJob( JobDeclParallelFor& outJobDecl, red::InstrumentationObject& instrumentationObject, Uint32 numElements, TFunc&& func )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( Uint32 index, const RunContext& ) >() ), TFunc >::value, "Incorrect function signature" );

	using TJobShim = prv::ParallelForJobShim< TMemoryPool, TFunc >;

	prv::InitJobDeclParallelFor< TMemoryPool, TJobShim >( outJobDecl, instrumentationObject, numElements, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, typename TFunc, typename TEpilogueFunc >
RED_INLINE void BuildParallelForJobWithEpilogue( JobDeclParallelFor& outJobDecl, red::InstrumentationObject& instrumentationObject, Uint32 numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	static_assert( prv::IsFunctionTrait< TFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( prv::IsFunctionTrait< TEpilogueFunc >::Value == false, "Shouldn't use a std::function!" );
	static_assert( std::is_assignable< decltype( std::function< void( Uint32 index, const RunContext& ) >() ), TFunc >::value, "Incorrect function signature" );
	static_assert( std::is_assignable< decltype( std::function< void( const RunContext& ) >() ), TEpilogueFunc >::value, "Incorrect epilogue signature" );

	using TJobShim = prv::ParallelForJobShim< TMemoryPool, TFunc, TEpilogueFunc >;
	prv::InitJobDeclParallelFor< TMemoryPool, TJobShim >( outJobDecl, instrumentationObject, numElements, std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
}

///---

template< typename TMemoryPool, typename TFunc >
RED_INLINE void DispatchJob( red::InstrumentationObject& instrumentationObject, const Counter& waitForZeroCounter, Counter& accumulateCounter, TFunc&& func )
{
	JobDecl jobDecl;
	BuildJob< TMemoryPool, TFunc >( jobDecl, instrumentationObject, std::forward< TFunc >( func ) );
	::job::RunJob( jobDecl, waitForZeroCounter, accumulateCounter );
}

template< typename TMemoryPool, typename TFunc >
RED_INLINE void DispatchParallelForJob( red::InstrumentationObject& instrumentationObject, const Counter& waitForZeroCounter, Counter& accumulateCounter, ImmediateValue numElements, TFunc&& func )
{
	JobDeclParallelFor jobDecl;
	BuildParallelForJob< TMemoryPool, TFunc >( jobDecl, instrumentationObject, numElements.GetValue(), std::forward< TFunc >( func ) );
	::job::RunParallelForJob( jobDecl, waitForZeroCounter, accumulateCounter );
}

template< typename TMemoryPool, typename TNumElementsFunc, typename TFunc >
RED_INLINE void DispatchParallelForJob( red::InstrumentationObject& instrumentationObject, const Counter& waitForZeroCounter, Counter& accumulateCounter, TNumElementsFunc&& numElementsFunc, TFunc&& func )
{
	// mutable for std::move()
	const auto continuationJob = 
		[ numElementsFunc = std::forward<TNumElementsFunc>( numElementsFunc ), func = std::forward<TFunc>( func ) ]
	( const job::RunContext& runContext ) mutable
	{
		static_assert( !std::is_reference<decltype( numElementsFunc )>::value, "" );
		static_assert( !std::is_reference<decltype( func )>::value, "" );

		const Uint32 numElements = numElementsFunc();

		JobDeclParallelFor jobDecl;
		auto* instrumentationObject = runContext.continuationContext.instrumentationObject;
		RED_FATAL_ASSERT( instrumentationObject );
		BuildParallelForJob< TMemoryPool, TFunc >( jobDecl, *instrumentationObject, numElements, std::move( func ) );

		Counter noWait{ runContext.continuationContext.param };
		Counter& continuationCounter = *runContext.continuationContext.counter;

		::job::RunParallelForJob( jobDecl, noWait, continuationCounter );
	};

	::job::DispatchJob< TMemoryPool >( instrumentationObject, waitForZeroCounter, accumulateCounter, continuationJob );
}

template< typename TMemoryPool, typename TFunc, typename TEpilogueFunc >
RED_INLINE void DispatchParallelForJobWithEpilogue( red::InstrumentationObject& instrumentationObject, const Counter& waitForZeroCounter, Counter& accumulateCounter, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	JobDeclParallelFor jobDecl;
	BuildParallelForJobWithEpilogue< TMemoryPool, TFunc >( jobDecl, instrumentationObject, numElements.GetValue(), std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
	::job::RunParallelForJob( jobDecl, waitForZeroCounter, accumulateCounter );
}

template< typename TMemoryPool, typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc >
RED_INLINE void DispatchParallelForJobWithEpilogue( red::InstrumentationObject& instrumentationObject, const Counter& waitForZeroCounter, Counter& accumulateCounter, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	// mutable for std::move()
	auto continuationJob =
		[ numElementsFunc = std::forward<TNumElementsFunc>( numElementsFunc ), func = std::forward<TFunc>( func ), epilogueFunc = std::forward<TEpilogueFunc>( epilogueFunc ) ]
	( const job::RunContext& runContext ) mutable
	{
		static_assert( !std::is_reference<decltype( numElementsFunc )>::value, "" );
		static_assert( !std::is_reference<decltype( func )>::value, "" );
		static_assert( !std::is_reference<decltype( epilogueFunc )>::value, "" );

		const Uint32 numElements = numElementsFunc();

		JobDeclParallelFor jobDecl;
		auto* instrumentationObject = runContext.continuationContext.instrumentationObject;
		RED_FATAL_ASSERT( instrumentationObject );
		BuildParallelForJobWithEpilogue< TMemoryPool, TFunc >( jobDecl, *instrumentationObject, numElements, std::move( func ), std::move( epilogueFunc ) );

		Counter noWait{ runContext.continuationContext.param };
		Counter& continuationCounter = *runContext.continuationContext.counter;

		::job::RunParallelForJob( jobDecl, noWait, continuationCounter );
	};

	::job::DispatchJob< TMemoryPool >( instrumentationObject, waitForZeroCounter, accumulateCounter, continuationJob );
}

///---

// Note: can use ScopedProfilerChannel instead of specifying channel for each job

template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TFunc, size_t N >
RED_INLINE void DispatchJob( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, TFunc&& func )
{
	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName, channel );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	DispatchJob< TMemoryPool >( s_instrumentationObject, waitForZeroCounter, accumulateCounter, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TFunc, size_t N >
RED_INLINE void DispatchParallelForJob( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, ImmediateValue numElements, TFunc&& func )
{
	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName, channel );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	DispatchParallelForJob< TMemoryPool >( s_instrumentationObject, waitForZeroCounter, accumulateCounter, numElements, std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TNumElementsFunc, typename TFunc, size_t N >
RED_INLINE void DispatchParallelForJob( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, TNumElementsFunc&& numElementsFunc, TFunc&& func )
{
	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName, channel );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	DispatchParallelForJob< TMemoryPool >( s_instrumentationObject, waitForZeroCounter, accumulateCounter, std::forward< TNumElementsFunc >( numElementsFunc ), std::forward< TFunc >( func ) );
}

template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TFunc, typename TEpilogueFunc, size_t N >
RED_INLINE void DispatchParallelForJobWithEpilogue( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName, channel );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	DispatchParallelForJobWithEpilogue< TMemoryPool >( s_instrumentationObject, waitForZeroCounter, accumulateCounter, numElements, std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
}

template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TNumElementsFunc, typename TFunc, typename TEpilogueFunc, size_t N >
RED_INLINE void DispatchParallelForJobWithEpilogue( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, TNumElementsFunc&& numElementsFunc, TFunc&& func, TEpilogueFunc&& epilogueFunc )
{
	// separate for every instantiation of the template, which is precisely what we need
	static red::InstrumentationObject s_instrumentationObject( debugName, channel );
	prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

	DispatchParallelForJobWithEpilogue< TMemoryPool >( s_instrumentationObject, waitForZeroCounter, accumulateCounter, std::forward< TNumElementsFunc >( numElementsFunc ), std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
}

/// ---

namespace ext
{
	template< typename TMemoryPool, typename TFunc, typename TEpilogueFunc >
	RED_INLINE void DispatchParallelForJobWithEpilogueWithBatchSize(
		red::InstrumentationObject& instrumentationObject,
		const Counter& waitForZeroCounter,
		Counter& accumulateCounter,
		ImmediateValue numElements,
		TFunc&& func,
		TEpilogueFunc&& epilogueFunc,
		Uint32 maxBatchSize )
	{
		JobDeclParallelFor jobDecl;
		BuildParallelForJobWithEpilogue< TMemoryPool, TFunc >(
			jobDecl,
			instrumentationObject,
			numElements.GetValue(),
			std::forward< TFunc >( func ),
			std::forward< TEpilogueFunc >( epilogueFunc ) );
		jobDecl.maxBatchSize = maxBatchSize;
		::job::RunParallelForJob(
			jobDecl,
			waitForZeroCounter,
			accumulateCounter );
	}

	template< job::JobHint hint, typename TMemoryPool, typename TFunc, size_t N >
	RED_INLINE void DispatchJobWithHint( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, TFunc&& func )
	{
		JobDecl jobDecl;
		BuildJob< TMemoryPool >( jobDecl, debugName, std::forward< TFunc >( func ) );
		jobDecl.hint = hint;

		::job::RunJob( jobDecl, waitForZeroCounter, accumulateCounter );
	}

	template< typename TMemoryPool, EProfilerBlockChannel channel = PBC_NONE, typename TFunc, typename TEpilogueFunc, size_t N >
	RED_INLINE void DispatchParallelForJobWithEpilogueWithBatchSize( StringLiteralConstRef< N > debugName, const Counter& waitForZeroCounter, Counter& accumulateCounter, ImmediateValue numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc, Uint32 maxBatchSize )
	{
		// separate for every instantiation of the template, which is precisely what we need
		static red::InstrumentationObject s_instrumentationObject( debugName, channel );
		prv::VerifyInstrumentationObjectDebugName( s_instrumentationObject, debugName );

		JobDeclParallelFor jobDecl;
		BuildParallelForJobWithEpilogue< TMemoryPool, TFunc >( jobDecl, s_instrumentationObject, numElements.GetValue(), std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) );
		jobDecl.maxBatchSize = maxBatchSize;
		::job::RunParallelForJob( jobDecl, waitForZeroCounter, accumulateCounter );
	}
}

} // job
