/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#ifdef RED_PLATFORM_ORBIS

#include "../../../common/redContainers/include/string/string.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include <time.h>
#include <rtc.h>

#pragma comment ( lib, "libSceRtc_stub_weak.a" )

#define INVALID_FD_VALUE (-1)

/************************************************************************/
/* System I/O implementation                                            */
/************************************************************************/

Bool CSystemIO::CopyFile(const char* existingFileName, const char* newFileName, Bool failIfExists)
{
	RED_UNUSED( existingFileName );
	RED_UNUSED( newFileName );
	RED_UNUSED( failIfExists );
	return false;
}

Bool CSystemIO::CreateDirectory(const char* pathName)
{
	//FIXME: Doesn't create intermediate directories. This is just using the "old" code before CL# 219192 changed it.
	// Also, confirm what 'group' and 'other' modes are really needed on Orbis
	int res = ::sceKernelMkdir( pathName, SCE_KERNEL_S_IRWXU );
	return ( res == SCE_OK ) || ( res == SCE_KERNEL_ERROR_EEXIST );
}

Bool CSystemIO::DeleteFile(const char* fileName)
{
	return ::sceKernelUnlink( fileName ) == SCE_OK;
}

Bool CSystemIO::IsFileReadOnly(const char* fileName)
{
	// files on cooked platforms are always read only
	RED_UNUSED( fileName );
	return true;
}

Bool CSystemIO::FileExist(const char* fileName)
{
	RED_LOG_WARNING( "Core: Someone tried to check if file exists, probably this is causing a bug now: %s", fileName);
	return false;
}

Bool CSystemIO::SetFileReadOnly(const char* fileName, Bool readOnlyFlag )
{
	return ::sceKernelChmod( fileName, readOnlyFlag ? SCE_KERNEL_S_IRU : SCE_KERNEL_S_IRWU ) == SCE_OK;
}

Bool CSystemIO::MoveFile(const char* existingFileName, const char* newFileName)
{
	return ::sceKernelRename( existingFileName, newFileName )== SCE_OK;
}

Bool CSystemIO::RemoveDirectory(const char* pathName)
{
	return ::sceKernelRmdir( pathName ) == SCE_OK;
}

static Bool IsPathSeparator( const char *sz )
{
	return *sz == '\\' || *sz == '/';
}

Bool CSystemIO::CreatePath( const char* pathName )
{
	RED_ASSERT( pathName );
	if( *pathName == 0 )
	{
		// Empty path cannot be created.
		return false;
	}

	char buffer[ 4096 ];
	red::Strcpy( buffer, pathName, RED_ARRAY_COUNT( buffer ) );

	// Create path
	char* path = buffer;
	char* pos = path;
	if( IsPathSeparator( pos ) )
	{
		// if path is starting from separator skip it
		++pos;
	}
	for ( ; *pos; pos++ )
	{
		if ( IsPathSeparator( pos ) )
		{
			char was = *pos;
			*pos = 0;
			if ( !CreateDirectory( path ))
			{
				RED_LOG_ERROR( "[CSystemIO] Failed to create part of the path '%hs'. Part: '%hs'", pathName, path );
				return false;
			}
			*pos = was;
		}
	}

	// Path created
	return true;
}

Uint64 CSystemIO::GetFileTimestamp( const char* pathName )
{
	SceKernelStat stat;
	if ( ::sceKernelStat( pathName, &stat ) == SCE_OK )
	{
		// Orbis uses the Unix time epoch but it stores
		// nanoseconds since 00:00:00 1 Jan 1970 UTC
		// Convert it to Windows timestamp which is counted in
		// 100 nanosecond intervals since 00:00:00 1 Jan 1601 UTC
		const Uint64 epochDifference = 116444736000000000ull;
		const Uint64 nanoseconds = stat.st_mtim.tv_sec * 1000000000ull + stat.st_mtim.tv_nsec;
		return (nanoseconds / 100) - epochDifference;
	}

	return 0ull;
}

