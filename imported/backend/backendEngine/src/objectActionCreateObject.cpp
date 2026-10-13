/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectResolver.h"
#include "objectHandler.h"
#include "objectActionCreateObject.h"
#include "objectPersistentCollection.h"
#include "editableProxy.h"
#include "selectionHandler.h"

namespace tools
{
	Action_CreateObject::Action_CreateObject( ObjectHandler& objectHandler, const red::DynArray<ObjectCreationInfo>& initInfos, const String& desc, Bool refreshSelection )
		: m_objectHandler( objectHandler )
		, m_desc( desc )
		, m_refreshSelection( refreshSelection )
		, m_capturedOldSelection( objectHandler.GetSelectionHandler()->CloneSelection() ) // capture current selection so it can be restored
		, m_infos{ red::PoolBackend() }
		, m_createdObjectPaths{ red::PoolBackend() }
	{
		m_infos.PushBack( initInfos );
	}

	Action_CreateObject::~Action_CreateObject() = default;

	String Action_CreateObject::GetDescription() const
	{
		return m_desc;
	}

	Bool Action_CreateObject::Do()
	{
		red::DynArray< EditorObjectIDPath > objectsToSelect{ red::PoolBackend() };

		m_createdObjectPaths.Clear();

		if ( m_createdObjects ) // restore objects created before (after undo)
		{
			IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_createdObjects );
			if( !resolver )
				return false;

			// we already have captured collection
			if ( !resolver->Action_RestoreObjects( m_createdObjects, m_createdObjectPaths ) )
			{
				return false;
			}
		}
		else if ( !m_infos.Empty() ) // create the objects for the first time
		{
			IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_infos );
			if( !resolver )
				return false;

			Bool selectAll = false;
			const IObjectResolver::CreateObjectsOutParams outParams =
			{
				m_createdObjectPaths,
				&objectsToSelect,
				selectAll,
			};

			// first time create the objects for real, next time, use the collection
			m_createdObjects = resolver->Action_CreateObjects( m_infos, outParams );

			if ( !m_createdObjects )
			{
				return false;
			}

			if ( selectAll )
			{
				for ( const EditorObjectIDPath& path : m_createdObjectPaths )
				{
					RED_FATAL_ASSERT( !path.IsEmpty() );
					objectsToSelect.PushBack( path );
				}
			}

			// initialize new selection
			m_newSelection = SelectionState::EmptySelection();

			// find the selection proxies for the objects
			for ( const EditorObjectIDPath& path : objectsToSelect )
			{
				// find the object representation
				const auto editableProxy = resolver->ResolveProxy( path );
				if ( editableProxy )
				{
					m_newSelection->Add( editableProxy );
				}
			}
		}

		// set new selection
		// NOTE: if we fail we are not that screwed yet so there's no "return false" here
		if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( m_newSelection, m_refreshSelection ) )
		{
			RED_LOG_ERROR( "Create object(s): Failed to change selection" );
		}

		// action done
		return true;
	}

	Bool Action_CreateObject::Undo()
	{
		// restore previous selection
		if ( !m_objectHandler.GetSelectionHandler()->Action_ChangeSelection( m_capturedOldSelection, m_refreshSelection ) )
		{
			RED_LOG_ERROR( "Undo create object(s): Failed to change selection" );
			return false;
		}

		// find resolver for created objects
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_createdObjectPaths );
		RED_ASSERT( resolver, "Failed to find unified resolver for selection" );
		if ( !resolver )
			return false;

		// delete exactly the same objects that were created in the Do step
		IObjectResolver::DeleteContext deleteContext;
		deleteContext.pathsToDelete = m_createdObjectPaths;
		return resolver->Action_DeleteObjects( deleteContext );
	}

	Bool Action_CreateObject::IsModification() const
	{
		return true;
	}

	void Action_CreateObject::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const EditorObjectIDPath& path : m_createdObjectPaths )
		{
			if ( !path.IsEmpty() )
			{
				const EditorObjectID& id = path.GetFinalChildID();
				outResourcePaths.Insert( id.GetResourcePath() );
			}
		}
	}

} // tools