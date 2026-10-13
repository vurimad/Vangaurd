/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "selectionHandler.h"
#include "editAction.h"
#include "editActionHistory.h"
#include "previewPanel.h"
#include "selectionState.h"
#include "selectionIterator.h"
#include "objectHandler.h"
#include "resourceEditor.h"
#include "resourceBackend.h"
#include "actionHideSelection.h"
#include "objectResolver.h"

#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/renderData/include/renderSelectable.h"
#include "../../../common/world/include/worldNode.h"
#include "../../../common/world/include/worldNodeGroup.h"
#include "../../../common/worldEntities/include/entityComponent.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Action_Selection, tools )
	RTTI_PARENT_TYPE( tools::IEditAction );
RTTI_END_TYPE()

using red::DynArray;

namespace Config
{
	TConfigVar< Bool > cvEnablePreviewSelection( "Editor/Selection", "EnablePreviewSelection", true );
	TConfigVar< Int32 > cvSelectionExtendedRange( "Editor/Selection", "ExtendedRange", 12, 0, 30, eConsoleVarFlag_Save );
}

namespace tools
{
	struct ResolvingParams
	{
		const SelectionContext& m_context;
		red::SharedPtr<rend::SelectionResult> m_selectionResult;
		Rect m_rect;
		Bool m_extendedRangeOnly;
	};

	class DisableSelectionTransformer : public ISelectionTransformer
	{
		virtual Priority GetPriority() const override final
		{
			return Priority::High;
		}

		virtual Bool Transform( const InputParameters&, red::DynArray<EditableProxyPtr>&, red::DynArray<EditorObjectIDPath>& ) override final
		{
			return true;
		}
	};

	class GenericSelectionTransformer : public ISelectionTransformer
	{
	public:
		virtual Priority GetPriority() const override final
		{
			return Priority::VeryLow;
		}

		virtual Bool Transform( const InputParameters& parameters, red::DynArray<EditableProxyPtr>& outEditorIds, red::DynArray<EditorObjectIDPath>& outPathsToLeafObjects ) override final
		{
			IObjectResolver::ResolvingParams resolvingParams;
			resolvingParams.m_ignoreLocked = false;
			resolvingParams.m_isForSelection = true;

			EditableProxyPtr resolvedProxy = parameters.objectHandler->ResolveProxy( parameters.editorObjectIdPath, resolvingParams );
			
			if ( !resolvedProxy )
			{
				return false;
			}

			if ( parameters.filter && !parameters.filter->Filter( resolvedProxy ) )
			{
				return false;
			}

			EditorObjectIDPath shortenedEditorPath = resolvedProxy->GetEditorObjectIDPath();
			outPathsToLeafObjects.PushBack( shortenedEditorPath );

			//////////////////////////////////////////////////////////////////////////

			Uint32 computedDepth = parameters.resolvingParams.m_context.m_depth;
			if ( parameters.resolvingParams.m_context.m_depth == SelectionContext::s_currentSelectionResolver )
			{
				shortenedEditorPath = shortenedEditorPath.StrippedFromKind( world::NodesGroup::EditorObjectKind );

				// adjust path according to active prefab depth
				const EditorObjectIDPath activeObjectPath = parameters.objectHandler->GetActiveObjectIDPath();
				if ( shortenedEditorPath.GetDepth() <= activeObjectPath.GetDepth() )
				{
					shortenedEditorPath = EditorObjectIDPath();
				}
				else
				{
					for ( Uint32 i = 0; i < activeObjectPath.GetDepth(); ++i )
					{
						if ( shortenedEditorPath.GetElements()[ i ] != activeObjectPath.GetElements()[ i ] )
						{
							shortenedEditorPath = EditorObjectIDPath();
							break;
						}
					}

					if ( !shortenedEditorPath.IsEmpty() )
					{
						computedDepth = shortenedEditorPath.GetDepth() - activeObjectPath.GetDepth() - 1;
					}
				}
			}

			if ( !shortenedEditorPath.IsEmpty() )
			{
				shortenedEditorPath.RemoveLastElements( computedDepth );
			}

			if ( !shortenedEditorPath.IsEmpty() )
			{
				const EditableProxyPtr editableProxy = parameters.objectHandler->ResolveProxy( shortenedEditorPath, resolvingParams );
				if ( editableProxy )
				{
					// HACK
					// This is required for editing area shapes inside component to work
					// We need to select parent component so the helper object for the area shape does not disappear
					auto parentProxy = editableProxy->GetParentEditableProxy();
					if( parentProxy && parentProxy->GetSerializable()->IsA< ent::IComponent >() )
					{
						outEditorIds.PushBack( parentProxy );
					}

					outEditorIds.PushBack( editableProxy );
				}
			}

			return true;
		}
	};

