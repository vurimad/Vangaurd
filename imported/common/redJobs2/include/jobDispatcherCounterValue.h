/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/utility.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace job { namespace prv
{

// Encapsulates an atomic counter with specialized locking semantics: you can know you were the first to reach zero
// but also whether there's currently any "lock" on it to indiciate a threading hazard.
// Keep this as stateless as possible to help avoid any errors in use
class DispatcherCounterValue : red::NonCopyable
{
public:
	DispatcherCounterValue(Uint32 initialValue)
		: m_counterValue( initialValue )
	{}

	struct ExchangeAddResult
	{
		Bool wasZero{ false };
	};

	struct DecrementResult
	{
		Bool isZero{ false };
	};

	ExchangeAddResult ExchangeAdd( Uint32 value );
	DecrementResult Decrement();

	Bool IsZero_Snapshot() const
	{
		return m_counterValue.GetValue() == 0;
	}

private:
	red::Atomic< Uint32 > m_counterValue;
};

} } // job/prv