red::DateTime CSystemIO::GetFileTime( const char* pathName )
{
	red::DateTime fileTime;

	// 	SceKernelTimespec on Orbis:
	// 	This structure represents the time. The time elapsed from January 1 1970 12:00 a.m. will be stored separately in the number of seconds and the fraction less than one second.

	SceKernelStat stat;
	if ( ::sceKernelStat( pathName, &stat ) == SCE_OK )
	{
		// Convert to format that can easily be passed through to 
		SceRtcDateTime orbisFileTime;
		if( ::sceRtcSetTime_t( &orbisFileTime, stat.st_mtim.tv_sec ) == SCE_OK )
		{
			fileTime.SetYear			( static_cast< Uint32 >( orbisFileTime.year ) );
			fileTime.SetMonth			( static_cast< Uint32 >( orbisFileTime.month ) - 1 );
			fileTime.SetDay				( static_cast< Uint32 >( orbisFileTime.day ) - 1 );
			fileTime.SetHour			( static_cast< Uint32 >( orbisFileTime.hour ) );
			fileTime.SetMinute			( static_cast< Uint32 >( orbisFileTime.minute ) );
			fileTime.SetSecond			( static_cast< Uint32 >( orbisFileTime.second ) );
			fileTime.SetMilliSeconds	( static_cast< Uint32 >( stat.st_mtim.tv_nsec ) / 1000000 );
		}
	}

	return fileTime;
}

bool CSystemIO::SetFileTime( const char* pathName, const red::DateTime& dateTime )
{
	return false;
}


std::time_t CSystemIO::GetStdFileTimestamp( const AnsiChar* pathName )
{
	SceKernelStat stat;
	if ( ::sceKernelStat( pathName, &stat ) == SCE_OK )
	{
		// Returns the seconds portion of the last modified time
		return stat.st_mtim.tv_sec;
	}

	return 0;
}

Uint64 CSystemIO::GetFileSize( const char* pathName )
{
	SceKernelStat stat;
	if ( ::sceKernelStat( pathName, &stat ) != SCE_OK )
	{
		return 0;
	}

	return stat.st_size >= 0 ? static_cast< Uint64 >( stat.st_size ) : 0;
}

/************************************************************************/
/* System FindFile                                                      */
/************************************************************************/
class COrbisDirWalker : red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
private:
	static const red::String DIR_SEPARATOR;

private:
	red::String	m_wildcard;

private:
	size_t		m_blockReadSize;
	SceChar8*	m_buf;
	SceChar8*	m_curBuf;
	SceChar8*	m_endBuf;
	SceInt32	m_fd;

private:
	red::String			m_curBasePath;	
#if 0
	red::Queue< red::String >	m_direntQueue;
#endif
	red::String		m_curDirentPath;

public:
	COrbisDirWalker( const red::String& wildcard );
	~COrbisDirWalker();
	Bool OpenDir( const red::String& path );
	Bool IsValid() const;
	Bool NextDirent();
	SceKernelDirent* CurDirent();
	void CloseDir();

public:
	const red::String& GetWildcard() const { return m_wildcard; }

public:
	const char* GetCurDirentPath() const;

private:
	Bool FetchDirents();
	Bool FillBuf();
	void UpdateCurrentDirent();
	void Reset();
};

CSystemFindFile::CSystemFindFile(const char* fileName)
	: m_findFile{}
{
	String normalizedFilePath( fileName, red::PoolEngine());
	normalizedFilePath.ReplaceAll( "\\", "/", red::PoolEngine());

	String normalizedBasePath;
	String wildcard;
	if ( ! normalizedFilePath.SplitFromRight( "*", &normalizedBasePath, &wildcard ) )
	{
		normalizedBasePath = normalizedFilePath;
	}
	else if ( wildcard == "*" || wildcard == ".*" )
	{
		wildcard.Clear();
	}

	// HACK: not much choice
	m_findFile = RED_NEW( COrbisDirWalker )( wildcard );

	m_findFile->OpenDir( normalizedBasePath );

	if ( wildcard.Empty() )
	{
		return;
	}

	//size_t wildcardStrLen = wildcard.Size() - 1; // minus one for NULL
	while ( m_findFile->IsValid() && m_findFile->CurDirent()->d_type == SCE_KERNEL_DT_REG )
	{
// 		const UniChar* curDirentPath = m_findFile->GetCurDirentPath();
// 		UniChar* pch = red::StrchrR( curDirentPath, wildcard.AsChar() );
// 		if ( *(pch + wildcardStrLen + 1 ) == '\0' ) // matches end of string
// 		{
// 			break;
// 		}

		String curDirentPath( m_findFile->GetCurDirentPath(), red::PoolEngine());
		if ( curDirentPath.EndsWith( wildcard ) )
		{
			break;
		}

		m_findFile->NextDirent();
	}
}

