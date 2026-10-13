/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "playbackBase.h"

namespace
{
	Float wrapPositionInRange( const Float position, const Float start, const Float end )
	{
		const Float mod = end < std::numeric_limits< Float >::epsilon() ? 0.0f : fmodf( position, end );
		return mod >= start ? mod : ( mod + end );
	}

	Float clampPositionInRange( const Float position, const Float start, const Float end )
	{
		return math::Clamp< Float >( position, start, end );
	}
}

namespace tools
{
	PlaybackBase::PlaybackBase()
		: m_timer( "" )
		, m_timerResultion( 0.1f )
		, m_state( PlaybackState::Stopped )
		, m_loop( false )
		, m_startTime( 0 )
		, m_duration( 0 )
		, m_currentTime( 0 )
		, m_playbackRatio( 1 )
		, m_subrangeEnabled( false )
		, m_subrangeStartTime( 0 )
		, m_subrangeDuration( 0 )
		, m_resizeToContentEnabled( true )
	{
	}

	Bool PlaybackBase::Initialized()
	{
		return true;
	}

	void PlaybackBase::SetTickResolution( Float time )
	{
		if ( m_timerResultion != time )
		{
			m_timerResultion = time;
			StartTimer();
		}
	}

	void PlaybackBase::UpdateRealTime( const Float timeDelta )
	{
		if ( GetPlaybackState() == PlaybackState::Playing )
		{
			SetPlaybackPosition( m_currentTime + timeDelta );
		}
	}

	void PlaybackBase::StartTimer()
	{
		StopTimer();
		m_timer.ExecuteEvery( m_timerResultion, [ this ] ()
		{
			if(m_tickEnabled)
				OnTick();
		} );
	}

	void PlaybackBase::StopTimer()
	{
		m_timer.Stop();
	}

	void PlaybackBase::AttachListener( IPlaybackListener* listener )
	{
		if ( !m_listeners.Exist( listener ) )
		{
			m_listeners.PushBack( listener );
		}
	}

	void PlaybackBase::DetachListener( IPlaybackListener* listener )
	{
		m_listeners.Remove( listener );
	}

	void PlaybackBase::SetPlaybackPosition( Float position )
	{
		if ( position == m_currentTime )
		{
			return;
		}

		const Float oldCurrentTime = m_currentTime;

		const Float startTime = GetCurrentStartTime();
		const Float endTime = GetCurrentEndTime();

		if ( m_state == PlaybackState::Edit )
		{
			m_currentTime = wrapPositionInRange( position, startTime, endTime );
		}
		else
		{
			if ( IsPlaybackLooped() )
			{
				if( clampPositionInRange( position, startTime, endTime ) == endTime )
				{
					OnEnd();
				}

				m_currentTime = wrapPositionInRange( position, startTime, endTime );
			}
			else
			{
				m_currentTime = clampPositionInRange( position, startTime, endTime );

				if ( m_currentTime == endTime )
				{
					SetState( PlaybackState::Stopped );
					OnEnd();
				}
			}
		}

		if ( oldCurrentTime != m_currentTime )
		{
			OnPlaybackSet( m_currentTime );
			SendPositionChanged( m_currentTime );
		}
	}

	void PlaybackBase::SetPlaybackRatio( Float ratio )
	{
		if ( m_playbackRatio != ratio )
		{
			m_playbackRatio = ratio;
			OnPlaybackRatioSet( m_playbackRatio );

			SendPlaybackRatioChanged( m_playbackRatio );
		}
	}

	void PlaybackBase::SetStartTime( Float startTime )
	{
		if ( startTime >= 0 && ( m_duration == 0 || startTime <= GetPlaybackEndTime() ) )
		{
			m_startTime = startTime;
			m_duration = GetPlaybackEndTime() - m_startTime;

			OnStartTimeSet( startTime );
			OnDurationSet( m_duration );
		}

		SendStartTimeChanged( m_startTime );

		UpdateDurationState();
		ValidateSubrange();
	}

