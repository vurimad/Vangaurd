/**
* Copyright (C) 2018 CD Projekt Red.All Rights Reserved.
*/

#pragma once

#include "playbackInterop.h"

#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class TimelinePlaybackBase;

	class BACKEND_ENGINE_API TimelinePlaybackInterop : public PlaybackInterop
	{
		DECLARE_INTEROP_DERIVED( TimelinePlaybackInterop, PlaybackInterop );
	public:
		TimelinePlaybackInterop( TimelinePlaybackBase* playback );

	private:
		TimelinePlaybackBase* m_playback;
	};
} // namespace tools
