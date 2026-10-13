/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../redSystem/include/timer.h"
#include "../../redSystem/include/threads.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/clock.h"
#include "../../redIO/include/redIOPublic.h"

#include "profilerToolRedInGame.h"

#ifdef USE_RED_INGAME_PROFILER

namespace red
{
	RedInGameProfilerTool REDCORE_API gRedInGameProfilerTool;

#ifdef MEASURE_INGAME_PROFILER_OVERHEAD
	ProfilerOverheadTimerStruct	gProfilerOverhead[32];

	void ProfilerInGameProfilerOverheadTick()
	{
		Uint64 overheadVal = 0;

		for( Uint32 i=0; i<32; ++i )
		{
			Uint64 threadVal = atomic::Exchange64( &gProfilerOverhead[i].m_overhead, 0 );
			overheadVal += threadVal;
		}

		char buffer[128];
		sprintf_s( buffer, "prof overhead %llu ticks\n", overheadVal );
		printf( "%s", buffer );
	}
#else
	#define ProfilerInGameProfilerOverheadTick()
#endif

	RED_TLS Int32 sm_profilerThreadIndex = RedInGameProfilerTool::kProfilerThreadInactive;

	RedInGameProfilerTool::RedInGameProfilerTool()
		: m_initialized( false )
	{
	}

	RedInGameProfilerTool::~RedInGameProfilerTool()
	{
	}

	void RedInGameProfilerTool::Init( const Uint32 mem )
	{
		m_threadCount.SetValue( 0 );
		m_frames.Init();

		Uint32 inGameProfMem = mem;

		m_memory = RED_ALLOCATE_ALIGNED( red::PoolDebug, inGameProfMem, 64 );
		red::memory::StaticLinearAllocatorParameter params = { m_memory,
															   inGameProfMem };

		m_allocator.Initialize( params );

		m_initialized = true;
	}

	void RedInGameProfilerTool::Shutdown()
	{
		RED_FREE( red::PoolDebug, m_memory );
	}

	void RedInGameProfilerTool::Update()
	{
	}

	void RedInGameProfilerTool::InitThread( const AnsiChar *name, Uint32 maxNumSamples )
	{
		RED_ASSERT( m_initialized, "In-game profiler not initilized - please call red::profiler::InitInGameProfiler()" );
		RED_ASSERT( m_threadCount.GetValue() < kMaxThreadCount );

		Sample *threadSamples = AllocateThreadMem( maxNumSamples * sizeof( Sample ) );
		RED_ASSERT( threadSamples != nullptr );

		Uint32 threadIndex = m_threadCount.Increment() - 1;

		red::Strcpy( m_threads[threadIndex].m_name, name, sizeof( m_threads[threadIndex].m_name ) );
		m_threads[threadIndex].m_sampleStack.Init();
		m_threads[threadIndex].m_samples.Init( threadSamples, maxNumSamples );
		m_threads[threadIndex].m_samplesAllocator.Init( maxNumSamples );

		sm_profilerThreadIndex = threadIndex;
	}


	RedInGameProfilerTool::Sample* RedInGameProfilerTool::AllocateThreadMem( Uint32 size )
	{
		return reinterpret_cast< Sample* >( m_allocator.AllocateAligned( size, 64 ).address );
	}


	void RedInGameProfilerTool::NextFrame( red::ProfilerFrameType frameType )
	{
		if ( frameType == PFT_ENGINE )
		{
			m_frames.NextFrame();

			ProfilerInGameProfilerOverheadTick();
		}
	}


	void RedInGameProfilerTool::FreeFrame()
	{
		Uint64 lastTime = m_frames.FreeFrame();

		if ( lastTime != 0 )
		{
			Uint32 numThreads = m_threadCount.GetValue();

			for( Uint32 i=0; i<numThreads; ++i )
			{
				m_threads[i].m_samplesAllocator.Free( m_threads[i].m_samples, lastTime );
			}
		}
	}

	Uint32 RedInGameProfilerTool::GetNumFramesReady() const
	{
		return m_frames.GetNumFrames();
	}


