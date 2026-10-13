/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "playbackInterop.h"

namespace tools
{
	void PlaybackInterop::SetupInterop( PlaybackInterop* obj, interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "play", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->Play(); } );
		op.RegisterFunctor< void >( "pause", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->Pause(); } );
		op.RegisterFunctor< void >( "stop", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->Stop(); } );
		op.RegisterFunctor< void >( "reset", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->Reset(); } );
		op.RegisterFunctor< void >( "toggleEditMode", [ obj ] ( ::interop::IContext& ctx ) { obj->m_playback->ToggleEditMode(); } );

		op.RegisterFunctor< void >( "fetch", [ obj ] ( ::interop::IContext& ctx ) { obj->Fetch(); } );

		op.RegisterValueRO< Float >( "playbackDuration", [ obj ] () { return obj->m_playback->GetPlaybackDuration(); } );
		op.RegisterValueRO< Float >( "playbackPosition", [ obj ] () { return obj->m_playback->GetPlaybackPosition(); } );

		op.RegisterFunctor< void, Float >( "setPlaybackPosition", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetPlaybackPosition( value ); } );
		op.RegisterFunctor< void, Float >( "setStartTime", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetStartTime( value ); } );
		op.RegisterFunctor< void, Float >( "setEndTime", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetEndTime( value ); } );
		op.RegisterFunctor< void, Float >( "setDuration", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetDuration( value ); } );
		op.RegisterFunctor< void, Float >( "setPlaybackRatio", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetPlaybackRatio( value ); } );
		op.RegisterFunctor< void, Bool >( "setLoop", [ obj ] ( ::interop::IContext& ctx, Bool value ) { obj->m_playback->SetLoop( value ); } );

		op.RegisterFunctor< void, Bool >( "setSubrange", [ obj ] ( ::interop::IContext& ctx, Bool value ) { obj->m_playback->SetSubrange( value ); } );
		op.RegisterFunctor< void, Float >( "setSubrangeStartTime", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetSubrangeStartTime( value ); } );
		op.RegisterFunctor< void, Float >( "setSubrangeDuration", [ obj ] ( ::interop::IContext& ctx, Float value ) { obj->m_playback->SetSubrangeDuration( value ); } );

		op.RegisterValue< Bool >( "resizeToContentEnabled",
			[obj]( ::interop::IContext& ctx, Bool& outValue ){ outValue = obj->m_playback->IsResizeToContentEnabled(); },
			[obj]( ::interop::IContext& ctx, const Bool& newValue ){ obj->m_playback->EnableResizeToContent( newValue ); } );

		op.RegisterNotifier( "_notifier", obj->m_notifer );
	}

	PlaybackInterop::PlaybackInterop( IPlayback* playback )
		: m_playback( playback )
	{
		m_playback->AttachListener( this );
	}

	void PlaybackInterop::OnInvalidatePlaybackControl()
	{
		m_notifer.Notify( String( "OnInvalidate" ) );
	}

	void PlaybackInterop::Fetch()
	{
		OnPlaybackPositionChanged( m_playback->GetPlaybackPosition() );
		OnStartTimeChanged( m_playback->GetPlaybackStartTime() );
		OnEndTimeChanged( m_playback->GetPlaybackEndTime() );
		OnDurationChanged( m_playback->GetPlaybackDuration() );
		OnResizeToContentEnabledChanged( m_playback->IsResizeToContentEnabled() );
	}

	void PlaybackInterop::OnStateChange( PlaybackState state )
	{
		String txt;
		text::SaveToText( &state, GetTypeObject<PlaybackState>(), txt );

		m_notifer.Notify( String::Printf( "OnPlaybackChanged %s", txt.AsChar() ) );
	}

	void PlaybackInterop::OnResizeToContentEnabledChanged( Bool value )
	{
		m_notifer.Notify( String::Printf( "OnResizeToContentEnabledChanged %s", ToStringDirect( value ).AsChar() ) );
	}

	void PlaybackInterop::OnEnableButton( const PlaybackButtonId& buttonId, Bool enable )
	{
		m_notifer.Notify( String::Printf( "OnEnableButton %s %s", buttonId, enable ? "true" : "false" ) );
	}

	void PlaybackInterop::OnToggleButton( const PlaybackButtonId& buttonId, Bool checked )
	{
		m_notifer.Notify( String::Printf( "OnToggleButton %s %s", buttonId, checked ? "true" : "false" ) );
	}

	void PlaybackInterop::OnStartTimeChanged( Float startTime )
	{
		m_notifer.Notify( String::Printf( "OnStartTimeChanged %f", startTime ) );
	}

	void PlaybackInterop::OnEndTimeChanged( Float endTime )
	{
		m_notifer.Notify( String::Printf( "OnEndTimeChanged %f", endTime ) );
	}

	void PlaybackInterop::OnDurationChanged( Float newDurationTime )
	{
		m_notifer.Notify( String::Printf( "OnDurationChanged %f", newDurationTime ) );
	}

	void PlaybackInterop::OnPlaybackPositionChanged( Float newTime )
	{
		m_notifer.Notify( String::Printf( "OnPlaybackPositionChanged %f", newTime ) );
	}

	void PlaybackInterop::OnPlaybackRatioChanged( Float newRatio )
	{
		m_notifer.Notify( String::Printf( "OnPlaybackRatioChanged %f", newRatio ) );
	}

	void PlaybackInterop::OnSubrangeEnabled( Bool enable )
	{
		m_notifer.Notify( String::Printf( "OnSubrangeEnableChanged %s", enable ? "true" : "false" ) );
	}

	void PlaybackInterop::OnSubrangeStartTime( Float value )
	{
		m_notifer.Notify( String::Printf( "OnSubrangeStartTimeChanged %f", value ) );
	}

	void PlaybackInterop::OnSubrangeDuration( Float value )
	{
		m_notifer.Notify( String::Printf( "OnSubrangeDurationChanged %f", value ) );
	}
}