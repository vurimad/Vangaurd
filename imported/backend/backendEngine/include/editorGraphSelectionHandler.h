/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "selectionHandler.h"

namespace tools
{
	class BACKEND_ENGINE_API EditorGraphSelectionHandler : public ISelectionHandler
	{
	public:
		EditorGraphSelectionHandler( IEditActionHistory& actions, TSelectionChanged selectionCallback, TSelectionUpdated selectionUpdatedCallback );

		virtual const SelectionStatePtr& GetSelection() const override { return m_selection; }
		virtual Bool HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc ) override;
		virtual Bool Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelectoin = false ) override;

		// CAUTION: Don't use it please! We need it to be easy to remove latter.
		// Why is needed: 
		//		We don't have a way to know if selection comes from (backend or frontend).
		//		Due to this, Action_ChangeSelection is called from several paths.
		//		In some case (undo/redo) this paths converge and we try to create multiple ChangeSelection editor actions.
		//		Trying to execute multiple actions at the same time is not allowed.
		// Thanks to this we can avoid creating selection actions when we don't need it.
		Bool HACK_IsCurrentlySelecting() const;

	private:
		SelectionStatePtr m_selection;

		IEditActionHistory* m_actions;

		TSelectionChanged m_selectionCallback;

		TSelectionUpdated m_selectionUpdatedCallback;

		// HACK: See HACK_IsCurrentlySelecting
		Bool m_HACK_selectionInProgress;
	};
}