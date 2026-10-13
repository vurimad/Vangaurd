#include "build.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

#include "fileSyncFileManager.h"
#include "fileSyncService.h"
#include "../../../internal/FileSync/include/API.h"

FileSyncFileManager::FileSyncFileManager(
	const red::AbsolutePath& engineRoot, const red::AbsolutePath& gameRoot,
	const red::AbsolutePath& cachePath )
	: CFileManager( engineRoot, gameRoot, cachePath )
{}

Bool FileSyncFileManager::IsExcludedPath( const AnsiChar* path )
{
	const red::StringView fileName = red::paths::GetFileName( path );
	const red::StringView extension = red::paths::GetExtension( fileName );

	// Filter out based on extension and/or file name

	return
		( extension.Data() && !red::StrcmpNC( extension.Data(), "log" ) ) ||
		( fileName.Data() && !red::StrcmpNC( fileName.Data(), "gamepipelinelibrary.cache" ) );
}

Bool FileSyncFileManager::TranslateFilePath( AnsiChar* path, const Uint32 pathSize )
{
	RED_ASSERT( FileSyncService::IsClient() );

	if ( IsExcludedPath( path ) )
	{
		return true;
	}

	UniChar pathUnicode[ io::REDIO_MAX_PATH_LENGTH ];
	red::EngineStringToFileSystemString( path, pathUnicode, io::REDIO_MAX_PATH_LENGTH );

	UniChar cachePathBuffer[ io::REDIO_MAX_PATH_LENGTH ];
	const file_sync::SyncedFilePathResult result = file_sync::Client_GetSyncedFilePath( FileSyncService::GetClient(), pathUnicode, cachePathBuffer, io::REDIO_MAX_PATH_LENGTH );
	switch ( result )
	{
	case file_sync::SyncedFilePathResult::OK_LocalPath:
	case file_sync::SyncedFilePathResult::FAILURE_NotInRootDir: // Data from outside of depot e.g. files in C:\Users\<username>\...
		return true;
	case file_sync::SyncedFilePathResult::OK_CachePath:
		red::WideCharToStdChar( path, cachePathBuffer, pathSize );
		return true;
	case file_sync::SyncedFilePathResult::ERROR_ConnectionProblem:
	case file_sync::SyncedFilePathResult::ERROR_IOIssue:
		RED_LOG_ERROR( "File sync failed with critical error=%s. The client can not continue.", file_sync::SyncedFilePathResultToString( result ) );
		FileSyncService::OnCriticalError();
		return false;
	}

	return false;
}

red::UniquePtr< IFile > FileSyncFileManager::CreateFileReader( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags ) const
{
	// Convert to raw char buffer

	AnsiChar absoluteFilePathBuffer[ io::REDIO_MAX_PATH_LENGTH ];
	red::Strcpy( absoluteFilePathBuffer, absoluteFilePath.AsChar(), io::REDIO_MAX_PATH_LENGTH );

	// Translate to file sync path

	if ( !TranslateFilePath( absoluteFilePathBuffer, io::REDIO_MAX_PATH_LENGTH ) )
	{
		return nullptr;
	}

	// Convert back to AbsolutePath

	const red::AbsolutePath translatedPath = red::AbsolutePath::CreateFilePath( absoluteFilePathBuffer );

	// Use base class functionality to create reader but with translated path

	return CFileManager::CreateFileReader( translatedPath, openFlags );
}

Uint64 FileSyncFileManager::GetFileSize( const red::AbsolutePath& absoluteFilePath ) const
{
	if ( IsExcludedPath( absoluteFilePath.AsChar() ) )
	{
		return CFileManager::GetFileSize( absoluteFilePath );
	}

	red::Utf16String pathUnicode = absoluteFilePath.ToUtf16String();
	const long long int size = file_sync::Client_GetFileSize( FileSyncService::GetClient(), pathUnicode.AsChar() );
	return size == -1 ? 0 : size; // In order to conform with CSystemIO let's return 0 if file doesn't exist
}

