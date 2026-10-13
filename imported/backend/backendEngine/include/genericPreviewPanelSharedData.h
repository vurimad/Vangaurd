/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "selectionFwd.h"
#include "objectFwd.h"
#include "previewPanelSharedData.h"

#include "../../../common/redReflection/include/serializableValueNotifier.h"

namespace tools
{
	class EditorObjectIDPath;

	/// shared data for the generic preview panel
	class BACKEND_ENGINE_API GenericPreviewPanelSharedData : public PreviewPanelSharedData
	{
		DECLARE_INTEROP_DERIVED(GenericPreviewPanelSharedData, PreviewPanelSharedData)

	public:
		GenericPreviewPanelSharedData( IResourceEditor& editor, const TRenderPtr<IRenderScene>& renderScene = nullptr, IRenderScene::Type sceneType = IRenderScene::PREVIEW );

		virtual ~GenericPreviewPanelSharedData();

		/// get selection handler, used by selection system
		RED_FORCE_INLINE SelectionHandler& GetSelectionHandler() const { return *m_selectionHandler; }

		/// object handler, allows to access elements of the scene
		RED_FORCE_INLINE ObjectHandler& GetObjectHandler() const { return *m_objectHandler; }

		/// get current selection
		const SelectionStatePtr& GetSelection() const;

		// Select (focus) on given objects
		virtual void SelectObjects( const red::DynArray< EditorObjectID >& objectIds );
		virtual void SelectObjects( const red::DynArray< EditorObjectIDPath >& objectPaths );

		ObjectCollection* GetSelectedObjects() const;

		// Deselect given objects
		virtual void DeselectObjects( const red::DynArray< EditorObjectIDPath >& paths );

		// Returns set of ids of selected objects
		void GetSelectedIDPaths( red::DynArray< EditorObjectIDPath >& nodePaths ) const;

		/// compute bounds for given objects, returns true if at least one valid object was added to the bounds
		const Bool ComputeObjectBounds(const red::DynArray< EditorObjectID >& nodeIDs, Box& outBounds);

		/// compute bounds for given selection
		virtual const Bool ComputeObjectBounds(const SelectionStatePtr& selection, Box& outBounds);

		virtual Bool	CalculateSelectionInfo( const Vector3& refPoint, SelectionInfo& info ) const override;

		/// build a selection state from given objects
		SelectionStatePtr BuildSelection( const red::ArraySpan< const EditorObjectID >& objectIds );
		SelectionStatePtr BuildSelection( const red::ArraySpan< const EditorObjectIDPath >& objectPaths );

	protected:
		virtual void OnNotifyPropertyChanged( const SerializableID& id, const rtti::AccessPath& path );

		red::UniquePtr< SelectionHandler > m_selectionHandler;
		ObjectCollection* m_selectedObjects;
		red::UniquePtr< ObjectHandler > m_objectHandler;

		Uint32					m_selectionCallbackToken;

		SerializableValueNotifier m_serializableNotifier;

		/// selected set has changed
		virtual void SelectionChanged(const red::SharedPtr< SelectionState >& selection);

		/// internal update
		virtual void OnRenderSharedState(CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo) override;
		virtual void OnUpdateSharedState( const UpdateContext& ctx ) override;

		void RenderSelectionBounds( rend::IDebugDrawer& dd );
	};

} // tools