/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectHandler.h"

#include "objectActionReplaceObjects.h"
#include "objectPersistentCollection.h"
#include "selectionHandler.h"

#include "editActionHistory.h"
#include "resourceEditor.h"

#include "../../../common/core/include/message.h"

namespace tools
{
	Action_ReplaceObjects::Action_ReplaceObjects( ObjectHandler& objectHandler, const red::DynArray<EditorObjectIDPath>& objectsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objects, const Bool selectCreatedObjects )
		: m_objectHandler(objectHandler)
		, m_incomingObjects(objects)
		, m_changeSelection(selectCreatedObjects)
		, m_objectsPathsToReplace(objectsToReplace)
	{
		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_objectsPathsToReplace );
		if ( !resolver )
			return;

		// capture data of the objects that will be deleted
		m_capturedObjects = resolver->CaptureObjects( m_objectsPathsToReplace );

		// capture the IDs of the selected objects
		if (m_changeSelection)
			m_capturedOldSelection = m_objectHandler.GetSelectionHandler()->CloneSelection();
	}

	String Action_ReplaceObjects::GetDescription() const
	{
		return "Replace objects";
	}

	Bool Action_ReplaceObjects::Do()
	{
		IObjectResolver* resolver = nullptr;

		// recreate previous objects
		if (m_createdObjects)
		{
			// find resolver
			resolver = m_objectHandler.GetUnifiedResolver( m_createdObjects );
			if ( !resolver )
				return false;

			// we already have captured collection
			m_createdObjectsPaths.Clear();
			if ( !resolver->Action_RestoreObjects( m_createdObjects, m_createdObjectsPaths ) )
				return false;

			// delete the created objects
			IObjectResolver::DeleteContext deleteContext;
			deleteContext.pathsToDelete = m_objectsPathsToReplace;
			if ( !resolver->Action_DeleteObjects( deleteContext ) )
				return false;
		}
		else
		{
			// find resolver
			resolver = m_objectHandler.GetUnifiedResolver( m_objectsPathsToReplace );
			if ( !resolver )
				return false;

			// create objects, this creates the visualization to
			m_createdObjectsPaths.Clear();
			m_createdObjects = resolver->Action_ReplaceObjects( m_objectsPathsToReplace, m_incomingObjects, m_createdObjectsPaths );
		}

		// select the objects
		if (m_changeSelection)
		{
			// set new selection
			SelectionStatePtr newSelection = SelectionState::EmptySelection();

			// find the selection proxies for the objects
			for ( const EditorObjectIDPath& path : m_createdObjectsPaths )
			{
				// find the object representation
				const auto editableProxy = resolver->ResolveProxy( path );
				if (editableProxy)
				{
					newSelection->Add(editableProxy);
				}
			}

			// set new selection state, NOTE: if this fails we are not yet that screwed so there's no return false here
			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( newSelection ) )
			{
				RED_LOG_ERROR( "Replace objects: Failed to change selection" );
			}
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% successful for % object(s)", RED_NAME_CONSTEXPR( "Replace" ), m_createdObjectsPaths.Size() );

		return true;
	}

	Bool Action_ReplaceObjects::Undo()
	{
		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_createdObjectsPaths );
		if ( !resolver )
			return false;

		// delete the created objects
		IObjectResolver::DeleteContext deleteContext;
		deleteContext.pathsToDelete = m_createdObjectsPaths;
		if ( !resolver->Action_DeleteObjects( deleteContext ) )
			return false;

		// restore the deleted object back
		red::DynArray< EditorObjectIDPath > restoredObjectsPaths;
		if ( !resolver->Action_RestoreObjects( m_capturedObjects, restoredObjectsPaths ) )
			return false;

		// restore selection
		if ( m_changeSelection )
		{
			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( m_capturedOldSelection ) )
			{
				RED_LOG_ERROR( "Undo replace objects: Failed to change selection" );
			}
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% % successful for % object(s)", RED_NAME_CONSTEXPR( "Undo" ), RED_NAME_CONSTEXPR( "Replace" ), m_createdObjectsPaths.Size() );

		return true;
	}

	Bool Action_ReplaceObjects::IsModification() const
	{
		return true;
	}

	void Action_ReplaceObjects::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const EditorObjectIDPath& path : m_createdObjectsPaths )
		{
			outResourcePaths.Insert( path.GetFinalChildID().GetResourcePath() );
		}

		for ( const EditorObjectIDPath& path : m_objectsPathsToReplace )
		{
			outResourcePaths.Insert( path.GetFinalChildID().GetResourcePath() );
		}
	}
} // tools
