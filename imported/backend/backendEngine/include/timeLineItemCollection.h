/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "timeLineTypes.h"

namespace tools
{
	class TimeLineBase;

	class BACKEND_ENGINE_API TimeLineItemCollection
	{
		RTTI_DECLARE_TYPE( TimeLineItemCollection )
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		TimeLineItemCollection();

		void GetItems( red::DynArray< TimelineId >& items ) const;
		const red::ArraySpan< const THandle< TimeLineItemDescription > > GetItems() const;

		void Add( THandle< TimeLineItemDescription > item );
		void Remove( TimelineId id );
		void Clear();

		template < typename T>
		void Initialize( TimeLineBase* timeline )
		{
			for ( THandle< TimeLineItemDescription >& item : m_items )
			{
				THandle< T > descriptor = Cast< T >( item );
				descriptor->SetTimeline( timeline );
				descriptor->OnIntialized();
			}
		}

		template < typename T>
		T* Find( TimelineId id ) const
		{
			TimeLineItemDescription* item = FindInLRUCache( id );
			if ( item != nullptr )
			{
				StoreInLRUCache( item );
				return Cast< T >( item );
			}
		
			for ( const THandle< TimeLineItemDescription >& item : m_items )
			{
				if ( item->GetId() == id )
				{
					StoreInLRUCache( item.Get() );
					return Cast< T >( item.Get() );
				}
			}

			return nullptr;
		}

	private:
		TimeLineItemDescription* FindInLRUCache( TimelineId id ) const;

		void StoreInLRUCache( TimeLineItemDescription* item ) const;
		void RemoveFromLRUCache( TimeLineItemDescription* item );

		red::DynArray< THandle< TimeLineItemDescription > > m_items;

		mutable TimeLineItemDescription* m_lruNode;
	};
}