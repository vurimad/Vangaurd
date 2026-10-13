/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#if defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
#include "../../redMemory/include/defaultAllocator.h"
#else
#include "../../redMemory/include/tlsfAllocator.h"
#endif

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redSystem/include/redThreadsThread.h"

#include "redIOStats.h"
#include "../redContainers/include/intrusiveList.h"
#include "../redContainers/include/idAllocator.h"

namespace io
{

class MemoryAllocator
{
	RED_USE_MEMORY_POOL(red::PoolEngine);

private:
	enum
	{
		PageSize = 4096
	};

public:
	RED_MEMORY_DECLARE_ALLOCATOR(MemoryAllocator, RuntimeIOMemoryMetrics, 16);

	MemoryAllocator();
	~MemoryAllocator();
	void GetMemoryRuntimeMetrics(RuntimeIOMemoryMetrics& outMetrics);

	struct RequestTokenKey
	{
		TFileHandle m_fileHandle{ INVALID_FILE_HANDLE };
		Int64 m_fileOffset{ -1 };
	};

	struct RequestToken
	{
		const char* m_debugFileName{ "" };
		Uint32 numBytesForIO{ 0 };
		Uint32 numBytesForDecompressor{ 0 };
		Bool mustAllocCachedIOResult{ false };
		RequestTokenKey cacheKey;
	};

	struct AllocResult
	{
		red::UniqueBuffer m_bufferForIO;
		red::UniqueBuffer m_bufferForDecompressor;
		Bool m_ioMemoryHasCachedResults{ false }; // whether I/O is actually necessary, or if the buffer already contains cached I/O read results
	};

	Bool TryAlloc(const RequestToken& request, AllocResult& outResult);

	red::memory::Block Reallocate(red::memory::Block& block, Uint32 size)
	{
		RED_FATAL_ASSERT(false, "Reallocate not supported!");
		return red::memory::NullBlock();
	}

	red::memory::Block ReallocateAligned(red::memory::Block& block, Uint32 size, Uint32 align)
	{
		RED_FATAL_ASSERT(false, "ReallocateAligned not supported!");
		return red::memory::NullBlock();
	}

	Uint64 GetBlockSize(Uint64 block) const
	{
		return m_wrappedAllocator.GetBlockSize(block);
	}

	void Free(red::memory::Block& block);

	void InvalidateCacheForFileHandle(TFileHandle handle);

private:

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
	using TAllocator = red::memory::StaticTLSFAllocator;
#else
	using TAllocator = red::memory::DebugAllocator;
#endif

	void Initialize();
	void Shutdown();

	Bool Alloc_NoLock(const RequestToken& req, AllocResult& outResult);
	Bool Alloc_EntireRequest_NoLock(const RequestToken& req, TAllocator& allocator, Uint32 align, void*& outPtrForIO, void*& outPtrForDecompression);

	void UpdateAllocStats_NoLock(const void* ptrForIO, const void* ptrForDecompression);

	void UpdateFreeStats_NoLock(red::memory::Block& block);

	struct CacheKey
	{
		TFileHandle m_fileHandle;
		Int64 m_fileOffset;
		Uint32 m_readSize;

		CacheKey()
			: m_fileHandle(INVALID_FILE_HANDLE)
			, m_fileOffset(-1)
			, m_readSize(0)
		{}

		explicit CacheKey(const RequestToken& req)
			: m_fileHandle(req.cacheKey.m_fileHandle)
			, m_fileOffset(req.cacheKey.m_fileOffset)
			, m_readSize(req.numBytesForIO)
		{}

		Bool IsValid() const
		{
			return m_fileHandle != INVALID_FILE_HANDLE && m_fileOffset >= 0 && m_readSize > 0;
		}

		Bool operator==(const CacheKey& rhs) const
		{
			return
				m_fileHandle == rhs.m_fileHandle &&
				m_fileOffset == rhs.m_fileOffset &&
				m_readSize == rhs.m_readSize;
		}
	};

	struct PendingCacheEntry
	{
		CacheKey m_cacheKey;
		red::memory::u64 m_address;
	};

