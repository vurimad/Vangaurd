/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectActionDeleteObjects.h"

#include "objectPersistentCollection.h"
#include "selectionHandler.h"
#include "selectionIterator.h"

#include "../../../common/core/include/message.h"
#include "editActionHistory.h"
#include "resourceEditor.h"

namespace tools
{
	Action_DeleteObjects::Action_DeleteObjects( ObjectHandler& objectHandler, const red::ArraySpan< const tools::EditorObjectIDPath >& paths )
		: m_objectHandler( objectHandler )
		, m_paths( paths )
	{		
		// Do not call resolver->CaptureObjects() here. This action might be a part of EditGroup action where first we need to ungroup objects and then delete them after ungrouping.
		// Capturing objects here means that we store them in memory while still being grouped so undo will cause asserts while undoing ungroup action because it will try to assign object to group
		// to which it already exists because of this premature capture.

		// Keep in mind that if action might be a part of EditGroup we shouldn't store state of objects in action ctor's. This is called before executing actions.
		// Making snapshots of objects in ctor's doesn't represent the actual state of objects after executing action from group of actions.
	}

	Action_DeleteObjects::~Action_DeleteObjects() = default;

	String Action_DeleteObjects::GetDescription() const
	{
		return "Deleting objects";
	}

	Bool Action_DeleteObjects::Do()
	{
		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_paths );
		RED_ASSERT( resolver, "Failed to find unified resolver for selection" );
		if ( !resolver )
			return false;

		// capture data of the objects that will be deleted
		m_capturedObjects = resolver->CaptureObjects( m_paths );
		// restore the deleted object back

		// capture selection state before objects are deleted
		m_capturedOldSelection = m_objectHandler.GetSelectionHandler()->CloneSelection();

		// make sure state is valid
		if ( !m_capturedObjects )
		{
			RED_LOG_ERROR( "Delete objects: No objects have been captured" );
			return false;
		}

		if ( !m_capturedOldSelection )
		{
			RED_LOG_ERROR( "Delete objects: Previous selection was not captured" );
			return false;
		}

		String failReason;
		if ( !resolver->CanDeleteObjects( m_paths, false, &failReason ) )
		{
			RED_LOG_ERROR( "Delete objects: Failed to change selection: %s", failReason.AsChar() );
			return false;
		}

		// pre change selection - deselect the objects that we are going to delete
		{
			const SelectionStatePtr newSelection = SelectionState::EmptySelection();
			for ( auto it = m_capturedOldSelection->Iterate(); it; ++it )
			{
				if( it->GetEditorObjectID().GetKind() == RED_NAME_CONSTEXPR_NOREG( "ComponentNode" ) )
				{
					newSelection->Add( it.GetCurrentPtr() );
				}
				else if ( !m_paths.Exist( it->GetEditorObjectIDPath() ) )
				{
					newSelection->Add( it.GetCurrentPtr() );
				}
			}

			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( newSelection, false ) )
			{
				RED_LOG_ERROR( "Delete objects: Failed to change selection" );
				return false;
			}
		}

		// now it's safe to delete objects because we captured the state
		IObjectResolver::DeleteContext deleteContext;
		deleteContext.pathsToDelete = m_paths;
		if ( !resolver->Action_DeleteObjects( deleteContext ) )
		{
			RED_LOG_ERROR( "Delete objects: Action_DeleteObjects failed" );
			return false;
		}

		// post change selection - select objects that Action_DeleteObjects requested
		{
			const SelectionStatePtr newSelection = SelectionState::EmptySelection();
			for ( const EditorObjectIDPath& path : deleteContext.outObjectsToSelect )
			{
				const EditableProxyPtr proxy = resolver->ResolveProxy( path );
				RED_FATAL_ASSERT( proxy, "Failed to resolve proxy" );
				newSelection->Add( proxy );
			}
			// ctremblay: I remove the force refresh. It was breaking editing Area Shape. Let me know if there are issue.
			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( newSelection, false ) )
			{
				RED_LOG_ERROR( "Delete objects: Failed to change selection" );
				return false;
			}
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% successful - Removed % object(s)", RED_NAME_CONSTEXPR( "Delete" ), m_paths.Size() );

		return true;
	}

	Bool Action_DeleteObjects::Undo()
	{
		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_capturedObjects );
		if ( !resolver )
			return false;

		// restore the deleted object back
		red::DynArray< EditorObjectIDPath > restoredObjectsPaths;
		if ( !resolver->Action_RestoreObjects( m_capturedObjects, restoredObjectsPaths ) )
			return false;

		m_paths = restoredObjectsPaths;
		m_capturedOldSelection->Clear();

		for ( const EditorObjectIDPath& path : m_paths )
		{
			auto object = m_objectHandler.ResolveProxy( path );
			if ( !object )
			{
				RED_LOG_ERROR( "Undo delete objects: Failed to find restored object" );
				return false;
			}
			m_capturedOldSelection->Add( object );
		}

		// restore selection
		if ( m_capturedOldSelection->IsEmpty() || !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( m_capturedOldSelection, true ) )
		{
			RED_LOG_ERROR( "Undo delete objects: Failed to change selection" );
			return false;
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_paths[ 0 ].GetFirstChildID().GetResourcePath() ), "% successful - % undone for % object(s)", RED_NAME_CONSTEXPR( "Restore" ), RED_NAME_CONSTEXPR( "Delete" ), m_paths.Size() );
		return true;
	}

	Bool Action_DeleteObjects::IsModification() const
	{
		return true;
	}

	void Action_DeleteObjects::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const EditorObjectIDPath& path : m_paths )
		{
			if ( !path.IsEmpty() )
			{
				const EditorObjectID& id = path.GetFinalChildID();
				outResourcePaths.Insert( id.GetResourcePath() );
			}
		}
	}
} // tools
