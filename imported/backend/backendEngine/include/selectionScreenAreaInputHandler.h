/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "previewInputHandler.h"

namespace tools
{
	class PreviewPanel;
	class SelectionHandler;

	/// input handler (capture mode input) for handling area selection
	class SelectionScreenAreaInputHandler : public IPreviewInputHandler
	{
	public:
		SelectionScreenAreaInputHandler( SelectionHandler& target, Int32 startX, Int32 startY, const SelectionContext& selectionContext );

		// IPreviewInputHandler interface
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override;
		virtual Bool OnMouseUp( const InputMouseButton& evt ) override;
		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override;

	private:
		SelectionHandler& m_target;

		Int32 m_startX;
		Int32 m_startY;
		Int32 m_endX;
		Int32 m_endY;

		const SelectionContext m_selectionContext;

		const Rect CalcSelectionRect() const;
	};

}