	struct CacheEntry : public red::IntrusiveListNode
	{
		CacheKey m_cacheKey;
		red::memory::u64 m_address;
	};

	Bool TryEvictMemory_NoLock(Uint32 bytesToEvict);
	Bool TryCacheFreedMemory_NoLock(red::memory::Block& block);
	void* ExtractEntryFromCache_NoLock(CacheEntry* entry);
	void TouchLRU_NoLock(CacheEntry* entry);
	
	// NOTE: doesn't protected against "ABA"-style synchronization issues.
	// Should only be used under the lock where you know you haven't cached a new entry yet
	Bool IsCacheEntryStillValid_NoLock(CacheEntry* entry);
	
	void TrackPendingIO_NoLock(const CacheKey& cacheKey, const void* ptrForIO);

	Uint32 GetCacheEntryIndex(const CacheEntry& entry) const;

	Uint32 GetCacheEntryIndex_NoValidityCheck(const CacheEntry& entry) const;

	red::HashMap<red::memory::u64, PendingCacheEntry> m_pendingIOMemoryRequests{ red::PoolEngine() };
	
	// video def shouldn't cache...
	// and need to remove video
	class CacheEntryLookup : red::NonCopyable
	{
	public:
		void InsertEntry(CacheEntry* entry);

		// Remove by pointer, technically could have the same memory cached multiple times...
		void RemoveEntry(const CacheEntry* entry);

		CacheEntry* FindOverlappingEntry(const CacheKey& key);

	private:
		
		// First find the entry that AT LEAST contains our end point (thanks to std::lower_bound)
		//
		// NOTE: The following breaks down and does NOT really apply for MERGED ASYNC OPS, but for now we'll just try to not merge them if the I/O cached memory here is available.
		// It could fragment the cache, but possibly better than nothing.
		//
		// Then we know that the entry should generally at least contain our starting point as well, because of how we're reading files.
		// E.g., even if reading only part of a file (say a DDB), then there's generally no reason to include our end position but not the start position.
		// No reason unless we're doing something weird e.g. like reading a header, then reading the rest, and then doing another read where we include the header+rest together.
		// It doesn't matter if aligning our reads to the sector size and length, because even then if we include our end point then something should have also read through our start point.
		//
		// With that in mind, the secondary ordering by read size can be useful so the first match has the LONGEST read length. Intuitively it'd be more likely to also contain our start point,
		// but as discussed above, any match that contains our end point already likely contains our starting point anyway.
		// So it's more about keeping longer matches alive longer in the LRU, since they might then match against other requests. Or that's the hope anyway.
		static Bool CompareFunc(CacheEntry* comp, const CacheKey& key);

		red::DynArray<CacheEntry*> m_entries{ red::PoolEngine() };
	};

	CacheEntryLookup m_cacheEntryLookup;

	// define DEBUG_CHECK_OUT_OF_CACHE_ENTRIES to see if we hit the limit (will debug break)
	static const Uint32 c_numCacheEntries = 4096;// 8192
	CacheEntry m_cacheEntries[c_numCacheEntries];
	IDAllocator< c_numCacheEntries > m_freeCacheEntries;
	red::IntrusiveList<CacheEntry> m_lruList;

	TAllocator m_wrappedAllocator;

	mutable red::RWSpinLock m_lock;
	mutable Bool m_recursionCheck{ false };
	void* m_arena{ nullptr };
	Int64 m_numBytesAlloc{ 0 };
	Int64 m_numAllocsInUse{ 0 };
	Int64 m_peakTotalBytesAlloc{ 0 };
	Int64 m_maxBytesAlloc{ 0 };

	// #tbd: ugly, but also a bit less ugly than bubbling it up or TLS vars
	struct AllocFlags
	{
		Bool m_evictedCacheMemoryDuringAllocAttempt{ false };
		Bool m_ioMemoryHasCachedResults{ false };
	};

	AllocFlags m_allocFlags;

	red::memory::VirtualRange m_range;
};

extern MemoryAllocator& GetMemoryAllocator();
void InitializeIOMemoryAllocator();
}
