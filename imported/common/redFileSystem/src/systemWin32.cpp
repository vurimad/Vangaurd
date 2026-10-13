/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"

#include "../../redCore/include/ioProfiler.h"

#ifdef RED_PROFILE_FILE_SYSTEM
#	define RED_PROFILE_OPEN_START( pathname ) IIOProfiler::Get()->ProfileSyncIOOpenFileStart( pathname )
#	define RED_PROFILE_OPEN_END( handle ) IIOProfiler::Get()->ProfileSyncIOOpenFileEnd( static_cast<Uint32>( reinterpret_cast<uintptr_t>( handle ) ) )
#	define RED_PROFILE_CLOSE_START( handle ) IIOProfiler::Get()->ProfileSyncIOCloseFileStart( static_cast<Uint32>( reinterpret_cast<uintptr_t>( handle ) ) )
#	define RED_PROFILE_CLOSE_END() IIOProfiler::Get()->ProfileSyncIOCloseFileEnd()
#else
#	define RED_PROFILE_OPEN_START( pathname )
#	define RED_PROFILE_OPEN_END( handle )
#	define RED_PROFILE_CLOSE_START( handle )
#	define RED_PROFILE_CLOSE_END()
#endif

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 )
#	include "Shlobj.h"
#endif

/************************************************************************/
/* System I/O implementation                                            */
/************************************************************************/
Bool CSystemIO::CopyFile(const char* existingFileName, const char* newFileName, Bool failIfExists)
{
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 )
	UniChar existingWideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( existingFileName, existingWideName, c_maxPath ) );
	UniChar newWideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( newFileName, newWideName, c_maxPath ) );

	return 0 != ::CopyFileW( existingWideName, newWideName, failIfExists );

#elif defined( RED_PLATFORM_DURANGO )
	RED_LOG_ERROR( "CopyFile is not implemented on this platform" );
	return false;
#endif
}

Bool CSystemIO::CreateDirectory(const char* pathName)
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	if ( ::CreateDirectoryW( wideName, nullptr ) == 0 )
	{
		DWORD code = ::GetLastError();
		if ( code != ERROR_SUCCESS && code != ERROR_ALREADY_EXISTS )
		{
			RED_LOG( "Core: [IO]: Low level create directory failed: path='%s', error code=0x%X", pathName, code );
			return false;
		}
	}

	return true;
}

Bool CSystemIO::DeleteFile(const char* fileName)
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( fileName, wideName, c_maxPath ) );

	if ( 0 == ::DeleteFileW( wideName ) )
	{
		RED_LOG( "Core: [IO]: Low level delete file failed: file='%s', error code=0x%X", fileName, GetLastError() );
		return false;
	}

	return true;
}

Bool CSystemIO::IsFileReadOnly(const char* fileName)
{
#ifdef RED_PLATFORM_WINPC
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( fileName, wideName, c_maxPath ) );

	const DWORD fileAttributes = ::GetFileAttributesW( wideName );

	// file doesn't exist
	if ( fileAttributes == INVALID_FILE_ATTRIBUTES )
	{
		return false;
	}

	// check if file is read only
	return fileAttributes & FILE_ATTRIBUTE_READONLY;
#else
	// files on cooked platforms are always read only
	return true;
#endif
}

Bool CSystemIO::FileExist(const char* fileName)
{
#ifdef RED_PLATFORM_WINPC
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( fileName, wideName, c_maxPath ) );

	const DWORD fileAttributes = ::GetFileAttributesW( wideName );
	return ( fileAttributes != INVALID_FILE_ATTRIBUTES );
#else
	RED_LOG_ERROR( "Core: Someone tried to check if file exists, probably this is causing a bug now: %hs", fileName);
	return false;
#endif
}

Bool CSystemIO::SetFileReadOnly(const char* fileName, Bool readOnlyFlag )
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( fileName, wideName, c_maxPath ) );

	DWORD fileAttributes = ::GetFileAttributesW( wideName );

	if ( readOnlyFlag )
	{
		fileAttributes |= FILE_ATTRIBUTE_READONLY;
	}
	else
	{
		fileAttributes &= ~FILE_ATTRIBUTE_READONLY;
	}

	return ::SetFileAttributesW( wideName, fileAttributes ) != 0;
}

