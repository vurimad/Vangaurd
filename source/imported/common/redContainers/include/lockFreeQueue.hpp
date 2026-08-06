/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "policies.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 QueueSize >
RED_INLINE BaseLockFreeQueue< TElement, QueueSize >::BaseLockFreeQueue()
	: m_writeIndex(0)
	, m_readIndex(0)
	, m_size(0)
{
	Memset(TypedData(), 0, QueueSize);
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool SingleProducerMultipleConsumersLockFreeQueue< TElement, QueueSize >::Push(const TElement& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "SingleProducerMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );

	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;
	return InternalPush<CopyConstructorExecutor>(element);
}

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool SingleProducerMultipleConsumersLockFreeQueue< TElement, QueueSize >::Push(TElement&& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "SingleProducerMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );

	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;
	return InternalPush<MoveConstructorExecutor>(std::forward<TElement>(element));
}

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool SingleProducerMultipleConsumersLockFreeQueue< TElement, QueueSize >::Pop(TElement& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "SingleProducerMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );

	Uint64 currentReadIndex{ 0 };
	while(true)
	{
		currentReadIndex = this->m_readIndex.GetValue();
		if (this->ConvertToQueueIndex(currentReadIndex) == this->ConvertToQueueIndex(this->m_writeIndex.GetValue()))
		{
			// queue is empty
			return false;
		}

		TElement tmpElement = this->TypedData()[ this->ConvertToQueueIndex( currentReadIndex ) ];

		const Uint64 oldValue = this->m_readIndex.CompareExchange(currentReadIndex + 1, currentReadIndex);
		if (currentReadIndex == oldValue)
		{
			this->m_size.Decrement();
			element = std::move( tmpElement );
			break;
		}
	}

	return true;
}

template < typename TElement, Uint32 QueueSize >
template < typename Executor, typename T >
RED_INLINE Bool SingleProducerMultipleConsumersLockFreeQueue< TElement, QueueSize >::InternalPush(T&& element)
{
	const Uint64 currentWriteIndex = this->m_writeIndex.GetValue();
	if (this->ConvertToQueueIndex(currentWriteIndex + 1) == this->ConvertToQueueIndex(this->m_readIndex.GetValue()))
	{
		// queue is full
		return false;
	}

	Executor::Execute(this->TypedData() + this->ConvertToQueueIndex(currentWriteIndex), &element);

	this->m_writeIndex.Increment();
	this->m_size.Increment();
	return true;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 QueueSize >
RED_INLINE MultipleProducersMultipleConsumersLockFreeQueue< TElement, QueueSize >::MultipleProducersMultipleConsumersLockFreeQueue()
	: m_maximumReadIndex(0)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "MultipleProducersMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );
}

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool MultipleProducersMultipleConsumersLockFreeQueue< TElement, QueueSize >::Push(const TElement& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "MultipleProducersMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );

	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;
	return InternalPush<CopyConstructorExecutor>(element);
}

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool MultipleProducersMultipleConsumersLockFreeQueue< TElement, QueueSize >::Push(TElement&& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "MultipleProducersMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;
	return InternalPush<MoveConstructorExecutor>(std::forward<TElement>(element));
}

template < typename TElement, Uint32 QueueSize >
RED_INLINE Bool MultipleProducersMultipleConsumersLockFreeQueue< TElement, QueueSize >::Pop(TElement& element)
{
	static_assert( std::is_trivially_destructible< TElement >::value, "MultipleProducersMultipleConsumersLockFreeQueue can only be use with trivially destructible element." );

	Uint64 currentReadIndex{ 0 };
	while (true)
	{
		currentReadIndex = this->m_readIndex.GetValue();
		if (this->ConvertToQueueIndex(currentReadIndex) == this->ConvertToQueueIndex(this->m_maximumReadIndex.GetValue()))
		{
			// the queue is empty or one of the producers is waiting to commit the data into queue
			return false;
		}

		TElement tmpElement = this->TypedData()[ this->ConvertToQueueIndex( currentReadIndex ) ];

		const Uint64 oldValue = this->m_readIndex.CompareExchange(currentReadIndex + 1, currentReadIndex);
		if (currentReadIndex == oldValue)
		{
			this->m_size.Decrement();
			element = std::move( tmpElement );
			break;
		}
	}

	return true;
}

template < typename TElement, Uint32 QueueSize >
template < typename Executor, typename T >
RED_INLINE Bool MultipleProducersMultipleConsumersLockFreeQueue< TElement, QueueSize >::InternalPush(T&& element)
{
	Uint64 currentWriteIndex{ 0 };
	while (true)
	{
		currentWriteIndex = this->m_writeIndex.GetValue();
		if (this->ConvertToQueueIndex(currentWriteIndex + 1) == this->ConvertToQueueIndex(this->m_readIndex.GetValue()))
		{
			// queue is full
			return false;
		}

		const Uint64 oldValue = this->m_writeIndex.CompareExchange(currentWriteIndex + 1, currentWriteIndex);
		if (currentWriteIndex == oldValue)
			break;
	}

	Executor::Execute(this->TypedData() + this->ConvertToQueueIndex(currentWriteIndex), &element);

	while (true)
	{
		const Uint64 oldValue = m_maximumReadIndex.CompareExchange(currentWriteIndex + 1, currentWriteIndex);
		if (currentWriteIndex == oldValue)
			break;

		// @note consider yielding thread here
		// The yield of execution (on Windows) is limited to the processor of the calling thread,
		// so maybe we should call here SleepOnCurrentThread(0) instead.
		// YieldCurrentThread();
	}

	this->m_size.Increment();
	return true;
}

template < typename TElement, Uint32 QueueSize >
MultipleProducersSingleConsumerLockFreeQueue< TElement, QueueSize >::MultipleProducersSingleConsumerLockFreeQueue()
	: m_queuePosition( 0 )
	, m_dequeuePosition( 0 )
	, m_consumerGuard( "This LockFree queue support only one consumer!" )
{
	std::memset( &m_entries, 0, sizeof( m_entries ) );

	for ( Uint32 index = 0; index != QueueSize; ++index )
	{
		m_entries[ index ].position = index;
	}
}

template < typename TElement, Uint32 QueueSize >
MultipleProducersSingleConsumerLockFreeQueue< TElement, QueueSize >::~MultipleProducersSingleConsumerLockFreeQueue()
{
	while( !Empty() )
	{
		TElement element;
		Pop( element );
	}
}

template < typename TElement, Uint32 QueueSize >
Bool MultipleProducersSingleConsumerLockFreeQueue<TElement, QueueSize>::Push( const TElement& element )
{
	Entry * entry = nullptr;
	Uint32 position = const_cast< volatile atomic::TAtomic32& >( m_queuePosition );
	while ( 1 )
	{
		entry = &m_entries[ position % QueueSize ];
		Uint32 entryPosition = const_cast<volatile atomic::TAtomic32&>( entry->position );
		const Int32 difference = static_cast< Int32 >( entryPosition ) - static_cast< Int32 >( position );

		if ( difference == 0 )
		{
			if ( atomic::CompareExchange32( &m_queuePosition, position + 1, position ) == position )
				break;
		}
		else if ( difference < 0 )
		{
			return false;
		}

		position = const_cast< volatile atomic::TAtomic32& >( m_queuePosition );
	}

	::new( entry->element ) TElement( element );
	atomic::Exchange32( &entry->position, position + 1 );
	m_size.Increment();
	return true;
}

template < typename TElement, Uint32 QueueSize >
LockFreeQueueFeintResult MultipleProducersSingleConsumerLockFreeQueue<TElement, QueueSize>::FeintPush() const
{
	const Entry * entry = nullptr;
	Uint32 position = const_cast<volatile atomic::TAtomic32&>( m_queuePosition );

	entry = &m_entries[ position % QueueSize ];
	const Uint32 entryPosition = const_cast<volatile atomic::TAtomic32&>( entry->position );
	const Int32 difference = static_cast<Int32>( entryPosition ) - static_cast<Int32>( position );

	if ( difference < 0 )
	{
		return LockFreeQueueFeintResult::FalseThisInstant;
	}

	return LockFreeQueueFeintResult::TrueForSomeThread;
}

template < typename TElement, Uint32 QueueSize >
Bool MultipleProducersSingleConsumerLockFreeQueue<TElement, QueueSize>::Push( TElement&& element )
{
	Entry * entry = nullptr;
	Uint32 position = m_queuePosition;
	while ( 1 )
	{
		entry = &m_entries[ position % QueueSize ];
		Uint32 entryPosition = entry->position;
		const Int32 difference = static_cast< Int32 >( entryPosition ) - static_cast< Int32 >( position );

		if ( difference == 0 )
		{
			if ( atomic::CompareExchange32( &m_queuePosition, position + 1, position ) == position )
				break;
		}
		else if ( difference < 0 )
		{
			return false;
		}

		position = m_queuePosition;
	}

	::new( entry->element ) TElement( std::move( element ) );
	atomic::Exchange32( &entry->position, position + 1 );
	m_size.Increment();
	return true;
}

template < typename TElement, Uint32 QueueSize >
Bool MultipleProducersSingleConsumerLockFreeQueue<TElement, QueueSize>::Pop(TElement& element)
{
	red::UpdateFlagGuard guard( m_consumerGuard, red::UpdateFlagGuard::Exclusive );

	Entry * entry = nullptr;
	Uint32 position = m_dequeuePosition;
	while ( 1 )
	{
		entry = &m_entries[ position % QueueSize ];
		Uint32 entryPosition = entry->position;
		const Int32 difference = static_cast< Int32 >( entryPosition ) - static_cast< Int32 >( position + 1 );
		if ( difference == 0 )
		{
			if ( atomic::CompareExchange32( &m_dequeuePosition, position + 1, position ) == position )
				break;
		}
		else if ( difference < 0 )
		{
			return false;
		}

		position = m_dequeuePosition;
	}
	
	TElement * entryElement = reinterpret_cast< TElement* >( entry->element );
	element = std::move( *entryElement );
	entryElement->~TElement(); 
	atomic::Exchange32( &entry->position, position + QueueSize );
	m_size.Decrement();
	return true;
}

} // red