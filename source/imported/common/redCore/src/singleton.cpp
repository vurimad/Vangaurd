/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////

#include "build.h"

#include "singleton.h"

namespace red
{
	CTrackerArray* GTrackerArray = NULL;

	void* PolicyMemoryAllocate( size_t size, size_t align )
	{
		return RED_ALLOCATE_ALIGNED( red::PoolEngine, size, align );
	}

	void PolicyMemoryFree( void* mem )
	{
		RED_FREE( red::PoolEngine, mem );
	}

	void AtExitFn()
	{
		RED_FATAL_ASSERT(GTrackerArray->m_elements > 0 && GTrackerArray->m_trackers[0], "" );

		CLifetimeTracker* tracker = GTrackerArray->m_trackers[GTrackerArray->m_elements - 1];

		GTrackerArray->m_elements --;

		tracker->~CLifetimeTracker();

		PolicyMemoryFree(tracker);

		if (GTrackerArray->m_elements == 0)
		{
			GTrackerArray->~CTrackerArray();

			PolicyMemoryFree(GTrackerArray);

			GTrackerArray = NULL;
		}
	}
}