	ISelectionTransformer::ISelectionTransformer() = default;
	ISelectionTransformer::~ISelectionTransformer() = default;

	ISelectionFilter::ISelectionFilter() = default;
	ISelectionFilter::~ISelectionFilter() = default;

	//////////////////////////////////////////////////////////////////////////

	Action_Selection::Action_Selection( SelectionHandler* selectionHandler, const String desc, Uint32 actionBatchIndex, SelectionStatePtr oldState, SelectionStatePtr newState )
		: m_handler( selectionHandler )
		, m_desc( desc )
		, m_batchIndex( actionBatchIndex )
		, m_oldState( oldState )
		, m_newState( newState )
	{
		RED_ASSERT( oldState != newState, "Redundant selection change" );

		if ( !m_oldState )
		{
			m_oldState = SelectionState::EmptySelection();
		}

		if ( !m_newState )
		{
			m_newState = SelectionState::EmptySelection();
		}
	}

	String Action_Selection::GetDescription() const
	{
		return m_desc;
	}

	Bool Action_Selection::Do()
{
		// this will return true if all of the selection objects were restored
		if ( m_handler->Action_ChangeSelection( m_newState ) )
		{
			m_handler->OnUserSelection( m_newState );
			return true;
		}
		return false;
	}

	Bool Action_Selection::Undo()
{
		// this will return true if all of the selection objects were restored
		if ( m_handler->Action_ChangeSelection( m_oldState ) )
		{
			m_handler->OnUserSelection( m_oldState );
			return true;
		}
		return false;
	}

	/// Try to merge action with already executed action
	Bool Action_Selection::TryMerge( const IEditAction* lastUndoAction )
	{
		// merge selection actions if the batch index is the same
		if ( lastUndoAction->GetClass() == GetStaticClass() )
		{
			auto prev = static_cast< const Action_Selection* >( lastUndoAction );
			if ( m_batchIndex == prev->m_batchIndex )
			{
				m_oldState = prev->m_oldState;
				return true;
			}
		}

		return false;
	}

	Bool Action_Selection::IsModification() const
	{
		return false;
	}

	//////////////////////////////////////////////////////////////////////////

	SelectionHandler::SelectionHandler( IEditActionHistory* actionHistory )
		: m_selection( SelectionState::EmptySelection() )
		, m_objectHandler( nullptr )
		, m_resourceEditor( nullptr )
		, m_actions( actionHistory )
		, m_batchIndex( 0 )
		, m_callbackNextToken( 1 )
		, m_filter( nullptr )
		, m_multilayerSelection( false )
	{
		m_disableSelectionTransformer = red::CreateSharedPtr< DisableSelectionTransformer >();
		m_genericTransformer = red::CreateSharedPtr< GenericSelectionTransformer >();
		AttachTransformer( m_genericTransformer );
	}

	SelectionHandler::~SelectionHandler() = default;

	void SelectionHandler::SetObjectHandler( ObjectHandler* objectHandler, IResourceEditor& editor )
	{
		m_objectHandler = objectHandler;
		m_resourceEditor = &editor;
	}

	void SelectionHandler::AttachTransformer( const SelectionTransformerPtr& transformer )
	{
		RED_FATAL_ASSERT( !m_transformers.Exist( transformer ), "Let's always know what we are doing - this shouldn't happen twice for any transformer." );

		m_transformers.Insert( transformer );
	}

	void SelectionHandler::RemoveTransformer( const SelectionTransformerPtr& transformer )
	{
		RED_FATAL_ASSERT( m_transformers.Exist( transformer ), "Let's always know what we are doing - this shouldn't happen twice for any transformer." );

		m_transformers.Remove( transformer );
	}

	Bool SelectionHandler::IsMultilayerSelectionEnabled() const
	{
		return m_multilayerSelection;
	}

	void SelectionHandler::EnableMultilayerSelection( Bool enable )
	{
		m_multilayerSelection = enable;
	}

	void SelectionHandler::SetFilter( const ISelectionFilter* filter )
	{
		m_filter = filter;
	}

