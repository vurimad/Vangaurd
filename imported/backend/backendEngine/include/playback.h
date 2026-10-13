#pragma once

/**
* Copyright (c) 2017-2020 CD Projekt Red.All Rights Reserved.
*/

namespace tools
{
	enum class PlaybackState
	{
		Stopped, // default state
		Playing,
		Paused,
		Edit,
		Inactive,
	};

	///
	///
	///
	
	typedef String PlaybackButtonId;

	class IPlaybackListener
	{
	public:
		virtual ~IPlaybackListener() { }

		// Reload control state on UI side
		virtual void OnInvalidatePlaybackControl() { }

		// Playback state was changed (playing, stopped, paused)
		virtual void OnStateChange( PlaybackState state ) { }

		// A button can be enabled/disabled from backend side according to internal resource state
		virtual void OnEnableButton( const PlaybackButtonId& buttonId, Bool enabled ) { }
		// A button was toggled (some state of buttons can be controlled by the backend side)
		virtual void OnToggleButton( const PlaybackButtonId& buttonId, Bool checked ) { }

		virtual void OnStartTimeChanged( Float startTime ) { }
		virtual void OnEndTimeChanged( Float endTime ) { }
		virtual void OnDurationChanged( Float newDurationTime ) { }
		virtual void OnPlaybackPositionChanged( Float newTime ) { }
		virtual void OnPlaybackRatioChanged( Float newTime ) { }

		virtual void OnSubrangeEnabled( Bool enable ) { }
		virtual void OnSubrangeStartTime( Float value ) { }
		virtual void OnSubrangeDuration( Float value ) { }

		virtual void OnResizeToContentEnabledChanged( Bool value ) {}
	};

	///
	///
	///

	// An abstract interface of playback control
	class IPlayback
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IPlayback() { }

		virtual void AttachListener( IPlaybackListener* listener ) = 0;
		virtual void DetachListener( IPlaybackListener* listener ) = 0;

		virtual Bool Initialized() = 0;

		///
		/// Below functions are typical functions supported by playback. Can be extended in subclasses
		///

		virtual void Play() = 0;
		virtual void Pause() = 0;
		virtual void Stop() = 0;
		virtual void Reset() = 0;
		virtual void ToggleEditMode() = 0;

		// Sets current playback position
		virtual PlaybackState GetPlaybackState() const = 0;
		virtual Float GetPlaybackPosition() const = 0;
		virtual Float GetPlaybackStartTime() const = 0;
		virtual Float GetPlaybackEndTime() const = 0;
		virtual Float GetPlaybackDuration() const = 0;
		virtual Float GetPlaybackRatio() const = 0;

		virtual void SetPlaybackPosition( Float position ) = 0;
		virtual void SetPlaybackRatio( Float ratio ) = 0;
		virtual void SetStartTime( Float startTime ) = 0;
		virtual void SetEndTime( Float endTime ) = 0;
		// Sets total playback duration
		virtual void SetDuration( Float duration ) = 0;
		// Enable/Disable playback looping
		virtual void SetLoop( Bool loop ) = 0;

		/// subrange feature - Possible to enable or disable subrange of playback (limit a playback to specific range)
		virtual Bool IsSubrangeEnabled() const = 0;
		virtual Float GetSubrangeStartTime() const = 0;
		virtual Float GetSubrangeDuration() const = 0;

		virtual void SetSubrange( Bool enable ) = 0;
		virtual void SetSubrangeStartTime( Float startTime ) = 0;
		virtual void SetSubrangeDuration( Float duration ) = 0;

		virtual Bool IsResizeToContentEnabled() const = 0;
		virtual void EnableResizeToContent( Bool enabled ) = 0;
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( PlaybackState, tools );
