/**
* Copyright (c) 2007 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include <ctime>

/************************************************************************/
/* Per platform types definitions                                       */
/************************************************************************/
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )

constexpr Uint32 c_maxPath = 512;

struct FindFileObject
{
	HANDLE			m_handle;
	WIN32_FIND_DATA	m_findData;
	char			m_filename[c_maxPath];
	Bool			m_hasMore;
};

#elif defined( RED_PLATFORM_ORBIS )

class COrbisDirWalker;
typedef COrbisDirWalker* FindFileObject;

#elif defined( RED_PLATFORM_LINUX )

#include <dirent.h>

struct FindFileObject
{
	DIR* m_folder;
	struct dirent* m_currentEntry;
	AnsiChar m_path[ PATH_MAX ];
	AnsiChar m_pattern[ PATH_MAX ];
	Bool m_usePattern;
};

#else
#	error undefined platform
#endif

/************************************************************************/
/* System I/O                                                           */
/************************************************************************/
class RED_FILESYSTEM_API CSystemIO
{
public:
	static Bool CopyFile(const char* existingFileName, const char* newFileName, Bool failIfExists);

	static Bool CreateDirectory(const char* pathName);

	static Bool DeleteFile(const char* fileName);

	static Bool IsFileReadOnly(const char* fileName);

	static Bool FileExist(const char* fileName);

	static Bool SetFileReadOnly(const char* fileName, Bool readOnlyFlag );

	static Bool MoveFile(const char* existingFileName, const char* newFileName);

	static Bool RemoveDirectory(const char* pathName);

	static Uint64 GetFileSize( const char* pathName );

	static Bool CreatePath( const char* pathName );

	static Uint64 GetFileTimestamp( const char* pathName );

	static std::time_t GetStdFileTimestamp( const char* pathName );

	static red::DateTime GetFileTime( const char* pathName );

	static bool SetFileTime( const char* pathName, const red::DateTime& dateTime );
};

/************************************************************************/
/* System FindFile                                                      */
/************************************************************************/
class RED_FILESYSTEM_API CSystemFindFile
{
	FindFileObject	m_findFile;
public:
	CSystemFindFile(const char* findName);

	~CSystemFindFile();

	operator Bool() const;

	void operator ++();

	const char* GetFileName();

	Bool IsDirectory() const;

	Uint32 GetSize();
};

//////////////////////////////////////////////////////////////////////////
// File synchronization service

#if defined( RED_PLATFORM_WINPC ) && !defined( RED_CONFIGURATION_FINAL )
	#define RED_FILE_SYNC_SERVICE_ENABLED
#endif