Bool CSystemIO::MoveFile(const char* existingFileName, const char* newFileName)
{
#ifdef RED_PLATFORM_WINPC
	UniChar existingWideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( existingFileName, existingWideName, c_maxPath ) );
	UniChar newWideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( newFileName, newWideName, c_maxPath ) );

	const Uint32 flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED;
	if ( 0 == ::MoveFileExW( existingWideName, newWideName, flags ) )
	{
		RED_LOG( "Core: [IO]: Low level move file failed: 0x%X", GetLastError() );
		return false;
	}

	return true;
#elif defined( RED_PLATFORM_DURANGO )
	RED_LOG_WARNING( "MoveFile is not implemented on this platform" );
	return false;
#endif
}

Bool CSystemIO::RemoveDirectory(const char* pathName)
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	return 0 != ::RemoveDirectoryW( wideName );
}

Bool CSystemIO::CreatePath( const char* pathName )
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	// Create path
	for ( UniChar* pos = wideName; *pos; pos++ )
	{
		if ( *pos == L'\\' || *pos == L'/' )
		{
			if ( pos[-1] == L':' )
			{
				continue;
			}

			UniChar was = *pos;
			*pos = 0;

			if ( ::CreateDirectoryW( wideName, nullptr ) == 0 )
			{
				DWORD code = ::GetLastError();
				if ( code != ERROR_SUCCESS && code != ERROR_ALREADY_EXISTS )
				{
					RED_LOG_ERROR( "Core: [IO]: CreatePath failed to create %ls error %u", wideName, code );
					*pos = was;
					return false;
				}
			}
			*pos = was;
		}
	}

	// Path created
	return true;
}

Bool GetFileTime( const char* pathName, FILETIME& filetime )
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	WIN32_FILE_ATTRIBUTE_DATA fileAttributeData = { 0 };
	if ( 0 == ::GetFileAttributesExW( wideName, ::GetFileExInfoStandard, &fileAttributeData ) )
	{
		return false;
	}

	filetime = fileAttributeData.ftLastWriteTime;
	return true;
}

Uint64 CSystemIO::GetFileTimestamp( const char* pathName )
{
	union
	{
		Uint64 timestamp;
		FILETIME systemFormat;
	};

	// Initialize the time
	timestamp = 0ull;

	::GetFileTime( pathName, systemFormat );

	return timestamp;
}

std::time_t ConvertFileTimeToStd( const FILETIME& filetime )
{
	// Microseconds between 1601-01-01 00:00:00 UTC and 1970-01-01 00:00:00 UTC
	static const uint64_t EPOCH_DIFFERENCE_MICROS = 11644473600000000ull;

	// First convert 100-ns intervals to microseconds, then adjust for the
	// epoch difference
	uint64_t total_us = ( static_cast< uint64_t >( filetime.dwHighDateTime ) << 32 | static_cast< uint64_t >( filetime.dwLowDateTime ) ) / 10;
	total_us -= EPOCH_DIFFERENCE_MICROS;

	// Convert to seconds
	return total_us / 1000000;
}

std::time_t CSystemIO::GetStdFileTimestamp( const AnsiChar* pathName )
{
	FILETIME filetime;
	::GetFileTime( pathName, filetime );

	return ConvertFileTimeToStd( filetime );
}

red::DateTime CSystemIO::GetFileTime( const char* pathName )
{
	red::DateTime fileTime;

	FILETIME filetimeSystemFormat;
	if( ::GetFileTime( pathName, filetimeSystemFormat ) )
	{
		// Convert the value into a format compatible with our DateTime
		SYSTEMTIME dateFormat;
		red::Memzero( &dateFormat, sizeof( SYSTEMTIME ) );

		::FileTimeToSystemTime( &filetimeSystemFormat, &dateFormat );

		// Fill in the return value
		fileTime.SetYear( static_cast<Uint32>( dateFormat.wYear ) );
		fileTime.SetMonth( static_cast<Uint32>( dateFormat.wMonth ) - 1 );
		fileTime.SetDay( static_cast<Uint32>( dateFormat.wDay ) - 1 );
		fileTime.SetHour( static_cast<Uint32>( dateFormat.wHour ) );
		fileTime.SetMinute( static_cast<Uint32>( dateFormat.wMinute ) );
		fileTime.SetSecond( static_cast<Uint32>( dateFormat.wSecond ) );
		fileTime.SetMilliSeconds( static_cast<Uint32>( dateFormat.wMilliseconds ) );
	}

	return fileTime;
}

