/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/function.h"

#include "jobPriority.h"
#include "jobCounterOwner.h"
#include "jobDeferral.h"

namespace job
{
	class Counter;
	struct JobDecl;
	struct JobDeclParallelFor;

	REDJOBS2_API void RunJob( const JobDecl& job, const Counter& waitForZeroCounter, Counter& accumulateCounter );

	REDJOBS2_API void RunParallelForJob( const JobDeclParallelFor& job, const Counter& waitForZeroCounter, Counter& accumulateCounter);

	REDJOBS2_API CompletionDeferral CreateDeferral( const char* debugName, const void* debugUserData, prv::CounterEntry& counter );

	// #todo: remove const!
	REDJOBS2_API Bool FlushCounter( const Counter& counter, Bool processLatent = false, Int32 timeoutMillseconds = -1 );
	REDJOBS2_API Bool FlushCounter( Counter&& counter, Bool processLatent = false, Int32 timeoutMillseconds = -1 );


	REDJOBS2_API Bool FlushCounterOnProcessFrame( const Counter& counter );

	// Emits RED's debugger analysis for this counter when the job debugger is
	// compiled and enabled.
	REDJOBS2_API void AnalyzeCounter( const Counter& counter );
}

