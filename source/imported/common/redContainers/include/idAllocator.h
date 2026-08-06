#pragma once

#include "bitSet.h"
#include "bitSetDynamic.h"

class IFile;
class IDAllocatorDynamicSerializer;

static constexpr Uint32 cIdAllocator_InvalidId = std::numeric_limits<Uint32>::max();

/// Dynamic allocator for IDs
///  - Not thread safe - wrap in something if you want to use it from threads
///  - Very fast if not fragmented
template< Uint32 MaxObjects >
class IDAllocator
{
public:
	IDAllocator();

	// reset the allocator - release all IDs
	void Reset();

	// allocate new ID, will return Invalid Index when full
	Uint32 Alloc();

	// release allocated ID
	void Release( const Uint32 id );

	Uint32 GetNextUsed( const Uint32 id ) const;
    Uint32 GetFirstUsed() const;

	// get number of allocated IDs
	RED_FORCE_INLINE const Uint32 GetNumAllocated() const { return m_numAllocated; }

	// get the capacity of the allocator
	RED_FORCE_INLINE const Uint32 GetCapacity() const { return MaxObjects; }

	// is full ?
	RED_FORCE_INLINE const Bool IsFull() const { return (m_numAllocated == MaxObjects); }

	// get bit mask of free IDs (bit is set to 1 for an free ID)
	RED_FORCE_INLINE const Uint64* GetFreeBitMask() const { return m_freeIndices.Data(); }
	RED_FORCE_INLINE Bool IsAlive( Uint32 id ) const { return !m_freeIndices.Get( id ); }

    static constexpr Uint32 c_invalidId = cIdAllocator_InvalidId;

private:
	red::BitSet64< MaxObjects >	m_freeIndices;
	Uint32						m_firstFreeIndex;
	Uint32						m_searchIndex;
	Uint32						m_numAllocated;
};

/// Dynamic allocator for IDs
///  - Not thread safe - wrap in something if you want to use it from threads
///  - Very fast if not fragmented
///  - ID 0 is assumed to have special meaning (no ID) and is never returned
class RED_CONTAINERS_API IDAllocatorDynamic
{
	friend class IDAllocatorDynamicSerializer;

public:
	static const Uint32 c_invalidID = std::numeric_limits< Uint32 >::max();

	IDAllocatorDynamic();

	// prepare cache for use with given maximum object count
	void Resize( const Uint32 maxObjects );

	// reset the allocator - release all IDs
	void Reset();

	// allocate new ID, will return c_invalidID when full
	Uint32 Alloc();

	// release allocated ID
	void Release( const Uint32 id );

	// get number of allocated IDs
	RED_FORCE_INLINE const Uint32 GetNumAllocated() const { return m_numAllocated; }

	// get the capacity of the allocator
	RED_FORCE_INLINE const Uint32 GetCapacity() const { return m_freeIndices.Size(); }

	// is full ?
	RED_FORCE_INLINE const Bool IsFull() const { return (m_numAllocated == m_freeIndices.Size()); }

	RED_FORCE_INLINE const red::BitSet64Dynamic& GetFreeIndices() const { return m_freeIndices; }

	// check if an ID is allocated
	RED_FORCE_INLINE Bool IsAlive( Uint32 id ) const { return !m_freeIndices.Get( id ); }

private:
	red::BitSet64Dynamic	m_freeIndices;
	Uint32					m_firstFreeIndex;
	Uint32					m_searchIndex;
	Uint32					m_numAllocated;
};

#include "idAllocator.hpp"