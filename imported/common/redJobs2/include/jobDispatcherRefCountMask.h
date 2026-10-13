/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/utility.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace job { namespace prv
{

// Encapsulates atomically keeping a refcount for the CounterChain and Dispatcher. It requires careful use
// to know when to call AddRefForDispatcher() and ReleaseForDispatcher() - basically addref'd to make sure
// a Counter stays alive while jobs are running. See the Dispatcher class for more details.
//
// Keep this as stateless as possible to help avoid any errors in use
class DispatcherRefCountMask : red::NonCopyable
{
public:
	DispatcherRefCountMask( Uint32 initialValue )
		: m_refCount( initialValue )
	{}

	struct ReleaseResult
	{
		Bool isZero{ false };
	};

	void AddRef();

	ReleaseResult Release();

	Bool IsZero_Snapshot() const { return m_refCount.GetValue() == 0; }

private:
	red::Atomic<Uint32> m_refCount;
};

} } // job/prv
