/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*
* Standalone waiting-list benchmark for REDJobs2.
*
* This file is built by the benchmarkJobs2 console target so it stays out of
* the normal redJobs2 library build.
*/

#include "build.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../../redCore/include/instrumentationObject.h"
#include "../include/jobCounter.h"
#include "../include/jobCounterFunctions.h"
#include "../include/jobDecl.h"
#include "../include/jobDispatcherInitParam.h"
#include "../include/jobMemoryPools.h"
#include "../include/jobSystem.h"

namespace
{
	enum class JobWork
	{
		Noop,
		SharedAtomic,
	};

	struct BenchState
	{
		std::atomic< unsigned long long > completed{ 0 };
	};

	struct Sample
	{
		unsigned long long elapsedMicros = 0;
		unsigned long long completed = 0;
	};

	static red::InstrumentationObject s_jobInstrumentationObject( "waiting_list_bench_job" );

	static void NoopJob( void*, const job::RunContext& )
	{
	}

	static void AtomicJob( void* jobData, const job::RunContext& )
	{
		auto* state = static_cast< BenchState* >( jobData );
		state->completed.fetch_add( 1, std::memory_order_relaxed );
	}

	static const char* JobWorkLabel( JobWork work )
	{
		switch( work )
		{
		case JobWork::Noop:
			return "noop";
		case JobWork::SharedAtomic:
			return "shared_atomic";
		}
		return "unknown";
	}

	static bool VerifiesCompletedCount( JobWork work )
	{
		return work == JobWork::SharedAtomic;
	}

	static job::InitParam MakeInitParam( unsigned threads, unsigned queueCapacity )
	{
		job::InitParam param = job::DefaultToolInitParam();
		param.maxThreads = threads;
		param.maxLatentJobs = queueCapacity;
		param.maxCriticalPathJobs = queueCapacity;
		param.maxImmediateJobs = queueCapacity;
		param.useJobDebugger = false;
		param.allJobsCriticalPath = false;
		return param;
	}

	static job::Counter MakeCounter()
	{
		job::ScheduleParam param;
		param.priority = job::Priority::CriticalPath;
		return job::Counter( param, nullptr );
	}

	static job::JobDecl MakeJobDecl( JobWork work, BenchState& state )
	{
		job::JobDecl jobDecl{};
		jobDecl.jobFunc = work == JobWork::Noop ? &NoopJob : &AtomicJob;
		jobDecl.jobData = &state;
		jobDecl.instrumentationObject = &s_jobInstrumentationObject;
		jobDecl.hint = job::JobHint::None;
		jobDecl.debugFlags = 0;
		return jobDecl;
	}

	static Sample RunScenario(
		unsigned jobsPerRound,
		unsigned rounds,
		bool gated,
		JobWork work,
		BenchState& state )
	{
		state.completed.store( 0, std::memory_order_relaxed );
		const job::JobDecl jobDecl = MakeJobDecl( work, state );

		const auto start = std::chrono::steady_clock::now();
		for( unsigned round = 0; round < rounds; ++round )
		{
			job::Counter accum = MakeCounter();
			job::Counter waitCounter = MakeCounter();
			job::CompletionDeferral deferral;

			if( gated )
			{
				deferral = waitCounter.CreateDeferral( nullptr, "waiting_list_bench_gate" );
			}

			for( unsigned i = 0; i < jobsPerRound; ++i )
			{
				job::RunJob( jobDecl, waitCounter, accum );
			}

			if( gated )
			{
				deferral.FinishDeferral();
			}

			job::FlushCounter( accum, false, -1 );
		}
		const auto end = std::chrono::steady_clock::now();

		Sample sample;
		sample.elapsedMicros = static_cast< unsigned long long >( std::chrono::duration_cast< std::chrono::microseconds >( end - start ).count() );
		sample.completed = state.completed.load( std::memory_order_relaxed );
		return sample;
	}

	static unsigned long long BestMicros( const std::vector< Sample >& samples )
	{
		unsigned long long best = samples.empty() ? 0 : samples.front().elapsedMicros;
		for( const Sample& sample : samples )
		{
			best = std::min( best, sample.elapsedMicros );
		}
		return best;
	}

	static unsigned long long MedianMicros( const std::vector< Sample >& samples )
	{
		std::vector< unsigned long long > values;
		values.reserve( samples.size() );
		for( const Sample& sample : samples )
		{
			values.push_back( sample.elapsedMicros );
		}
		std::sort( values.begin(), values.end() );
		return values.empty() ? 0 : values[ ( values.size() - 1 ) / 2 ];
	}

	static double AverageMicros( const std::vector< Sample >& samples )
	{
		double total = 0.0;
		for( const Sample& sample : samples )
		{
			total += static_cast< double >( sample.elapsedMicros );
		}
		return samples.empty() ? 0.0 : total / static_cast< double >( samples.size() );
	}

