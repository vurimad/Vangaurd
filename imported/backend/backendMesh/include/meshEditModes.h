#pragma once

#include "../src/backendMeshPreviewEditMode.h"
#include "backendMeshPanelSharedData.h"

namespace tools
{
    class MeshPreviewInputListener;

	// Physics edit mode for mesh editor
	class BACKEND_MESH_API MeshEditModePhysics : public MeshPreviewEditMode
	{
        RED_BASE_CLASS(MeshPreviewEditMode);

	public:
		MeshEditModePhysics( PhysicsObjectResolver* objectResolver );
		virtual ~MeshEditModePhysics();

		virtual Bool OnActivate( IPreviewEditMode* currentMode ) override;
		virtual void OnDeactivate() override;
		virtual void OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo ) override;
		virtual void OnUpdate( const UpdateContext& ctx ) override;
		virtual void OnStateChanged(const ClothEditorState& ) override final{}

		PhysicsObjectResolver* m_resolver;

	private:
        red::UniquePtr<MeshPreviewInputListener> m_meshPreviewInputListener;
	};
}

