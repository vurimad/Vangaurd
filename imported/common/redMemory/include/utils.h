/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_UTILS_H_
#define _RED_MEMORY_INCLUDE_UTILS_H_

#include "redMemoryInternal.h"

#define RED_KILO_BYTE( count ) ( count << 10 )
#define RED_MEGA_BYTE( count ) ( count << 20 )
#define RED_GIGA_BYTE( count ) ( static_cast< red::memory::u64 >( count ) << 30 )

constexpr size_t operator"" _KB(unsigned long long size)
{
	return static_cast<size_t>(size * 1024);
}

constexpr size_t operator"" _MB(unsigned long long size)
{
	return static_cast<size_t>(size * 1024_KB);
}

constexpr size_t operator"" _GB(unsigned long long size)
{
	return static_cast<size_t>(size * 1024_MB);
}

#if defined( RED_COMPILER_CLANG )
	#define RED_STATIC_PRIORITY( number ) __attribute__ ((init_priority (number)))
#else
	#define RED_STATIC_PRIORITY( number )
#endif

#define RED_FLAGS( value ) ( 1 << ( value ) )

namespace red
{
namespace memory
{
	struct Block;
	class ThreadMonitor;

	//////////////////////////////////////////////////////////////////////////
	// When RegisterCurrentThread is called, the current thread future allocation will be routed
	// through lockless allocator when possible.
	RED_MEMORY_API void RegisterCurrentThread( const AnsiChar* threadName );

	RED_MEMORY_API ThreadMonitor* GetThreadMonitor();

	//////////////////////////////////////////////////////////////////////////
	// Notify redMemory about new frame and reset frame-based allocators.
	// It should be called exactly once per frame.
	RED_MEMORY_API void ProcessNextFrame();

	bool IsPowerOf2( u64 value );

	bool IsAligned( u64 value, u32 alignment );
	bool IsAligned( const void * block, u32 alignment );

	u64 AlignAddress( u64 address, u32 alignment );
	void * AlignAddress( const void * address, u32 alignment );

	// Only for power of two. Same than AlignAddress, just for being clear. 
	u64 RoundUp( u64 value, u64 roundUpTo );
	u32 RoundUp( u32 value, u32 roundUpTo );

	template< typename T >
	u64 AddressOf( T & value );

	// IMPORTANT this version do not behave like std::addressof. 
	// It return address pointed by pointer. use AddressOf( &pointer ) if you the pointer address.
	template< typename T >
	u64 AddressOf( T * value );

	template< typename Proxy >
	u32 GetDefaultAlignment();

	void MemsetBlock( const Block & block, i32 value );
	void MemsetBlock( u64 block, i32 value, u64 size );

	RED_MEMORY_API void MemcpyBlock( u64 destination, u64 source, u64 size );
	RED_MEMORY_API void MemcpyBlock( Block & destination, const Block & source );
}
}

#include "utils.hpp"

#endif