bool CSystemIO::SetFileTime( const char* pathName, const red::DateTime& dateTime )
{
	// Convert the value into a format compatible with our DateTime
	SYSTEMTIME dateFormat;
	red::Memzero( &dateFormat, sizeof( FILETIME ) );

	// Fill in the return value
	dateFormat.wYear = dateTime.GetYear();
	dateFormat.wMonth = dateTime.GetMonth() + 1;
	dateFormat.wDay = dateTime.GetDay() + 1;
	dateFormat.wHour = dateTime.GetHour();
	dateFormat.wMinute = dateTime.GetMinute();
	dateFormat.wSecond = dateTime.GetSecond();
	dateFormat.wMilliseconds = dateTime.GetMilliSeconds();

	// Initialize the time
	FILETIME systemFormat;
	red::Memzero( &systemFormat, sizeof( FILETIME ) );
	if ( !::SystemTimeToFileTime( &dateFormat,  &systemFormat ) )
	{
		return false;
	}

	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	// Open the file handle without opening the file
	RED_PROFILE_OPEN_START( pathName );
	HANDLE handle = ::CreateFileW( wideName, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	RED_PROFILE_OPEN_END( handle );

	if ( handle == INVALID_HANDLE_VALUE )
	{
		return false;
	}

	// Get the time of last write access to the file ( modification time )
	::SetFileTime( handle, NULL, NULL, &systemFormat );

	RED_PROFILE_CLOSE_START( handle );
	CloseHandle( handle );
	RED_PROFILE_CLOSE_END();

	return true;
}

Uint64 CSystemIO::GetFileSize( const char* pathName )
{
	UniChar wideName[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( pathName, wideName, c_maxPath ) );

	RED_PROFILE_OPEN_START( pathName );
	HANDLE file = ::CreateFileW( wideName, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	RED_PROFILE_OPEN_END( file );

	if ( INVALID_HANDLE_VALUE == file )
	{
		return 0;
	}
	else
	{
		LARGE_INTEGER size;
		red::Memset( &size, 0, sizeof(size) );
		::GetFileSizeEx( file, &size );

		RED_PROFILE_CLOSE_START( file );
		CloseHandle( file );
		RED_PROFILE_CLOSE_END();

		return size.QuadPart;
	}

	return 0;
}

/************************************************************************/
/* System FindFile                                                      */
/************************************************************************/
CSystemFindFile::CSystemFindFile(const char* fileName)
{
	UniChar searchPattern[ c_maxPath ];
	RED_VERIFY( red::EngineStringToFileSystemString( fileName, searchPattern, c_maxPath ) );

	m_findFile.m_hasMore = true;
	m_findFile.m_handle = ::FindFirstFileExW( searchPattern, FindExInfoStandard, &m_findFile.m_findData, FindExSearchNameMatch, NULL, 0 );
}

CSystemFindFile::~CSystemFindFile()
{
	if ( m_findFile.m_handle != INVALID_HANDLE_VALUE )
	{
		::FindClose( m_findFile.m_handle );
		m_findFile.m_handle = INVALID_HANDLE_VALUE;
	}
}

CSystemFindFile::operator Bool() const
{
	return m_findFile.m_handle != INVALID_HANDLE_VALUE && m_findFile.m_hasMore;
}

const char* CSystemFindFile::GetFileName()
{
	red::FileSystemStringToEngineString( m_findFile.m_findData.cFileName, m_findFile.m_filename, c_maxPath );
	return m_findFile.m_filename;
}

Bool CSystemFindFile::IsDirectory() const
{
	return 0 != (m_findFile.m_findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

Uint32 CSystemFindFile::GetSize()
{
	return m_findFile.m_findData.nFileSizeLow;
}

void CSystemFindFile::operator ++()
{
	m_findFile.m_hasMore = 0 != ::FindNextFileW(m_findFile.m_handle, &m_findFile.m_findData);
}

#endif // RED_PLATFORM_WIN32 || RED_PLATFORM_WIN64 || RED_PLATFORM_DURANGO