	Uint32 SelectionHandler::AttachSelectionCallback( TSelectionChanged callbackFunc )
	{
		if ( !callbackFunc )
			return 0;

		Callback callback;
		callback.m_callback = callbackFunc;
		callback.m_token = m_callbackNextToken++;
		m_callbacks.PushBack( callback );

		return callback.m_token;
	}

	void SelectionHandler::AttachUserSelectionCallback( TSelectionChanged callbackFunc )
	{
		if ( !callbackFunc )
			return;

		m_userSelectionCallback = callbackFunc;
	}

	void SelectionHandler::AttachUserSelectionUpdatedCallback( TSelectionUpdated callback )
	{
		m_selectionUpdatedCallback = callback;
	}

	void SelectionHandler::AttachUserCanChangeSelectionCallback( TCanChangeSelection callback )
	{
		m_canChangeSelectionCallback = callback;
	}
	
	void SelectionHandler::DetachUserCanChangeSelectionCallback()
	{
		m_canChangeSelectionCallback = nullptr;
	}

	void SelectionHandler::DetachSelectionCallback(const Uint32 token)
	{
		for ( Uint32 i = 0; i < m_callbacks.Size(); ++i )
		{
			if ( m_callbacks[i].m_token == token )
			{
				m_callbacks.RemoveAt( i );
				break;
			}
		}
	}

	void SelectionHandler::HideSelection()
	{
		m_actions->Execute( red::CreateUniquePtr< Action_HideSelection >( m_selection ) );
	}

	void SelectionHandler::ClearSelection()
	{
		if ( !m_selection->IsEmpty() )
		{
			m_actions->Execute( red::CreateUniquePtr< Action_Selection >( this, "Clear selection", m_batchIndex, m_selection, SelectionState::EmptySelection() ) );
		}
	}

	Bool SelectionHandler::PerformSelection( const SelectionContext& context )
	{
		SelectionStatePtr newState = ComputeSelection( context );
		if ( !newState )
			return false;

		if ( m_canChangeSelectionCallback && !m_canChangeSelectionCallback( newState ) )
			return false;

		// nothing changed
		if ( m_selection && ( *m_selection == *newState ) )
		{
			if ( m_selectionUpdatedCallback )
			{
				m_selectionUpdatedCallback();
			}

			return true;
		}

		// issue selection action
		return m_actions->Execute( red::CreateUniquePtr< Action_Selection >( this, "Selection changed", m_batchIndex, m_selection->Clone(), newState ) );
	}

	SelectionStatePtr SelectionHandler::ComputeSelection( const SelectionContext& context, red::DynArray< EditorObjectIDPath >* outPathsToLeafObjects )
	{
		// user actions always triggers new selection batch
		NextSelectionBatch();

		if ( !Config::cvEnablePreviewSelection.Get() )
		{
			return SelectionState::EmptySelection();
		}

		red::DynArray< EditorObjectIDPath > pathsToLeafObjects;
		DynArray< EditableProxyPtr > newSelectionProxies;

		if ( !context.m_forceEmptySelection )
		{
			// Determine selection map rect
			Rect selectionMapRect = context.m_selectionRect;
			const Uint32 extendedRange = Config::cvSelectionExtendedRange.Get();
			Bool useExtendedRange = false;
			if ( extendedRange > 0 )
			{
				if ( ( context.m_selectionRect.m_left + 1 == context.m_selectionRect.m_right ) && ( context.m_selectionRect.m_bottom == context.m_selectionRect.m_top + 1 ) )
				{
					selectionMapRect.Grow( extendedRange, extendedRange );
					useExtendedRange = true;
				}
			}

			// Render the scene in the "selection mode"
			auto selectionResult = red::CreateSharedPtr<rend::SelectionResult>( red::PoolBackend() );

			const Bool buffered = false;
			context.m_previewPanel.RenderSelectionMap( selectionMapRect, selectionResult, buffered, context.m_multlayer );

			// Try to resolve selection in "extended" range first if the range is non-zero and we are selecting a single pixel.
			if ( useExtendedRange )
			{
				const ResolvingParams params =
				{
					context, selectionResult, selectionMapRect, true
				};
				ResolveSelection( params, newSelectionProxies, pathsToLeafObjects );
			}

			// If have found something in extended range, skip further resolving
			if ( newSelectionProxies.Empty() )
			{
				const ResolvingParams params =
				{
					context, selectionResult, context.m_selectionRect, false,
				};
				ResolveSelection( params, newSelectionProxies, pathsToLeafObjects );
			}
		}

		if ( outPathsToLeafObjects )
		{
			*outPathsToLeafObjects = pathsToLeafObjects;
		}

		return CombineSelection( newSelectionProxies, context.m_mode );
	}

