#include "build.h"
#include "contextMenu.h"

#include "../../../common/interop/include/interopUtilsRTTI.h"


namespace tools
{
	ContextMenuBackend::ContextMenuBackend()
		: m_itemsMap{ red::PoolBackend() }
		, m_actions{ red::PoolBackend() }
		, m_groupsMap{ red::PoolBackend() }
	{}

	Bool ContextMenuBackend::AddItem( const shared::MenuItem& item, ContextMenuBackend::TMenuItemAction action )
	{
		if ( m_itemsMap.KeyExist( item.m_id ) )
		{
			return false;
		}

		m_menu.m_items.PushBack( item );
		m_itemsMap.Insert( item.m_id, &m_menu.m_items.Back() );

		if ( action )
		{
			m_actions.Insert( item.m_id, action );
		}
		return true;
	}

	Bool ContextMenuBackend::AddItem( const shared::MenuItem& item, const CName& parentId, ContextMenuBackend::TMenuItemAction action )
	{
		if ( m_itemsMap.KeyExist( item.m_id ) )
		{
			return false;
		}

		if ( m_itemsMap.KeyExist( parentId ) )
		{
			shared::MenuItem* parentItem = m_itemsMap[parentId];
			parentItem->m_subItems.PushBack( item );
			parentItem->m_type = shared::MenuItemType::Group;
			m_itemsMap.Insert( item.m_id, &parentItem->m_subItems.Back() );
		}
		else
		{
			m_menu.m_items.PushBack( item );
			m_itemsMap.Insert( item.m_id, &m_menu.m_items.Back() );
		}

		if ( action )
		{
			m_actions.Insert( item.m_id, action );
		}
		return true;
	}

	void ContextMenuBackend::OnSelection( const CName& id )
	{
		if ( m_actions.KeyExist( id ) )
		{
			m_actions[id]();
		}
	}

	void ContextMenuBackend::SetupInterop( ContextMenuBackend* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "hierarchy", [ptr]( interop::IContext& ctx ) { return interop::utils::CreateRTTIAccessFunc( ctx, &ptr->m_menu ); } );

		op.RegisterFunctor<void, CName>( "select", [ptr]( interop::IContext& ctx, const CName id ) { ptr->OnSelection( id ); } );
	}
}