/**
* Copyright (C) 2018 CD Projekt Red.All Rights Reserved.
*/

#include "build.h"
#include "../include/timelinePlaybackInterop.h"
#include "../include/timelinePlaybackBase.h"

namespace tools
{
	TimelinePlaybackInterop::TimelinePlaybackInterop( TimelinePlaybackBase* playback )
		: PlaybackInterop( playback )
		, m_playback( playback )
	{
	}

	void TimelinePlaybackInterop::SetupInterop( TimelinePlaybackInterop* obj, interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "adjustLength", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->AdjustLength(); } );
	}
} //namespace tools 