red::DateTime FileSyncFileManager::GetFileTime( const char* absoluteFilePath ) const
{
	if ( IsExcludedPath( absoluteFilePath ) )
	{
		return CFileManager::GetFileTime( absoluteFilePath );
	}

	// Get file time

	FILETIME fileTime;
	UniChar absoluteFilePathUnicode[ io::REDIO_MAX_PATH_LENGTH ];
	red::EngineStringToFileSystemString( absoluteFilePath, absoluteFilePathUnicode, io::REDIO_MAX_PATH_LENGTH );
	if ( !file_sync::Client_GetFileTime( FileSyncService::GetClient(), absoluteFilePathUnicode, fileTime ) )
	{
		return red::DateTime();
	}

	// Convert to system time

	SYSTEMTIME systemTime;
	::FileTimeToSystemTime( &fileTime, &systemTime );

	// Convert to engine date-time

	red::DateTime dateTime;
	dateTime.SetYear( static_cast< Uint32 >( systemTime.wYear ) );
	dateTime.SetMonth( static_cast< Uint32 >( systemTime.wMonth ) - 1 );
	dateTime.SetDay( static_cast< Uint32 >( systemTime.wDay ) - 1 );
	dateTime.SetHour( static_cast< Uint32 >( systemTime.wHour ) );
	dateTime.SetMinute( static_cast< Uint32 >( systemTime.wMinute ) );
	dateTime.SetSecond( static_cast< Uint32 >( systemTime.wSecond ) );
	dateTime.SetMilliSeconds( static_cast< Uint32 >( systemTime.wMilliseconds ) );

	return dateTime;
}

red::DateTime FileSyncFileManager::GetFileTime( const red::AbsolutePath& absoluteFilePath ) const
{
	return GetFileTime( absoluteFilePath.AsChar() );
}

Uint64 FileSyncFileManager::GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const
{
	union
	{
		Uint64 timestamp;
		FILETIME fileTime;
	};

	if ( IsExcludedPath( absoluteFilePath.AsChar() ) )
	{
		return CFileManager::GetFileTimeStamp( absoluteFilePath );
	}

	red::Utf16String absoluteFilePathUnicode = absoluteFilePath.ToUtf16String();
	if ( !file_sync::Client_GetFileTime( FileSyncService::GetClient(), absoluteFilePathUnicode.AsChar(), fileTime ) )
	{
		return 0ull;
	}

	return timestamp;
}

Bool FileSyncFileManager::FileExist( const red::AbsolutePath& absoluteFilePath )
{
	if ( IsExcludedPath( absoluteFilePath.AsChar() ) )
	{
		return CFileManager::FileExist( absoluteFilePath );
	}

	red::Utf16String absoluteFilePathUnicode = absoluteFilePath.ToUtf16String();
	return file_sync::Client_FileExists( FileSyncService::GetClient(), absoluteFilePathUnicode.AsChar() );
}

red::DynArray< String > FileSyncFileManager::FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const
{
	return FindFilesOrDirectories( searchAbsolutePathWithPattern, false );
}
	
red::DynArray< String > FileSyncFileManager::FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const
{
	return FindFilesOrDirectories( searchAbsolutePathWithoutPattern, true );
}

red::DynArray< String > FileSyncFileManager::FindFilesOrDirectories( const red::AbsolutePath& searchAbsolutePath, const Bool findDirectories ) const
{
	// Fallback to base implementation for excluded files

	if ( IsExcludedPath( searchAbsolutePath.AsChar() ) )
	{
		return
			findDirectories ?
				CFileManager::FindDirectories( searchAbsolutePath ) :
				CFileManager::FindFiles( searchAbsolutePath );
	}

	// Append *.* to find directories

	const red::AbsolutePath searchAbsolutePathWithPattern =
		findDirectories ?
			searchAbsolutePath.AddFilePath( "*.*" ) :
			searchAbsolutePath;

	// Enumerate files/directories at given location

	red::Utf16String searchAbsolutePathUnicode = searchAbsolutePathWithPattern.ToUtf16String();
	if ( file_sync::FileSearchIterator* it = file_sync::Client_SearchFiles( FileSyncService::GetClient(), searchAbsolutePathUnicode.AsChar() ) )
	{
		red::DynArray< String > fileNames{ red::PoolEngine() };
		while ( file_sync::FileSearchIterator_HasCurrent( it ) )
		{
			if ( file_sync::FileSearchIterator_IsCurrentElementDirectory( it ) == findDirectories )
			{
				const wchar_t* unicodePath = file_sync::FileSearchIterator_GetCurrentName( it );
				AnsiChar ansiPath[ io::REDIO_MAX_PATH_LENGTH ];
				red::WideCharToStdChar( ansiPath, unicodePath, io::REDIO_MAX_PATH_LENGTH );

				fileNames.PushBack( ansiPath );
			}

			file_sync::FileSearchIterator_MoveToNext( it );
		}

		file_sync::FileSearchIterator_Release( it );
		return fileNames;
	}

	// Fallback to base implementation

	return
		findDirectories ?
			CFileManager::FindDirectories( searchAbsolutePath ) :
			CFileManager::FindFiles( searchAbsolutePath );
}

#else

RED_NO_EMPTY_FILE()

#endif // RED_FILE_SYNC_SERVICE_ENABLED