	void PlaybackBase::SetEndTime( Float endTime )
	{
		if ( endTime >= m_startTime && endTime >= 0 )
		{
			m_duration = endTime - m_startTime;
		}
		else
		{
			m_duration = 0;
		}

		OnDurationSet( m_duration );

		UpdateDurationState();
		ValidateSubrange();
	}

	void PlaybackBase::SetDuration( Float duration )
	{
		const Bool durationChanged = duration != m_duration;
		if ( duration >= 0 && durationChanged )
		{
			m_duration = duration;
			OnDurationSet( m_duration );
		}

		if ( durationChanged )
		{
			UpdateDurationState();
			ValidateSubrange();
		}
	}

	void PlaybackBase::UpdateDurationState()
	{
		SendDurationChanged( m_duration );
		SendEndTimeChanged( GetPlaybackEndTime() );

		if ( m_currentTime > GetCurrentEndTime() )
		{
			SetPlaybackPosition( m_duration );
		}
	}

	void PlaybackBase::SetSubrangeStartTime( Float startTime )
	{
		m_subrangeStartTime = startTime;
		ValidateSubrange();

		if ( m_subrangeStartTime == startTime )
		{
			SendSubrangeStartTimeChanged();
			SendSubrangeDurationChanged();
		}
	}

	void PlaybackBase::SetSubrangeDuration( Float duration )
	{
		m_subrangeDuration = duration;
		ValidateSubrange();

		if ( m_subrangeDuration == duration )
		{
			SendSubrangeDurationChanged();
		}
	}

	Bool PlaybackBase::IsResizeToContentEnabled() const
	{
		return m_resizeToContentEnabled;
	}

	void PlaybackBase::EnableResizeToContent( Bool enable )
	{
		m_resizeToContentEnabled = enable;
	}

	void PlaybackBase::EnableTick()
	{
		m_tickEnabled = true;
	}

	void PlaybackBase::DisableTick()
	{
		m_tickEnabled = false;
	}

	void PlaybackBase::ValidateSubrange()
	{
		if ( m_subrangeStartTime < GetPlaybackStartTime() )
		{
			m_subrangeStartTime = GetPlaybackStartTime();

			SendSubrangeStartTimeChanged();
			SendSubrangeDurationChanged();
		}

		if ( m_subrangeStartTime > GetPlaybackEndTime() )
		{
			m_subrangeStartTime = GetPlaybackEndTime();
			m_subrangeDuration = 0;

			SendSubrangeStartTimeChanged();
			SendSubrangeDurationChanged();
		}

		if ( m_subrangeDuration < 0 )
		{
			m_subrangeDuration = 0;
			SendSubrangeDurationChanged();
		}

		Float subrangeEndTime = m_subrangeStartTime + m_subrangeDuration;
		if ( subrangeEndTime > GetPlaybackEndTime() )
		{
			Float diff = subrangeEndTime - GetPlaybackEndTime();
			m_subrangeDuration -= diff;

			SendSubrangeDurationChanged();
		}

		// validate current playback position after subrange change
		SetPlaybackPosition( m_currentTime );
	}

	void PlaybackBase::SetLoop( Bool loop )
	{
		if ( m_loop != loop )
		{
			m_loop = loop;
			OnLoopSet( loop );
		}
	}

	void PlaybackBase::SetState( PlaybackState state )
	{
		if ( m_state != state )
		{
			m_previousState = m_state;
			m_state = state;
			SendPlaybackStateChanged( m_state );
		}
	}

	void PlaybackBase::UpdateCurrentTime()
	{
		m_currentTime += GetTimerResolution() * m_playbackRatio;
	}
	
	Float PlaybackBase::GetCurrentStartTime() const
	{
		if ( IsSubrangeEnabled() )
		{
			return m_subrangeStartTime;
		}

		return GetPlaybackStartTime();
	}

