#include "build.h"
#include "meshEditModes.h"

#include "physicsObjectResolver.h"
#include "physicsEditorScene.h"
#include "physicsShapeRenderer.h"

#include "backendMeshPanelSharedData.h"
#include "backendMeshPreviewPanelInputListener.h"
#include "backendMeshPreviewPanel.h"

#include "../../backendEngine/include/renderingWindow.h"
#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../backendEngine/include/selectionHandler.h"

namespace tools
{
	MeshEditModePhysics::MeshEditModePhysics( PhysicsObjectResolver* objectResolver )
		: TBaseClass( "MeshEditorPhysics" )
		, m_resolver( objectResolver )
	{
		RED_ASSERT( objectResolver, "Trying to create edit mode physics with an invalid object resolver." );
	}

	MeshEditModePhysics::~MeshEditModePhysics() = default;

	Bool MeshEditModePhysics::OnActivate( IPreviewEditMode* currentMode )
	{
		TBaseClass::OnActivate( currentMode );

		auto* editorScene = m_resolver->GetPhysicsEditorScene();
		RED_ASSERT( editorScene != nullptr, "Editor scene not present during edit mode activation." );
		editorScene->SetVisible( true );

		m_meshPreviewInputListener = red::CreateUniquePtr< MeshPreviewInputListener >( GetSharedData()->GetEditor().GetPath(),
			GetSharedData()->GetObjectHandler(), editorScene->m_physicsEditorOptions->m_markVerticesHandlerOptions );

        for( auto* panel : GetSharedData()->GetPreviewPanels() )
        {
            panel->GetCamera()->ResetDefaultMappings();
            panel->GetCamera()->OrbitLockRotateDuringZoom(false);

            panel->AttachInputListener( m_meshPreviewInputListener.Get());
            m_meshPreviewInputListener->RegisterShortcuts( *panel->GetShortcutManager(), static_cast<MeshPreviewPanel*>(panel) );

        }

		return true;
	}

	void MeshEditModePhysics::OnDeactivate()
	{
		physics::PhysicsEditorScene* editorScene = m_resolver->GetPhysicsEditorScene();
		if( editorScene )
		{
			editorScene->SetVisible( false );
		}
		
		// Clear selection so we can't modify stuff we are not editing
		MeshPanelSharedData* meshPanelSharedData = GetSharedData();
		if( meshPanelSharedData )
		{
			SelectionHandler& selectionHandler = meshPanelSharedData->GetSelectionHandler();
			selectionHandler.ClearSelection();
		}

        for( auto* panel : GetSharedData()->GetPreviewPanels() )
        {
            m_meshPreviewInputListener->UnregisterShortcuts(*panel->GetShortcutManager());
            panel->DetachInputListener(m_meshPreviewInputListener.Get());
        }
	}

	void MeshEditModePhysics::OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo )
	{
		TBaseClass::OnRender( frame, cameraInfo );

		MeshPanelSharedData* meshPanelSharedData = GetSharedData();
		if( meshPanelSharedData )
		{
			const physics::PhysicsRendererOptionsPtr& options = meshPanelSharedData->GetRenderOptions();
			options->m_renderScene = meshPanelSharedData->GetRenderScene();
			physics::PhysicsEditorScene* editorScene = m_resolver->GetPhysicsEditorScene();
			if( editorScene )
			{
				editorScene->PrepareForRendering();
				editorScene->OnRender( cameraInfo.GetDebugDrawer() );
			}
		}
	}

	void MeshEditModePhysics::OnUpdate( const UpdateContext& ctx )
	{
		TBaseClass::OnUpdate( ctx );

		physics::PhysicsEditorScene* editorScene = m_resolver->GetPhysicsEditorScene();
		if( editorScene )
		{
			editorScene->OnUpdate( ctx.m_timeDelta );
		}
	}

}