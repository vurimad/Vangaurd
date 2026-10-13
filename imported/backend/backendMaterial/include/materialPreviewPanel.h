/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/previewPanel.h"

class CMesh;
class IMaterial;

namespace tools
{
	class MaterialPreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( MaterialPreviewPanel, PreviewPanel );
	public:
		MaterialPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState );
		virtual ~MaterialPreviewPanel();

		void SetupRendering( const THandle< CMesh >& mesh, const THandle< IMaterial >& material );
		void ReleaseRendering();
		TRenderPtr<IRenderProxy> CreateRenderProxy( const TRenderPtr<IRenderScene>& scene, const THandle< CMesh >& mesh, const THandle< IMaterial >& material );

	private:
		TRenderPtr<IRenderProxy> m_renderProxy;
	};

	typedef red::SharedPtr< MaterialPreviewPanel > MaterialPreviewPanelPtr;

} // namespace tools