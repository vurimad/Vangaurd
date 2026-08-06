/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOFile.h"
#include "redIOAsyncReadToken.h"

#include "../../redSystem/include/redThreadsAtomic.h"
#include "redIOProfilerInterface.h"

namespace io
{

//////////////////////////////////////////////////////////////////////////
// NativeFileHandle
//////////////////////////////////////////////////////////////////////////
NativeFileHandle::NativeFileHandle()
	: m_async( false )
{
}

NativeFileHandle::NativeFileHandle( NativeFileHandle&& other )
	: m_file( std::move( other.m_file ) )
	, m_async( other.m_async )
{
}

NativeFileHandle::~NativeFileHandle()
{
	Close();
}

NativeFileHandle& NativeFileHandle::operator=( NativeFileHandle&& other )
{
	if ( this != &other )
	{
		m_file = std::move( other.m_file );
		m_async = other.m_async;
	}
	return *this;
}

Bool NativeFileHandle::Open( const char* path, Uint32 openFlags )
{
	m_async = (openFlags & eOpenFlag_Async) != 0;
#ifdef RED_PROFILE_FILE_SYSTEM
	if ( m_async )
		IIOProfiler::Get()->ProfileAsyncIOOpenFileStart( path );
	else
		IIOProfiler::Get()->ProfileSyncIOOpenFileStart( path );
#endif

	const Bool ret = m_file.Open( path, openFlags );

#ifdef RED_PROFILE_FILE_SYSTEM
	if ( m_async )
		IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( m_file.GetFileID() );
	else
		IIOProfiler::Get()->ProfileSyncIOOpenFileEnd( m_file.GetFileID() );
#endif
	return ret;
}

Bool NativeFileHandle::Close()
{
#ifdef RED_PROFILE_FILE_SYSTEM
	if ( m_async )
		IIOProfiler::Get()->ProfileAsyncIOCloseFileStart( m_file.GetFileID() );
	else
		IIOProfiler::Get()->ProfileSyncIOCloseFileStart( m_file.GetFileID() );
#endif

	const Bool ret = m_file.Close();

#ifdef RED_PROFILE_FILE_SYSTEM
	if ( m_async )
		IIOProfiler::Get()->ProfileAsyncIOCloseFileEnd();
	else
		IIOProfiler::Get()->ProfileSyncIOCloseFileEnd();
#endif
	return ret;
}

Bool NativeFileHandle::Read( void* dest, Uint32 length, Uint32& /*[out]*/ outNumberOfBytesRead )
{
#ifdef RED_PROFILE_FILE_SYSTEM
	if ( !m_async )
		IIOProfiler::Get()->ProfileSyncIOReadStart( m_file.GetFileID(), length );
#endif

	Bool ret = m_file.Read( dest, length, outNumberOfBytesRead );

#ifdef RED_PROFILE_FILE_SYSTEM
	if ( !m_async )
		IIOProfiler::Get()->ProfileSyncIOReadEnd();
#endif

	return ret;
}

Bool NativeFileHandle::Write( const void* src, Uint32 length, Uint32& /*[out]*/ outNumberOfBytesWritten )
{
	return m_file.Write( src, length, outNumberOfBytesWritten );
}

Bool NativeFileHandle::Seek( Int64 offset, ESeekOrigin seekOrigin )
{
#ifdef RED_PROFILE_FILE_SYSTEM
	if ( !m_async && seekOrigin == eSeekOrigin_Set )
		IIOProfiler::Get()->ProfileSyncIOSeekStart( m_file.GetFileID(), (Uint64)offset );
#endif
	const Bool ret = m_file.Seek( offset, seekOrigin );

#ifdef RED_PROFILE_FILE_SYSTEM
	if ( !m_async && seekOrigin == eSeekOrigin_Set )
		IIOProfiler::Get()->ProfileSyncIOSeekEnd();
#endif

	return ret;
}

Int64 NativeFileHandle::Tell() const
{
	return m_file.Tell();
}

Bool NativeFileHandle::Truncate( Uint64 totalFileSize )
{
	return m_file.Truncate( totalFileSize );
}

Bool NativeFileHandle::IsValid() const
{
	return m_file.IsValid();
}

Bool NativeFileHandle::Flush()
{
	return m_file.Flush();
}

Uint64 NativeFileHandle::GetFileSize() const
{
	return m_file.GetFileSize();
}

Uint64 NativeFileHandle::GetFileTimestamp() const
{
	return m_file.GetFileTimestamp();
}

} // io
