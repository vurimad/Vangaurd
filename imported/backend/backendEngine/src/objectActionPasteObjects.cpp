/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectHandler.h"

#include "objectPersistentCollection.h"
#include "objectActionPasteObjects.h"
#include "selectionHandler.h"

#include "editActionHistory.h"
#include "resourceEditor.h"

#include "../../../common/core/include/message.h"

namespace tools
{
	Action_PasteObjects::Action_PasteObjects( ObjectHandler& objectHandler, const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& placement, const Bool selectCreatedObjects )
		: m_objectHandler( objectHandler )
		, m_objects( objects )
		, m_placementInfo( placement )
		, m_changeSelection( selectCreatedObjects )
		, m_wasPasted( false )
	{		
		// capture the IDs of the selected objects
		if ( m_changeSelection )
			m_capturedOldSelection = m_objectHandler.GetSelectionHandler()->CloneSelection();
	}

	String Action_PasteObjects::GetDescription() const
	{
		return "Paste objects";
	}

	Bool Action_PasteObjects::Do()
	{
		// create objects, this creates the visualization to
		m_objectPaths.Clear();

		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_objects );
		if ( !resolver )
			return false;

		if ( !m_wasPasted )
		{
			// real pasting - Do() is called for the first time
			if ( !resolver->Action_PlaceObjects( m_objects, m_placementInfo, m_objectPaths ) )
				return false;

			// capture the pasted objects
			m_objects = resolver->CaptureObjects( m_objectPaths );
			if ( !m_objects )
				return false;

			m_wasPasted = true;
		}
		else
		{
			// restore already pasted objects
			if ( !resolver->Action_RestoreObjects( m_objects, m_objectPaths ) )
				return false;
		}

		// select the objects
		if ( m_changeSelection )
		{
			// set new selection
			SelectionStatePtr newSelection = SelectionState::EmptySelection();

			// find the selection proxies for the objects
			for ( const auto& id : m_objectPaths )
			{
				// find the object representation
				const auto editableProxy = resolver->ResolveProxy( id );
				if ( editableProxy )
				{
					newSelection->Add( editableProxy );
				}
			}

			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( newSelection ) )
			{
				RED_LOG_ERROR( "Paste objects: Failed to change selection" );
				return false;
			}
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% successful for % object(s)", RED_NAME_CONSTEXPR( "Paste" ), m_objectPaths.Size() );

		return true;
	}

	Bool Action_PasteObjects::Undo()
	{
		// find resolver
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_objects );
		if ( !resolver )
			return false;

		// restore selection
		if ( m_changeSelection )
		{
			if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( m_capturedOldSelection ) )
			{
				RED_LOG_ERROR( "Undo paste objects: Failed to change selection" );
				return false;
			}
		}

		// delete the created objects
		IObjectResolver::DeleteContext deleteContext;
		deleteContext.pathsToDelete = m_objectPaths;
		if ( !resolver->Action_DeleteObjects( deleteContext ) )
			return false;

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% % successful for % object(s)", RED_NAME_CONSTEXPR( "Undo" ), RED_NAME_CONSTEXPR( "Paste" ), m_objectPaths.Size() );

		return true;
	}

	Bool Action_PasteObjects::IsModification() const
	{
		return true;
	}

	void Action_PasteObjects::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const EditorObjectIDPath& path : m_objectPaths )
		{
			if ( !path.IsEmpty() )
			{
				const EditorObjectID& id = path.GetFinalChildID();
				outResourcePaths.Insert( id.GetResourcePath() );
			}
		}
	}

} // tools