CSystemFindFile::~CSystemFindFile()
{
	RED_DELETE( m_findFile );
}

CSystemFindFile::operator Bool() const
{
	return m_findFile->IsValid();
}

const char* CSystemFindFile::GetFileName()
{
	return m_findFile->GetCurDirentPath();
}

Bool CSystemFindFile::IsDirectory() const
{
	const SceKernelDirent* dirent = m_findFile->CurDirent();
	return dirent ? m_findFile->CurDirent()->d_type == SCE_KERNEL_DT_DIR : false;
}

Uint32 CSystemFindFile::GetSize()
{
	return 0;
}

void CSystemFindFile::operator ++()
{
	m_findFile->NextDirent();

	const String& wildcard = m_findFile->GetWildcard();
	if ( wildcard.Empty() )
	{
		return;
	}

//	size_t wildcardStrLen = wildcard.Size() - 1; // minus one for NULL
	while ( m_findFile->IsValid() && m_findFile->CurDirent()->d_type == SCE_KERNEL_DT_REG )
	{
// 		const UniChar* curDirentPath = m_findFile->GetCurDirentPath();
// 		UniChar* pch = red::StrchrR( curDirentPath, wildcard.AsChar() );
// 		if ( *(pch + wildcardStrLen + 1 ) == '\0' ) // matches end of string
// 		{
// 			break;
// 		}
		
		String curDirentPath( m_findFile->GetCurDirentPath() );
		if ( curDirentPath.EndsWith( wildcard ) )
		{
			break;
		}

		m_findFile->NextDirent();
	}
}

const red::String COrbisDirWalker::DIR_SEPARATOR = "/";

COrbisDirWalker::COrbisDirWalker( const String& wildcard )
	: m_wildcard( wildcard )
	, m_blockReadSize( 0 )
	, m_buf( nullptr )
	, m_curBuf( nullptr )
	, m_endBuf( nullptr )
	, m_fd( INVALID_FD_VALUE )
{
}

Bool COrbisDirWalker::OpenDir( const String& path )
{
	String normalizedBasePath = path;
	if ( normalizedBasePath.EndsWith("/") )
	{
		normalizedBasePath.RemoveAt( normalizedBasePath.Length() );
	}

	CloseDir();

	m_fd = ::sceKernelOpen( normalizedBasePath.AsChar(), SCE_KERNEL_O_DIRECTORY | SCE_KERNEL_O_RDONLY, 0 );
	if ( m_fd <= INVALID_FD_VALUE )
	{
		m_fd = INVALID_FD_VALUE;

#ifndef RED_CONFIGURATION_FINAL
		if ( m_fd == SCE_KERNEL_ERROR_ENOBLK )
		{
			RED_LOG_ERROR( "sceKernelOpen: Failed to open directory '%hs' since it's on the network locus!", path.AsChar() );
		}
		else if ( m_fd == SCE_KERNEL_ERROR_ENOPLAYGOENT )
		{
			RED_LOG_ERROR( "sceKernelOpen: Failed to open directory '%hs' since it's not in the PlayGo definition file!", path.AsChar() );
		}
#endif

		return false;
	}

	SceKernelStat dirStat;
	const SceInt32 err = ::sceKernelFstat( m_fd, &dirStat );
	if ( err < SCE_OK )
	{
		RED_HALT( "sceKernelFstat failed: 0x%08x", err );
		Reset();
		return false;
	}

	m_blockReadSize = dirStat.st_blksize;
	m_buf = static_cast< SceChar8* >( RED_ALLOCATE( red::PoolEngine, m_blockReadSize ) );
	RED_ASSERT( m_buf );
	if ( ! m_buf )
	{
		Reset();
		return false;
	}

	m_curBasePath = normalizedBasePath;

	if ( FillBuf() )
	{
		UpdateCurrentDirent();
		return true;
	}

	return false;
}

