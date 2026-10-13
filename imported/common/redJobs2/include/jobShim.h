/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "jobDecl.h"

namespace io
{
	enum EAsyncPriority : Uint8;
	REDJOBS2_API EAsyncPriority GetThreadLocalIOPriority();
	REDJOBS2_API void SetThreadLocalIOPriority( EAsyncPriority prio );
}

namespace job { namespace prv {

template< typename TFunc, typename TMemoryPool >
class JobShim : red::NonCopyable
{
public:
	JobShim( TFunc&& func )
		: m_func( std::forward< TFunc >( func ) )
	{
	}

	static void RunJob( void* jobData, const RunContext& runContext )
	{
		auto* const shim = static_cast<JobShim*>( jobData );
		RED_FATAL_ASSERT( shim );
		io::SetThreadLocalIOPriority( runContext.continuationContext.param.ioPriority ); 
		shim->m_func( runContext );
		io::SetThreadLocalIOPriority( io::EAsyncPriority::eAsyncPriority_INVALID );
		RED_DELETE( shim, TMemoryPool );
	}

private:
	typename red::RemoveCVRef< TFunc >::Type m_func;
};

template< typename TFunc, typename TEpilogueFunc >
struct ParallelForFuncs
{
	ParallelForFuncs( TFunc&& func, TEpilogueFunc&& epilogueFunc )
		: m_func( std::forward< TFunc >( func ) )
		, m_epilogueFunc( std::forward< TEpilogueFunc >( epilogueFunc ) )
	{
	}

	static void InvokeFunc( ParallelForFuncs& self, Uint32 index, const RunContext& runContext )
	{
		self.m_func( index, runContext );
	}

	static void InvokeEpilogue( ParallelForFuncs& self, const RunContext& runContext )
	{
		self.m_epilogueFunc( runContext );
	}

	typename red::RemoveCVRef< TFunc >::Type m_func;
	typename red::RemoveCVRef< TEpilogueFunc >::Type m_epilogueFunc;
};

template< typename TFunc >
struct ParallelForFuncs< TFunc, void >
{
	ParallelForFuncs( TFunc&& func )
		: m_func( std::forward< TFunc >( func ) )
	{
	}

	static void InvokeFunc( ParallelForFuncs& self, Uint32 index, const RunContext& runContext )
	{
		self.m_func( index, runContext );
	}

	static void InvokeEpilogue( ParallelForFuncs&, const RunContext& )
	{
		// nothing to do
	}

	typename red::RemoveCVRef< TFunc >::Type m_func;
};

template< typename TMemoryPool, typename TFunc, typename TEpilogueFunc = void >
class ParallelForJobShim : red::NonCopyable
{
public:
	using TParallelForFuncs = ParallelForFuncs< TFunc, TEpilogueFunc >;

	explicit ParallelForJobShim( TParallelForFuncs&& funcs )
		: m_funcs( std::forward< TParallelForFuncs >( funcs ) )
	{
	}

	static void RunParallelForJob( void* sharedData, void* elements, Uint32 elementStartIndex, Uint32 elementEndIndex, const RunContext& runContext )
	{
		RED_UNUSED( elements );
		auto* const shim = static_cast<ParallelForJobShim*>( sharedData );
		RED_FATAL_ASSERT( shim );
		TParallelForFuncs& self = shim->m_funcs;
		io::SetThreadLocalIOPriority( runContext.continuationContext.param.ioPriority ); 
		for ( Uint32 i = elementStartIndex; i < elementEndIndex; ++i )
		{
			TParallelForFuncs::InvokeFunc( self, i, runContext );
		}
		io::SetThreadLocalIOPriority( io::EAsyncPriority::eAsyncPriority_INVALID ); 
	}

	static void EpilogueParallelForJob( void* sharedData, void* elements, Uint32 numElements, const RunContext& runContext )
	{
		RED_UNUSED2( elements, numElements );

		auto* const shim = static_cast<ParallelForJobShim*>( sharedData );

		io::SetThreadLocalIOPriority( runContext.continuationContext.param.ioPriority ); 
		TParallelForFuncs::InvokeEpilogue( shim->m_funcs, runContext );
		io::SetThreadLocalIOPriority( io::EAsyncPriority::eAsyncPriority_INVALID );

		RED_DELETE( shim, TMemoryPool );
	}

private:
	TParallelForFuncs m_funcs;
};

} } // job/prv
