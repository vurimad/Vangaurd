/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/intrusivePtr.h"
#include "../../redJobs2/include/jobCounterOwner.h"

namespace res
{
	class ResourceLoadingFence;
	
	using ResourceLoadingFencePtr = red::IntrusivePtr< ResourceLoadingFence >;
}

namespace job
{
	class Builder;
}

namespace res
{

class RED_REFLECTION_API ResourceLoadingFence : red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolResource );
	friend struct red::memory::internal::DeleteResolver< ResourceLoadingFence, false >;

public:
	void Wait();
	Bool IsFinished() const;

	static ResourceLoadingFencePtr Create();
	// #tbd: more like "SyncWithSyncObject"...
	void InitWithSyncObject( const job::Counter& waitCounter );

	static ResourceLoadingFencePtr Create( const job::Counter& waitCounter );

	// For intrusiveptr
	Int32 Release();
	void AddRef();

	const job::Counter& GetWaitCounter() const;

private:
	void FinishLoading();

	job::Counter m_waitCounter;
	job::CompletionDeferral m_deferral;

	explicit ResourceLoadingFence();
	~ResourceLoadingFence();

	red::Atomic< Int32 > m_refCount;
	red::Atomic< Bool > m_isFinished;
	red::Atomic< Bool > m_isInitialized;
};

}
