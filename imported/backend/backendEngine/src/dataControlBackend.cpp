/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dataControlBackend.h"

namespace tools
{
	IDataControlBackendListener::~IDataControlBackendListener() = default;

	DataControlBackend::DataControlBackend() = default;
	DataControlBackend::~DataControlBackend() = default;

	proxy::GenericId DataControlBackend::GetRootId() const
	{
		return proxy::GenericId();
	}

	proxy::ItemHierarchy DataControlBackend::GetBranchIds( const proxy::GenericId branchRootId ) const
	{
		return proxy::ItemHierarchy();
	}

	red::DynArray< proxy::ItemData > DataControlBackend::GetItemsData( const red::DynArray< proxy::GenericId >& itemsIds ) const
	{
		return red::DynArray< proxy::ItemData >();
	}

	void DataControlBackend::CreateItem( const proxy::ItemData& info )
	{
	}

	void DataControlBackend::RemoveItem( const proxy::GenericId parentId, const proxy::GenericId itemId )
	{
	}

	proxy::ItemsHierarchyData DataControlBackend::GetBranchIdsAndData( const proxy::GenericId branchRootId ) const
	{
		proxy::ItemsHierarchyData hierarchyAndData;
		hierarchyAndData.m_hierarchy = GetBranchIds( branchRootId );

		if ( hierarchyAndData.m_hierarchy.IsValid() )
		{
			red::DynArray< proxy::GenericId > hierarchyIds;
			proxy::ExtractIdsFromHierarchy( hierarchyAndData.m_hierarchy, hierarchyIds );

			hierarchyAndData.m_data = GetItemsData( hierarchyIds );
		}

		return hierarchyAndData;
	}

	void DataControlBackend::AttachListener( IDataControlBackendListener* listener )
	{
		red::alg::PushBackUnique( m_listeners, listener );
	}

	void DataControlBackend::DetachListener( IDataControlBackendListener* listener )
	{
		m_listeners.Remove( listener );
	}

	void DataControlBackend::NotifyItemChanged( const proxy::GenericId itemId )
	{
		for ( IDataControlBackendListener* listener : m_listeners )
		{
			listener->OnItemChanged( itemId );
		}
	}

	void DataControlBackend::NotifyItemRemoved( const proxy::GenericId itemId )
	{
		for ( IDataControlBackendListener* listener : m_listeners )
		{
			listener->OnItemRemoved( itemId );
		}
	}

	void DataControlBackend::NotifyItemCreated( const proxy::GenericId parentId, const proxy::GenericId itemId, const Uint32 index )
	{
		for ( IDataControlBackendListener* listener : m_listeners )
		{
			listener->OnItemCreated( parentId, itemId, index );
		}
	}

	void DataControlBackend::NotifyItemMoved( const proxy::GenericId id, const Uint32 index )
	{
		for ( IDataControlBackendListener* listener : m_listeners )
		{
			listener->OnItemMoved( id, index );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	void DataControlInterop::SetupInterop( DataControlInterop* ptr, ::interop::utils::Object& op )
	{
		op.RegisterValueRO<proxy::GenericId>( "rootId", [ptr]()
		{
			return ptr->m_dataControlBackend->GetRootId();
		} );

		op.RegisterFunctor< proxy::ItemHierarchy, proxy::GenericId >( "getBranchIds", [ptr]( interop::IContext& ctx, const proxy::GenericId branchRootId )
		{
			return ptr->m_dataControlBackend->GetBranchIds( branchRootId );
		}, interop::FunctorType::Const );

		op.RegisterFunctor< red::DynArray< proxy::ItemData >, red::DynArray< proxy::GenericId > >( "getItemsData", [ptr]( interop::IContext& ctx, const red::DynArray< proxy::GenericId >& itemsIDs )
		{
			return ptr->m_dataControlBackend->GetItemsData( itemsIDs );
		}, interop::FunctorType::Const );

		op.RegisterFunctor< proxy::ItemsHierarchyData, proxy::GenericId >( "getBranchIdsAndData", [ptr]( interop::IContext& ctx, const proxy::GenericId branchRootId )
		{
			return ptr->m_dataControlBackend->GetBranchIdsAndData( branchRootId );
		}, interop::FunctorType::Const );

		op.RegisterFunctor< void, proxy::ItemData >( "createItem", [ptr]( interop::IContext& ctx, const proxy::ItemData& info )
		{
			ptr->m_dataControlBackend->CreateItem( info );
		} );

		op.RegisterFunctor< void, proxy::GenericId, proxy::GenericId >( "removeItem", [ptr]( interop::IContext& ctx, const proxy::GenericId parentID, const proxy::GenericId itemID )
		{
			ptr->m_dataControlBackend->RemoveItem( parentID, itemID );
		} );

		op.RegisterFunctor<void>( "_notifierRegistered", [ptr]( interop::IContext& ctx )
		{
			if ( !ptr->m_timer.IsScheduled() )
			{
				ptr->m_timer.ExecuteEvery( 0.01f, [ptr]()
				{
					ptr->UpdateQueue();
				} );
			}

			ptr->OnItemChanged( ptr->m_dataControlBackend->GetRootId() );
		}, interop::FunctorType::Const );

		op.RegisterNotifier( "_onChanged", ptr->m_notifier );
	}

	DataControlInterop::DataControlInterop( const red::SharedPtr< DataControlBackend >& dataControlBackend )
		: m_dataControlBackend( dataControlBackend )
		, m_timer( "DataControlBackend" )
	{
		m_dataControlBackend->AttachListener( this );
	}

	DataControlInterop::~DataControlInterop()
	{
		m_dataControlBackend->DetachListener( this );
	}

	void DataControlInterop::OnItemChanged( const proxy::GenericId itemId )
	{
		proxy::GenericMessage message{ "ch", itemId };
		red::alg::PushBackUnique( m_messageQueue, std::move( message ) );
	}

	void DataControlInterop::OnItemRemoved( const proxy::GenericId itemId )
	{
		proxy::GenericMessage message{ "r", itemId };
		m_messageQueue.PushBack( std::move( message ) );
	}

	void DataControlInterop::OnItemCreated( const proxy::GenericId parentId, const proxy::GenericId itemId, const Uint32 index )
	{
		proxy::GenericMessage message{ "cr", itemId, parentId, index };
		red::alg::PushBackUnique( m_messageQueue, std::move( message ) );
	}

	void DataControlInterop::OnItemMoved( const proxy::GenericId itemId, const Uint32 index )
	{
		proxy::GenericMessage message{ "m", itemId, index };
		red::alg::PushBackUnique( m_messageQueue, std::move( message ) );
	}

	void DataControlInterop::UpdateQueue()
	{
		// TODO: may want to send only data which is relevant
		if ( !m_messageQueue.Empty() )
		{
			m_notifier.Notify( m_messageQueue );
			m_messageQueue.Clear();
		}
	}

}