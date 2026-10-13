/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "mlmaskView.h"

#include "../../backendEngine/include/previewPanel.h"

class CMaterialInstance;

namespace tools
{
	class MLMaskPreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( MLMaskPreviewPanel, PreviewPanel );
	public:
		MLMaskPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState );
		virtual ~MLMaskPreviewPanel();

		virtual void OnRender( RenderContext& context ) override final;

		virtual void SetController( const red::SharedPtr< MLMaskController >& options );

		void OnParametersChanged();

	private:
		void SetupRendering();

		red::SharedPtr< MLMaskController > m_controller;
		THandle<CMaterialInstance> m_previewMatInst;
	};
} // namespace tools