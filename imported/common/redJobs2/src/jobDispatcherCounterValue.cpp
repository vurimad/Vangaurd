/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobDispatcherCounterValue.h"

namespace job { namespace prv
{

DispatcherCounterValue::ExchangeAddResult DispatcherCounterValue::ExchangeAdd( Uint32 value )
{
	RED_FATAL_ASSERT( value > 0 );

	ExchangeAddResult result;
	const Uint32 oldValue = m_counterValue.ExchangeAdd( value );

	// Only care about the lower half of oldValue, the upper half is for locking the waiting list.
	// Also if oldValue was zero, then some thread will try to release the counter entry. We can't stop
	// other threads from trying to lock the upper half anyway.
	if ( oldValue == 0 )
	{
		result.wasZero = true;
	}
	return result;
}

DispatcherCounterValue::DecrementResult DispatcherCounterValue::Decrement()
{
	DecrementResult result;

	const Uint32 newCounterValue = m_counterValue.Decrement();
	RED_FATAL_ASSERT( newCounterValue != std::numeric_limits<Uint32>::max(), "Counter value underflow!" );

	if ( newCounterValue == 0 )
	{
		result.isZero = true;
	}
	return result;
}

} } // job/prv
