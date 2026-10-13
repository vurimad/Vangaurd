/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/utility.h"

namespace job
{

namespace prv
{
	class Dispatcher;
	class CounterEntry;
}

class REDJOBS2_API CompletionDeferral: public red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
	friend class prv::Dispatcher;

public:
	CompletionDeferral( CompletionDeferral&& other );
	CompletionDeferral();
	~CompletionDeferral();

	CompletionDeferral& operator=( CompletionDeferral&& rhs );

	void FinishDeferral();

	const char* GetDebugName() const { return m_debugName; }
	const void* GetDebugUserData() const { return m_debugUserData;  }
	Uint64 GetDebugCounterMemAddr() const { return reinterpret_cast<Uint64>( m_counterEntry ); }
	Bool GetDebugIsFinished() const { return m_isFinished.GetValue(); }
	
private:
	void Swap( CompletionDeferral& rhs );
	void RegisterWithDebugger();
	void UnregisterFromDebugger();

	explicit CompletionDeferral( const char* debugName, const void* debugUserData, prv::CounterEntry& counter );
	Bool TryFinishDeferral();

	const char* m_debugName;
	const void* m_debugUserData;
	prv::CounterEntry* m_counterEntry;
	red::Atomic< Bool > m_isFinished;
};

}
