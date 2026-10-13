#pragma once

#include <windows.h>

#ifdef FILE_SYNC_EXPORT
   #define DLL_API __declspec( dllexport )
#else  
   #define DLL_API __declspec( dllimport )
#endif  

// Default file sync server's listen port
#define FILE_SYNC_DEFAULT_SERVER_LISTEN_PORT 5678

namespace file_sync
{
	// =================================
	// Client API
	// =================================

	class Client;
	class FileSearchIterator;

	// Possible return values of the Client_GetSyncedFilePath() function
	enum class SyncedFilePathResult
	{
		// Success! Given path is OK to use
		OK_LocalPath,

		// Success! Given cache path buffer has been filled with cache file path to use (note: file was already in cache or has just been downloaded)
		OK_CachePath,

		// Non critical error: Client not initialized
		FAILURE_NotInitialized,

		// Non critical error: File not in root directory
		FAILURE_NotInRootDir,

		// Non critical error: File not present on the server
		FAILURE_NotPresentOnServer,

		// Non critical error: File was present (at client startup) but is not anymore on the server
		FAILURE_DisappearedFromServer,

		// Critical error (file sync can't continue): There was a problem with connection to server
		ERROR_ConnectionProblem,

		// Critical error (file sync can't continue): There was a problem with file or directory creation client side
		ERROR_IOIssue
	};

	struct SharedParams
	{
		// null terminated array of (root) directories to scan
		const wchar_t** localRootDirs = nullptr;
		// Time limit for startup recalculation of file checksums
		// -1 indicates not to do any recalculation
		// 0 indicates to do full recalculation (no time limit)
		// positive values indicate time limit for recalculation in milliseconds
		unsigned int startupChecksumRecalculationTimeLimitMilliSecs = 10000;
	};

	// Client startup parameters
	struct ClientParams : SharedParams
	{
		// File synced cache directory - this is where files downloaded from the server get stored
		const wchar_t* cacheDir = nullptr;
		// IP of the server to connect to; 0 indicates local host
		unsigned int serverIP = 0;
		// Listen port of the server
		unsigned short serverPort = FILE_SYNC_DEFAULT_SERVER_LISTEN_PORT;
	};

	// Client side statistics
	struct ClientStats
	{
		// How many files have already been synced
		unsigned int syncedFilesCount;
		// Total size of synced files
		unsigned int syncedFilesSize;
		// How many milliseconds were spent in syncing
		unsigned int milliSecsSpentSyncing;
		// How much time (in milliseconds) elapsed since last file sync
		unsigned int milliSecsSinceLastSync;
	};

	// Creates client
	//
	// Blocks until it fully succeeds (or fails) which means:
	// - scanning the whole client's view
	// - establishing connection to the server
	// - exchanging initial chunk of information with the server
	DLL_API Client* Client_Create( const ClientParams& params );
	// Destroys client
	DLL_API void Client_Destroy( Client* client );
	// Saves cache information (file checksums etc.)
	// This information is then used during future sessions to speed up data synchronization.
	//
	// Note: this also gets done on exit but in the case of crash it won't happen.
	DLL_API void Client_SaveCacheInfo( Client* client );
	// Gets synchronized file path; only to be used for files
	// Note: This call can be slow depending on availability of requested file. Worst case scenario
	//	is that requested file gets downloaded from the server.
	DLL_API SyncedFilePathResult Client_GetSyncedFilePath( Client* client, const wchar_t* fullPath, wchar_t* cachePath, const int cachePathSize );
	// Checks if file (or directory) exists
	DLL_API bool Client_FileExists( Client* client, const wchar_t* fullPath );
	// Gets file (or directory) time
	DLL_API bool Client_GetFileTime( Client* client, const wchar_t* fullPath, FILETIME& fileTime );
	// Gets file size; returns -1 on failure
	DLL_API long long int Client_GetFileSize( Client* client, const wchar_t* fullPath );
	// Searches for files using given pattern
	DLL_API FileSearchIterator* Client_SearchFiles( Client* client, const wchar_t* pattern );
	// Gets client side stats
	DLL_API const ClientStats& Client_GetStats( Client* client );

	// =================================
	// Server API
	// =================================

	class Server;

	struct ServerParams : SharedParams
	{
		// Listen port of the server
		unsigned short listenPort = FILE_SYNC_DEFAULT_SERVER_LISTEN_PORT;
	};

	// Creates server
	//
	// Blocks until it fully succeeds (or fails) to scan local file system.
	// Once started it creates 1 thread for to manage connections and 1 thread per client to manage file synchronization with individual clients.
	DLL_API Server* Server_Create( const ServerParams& params );
	// Stops file server
	DLL_API void Server_Destroy( Server* server );
	// Saves cache information (file checksums etc.)
	// This information is then used during future sessions to speed up data synchronization.
	//
	// Note: this also gets done on exit but in the case of crash it won't happen.
	DLL_API void Server_SaveCacheInfo( Server* server );

	// =================================
	// File search API
	// =================================

	// Checks if there is current element valid
	DLL_API bool FileSearchIterator_HasCurrent( FileSearchIterator* searchIterator );
	// Moves to the next element
	DLL_API void FileSearchIterator_MoveToNext( FileSearchIterator* searchIterator );
	// Checks if current element is directory
	DLL_API bool FileSearchIterator_IsCurrentElementDirectory( FileSearchIterator* searchIterator );
	// Gets current element name
	DLL_API const wchar_t* FileSearchIterator_GetCurrentName( FileSearchIterator* searchIterator );
	// Gets current element last write date
	DLL_API FILETIME FileSearchIterator_GetCurrentLastWriteDate( FileSearchIterator* searchIterator );
	// Releases file search
	DLL_API void FileSearchIterator_Release( FileSearchIterator* searchIterator );

	// =================================
	// Database API
	// =================================

	// Database building parameters
	struct DatabaseParams : SharedParams
	{
		DatabaseParams()
		{
			startupChecksumRecalculationTimeLimitMilliSecs = 0; // Default to "no time limit"
		}
	};

	// Builds and saves file sync database
    // Database is a complete information on file and directory structure including sizes and checksums of all files
	// Once created, database can be loaded by clients and servers (see 'loadDatabaseDirs' parameter in ClientParams and ServerParams)
	DLL_API bool Database_Build( const DatabaseParams& params );

	// =================================
	// Logging API
	// =================================

	enum class LogLevel
	{
		Spam,
		Info,
		Warning,
		Error
	};

	typedef void ( *LogFunction )( const LogLevel level, const char* message );

	// Sets custom log function
	DLL_API void SetLogFunction( LogFunction logFunction );

	// =================================
	// Memory API
	// =================================

	typedef void* ( *AllocFunction )( size_t size );
	typedef void ( *FreeFunction )( void* ptr );

	// Sets custom memory allocation/deallocation functions
	DLL_API void SetMemoryFunctions( AllocFunction allocFunction, FreeFunction freeFunction );

	// =================================
	// Helpers
	// =================================

	DLL_API const char* SyncedFilePathResultToString( const SyncedFilePathResult result );
}