/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

/**
 *	Fixed size block allocator (aka pool).
 *
 *	Features:
 *	- very fast: constant allocation / deallocation time
 *	- zero bytes per element overhead
 *	- doesn't do any dynamic memory allocations; only uses memory given via Init() function
 */

class RED_CONTAINERS_API Pool
{
public:

	// Default constructor
	constexpr Pool();
	// Move constructor
	Pool( Pool&& other );
	// Move assignment
	Pool& operator=( Pool&& other );
	// Swap data with other pool
	void Swap( Pool& other );

	// Initializes pool with given buffer; Note: it's up to user to release the buffer when not using pool
	void Init( void* buffer, Uint32 bufferSize, Uint32 blockSize );
	// Deinitialized pool
	void Deinit();
	// Gets whether pool is initialized
	constexpr Bool IsInitialized() const { return m_buffer != nullptr; }
	
	// Upsizes pool capacity while preserving indices of allocated elements; performs typed 'move' operator for all existing elements
	template < typename T >
	void Upsize( void* newBuffer, Uint32 newCapacity );
	// Clears all elements; doesn't call destructors on allocated elements
	void Clear();

	// Gets pointer to the used memory buffer
	RED_INLINE void* Data() { return m_buffer; }
	// Gets pointer to the used memory buffer
	constexpr const void* Data() const { return m_buffer; }
	// Gets maximum number of elements that can be stored in the pool
	constexpr Uint32 Capacity() const { return m_capacity; }
	// Gets maximum size (in bytes) of data that can be stored in the pool
	constexpr Uint32 DataCapacity() const { return m_capacity * m_blockSize; }
	// Gets whether pool contains maximum number of allocated blocks
	constexpr Bool Full() const { return m_firstFreeBlockIndex == INVALID_INDEX; }
	// Get size (in bytes) of single block
	constexpr Uint32 BlockSize() const { return m_blockSize; }

	// Allocates new block; returns nullptr on failure
	void* AllocateBlock();
	// Allocates new block; returns new block index or INVALID_INDEX on failure
	Uint32 AllocateBlockIndex();
	// Frees block
	void FreeBlock( const void* block );
	// Frees block by index
	void FreeBlockIndex( Uint32 index );
	
	// Gets memory of the block at given index
	RED_INLINE void* GetBlock( Uint32 index );
	// Gets memory of the block at given index
	RED_INLINE const void* GetBlock( Uint32 index ) const;
	// Get index of specified block
	RED_INLINE Uint32 GetBlockIndex( const void* block ) const;

	// Count number of free blocks; It has linear complexity so use it with caution
	Uint32 CountFreeBlocks() const;

private:

	struct Entry
	{
		Uint32 m_next;
	};

	void* m_buffer;					// Pool buffer
	Uint32 m_capacity;				// Number of managed blocks
	Uint32 m_blockSize;				// Size of single block
	Uint32 m_firstFreeBlockIndex;	// List of free elements
	Uint32 m_numberOfInitialized;	// Number of initialized blocks (allocated or freed but linked into list)

	void UpdateNextFreeBlock( const Entry* allocatedBlock );
};

} // red

 //////////////////////////////////////////////////////////////////////////
 // Placement new

RED_INLINE void* operator new( size_t, red::Pool& pool )
{
	return pool.AllocateBlock();
}

#include "pool.hpp"