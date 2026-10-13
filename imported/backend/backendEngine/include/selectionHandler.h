/**
* Copyright (c) 2015-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "previewFwd.h"
#include "selectionFwd.h"
#include "undoFwd.h"
#include "objectFwd.h"
#include "selectionState.h"
#include "editAction.h"

#include "../../../common/renderData/include/renderSelectable.h"

namespace tools
{
	class IResourceEditor;
	struct ResolvingParams;


	// Selection edit action
	class BACKEND_ENGINE_API Action_Selection : public IEditAction
	{
		RTTI_DECLARE_TYPE( Action_Selection );

	public:
		Action_Selection( SelectionHandler* selectionHandler, const String desc, Uint32 actionBatchIndex, SelectionStatePtr oldState, SelectionStatePtr newState );
		virtual String GetDescription() const override;
		virtual Bool Do();
		virtual Bool Undo();
		virtual Bool TryMerge( const IEditAction* lastUndoAction );
		virtual Bool IsModification() const override;

	private:
		SelectionHandler*	m_handler;		// actual selection handler that is responsible for the selection state
		SelectionStatePtr	m_oldState;		// old selection state
		SelectionStatePtr	m_newState;		// new selection state
		String				m_desc;			// user description for the undo/redo actions
		Uint32				m_batchIndex;	// "batch index" of selection action, actions happening withing the same instant (frame) have the same batch index

	};

	// selection callback
	// called after every selection change
	// NOTE: selection may be also changed due to undo/redo operations
	typedef std::function< void( const SelectionStatePtr& selection ) > TSelectionChanged;
	typedef std::function< Bool( const SelectionStatePtr& selection ) > TCanChangeSelection;
	typedef std::function< void() > TSelectionUpdated;

	// Allows for filtering resolved editable proxies
	class BACKEND_ENGINE_API ISelectionFilter
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
	public:
		ISelectionFilter();
		virtual ~ISelectionFilter();
		virtual Bool Filter( const EditableProxyPtr& proxy ) const = 0;
	};

	// Transformer allows for reinterpretation of some node(s) selection into other node(s) selection.
	// Example1: We "click" terrain MESH nodes, but we want the underlying terrain PATCH node to be selected.
	// Example2: (not implemented yet) We click a mesh node, but if this mesh is part of a group, we want the whole group of nodes to become selected
	class BACKEND_ENGINE_API ISelectionTransformer
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

	public:

		// By default, all Selection Transformers are "Medium" and the Generic fallback transformer is "Very Low"
		// If you write a transformer to handle standard selection behaviour because the generic fallback doesn't work, use "Medium"
		// If your transformer is intended to change the standard selection behaviour temporarily, use "Low" if the standard behaviour
		// is normally handled by the generic fallback or "High" if it's performed by another transformer
		enum Priority
		{
			High,
			Medium,
			Low,
			VeryLow,
		};

		struct InputParameters
		{
			const ResolvingParams& resolvingParams;
			EditorObjectIDPath editorObjectIdPath;
			EditorObjectID editorObjectID;

			const ISelectionFilter* filter = nullptr;
			ObjectHandler* objectHandler = nullptr;
			IRenderSelectable::GlobalID selectableID;
			Vector3 worldPos;

			InputParameters( const ResolvingParams& params ) : resolvingParams( params ) {}
		};

		ISelectionTransformer();
		virtual ~ISelectionTransformer();

		// Interface: do this transformer use depth information?
		virtual Bool NeedsDepth() const { return false; }

		// Used when the transformer needs depth, determines the maximum depth to which we want to track selection.
		virtual Float MaxDepth() const { return 200.0f; }

		virtual Priority GetPriority() const { return Priority::Medium; }

		// Check if the transformer supports given object type
		virtual Bool SupportsObject( const EditorObjectID& objectID ) const { return true; }

		// Accumulate selections
		virtual Bool Transform( const InputParameters& parameters, red::DynArray< EditableProxyPtr >& outEditorIds, red::DynArray< EditorObjectIDPath >& outPathsToLeafObjects ) = 0;
	};
	
	using SelectionTransformerPtr = red::SharedPtr< ISelectionTransformer >;

	RED_INLINE static Bool operator<( const SelectionTransformerPtr& a, const SelectionTransformerPtr& b )
	{
		return a->GetPriority() < b->GetPriority();
	}

	// selection combining modes
	enum class SelectionMode : Uint8
	{
		Replace,
		Add,
		Subtract,
		Invert,
	};

	/// Describes selection context (selection area on screen, resolving, etc.)
	class SelectionContext
	{
	public:
		static const Uint32 s_currentSelectionResolver = 0xFFFFFFFF;

		// prepare for area selection
		explicit SelectionContext( PreviewPanel& panel, const Rect& rect, Uint32 depth = s_currentSelectionResolver, SelectionMode mode = SelectionMode::Replace, Bool multilayer = false )
			: m_previewPanel( panel )
			, m_selectionRect( rect )
			, m_depth( depth )
			, m_forceEmptySelection( false )
			, m_mode( mode )
			, m_multlayer( multilayer )
		{ }

		// prepare for point selection
		explicit SelectionContext( PreviewPanel& panel, Int32 x, Int32 y, Uint32 depth = s_currentSelectionResolver, SelectionMode mode = SelectionMode::Replace, Bool multilayer = false )
			: SelectionContext( panel, { x, x + 1, y, y + 1 }, depth, mode, multilayer )
		{ }

		PreviewPanel& m_previewPanel;
		Rect m_selectionRect;

		// At which level of the EditorObjectIDPath should we resolve the objects?
		// If set to 0, full path is taken into account, so bottom-most objects will be selected.
		// If set to 1, prefabs above bottom-most object will be selected, etc.
		// If set to CURRENT_RESOLVER_SELECTION, we can select things that are managed by current object resolver only.
		Uint32 m_depth;

		// if set to true, will resolve to empty selection
		Bool m_forceEmptySelection;

		// current selection combining mode
		SelectionMode m_mode;

		Bool m_multlayer;
	};

	class BACKEND_ENGINE_API ISelectionHandler : red::NonCopyable
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~ISelectionHandler() {}

		/// get current selection 
		virtual const SelectionStatePtr& GetSelection() const = 0;

		// handle direct selection of objects, selects the objects from the set
		virtual Bool HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc ) = 0;

		// change the actual selection, MUST BE called from the action ('actionHistory' is a dummy token)
		// returns true if the selection was changed properly
		// the selection proxy may not be able to restore the link to actual object, if this happens the selection state is no longer valid and the undo/redo actions fail
		virtual Bool Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection = false ) = 0;

		// Shortcut method for cleating copy of selection state
		RED_FORCE_INLINE SelectionStatePtr CloneSelection() const { return GetSelection()->Clone(); }
	};

	/// selection handler, controls the process of selection objects in the rendering view
	/// this uses the 3D rendering to generate the selection image and performs pixel-picking to determine what was selected
	class BACKEND_ENGINE_API SelectionHandler : public ISelectionHandler
	{
	public:
		SelectionHandler( IEditActionHistory* actionHistory );
		virtual ~SelectionHandler();

		// ISelectionHandler interface implementation
		virtual const SelectionStatePtr& GetSelection() const override { return m_selection; }
		virtual Bool HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc ) override;
		virtual Bool Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection = false ) override;

		// deselect all objects, calls the selection callback if selection changed due to that action
		void ClearSelection();

		// AddRemove selection transformers
		void AttachTransformer( const SelectionTransformerPtr& transformer );
		void RemoveTransformer( const SelectionTransformerPtr& transformer );

		Bool IsMultilayerSelectionEnabled() const;
		void EnableMultilayerSelection( Bool enable );

		void SetFilter( const ISelectionFilter* filter );

		// attach selection callback
		Uint32 AttachSelectionCallback( TSelectionChanged callback );
		void AttachUserSelectionCallback( TSelectionChanged callback );
		void AttachUserSelectionUpdatedCallback( TSelectionUpdated callback );
		void AttachUserCanChangeSelectionCallback( TCanChangeSelection callback );
		void DetachUserCanChangeSelectionCallback();

		// detach selection callback
		void DetachSelectionCallback(const Uint32 token);

		// perform selection of objects in given region, calls the selection callback if selection changed due to that action
		Bool PerformSelection( const SelectionContext& context );

		// Compute selection at given area of a preview window
		// This method construct selection state object without executing selection action
		// Optionally this method can return full editor object ID paths to selected objects
		SelectionStatePtr ComputeSelection( const SelectionContext& context, red::DynArray< EditorObjectIDPath >* outPathsToLeafObjects = nullptr );

		// HACK
		void SetObjectHandler( ObjectHandler* objectHandler, IResourceEditor& editor );

		void HideSelection();

		// begin new selection batch
		void NextSelectionBatch();

		// create custom selection change action
		// this method is a HACK
		red::UniquePtr< Action_Selection > CreateSelectionAction( const SelectionStatePtr& initialSelection, const SelectionStatePtr& finalSelection );

		void Enable( Bool enabled );

	private:
		IEditActionHistory*		m_actions;		// action history (selection events are stored there)
		SelectionStatePtr		m_selection;	// actual selection list (unfiltered)
		ObjectHandler*			m_objectHandler;
		IResourceEditor*		m_resourceEditor;

		const ISelectionFilter* m_filter;

		red::SortedArray< SelectionTransformerPtr > m_transformers;
		
		red::SharedPtr< rend::DepthResult > m_depthMap;	// Depth map for selection rendering result
		SelectionTransformerPtr m_genericTransformer;
		SelectionTransformerPtr m_disableSelectionTransformer;

		struct Callback
		{
			Uint32 m_token;
			TSelectionChanged m_callback;
		};

		typedef red::DynArray<Callback> TCallbacks;
		TCallbacks m_callbacks;								// called each time selection changes
		TSelectionChanged m_userSelectionCallback;			// called each time user changes selection
		TSelectionUpdated m_selectionUpdatedCallback;		// called each time user picks already existing selection
		TCanChangeSelection m_canChangeSelectionCallback;	// called before actually changing selection (execution action)
		Uint32 m_callbackNextToken;
		Uint32 m_batchIndex;	// internal batch index
		Bool m_multilayerSelection;

		// resolve editable proxies from selection result
		void ResolveSelection( const ResolvingParams& params, red::DynArray< EditableProxyPtr >& outProxies, red::DynArray< EditorObjectIDPath >& outPathsToLeafObjects );

		// Combine old selection with new proxies using appropriate mode (add, subtract, replace, etc.)
		SelectionStatePtr CombineSelection( const red::DynArray< EditableProxyPtr >& newSelectionProxies, SelectionMode mode ) const;

		void OnUserSelection( const SelectionStatePtr& selection );

		friend class Action_Selection;
		friend class Action_DeleteObjects;
	};
}