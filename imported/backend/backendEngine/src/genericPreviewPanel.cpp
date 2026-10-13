/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "genericPreviewPanel.h"
#include "genericPreviewPanelSharedData.h"
#include "objectDragDropListener.h"
#include "editableProxy.h"

#include "selectionHandler.h"
#include "selectionInputListener.h"

#include "gizmoManager.h"
#include "gizmoCollection.h"
#include "gizmoTransformHandler.h"

#include "previewPanel.h"
#include "previewInput.h"

namespace tools
{

	GenericPreviewPanel::GenericPreviewPanel( RenderingWindowPtr renderingWindow, const GenericPreviewPanelSharedDataPtr& sharedData, Bool attachDragDropListener )
		: PreviewPanel( renderingWindow, sharedData )
		, m_gizmoManager( nullptr )
		, m_gizmoCollection( nullptr )
		, m_selectionInput( nullptr )
		, m_selectionCallbackToken( 0 )
		, m_objectDragDropListener( nullptr )
	{
		// create input listener for selection handling
		m_selectionInput = red::CreateUniquePtr< SelectionInputListener >( sharedData->GetSelectionHandler() );
		AttachInputListener( m_selectionInput.Get() );
		m_selectionInput->RegisterShortcuts( GetShortcutManager().Get(), this );

		// register selection notification
		m_selectionCallbackToken = sharedData->GetSelectionHandler().AttachSelectionCallback(
			[this](const SelectionStatePtr& ptr) { SelectionChanged(ptr); });

		// create generic drag&drop listener
        if( attachDragDropListener )
		{
			m_objectDragDropListener = red::CreateUniquePtr< ObjectDragDropListener >( &sharedData->GetObjectHandler() );
			AttachInputListener( m_objectDragDropListener.Get() ); // this connects the listener with OnDragEnter event
		}

		// create the gizmo system
		{
			m_gizmoManager = red::CreateUniquePtr<GizmoManager>();
			AttachInputListener( m_gizmoManager.Get() );
			m_gizmoManager->RegisterShortcuts( GetShortcutManager(), this );

			// connect the gizmo collection in a way that whatever user will do via the gizmos will be routed through the action history
			// there's a special class providing all the glue code: GizmoTransformHandlerActions
			GizmoCollection::TTransformHandlerFactory factory =
				[this]( SelectionStatePtr selection, GizmoSpaceType spaceType )
			{
				return red::CreateSharedPtr< GizmoTransformHandler >( GetSharedData()->GetActionHistory(), &GetSharedData()->GetSnapManager(), selection, spaceType );
			};

			// create a typical (standard) TRS gizmo collection
			m_gizmoCollection = red::CreateUniquePtr< GizmoCollection >( m_gizmoManager.Get(), factory );
		}
	}

	GenericPreviewPanel::~GenericPreviewPanel()
	{
		m_gizmoCollection.Reset();

		DetachInputListener( m_gizmoManager.Get() );
		DetachInputListener( m_objectDragDropListener.Get() );
		DetachInputListener( m_selectionInput.Get() );

		m_objectDragDropListener.Reset();

		GetSharedData()->GetSelectionHandler().DetachSelectionCallback( m_selectionCallbackToken );
	}

	const GenericPreviewPanelSharedDataPtr GenericPreviewPanel::GetSharedData() const
	{
		return red::StaticCast<GenericPreviewPanelSharedData>(PreviewPanel::GetSharedData());
	}

	void GenericPreviewPanel::FocusOnNodes( const red::DynArray< EditorObjectID >& nodeIDs )
	{
		auto selection = GetSharedData()->BuildSelection(nodeIDs);

		Box selectionBounds;
		if ( GetSharedData()->ComputeObjectBounds( selection, selectionBounds ) )
		{
			FocusOnBounds( selectionBounds );
		}
	}

	void GenericPreviewPanel::FocusOnSelectedObjects()
	{
		auto selection = GetSharedData()->GetSelection();

		Box selectionBounds;
		if ( GetSharedData()->ComputeObjectBounds( selection, selectionBounds ) )
			FocusOnBounds( selectionBounds );
	}

	void GenericPreviewPanel::SelectObject( const EditorObjectIDPath& path )
	{
		const GenericPreviewPanelSharedDataPtr sharedData = GetSharedData();

		sharedData->SelectObjects( { path } );
	}

	void GenericPreviewPanel::SetupInterop( GenericPreviewPanel* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "gizmo", ptr->m_gizmoCollection.Get() );
	}

	void GenericPreviewPanel::SelectionChanged( const red::SharedPtr< SelectionState >& selection )
	{
		// allow the gizmo system to update
		m_gizmoCollection->SelectionChanged( selection );

		if ( OnSelectionChanged )
			OnSelectionChanged();
	}

	void GenericPreviewPanel::OnRender(RenderContext& ctx)
	{
		PreviewPanel::OnRender(ctx);
	}


} // tools