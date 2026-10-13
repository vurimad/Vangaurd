#pragma once

#include "system.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

namespace file_sync
{
	class Client;
	class Server;
}

enum class FileSyncServiceMode
{
	// Service is disabled
	Disabled,

	// Service runs as a client i.e. downloads files from the server
	Client,

	// Service runs as a server i.e. serves files to clients
	Server
};

// Parameters passed to file sync service at startup
struct RED_FILESYSTEM_API FileSyncServiceConfig
{
	struct Client
	{
		// IP of the file sync server to connect to
		Uint32 m_serverIP = 0;
		// Port of the file sync server to connect to
		Uint16 m_serverPort = 0;
		// Local cache directory to use
		Utf16String m_cacheDir;

		Client();
	};

	// Mode in which to start the service
	FileSyncServiceMode m_mode = FileSyncServiceMode::Disabled;

	// Game data root path
	Utf16String m_gameRoot;

	// Engine data root path
	Utf16String m_engineRoot;

	// Client only parameters
	Client m_client;

	FileSyncServiceConfig();
};

// Synchronizes files between server and client(s) at runtime.
//
// Used with multiplayer playtesting to assure all clients and server use the same assets.
//
// NOTES:
//	* Only used during development
//	* Internally uses external library (FileSync)
//	* Synchronized files are stored on the client side in separate cache directory.
//	* Only files that aren't present locally - either in depot or in cache - are downloaded from the server.
//	* File comparison is done by calculating checksum.
//	* Multiple files of the same name but different version may exist in the cache at the same time.
class RED_FILESYSTEM_API FileSyncService
{
public:
	// Starts the service; returns false on failure
	static Bool Startup( const FileSyncServiceConfig& config );
	// Shuts down the service
	static void Shutdown();

	// Saves cache information (info used to speed up future file sync sessions)
	static void SaveCacheInfo();

	// To be invoked when critical file sync issue occurs
	static void OnCriticalError();
	// Checks if there was critical error
	static Bool DidCriticalErrorOccur();

	// Checks if service is running as a client
	static Bool IsClient();
	// Checks if service is running as a server
	static Bool IsServer();

	// Gets client part of the service; only valid if the service is running as a client
	static file_sync::Client* GetClient();
	// Gets server part of the service; only valid if the service is running as a server
	static file_sync::Server* GetServer();
};

#endif