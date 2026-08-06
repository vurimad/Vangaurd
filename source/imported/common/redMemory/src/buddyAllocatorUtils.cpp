/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "buddyAllocatorUtils.h"

namespace red
{
namespace memory
{
	void BuddyListInit( BuddyBlockInfo* list )
	{
		list->next = list;
		list->prev = list;
	}

	void BuddyListAdd( BuddyBlockInfo* list, BuddyBlockInfo* node )
	{
		node->next = list;
		node->prev = list->prev;
		list->prev->next = node;
		list->prev = node;
	}

	void BuddyListRemove( BuddyBlockInfo* list )
	{
		list->next->prev = list->prev;
		list->prev->next = list->next;
		list->next = list;
		list->prev = list;
	}

	bool BuddyListContains( BuddyBlockInfo* list, BuddyBlockInfo* node )
	{
		BuddyBlockInfo* front = list;
		while ( !IsBuddyListEmpty( front ) )
		{
			front = front->next;
			if ( front == node )
			{
				return true;
			}
			else if ( front == list )
			{
				return false;
			}
		}

		return false;
	}

	bool IsBuddyListEmpty( BuddyBlockInfo* list )
	{
		return list->next == list;
	}

	BuddyBlockInfo* BuddyListPop( BuddyBlockInfo* list )
	{
		BuddyBlockInfo* front = nullptr;
		if ( !IsBuddyListEmpty( list ) )
		{
			front = list->next;
			BuddyListRemove( front );
		}
		return front;
	}

	u32 BuddyILog2( u32 value )
	{
		return ( ( c_buddyNumBits - 1 ) - BUILTIN_CLZ( value ) );
	}

	u32 BuddyMaxLevels( u32 totalSize, u32 minSize )
	{
		return BuddyILog2( totalSize ) - BuddyILog2( minSize );
	}

	u32 BuddyMaxIndexes( u32 totalSize, u32 minSize )
	{
		return ( 1 << ( BuddyMaxLevels( totalSize, minSize ) + 1 ) );
	}

	u32 BuddyBlockIndexSize( u32 totalSize, u32 minSize )
	{
		return ( ( BuddyMaxIndexes( totalSize, minSize ) + ( c_buddyNumBits - 1 ) ) / c_buddyNumBits );
	}

	void BuddyBitArraySet( u32* bitArray, u32 index )
	{
		bitArray[index >> c_buddyBitArrayIndexShift] |= ( 1 << ( index & c_buddyBitArrayIndexMask ) );
	}

	void BuddyBitArrayClear( u32* bitArray, u32 index )
	{
		bitArray[index >> c_buddyBitArrayIndexShift] &= ~( 1 << ( index & c_buddyBitArrayIndexMask ) );
	}

	bool IsBuddyBitArraySet( u32* bitArray, u32 index )
	{
		return ( bitArray[index >> c_buddyBitArrayIndexShift] & ( 1 << ( index & c_buddyBitArrayIndexMask ) ) ) != 0;
	}

	void BuddyBitArrayNot( u32* bitArray, u32 index )
	{
		u32 arrayIndex = index >> c_buddyBitArrayIndexShift;
		u32 bitValue = ( 1 << ( index & c_buddyBitArrayIndexMask ) );
		if ( bitArray[arrayIndex] & bitValue )
		{
			bitArray[arrayIndex] &= ~bitValue;
		}
		else
		{
			bitArray[arrayIndex] |= bitValue;
		}
	}
}
}