/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "selectionHandler.h"

namespace tools
{
	class BACKEND_ENGINE_API TimeLineSelectionHandler : public ISelectionHandler
	{
	public:
		TimeLineSelectionHandler( IEditActionHistory& actions, TSelectionChanged selectionCallback );

		virtual const SelectionStatePtr& GetSelection() const override { return m_selection; }
		virtual Bool HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc ) override;
		virtual Bool Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection = false ) override;

	private:
		SelectionStatePtr m_selection;

		IEditActionHistory* m_actions;
		TSelectionChanged m_selectionCallback;
	};
}