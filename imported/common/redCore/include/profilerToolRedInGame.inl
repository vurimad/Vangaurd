/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
#ifdef USE_RED_INGAME_PROFILER

#ifdef MEASURE_INGAME_PROFILER_OVERHEAD
	struct RED_ALIGN(64) ProfilerOverheadTimerStruct
	{
		atomic::TAtomic64 m_overhead;
	};
	extern ProfilerOverheadTimerStruct	gProfilerOverhead[32];

	struct ProfilerOverheadTimer
	{
		ProfilerOverheadTimer()
		{
			m_start = Timer::GetTicks();
		}

		~ProfilerOverheadTimer()
		{
			Uint64 end = Timer::GetTicks();

			Uint64 diff = end - m_start;

			atomic::ExchangeAdd64( &gProfilerOverhead[RedInGameProfilerTool::GetProfilerThreadIndex() + 1].m_overhead, diff );
		}

		Uint64 m_start;
	};

	#define ProfileInGameProfilerOverhead()		ProfilerOverheadTimer timer;
#else
	#define ProfileInGameProfilerOverhead()
#endif

	extern RED_TLS Int32 sm_profilerThreadIndex;

	RED_INLINE void RedInGameProfilerTool::Samples::Init( Sample* memory, Uint32 numSamples )
	{
		m_samples = static_cast< Sample* >( memory );
		m_numSamples = numSamples;
	}


	RED_INLINE RedInGameProfilerTool::Sample& RedInGameProfilerTool::Samples::GetSample( Uint32 sampleIndex )
	{
		return m_samples[sampleIndex];
	}


	RED_INLINE const RedInGameProfilerTool::Sample& RedInGameProfilerTool::Samples::GetSample( Uint32 sampleIndex ) const
	{
		return m_samples[sampleIndex];
	}

	RED_INLINE void RedInGameProfilerTool::SampleAllocator::Init( Uint32 poolSize )
	{
		RED_ASSERT( red::IsPowerOf2( poolSize ) );

		m_poolSize = poolSize;
		m_mask = poolSize - 1;

		m_writePtr = 0;
		m_readPtr = 0;
	}


	RED_INLINE Uint32 RedInGameProfilerTool::SampleAllocator::PreAllocate()
	{
		Uint32 readPtr = m_readPtr;
		Uint32 writePtr = m_writePtr;

		// "-" here so even if the indices wrap, it all should be fine
		if ( writePtr - readPtr >= m_poolSize - 1 )
		{
			return kInvalidSampleIndex;
		}

		// barrier here to ensure that mem access from BELOW gets moved *before* the check
		// technically it could happen that:
		// a) writes to allocated sample
		// b) reads the data from the end of the queue
		// b) modifies m_readPtr after reading, actually freeing space to write
		// a) does the check for empty space which succeeds
		// and we want to be sure that we dont start writing on (a) until (b) is done reading
		RED_THREADS_MEMORY_BARRIER();

		return writePtr & m_mask;
	}


	RED_INLINE void RedInGameProfilerTool::SampleAllocator::CommitAlloc( Uint32 alloc )
	{
		Uint32 writePtr = m_writePtr;

	#ifdef INGAME_PROFILER_ASSERTS
		RED_ASSERT( alloc == ( writePtr & m_mask ) );
	#endif

		// barrier to ensure that the reading thread doesn't see the data
		// (so the moved writePtr) until they are actually written
		// we need to make sure that modification of writePtr happens
		// *after* after the actual data is written to mem
		RED_THREADS_MEMORY_BARRIER();

		m_writePtr = writePtr + 1;
	}


	RED_INLINE Uint32 RedInGameProfilerTool::SampleAllocator::FindLastSample( const Samples &samples, Uint32 rangeStart, Uint32 rangeEnd, Uint64 time ) const
	{
		Int32 rangeLength = rangeEnd - rangeStart;

		while( rangeLength > 0 )
		{
			Int32 rangeHalf = rangeLength / 2;
			const Sample &middleSample = samples.GetSample( ToSampleIndex( rangeStart + rangeHalf ) );

			if ( middleSample.m_end < time )
			{
				rangeStart += rangeHalf + 1;
				rangeLength -= rangeHalf + 1;
			}
			else
			{
				rangeLength = rangeHalf;
			}
		}

		return rangeStart;
	}


	RED_INLINE void RedInGameProfilerTool::SampleAllocator::Free( const Samples &samples, Uint64 endTime )
	{
		Uint32 searchStart = m_readPtr;
		Uint32 searchEnd = m_writePtr;

		Uint32 newReadPtr = FindLastSample( samples, searchStart, searchEnd, endTime );

		// we dont any reads from *above* to accidently go behind that modification of
		// the m_readPtr
		RED_THREADS_MEMORY_BARRIER();

		m_readPtr = newReadPtr;
	}

	RED_INLINE Uint32 RedInGameProfilerTool::SampleAllocator::CopySamples( const Samples &samples, Uint64 endTime, Sample *dest0, Uint32 dest0Size, Sample *dest1, Uint32 dest1Size ) const
	{
		Uint32 searchStart = m_readPtr;
		Uint32 searchEnd = m_writePtr;

		Uint32 firstSampleToCopy = searchStart;
		Uint32 lastSampleToCopy = FindLastSample( samples, searchStart, searchEnd, endTime );

		Uint32 firstSampleToCopyIndex = ToSampleIndex( firstSampleToCopy );
		Uint32 lastSampleToCopyIndex = ToSampleIndex( lastSampleToCopy );

		Sample *destBuffers[2] = { dest0, dest1 };
		Uint32 destBufferSizes[2] = { dest0Size, dest1Size };
		Uint32 currDestBuffer = 0;

		Sample *srcBuffers[2];
		Uint32 srcBufferSizes[2];
		Uint32 currSrcBuffer = 0;

		if ( lastSampleToCopyIndex >= firstSampleToCopyIndex )
		{
			srcBuffers[0] = samples.m_samples + firstSampleToCopyIndex;
			srcBufferSizes[0] = lastSampleToCopyIndex - firstSampleToCopyIndex;

			srcBuffers[1] = nullptr;
			srcBufferSizes[1] = 0;
		}
		else
		{
			srcBuffers[0] = samples.m_samples + firstSampleToCopyIndex;
			srcBufferSizes[0] = m_poolSize - firstSampleToCopyIndex;

			srcBuffers[1] = samples.m_samples;
			srcBufferSizes[1] = lastSampleToCopyIndex;
		}

		RED_ASSERT( ( srcBufferSizes[0] + srcBufferSizes[1] ) == ( lastSampleToCopy - firstSampleToCopy ) );

		Uint32 numSamplesCopied = 0;

		while( ( currSrcBuffer < 2 ) && ( currDestBuffer < 2 ) )
		{
			if ( ( srcBuffers[currSrcBuffer] == nullptr ) || ( destBuffers[currDestBuffer] == nullptr ) )
			{
				break;
			}

			Uint32 batchSize = Min( srcBufferSizes[currSrcBuffer], destBufferSizes[currDestBuffer] );

			if ( batchSize > 0 )
			{
				Memcpy( destBuffers[currDestBuffer], srcBuffers[currSrcBuffer], batchSize * sizeof( Sample ) );
				numSamplesCopied += batchSize;
			}

			srcBuffers[currSrcBuffer] += batchSize;
			srcBufferSizes[currSrcBuffer] -= batchSize;

			destBuffers[currDestBuffer] += batchSize;
			destBufferSizes[currDestBuffer] -= batchSize;

			if ( srcBufferSizes[currSrcBuffer] == 0 )
			{
				++currSrcBuffer;
			}

			if ( destBufferSizes[currDestBuffer] == 0 )
			{
				++currDestBuffer;
			}
		}

		return numSamplesCopied;
	}


	RED_INLINE Uint32 RedInGameProfilerTool::SampleAllocator::ToSampleIndex( Uint32 alloc ) const
	{
		return alloc & m_mask;
	}


	void RedInGameProfilerTool::SampleStack::Init()
	{
		static_assert( sizeof( m_stackMem ) == ( kMaxSampleDepth * sizeof( Sample ) ), "Stack mem size should match the SampleSize * stack depth" );
		m_stackSamples.Init( reinterpret_cast< Sample* >( m_stackMem ), kMaxSampleDepth );

		m_top = 0;
		m_overflowTop = 0;
	}

	RED_INLINE RedInGameProfilerTool::Sample& RedInGameProfilerTool::SampleStack::GetTopSample()
	{
	#ifdef INGAME_PROFILER_ASSERTS
		RED_ASSERT( m_top > 0 );
	#endif
		return m_stackSamples.m_samples[m_top - 1];
	}

	RED_INLINE Uint32 RedInGameProfilerTool::SampleStack::GetSize() const
	{
		return m_top;
	}


	RED_INLINE bool RedInGameProfilerTool::SampleStack::Overflow() const
	{
		return m_overflowTop > 0;
	}

	RED_INLINE Uint32 RedInGameProfilerTool::SampleStack::PushSample()
	{
		if ( m_top >= kMaxSampleDepth )
		{
			++m_overflowTop;
			return kInvalidSampleIndex;
		}

		Uint32 retVal = m_top;
		++m_top;

		return retVal;
	}


	RED_INLINE void	RedInGameProfilerTool::SampleStack::PopSample()
	{
		if ( m_overflowTop > 0 )
		{
			--m_overflowTop;
		}
		else
		{
		#ifdef INGAME_PROFILER_ASSERTS
			RED_ASSERT(  m_top > 0 );
		#endif
			--m_top;
		}
	}


	Uint32 RedInGameProfilerTool::GetProfilerThreadIndex()
	{
		return sm_profilerThreadIndex;
	}


	void RedInGameProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
	{
		ProfileInGameProfilerOverhead();

		Uint32 profilerThread = GetProfilerThreadIndex();
		if ( profilerThread == kProfilerThreadInactive )
		{
			return;
		}

		ThreadData &threadData = m_threads[profilerThread];

		Uint32 sampleIndex = threadData.m_sampleStack.PushSample();

		if ( sampleIndex != kInvalidSampleIndex )
		{
			Sample &sample = threadData.m_sampleStack.GetTopSample();

			sample.m_end	= ~0ULL;
			sample.m_block	= block;
			sample.m_scopeName = scopeName;
			sample.m_thread	= static_cast< Uint8 >( profilerThread );
			sample.m_color	= SelectMarkerColor( block, scopeName );
			sample.m_level	= threadData.m_sampleStack.GetSize() - 1;
			sample.m_type   = SampleType::Regular;
			sample.m_subType= SampleType::None;
			sample.m_start	= red::Timer::GetTicks();
		}
	}
	
	void RedInGameProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
	{
		ProfileInGameProfilerOverhead();

		Uint32 profilerThread = GetProfilerThreadIndex();
		if ( profilerThread == kProfilerThreadInactive )
		{
			return;
		}

		ThreadData &threadData = m_threads[profilerThread];

		if ( !threadData.m_sampleStack.Overflow() )
		{
			Uint32 finishedSampleIndex = threadData.m_samplesAllocator.PreAllocate();

			if ( finishedSampleIndex != kInvalidSampleIndex )
			{
				const Sample &stackTopSample = threadData.m_sampleStack.GetTopSample();
				Sample &finishedSample = threadData.m_samples.GetSample( threadData.m_samplesAllocator.ToSampleIndex( finishedSampleIndex ) );

				// copy the whole thing and override the m_end instead of field by field so that the generatated code is simpler
				finishedSample = stackTopSample;
				finishedSample.m_end = red::Timer::GetTicks();

				threadData.m_samplesAllocator.CommitAlloc( finishedSampleIndex );
			}
		}

		threadData.m_sampleStack.PopSample();
	}

	RED_FORCE_INLINE void SetSampleDefaults( red::RedInGameProfilerTool::Sample* sample, red::InstrumentationObject* block, SampleType::Main type, Uint32 profilerThread )
	{
		sample->m_start = red::Timer::GetTicks();
		sample->m_end = sample->m_start;
		sample->m_block = block;
		sample->m_thread = static_cast<Uint8>(profilerThread);
		sample->m_color = math::Color( block->m_profilerColor );
		sample->m_level = 1;
		sample->m_type = type;
		sample->m_subType = SampleType::None;
	}

	void RedInGameProfilerTool::PutSyncPoint( red::InstrumentationObject* block )
	{
		ProfileInGameProfilerOverhead();

		Uint32 profilerThread = GetProfilerThreadIndex();
		if ( profilerThread == kProfilerThreadInactive )
		{
			return;
		}

		ThreadData &threadData = m_threads[profilerThread];

		const Uint32 sampleIndex = threadData.m_samplesAllocator.PreAllocate();
		if ( sampleIndex != kInvalidSampleIndex )
		{
			Sample& sample = threadData.m_samples.GetSample( threadData.m_samplesAllocator.ToSampleIndex( sampleIndex ) );
			SetSampleDefaults( &sample, block, SampleType::Point, profilerThread );
			
			threadData.m_samplesAllocator.CommitAlloc( sampleIndex );
		}
	}

	void RedInGameProfilerTool::BeginGroup( red::InstrumentationObject* block )
	{
		Uint32 profilerThread = GetProfilerThreadIndex();
		if ( profilerThread == kProfilerThreadInactive )
		{
			return;
		}

		ThreadData &threadData = m_threads[profilerThread];

		const Uint32 sampleIndex = threadData.m_samplesAllocator.PreAllocate();
		if ( sampleIndex != kInvalidSampleIndex )
		{
			Sample& sample = threadData.m_samples.GetSample( threadData.m_samplesAllocator.ToSampleIndex( sampleIndex ) );
			SetSampleDefaults( &sample, block, SampleType::Group, profilerThread );
			sample.m_subType = SampleType::Begin;

			threadData.m_samplesAllocator.CommitAlloc( sampleIndex );
		}
	}

	void RedInGameProfilerTool::EndGroup( red::InstrumentationObject* block )
	{
		Uint32 profilerThread = GetProfilerThreadIndex();
		if ( profilerThread == kProfilerThreadInactive )
		{
			return;
		}

		ThreadData &threadData = m_threads[profilerThread];

		const Uint32 sampleIndex = threadData.m_samplesAllocator.PreAllocate();
		if ( sampleIndex != kInvalidSampleIndex )
		{
			Sample& sample = threadData.m_samples.GetSample( threadData.m_samplesAllocator.ToSampleIndex( sampleIndex ) );
			SetSampleDefaults( &sample, block, SampleType::Group, profilerThread );
			sample.m_subType = SampleType::End;

			threadData.m_samplesAllocator.CommitAlloc( sampleIndex );
		}
	}

#endif
}


