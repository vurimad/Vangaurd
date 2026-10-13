/**
* Copyright (C) 2018 CD Projekt Red.All Rights Reserved.
*/

#include "build.h"
#include "../include/timeLineBase.h"
#include "../include/timelinePlaybackBase.h"

namespace tools
{
	TimelinePlaybackBase::TimelinePlaybackBase( const ITimeLine* timeline )
		: m_timeline( timeline )
	{
	}

	void TimelinePlaybackBase::AdjustLength()
	{
		if( m_timeline )
		{
			SetDuration( std::max( m_timeline->GetEventsEndTime(), 0.0f ) );
		}
	}
} // namespace tools