	void RedInGameProfilerTool::GetLastFrame( Uint64 &startTime, Uint64 &endTime ) const
	{
		m_frames.GetLastFrame( startTime, endTime );
	}


	Uint32 RedInGameProfilerTool::CopyNewSamples( Uint64 endTime, Sample *dest0, Uint32 dest0Size, Sample *dest1, Uint32 dest1Size )
	{
		Uint32 numSamplesCopied = 0;

		if ( endTime != 0 )
		{
			Uint32 numThreads = m_threadCount.GetValue();

			for( Uint32 i=0; i<numThreads; ++i )
			{				
				Uint32 numCopied = m_threads[i].m_samplesAllocator.CopySamples( m_threads[i].m_samples, endTime, dest0, dest0Size, dest1, dest1Size );
				numSamplesCopied += numCopied;

				Uint32 batch0Size = Min( numCopied, dest0Size );
				Uint32 batch1Size = Min( numCopied - batch0Size, dest1Size );

				RED_ASSERT( dest0Size >= batch0Size );
				RED_ASSERT( dest1Size >= batch1Size );

				dest0 += batch0Size;
				dest0Size -= batch0Size;

				dest1 += batch1Size;
				dest1Size -= batch1Size;

				if ( dest0Size == 0 )
				{
					dest0Size = dest1Size;
					dest0 = dest1;

					dest1Size = 0;
					dest1 = nullptr;
				}

				if ( dest0Size == 0 )
				{
					break;
				}
			}
		}

		return numSamplesCopied;
	}


	void RedInGameProfilerTool::Start()
	{
	}

	void RedInGameProfilerTool::Stop()
	{
	}
	   
	void RedInGameProfilerTool::Frames::Init()
	{
		m_readPtr = 0;
		m_writePtr = 0;

		m_frames[0].m_start = red::Timer::GetTicks();
		m_frames[0].m_end = ~0ULL;
	}


	void RedInGameProfilerTool::Frames::NextFrame()
	{
		Uint32 readPtr = m_readPtr;
		Uint32 writePtr = m_writePtr;

		m_frames[writePtr & kFrameIndexMask].m_end = red::Timer::GetTicks();			

		Uint32 nextWritePtr = writePtr + 1;

		if ( writePtr - readPtr >= kMaxFrameCount - 1 )
		{
			// out of mem in the frame buffer, keep reusing current frame
			nextWritePtr = writePtr;
		}

		RED_THREADS_MEMORY_BARRIER();

		m_frames[nextWritePtr & kFrameIndexMask].m_start = red::Timer::GetTicks();
		m_frames[nextWritePtr & kFrameIndexMask].m_end = ~0ULL;

		RED_THREADS_MEMORY_BARRIER();

		m_writePtr = nextWritePtr;
	}

	Uint64 RedInGameProfilerTool::Frames::FreeFrame()
	{
		Uint32 readPtr = m_readPtr;
		Uint32 writePtr = m_writePtr;

		if ( writePtr - readPtr > 0 )
		{
			RED_THREADS_MEMORY_BARRIER();

			Uint64 lastTime = m_frames[readPtr & kFrameIndexMask].m_end;
			m_readPtr = readPtr + 1;

			RED_THREADS_MEMORY_BARRIER();

			return lastTime;
		}

		return 0;
	}

	Uint32 RedInGameProfilerTool::Frames::GetNumFrames() const
	{
		Uint32 numFrames = m_writePtr - m_readPtr;

		// barrier here to make sure mem access from below doesnt get moved above
		// we dont want any reads to happen befer we are sure that there's something to read
		RED_THREADS_MEMORY_BARRIER();

		return numFrames;
	}

	void RedInGameProfilerTool::Frames::GetLastFrame( Uint64 &startTime, Uint64 &endTime ) const
	{
		if ( m_writePtr - m_readPtr > 0 )
		{
			RED_THREADS_MEMORY_BARRIER();

			startTime = m_frames[m_readPtr & kFrameIndexMask].m_start;
			endTime = m_frames[m_readPtr & kFrameIndexMask].m_end;
		}
		else
		{
			startTime = 0;
			endTime = 0;
		}
	}

}
#else 
	RED_NO_EMPTY_FILE();
#endif
