/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOCommon.h"

#include "../../../common/redSystem/include/crt.h"

#include "redIOAsyncReadToken.h"
#include "redIOSystemFileWinAPI.h"
#include "redIOProfilerInterface.h"
#include "redIOSettings.h"

#include <utility>
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )

# ifdef RED_ASSERTS_ENABLED
// Speculatively GetLastError() in case assert checking changes it. Compare with != 0 so can check pointers, numbers, or bools without "performance" warnings.
// Technically relies on "expression" not doing some conversion that could clobber last error...
# define REDIO_WIN_CHECK( expression ) do{ Bool winret_ = ( (expression) != 0 ); DWORD lastError_ = ::GetLastError(); (void)lastError_; RED_FATAL_ASSERT( winret_, #expression "\n GetLastError() result: 0x%08X", lastError_ ); }while(false)
# else
# define REDIO_WIN_CHECK( expression ) RED_FATAL_ASSERT( expression )
#endif

namespace io
{

static thread_local Uint32 g_tls_lastError = ERROR_SUCCESS;

Uint32 LastError()
{
	return g_tls_lastError;
}

namespace win32
{

//////////////////////////////////////////////////////////////////////////
// SystemFile
//////////////////////////////////////////////////////////////////////////
SystemFile::SystemFile()
	: m_hFile( INVALID_HANDLE_VALUE )
{
}

SystemFile::SystemFile( SystemFile&& other )
	: m_hFile( other.m_hFile )
{
	other.m_hFile = INVALID_HANDLE_VALUE;
}

SystemFile& SystemFile::operator=( SystemFile&& other )
{
	if ( this != &other )
	{
		Close();
		std::swap( m_hFile, other.m_hFile );
	}
	return *this;
}

SystemFile::~SystemFile()
{
	if ( m_hFile!= INVALID_HANDLE_VALUE )
	{
		Bool bOk = ( ::CloseHandle(m_hFile) != 0);
		REDIO_WIN_CHECK( bOk );
	}
}

Bool SystemFile::Open( const char* path, Uint32 openFlags )
{
	RED_FATAL_ASSERT( m_hFile == INVALID_HANDLE_VALUE );
	if ( m_hFile != INVALID_HANDLE_VALUE )
	{
		g_tls_lastError = ERROR_SUCCESS; // #tbd: no system error anyway
		return false;
	}

	DWORD sysDesiredAccess = 0;
	DWORD sysShareMode = 0;
	DWORD sysCreationDisposition = OPEN_EXISTING;
	DWORD sysFlagsAndAttributes = FILE_FLAG_SEQUENTIAL_SCAN;

	if ( openFlags & eOpenFlag_Read )
	{
		sysDesiredAccess |= GENERIC_READ;
		sysShareMode |= FILE_SHARE_READ;
	}
	if ( openFlags & eOpenFlag_Write )
	{
		sysDesiredAccess |= GENERIC_WRITE;
		sysFlagsAndAttributes &= ~FILE_FLAG_SEQUENTIAL_SCAN;
	}
	if ( openFlags & eOpenFlag_Append )
	{
		sysDesiredAccess = FILE_APPEND_DATA; // no other attributes for atomic appends
	}

	// CREATE_ALWAYS truncates, OPEN_ALWAYS creates but does not truncate
	if ( openFlags & eOpenFlag_Create )
	{
		sysCreationDisposition = ( openFlags & eOpenFlag_Truncate ) ? CREATE_ALWAYS : OPEN_ALWAYS;
	}
	else if ( openFlags & eOpenFlag_Truncate )
	{
		sysCreationDisposition = TRUNCATE_EXISTING;
	}

#ifdef RED_USE_IOWORKER_WIN32
 	if ( openFlags & eOpenFlag_Async )
 	{
// 		sysFlagsAndAttributes &= ~FILE_FLAG_SEQUENTIAL_SCAN;
// 
// 		// If you want true async IO on Windows, you must remove buffering,
// 		// and ensue the consequences of properly aligned reads and sector read sizes
// 		sysFlagsAndAttributes |= (FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING);
		sysFlagsAndAttributes |= FILE_FLAG_OVERLAPPED;
		if ( openFlags & eOpenFlag_Unbuffered )
		{
			sysFlagsAndAttributes |= FILE_FLAG_NO_BUFFERING;
		}
 	}
#endif

	UniChar sysPath[ REDIO_MAX_PATH_LENGTH ];
	RED_VERIFY( red::EngineStringToFileSystemString( path, sysPath, REDIO_MAX_PATH_LENGTH ) );

#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileSyncIOOpenFileStart( path );
#endif
	m_hFile = ::CreateFileW( sysPath, sysDesiredAccess, sysShareMode, nullptr, sysCreationDisposition, sysFlagsAndAttributes, nullptr );
	const Uint32 lastError = ::GetLastError();
#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileSyncIOOpenFileEnd( static_cast<Uint32>(reinterpret_cast<uintptr_t>(m_hFile)) );
#endif
	
	//REDIO_WIN_CHECK( m_hFile != INVALID_HANDLE_VALUE );

	if ( m_hFile != INVALID_HANDLE_VALUE )
	{
		// Function doesn't exist for Xbox
#if defined( RED_USE_IOWORKER_WIN32 ) && defined( RED_PLATFORM_WINPC )
		if ( openFlags & eOpenFlag_Async )
		{
			// Ensure we only use OVERLAPPED
			SetFileCompletionNotificationModes( m_hFile, FILE_SKIP_SET_EVENT_ON_HANDLE );
		}
#endif
		return true;
	}

	struct ErrCodeMsg
	{
		static const char* AsChar(Uint32 lastError)
		{
			switch (lastError)
			{
			case ERROR_FILE_NOT_FOUND:
			case ERROR_PATH_NOT_FOUND:
				return " (No such file)";
			case ERROR_ACCESS_DENIED:
				return " (Access denied)";
			case ERROR_SHARING_VIOLATION:
				return " (Sharing violation)";
			case ERROR_HANDLE_DISK_FULL:
				return " (Disk full)";
			default:
				return "";
			}
		}
	 };

	g_tls_lastError = lastError;
	RED_LOG_ERROR( "Failed to open '%hs', openFlags=0x%08X, GetLastError=%u%hs", path, openFlags, lastError, ErrCodeMsg::AsChar(lastError) );

	return false;
}

Bool SystemFile::Close()
{
	if ( m_hFile!= INVALID_HANDLE_VALUE )
	{
#ifdef RED_PROFILE_FILE_SYSTEM
		IIOProfiler::Get()->ProfileSyncIOCloseFileStart(static_cast<Uint32>(reinterpret_cast<uintptr_t>(m_hFile)));
#endif
		const Bool bOk = ( ::CloseHandle(m_hFile) != 0 );
		REDIO_WIN_CHECK( bOk );
		if (!bOk)
		{
			g_tls_lastError = ::GetLastError();
		}
#ifdef RED_PROFILE_FILE_SYSTEM
		IIOProfiler::Get()->ProfileSyncIOCloseFileEnd();
#endif

		m_hFile = INVALID_HANDLE_VALUE;
		return bOk;
	}

	g_tls_lastError = ERROR_SUCCESS; //#tbd: no system error
	return false;
}

Bool SystemFile::Read( void* dest, Uint32 length, Uint32& outNumberOfBytesRead )
{
	DWORD dwNumberOfBytesRead = 0;
	Bool bOK = ::ReadFile( m_hFile, dest, length, &dwNumberOfBytesRead, nullptr ) != FALSE;
	RED_FATAL_ASSERT( bOK, "SystemFile::Read failed: dest=%p, length=%u, lastError=0x%08X", dest, length, ::GetLastError());

	if (bOK)
	{
		outNumberOfBytesRead = static_cast< Uint32 >( dwNumberOfBytesRead );
	}
	else
	{
		g_tls_lastError = ::GetLastError();
	}

	return bOK;
}

Bool SystemFile::Write( const void* src, Uint32 length, Uint32& outNumberOfBytesWritten )
{
	outNumberOfBytesWritten = 0;
	DWORD dwNumberOfBytesWritten = 0;
	Bool bOK = ::WriteFile( m_hFile, src, length, &dwNumberOfBytesWritten, nullptr ) != FALSE;
	REDIO_WIN_CHECK( bOK );

	if ( bOK )
	{
		outNumberOfBytesWritten = dwNumberOfBytesWritten;
	}
	else
	{
		g_tls_lastError = ::GetLastError();
	}

	return bOK;
}

Bool SystemFile::Seek( Int64 offset, ESeekOrigin seekOrigin )
{
	LARGE_INTEGER distanceToMove;
	distanceToMove.QuadPart = offset;

	DWORD moveMethod = FILE_BEGIN;
	switch( seekOrigin )
	{
		case eSeekOrigin_Set:
			moveMethod = FILE_BEGIN;
			break;
		case eSeekOrigin_Current:
			moveMethod = FILE_CURRENT;
			break;
		case eSeekOrigin_End:
			moveMethod = FILE_END;
			break;
		default:
			RED_FATAL( "Unexpected seekOrigin %u", (Uint32)seekOrigin );
			break;
	}
	
	const Bool bOK = ::SetFilePointerEx( m_hFile, distanceToMove, nullptr, moveMethod ) != FALSE;
	REDIO_WIN_CHECK( bOK );
	if (!bOK)
	{
		g_tls_lastError = ::GetLastError();
	}

	return bOK;
}

Int64 SystemFile::Tell() const
{
	const LARGE_INTEGER distanceToMove = { 0 };
	LARGE_INTEGER offset = { 0 };
	const Bool bOK = ::SetFilePointerEx( m_hFile, distanceToMove, &offset, FILE_CURRENT ) != FALSE;
	REDIO_WIN_CHECK( bOK );

	if (!bOK)
	{
		g_tls_lastError = ::GetLastError();
	}

	return bOK ? offset.QuadPart : -1;
}

Bool SystemFile::Truncate( Uint64 totalFileSize )
{
	LARGE_INTEGER position;
	position.QuadPart = static_cast<LONGLONG>( totalFileSize );
	if ( position.QuadPart < 0 )
	{
		g_tls_lastError = ERROR_SUCCESS;
		return false;
	}

	if ( ::SetFilePointerEx( m_hFile, position, nullptr, FILE_BEGIN ) == 0 )
	{
		g_tls_lastError = ::GetLastError();
		return false;
	}

	if ( ::SetEndOfFile( m_hFile ) == 0 )
	{
		g_tls_lastError = ::GetLastError();
		return false;
	}

	return true;
}

Bool SystemFile::Flush()
{
	const Bool bOK = ::FlushFileBuffers( m_hFile ) != FALSE;
	REDIO_WIN_CHECK( bOK );

	if (!bOK)
	{
		g_tls_lastError = ::GetLastError();
	}

	return bOK;
}

Uint64 SystemFile::GetFileSize() const
{
	LARGE_INTEGER fileSize;
	const Bool bOK = ::GetFileSizeEx( m_hFile, &fileSize ) != FALSE;
	if ( bOK )
	{
		return static_cast< Uint64 >( fileSize.QuadPart );
	}
	else
	{
		g_tls_lastError = ::GetLastError();
	}

	return 0;
}

Uint64 SystemFile::GetFileTimestamp() const
{
	FILETIME modifiedTime;
	if ( ::GetFileTime( m_hFile, nullptr, nullptr, &modifiedTime ) != 0 )
	{
		// 100 nanosecond intervals since 00:00:00 1 January 1601 UTC
		return ( static_cast<Uint64>( modifiedTime.dwHighDateTime ) << 32 ) | modifiedTime.dwLowDateTime;
	}

	g_tls_lastError = GetLastError();
	return 0ull;
}

Uint32 SystemFile::GetFileID() const
{
	return static_cast<Uint32>(reinterpret_cast<uintptr_t>(m_hFile));
}

 
// TODO: Actual async cancelation instead of canceling scheduling chunks. Besides not being part of the supported Durango API,
// the comment in the CancelIOEx function heavily suggests the possibility of the IOCP *not* receiving a completion packet, in which case
// we have no way of safely knowing when to free the overlapped structure. You can't just check with ::GetOverlappedResult because if
// the I/O *wasn't* actually canceled before it completed, then we will get a completion port notification, at which point we'll corrupt memory having
// already freed the overlapped structure.
// Perhaps the way to go is to use unbuffered I/O, which should not produce any synchronous results. Or just ditch IOCP for I/O over this one apparent issue...
/* CancelIoEx function:
		If the file handle is associated with a completion port, an I/O completion packet is not queued to the port if a synchronous operation is
		successfully canceled. For asynchronous operations still pending, the cancel operation will queue an I/O completion packet.
*/

// Win32 I/O Cancellation Support in Windows Vista
// http://msdn.microsoft.com/en-us/library/aa480216.aspx	 

// void SystemFile::CancelAsync( SSysOverlappedEx& /*overlappexEx*/ )
// {
// #if 0//ndef RED_PLATFORM_DURANGO
// 	Bool bOK = ::CancelIoEx( m_hFile, &overlappexEx ) != FALSE;
// 	DWORD dwError = ::GetLastError();
// 
// 	REDIO_WIN_CHECK( bOK || dwError == ERROR_NOT_FOUND );
// #endif
// }
// 
// void SystemFile::CancelAsync()
// {
// #if 0//ndef RED_PLATFORM_DURANGO
// 	Bool bOK = ::CancelIoEx( m_hFile, nullptr ) != FALSE; // Use instead of CancelIo because this cancels all in the process not just the calling thread
// 	DWORD dwError = ::GetLastError();
// 	REDIO_WIN_CHECK( bOK || dwError == ERROR_NOT_FOUND );
// #endif
// }

} // win32
} // io
#else
RED_NO_EMPTY_FILE();
#endif // #if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
