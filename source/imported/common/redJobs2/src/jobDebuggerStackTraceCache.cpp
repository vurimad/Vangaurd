/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "jobDebuggerStackTraceCache.h"

namespace job { namespace prv {


namespace helper
{
	static void InitPathKey( const StackTraceCacheEntry* entry, const StackTraceCacheEntryPath* parentPath, StackTraceCacheEntryPath& outPathKey )
	{
		outPathKey.stackTraces.Clear();
		outPathKey.stackTraces.PushBack( entry );

		if ( parentPath )
		{
			for ( Uint32 i = 0; i < parentPath->stackTraces.Size(); ++i )
			{
				if ( outPathKey.stackTraces.Full() )
					break;

				RED_FATAL_ASSERT( parentPath->stackTraces[ i ] );
				outPathKey.stackTraces.PushBack( parentPath->stackTraces[ i ] );
			}
		}
	}

	struct StackWriter
	{
		StackWriter( char* buffer, Uint32 bufferLen )
			: m_buffer( buffer )
			, m_bufferLen( bufferLen )
		{
		}

		static Int32 Printf( void* callbackUserData, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
		{
			auto* pWriter = reinterpret_cast<StackWriter*>( callbackUserData );
			va_list args;
			va_start( args, format );
			Int32 ret = red::VSNPrintF( pWriter->m_buffer, pWriter->m_bufferLen, format, args );
			va_end( args );

			if ( ret > 0 )
			{
				RED_FATAL_ASSERT( (Int32)pWriter->m_bufferLen >= ret );
				pWriter->m_buffer += ret;
				pWriter->m_bufferLen -= ret;
			}
			return ret;
		}

	private:
		char* m_buffer;
		Uint32 m_bufferLen;
	};
}

const StackTraceCacheEntry* StackTraceCache::GetStackCachedTrace( const dbgutils::StackTrace& stackTrace ) const
{
	StackTraceCacheEntry* ret = nullptr;

	// Shared, try to find existing value
	{
		TScopedSharedLock lock( m_lock );
		auto ptr = m_stackTraces.FindPtr( stackTrace );
		if ( ptr )
		{
			ret = ptr->Get();
		}
	}

	// Exclusive, attempted update
	if ( !ret )
	{
		TScopedLock lock( m_lock );
		auto ptr = m_stackTraces.FindPtr( stackTrace );

		// check if some thread already beat us and updated it
		if ( ptr )
		{
			ret = ptr->Get();
		}
		else
		{
			red::UniquePtr< StackTraceCacheEntry > entry( RED_NEW( StackTraceCacheEntry ) );
			entry->stackTrace = stackTrace;

			// #tbd: cache modularization threadsafely when dumping
			auto stackTraceCopy = stackTrace;
			if ( stackTraceCopy.m_frameAddress[ 0 ].m_absoluteVirtualAddress != 0 && stackTraceCopy.m_frameAddress[ 0 ].m_moduleBaseAbsoluteVirtualAddress == 0 )
			{
				const Uint32 bufSize = 8192;
				entry->debugString.Resize( bufSize );
				helper::StackWriter writer( (char*)entry->debugString.Data(), bufSize );
				dbgutils::ModularizeStackBackTrace( stackTraceCopy );
				dbgutils::PrintJumpToLineFriendlyStackTraceLineByLine( stackTraceCopy, &helper::StackWriter::Printf, &writer );
			}
			auto res = m_stackTraces.Insert( std::move( entry ) );
			RED_FATAL_ASSERT( res.IsSuccessful() );
			ret = res.Iterator()->Get();
		}
	}

	RED_FATAL_ASSERT( ret );
	return ret;
}

const StackTraceCacheEntryPath* StackTraceCache::GetStackPathCachedTrace( const dbgutils::StackTrace& stackTrace, const StackTraceCacheEntryPath* parentPath ) const
{
	const StackTraceCacheEntryPath* ret = nullptr;

	const StackTraceCacheEntry* entry = GetStackCachedTrace( stackTrace );
	RED_FATAL_ASSERT( entry );

	StackTraceCacheEntryPath pathKey;
	helper::InitPathKey( entry, parentPath, pathKey );

	// Shared, try to find existing value
	{
		TScopedSharedLock lock( m_lock );
		auto ptr = m_stackTracePaths.FindPtr( pathKey );
		if ( ptr )
		{
			ret = ptr->Get();
		}
	}

	// Exclusive, attempted update
	if ( !ret )
	{
		TScopedLock lock( m_lock );
		auto ptr = m_stackTracePaths.FindPtr( pathKey );
		if ( ptr )
		{
			ret = ptr->Get();
		}
		else
		{
			red::UniquePtr< StackTraceCacheEntryPath > entryPath( RED_NEW( StackTraceCacheEntryPath ) );
			entryPath->stackTraces = pathKey.stackTraces;
			for ( const auto* it : entryPath->stackTraces )
			{
				entryPath->debugStringView.PushBack( it->debugString.AsChar() );
			}

			auto res = m_stackTracePaths.Insert( std::move( entryPath ) );
			RED_FATAL_ASSERT( res.IsSuccessful() );
			ret = res.Iterator()->Get();
		}
	}

	RED_FATAL_ASSERT( ret );
	return ret;
}

} }