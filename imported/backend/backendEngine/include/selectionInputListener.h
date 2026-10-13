/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "previewInputListener.h"

namespace tools
{
	class PreviewPanel;
	class SelectionHandler;
	class ShortcutManager;

	/// Translates user input to selection actions via the edit mode input listener
	/// triggers normal selection (point selection) on release of LMB
	/// if the action button (Ctrl) was pressed it starts an area selection on press of LMB
	/// it also tracks the multi-selection button (Shift)
	class BACKEND_ENGINE_API SelectionInputListener : public IPreviewInputListener
	{
		DECLARE_INTEROP( SelectionInputListener );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		SelectionInputListener( SelectionHandler& target );
		virtual ~SelectionInputListener();

		void RegisterShortcuts( ShortcutManager* manager, PreviewPanel* panel );

		// IPreviewInputListener interface
		virtual Bool OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt ) override;
		virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt ) override;

	protected:

		SelectionHandler& GetSelectionHandler() const { return m_target; }

		virtual Uint32 GetDefaultSelectionDepth() const;
		virtual Bool OnSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY );
		virtual Bool OnMultiselectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY );
		virtual Bool OnAdditiveAreaSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY );
		virtual Bool OnSubtractiveAreaSelectionAction( PreviewPanel& panel, Int32 mouseX, Int32 mouseY );
		virtual Bool OnClearSelectionAction();
		virtual Bool OnHideSelectionAction();

		Bool ShouldIgnoreSelection() const;

	private:

		SelectionHandler& m_target; // actual selection handler

		Bool m_ignoreSelection;

		Bool m_multiSelectionKey;
		Bool m_areaSelectionKey;

		// enable/disable multilayer selection for marquee selection
		Bool m_marqueeSelectsThroughObjects;
		interop::InteropNotifier m_marqueeSelectsThroughObjectsNotifier;
	};
}
