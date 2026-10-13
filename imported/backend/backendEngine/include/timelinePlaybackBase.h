/**
* Copyright (C) 2018 CD Projekt Red.All Rights Reserved.
*/

#pragma once

#include "playbackBase.h"

namespace tools
{
	class ITimeLine;

	class BACKEND_ENGINE_API TimelinePlaybackBase : public PlaybackBase
	{
	public:
		TimelinePlaybackBase( const ITimeLine* timeline );
		virtual ~TimelinePlaybackBase() override = default;

		virtual void AdjustLength();

	private:
		const ITimeLine* m_timeline;
	};

}