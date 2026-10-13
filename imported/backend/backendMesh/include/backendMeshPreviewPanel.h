/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "meshStructure.h"
#include "../../backendEngine/include/genericPreviewPanel.h"

class CMesh;
class IRenderProxy;

namespace tools
{
	class IResourceEditor;
	class GenericPreviewPanelSharedData;
	class MeshPreviewPanel;
	class MeshPreviewInputListener;

	/// mesh preview panel
	class MeshPreviewPanel : public GenericPreviewPanel, public IMeshVisualizationContainer
	{
        RED_BASE_CLASS(GenericPreviewPanel);

	public:
		MeshPreviewPanel( ::tools::RenderingWindowPtr renderingWindow, const red::SharedPtr<GenericPreviewPanelSharedData>& sharedData );
		virtual ~MeshPreviewPanel();

		void Refresh();
		void SetMeshAppearance( CName newMeshAppearance );
		virtual void EnableOrbitCamera( Bool enable ) override;
		virtual Box GetSceneBounds() const override;
		THandle<CMesh> GetMesh() const;

	private:
		void SetVisualizationsVisibility( Bool state );
		virtual void RemoveAllVisualizations() override final;
		virtual void AttachVisualization( MeshStructureNodeVisualizationPtr&& v) override final;
		virtual Bool SetForcedLOD(Int32 lodLevel) override final;
		virtual const Int32 GetForcedLOD() override final;
		virtual void SetMeshVisibility(const Bool isVisible) override final;
		virtual void OnRender( RenderContext& ctx ) override final;
		virtual void OnEditorMessage(const String& message, const void* userData) override final;
		void SetupRendering();

		CName				m_meshAppearance;

		Bool				m_forcedMeshHide;			// hide mesh (we are rendering an overlay)
		Int32				m_forcedLodLevel;			// forced mesh LOD level (-1 if not used)

		using TVisualizations = red::DynArray< MeshStructureNodeVisualizationPtr >;
		TVisualizations		m_visualizations;
		Bool				m_visualizationsEnabled;	// are visualizations enabled?

		red::UniquePtr< MeshPreviewInputListener > m_meshPreviewInputListener;
	};
}
