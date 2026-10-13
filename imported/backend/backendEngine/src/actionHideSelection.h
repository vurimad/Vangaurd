/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editAction.h"
#include "selectionFwd.h"

namespace tools
{
	class Action_HideSelection : public IEditAction
	{
	public:
		Action_HideSelection( SelectionStatePtr& selectionState );

	protected:

		// Inherited via IEditAction
		virtual String GetDescription() const override;
		virtual Bool Do() override;
		virtual Bool Undo() override;

	private:

		Bool SetVisibility( const Bool isVisible );

		SelectionStatePtr m_selectionState;
	};
} // tools
