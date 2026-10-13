/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "selectionFwd.h"
#include "objectFwd.h"
#include "gizmoFwd.h"
#include "previewFwd.h"
#include "previewPanel.h"
#include "genericPreviewPanelSharedData.h"
#include "selectionState.h"

#include "../../../common/redContainers/include/set.h"

namespace tools
{	
	class SnapManager;
	class SimpleGridSnapper;

	typedef std::function< void() > TOnSelectionChanged;

	/// generic panel with selection, gizmos, drag&drop, grid and other typical stuff
	class BACKEND_ENGINE_API GenericPreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( GenericPreviewPanel, PreviewPanel );

	public:
		GenericPreviewPanel( RenderingWindowPtr renderingWindow, const GenericPreviewPanelSharedDataPtr& sharedData, Bool attachDragDropListener = true );
		virtual ~GenericPreviewPanel();

		/// get the shared data (the scene)
		const GenericPreviewPanelSharedDataPtr GetSharedData() const;

		/// get current gizmo manager, allows to add more gizmos
		RED_FORCE_INLINE GizmoManager& GetGizmoManager() const { return *m_gizmoManager; }
        RED_FORCE_INLINE GizmoCollection& GetGizmoCollection() const { return *m_gizmoCollection; }

		// Focus on given node (without selecting it)
		void FocusOnNodes( const red::DynArray< EditorObjectID >& nodeIDs );

		// Focus on selection
		void FocusOnSelectedObjects();

		TOnSelectionChanged OnSelectionChanged;

		virtual void SelectObject( const EditorObjectIDPath& path ) override final;

	protected:
		virtual void OnRender( RenderContext& ctx) override;

		// selection input
		red::UniquePtr< SelectionInputListener > m_selectionInput;
		Uint32 m_selectionCallbackToken;

		// traditional gizmo system is per-viewport
		red::UniquePtr < GizmoManager > m_gizmoManager;
		red::UniquePtr < GizmoCollection > m_gizmoCollection; // TRS gizmo setup

		/// drag&drop handling
		red::UniquePtr < ObjectDragDropListener > m_objectDragDropListener;

		/// selected set has changed
		void SelectionChanged( const red::SharedPtr< SelectionState >& selection );
	};

} // tools