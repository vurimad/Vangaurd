/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "spinLock.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_DURANGO
	SpinLock::SpinLock()
	{
		InitializeSRWLock( &m_SRWLock );
	}

	SpinLock::~SpinLock()
	{
	}

	void SpinLock::AcquireShared()
	{
		AcquireSRWLockShared( &m_SRWLock );
	}
	
	void SpinLock::Acquire()
	{
		AcquireSRWLockExclusive( &m_SRWLock );
	}

	void SpinLock::ReleaseShared()
	{
		ReleaseSRWLockShared( &m_SRWLock );
	}
	
	void SpinLock::Release()
	{
		ReleaseSRWLockExclusive( &m_SRWLock );
	}

	bool SpinLock::TryAcquire()
	{
		if( TryAcquireSRWLockExclusive( &m_SRWLock ) != 0 )
		{
			return true;
		}

		return false;
	}
#endif
}
}
