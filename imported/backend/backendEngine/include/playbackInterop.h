#pragma once
/**
* Copyright (c) 2016-2020 CD Projekt Red.All Rights Reserved.
*/

#include "Playback.h"

#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	class BACKEND_ENGINE_API PlaybackInterop : public IPlaybackListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( PlaybackInterop );

	public:
		PlaybackInterop( IPlayback* playback );

		///
		/// IPlaybackListener implementation
		///
		// Reload control state on UI side
		virtual void OnInvalidatePlaybackControl() override;

		// Playback state was changed (playing, stopped, paused)
		virtual void OnStateChange( PlaybackState state ) override;

		// A button can be enabled/disabled from backend side according to internal resource state
		virtual void OnEnableButton( const PlaybackButtonId& buttonId, Bool enabled ) override;
		// A button was toggled (some state of buttons can be controlled by the backend side)
		virtual void OnToggleButton( const PlaybackButtonId& buttonId, Bool checked ) override;

		virtual void OnStartTimeChanged( Float startTime ) override;
		virtual void OnEndTimeChanged( Float endTime ) override;
		virtual void OnDurationChanged( Float newDurationTime ) override;
		virtual void OnPlaybackPositionChanged( Float newTime ) override;
		virtual void OnPlaybackRatioChanged( Float newRatio ) override;

		virtual void OnSubrangeEnabled( Bool enable ) override;
		virtual void OnSubrangeStartTime( Float value ) override;
		virtual void OnSubrangeDuration( Float value ) override;

		virtual void OnResizeToContentEnabledChanged( Bool value ) override;

	private:
		void Fetch();

		IPlayback*  m_playback;
		interop::InteropNotifier m_notifer;
	};
}