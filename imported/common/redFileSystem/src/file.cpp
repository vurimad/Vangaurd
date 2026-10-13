/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "file.h"
#include "fileVersionList.h"

IFile::IFile( Uint32 flags )
	: m_flags( flags )
	, m_version( VER_CURRENT )
	, m_mapper( nullptr ) 
	, m_pool( nullptr )
{
#ifdef RED_COMPILER_MSC
	RED_ASSERT( 1 == __popcnt( flags & (FF_FileBased | FF_MemoryBased | FF_NullBased) ), "Invalid flags configuration. Must be one of: FF_FileBased, FF_MemoryBased or FF_NullBased" );
#else
	RED_ASSERT( 1 == __builtin_popcount( flags & (FF_FileBased | FF_MemoryBased | FF_NullBased) ), "Invalid flags configuration. Must be one of: FF_FileBased, FF_MemoryBased or FF_NullBased" );
#endif
}

IFile::~IFile()
{}

const char* IFile::GetFileNameForDebug() const
{
	return ("<unknown file>");
}

STATIC_CHECK_USE_DECL
void IFile::HandleIOError( const AnsiChar* txt, ... )
{
	// report only first IO error
	if ( !(m_flags & FF_ErrorOccured) )
	{
		m_flags |= FF_ErrorOccured;

		// format message
		{
			AnsiChar buf[ 1024 ];
			va_list args;

			va_start( args, txt );
			red::VSNPrintF( buf, RED_ARRAY_COUNT(buf), txt, args );
			va_end( args );

			RED_LOG_ERROR( "Core: FileIO error at '%hs': %hs", GetFileNameForDebug(), buf );
		}
	}
}

void IFile::ClearError()
{
	m_flags &= ~FF_ErrorOccured;
}

void IFile::SetInternalMemoryPool( const red::memory::Pool & pool )
{
	m_pool = &pool;
}

const red::memory::Pool & IFile::GetInternalMemoryPool() const
{
	return m_pool ? *m_pool : red::PoolEngine::GetInstance();
}
