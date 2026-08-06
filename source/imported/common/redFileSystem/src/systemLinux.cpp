/*
 * Copyright (c) 2007-18 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#if defined( RED_PLATFORM_LINUX )

#include "../../../common/redContainers/include/string/string.h"

#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>
#include <utime.h>



void LogLastError( const char* action, const char* target )
{
	Int32 errorCode = errno;					
	char errorBuffer[256];						
	strerror_r( errorCode, errorBuffer, 256 );

	RED_LOG( "Core: [IO]: %s failed: target=%s. error=%s", action, target, errorBuffer );
}

/************************************************************************/
/* System I/O implementation                                            */
/************************************************************************/
Bool CSystemIO::CopyFile(const char* existingFileName, const char* newFileName, Bool failIfExists)
{
	// check if new file already exists
	Bool exists = FileExist( newFileName );
	if ( exists  && failIfExists )
	{
		RED_LOG( "Core: [IO]: Low level copy file failed: origin=%s target=%s. Error=file already exists", existingFileName, newFileName );
		return false;
	}

	// copy the file 
	std::ifstream  sourceFile( existingFileName, std::ios::binary );
	std::ofstream  destinationFile( newFileName, std::ios::binary );

	if ( sourceFile.good() && destinationFile.good() )
	{
		destinationFile << sourceFile.rdbuf();
		return true;
	}

	RED_LOG( "Core: [IO]: Low level copy file failed: origin=%s target=%s.", existingFileName, newFileName );
	return false;
}

Bool CSystemIO::CreateDirectory(const char* pathName)
{
	Int32 status = mkdir( pathName, S_IRWXU );
	if ( status != 0 )
	{
		LogLastError( "Low level create directory", pathName );
		return false;
	}
	return true;
}

Bool CSystemIO::DeleteFile(const char* fileName)
{
	Int32 status = ::unlink( fileName );
	if ( status != 0 )
	{
		LogLastError( "Low level delete file", fileName );
		return false;
	}

	return true;
}

Bool CSystemIO::IsFileReadOnly(const char* fileName)
{
	Bool readable = ( ::access( fileName, R_OK ) == 0 );
	Bool writable = ( ::access( fileName, W_OK ) == 0 );

	return readable && !writable;
}

Bool CSystemIO::FileExist(const char* fileName)
{
	Bool status = ::access( fileName, F_OK );
	return status == 0;
}

Bool CSystemIO::SetFileReadOnly(const char* fileName, Bool readOnlyFlag )
{
	Int32 status = ::chmod( fileName, readOnlyFlag ? S_IRUSR : ( S_IRUSR | S_IWUSR ) );
	if ( status != 0 )
	{
		LogLastError( "Low level set file read only", fileName );
		return false;
	}
	return true;
}

Bool CSystemIO::MoveFile(const char* existingFileName, const char* newFileName)
{
	// #tbd: need to preserve permissions?
	Int32 status = rename( existingFileName, newFileName );
	if ( status != 0 )
	{
		LogLastError( "Low level move file", existingFileName );
		return false;
	}
	return true;
}

Bool CSystemIO::RemoveDirectory(const char* pathName)
{
	Int32 status = ::rmdir( pathName );
	if ( status != 0 )
	{
		LogLastError( "Low level remove directory", pathName );
		return false;
	}
	return true;
}

Bool CSystemIO::CreatePath( const char* pathName )
{
	char buffer[ 4096 ];
	red::Strcpy( buffer, pathName, RED_ARRAY_COUNT( buffer ) );

	// Create path
	char *path = buffer;	 
	for ( char *pos=path; *pos; pos++ )
	{
		if ( *pos == '\\' || *pos == '/' )
		{
			char was = *pos;
			*pos = 0;
			if ( !CreateDirectory( path ))
			{
				if ( !red::Strchr( path, ':' ) )
				{
					return false;
				}
			}
			*pos = was;
		}
	}

	// Path created
	return true;
}

Uint64 CSystemIO::GetFileTimestamp( const char* pathName )
{
	struct stat stats;
	if ( ::stat( pathName, &stats ) == 0 )
	{
		// Unix time epoch stores
		// nanoseconds since 00:00:00 1 Jan 1970 UTC
		// Convert it to Windows timestamp which is counted in
		// 100 nanosecond intervals since 00:00:00 1 Jan 1601 UTC
		const Uint64 epochDifference = 116444736000000000ull;
		return ( stats.st_mtime / 100 ) - epochDifference;
	}

	return 0ull;
}

std::time_t CSystemIO::GetStdFileTimestamp( const AnsiChar* pathName )
{
	struct stat stats;
	if ( ::stat( pathName, &stats ) == 0 )
	{
		return stats.st_mtime;
	}

	return 0;
}

