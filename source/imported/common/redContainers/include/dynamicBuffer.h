/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red 
{

//////////////////////////////////////////////////////////////////////////
// We need to pack DynamicBuffer to 4 bytes, so the whole structure is 12 bytes long.
// This is because children classes contains another 4 byte and we need them to be 16 bytes long.
// Since DynamicBuffer cannot be instantiated itself, and all its children classes are aligned to 16 bytes
// the following "trick" shouldn't introduce any negative performance issues.

#pragma pack (push, 4)

class RED_CONTAINERS_API DynamicBuffer
{
protected:

	void*	m_buffer;
	Uint32  m_capacity;

	DynamicBuffer( const red::memory::Pool& pool = red::PoolDefault() );
	~DynamicBuffer();

	RED_FORCE_INLINE void* Data() { return m_buffer; }
	RED_FORCE_INLINE const void* Data() const { return m_buffer; }
	RED_FORCE_INLINE Uint32 Capacity() const { return m_capacity; }

	// IMPORTANT: 'moveFunc' has to move "old" objects to a "new" place AND call destructors for "old" objects.
	void ResizeBuffer( Uint32 capacity, Uint32 elementSize, Uint32 alignment, void ( *moveFunc )( void*, void*, Uint32, const void* ) = nullptr );
	void ReleaseBuffer( Uint32 elementSize, Uint32 alignment );
	void* GetPoolStoragePtr( Uint32 elementSize ) const;

private:

	static Uint64 CalcPoolStorageOffset( Uint32 capacity, Uint32 elementSize );
	static Uint32 CalcBufferWithPoolStorageSize( Uint32 capacity, Uint32 elementSize );
};

#pragma pack(pop)

} // red

