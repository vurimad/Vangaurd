/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "timeLineBaseItem.h"

#include "timeLineBase.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TimeLineItemCollection, tools );
	RTTI_PROPERTY( m_items );
RTTI_END_TYPE();

namespace tools
{
	TimeLineItemCollection::TimeLineItemCollection()
		: m_lruNode( nullptr )
		, m_items{ red::PoolBackend() }
	{
	}

	void TimeLineItemCollection::GetItems( red::DynArray< TimelineId >& items ) const
	{
		items.Reserve( m_items.Size() );

		for ( const THandle< TimeLineItemDescription >& item : m_items )
		{
			items.PushBack( item->GetId() );
		}
	}

	const red::ArraySpan< const THandle< tools::TimeLineItemDescription > > TimeLineItemCollection::GetItems() const
	{
		return m_items;
	}

	void TimeLineItemCollection::Add(THandle< TimeLineItemDescription > item)
	{
		if ( !item || item->GetId() == TimelineInvalidId )
			return;

		m_items.PushBack( item );
		StoreInLRUCache( item.Get() );
	}
		
	void TimeLineItemCollection::Remove( TimelineId id )
	{
		TimeLineItemDescription* item = FindInLRUCache( id );
		if ( item != nullptr )
		{
			RemoveFromLRUCache( item );
		}

		for ( THandle< TimeLineItemDescription >& item : m_items )
		{
			if ( item->GetId() == id )
			{
				m_items.Remove( item );
				break;
			}
		}
	}

	void TimeLineItemCollection::Clear()
	{
		m_items.Clear();
		// #backendEngine_timeline_szi Shouldn't we also clear LRU cache? Otherwise Find() may still return us that item (if we ask for that item).
	}

	TimeLineItemDescription* TimeLineItemCollection::FindInLRUCache( TimelineId id ) const
	{
		if ( m_lruNode && m_lruNode->GetId() == id )
			return m_lruNode;

		return nullptr;
	}

	void TimeLineItemCollection::StoreInLRUCache( TimeLineItemDescription* item ) const
	{
		m_lruNode = item;
	}

	void TimeLineItemCollection::RemoveFromLRUCache( TimeLineItemDescription* item )
	{
		if ( m_lruNode == item )
			m_lruNode = nullptr;
	}
}