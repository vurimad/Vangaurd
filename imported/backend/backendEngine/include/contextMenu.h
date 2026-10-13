#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/interop/include/backendMenu.h"

namespace tools
{
	class BACKEND_ENGINE_API ContextMenuBackend
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( ContextMenuBackend );

	public:
		typedef std::function< void() > TMenuItemAction;

		ContextMenuBackend();
		virtual ~ContextMenuBackend() {}

		Bool AddItem( const shared::MenuItem& item, TMenuItemAction action = TMenuItemAction() );
		Bool AddItem( const shared::MenuItem& item, const CName& parentId, TMenuItemAction action = TMenuItemAction() );

	private:
		void OnSelection( const CName& id );

		shared::MenuCollection m_menu;

		red::HashMap< CName, shared::MenuItem* > m_itemsMap;
		red::HashMap< CName, TMenuItemAction > m_actions;
		red::HashMap< CName, CName > m_groupsMap;
	};
}