/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "actionHideSelection.h"

#include "selectionIterator.h"
#include "selectionState.h"

namespace tools {

Action_HideSelection::Action_HideSelection( SelectionStatePtr& selectionState ) : m_selectionState( selectionState )
{

}

String Action_HideSelection::GetDescription() const
{
	return "Hide selection";
}

Bool Action_HideSelection::Do()
{
	return SetVisibility( false );
}

Bool Action_HideSelection::Undo()
{
	return SetVisibility( true );
}

Bool Action_HideSelection::SetVisibility( const Bool isVisible )
{
	Bool success = false;

	SelectionIterator it = m_selectionState->Iterate();
	while ( it )
	{
		it->SetVisibility( isVisible );
		++it;

		success = true;
	}

	return success;
}

} // namespace tools {
