/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollectionDataControlBackend.h"

#include "editableCollectionInterop.h"

#include "../../common/generalEditorBackend/include/interopHierarchyWrappers.h"

namespace Config
{
	TConfigVar < Bool > cvShowReadonlyProperties( "Editor/PropertyPage", "ShowReadOnlyProperties", true );
}

namespace tools
{
	namespace prv
	{
		EditableCollectionDataControlBackend::EditableCollectionDataControlBackend( prv::EditableCollectionRootItem* root )
			: m_root( root )
			, m_nextId( 1 )
		{
			SetRoot( root );
		}

		EditableCollectionDataControlBackend::~EditableCollectionDataControlBackend()
		{
			RED_ASSERT( m_idToItem.Empty(), "Not all items were unregistered properly" );
			RED_ASSERT( m_itemToId.Empty(), "Not all items were unregistered properly" );
		}

		proxy::GenericId EditableCollectionDataControlBackend::GetRootId() const
		{
			return proxy::GenericId( 0 );
		}

		proxy::ItemHierarchy EditableCollectionDataControlBackend::GetBranchIds( const proxy::GenericId itemId ) const
		{
			proxy::ItemHierarchy branchRoot;

			auto it = m_idToItem.Find( itemId );
			if ( it != m_idToItem.End() )
			{
				interop::IBaseObject* item = it.Value();
				CollectIds( item, branchRoot );
			}

			return branchRoot;
		}

		red::DynArray< proxy::ItemData > EditableCollectionDataControlBackend::GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const
		{
			red::DynArray< proxy::ItemData > outData;
			for ( const proxy::GenericId& id : itemsIds )
			{
				auto it = m_idToItem.Find( id );
				if ( it != m_idToItem.End() )
				{
					interop::IBaseObject* item = it.Value();

					proxy::ItemData itemData;
					itemData.m_itemId = id;
					tools::ExtractInteropData( item, itemData );

					// TODO: something safer
					THandle< tools::BaseObjectData > baseData = Cast< tools::BaseObjectData >( itemData.m_data.Front() );
					RED_ASSERT( baseData );

			 		auto it = m_idToNameInfo.Find( id );
					if ( it != m_idToNameInfo.End() )
					{
						baseData->m_nameAndCategoryInfo = it.Value();
					}

					outData.PushBack( itemData );
				}
			}

			return outData;
		}

		void EditableCollectionDataControlBackend::OnItemChanged( const interop::IBaseObject* item )
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );
			proxy::GenericId id = it.Value();

			NotifyItemChanged( id );
		}

		void EditableCollectionDataControlBackend::OnItemCreated( interop::IBaseObject* parent, interop::IBaseObject* item, const Uint32 index )
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );
			proxy::GenericId itemId = it.Value();

			it = m_itemToId.Find( parent );
			RED_ASSERT( it != m_itemToId.End() );
			proxy::GenericId parentId = it.Value();

			NotifyItemCreated( parentId, itemId, index );
		}

		void EditableCollectionDataControlBackend::OnItemRemoved( const interop::IBaseObject* item )
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );
			proxy::GenericId id = it.Value();

			NotifyItemRemoved( id );
		}

		void EditableCollectionDataControlBackend::OnItemMoved( const interop::IBaseObject* item, const Uint32 index )
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );
			proxy::GenericId id = it.Value();

			NotifyItemMoved( id, index );
		}

		void EditableCollectionDataControlBackend::SetRoot( interop::IBaseObject* rootItem )
		{
			const proxy::GenericId rootId = GetRootId();

			auto it = m_idToItem.Find( rootId );
			if ( it != m_idToItem.End() )
			{
				m_itemToId.Remove( it.Value() );
			}

			m_idToItem[rootId] = rootItem;
			m_itemToId[rootItem] = rootId;
		}

		interop::IBaseObject* EditableCollectionDataControlBackend::GetItemById( const proxy::GenericId id )
		{
			auto it = m_idToItem.Find( id );
			return it != m_idToItem.End() ? it.Value() : nullptr;
		}

		proxy::GenericId EditableCollectionDataControlBackend::GetItemId( const interop::IBaseObject* item )
		{
			auto it = m_itemToId.Find( item );
			return it != m_itemToId.End() ? it.Value() : proxy::GenericId();
		}

		void EditableCollectionDataControlBackend::RegisterItem( interop::IBaseObject* item )
		{
			proxy::GenericId newId( m_nextId.PostIncrement() );
			m_idToItem.Insert( newId, item );
			m_itemToId.Insert( item, newId );
		}

		void EditableCollectionDataControlBackend::UnregisterItem( interop::IBaseObject* item )
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );

			proxy::GenericId id = it.Value();

			m_itemToId.Remove( it );
			m_idToItem.Remove( id );

			NotifyItemRemoved( id );
		}

		void EditableCollectionDataControlBackend::CollectIds( const interop::IBaseObject* item, proxy::ItemHierarchy& outHierarchyData ) const
		{
			auto it = m_itemToId.Find( item );
			RED_ASSERT( it != m_itemToId.End() );
			outHierarchyData.m_itemId = it.Value();

			if ( item->GetMetaType() == interop::IBaseObject::MetaType::Array )
			{
				const interop::IArrayObject* arrayItem = static_cast<const interop::IArrayObject*>( item );
				const Uint32 size = arrayItem->GetSize( DummyContext() );
				for ( Uint32 index = 0; index < size; ++index )
				{
					const interop::ObjectPtr childItem = arrayItem->GetElement( DummyContext(), index );

					proxy::ItemHierarchy childHierarchyData;
					CollectIds( childItem.Get(), childHierarchyData );
					outHierarchyData.m_children.PushBack( childHierarchyData );
				}
			}
			else if ( item->GetMetaType() == interop::IBaseObject::MetaType::Compound )
			{
				const interop::ICompoundObject* compoundItem = static_cast<const interop::ICompoundObject*>( item );
				red::DynArray< interop::ChildWithCategory > childrenInfo;
				compoundItem->GetChildrenInfo( DummyContext(), childrenInfo );
				for ( const interop::ChildWithCategory& childInfo : childrenInfo )
				{
					const interop::ObjectPtr childItem = compoundItem->GetChild( DummyContext(), childInfo.m_name );

					proxy::ItemHierarchy childHierarchyData;
					CollectIds( childItem.Get(), childHierarchyData );

					if ( !m_idToNameInfo.KeyExist( childHierarchyData.m_itemId ) )
					{
						m_idToNameInfo.Insert( childHierarchyData.m_itemId, { childInfo.m_name, childInfo.m_category } );
					}

					outHierarchyData.m_children.PushBack( childHierarchyData );
				}
			}
		}
	}
}