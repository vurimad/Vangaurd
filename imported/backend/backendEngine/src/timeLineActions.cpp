/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineActions.h"

namespace tools
{
	String TimelineSelectionAction::s_description = String( "Select timeline events" );

	TimelineSelectionAction::TimelineSelectionAction( ISelectionHandler* selectionHandler, const String desc, Uint32 actionBatchIndex, SelectionStatePtr oldState, SelectionStatePtr newState )
		: m_handler( selectionHandler )
		, m_desc( desc )
		, m_batchIndex( actionBatchIndex )
		, m_oldState( oldState )
		, m_newState( newState )
	{
	}

	TimelineSelectionAction::~TimelineSelectionAction()
	{
	}

	String TimelineSelectionAction::GetDescription() const
	{
		return s_description;
	}

	Bool TimelineSelectionAction::Do()
{
		// this will return true if all of the selection objects were restored
		return m_handler->Action_ChangeSelection( m_newState );
	}

	Bool TimelineSelectionAction::Undo()
{
		// this will return true if all of the selection objects were restored
		return m_handler->Action_ChangeSelection( m_oldState );
	}

	Bool TimelineSelectionAction::IsModification() const
	{
		return false;
	}

	///
	///
	///

	TimelineMoveEvents::TimelineMoveEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, Float timeChange )
		: m_eventsId{ ids, { red::PoolBackend() } }
		, m_timeline( timeline )
		, m_timeOffset( timeChange )
	{
	}

	String TimelineMoveEvents::GetDescription() const
	{
		return "Move events by the time offset";
	}

	Bool TimelineMoveEvents::Do()
{
		for ( TimelineId id : m_eventsId )
		{
			m_timeline->MoveEvent( id, m_timeOffset );
		}
		return true;
	}

	Bool TimelineMoveEvents::Undo()
{
		for ( TimelineId id : m_eventsId )
		{
			m_timeline->MoveEvent( id, -m_timeOffset );
		}
		return true;
	}

	///
	///
	///

	TimelineMuteSelectedEvents::TimelineMuteSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, Bool isMuted )
		: m_eventsId{ ids, { red::PoolBackend() } }
		, m_timeline( timeline )
		, m_isMuted( isMuted )
		, m_previousMuteState{ red::PoolBackend() }
	{
		m_previousMuteState.Reserve( m_eventsId.Size() );
		for ( const TimelineId id : m_eventsId )
		{
			m_previousMuteState.Insert( id, m_timeline->IsEventMuted( id ) );
		}
	}

	String TimelineMuteSelectedEvents::GetDescription() const
	{
		return "Mute selected events";
	}

	Bool TimelineMuteSelectedEvents::Do()
	{
		for ( const TimelineId id : m_eventsId )
		{
			m_timeline->MuteEvent( id, m_isMuted );
		}

		return true;
	}

	Bool TimelineMuteSelectedEvents::Undo()
	{
		for ( const TimelineId id : m_eventsId )
		{
			m_timeline->MuteEvent( id, m_previousMuteState[ id ] );
		}

		return true;
	}

	///
	///
	///

	TimelineToggleMuteSelectedEvents::TimelineToggleMuteSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids )
		: m_eventsId{ ids, { red::PoolBackend() } }
		, m_timeline( timeline )
		, m_previousMuteState{ red::PoolBackend() }
	{
		m_previousMuteState.Reserve( m_eventsId.Size() );
		for ( const TimelineId id : m_eventsId )
		{
			m_previousMuteState.Insert( id, m_timeline->IsEventMuted( id ) );
		}
	}

	String TimelineToggleMuteSelectedEvents::GetDescription() const
	{
		return "Toggle mute on selected events";

	}

	Bool TimelineToggleMuteSelectedEvents::Do()
	{
		for ( const TimelineId id : m_eventsId )
		{
			m_timeline->MuteEvent( id, !m_previousMuteState[ id ] );
		}

		return true;
	}

	Bool TimelineToggleMuteSelectedEvents::Undo()
	{
		for ( const TimelineId id : m_eventsId )
		{
			m_timeline->MuteEvent( id, m_previousMuteState[ id ] );
		}

		return true;
	}

	///
	///
	///

	TimelineResizeSelectedEvents::TimelineResizeSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, TimelineResizeMode mode, Float timeChange )
		: m_eventsId{ ids, { red::PoolBackend() } }
		, m_timeline( timeline )
		, m_timeOffset( timeChange )
		, m_mode( mode )
	{
	}

	String TimelineResizeSelectedEvents::GetDescription() const
	{
		return "Resize selected events by the time offset";
	}

	Bool TimelineResizeSelectedEvents::Do()
{
		for ( TimelineId id : m_eventsId )
		{
			m_timeline->ResizeEvent( id, m_mode, m_timeOffset );
		}
		return true;
	}

	Bool TimelineResizeSelectedEvents::Undo()
{
		for ( TimelineId id : m_eventsId )
		{
			m_timeline->ResizeEvent( id, m_mode, -m_timeOffset );
		}
		return true;
	}

	///
	///
	///

	TimelineClipSelectedEvents::TimelineClipSelectedEvents( ITimeLine* timeline, const red::DynArray< TimelineId >& ids, TimelineResizeMode mode, Float timeChange )
		: m_eventsId{ ids, { red::PoolBackend() } }
		, m_timeline( timeline )
		, m_timeOffset( timeChange )
		, m_mode( mode )
	{
	}

	String TimelineClipSelectedEvents::GetDescription() const
	{
		return "Clip selected events by the time offset";
	}

	Bool TimelineClipSelectedEvents::Do()
	{
		for( TimelineId id : m_eventsId )
		{
			m_timeline->ClipEvent( id, m_mode, m_timeOffset );
		}
		return true;
	}

	Bool TimelineClipSelectedEvents::Undo()
	{
		for( TimelineId id : m_eventsId )
		{
			m_timeline->ClipEvent( id, m_mode, -m_timeOffset );
		}
		return true;
	}
}