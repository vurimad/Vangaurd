/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMeshPreviewPanel.h"
#include "meshStructureVisualization.h"
#include "backendMeshPanelSharedData.h"
#include "backendMeshPreviewPanelInputListener.h"

#include "../../backendEngine/include/objectDragDropListener.h"
#include "../../../common/resourceMesh/include/mesh.h"

namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	static const char* SDefaultEnvMode = "outdoor";
	rend::DebugFilter DF_MeshVisualizations( "Overlay/Visualizations" );

	//////////////////////////////////////////////////////////////////////////////////////////////
	MeshPreviewPanel::MeshPreviewPanel( ::tools::RenderingWindowPtr renderingWindow, const red::SharedPtr<GenericPreviewPanelSharedData>& sharedData )
		: GenericPreviewPanel( renderingWindow, sharedData, false )
		, m_forcedMeshHide( false )
		, m_forcedLodLevel( -1 )
		, m_visualizations( red::PoolBackend() )
		, m_visualizationsEnabled( true )
	{
		// set free camera with slight rotation
        auto box = GetMesh()->GetBoundingBox();
		if ( !box.IsEmpty() )
		{
			GetViewport()->SetupFreeCamera( box.CalcCenter() - box.CalcSize() * 0.5f, EulerAngles( 0, -30, 210 ) );
		}

		// zoom out to show the whole mesh
		ZoomOut();

		// create rendering visualization
		SetupRendering();
	}

	MeshPreviewPanel::~MeshPreviewPanel() = default;

	void MeshPreviewPanel::RemoveAllVisualizations()
	{
		m_visualizations.Clear();
	}

	void MeshPreviewPanel::AttachVisualization( MeshStructureNodeVisualizationPtr&& v )
	{
        RED_FATAL_ASSERT(v, "Error: Visualization is a nullptr");
		m_visualizations.EmplaceBack(std::forward<MeshStructureNodeVisualizationPtr>(v));
	}

	const Int32 MeshPreviewPanel::GetForcedLOD()
	{
		return m_forcedLodLevel;
	}

	Bool MeshPreviewPanel::SetForcedLOD( Int32 lodLevel )
	{
		if( m_forcedLodLevel != lodLevel )
		{
			m_forcedLodLevel = lodLevel;
			Refresh();
            return true;
		}

        return false;
	}

	void MeshPreviewPanel::SetMeshVisibility( const Bool isVisible )
	{
		const auto forceHide = !isVisible;
		if( forceHide != m_forcedMeshHide )
		{
			m_forcedMeshHide = forceHide;
			Refresh();
		}
	}

	void MeshPreviewPanel::SetMeshAppearance( CName newMeshAppearance )
	{
		if( newMeshAppearance != m_meshAppearance )
		{
			m_meshAppearance = newMeshAppearance;
			Refresh();
		}
	}

	void MeshPreviewPanel::EnableOrbitCamera( Bool enable )
	{
		EnableOrbitCameraInternal( enable, GetMesh()->GetBoundingBox().CalcCenter() );
	}

	Box MeshPreviewPanel::GetSceneBounds() const
	{
		return GetMesh()->GetBoundingBox();
	}

	THandle<CMesh> MeshPreviewPanel::GetMesh() const
	{
		return Cast<CMesh>(GetSharedData()->GetEditor().GetResource());
	}

	void MeshPreviewPanel::OnRender( RenderContext& ctx )
	{
		PreviewPanel::OnRender( ctx );

		ctx.m_frameInfo.m_worldMaxBounds = GetMesh()->GetBoundingBox();
		const Vector4 extents = ctx.m_frameInfo.m_worldMaxBounds.CalcExtents();
		// Extend the bounds on XY, to reduce artifacts on faces that are exactly on the bounds.
		ctx.m_frameInfo.m_worldMaxBounds.Min.X -= extents.X;
		ctx.m_frameInfo.m_worldMaxBounds.Min.Y -= extents.Y;
		ctx.m_frameInfo.m_worldMaxBounds.Max.X += extents.X;
		ctx.m_frameInfo.m_worldMaxBounds.Max.Y += extents.Y;

		const Bool showVis = ctx.m_cameraInfo.GetRenderingMask().Test( DF_MeshVisualizations );
		if( showVis != m_visualizationsEnabled )
		{
			SetVisualizationsVisibility( showVis );
		}

		if( showVis )
		{
			std::sort( m_visualizations.Begin(), m_visualizations.End(),
				[]( const MeshStructureNodeVisualizationPtr& a, const MeshStructureNodeVisualizationPtr& b )
			{ return a->GetRenderOrder() < b->GetRenderOrder(); } );

			MeshVisualizationRenderContext context( ctx.m_cameraInfo.GetDebugDrawer() );

			for( auto& vis : m_visualizations )
			{
				if( vis->GetType() == MeshVisualization_Texture::TYPE )
					++context.m_texturesCount;
			}

			for( auto& vis : m_visualizations )
				vis->RenderDebug( context );
		}
	}

	void MeshPreviewPanel::SetupRendering()
	{
		if( auto sharedData = GetSharedData() )
		{
			if( MeshPanelSharedData* meshSharedData = (MeshPanelSharedData*)(sharedData.Get()) )
			{
				meshSharedData->SetupRendering( GetMesh(), m_meshAppearance, m_forcedLodLevel );
			}
		}
	}

	void MeshPreviewPanel::Refresh()
	{
		SetupRendering();
		res::MonitorRouter::GetInstance().QueueChange( GetMesh()->GetPath(), RED_NAME_CONSTEXPR( "refresh" ) );
	}

	void MeshPreviewPanel::SetVisualizationsVisibility( Bool state )
	{
		if( state != m_visualizationsEnabled )
		{
			m_visualizationsEnabled = state;
			Refresh();
		}
	}

	void MeshPreviewPanel::OnEditorMessage( const String& message, const void* userData )
	{
		if( message == "refresh" )
			Refresh();
	}
}