	Float PlaybackBase::GetCurrentEndTime() const
	{
		if ( IsSubrangeEnabled() )
		{
			return m_subrangeStartTime + m_subrangeDuration;
		}

		return GetPlaybackEndTime();
	}
	
	void PlaybackBase::OnTick()
	{
		UpdateCurrentTime();

		if ( m_currentTime >= GetCurrentEndTime() )
		{
			if ( IsPlaybackLooped() )
			{
				m_currentTime = GetCurrentStartTime();
			}
			else
			{
				m_currentTime = GetCurrentEndTime();

				SetState( PlaybackState::Stopped );
				StopTimer();
				OnEnd();
			}
		}

		SendPositionChanged( m_currentTime );
	}

	void PlaybackBase::SendPlaybackStateChanged( PlaybackState state )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnStateChange( state );
		}
	}

	void PlaybackBase::SendPlaybackRatioChanged( Float ratio )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnPlaybackRatioChanged( ratio );
		}
	}

	void PlaybackBase::SendPositionChanged( Float position )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnPlaybackPositionChanged( position );
		}
	}

	void PlaybackBase::SendDurationChanged( Float duration )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnDurationChanged( duration );
		}
	}

	void PlaybackBase::SendStartTimeChanged( Float startTime )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnStartTimeChanged( startTime );
		}
	}

	void PlaybackBase::SendEndTimeChanged( Float endTime )
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnEndTimeChanged( endTime );
		}
	}

	void PlaybackBase::SendSubrangeStartTimeChanged()
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnSubrangeStartTime( m_subrangeStartTime );
		}
	}

	void PlaybackBase::SendSubrangeDurationChanged()
	{
		for ( IPlaybackListener* listener : GetListeners() )
		{
			listener->OnSubrangeDuration( m_subrangeDuration );
		}
	}

	void PlaybackBase::Play()
	{
		if ( m_state == PlaybackState::Playing )
		{
			return;
		}

		if ( m_state == PlaybackState::Stopped )
		{
			if ( m_currentTime == GetCurrentEndTime() )
			{
				m_currentTime = GetCurrentStartTime();
			}
		}

		StartTimer();

		SetPlaybackPosition( m_currentTime );
		SetState( PlaybackState::Playing );

		OnPlay();
	}

	void PlaybackBase::Pause()
	{
		if ( m_state == PlaybackState::Paused )
			return;

		StopTimer();
		SetState( PlaybackState::Paused );

		OnPause();
	}

	void PlaybackBase::Reset()
	{
		Stop();
		Play();

		OnReset();
	}

	void PlaybackBase::Stop()
	{
		StopTimer();

		SetPlaybackPosition( GetCurrentStartTime() );
		SetState( PlaybackState::Stopped );

		OnStop();
	}

	void PlaybackBase::ToggleEditMode()
	{
		if ( m_state == PlaybackState::Edit )
		{
			switch ( m_previousState )
			{
			case PlaybackState::Playing:
				Play();
				break;
			case PlaybackState::Paused:
				Pause();
				break;
			case PlaybackState::Stopped:
				StopTimer();
				SetState( PlaybackState::Stopped );
				OnStop();
				break;
			}
		}
		else
		{
			SetState( PlaybackState::Edit );
			StopTimer();
			OnStop();
		}
	}

	void PlaybackBase::SetSubrange( Bool enable )
	{
		if ( m_subrangeEnabled != enable )
		{
			m_subrangeEnabled = enable;

			Float currentTime = GetPlaybackPosition();
			if ( !( currentTime >= GetCurrentStartTime() && currentTime <= GetCurrentEndTime() ) )
			{
				SetPlaybackPosition( GetCurrentStartTime() );
			}

			for ( IPlaybackListener* listener : GetListeners() )
			{
				listener->OnSubrangeEnabled( enable );
			}
		}
	}
}