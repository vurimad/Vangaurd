/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"
#include "physicsObjectActionsHelper.h"
#include "physicsObjectResolver.h"
#include "../../backendEngine/include/selectionHandler.h"

namespace tools
{
	namespace helper
	{
		red::DynArray< EditorObjectIDPath > EditorObjectIdsToPaths( const red::DynArray< EditorObjectID >& ids )
		{
			red::DynArray< EditorObjectIDPath > ret;
			ret.Reserve( ids.Size() );

			for ( const EditorObjectID& id : ids )
			{
				ret.PushBack( EditorObjectIDPath( id ) );
			}

			return ret;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// IPhysicsSimpleEditAction
	//////////////////////////////////////////////////////////////////////////


	IPhysicsSimpleEditAction::IPhysicsSimpleEditAction( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids )
		: m_resolver( resolver)
		, m_selectionHandler( selection )
		, m_ids( ids )
	{
		// capture data of the objects that will be snapped
		m_capturedObjects = m_resolver->CaptureObjects( helper::EditorObjectIdsToPaths( m_ids ) );

		// capture selection state before objects are snapped
		m_capturedOldSelection = m_selectionHandler->CloneSelection();

	}

	String IPhysicsSimpleEditAction::GetDescription() const
	{
		return m_desc;
	}

	Bool IPhysicsSimpleEditAction::Undo()
	{
		// restore the deleted object back
		red::DynArray< EditorObjectIDPath > restoredObjectsPaths;

		IObjectResolver::DeleteContext deleteContext;
		const red::DynArray< EditorObjectIDPath >& pathsToDelete = helper::EditorObjectIdsToPaths( m_ids );
		deleteContext.pathsToDelete = pathsToDelete;
		if( !m_resolver->Action_DeleteObjects( deleteContext ) )
			return false;

		if( !m_resolver->Action_RestoreObjects( m_capturedObjects, restoredObjectsPaths ) )
			return false;

		// restore selection
		if( !m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection ) )
			return false;

		return true;
	}

	//////////////////////////////////////////////////////////////////////////
	// Action_SnapObject
	//////////////////////////////////////////////////////////////////////////

	Action_SnapObject::Action_SnapObject(PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids, Bool alternativeMode )
		: IPhysicsSimpleEditAction(resolver, selection, ids )
		, m_alternativeMode( alternativeMode )
	{
		m_desc = "Snapping objects";
	}

	Bool Action_SnapObject::Do()
	{
		// make sure state is valid
		if (!m_capturedObjects || !m_capturedOldSelection)
			return false;

		// Snap objects.
		if (!m_resolver->Action_SnapObjects(m_ids, m_alternativeMode ))
			return false;

		return true;
	}	

	//////////////////////////////////////////////////////////////////////////
	// Action_SelectParent
	//////////////////////////////////////////////////////////////////////////

	Action_SelectParent::Action_SelectParent( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids )
		: m_resolver( resolver )
		, m_selectionHandler( selection )
		, m_ids( ids )
	{
		m_desc = "Select object's parent";

		// capture data of the objects that will be snapped
		m_capturedObjects = m_resolver->CaptureObjects( helper::EditorObjectIdsToPaths( m_ids ) );

		// capture selection state before objects are snapped
		m_capturedOldSelection = m_selectionHandler->GetSelection()->Clone();
	}


	String Action_SelectParent::GetDescription() const
	{
		return m_desc;
	}

	Bool Action_SelectParent::Do()
	{
		// make sure state is valid
		if( !m_capturedObjects || !m_capturedOldSelection )
			return false;

		// Snap objects.
		if( !m_resolver->Action_SelectParent( m_ids ) )
			return false;

		return true;
	}

	Bool Action_SelectParent::Undo()
	{
		// restore selection
		if( !m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection ) )
			return false;

		return true;
	}

	Bool Action_SelectParent::IsModification() const
	{
		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Action_SelectObject
	//////////////////////////////////////////////////////////////////////////

	Action_SelectObject::Action_SelectObject( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids )
		: m_resolver( resolver )
		, m_selectionHandler( selection )
		, m_ids( ids )
	{
		m_desc = "Select object";

		// capture data of the objects that will be snapped
		m_capturedObjects = m_resolver->CaptureObjects( helper::EditorObjectIdsToPaths( m_ids ) );

		// capture selection state before objects are snapped
		m_capturedOldSelection = m_selectionHandler->GetSelection()->Clone();
	}


	String Action_SelectObject::GetDescription() const
	{
		return m_desc;
	}

	Bool Action_SelectObject::Do()
	{
		// make sure state is valid
		if( !m_capturedObjects || !m_capturedOldSelection )
			return false;

		// Snap objects.
		if( !m_resolver->Action_SelectObject( m_ids ) )
			return false;

		return true;
	}

	Bool Action_SelectObject::Undo()
	{
		// restore selection
		if( !m_selectionHandler->Action_ChangeSelection( m_capturedOldSelection ) )
			return false;

		return true;
	}

	Bool Action_SelectObject::IsModification() const
	{
		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Action_RecomputeCOM
	//////////////////////////////////////////////////////////////////////////

	Action_RecomputeCOM::Action_RecomputeCOM( PhysicsObjectResolver* resolver, SelectionHandler* selection, const TObjectIDs& ids )
		: IPhysicsSimpleEditAction( resolver, selection, ids )
	{
		m_desc = "Recompute COM";
	}

	Bool Action_RecomputeCOM::Do()
	{
		// make sure state is valid
		if( !m_capturedObjects || !m_capturedOldSelection )
			return false;

		// Snap objects.
		if( !m_resolver->Action_RecomputeCOM( m_ids ) )
			return false;

		return true;
	}

	//////////////////////////////////////////////////////////////////////////
	// Action_GenerateDefaultDestructionHierarchy
	//////////////////////////////////////////////////////////////////////////
	
	Action_GenerateDefaultDestructionHierarchy::Action_GenerateDefaultDestructionHierarchy(PhysicsObjectResolver* resolver)
		: m_resolver(resolver)
	{
		m_desc = "Generate default destruction hierarchy";
	}

	String Action_GenerateDefaultDestructionHierarchy::GetDescription() const
	{
		return m_desc;
	}

	Bool Action_GenerateDefaultDestructionHierarchy::Do()
	{
		// Snap objects.
		if( !m_resolver->Action_GenerateDefaultDestructionHierarchy( m_previousHierarchy ) )
			return false;

		return true;
	}

	Bool Action_GenerateDefaultDestructionHierarchy::Undo()
	{
		if( !m_resolver->Action_SetDestructionHierarchy(m_previousHierarchy) )
			return false;

		return true;
	}

}
