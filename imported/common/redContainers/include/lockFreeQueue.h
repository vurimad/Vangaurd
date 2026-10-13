/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "fixedBuffer.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redSystem/include/redThreadsThread.h"

 //////////////////////////////////////////////////////////////////////////
 // FIFO, fixed size, lock-free, single-producer & multi-consumer or
 // multi-producer & multi-consumer queue using
 // FixedBuffer internally to store elements.
 // The actual size of the queue is QueueSize - 1.
 // Push/Pop is O( 1 )
 //////////////////////////////////////////////////////////////////////////

namespace red {

enum class LockFreeQueueFeintResult
{
	// A push/pop would have returned false for all threads in this instant
	// However, the next push/pop may still return a different result
	FalseThisInstant,

	// A push/pop would have returned true for some thread; however, possibly still returning false for this calling thread
	TrueForSomeThread,
};

template < typename TElement, Uint32 QueueSize >
class SingleProducerMultipleConsumersLockFreeQueue;

template < typename TElement, Uint32 QueueSize >
class MultipleProducersMultipleConsumersLockFreeQueue;

template <
	typename TElement,
	Uint32 QueueSize, // the actual size of the queue is QueueSize - 1
	template < typename, Uint32 > class TQueueType = MultipleProducersMultipleConsumersLockFreeQueue
>
class LockFreeQueue : NonCopyable
{
public:

	// Get number of elements in the queue
	RED_INLINE Uint32 Size() const { return m_queueImpl.Size(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_queueImpl.DataSize(); }
	// Get maximum number of elements that can be stored in the queue
	RED_INLINE Uint32 Capacity() const { return QueueSize - 1; }
	// Get maximum size of data which can be stored in queue, in bytes
	RED_INLINE Uint32 DataCapacity() const { return (QueueSize - 1) * sizeof(TElement); }
	// Returns true if queue contains 0 elements
	RED_INLINE Bool Empty() const { return m_queueImpl.Empty(); }

	// Add 'element' to the end of the queue
	RED_INLINE Bool Push( const TElement& element ) { return m_queueImpl.Push(element); }
	// Add 'element' to the end of the queue
	RED_INLINE Bool Push( TElement&& element ) { return m_queueImpl.Push(std::forward<TElement>(element)); }
	// Remove and return first element on the queue
	RED_INLINE Bool Pop( TElement& element ) { return m_queueImpl.Pop(element); }

	// Advanced use; see LockFreeQueueFeintResult
	RED_INLINE LockFreeQueueFeintResult FeintPush() const { return m_queueImpl.FeintPush(); }

private:

	TQueueType<TElement, QueueSize> m_queueImpl;
};

template < typename TElement, Uint32 QueueSize >
class BaseLockFreeQueue : NonCopyable
{
public:

	using StorageType = FixedBuffer < QueueSize * sizeof(TElement), __alignof(TElement) >;

	// Get number of elements in the queue
	RED_INLINE Uint32 Size() const { return m_size.GetValue(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_size.GetValue() * sizeof(TElement); }
	// Returns true if queue contains 0 elements
	RED_INLINE Bool Empty() const { return m_size.GetValue() == 0; }

	RED_INLINE LockFreeQueueFeintResult FeintPush() const { RED_FATAL( "Not supported in this CL" ); for ( ;; ) continue; return LockFreeQueueFeintResult::FalseThisInstant; }

protected:

	// Construct queue
	RED_INLINE BaseLockFreeQueue();

	// Default destructor
	~BaseLockFreeQueue() = default;

	RED_INLINE Uint32 ConvertToQueueIndex(Uint64 index) const { return index % QueueSize; }

	RED_INLINE TElement* TypedData() { return reinterpret_cast< TElement* >( m_data.Data() ); }
	RED_INLINE const TElement* TypedData() const { return reinterpret_cast< TElement* >( m_data.Data() ); }

	// where a new element will be inserted
	RED_ALIGN( 64 ) Atomic<Uint64> m_writeIndex;

	// queue
	StorageType m_data;

	// where the next element will be extracted from
	RED_ALIGN(64) Atomic<Uint64> m_readIndex;

	// number of elements in the queue
	Atomic<Uint32> m_size;
};

template < typename TElement, Uint32 QueueSize >
class SingleProducerMultipleConsumersLockFreeQueue : public BaseLockFreeQueue<TElement, QueueSize >
{
public:

	// Add 'element' to the end of the queue
	RED_INLINE Bool Push(const TElement& element);
	// Add 'element' to the end of the queue
	RED_INLINE Bool Push(TElement&& element);
	// Remove and return first element on the queue
	RED_INLINE Bool Pop(TElement& element);

private:

	template < typename Executor, typename T>
	RED_INLINE Bool InternalPush(T&& element);
};

template < typename TElement, Uint32 QueueSize >
class MultipleProducersMultipleConsumersLockFreeQueue : public BaseLockFreeQueue < TElement, QueueSize >
{
public:

	// Construct queue
	RED_INLINE MultipleProducersMultipleConsumersLockFreeQueue();

	// Add 'element' to the end of the queue
	RED_INLINE Bool Push(const TElement& element);
	// Add 'element' to the end of the queue
	RED_INLINE Bool Push(TElement&& element);
	// Remove and return first element on the queue
	RED_INLINE Bool Pop(TElement& element);

private:

	template < typename Executor, typename T >
	RED_INLINE Bool InternalPush(T&& element);

	// maximum index of element which is ready to read
	Atomic<Uint64> m_maximumReadIndex;
};

template < typename TElement, Uint32 QueueSize >
class MultipleProducersSingleConsumerLockFreeQueue
{
public:
	MultipleProducersSingleConsumerLockFreeQueue();
	~MultipleProducersSingleConsumerLockFreeQueue();

	Bool Push( const TElement& element );
	Bool Push(TElement&& element);
	Bool Pop(TElement& element);

	RED_INLINE Uint32 Size() const { return m_size.GetValue(); }
	RED_INLINE Uint32 DataSize() const { return m_size.GetValue() * sizeof(TElement); }
	RED_INLINE Bool Empty() const { return m_size.GetValue() == 0; }
	RED_INLINE LockFreeQueueFeintResult FeintPush() const;

private:

	struct Entry
	{
		atomic::TAtomic32 position;
		alignas( alignof( TElement ) ) Uint8 element[ sizeof( TElement ) ];
	};

	RED_ALIGN( 64 ) atomic::TAtomic32 m_queuePosition;
	RED_ALIGN( 64 ) atomic::TAtomic32 m_dequeuePosition;
	RED_ALIGN( 64 ) Entry m_entries[ QueueSize ];
	Atomic<Uint32> m_size;
	red::UpdateFlag m_consumerGuard;
};

template < typename TElement, Uint32 QueueSize >
using SPMCLockFreeQueue = LockFreeQueue<TElement, QueueSize, SingleProducerMultipleConsumersLockFreeQueue>;

template < typename TElement, Uint32 QueueSize >
using MPMCLockFreeQueue = LockFreeQueue<TElement, QueueSize>;

template < typename TElement, Uint32 QueueSize >
using MPSCLockFreeQueue = LockFreeQueue< TElement, QueueSize, MultipleProducersSingleConsumerLockFreeQueue >;

} // red

#include "lockFreeQueue.hpp"