	SelectionStatePtr SelectionHandler::CombineSelection( const red::DynArray< EditableProxyPtr >& newSelectionProxies, SelectionMode mode ) const
	{
		// setup new selection state
		SelectionStatePtr selectionState;

		if ( mode == SelectionMode::Replace )
		{
			// replace selection
			selectionState = red::CreateSharedPtr< SelectionState >();

			for ( const EditableProxyPtr& proxy : newSelectionProxies )
			{
				selectionState->Add( proxy );
			}
		}
		else
		{
			// modify current selection
			selectionState = m_selection->Clone();

			if ( mode == SelectionMode::Add )
			{
				for ( auto proxy : newSelectionProxies )
				{
					selectionState->Add( proxy );
				}
			}
			else if ( mode == SelectionMode::Subtract )
			{
				for ( auto proxy : newSelectionProxies )
				{
					selectionState->Remove( proxy->GetEditorObjectIDPath() );
				}
			}
			else if ( mode == SelectionMode::Invert )
			{
				for ( auto proxy : newSelectionProxies )
				{
					if ( selectionState->Contains( proxy->GetEditorObjectIDPath() ) )
					{
						// ctremblay HACK: needed to make Multiselection work of Area Shape.
						if( auto& serializable = proxy->GetSerializable() )
						{
							if( !serializable->IsA< ent::IComponent >() )
							{
								selectionState->Remove( proxy->GetEditorObjectIDPath() );
							}
						}
					}
					else
					{
						selectionState->Add( proxy );
					}
				}
			}
			else
			{
				RED_FATAL( "Invalid selection mode" );
			}
		}

		return selectionState;
	}

	Bool SelectionHandler::HandleDirectSelection( const SelectionStatePtr& newSelection, const String& desc )
	{
		if ( !newSelection )
			return false;

		SelectionStatePtr oldState(m_selection->Clone());
		return m_actions->Execute( red::CreateUniquePtr< Action_Selection >(this, !desc.Empty() ? desc : "Selection changed", m_batchIndex, oldState, newSelection));
	}

	Bool SelectionHandler::Action_ChangeSelection( const SelectionStatePtr& state, Bool refreshSelection )
	{
		if ( !m_selection )
			m_selection = SelectionState::EmptySelection();

		// make sure that all objects in the incoming selection set are valid
		if (!state || !state->Restore())
		{
			m_selection = SelectionState::EmptySelection();

			for (const auto entry : m_callbacks)
				entry.m_callback(m_selection);

			return false;
		}

		// remove selection visualization from all existing proxies that will NOT be in the new selection state
		for ( auto it = m_selection->Iterate(); it; ++it )
		{
			if ( !state->Contains( it->GetEditorObjectIDPath() ) || refreshSelection )
			{
				// proxy is going to be deselected
				it->ToggleSelectionVisualization( false, state );
			}
		}

		// create selection visualization for all incoming selection proxies (ones that were NOT in the previous selection state)
		// Also store last newly added proxy for later use.
		EditableProxyPtr lastNewlyAddedProxy;
		for ( auto it = state->Iterate(); it; ++it )
		{
			if ( !m_selection->Contains( it->GetEditorObjectIDPath() ) || refreshSelection )
			{
				// proxy is going to be selected
				it->ToggleSelectionVisualization( true, state );
				lastNewlyAddedProxy = it.GetCurrentPtr();
			}
		}

		// Notify the resource backend, store the current selection as the last selection within the resource backend.
		// Do this only if we have selected a Node, any other objects are rejected here.
		if ( lastNewlyAddedProxy != nullptr )
		{
			if ( m_resourceEditor != nullptr )
			{
				auto object = lastNewlyAddedProxy->GetSerializable();

				if( object && object->IsA<world::Node>() )
				{
					ResourceBackend* resourceBackend = m_resourceEditor->GetBackend();
					resourceBackend->SetLastNodeSelection( m_resourceEditor, lastNewlyAddedProxy->GetEditorObjectIDPath() );
				}

			}
		}

		// set new selection set
		m_selection = state;

		// notify our client
		for ( const auto entry : m_callbacks )
		{
			entry.m_callback( m_selection );
		}
		
		// processed
		return true;
	}

