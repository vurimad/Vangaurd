/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timelineSelectionHandler.h"

#include "editAction.h"
#include "editActionHistory.h"
#include "timeLineActions.h"

namespace tools
{
	TimeLineSelectionHandler::TimeLineSelectionHandler( IEditActionHistory& actionHistory, TSelectionChanged selectionCallback )
		: m_selection( RED_NEW( SelectionState )() )
		, m_actions( &actionHistory )
		, m_selectionCallback( selectionCallback )
	{
	}

	Bool TimeLineSelectionHandler::HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc )
	{
		if (!newSelection)
			return false;

		SelectionStatePtr oldState( m_selection->Clone() );
		if ( *oldState == *newSelection )
			return false;

		return m_actions->Execute( red::CreateUniquePtr< TimelineSelectionAction >( this, !desc.Empty() ? desc : "Selection changed", 0, oldState, newSelection ) );
	}

	Bool TimeLineSelectionHandler::Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection )
	{
		if ( !m_selection )
			m_selection.Reset( RED_NEW( SelectionState )( ) );

		// make sure that all objects in the incoming selection set are valid
		if ( !state || !state->Restore() )
		{
			m_selection.Reset( RED_NEW( SelectionState )( ) );

			m_selectionCallback( m_selection );

			return false;
		}

		// set new selection set
		m_selection = state;

		// notify our client
		m_selectionCallback( m_selection );

		// processed
		return true;
	}
}