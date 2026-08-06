/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_BUDDY_ALLOCATOR_UTILS_H_
#define RED_MEMORY_BUDDY_ALLOCATOR_UTILS_H_

namespace red
{
namespace memory
{

#if defined( RED_PLATFORM_ORBIS ) || defined ( RED_PLATFORM_LINUX )
#define BUILTIN_CLZ( value ) __builtin_clz( value )
#define BUILTIN_CTZ( value ) __builtin_ctz( value )
#else
	inline i32 clz( u32 value )
	{
		DWORD ret;
		_BitScanReverse( &ret, value );
		return (i32)( 31 ^ ret );
	}

	inline i32 ctz( u32 value )
	{
		DWORD ret;
		_BitScanForward( &ret, value );
		return (i32)ret;
	}

#define BUILTIN_CLZ( value ) clz( value )
#define BUILTIN_CTZ( value ) ctz( value )
#endif

	const u32 c_buddyNumBits = 8 * sizeof( u32 );
	const u32 c_buddyMinLeafSize = sizeof( void* ) * 2;

	// bits metadata
	const u32 c_buddyBitArrayNumBits = 8 * sizeof( u32 );
	const u32 c_buddyBitArrayIndexShift = BUILTIN_CTZ( c_buddyBitArrayNumBits );
	const u32 c_buddyBitArrayIndexMask = c_buddyBitArrayNumBits - 1;

	struct BuddyBlockInfo
	{
		BuddyBlockInfo* next;
		BuddyBlockInfo* prev;
	};

	void BuddyListInit( BuddyBlockInfo* list );
	void BuddyListAdd( BuddyBlockInfo* list, BuddyBlockInfo* node );
	void BuddyListRemove( BuddyBlockInfo* list );
	bool BuddyListContains( BuddyBlockInfo* list, BuddyBlockInfo* node );
	bool IsBuddyListEmpty( BuddyBlockInfo* list );
	BuddyBlockInfo* BuddyListPop( BuddyBlockInfo* list );

	u32 BuddyILog2( u32 value );
	u32 BuddyMaxLevels( u32 totalSize, u32 minSize );
	u32 BuddyMaxIndexes( u32 totalSize, u32 minSize );
	u32 BuddyBlockIndexSize( u32 totalSize, u32 minSize );

	void BuddyBitArraySet( u32* bitArray, u32 index );
	void BuddyBitArrayClear( u32* bitArray, u32 index );
	bool IsBuddyBitArraySet( u32* bitArray, u32 index );
	void BuddyBitArrayNot( u32* bitArray, u32 index );
}
}

#endif