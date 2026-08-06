/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// #fixme: should move into redSystem?
#include "../../redCore/include/instrumentationObject.h"

#include "jobDecl.h"
#include "jobShim.h"
#include "jobMemoryPools.h"
#include <functional>

namespace job
{

namespace prv
{
	template< typename T, typename U = void >
	struct IsFunctionTrait
	{
		static const constexpr Bool Value = false;
	};

	template< typename U >
	struct IsFunctionTrait< std::function< U > >
	{
		static const constexpr Bool Value = true;
	};

	///---

	RED_INLINE void VerifyInstrumentationObjectDebugName( red::InstrumentationObject& instrumentationObject, const char* debugName )
	{
		// Yes, check address not strcmp.
		RED_FATAL_ASSERT( instrumentationObject.m_name == debugName,
						  "InstrumentationObject '%hs' reused for different debugName '%hs'.\n"
						  "Consider using the explicit instrumentationObject vs debugName version",
						  instrumentationObject.m_name,
						  debugName );

		RED_TOUCH2( instrumentationObject, debugName );
	}

	///---

	template< typename TMemoryPool, typename TJobShim, typename TFunc >
	RED_INLINE void InitJobDecl( JobDecl& outJobDecl, red::InstrumentationObject& instrumentationObject, TFunc&& func )
	{
		static_assert( !std::is_same< TMemoryPool, PoolJobs2Data >::value || sizeof( TJobShim ) <= 512, "Job Allocator allow only 512 user data." );

		outJobDecl.jobFunc = &TJobShim::RunJob;
		outJobDecl.jobData = RED_NEW( TJobShim, TMemoryPool )( std::forward< TFunc >( func ) );
		outJobDecl.instrumentationObject = &instrumentationObject;
	}

	template< typename TMemoryPool, typename TJobShim, typename TFunc >
	RED_INLINE void InitJobDeclParallelFor( JobDeclParallelFor& outJobDecl, red::InstrumentationObject& instrumentationObject, Uint32 numElements, TFunc&& func )
	{
		static_assert(!std::is_same< TMemoryPool, PoolJobs2Data >::value || sizeof( TJobShim ) <= 512, "Job Allocator allow only 512 user data.");

		using TParallelForFuncs = typename TJobShim::TParallelForFuncs;
		outJobDecl.jobFunc = &TJobShim::RunParallelForJob;
		outJobDecl.epilogueFunc = &TJobShim::EpilogueParallelForJob;
		outJobDecl.sharedData = RED_NEW( TJobShim, TMemoryPool )( TParallelForFuncs( std::forward< TFunc >( func ) ) );
		outJobDecl.elements = nullptr;
		outJobDecl.numElements = numElements;
		outJobDecl.instrumentationObject = &instrumentationObject;
	}

	template< typename TMemoryPool, typename TJobShim, typename TFunc, typename TEpilogueFunc >
	RED_INLINE void InitJobDeclParallelFor( JobDeclParallelFor& outJobDecl, red::InstrumentationObject& instrumentationObject, Uint32 numElements, TFunc&& func, TEpilogueFunc&& epilogueFunc )
	{
		static_assert(!std::is_same< TMemoryPool, PoolJobs2Data >::value || sizeof( TJobShim ) <= 512, "Job Allocator allow only 512 user data." );

		using TParallelForFuncs = typename TJobShim::TParallelForFuncs;
		outJobDecl.jobFunc = &TJobShim::RunParallelForJob;
		outJobDecl.epilogueFunc = &TJobShim::EpilogueParallelForJob;
		outJobDecl.sharedData = RED_NEW( TJobShim, TMemoryPool )( TParallelForFuncs( std::forward< TFunc >( func ), std::forward< TEpilogueFunc >( epilogueFunc ) ) );
		outJobDecl.elements = nullptr;
		outJobDecl.numElements = numElements;
		outJobDecl.instrumentationObject = &instrumentationObject;
	}
}

} // job
