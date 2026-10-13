/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "genericPreviewPanelSharedData.h"
#include "selectionHandler.h"
#include "selectionInputListener.h"
#include "objectHandler.h"
#include "objectCollection.h"
#include "selectionIterator.h"
#include "snapManager.h"
#include "resourceBackend.h"
#include "resourceEditor.h"

#include "../../../common/renderData/include/debugFilter.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/renderFrameInfo.h"

namespace Config
{
	TConfigVar< Color > cvSelectionBoundsColor( "Editor/Selection", "BoundsColor", Color( 240, 160, 40, 200 ), eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
}

namespace tools
{
	GenericPreviewPanelSharedData::GenericPreviewPanelSharedData( IResourceEditor& editor, const TRenderPtr<IRenderScene>& renderScene, IRenderScene::Type sceneType )
		: PreviewPanelSharedData( editor, renderScene, sceneType )
		, m_selectedObjects( editor.GetSharedSelection( RED_NAME_CONSTEXPR( "Preview" ) ) )
	{
		// create the selection handling
		{
			m_selectionHandler = red::CreateUniquePtr< SelectionHandler >( &GetActionHistory() );
			m_selectionCallbackToken = m_selectionHandler->AttachSelectionCallback(
				[this](const SelectionStatePtr& ptr) { SelectionChanged(ptr); });

			m_selectionHandler->AttachUserSelectionUpdatedCallback(
				[this]() { m_selectedObjects->UpdateGlobalSelection(); });
		}

		// create the object handlers to deal with the world
		{
			// create the object handler for the world object
			m_objectHandler = red::CreateUniquePtr< ObjectHandler >( GetActionHistory(), GetResourceBackend().GetClipboard(), m_selectionHandler.Get() );
			m_selectionHandler->SetObjectHandler( m_objectHandler.Get(), GetEditor() );
		}
		
		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{ 
			//if( m_selectedObjects->HasObject( id ) )
			//{
				// refresh custom properties
				// WCz: https://jira.cdprojektred.com/browse/CYB-178262 we need to find some other way to keep custom properties in sync
				// this solution is too heavy (workaround when only one object is selected)
// 				if( m_selectedObjects->GetViews().Size() == 1 )
// 				{
// 					m_selectedObjects->Refresh();
// 				}
			//}

			// handle custom behavior
			OnNotifyPropertyChanged( id, path );
		} );
	}

	GenericPreviewPanelSharedData::~GenericPreviewPanelSharedData()
	{
		m_selectionHandler->DetachSelectionCallback(m_selectionCallbackToken);
		m_selectionCallbackToken = 0;

		m_objectHandler.Reset();
		m_selectionHandler.Reset();

		// not owned by this class
		m_selectedObjects = nullptr;
	}

	void GenericPreviewPanelSharedData::SelectObjects( const red::DynArray< EditorObjectID >& objectIds )
	{
		auto selection = BuildSelection( objectIds );
		m_selectionHandler->HandleDirectSelection( selection, "Objects selected" );
	}

	void GenericPreviewPanelSharedData::SelectObjects( const red::DynArray< EditorObjectIDPath >& objectPaths )
	{
		auto selection = BuildSelection( objectPaths );
		m_selectionHandler->HandleDirectSelection( selection, "Objects selected" );
	}

	ObjectCollection* GenericPreviewPanelSharedData::GetSelectedObjects() const
	{
		return m_selectedObjects;
	}

	void GenericPreviewPanelSharedData::DeselectObjects( const red::DynArray< EditorObjectIDPath >& paths )
	{
		auto selection = m_selectionHandler->CloneSelection();
		for ( const EditorObjectIDPath& path : paths )
		{
			selection->Remove( path );
		}

		m_selectionHandler->HandleDirectSelection( selection, "Objects deselected" );
	}

	void GenericPreviewPanelSharedData::GetSelectedIDPaths( red::DynArray< EditorObjectIDPath >& nodePaths ) const
	{
		SelectionStatePtr selection = GetSelectionHandler().GetSelection();

		if ( !selection )
			return;

		nodePaths.Reserve( selection->Size() );

		SelectionIterator it = selection->Iterate();
		while ( it )
		{
			red::alg::PushBackUnique( nodePaths, it->GetEditorObjectIDPath() );
			++it;
		}
	}

	const SelectionStatePtr& GenericPreviewPanelSharedData::GetSelection() const
	{
		return m_selectionHandler->GetSelection();
	}

	void GenericPreviewPanelSharedData::SetupInterop(GenericPreviewPanelSharedData* ptr, ::interop::utils::Object& op)
	{
		op.RegisterObject("objects", ptr->m_objectHandler.Get());

		op.RegisterValue<Uint32>("selectionCount", [ptr](::interop::IContext& ctx, Uint32& outValue)
		{
			auto& selection = ptr->GetSelectionHandler().GetSelection();
			outValue = selection ? selection->Size() : 0;
		});

		op.RegisterFunctor<void>( "clearSelection", [ptr]( ::interop::IContext& ctx )
		{
			ptr->GetSelectionHandler().NextSelectionBatch();
			ptr->GetSelectionHandler().ClearSelection();
		} );
	}

