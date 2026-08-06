/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SPIN_LOCK_H_
#define _RED_MEMORY_SPIN_LOCK_H_

#include "../../redSystem/include/readWriteSpinLock.h"

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_DURANGO

	class SpinLock
	{
	public:

		SpinLock();
		~SpinLock();

		void AcquireShared();
		void Acquire();

		void ReleaseShared();
		void Release();

		bool TryAcquire();

	private:

		void EndTryWrite();

		SRWLOCK m_SRWLock;
	};
#else

	using SpinLock = red::RWSpinLock;

#endif
}
}

#endif
