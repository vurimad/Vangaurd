/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editorGraphSelectionHandler.h"

#include "editAction.h"
#include "editActionHistory.h"

namespace tools
{
	class Action_Selection1 : public IEditAction
	{
	public:
		RTTI_DECLARE_TYPE( Action_Selection1 );

		Action_Selection1( ISelectionHandler* selectionHandler, const String desc, Uint32 actionBatchIndex, SelectionStatePtr oldState, SelectionStatePtr newState )
			: m_handler( selectionHandler )
			, m_desc( desc )
			, m_batchIndex( actionBatchIndex )
			, m_oldState( oldState )
			, m_newState( newState )
		{}

		~Action_Selection1()
		{
		}

		virtual String GetDescription() const override
		{
			return m_desc;
		}

		virtual Bool Do() override
		{
			// this will return true if all of the selection objects were restored
			return m_handler->Action_ChangeSelection( m_newState );
		}

		virtual Bool Undo() override
		{
			// this will return true if all of the selection objects were restored
			return m_handler->Action_ChangeSelection( m_oldState );
		}

		/// Try to merge action with already executed action
		virtual Bool TryMerge( const IEditAction* lastUndoAction ) override
		{
			// merge selection actions if the batch index is the same
			if ( lastUndoAction->GetClass() == GetStaticClass() )
			{
				auto prev = static_cast< const Action_Selection1* >( lastUndoAction );
				if ( m_batchIndex == prev->m_batchIndex )
				{
					m_oldState = prev->m_oldState;
					return true;
				}
			}

			return false;
		}

		virtual Bool IsModification() const override
		{
			return false;
		}

	private:
		ISelectionHandler*		m_handler;		// actual selection handler that is responsible for the selection state

		String					m_desc;			// user description for the undo/redo actions

		Uint32					m_batchIndex;	// "batch index" of selection action, actions happening withing the same instant (frame) have the same batch index

		SelectionStatePtr		m_oldState;		// old selection state
		SelectionStatePtr		m_newState;		// new selection state
	};

	//----

	EditorGraphSelectionHandler::EditorGraphSelectionHandler( IEditActionHistory& actionHistory, TSelectionChanged selectionCallback, TSelectionUpdated selectionUpdatedCallback )
		: m_selection( RED_NEW( SelectionState )( ) )
		, m_actions( &actionHistory )
		, m_selectionCallback( selectionCallback )
		, m_selectionUpdatedCallback( selectionUpdatedCallback )
		, m_HACK_selectionInProgress{ false }
	{
	}

	Bool EditorGraphSelectionHandler::HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc )
	{
		if (!newSelection)
			return false;

		SelectionStatePtr oldState( m_selection->Clone() );
		if ( *oldState == *newSelection )
		{
			m_selectionUpdatedCallback();
			return false;
		}

		return m_actions->Execute( red::CreateUniquePtr< Action_Selection1 >( this, !desc.Empty() ? desc : "Selection changed", 0, oldState, newSelection ) );
	}

	Bool EditorGraphSelectionHandler::Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection )
	{
		m_HACK_selectionInProgress = true;

		if ( !m_selection )
			m_selection.Reset( RED_NEW( SelectionState )( ) );

		// make sure that all objects in the incoming selection set are valid
		if ( !state || !state->Restore() )
		{
			m_selection.Reset( RED_NEW( SelectionState )( ) );

			m_selectionCallback( m_selection );

			m_HACK_selectionInProgress = false;
			return false;
		}

		// remove selection visualization from all existing proxies that will NOT be in the new selection state
		/*for ( auto it = m_selection->Iterate(); it; ++it )
		{
			auto proxyId = it->GetEditorObjectID();
			if ( !state->Contains( proxyId ) )
			{
				// proxy is going to be deselected
				it->ToggleSelectionVisualization( false );
			}
		}

		// create selection visualization for all incoming selection proxies (ones that were NOT in the previous selection state)
		for ( auto it = state->Iterate(); it; ++it )
		{
			auto proxyId = it->GetEditorObjectID();
			if ( !m_selection->Contains( proxyId ) )
			{
				// proxy is going to be selected
				it->ToggleSelectionVisualization( true );
			}
		}*/

		// set new selection set
		m_selection = state;

		// notify our client
		m_selectionCallback( m_selection );
		
		m_HACK_selectionInProgress = false;
		// processed
		return true;
	}

	Bool EditorGraphSelectionHandler::HACK_IsCurrentlySelecting() const
	{
		return m_HACK_selectionInProgress;
	}
}

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Action_Selection1, tools )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()