	void GenericPreviewPanelSharedData::SelectionChanged(const red::SharedPtr< SelectionState >& selection)
	{
		// update the edition container with new selected object
		m_selectedObjects->SetObjectsFromSelection(selection);

		// TODO: propagate to all panels
	}

	SelectionStatePtr GenericPreviewPanelSharedData::BuildSelection( const red::ArraySpan< const EditorObjectID >& objectIds )
	{
		SelectionStatePtr state = red::CreateSharedPtr< SelectionState >();

		for ( const EditorObjectID& id : objectIds )
		{
			auto object = m_objectHandler->ResolveProxy( id );
			if ( object )
			{
				state->Add( object );
			}
		}

		return state;
	}

	SelectionStatePtr GenericPreviewPanelSharedData::BuildSelection( const red::ArraySpan< const EditorObjectIDPath >& objectPaths )
	{
		SelectionStatePtr state = red::CreateSharedPtr< SelectionState >();

		IObjectResolver::ResolvingParams params;
		params.m_isForSelection = true;
		for ( const EditorObjectIDPath& path : objectPaths )
		{
			auto object = m_objectHandler->ResolveProxy( path, params );
			if (object)
			{
				state->Add(object);
			}
		}

		return state;
	}

	const Bool GenericPreviewPanelSharedData::ComputeObjectBounds(const red::DynArray< EditorObjectID >& objectIDs, Box& outBounds)
	{
		SelectionStatePtr state = red::CreateSharedPtr< SelectionState >();

		for (const auto& id : objectIDs)
		{
			auto object = m_objectHandler->ResolveProxy(id);
			if (object)
			{
				state->Add(object);
			}
		}

		return ComputeObjectBounds( state, outBounds );
	}

	const Bool GenericPreviewPanelSharedData::ComputeObjectBounds(const SelectionStatePtr& selection, Box& outBounds)
	{
		if ( selection )
		{
			Bool hasValidBounds = false;
			Box selectionBounds;
			selectionBounds.Clear();

			for ( auto it = selection->Iterate(); it; ++it )
			{
				hasValidBounds = true;
				selectionBounds.AddBox( it->GetWorldBoundingBox() );
			}

			if ( !selectionBounds.IsEmpty() )
			{
				outBounds = selectionBounds;
				return true;
			}
		}

		return false;
	}

	Bool GenericPreviewPanelSharedData::CalculateSelectionInfo( const Vector3& refPoint, SelectionInfo& info ) const
	{
		info.m_minDistance = RED_FLT_MAX;
		info.m_bounding.Clear();
		info.m_numItems = 0;

		if(auto selection = GetSelectionHandler().GetSelection())
		{
			for(auto it = selection->Iterate(); it; ++it)
			{
				const Float dst = refPoint.DistanceSquaredTo( it->GetWorldTransform().GetPosition3() );

				info.m_numItems++;
				info.m_bounding.AddBox( it->GetWorldBoundingBox() );
				info.m_minDistance = std::min( info.m_minDistance, dst );
			}
		}

		if(info.m_numItems)
		{
			info.m_minDistance = std::sqrt( info.m_minDistance );
		}

		return info.m_numItems > 0;
	}

	void GenericPreviewPanelSharedData::RenderSelectionBounds( rend::IDebugDrawer& dd )
	{
		static rend::DebugFilter DF_SelectionBounds( "Selection/Bounds" );

		if ( dd.GetMask().Test( DF_SelectionBounds ) )
		{
			math::Box selectionBounds = math::Box::EMPTY();

			const SelectionStatePtr& selection = GetSelection();
			for ( auto iter = selection->Iterate(); iter; ++iter )
			{
				selectionBounds.AddBox( iter->GetWorldBoundingBox() );
			}

			if ( !selectionBounds.IsEmpty() )
			{
				rend::FragmentBuilder builder( dd );
				builder.SetColor( Config::cvSelectionBoundsColor.Get() );
				builder.EnableTransparency( true );
				builder.AddWireBox( selectionBounds );
			}
		}
	}

	void GenericPreviewPanelSharedData::OnRenderSharedState(CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo)
	{
		PreviewPanelSharedData::OnRenderSharedState(info, cameraInfo);

		RenderSelectionBounds( cameraInfo.GetDebugDrawer() );
	}

	void GenericPreviewPanelSharedData::OnUpdateSharedState( const UpdateContext& ctx )
	{
		PreviewPanelSharedData::OnUpdateSharedState( ctx );
	}

	void GenericPreviewPanelSharedData::OnNotifyPropertyChanged( const SerializableID& id, const rtti::AccessPath& path )
	{}
	
} // tools
