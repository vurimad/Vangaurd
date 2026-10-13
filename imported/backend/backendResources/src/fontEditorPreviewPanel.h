/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/genericPreviewPanel.h"

namespace ink
{
	class CanvasWidget;
	class WidgetBrush;
}

namespace tools
{
	class FontEditorPreviewPanelSharedData;

	class BACKEND_RESOURCES_API FontEditorPreviewPanel : public GenericPreviewPanel
	{
		DECLARE_INTEROP_DERIVED( FontEditorPreviewPanel, GenericPreviewPanel );
		RED_BASE_CLASS( GenericPreviewPanel );

	public:
		FontEditorPreviewPanel( const red::SharedPtr<RenderingWindow>& renderingWindow, const red::SharedPtr<FontEditorPreviewPanelSharedData>& sharedData );
		virtual ~FontEditorPreviewPanel();

		virtual void OnRender ( RenderContext& ctx ) override;

		const red::SharedPtr< FontEditorPreviewPanelSharedData > GetSharedData() const;
	};

}	// namespace tools
