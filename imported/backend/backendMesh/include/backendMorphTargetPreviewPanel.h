/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/previewPanel.h"

namespace MorphTargets
{
	struct MorphTargetWeight;
}
class MorphTargetMesh;

namespace tools
{
	class MorphTargetResourceEditor;

	class MorphTargetPreviewPanel: public PreviewPanel
	{

	public:
		MorphTargetPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState );
		virtual ~MorphTargetPreviewPanel();

		void ReleaseRenderProxy();
		void SetupRendering( const THandle< MorphTargetMesh >& mesh );
		void SetMorphWeights( const red::DynArray< MorphTargets::MorphTargetWeight >& morphWeights );

		virtual void OnRender( RenderContext& ctx ) override;
		virtual void OnUpdate( const UpdateContext& ctx ) override;
		
		void PrepareRenderProxy( const THandle< MorphTargetMesh >& morph );

	private: 
		red::UniquePtr< RenderProxyHandle >	m_renderProxy;
		MorphTargetResourceEditor*			m_editor;
	};

	typedef red::SharedPtr< MorphTargetPreviewPanel > MorphTargetPreviewPanelPtr;

} // namespace tools

