/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace job { namespace prv {

// Added to avoid performance overhead caused by lots of sched_yield system calls on PS4.
#ifdef RED_PLATFORM_ORBIS
	const Int32 c_spinCount = 1000 * 1024;
#else
	const Int32 c_spinCount = 10 * 1024;
#endif

template< typename TEntry >
class LockFreeQueueMPMCExternalBuffer
{
public:
	LockFreeQueueMPMCExternalBuffer()
		: m_entries( nullptr )
		, m_numEntries( 0 )
	{
	}

	void Initialize( red::ArraySpan< TEntry > externalBuffer )
	{
		m_entries = externalBuffer.Data();
		m_numEntries = externalBuffer.Size();
		RED_FATAL_ASSERT( red::IsPowerOf2( m_numEntries ), "Queue size must be a power of two, got %llu", m_numEntries );
	}

	Bool Push( const TEntry& entry )
	{
		const Uint64 writeIndex = m_writeIndex.PostIncrement();

		// No space; queue is full
		while ( writeIndex - m_readIndex.GetValue() >= m_numEntries )
		{
			continue;
		}

		// Fairness for writers, writeIndex basically a ticket
		// Once there's enough space, nothing can steal the writeIndex we took, so enough space guaranteed
		m_entries[ writeIndex & ( m_numEntries - 1 ) ] = entry;

		const Uint64 oldWriteEnd = writeIndex;
		const Uint64 newWriteEnd = writeIndex + 1;

		Uint32 iteration = 0;
		while ( m_comittedWriteEnd.CompareExchange( newWriteEnd, oldWriteEnd ) != oldWriteEnd )
		{
			if ( iteration++ > c_spinCount )
			{
				iteration = 0;
				red::YieldCurrentThread();
			}

			continue;
		}

		return true;
	}

	Bool PushBulk( const red::ArraySpan< const TEntry >& entries )
	{
		Uint64 writeIndex = m_writeIndex.ExchangeAdd( entries.Size() );
		const Uint64 writeIndexEnd = writeIndex + entries.Size();

		Uint32 spanIndex = 0;
		while ( writeIndex < writeIndexEnd )
		{
			// Capture current readIndex, then write as much as possible without exceeding queue size
			for ( const Uint64 readIndex = m_readIndex.GetValue(); ( writeIndex < writeIndexEnd ) && ( writeIndex - readIndex < m_numEntries ); ++writeIndex, ++spanIndex )
			{
				m_entries[ writeIndex & ( m_numEntries - 1 ) ] = entries[ spanIndex ];
			}
		}

		return true;
	}

	Bool Pop( TEntry& outEntry )
	{
		// Retrieval order of comittedWriteEnd and readIndex here doesn't matter for correctness
		// However, getting comittedWriteEnd after since may grow and help with more successful Pop()s
		// Invariant: readIndex must always be <= comittedWriteEnd, no matter what thread
		// comittedWriteEnd and readIndex can only grow: 
		// 1) if m_comittedWriteEnd increases async then local readIndex is still <= m_comittedWriteEnd
		// 2) if m_readIndex increases async then CompareExchange below will fail, so the Pop() will fail
		// For some atomic snapshot m_readIndex <= m_comittedWriteEnd, but getting them separately
		// you will see e.g., readIndex > comittedWriteEnd, but that's a torn update which we guard against
		const Uint64 readIndex = m_readIndex.GetValue();
		const Uint64 comittedWriteEnd = m_comittedWriteEnd.GetValue();

		// Queue is empty (for what's visible to readers)
		if ( readIndex >= comittedWriteEnd )
		{
			return false;
		}

		// Copy before updating readIndex, since could then get overwritten async
		// Don't access: this value could even be overwritten; can't know until after the CompareExchange succeeds
		const TEntry tmpVal = m_entries[ readIndex & (Uint64)( m_numEntries - 1 ) ];

		// No fairness for readers
		const Uint64 oldReadIndex = readIndex;
		const Uint64 newReadIndex = readIndex + 1;
		if ( m_readIndex.CompareExchange( newReadIndex, oldReadIndex ) != oldReadIndex )
		{
			return false;
		}

		outEntry = tmpVal;

		return true;
	}

private:
	static const constexpr Uint32 c_cacheLineSize = 64;

	TEntry* m_entries;
	Uint64 m_numEntries;

	// Aligned so not in the same cacheline as a previous LFQ
	RED_ALIGN( c_cacheLineSize ) red::Atomic< Uint64 > m_readIndex;
	RED_ALIGN( c_cacheLineSize ) red::Atomic< Uint64 > m_comittedWriteEnd;

	// Aligned to separate cacheline, since only updated by producers, not consumers
	RED_ALIGN( c_cacheLineSize ) red::Atomic< Uint64 > m_writeIndex;
};

} } // job/prv
