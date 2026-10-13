/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "dataControlBackend.h"

namespace tools
{
	struct ChildWithCategory;

	namespace prv
	{
		class EditableCollectionRootItem;

		class EditableCollectionDataControlBackend : public DataControlBackend
		{
		public:
			EditableCollectionDataControlBackend( prv::EditableCollectionRootItem* root );
			virtual ~EditableCollectionDataControlBackend() override;

			virtual proxy::GenericId GetRootId() const override;

			virtual proxy::ItemHierarchy GetBranchIds( const proxy::GenericId itemId ) const override;
			virtual red::DynArray< proxy::ItemData > GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const override;

			void OnItemChanged( const interop::IBaseObject* item );
			void OnItemCreated( interop::IBaseObject* parent, interop::IBaseObject* item, const Uint32 index );
			void OnItemRemoved( const interop::IBaseObject* item );
			void OnItemMoved( const interop::IBaseObject* item, const Uint32 index );

			interop::IBaseObject* GetItemById( const proxy::GenericId id );
			proxy::GenericId GetItemId( const interop::IBaseObject* item );

			void RegisterItem( interop::IBaseObject* item );
			void UnregisterItem( interop::IBaseObject* item );

		private:
			void SetRoot( interop::IBaseObject* rootItem );
			void CollectIds( const interop::IBaseObject* item, proxy::ItemHierarchy& outHierarchyData ) const;

			prv::EditableCollectionRootItem* m_root;

			red::HashMap< proxy::GenericId, interop::IBaseObject* > m_idToItem;
			red::HashMap< interop::IBaseObject*, proxy::GenericId > m_itemToId;

			mutable red::HashMap< proxy::GenericId, ChildWithCategory > m_idToNameInfo;

			red::Atomic< Uint64 > m_nextId;
		};
	}
}