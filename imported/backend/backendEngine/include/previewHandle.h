/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
 
#pragma once

#include "functional"

namespace tools
{
	class IEditActionHistory;

	// Helper class for preview. Uses session data.
	template<class TPreviewClass>
	class PreviewHandle
	{
	public:
		typedef red::SharedPtr< TPreviewClass > PreviewPanelSharedPtr;

		void SetPreviewPanel( PreviewPanelSharedPtr panel )
		{
			if( IsPreviewValid() )
				return;

			m_panelPtr = panel;
		}

		// Reset preview (destroys preview panel and window)
		void Reset()
		{
			m_panelPtr.Reset();
		}

		// Get preview panel
		TPreviewClass* Get()
		{
			return m_panelPtr.Get();
		}

		// Checks if preview is created
		Bool IsPreviewValid()
		{
			return m_panelPtr != nullptr;
		}

	private:
		PreviewPanelSharedPtr m_panelPtr;
	};
}
