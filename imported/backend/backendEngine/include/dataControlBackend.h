/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "backendTimer.h"

#include "../../../common/generalEditorBackend/include/interopControlProxy.h" 

namespace tools
{
	class BACKEND_ENGINE_API IDataControlBackendListener
	{
	public:
		virtual ~IDataControlBackendListener();

		virtual void OnItemChanged( const proxy::GenericId itemId ) = 0;
		virtual void OnItemRemoved( const proxy::GenericId itemId ) = 0;
		virtual void OnItemCreated( const proxy::GenericId parentId, const proxy::GenericId itemId, const Uint32 index ) = 0;
		virtual void OnItemMoved( const proxy::GenericId itemId, const Uint32 index ) = 0;
	};

	class BACKEND_ENGINE_API DataControlBackend
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		DataControlBackend();
		virtual ~DataControlBackend();

		virtual proxy::GenericId GetRootId() const;

		virtual proxy::ItemHierarchy GetBranchIds( const proxy::GenericId branchRootId ) const;
		virtual red::DynArray< proxy::ItemData > GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const;

		virtual void CreateItem( const proxy::ItemData& info );
		virtual void RemoveItem( const proxy::GenericId parentId, const proxy::GenericId itemId );

		proxy::ItemsHierarchyData GetBranchIdsAndData( const proxy::GenericId branchRootId ) const;

		// listeners
		void AttachListener( IDataControlBackendListener* listener );
		void DetachListener( IDataControlBackendListener* listener );

		void NotifyItemChanged( const proxy::GenericId itemId );
		void NotifyItemRemoved( const proxy::GenericId itemId );
		void NotifyItemCreated( const proxy::GenericId parentId, const proxy::GenericId itemId, const Uint32 index );
		void NotifyItemMoved( const proxy::GenericId id, const Uint32 index );

	private:
		red::DynArray< IDataControlBackendListener* > m_listeners;
	};

	class BACKEND_ENGINE_API DataControlInterop final : public IDataControlBackendListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( DataControlInterop );

	public:
		DataControlInterop( const red::SharedPtr< DataControlBackend >& dataControlBackend );
		virtual ~DataControlInterop();

		// IDataControlBackendListener
		virtual void OnItemChanged( const proxy::GenericId itemId ) override;
		virtual void OnItemRemoved( const proxy::GenericId itemId ) override;
		virtual void OnItemCreated( const proxy::GenericId parentId, const proxy::GenericId itemId, const Uint32 index ) override;
		virtual void OnItemMoved( const proxy::GenericId itemId, const Uint32 index ) override;

	private:
		void UpdateQueue();

		red::SharedPtr< DataControlBackend > m_dataControlBackend;

		red::DynArray< proxy::GenericMessage > m_messageQueue;
		interop::InteropNotifier m_notifier;
		Timer m_timer;
	};
}