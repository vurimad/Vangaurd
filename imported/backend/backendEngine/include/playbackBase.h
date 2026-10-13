#pragma once

/**
* Copyright (c) 2017-2018 CD Projekt Red.All Rights Reserved.
*/

#include "playback.h"
#include "undoFwd.h"
#include "backendTimer.h"

namespace tools
{
	// Everything what is going on UI side is controlled by this Playback
	// implementation. Implementation can change:
	// - Number of buttons (buttons glyphs, visibility, enable/disabled state)
	class BACKEND_ENGINE_API PlaybackBase : public IPlayback
	{
	public:
		PlaybackBase();

		virtual void AttachListener( IPlaybackListener* listener ) override;
		virtual void DetachListener( IPlaybackListener* listener ) override;

		virtual Bool Initialized() override;

		///
		/// Entry functions called from the interop
		///

		virtual void Play() override;
		virtual void Pause() override;
		virtual void Reset() override;
		virtual void Stop() override;
		virtual void ToggleEditMode() override;

		// Returns state of playback controls
		virtual PlaybackState GetPlaybackState() const override { return m_state; }

		virtual Float GetPlaybackPosition() const override { return m_currentTime; }
		virtual Float GetPlaybackStartTime() const override { return m_startTime; }
		virtual Float GetPlaybackEndTime() const override { return m_startTime + m_duration; }
		virtual Float GetPlaybackDuration() const override { return m_duration; }
		virtual Float GetPlaybackRatio() const override { return m_playbackRatio; }

		// Sets current playback position
		virtual void SetPlaybackPosition( Float position ) override;
		virtual void SetPlaybackRatio( Float position ) override;
		virtual void SetStartTime( Float startTime ) override;
		virtual void SetEndTime( Float endTime ) override;
		// Sets total playback duration
		virtual void SetDuration( Float duration ) override;
		// Enable/Disable playback looping
		virtual void SetLoop( Bool loop ) override;

		/// subrange feature - Possible to enable or disable subrange of playback (limit a playback to specific range)
		virtual Bool IsSubrangeEnabled() const override { return m_subrangeEnabled; }
		virtual Float GetSubrangeStartTime() const override { return m_subrangeStartTime; }
		virtual Float GetSubrangeDuration() const override { return m_subrangeDuration; }

		virtual void SetSubrange( Bool enable ) override;
		virtual void SetSubrangeStartTime( Float startTime ) override;
		virtual void SetSubrangeDuration( Float endTime ) override;

		virtual Bool IsResizeToContentEnabled() const override;
		virtual void EnableResizeToContent( Bool enable ) override;

		///
		/// Internal timer support to enforce playback UI updates
		///

		// Enable/Disable internal timer to update the playback position
		void EnableTick();
		void DisableTick();

		// Sets resolution of internal timer
		void SetTickResolution( Float time );

		void UpdateRealTime( const Float timeDelta );

	protected:
		// Internal timer ticked. You can send to UI current runtime playback time
		virtual void OnTick();

		// UI side changed the position of playback
		virtual void OnPlaybackSet( Float position ) { }
		// UI side changed the playback ratio
		virtual void OnPlaybackRatioSet( Float ratio ) { }
		// UI side changed the duration of playback.
		virtual void OnStartTimeSet( Float startTime ) { }
		// UI side changed the duration of playback.
		virtual void OnDurationSet( Float duration ) { }
		// UI side changed the looping setting
		virtual void OnLoopSet( Bool enableLooping ) { }

		virtual void OnPlay() { }
		virtual void OnPause() { }
		virtual void OnStop() { }
		virtual void OnReset() { }
		virtual void OnEnd() { }

		// Function allows to set current playback time base on runtime data during OnTick
		virtual void UpdateCurrentTime();

		// Sets internal playback state
		void SetState( PlaybackState state );

		Bool IsPlaybackLooped() const { return m_loop; }

		const red::DynArray< IPlaybackListener* >& GetListeners() const { return m_listeners; }

		// Start and stop internal ticking timer used to feed proper playback position form
		// runtime system... maybe there is a better way to do this. 
		void StartTimer();
		void StopTimer();

		Float GetTimerResolution() const { return m_timerResultion; }

		// Can be reset during the tick update
		Float m_startTime;
		Float m_duration;		//!< Current playback duration
		Float m_currentTime;	//!< Current playback time. This is controlled by this implementation

	private:
		void SendPlaybackStateChanged( PlaybackState state );
		void SendPlaybackRatioChanged( Float ratio );
		void SendPositionChanged( Float position );
		void SendStartTimeChanged( Float startTime );
		void SendEndTimeChanged( Float endTime );
		void SendDurationChanged( Float duration );
		void SendSubrangeStartTimeChanged();
		void SendSubrangeDurationChanged();

		void UpdateDurationState();
		void ValidateSubrange();

		Float GetCurrentStartTime() const;
		Float GetCurrentEndTime() const;

		IEditActionHistory* m_editHistory;

		red::DynArray< IPlaybackListener* > m_listeners;

		Bool m_tickEnabled = true;
		Timer m_timer;
		Float m_timerResultion;
		Float m_timerTime;

		Float m_playbackRatio;

		PlaybackState m_state;
		PlaybackState m_previousState;

		Bool m_loop; //!< playback is looped

		Bool m_subrangeEnabled;
		Float m_subrangeStartTime;
		Float m_subrangeDuration;

		Bool m_resizeToContentEnabled;
	};
}