red::DateTime CSystemIO::GetFileTime( const char* pathName )
{
	red::DateTime fileTime;

	struct stat stats;
	if ( ::stat( pathName, &stats ) == 0 )
	{
		// Convert to format that can easily be passed through to 
		struct tm linuxFileTime = { 0 };
		if ( gmtime_r( &stats.st_mtime, &linuxFileTime ) != NULL  )
		{
			fileTime.SetYear( static_cast< Uint32 >( linuxFileTime.tm_year ) + 1900 ); // 1900+n
			fileTime.SetMonth( static_cast< Uint32 >( linuxFileTime.tm_mon ) ); // 0-11
			fileTime.SetDay( static_cast< Uint32 >( linuxFileTime.tm_mday ) - 1 ); // 1-31
			fileTime.SetHour( static_cast< Uint32 >( linuxFileTime.tm_hour ) );
			fileTime.SetMinute( static_cast< Uint32 >( linuxFileTime.tm_min ) );
			fileTime.SetSecond( static_cast< Uint32 >( linuxFileTime.tm_sec ) );
		}
	}

	return fileTime;
}

bool CSystemIO::SetFileTime( const char* pathName, const red::DateTime& dateTime )
{
	struct tm linuxFileTime = { 0 };
	linuxFileTime.tm_year = dateTime.GetYear() - 1900;
	linuxFileTime.tm_mon = dateTime.GetMonth();
	linuxFileTime.tm_mday = dateTime.GetDay() + 1;
	linuxFileTime.tm_hour = dateTime.GetHour();
	linuxFileTime.tm_min = dateTime.GetMinute();
	linuxFileTime.tm_sec = dateTime.GetSecond();

	struct utimbuf times;
	times.modtime = timegm( &linuxFileTime ); // not fully thread-safe
	times.actime = times.modtime;
	Int32 status = utime( pathName, &times );
	
	if ( status != 0 )
	{
		LogLastError( "Low level set file time", pathName );
		return false;
	}
	return true;
}

Uint64 CSystemIO::GetFileSize( const char* pathName )
{
	struct stat stats;
	if ( ::stat( pathName, &stats ) != 0 )
	{
		return 0;
	}

	return stats.st_size >= 0 ? static_cast< Uint64 >( stats.st_size ) : 0;
}

/************************************************************************/
/* System FindFile                                                      */
/************************************************************************/

CSystemFindFile::CSystemFindFile(const char* fileName)
{
	String normalizedFilePath( fileName, red::PoolEngine());
	normalizedFilePath.ReplaceAll( "\\", "/", red::PoolEngine());

	// TODO s.ortiz use red::paths:: functions?
	String path{ red::PoolEngine() };
	String pattern{ red::PoolEngine() };
	if ( !normalizedFilePath.SplitFromRight( "/", &path, &pattern ) )
	{
		// theres no pattern
		path = normalizedFilePath;
	}

	if ( pattern == "*" || pattern == "*." )
	{
		// its searching for everything, so no need to pattern match anything
		pattern.Clear();
	}
	
	m_findFile.m_usePattern = !pattern.Empty();

	red::Strcpy( m_findFile.m_path, path.AsChar(), PATH_MAX );
	if ( m_findFile.m_usePattern )
	{
		red::Strcpy( m_findFile.m_pattern, pattern.AsChar(), PATH_MAX );
	}

	m_findFile.m_folder = ::opendir( m_findFile.m_path );
	if ( m_findFile.m_folder == nullptr )
	{
		LogLastError( "CSystemFindFile", fileName );
		return;
	}

	// find first valid entry
	++( *this );
}

CSystemFindFile::~CSystemFindFile()
{
	if ( m_findFile.m_folder != nullptr )
	{
		::closedir( m_findFile.m_folder );
		m_findFile.m_folder = nullptr;
		m_findFile.m_currentEntry = nullptr;
	}
}

CSystemFindFile::operator Bool() const
{
	return m_findFile.m_folder != nullptr && m_findFile.m_currentEntry != nullptr;
}

const char* CSystemFindFile::GetFileName()
{
	return m_findFile.m_currentEntry->d_name;
}

Bool CSystemFindFile::IsDirectory() const
{
	// if theres no support for d_type use stat
	if ( m_findFile.m_currentEntry->d_type == DT_UNKNOWN )
	{
		// stat requires the full path
		String fullPath( m_findFile.m_path );
		fullPath += m_findFile.m_currentEntry->d_name;

		struct stat stats;
		stat( fullPath.AsChar(), &stats );
		return S_ISDIR( stats.st_mode );
	}

	return (m_findFile.m_currentEntry->d_type == DT_DIR );
}

Uint32 CSystemFindFile::GetSize()
{
	// stat requires the full path
	String fullPath( m_findFile.m_path );
	fullPath += GetFileName();

	struct stat stats;
	if ( ::stat( fullPath.AsChar(), &stats ) != 0 )
	{
		return 0;
	}

	return stats.st_size;
}

void CSystemFindFile::operator ++()
{
	m_findFile.m_currentEntry = ::readdir( m_findFile.m_folder );

	// if theres a pattern, read entries until the first match appears
	if ( m_findFile.m_usePattern )
	{
		while ( Bool( *this ) )
		{
			const char* currentFileName = GetFileName();
			Int32 status = fnmatch( m_findFile.m_pattern, currentFileName, 0 );
			if ( status == 0 )
			{
				return;
			}

			m_findFile.m_currentEntry = ::readdir( m_findFile.m_folder );
		}
	}
}

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_LINUX
