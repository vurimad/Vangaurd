/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOSystemFileOrbisAPI.h"
#include "redIOAsyncReadToken.h"

#if defined( RED_PLATFORM_ORBIS )

#include <locale>
#include "../../redSystem/include/threads.h"
#include <utility>

namespace io
{

namespace orbis
{

static thread_local Uint32 g_tls_lastError = SCE_OK;

Uint32 LastError()
{
	return g_tls_lastError;
}

//////////////////////////////////////////////////////////////////////////
// SystemFile
//////////////////////////////////////////////////////////////////////////
SystemFile::SystemFile()
	: m_sceFileHandle( -1 )
{
}

SystemFile::SystemFile( SystemFile&& other )
	: m_sceFileHandle( other.m_sceFileHandle )
{
	other.m_sceFileHandle = -1;
}

SystemFile& SystemFile::operator=( SystemFile&& other )
{
	if ( this != &other )
	{
		Close();
		std::swap( m_sceFileHandle, other.m_sceFileHandle );
	}
	return *this;
}

SystemFile::~SystemFile()
{
	if ( m_sceFileHandle != -1 )
	{
		Close();
	}
}

Bool SystemFile::Open( const char* path, Uint32 openFlags )
{
	// Ignores eOpenFlag_Async

	RED_FATAL_ASSERT( m_sceFileHandle == -1 );

	SceInt32 sceKernelOpenFlags = 0;

	if ((openFlags & (eOpenFlag_Read | eOpenFlag_Write)) == (eOpenFlag_Read | eOpenFlag_Write))
	{
		sceKernelOpenFlags |= SCE_KERNEL_O_RDWR;
	}
	else
	{
		if ( openFlags & eOpenFlag_Read )
		{
			sceKernelOpenFlags |= SCE_KERNEL_O_RDONLY;
		}
		if ( openFlags & eOpenFlag_Write )
		{
			sceKernelOpenFlags |= SCE_KERNEL_O_WRONLY;
		}
	}

	if ( openFlags & eOpenFlag_Append )
	{
		sceKernelOpenFlags |= SCE_KERNEL_O_APPEND;
	}
	if ( openFlags & eOpenFlag_Create )
	{
		sceKernelOpenFlags |= SCE_KERNEL_O_CREAT;
	}

	// #tbd: check if SCE_KERNEL_O_RDONLY is set and fail?
	if ( openFlags & eOpenFlag_Truncate )
	{
		sceKernelOpenFlags |= SCE_KERNEL_O_TRUNC;
	}

	m_sceFileHandle = ::sceKernelOpen(path, sceKernelOpenFlags, SCE_KERNEL_S_IRWU );
	
	if (m_sceFileHandle < 0 )
	{
		g_tls_lastError = m_sceFileHandle;

		RED_LOG_ERROR( "RedIO: sceKernelOpen failed to open '%hs', errorCode=0x%08X, openFlags=0x%08X", path, m_sceFileHandle, sceKernelOpenFlags);
		m_sceFileHandle = -1;
		return false;
	}

	return true;
}

Bool SystemFile::Close()
{
	if ( m_sceFileHandle != -1 )
	{
		const Int32 err = ::sceKernelClose(m_sceFileHandle);
		m_sceFileHandle = -1;
		if (err != SCE_OK)
		{
			g_tls_lastError = err;
			return false;
		}
	}

	return true;
}

Bool SystemFile::Read( void* dest, Uint32 length, Uint32& outNumberOfBytesRead )
{
	outNumberOfBytesRead = 0;
	
	const ssize_t sceReadSize = ::sceKernelRead( m_sceFileHandle, dest, length );
	if ( sceReadSize < 0 )
	{	
		g_tls_lastError = sceReadSize;

		RED_LOG_ERROR("RedIO: sceKernelRead failed for fd %d. Error code 0x%08X", m_sceFileHandle, static_cast<Int32>(sceReadSize));
		outNumberOfBytesRead = 0;
		return false;
	}

	outNumberOfBytesRead = static_cast< Uint32 >( sceReadSize );
	
	return true;
}

Bool SystemFile::Write( const void* src, Uint32 length, Uint32& outNumberOfBytesWritten )
{
	outNumberOfBytesWritten = 0;

	const ssize_t sceWriteSize = ::sceKernelWrite( m_sceFileHandle, src, length );
	if ( sceWriteSize < 0 )
	{
		g_tls_lastError = sceWriteSize;

		RED_LOG_ERROR("RedIO: sceKernelWrite failed for fd %d. Error code 0x%08X", m_sceFileHandle, static_cast<Int32>(sceWriteSize));
		outNumberOfBytesWritten = 0;
		return false;
	}

	outNumberOfBytesWritten = static_cast< Uint32 >( sceWriteSize );
	
	return true;
}

Bool SystemFile::Seek( Int64 offset, ESeekOrigin seekOrigin )
{
	static_assert( eSeekOrigin_Set == SCE_KERNEL_SEEK_SET, "Enum mismatch" );
	static_assert( eSeekOrigin_Current == SCE_KERNEL_SEEK_CUR, "Enum mismatch" );
	static_assert( eSeekOrigin_End == SCE_KERNEL_SEEK_END, "Enum mismatch" );

	const off_t newPos = ::sceKernelLseek( m_sceFileHandle, offset, seekOrigin );

	if (newPos >= 0)
	{
		return true;
	}

	g_tls_lastError = newPos;
	RED_LOG_ERROR("RedIO: sceKernelLseek failed for fd %d. Error code 0x%08X", m_sceFileHandle, static_cast<Int32>(newPos));
	return false;
}

Int64 SystemFile::Tell() const
{
	const off_t pos = ::sceKernelLseek(m_sceFileHandle, 0, SCE_KERNEL_SEEK_CUR );
	if (pos >= 0)
	{
		return pos;
	}

	g_tls_lastError = pos;
	RED_LOG_ERROR("RedIO: sceKernelLseek failed for fd %d. Error code 0x%08X", m_sceFileHandle, static_cast<Int32>(pos));
	return -1;
}

Bool SystemFile::Truncate( Uint64 totalFileSize )
{
	const auto size = static_cast<off_t>( totalFileSize );
	if ( size < 0 )
	{
		g_tls_lastError = 0; // #tbd: no system error anyway
		return false;
	}
	const Int32 err = ::sceKernelFtruncate( m_sceFileHandle, size );
	if (err == SCE_OK)
	{
		return true;
	}

	g_tls_lastError = err;
	RED_LOG_ERROR("RedIO: sceKernelFtruncate failed for fd %d. Error code 0x%08X", m_sceFileHandle, err);
	return false;
}

Bool SystemFile::IsValid() const
{
	return m_sceFileHandle >= 0;
}

Bool SystemFile::Flush()
{
	const Int32 err = ::sceKernelFsync( m_sceFileHandle );
	if (err == SCE_OK)
	{
		return true;
	}
	
	g_tls_lastError = err;
	RED_LOG_ERROR("RedIO: sceKernelFsync failed for fd %d. Error code 0x%08X", m_sceFileHandle, err);
	return false;
}

Uint64 SystemFile::GetFileSize() const
{
	SceKernelStat stat;
	const Int32 err = ::sceKernelFstat(m_sceFileHandle, &stat);
	if (err != SCE_OK)
	{
		g_tls_lastError = err;
		RED_LOG_ERROR("RedIO: sceKernelFstat failed for fd %d. Error code 0x%08X", m_sceFileHandle, err);
		return 0;
	}

	return stat.st_size;
}

Uint64 SystemFile::GetFileTimestamp() const
{
	SceKernelStat stat;
	const Int32 err = ::sceKernelFstat(m_sceFileHandle, &stat);
	if (err != SCE_OK)
	{
		g_tls_lastError = err;
		RED_LOG_ERROR("RedIO: sceKernelFstat failed for fd %d. Error code 0x%08X", m_sceFileHandle, err);
		return 0;
	}

	// Orbis uses the Unix time epoch but it stores
	// nanoseconds since 00:00:00 1 Jan 1970 UTC
	// Convert it to Windows timestamp which is counted in
	// 100 nanosecond intervals since 00:00:00 1 Jan 1601 UTC
	const Uint64 epochDifference = 116444736000000000ull;
	return (stat.st_mtime / 100) - epochDifference;
}

Uint32 SystemFile::GetFileID() const
{
	return static_cast< Uint32 >( m_sceFileHandle );
}

} // orbis

} // io

#else
RED_NO_EMPTY_FILE();
#endif // #if defined( RED_PLATFORM_ORBIS )