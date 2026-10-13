/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/dbgUtils.h"
#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../redContainers/include/staticArray.h"
#include "jobMemoryPools.h"
#include "jobStackTrace.h"

namespace job { namespace prv {

struct StackTracePredicate
{
	static RED_INLINE Uint32 GetHash( const dbgutils::StackTrace& stackTrace )
	{
		// Calculate based only on absoluteVirtualAddress, so doesn't matter if has been "modularized" yet.
		Uint64 hash = 0;
		for ( Uint32 i = 0, numFrames = stackTrace.m_numFrameAddresses; i < numFrames; ++i )
		{
			// #todo: better hash, but don't include modularized addresses
			hash += stackTrace.m_frameAddress[ i ].m_absoluteVirtualAddress;
		}
		hash &= ( std::numeric_limits<Uint32>::max() - 1 );
		return static_cast<Uint32>( hash );
	}

	static RED_INLINE Uint32 GetHash( const red::UniquePtr< StackTraceCacheEntry >& wrapper )
	{
		return GetHash( wrapper->stackTrace );
	}

	static RED_INLINE Bool Equal( const dbgutils::StackTrace& a, const dbgutils::StackTrace& b )
	{
		if ( a.m_numFrameAddresses != b.m_numFrameAddresses )
		{
			return false;
		}

		const Uint32 numFrames = a.m_numFrameAddresses;
		for ( Uint32 i = 0; i < numFrames; ++i )
		{
			if ( a.m_frameAddress[ i ].m_absoluteVirtualAddress != b.m_frameAddress[ i ].m_absoluteVirtualAddress )
			{
				return false;
			}
		}

		return true;
	}

	static RED_INLINE Bool Equal( const red::UniquePtr< StackTraceCacheEntry >& a, const red::UniquePtr< StackTraceCacheEntry >& b )
	{
		return Equal( a->stackTrace, b->stackTrace );
	}

	static RED_INLINE Bool Equal( const StackTraceCacheEntry& a, const StackTraceCacheEntry& b )
	{
		return Equal( a.stackTrace, b.stackTrace );
	}

	static RED_INLINE Bool Equal( const red::UniquePtr< StackTraceCacheEntry >& a, const dbgutils::StackTrace& b )
	{
		return Equal( a->stackTrace, b );
	}

	static RED_INLINE Bool Equal( const dbgutils::StackTrace& a, const red::UniquePtr< StackTraceCacheEntry >& b )
	{
		return Equal( a, b->stackTrace );
	}
};

struct StackTracePathPredicate
{
	static RED_INLINE Uint32 GetHash( const StackTraceCacheEntryPath& path )
	{
		Uint64 hash = 0;
		for ( const auto* cachePtr : path.stackTraces )
		{
			hash ^= reinterpret_cast<Uint64>( cachePtr );
		}
		return static_cast<Uint32>( hash );
	}

	static RED_INLINE Uint32 GetHash( const red::UniquePtr< StackTraceCacheEntryPath >& path )
	{
		return GetHash( *path );
	}

	static RED_INLINE Bool Equal( const red::UniquePtr< StackTraceCacheEntryPath >& a, const StackTraceCacheEntryPath& b )
	{
		return Equal( *a, b );
	}

	static RED_INLINE Bool Equal( const StackTraceCacheEntryPath& a, const StackTraceCacheEntryPath& b )
	{
		if ( a.stackTraces.Size() != b.stackTraces.Size() )
		{
			return false;
		}

		const Uint32 numStackTraces = a.stackTraces.Size();
		for ( Uint32 i = 0; i < numStackTraces; ++i )
		{
			const auto* cachePtrA = a.stackTraces[ i ];
			const auto* cachePtrB = b.stackTraces[ i ];
			if ( cachePtrA != cachePtrB )
			{
				return false;
			}
		}

		return true;
	}

	static RED_INLINE Bool Equal( const red::UniquePtr< StackTraceCacheEntryPath >& a, const red::UniquePtr< StackTraceCacheEntryPath >& b )
	{
		return Equal( *a, *b );
	}
};

class StackTraceCache : red::NonCopyable
{
public:
	const StackTraceCacheEntry* GetStackCachedTrace( const dbgutils::StackTrace& stackTrace ) const;
	const StackTraceCacheEntryPath* GetStackPathCachedTrace( const dbgutils::StackTrace& stackTrace, const StackTraceCacheEntryPath* parentPath ) const;

private:
	typedef red::RWSpinLock TLock;
	typedef red::ScopedLock< TLock > TScopedLock;
	typedef red::ScopedSharedLock< TLock > TScopedSharedLock;

	typedef red::HashSet< red::UniquePtr< StackTraceCacheEntry >, red::HashPolicy< StackTracePredicate, StackTracePredicate > > TStackTraceSet;
	typedef red::HashSet< red::UniquePtr< StackTraceCacheEntryPath >, red::HashPolicy< StackTracePathPredicate, StackTracePathPredicate > > TStackTracePathSet;
	typedef red::HashMap< const StackTraceCacheEntry*, String > TCallstackStringMap;
	mutable TStackTraceSet m_stackTraces{ PoolJobs2Debug{} };
	mutable TStackTracePathSet m_stackTracePaths{ PoolJobs2Debug{} };
	mutable TCallstackStringMap m_stackStrings{ PoolJobs2Debug{} };
	mutable TLock m_lock;
};

} }