	static void AssertCompleted( const std::vector< Sample >& samples, unsigned long long expected, const char* label )
	{
		for( const Sample& sample : samples )
		{
			if( sample.completed != expected )
			{
				std::printf( "%s benchmark job count mismatch: expected=%llu completed=%llu\n", label, expected, sample.completed );
				std::abort();
			}
		}
	}

	static void PrintSummary( const char* label, const std::vector< Sample >& samples )
	{
		std::printf(
			"%s: best %.3f ms | median %.3f ms | avg %.3f ms\n",
			label,
			static_cast< double >( BestMicros( samples ) ) / 1000.0,
			static_cast< double >( MedianMicros( samples ) ) / 1000.0,
			AverageMicros( samples ) / 1000.0 );
	}

	static void RunWorkMode(
		JobWork work,
		unsigned jobsPerRound,
		unsigned rounds,
		unsigned warmups,
		unsigned samples,
		unsigned long long expectedJobs )
	{
		BenchState directState;
		BenchState gatedState;

		for( unsigned i = 0; i < warmups; ++i )
		{
			RunScenario( jobsPerRound, rounds, false, work, directState );
			RunScenario( jobsPerRound, rounds, true, work, gatedState );
		}

		std::vector< Sample > directSamples;
		std::vector< Sample > gatedSamples;
		directSamples.reserve( samples );
		gatedSamples.reserve( samples );

		for( unsigned sampleIndex = 0; sampleIndex < samples; ++sampleIndex )
		{
			if( ( sampleIndex & 1u ) == 0u )
			{
				directSamples.push_back( RunScenario( jobsPerRound, rounds, false, work, directState ) );
				gatedSamples.push_back( RunScenario( jobsPerRound, rounds, true, work, gatedState ) );
			}
			else
			{
				gatedSamples.push_back( RunScenario( jobsPerRound, rounds, true, work, gatedState ) );
				directSamples.push_back( RunScenario( jobsPerRound, rounds, false, work, directState ) );
			}
		}

		if( VerifiesCompletedCount( work ) )
		{
			AssertCompleted( directSamples, expectedJobs, "direct" );
			AssertCompleted( gatedSamples, expectedJobs, "gated" );
		}

		const unsigned long long directBest = BestMicros( directSamples );
		const unsigned long long gatedBest = BestMicros( gatedSamples );
		std::printf( "\nwork: %s\n", JobWorkLabel( work ) );
		PrintSummary( "direct", directSamples );
		PrintSummary( "gated ", gatedSamples );
		std::printf( "gated/direct best: %.2fx\n", static_cast< double >( gatedBest ) / static_cast< double >( directBest ? directBest : 1 ) );
	}
}

int RunWaitingListBench( unsigned jobsPerRound = 32768u, unsigned rounds = 16u, unsigned threads = 4u, unsigned samples = 5u, unsigned warmups = 1u )
{
	const unsigned queueCapacity = red::RoundUpToPowerOf2( jobsPerRound + 1024u );
	const unsigned long long expectedJobs = static_cast< unsigned long long >( jobsPerRound ) * static_cast< unsigned long long >( rounds );

	std::printf( "REDJobs2 waiting-list benchmark\n" );
	std::printf( "threads=%u jobs/round=%u rounds=%u queueCapacity=%u\n", threads, jobsPerRound, rounds, queueCapacity );
	std::printf( "warmups=%u samples=%u\n", warmups, samples );
	std::printf( "expected jobs=%llu\n", expectedJobs );

	red::memory::RegisterCurrentThread( "Main Thread" );
	red::profiler::InitInGameProfiler();
	job::Initialize( MakeInitParam( threads, queueCapacity ) );

	RunWorkMode( JobWork::Noop, jobsPerRound, rounds, warmups, samples, expectedJobs );
	RunWorkMode( JobWork::SharedAtomic, jobsPerRound, rounds, warmups, samples, expectedJobs );

	job::Shutdown();
	job::PoolJobScope::GetAllocator().Uninitialize();
	return 0;
}

#ifdef REDJOBS2_BENCH_STANDALONE
int main( int argc, char** argv )
{
	const unsigned jobsPerRound = ( argc > 1 ) ? static_cast< unsigned >( std::strtoul( argv[1], nullptr, 10 ) ) : 32768u;
	const unsigned rounds = ( argc > 2 ) ? static_cast< unsigned >( std::strtoul( argv[2], nullptr, 10 ) ) : 16u;
	const unsigned threads = ( argc > 3 ) ? static_cast< unsigned >( std::strtoul( argv[3], nullptr, 10 ) ) : 4u;
	const unsigned samples = ( argc > 4 ) ? static_cast< unsigned >( std::strtoul( argv[4], nullptr, 10 ) ) : 5u;
	const unsigned warmups = ( argc > 5 ) ? static_cast< unsigned >( std::strtoul( argv[5], nullptr, 10 ) ) : 1u;
	return RunWaitingListBench( jobsPerRound, rounds, threads, samples, warmups );
}
#endif
