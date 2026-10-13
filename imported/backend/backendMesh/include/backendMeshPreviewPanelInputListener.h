/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/previewInputListener.h"

class CMesh;
class IRenderProxy;

namespace tools
{
	class MeshPreviewPanel;
	class ObjectHandler;
	class ShortcutManager;

	struct MarkVerticesHandlerOptions : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( MarkVerticesHandlerOptions );
	
		Float m_radius = 0.02f;
		Float m_radiusStep = 0.005f;
		Float m_verticeSize = 0.002f;
		Float m_boxForPretestSize = 0.2f;

		Color m_selectedVerticesColor = Color( 0, 255, 32, 128 );
		Color m_freeVerticesColor = Color( 255, 0, 0, 128 );
		Color m_selectedBoxSolidColor = Color( 0, 192, 192, 96 );
		Color m_selectedBoxOutlineColor = Color( 0, 192, 192, 255 );

		Color m_sphereSelectingColor = Color( 0, 192, 0, 112 );
		Color m_sphereFreeingColor = Color( 192, 0, 0, 112 );

		Bool m_enableBoxOverlay = true;
		Bool m_renderSelectedBox = true;
		Bool m_renderSelectedVertices = true;

	};

	/// selection input listener
	class MeshPreviewInputListener : public IPreviewInputListener
	{
	public:
		MeshPreviewInputListener( const res::ResourcePath& path, ObjectHandler& handler, const THandle<MarkVerticesHandlerOptions>& verticesHandlerOptions );
		virtual ~MeshPreviewInputListener();

		void RegisterShortcuts(ShortcutManager& manager, MeshPreviewPanel* panel);
        void UnregisterShortcuts(ShortcutManager& manager);
		virtual void OnRender(const CRenderCamera& camera, rend::IDebugDrawer& dd) const override final;
		virtual IPreviewDragDropHandler* OnDragEnter( PreviewPanel& panel, const String& data, const InputMouseMove& evt ) override final;
		virtual void OnDragDrop() override final;
		virtual void OnDragLeave() override final;

		red::DynArray<Vector3> GetSelectedVertices();
	protected:
		Bool OnMarkVerticesAction(MeshPreviewPanel& panel, Int32 mouseX, Int32 mouseY, Bool isSelecting);
		void ClearSelected();

	private:
		ObjectHandler& m_handler;
		red::DynArray< Vector3 > m_meshVertices;
		red::DynArray< Bool > m_meshVerticeSelectionLookup;
		Box m_meshBounds;
		res::ResourcePath m_resource;
		Uint64 m_currentMeshId;
		THandle<MarkVerticesHandlerOptions> m_markVerticesHandlerOptions;
	};
}
