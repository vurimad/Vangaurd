/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "objectActionUngroupObjects.h"


#include "../../../common/core/include/message.h"
#include "editActionHistory.h"
#include "resourceEditor.h"

namespace tools
{
	Action_UngroupObjects::Action_UngroupObjects( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds )
		: m_objectHandler( objectHandler )
		, m_paths( paths )
		, m_groupIds( groupIds )
	{		
	}

	Action_UngroupObjects::~Action_UngroupObjects() = default;

	String Action_UngroupObjects::GetDescription() const
	{
		return "Ungroup objects";
	}

	Bool Action_UngroupObjects::Do()
	{
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_paths );
		if ( !resolver )
			return false;

		String failReason;
		if ( !resolver->CanUngroupObjects( m_paths, &failReason ) )
		{
			RED_LOG_ERROR( "Cannot ungroup objects: %s", failReason.AsChar() );
			return false;
		}

		// now it's safe to delete objects because we captured the state
		if ( !resolver->Action_UngroupObjects( m_paths ) )
		{
			RED_LOG_ERROR( "Ungroup objects: Action_DeleteObjects failed" );
			return false;
		}

		RED_REPORT_SUCCESS( RED_LOCATION( m_objectHandler.GetActionHistory()->GetResourceEditor()->GetPath() ), "% successful - Ungrouped % object(s)", RED_NAME_CONSTEXPR( "Ungroup" ), m_paths.Size() );

		return true;
	}

	Bool Action_UngroupObjects::Undo()
	{
		IObjectResolver* resolver = m_objectHandler.GetUnifiedResolver( m_paths );
		if ( !resolver )
			return false;

		if ( !resolver->Action_GroupObjects( m_paths, m_groupIds ) )
			return false;

		RED_REPORT_SUCCESS( RED_LOCATION( m_paths[ 0 ].GetFirstChildID().GetResourcePath() ), "% successful - % undone for % object(s)", RED_NAME_CONSTEXPR( "Regrouping" ), RED_NAME_CONSTEXPR( "Ungrouping" ), m_paths.Size() );
		return true;
	}

	Bool Action_UngroupObjects::IsModification() const
	{
		return true;
	}

	void Action_UngroupObjects::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
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