	void SelectionHandler::ResolveSelection( const ResolvingParams& params, red::DynArray< EditableProxyPtr >& outProxies, red::DynArray< EditorObjectIDPath >& outPathsToLeafObjects )
	{
		RED_FATAL_ASSERT( outProxies.Empty(), "This should be empty." );

		Rect rect = params.m_rect;

		// fix the rectangle
		if ( rect.m_right < rect.m_left )
			std::swap( rect.m_right, rect.m_left );
		if ( rect.m_bottom < rect.m_top )
			std::swap( rect.m_bottom, rect.m_top );

		red::DynArray< rend::SelectedObjectInfo > uniqueSelectedObjects = params.m_selectionResult->GetUniqueSelectedObjects( &params.m_rect );

		if ( params.m_extendedRangeOnly )
		{
			Point rectCenter = rect.GetCenter();
			std::sort( uniqueSelectedObjects.Begin(), uniqueSelectedObjects.End(), [ &rectCenter ] ( const rend::SelectedObjectInfo& objectA, const rend::SelectedObjectInfo& objectB ) 
			{
				const Point pointA = rectCenter - objectA.GetPos();
				const Point pointB = rectCenter - objectB.GetPos();
				return pointA.x * pointA.x + pointA.y * pointA.y < pointB.x * pointB.x + pointB.y * pointB.y;
			} );
		}

		outProxies.Reserve( uniqueSelectedObjects.Size() );
		outPathsToLeafObjects.Reserve( uniqueSelectedObjects.Size() );

		Bool didRenderDepth = false;

		ISelectionTransformer::InputParameters transformerParameters( params );
		transformerParameters.objectHandler = m_objectHandler;
		transformerParameters.filter = m_filter;

		// Process transformable selections
		for ( const rend::SelectedObjectInfo& obj : uniqueSelectedObjects )
		{
			transformerParameters.editorObjectIdPath = IRenderSelectable::ResolveSelectionID( obj.GetId(), params.m_extendedRangeOnly );
			if ( transformerParameters.editorObjectIdPath.IsEmpty() )
				continue;

			for ( const SelectionTransformerPtr& transformer : m_transformers )
			{
				if ( !transformer->SupportsObject( transformerParameters.editorObjectIdPath.GetFinalChildID() ) )
				{
					continue;
				}

				if ( transformer->NeedsDepth() )
				{
					if ( !didRenderDepth )
					{
						m_depthMap = red::CreateSharedPtr< rend::DepthResult >( red::PoolBackend() );
						params.m_context.m_previewPanel.RenderDepthMap( params.m_context.m_selectionRect, m_depthMap );
						didRenderDepth = true;
					}

					Float maxDepth = transformer->MaxDepth();

					if ( !m_depthMap->CalcWorldSpacePoint( obj.GetPos().x, obj.GetPos().y, transformerParameters.worldPos, maxDepth ) )
						continue;
				}

				transformerParameters.editorObjectID = transformerParameters.editorObjectIdPath.GetFinalChildID();
				transformerParameters.selectableID = obj.GetId();

				if ( transformer->Transform( transformerParameters, outProxies, outPathsToLeafObjects ) )
				{
					// Object transformed, don't pass it into any more transformers
					break;
				}
			}

			if ( params.m_extendedRangeOnly )
			{
				// In extended range mode select only a single object.
				// Otherwise we could select multiple object with one click.

				break;
			}
		}
	}

	void SelectionHandler::NextSelectionBatch()
	{
		++m_batchIndex;
	}

	red::UniquePtr< Action_Selection > SelectionHandler::CreateSelectionAction( const SelectionStatePtr& initialSelection, const SelectionStatePtr& finalSelection )
	{
		return red::CreateUniquePtr< Action_Selection >( this, "Selection changed", m_batchIndex, initialSelection, finalSelection );
	}

	void SelectionHandler::Enable( Bool enabled )
	{
		if( enabled )
		{
			RemoveTransformer( m_disableSelectionTransformer);
		}
		else
		{
			AttachTransformer( m_disableSelectionTransformer );
		}
	}

	void SelectionHandler::OnUserSelection( const SelectionStatePtr& selection )
	{
		if ( m_userSelectionCallback )
		{
			m_userSelectionCallback( selection );
		}
	}
} // tools
