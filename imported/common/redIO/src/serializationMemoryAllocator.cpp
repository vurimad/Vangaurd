/**
* Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "../../redMemory/include/defaultAllocator.h"
#include "serializationMemoryAllocator.h"
#include "redIOAsyncIO.h"
#include "../redMemory/include/systemAllocator.h"

// Remove HookType_All?? vs just HookType_Marking?
// Don't cache everything (maybe video)?
#ifdef NO_EDITOR
#define ENABLE_IO_MEMORY_CACHE
#endif

#ifdef ENABLE_IO_MEMORY_CACHE
const Bool c_enabledIOMemoryCache = true;
#else
const Bool c_enabledIOMemoryCache = false;
#endif

namespace red
{
	namespace internal
	{
		// Specialize to stop the memory hook from overwriting released memory. We want to be able to reuse the data written to it.
		template<>
		class UniqueBufferAllocator< io::MemoryAllocator> : public UniqueBufferAllocatorInterface
		{
		public:
			UniqueBufferAllocator(io::MemoryAllocator* proxy)
				: m_proxy(proxy)
			{}

			virtual void* ReallocateAligned(void* inputBuffer, Uint32 size, Uint32 alignment) override final
			{
				return memory::ReallocateAligned(*m_proxy, inputBuffer, size, alignment, red::memory::HookType_All);
			}

			virtual void Free(void* buffer) override final
			{
				memory::Free(*m_proxy, buffer, red::memory::HookType_All);
			}

			virtual const red::memory::Pool* GetPool() const
			{
				return nullptr;
			}

		private:
			io::MemoryAllocator* m_proxy;
		};
	}
}

namespace io
{
#ifdef RED_PLATFORM_CONSOLE
const Uint32 c_ioAllocatorBudget = RED_MEGA_BYTE( 62 );
#else
const Uint32 c_ioAllocatorBudget = RED_MEGA_BYTE( 126 );
#endif
const Uint32 s_pendingAllocs = 4096;
const Uint32 s_defaultAlign = 16;

// NOTE: If later support cancellation for merged async ops, need to ensure that will set this on the main merged op (and not just the sub-merged ops)
// since they'll only have the rawptr buffer and not the bufferForIO shared value. Help ensure that no partially read then cancelled values will go into the cache.

// Usually I/O is going to be a resource ('W2RC') or compressed file 'KRAK'. So choosing something unlikely to occur as valid starting I/O.
// The only other guarantee is to just avoid I/O cancellation entirely.
extern const Uint32 c_uncacheableMemoryTag = 0xdeadbeef;

MemoryAllocator::MemoryAllocator()
{
	Initialize();
}

MemoryAllocator::~MemoryAllocator()
{
	Shutdown();
}

Bool MemoryAllocator::TryAlloc(const RequestToken& request, AllocResult& outResult)
{
	// Early out if disabled
	if (!c_enabledIOMemoryCache && request.mustAllocCachedIOResult)
	{
		return false;
	}

	Bool ret = false;
	Bool mustKickIOWorker = false;
	{
		RED_SCOPE_LOCK(m_lock);

		ret = Alloc_NoLock(request, outResult);

		outResult.m_ioMemoryHasCachedResults = m_allocFlags.m_ioMemoryHasCachedResults;

		if (m_allocFlags.m_evictedCacheMemoryDuringAllocAttempt)
		{
			// Freed I/O memory, so we must inform the IOWorker that it can wake up and maybe do more I/O now
			mustKickIOWorker = true;
		}

		m_allocFlags = AllocFlags{}; // reset
	}

	// Not under the lock, since our locks and events don't play nicely
	if (mustKickIOWorker)
	{
		GAsyncIO.KickWorkerThread();
	}

	return ret;
}

void MemoryAllocator::Free( red::memory::Block& block )
{
	// Hooks fill in the size
#ifdef RED_MEMORY_ENABLE_HOOKS
	//RED_FATAL_ASSERT( block.address != 0 && block.size != 0, "Should not have wrapped zero allocation!" );
#else
	//RED_FATAL_ASSERT( block.address != 0, "Should not have wrapped zero allocation!");
#endif

	{
		//PC_SCOPE(SerializationMemoryFreeThenNextAlloc);

		RED_SCOPE_LOCK(m_lock);

		RED_FATAL_ASSERT(!m_recursionCheck);
		red::ScopedFlag< Bool > guard{ m_recursionCheck = true, false };

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		if (!c_enabledIOMemoryCache || !TryCacheFreedMemory_NoLock(block))
		{
			m_wrappedAllocator.Free(block);
		}
#else
		m_wrappedAllocator.Free(block);
#endif
		UpdateFreeStats_NoLock(block); // Caching is transparent
	}

	GAsyncIO.KickWorkerThread();
}

void MemoryAllocator::InvalidateCacheForFileHandle(TFileHandle handle)
{
	RED_UNUSED(handle);

	RED_SCOPE_SHARED_LOCK(m_lock);

	// Since archives, then for now just do this which should be faster and less complicated in general.
	// Mainly only if changing DLC or censorship settings.
	TryEvictMemory_NoLock(UINT32_MAX);
}

void MemoryAllocator::GetMemoryRuntimeMetrics( RuntimeIOMemoryMetrics& outMetrics )
{
	RED_SCOPE_SHARED_LOCK(m_lock);

	// #fixme: really block sizes, but not sure how to get (smaller) actual request size from the allocator during the Free() call
	// #fixme: interesting stats like biggest block avail, fragmentation etc.
	// guard bytes overhead
	outMetrics.m_totalBytesAllocated = m_numBytesAlloc;
	outMetrics.m_peakTotalBytesAllocated = m_peakTotalBytesAlloc;
	outMetrics.m_maxRequestBytes = m_maxBytesAlloc;
	outMetrics.m_systemMemoryConsumed = c_ioAllocatorBudget;
	outMetrics.m_numberGuardBytes = 0;
	outMetrics.m_memoryBudgetTotal = c_ioAllocatorBudget;
	outMetrics.m_numAllocsInUse = m_numAllocsInUse;
}

Bool MemoryAllocator::Alloc_EntireRequest_NoLock( const RequestToken& req, TAllocator& allocator, Uint32 align, void*& outPtrForIO, void*& outPtrForDecompression )
{
	const Uint32 numBytesForIODest = req.numBytesForIO;
	const Uint32 numBytesForDecompressorDest = req.numBytesForDecompressor;

	CacheKey cacheKey{ req };
	CacheEntry* cacheEntry = nullptr;

	// Check now before allocating any decompression memory and potentially evicted cache for no reason
	// It's possible we'll then try to alloc decompression memory and then lose this entry, but should generally be OK
	// FIXME: make stats to see how often that actually happens
	if (req.mustAllocCachedIOResult && numBytesForIODest > 0)
	{
		// Allocating with the allocator, not pool, so CAN return nullptr for OOM instead of invoking the OOM handler
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		cacheEntry = cacheKey.IsValid() ? m_cacheEntryLookup.FindOverlappingEntry(cacheKey) : nullptr;
		if (!cacheEntry)
		{
			return false;
		}
#endif
	}

	if (numBytesForDecompressorDest > 0)
	{
#ifndef RED_MEMORY_USE_DEBUG_ALLOCATOR
		// disabling memory marking, because it will slow entire allocation and we don't need it here anyway
		for (;;)
		{
			outPtrForDecompression = RED_ALLOCATE_ALIGNED_WITHOUT_HOOKS( allocator, numBytesForDecompressorDest, align, red::memory::HookType::HookType_Memory_Marking );
			if (outPtrForDecompression)
			{
				break;
			}
			if (!TryEvictMemory_NoLock(numBytesForDecompressorDest))
			{
				break;
			}
		}

#else
		outPtrForDecompression = RED_ALLOCATE_ALIGNED( allocator, numBytesForDecompressorDest, align );
#endif
		if (!outPtrForDecompression)
		{
			RED_FATAL_ASSERT(m_numAllocsInUse > 0);
			return false;
		}

		if (cacheEntry && !IsCacheEntryStillValid_NoLock(cacheEntry))
		{
			cacheEntry = nullptr;
		}
	}

	if ( numBytesForIODest > 0 )
	{
		// Allocating with the allocator, not pool, so CAN return nullptr for OOM instead of invoking the OOM handler
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR

		// If exact match, simply extract and reuse memory
		// !!! NOTE: cacheEntry maybe become stale if evicted
		if (!cacheEntry)
		{
			cacheEntry = cacheKey.IsValid() ? m_cacheEntryLookup.FindOverlappingEntry(cacheKey) : nullptr;
		}

		if (!cacheEntry && req.mustAllocCachedIOResult)
		{
			if (outPtrForDecompression)
			{
				red::memory::Block block = { reinterpret_cast<Uint64>(outPtrForDecompression), 0 };
				allocator.Free(block);
			}
			outPtrForDecompression = nullptr;
			return false;
		}

		if (cacheEntry)
		{
			if (cacheEntry->m_cacheKey == cacheKey)
			{
				m_allocFlags.m_ioMemoryHasCachedResults = true;

				outPtrForIO = ExtractEntryFromCache_NoLock(cacheEntry);
			}
			else
			{
				// Try to 1. ensure it doesn't get evicted next, and 2. update the LRU list so it doesn't get evicted soon in the future
				TouchLRU_NoLock(cacheEntry);
			}
		}

		// disabling memory marking, because it will slow entire allocation and we don't need it here anyway
		if (!outPtrForIO)
		{
			// Just round up so we can tag the memory. When releasing it, we can't be certain to know the alloc size here.
			// Perhaps better to have some custom memory hook instead, but much easier here than inside the memory system right now.
			const Uint32 roundedUpSize = Max<Uint32>(numBytesForIODest, (Uint32)sizeof(c_uncacheableMemoryTag));
			
			for (;;)
			{
				outPtrForIO = RED_ALLOCATE_ALIGNED_WITHOUT_HOOKS( allocator, roundedUpSize, align, red::memory::HookType::HookType_Memory_Marking );
				if (outPtrForIO)
				{
					*reinterpret_cast<Uint32*>(outPtrForIO) = c_uncacheableMemoryTag;

					break;
				}

				// !!! NOTE: can invalidate cacheEntry
				if (!TryEvictMemory_NoLock(numBytesForIODest))
				{
					break;
				}
			}

			// It wasn't an exact match, but we can still copy the data out of it to avoid I/O
			if (cacheEntry && IsCacheEntryStillValid_NoLock(cacheEntry))
			{
				m_allocFlags.m_ioMemoryHasCachedResults = true;

				const Uint8* cachedIOResults = reinterpret_cast<const Uint8*>(cacheEntry->m_address);
				const Int64 offsetDiff = cacheKey.m_fileOffset - cacheEntry->m_cacheKey.m_fileOffset;
				RED_FATAL_ASSERT( offsetDiff >= 0 );
				RED_FATAL_ASSERT( numBytesForIODest + (Uint64)offsetDiff <= cacheEntry->m_cacheKey.m_readSize ); // make sure we won't overread from src

				red::Memcpy(outPtrForIO, cachedIOResults + offsetDiff, numBytesForIODest);
			}
			else
			{
				// Try to merge ops, so don't give it this memory yet
				if (req.mustAllocCachedIOResult)
				{
					if (outPtrForDecompression)
					{
						red::memory::Block block = { reinterpret_cast<Uint64>(outPtrForDecompression), 0 };
						allocator.Free(block);
					}
					outPtrForDecompression = nullptr;
					return false;
				}
			}
		}
#else
		outPtrForIO = RED_ALLOCATE_ALIGNED( allocator, numBytesForIODest, align );
#endif
		if ( !outPtrForIO )
		{
			RED_FATAL_ASSERT(m_numAllocsInUse > 0);

			if (outPtrForDecompression)
			{
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
				red::memory::Block block = { reinterpret_cast<Uint64>(outPtrForDecompression), 0 };
				allocator.Free(block);
#else
				RED_FREE(allocator, outPtrForDecompression);
#endif
				outPtrForDecompression = nullptr;
			}

			return false;
		}
	}

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
	TrackPendingIO_NoLock(cacheKey, outPtrForIO);
#endif

	return true;
}

Bool MemoryAllocator::Alloc_NoLock( const RequestToken& req, AllocResult& outResult )
{
	// Make sure cleared
	outResult = AllocResult{};

	const Uint32 align = s_defaultAlign;
	const Uint32 numBytesForIODest = req.numBytesForIO;
	const Uint32 numBytesForDecompressorDest = req.numBytesForDecompressor;

#if 0 // TODO: shortcut
#ifndef RED_MEMORY_USE_DEBUG_ALLOCATOR
	const Uint32 maxBlockSize = static_cast<Uint32>(m_wrappedAllocator.GetBiggestBlockSize());
#else
	const Uint32 maxBlockSize = s_tempMemBudget;
#endif

	const Uint32 biggestSize = std::max<Uint32>(pending.request.numBytesForIO, pending.request.numBytesForDecompressor);
	if (biggestSize > maxBlockSize)
	{
		return;
	}
#endif

	void* ptrForIO = nullptr;
	void* ptrForDecompression = nullptr;
	const char* sanitizedDebugName = req.m_debugFileName ? req.m_debugFileName : "<Unknown source>";
	
	// Not an exact assert, since allocator overhead may still prevent the allocation. But better to try and catch too big requests in the callstack where they occur.
	// Then we also check numAllocsInUse if the allocation fails, which may be after a Free(), so detached from the initial request.
	// NOTE: not asserting if 'mustAllocCachedIOResult' just to reduce any risk here, since have a special hack check to avoid trying to alloc I/O memory (in the editor) because of editor terrain
	RED_FATAL_ASSERT( req.mustAllocCachedIOResult || (numBytesForIODest + numBytesForDecompressorDest <= c_ioAllocatorBudget), "%s: Cannot alloc %u+%u bytes: %u max", sanitizedDebugName, numBytesForIODest, numBytesForDecompressorDest, c_ioAllocatorBudget );

	if ( !Alloc_EntireRequest_NoLock( req, m_wrappedAllocator, align, ptrForIO, ptrForDecompression ) )
	{
		RED_FATAL_ASSERT( req.mustAllocCachedIOResult || m_numAllocsInUse > 0, "%s: Allocation can never be given memory!", sanitizedDebugName );
		return false;
	}

	// Update buffers, now that all memory allocated; otherwise freeing the buffer now would trigger another attempted alloc!
	if ( ptrForIO )
	{
		outResult.m_bufferForIO = red::MakeUniqueBuffer( *this, ptrForIO, numBytesForIODest, align );
	}

	if ( ptrForDecompression )
	{
		outResult.m_bufferForDecompressor = red::MakeUniqueBuffer( *this, ptrForDecompression, numBytesForDecompressorDest, align );
	}

	UpdateAllocStats_NoLock( ptrForIO, ptrForDecompression );

	return true;
}

void MemoryAllocator::UpdateAllocStats_NoLock( const void* ptrForIO, const void* ptrForDecompression )
{
	Uint32 actualNumBytesForIODest = 0;
	const Uint64 ptrForIOAddress = red::memory::AddressOf( ptrForIO );
	if ( ptrForIOAddress )
	{
		actualNumBytesForIODest = static_cast< Uint32 >( m_wrappedAllocator.GetBlockSize( ptrForIOAddress ) );
		m_numAllocsInUse += 1;
	}

	Uint32 actualNumBytesForDecompressorTarget = 0;
	const Uint64 ptrForDecompressorAddress = red::memory::AddressOf( ptrForDecompression );
	if ( ptrForDecompressorAddress )
	{
		actualNumBytesForDecompressorTarget = static_cast< Uint32 >( m_wrappedAllocator.GetBlockSize( ptrForDecompressorAddress ) );
		m_numAllocsInUse += 1;
	}

	const Int64 totalRequestBlockSize = actualNumBytesForIODest + actualNumBytesForDecompressorTarget;

	RED_FATAL_ASSERT( totalRequestBlockSize <= c_ioAllocatorBudget, "I/O pool too small for memory request! Budget=%u, numBytesForIODest=%u, numBytesForDecompressorDest=%u",
					  c_ioAllocatorBudget, actualNumBytesForIODest, actualNumBytesForDecompressorTarget );

	m_numBytesAlloc += totalRequestBlockSize;

	if ( totalRequestBlockSize > m_maxBytesAlloc )
	{
		m_maxBytesAlloc = totalRequestBlockSize;
	}

	if ( m_numBytesAlloc > m_peakTotalBytesAlloc )
	{
		m_peakTotalBytesAlloc = m_numBytesAlloc;
	}
}

void MemoryAllocator::UpdateFreeStats_NoLock( red::memory::Block& block )
{
	m_numBytesAlloc -= block.size;
	m_numAllocsInUse -= 1;
	RED_FATAL_ASSERT( m_numAllocsInUse >= 0 );
}

void MemoryAllocator::TrackPendingIO_NoLock(const CacheKey& cacheKey, const void* ptrForIO)
{
	if (ptrForIO && cacheKey.IsValid())
	{
		PendingCacheEntry entry;
		entry.m_address = reinterpret_cast<red::memory::u64>(ptrForIO);
		entry.m_cacheKey = cacheKey;
		m_pendingIOMemoryRequests.Insert(entry.m_address, entry);
	}
}

Bool MemoryAllocator::TryEvictMemory_NoLock(Uint32 bytesToEvict)
{
	Uint32 approxBytesEvicted = 0;
	while (!m_lruList.Empty())
	{
		CacheEntry& cacheEntry = *m_lruList.Begin();
		const Uint32 id = GetCacheEntryIndex(cacheEntry);

		RED_FATAL_ASSERT(cacheEntry.m_cacheKey.m_readSize > 0);
		RED_FATAL_ASSERT(cacheEntry.m_address != 0);
		approxBytesEvicted += cacheEntry.m_cacheKey.m_readSize; // can't use red::memory::Block size in final, no stats!

		red::memory::Block block{ cacheEntry.m_address, 0 };
		m_wrappedAllocator.Free(block);
		m_allocFlags.m_evictedCacheMemoryDuringAllocAttempt = true;
		//UpdateFreeStats_NoLock(block); NO! we've already called then when Free() was called. Caching should be transparent to the stats.

		m_cacheEntryLookup.RemoveEntry(&cacheEntry);
		m_lruList.Remove(m_lruList.Begin());
		m_freeCacheEntries.Release(id);

		cacheEntry.m_address = 0; // ensure non-reusable

		if (approxBytesEvicted >= bytesToEvict)
		{
			break;
		}
	}

	RED_FATAL_ASSERT(!m_lruList.Empty() || m_freeCacheEntries.GetNumAllocated() == 0, "Corrupted state");

	if (approxBytesEvicted > 0)
	{
		return true;
	}

	RED_FATAL_ASSERT(m_lruList.Empty());

	//red::memory::TLSFAllocatorMetrics metrics{};
	//m_wrappedAllocator.BuildMetrics(metrics);

	return false;
}

Bool MemoryAllocator::TryCacheFreedMemory_NoLock(red::memory::Block& block)
{
	auto it = m_pendingIOMemoryRequests.Find(block.address);
	if (it == m_pendingIOMemoryRequests.End())
	{
		// we don't track decompression memory
		return false;
	}

	PendingCacheEntry pendingEntryCopy = it.Value();
	m_pendingIOMemoryRequests.Remove(it);

	const Uint32 tag = *reinterpret_cast<const Uint32*>(block.address);
	if (tag == c_uncacheableMemoryTag)
	{
		// Assume memory wasn't written to; e.g., I/O cancellation
		return false;
	}

	Uint32 id = m_freeCacheEntries.Alloc();
	if (id == cIdAllocator_InvalidId)
	{
#ifdef DEBUG_CHECK_OUT_OF_CACHE_ENTRIES
		RED_DEBUG_BREAK();
#endif

		if (TryEvictMemory_NoLock(1))
		{
			id = m_freeCacheEntries.Alloc();
			RED_FATAL_ASSERT(id != cIdAllocator_InvalidId);
		}
		else
		{
			// TODO: assert all memory in flight for decompression
		}
	}

	if (id == cIdAllocator_InvalidId)
	{
		return false;
	}

	CacheEntry& entry = m_cacheEntries[id];
	entry.m_cacheKey = pendingEntryCopy.m_cacheKey;
	entry.m_address = block.address;
	
	RED_FATAL_ASSERT(entry.m_cacheKey.m_readSize > 0);

	// Possible to "miss" a cached entry in flight and make a duplicate read request.
	// Should possibly look for these in the I/O worker instead of wasting the I/O or memory
	// But it seems somewhat rare so far.
	// Can insert duplicates this way. Could check for it, but it's generally pretty rare and shouldn't really cause problems.
	m_cacheEntryLookup.InsertEntry(&entry);

	m_lruList.PushBack(entry);

	return true;
}

void* MemoryAllocator::ExtractEntryFromCache_NoLock(CacheEntry* entry)
{
	RED_FATAL_ASSERT(entry);
	
	const Uint32 index = GetCacheEntryIndex(*entry);

	void* ptr = reinterpret_cast<void*>(entry->m_address);
	entry->m_address = 0; // sanity operation to ensure can't accidentally be used twice

	m_cacheEntryLookup.RemoveEntry(entry);
	entry->Remove();

	m_freeCacheEntries.Release(index);

	return ptr;
}

void MemoryAllocator::TouchLRU_NoLock(CacheEntry* entry)
{
	RED_FATAL_ASSERT(entry);

	const Uint32 index = GetCacheEntryIndex(*entry);

	entry->Remove();
	m_lruList.PushBack(*entry);
}

Bool MemoryAllocator::IsCacheEntryStillValid_NoLock(CacheEntry* entry)
{
	RED_FATAL_ASSERT(entry);

	const Uint32 index = GetCacheEntryIndex_NoValidityCheck(*entry);
	return m_freeCacheEntries.IsAlive(index);
}

Uint32 MemoryAllocator::GetCacheEntryIndex(const CacheEntry& entry) const
{
	Uint32 index = GetCacheEntryIndex_NoValidityCheck(entry);
	RED_FATAL_ASSERT(m_freeCacheEntries.IsAlive(index));
	return index;
}

Uint32 MemoryAllocator::GetCacheEntryIndex_NoValidityCheck(const CacheEntry& entry) const
{
	Uint32 index = (Uint32)(&entry - &m_cacheEntries[0]);
	return index;
}

// #tbd, proper pools and whether we want any direct gpu mem even
void MemoryAllocator::Initialize()
{
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
	
	// disable memory profiler hook otherwise it would count all allocations twice (once per PoolIO and second for m_wrappedAllocator)
	
	red::memory::SystemAllocator& systemAllocator = red::memory::AcquireSystemAllocator();

	m_range = systemAllocator.ReserveVirtualRange( c_ioAllocatorBudget, red::memory::Flags_CPU_Read_Write );
	red::memory::SystemBlock arena = systemAllocator.Commit( {m_range.start, c_ioAllocatorBudget}, red::memory::Flags_CPU_Read_Write );

	red::memory::StaticTLSFAllocatorParameter param = { reinterpret_cast< void* >( arena.address ), c_ioAllocatorBudget };

	m_wrappedAllocator.Initialize( param );
#endif
}

void MemoryAllocator::Shutdown()
{
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR

	TryEvictMemory_NoLock(UINT32_MAX);

	m_wrappedAllocator.Uninitialize();

	red::memory::SystemAllocator& systemAllocator = red::memory::AcquireSystemAllocator();
	systemAllocator.ReleaseVirtualRange( m_range );
	m_range = red::memory::NullVirtualRange();


#endif
}

void MemoryAllocator::CacheEntryLookup::InsertEntry(CacheEntry* entry)
{
	auto it = std::lower_bound(m_entries.Begin(), m_entries.End(), entry->m_cacheKey, &CompareFunc);
	m_entries.Insert(it, entry);
}

void MemoryAllocator::CacheEntryLookup::RemoveEntry(const CacheEntry* entry)
{
	auto it = std::lower_bound(m_entries.Begin(), m_entries.End(), entry->m_cacheKey, &CompareFunc);
	RED_FATAL_ASSERT(it != m_entries.End());
	for (auto end = m_entries.End(); it != end; ++it)
	{
		if (*it == entry)
		{
			m_entries.Remove(it);
			return;
		}
	}

	RED_FATAL("Failed to remove entry");
}

io::MemoryAllocator::CacheEntry* MemoryAllocator::CacheEntryLookup::FindOverlappingEntry(const CacheKey& key)
{
	auto it = std::lower_bound(m_entries.Begin(), m_entries.End(), key, &CompareFunc);
	if (it == m_entries.End())
	{
		return nullptr;
	}

	CacheEntry* foundEntry = *it;
	if (foundEntry->m_cacheKey.m_fileHandle != key.m_fileHandle)
	{
		return nullptr;
	}

	if (foundEntry->m_cacheKey.m_fileOffset > key.m_fileOffset)
	{
		return nullptr;
	}

	return foundEntry;
}

Bool MemoryAllocator::CacheEntryLookup::CompareFunc(CacheEntry* comp, const CacheKey& key)
{
	if (comp->m_cacheKey.m_fileHandle != key.m_fileHandle)
	{
		return comp->m_cacheKey.m_fileHandle < key.m_fileHandle;
	}

	Uint64 endPosComp = comp->m_cacheKey.m_fileOffset + comp->m_cacheKey.m_readSize;
	Uint64 endPosVal = key.m_fileOffset + key.m_readSize;
	if (endPosComp != endPosVal)
	{
		// Ascending
		return endPosComp < endPosVal;
	}

	// Descending
	return comp->m_cacheKey.m_readSize > key.m_readSize;
}

}
