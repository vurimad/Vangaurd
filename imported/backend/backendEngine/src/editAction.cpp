/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editAction.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IEditAction, tools )
RTTI_END_TYPE()

namespace tools
{
	IEditAction::IEditAction()
	{
	}

	IEditAction::~IEditAction()
	{
	}

	Bool IEditAction::TryMerge( const IEditAction* lastUndoAction )
	{
		return false;
	}

	Bool IEditAction::CanDo() const
	{
		return true;
	}

	Bool IEditAction::IsModification() const
	{
		// TODO this should be pure virtual
		return true;
	}

	void IEditAction::GetAffectedResources( red::Set< res::ResourcePath >& /* outResourcePaths */ ) const
	{
		// TODO this should be pure virtual
	}

	//---

	EditGroupAction::EditGroupAction(const String& desc)
		: m_desc(desc)
	{}

	void EditGroupAction::AddAction( red::UniquePtr< IEditAction > action)
	{
		m_actions.PushBack( std::move( action ) );
	}

	String EditGroupAction::GetDescription() const
	{
		return m_desc;
	}

	Bool EditGroupAction::Do()
{
		for ( Uint32 i = 0; i < m_actions.Size(); ++i )
		{
			if (!m_actions[i]->Do())
			{
				// roll back
				for ( Uint32 j = i; j-- > 0; )
				{
					if (!m_actions[j]->Undo())
					{
						RED_LOG_ERROR("Failed to roll back group action '%hs' at action %d/%d: %hs",
							m_desc.AsChar(), j, m_actions.Size(), m_actions[j]->GetDescription().AsChar());
						break;
					}
				}

				return false;
			}
		}

		OnSucces();
		return true;
	}

	Bool EditGroupAction::Undo()
	{
		// undo in reversed order
		for ( Uint32 i = m_actions.Size(); i-- > 0; )
		{
			if ( !m_actions[i]->Undo() )
			{
				// roll back
				for ( Uint32 j = i; j < m_actions.Size(); ++j )
				{
					if ( !m_actions[j]->Do() )
					{
						RED_LOG_ERROR( "Failed to roll back undo of group action '%hs' at action %d/%d: %hs",
							m_desc.AsChar(), j, m_actions.Size(), m_actions[j]->GetDescription().AsChar() );
						break;
					}
				}

				return false;
			}
		}

		OnSucces();
		return true;
	}

	Bool EditGroupAction::TryMerge(const IEditAction* lastUndoAction)
	{
		return false;
	}

	Bool EditGroupAction::CanDo() const
	{
		for ( const auto& action : m_actions )
		{
			if ( !action->CanDo() )
			{
				return false;
			}
		}

		return true;
	}

	Bool EditGroupAction::IsModification() const
	{
		for ( const auto& action : m_actions )
		{
			if ( action->IsModification() )
			{
				return true;
			}
		}

		return false;
	}

	void EditGroupAction::GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const
	{
		for ( const auto& action : m_actions )
		{
			action->GetAffectedResources( outResourcePaths );
		}
	}

	void EditGroupAction::OnSucces()
	{
	}
	
} // tools