COrbisDirWalker::~COrbisDirWalker()
{
	Reset();
}

Bool COrbisDirWalker::IsValid() const
{ 
	return m_curBuf != nullptr;
}
	
Bool COrbisDirWalker::NextDirent()
{
	// Go to the next entry or clear buffer
	if ( m_curBuf )
	{
		const SceKernelDirent* dirent = reinterpret_cast< SceKernelDirent* >( m_curBuf );
		if ( m_curBuf + dirent->d_reclen >= m_endBuf )
		{
			m_curBuf = nullptr;
		}
		else
		{
			m_curBuf += dirent->d_reclen;
		}
	}

	const Bool hasNext = m_curBuf || FetchDirents();
	UpdateCurrentDirent();

	return hasNext;
}

SceKernelDirent* COrbisDirWalker::CurDirent()
{
	if ( ! m_curBuf )
	{
		return nullptr;
	}

	SceKernelDirent* dirent = reinterpret_cast< SceKernelDirent* >( m_curBuf );
	return dirent;
}

void COrbisDirWalker::CloseDir()
{
	m_blockReadSize = 0;

	if ( m_fd > INVALID_FD_VALUE )
	{
		::sceKernelClose( m_fd );
		m_fd = INVALID_FD_VALUE;
	}

	if ( m_buf )
	{
		RED_FREE( red::PoolEngine, m_buf );
	}

	m_buf = m_curBuf = m_endBuf = nullptr;

	UpdateCurrentDirent();
}

Bool COrbisDirWalker::FetchDirents()
{
	if ( m_fd <= INVALID_FD_VALUE )
	{
		return false;
	}

	if ( FillBuf() )
	{
		return true;
	}

#if 0
	// OpenDir calls FillBuf()
	if ( ! m_direntQueue.Empty() )
	{
		String path = m_direntQueue.Front();
		m_direntQueue.Pop();
		return OpenDir( path );
	}
#endif

	return false;
}

Bool COrbisDirWalker::FillBuf()
{
	m_curBuf = m_endBuf = nullptr;

	SceInt32 ret = ::sceKernelGetdents( m_fd, m_buf, m_blockReadSize );
	if ( ret < SCE_OK )
	{
		RED_HALT( "sceKernelGetdents failed: 0x%08x", ret );
		return false;
	}

	if ( ret == 0 )
	{
		// End of directory
		return false;
	}

	const SceInt32 numRead = ret;

	m_curBuf = m_buf;
	m_endBuf = m_buf + numRead;

	return true;
}

void COrbisDirWalker::UpdateCurrentDirent()
{
	if ( ! m_curBuf )
	{
		m_curDirentPath.Clear();
		return;
	}

	SceKernelDirent* dirent = reinterpret_cast< SceKernelDirent* >( m_curBuf );
	
	m_curDirentPath = dirent->d_name;

#if 0
	if ( dirent->d_type == SCE_KERNEL_DT_DIR )
	{
		String dirPath = m_curBasePath + DIR_SEPARATOR + dirent->d_name;
		m_direntQueue.Push( dirPath );
	}
#endif
}

void COrbisDirWalker::Reset()
{
	CloseDir();

	m_curBasePath.Clear();
#if 0
	m_direntQueue.Clear();
#endif
}

const char* COrbisDirWalker::GetCurDirentPath() const
{
	return m_curDirentPath.AsChar();
}

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_ORBIS
