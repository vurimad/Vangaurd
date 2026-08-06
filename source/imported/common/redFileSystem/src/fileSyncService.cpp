#include "build.h"
#include "fileSyncService.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

#include "../../redCore/include/commandline.h"
#include "../../redCore/include/absolutePath.h"
#include "../../../internal/FileSync/include/API.h"

namespace
{
	file_sync::Client* s_client = nullptr;
	file_sync::Server* s_server = nullptr;
	Bool s_didCriticalErrorOccur = false;
}

FileSyncServiceConfig::Client::Client()
{
}

FileSyncServiceConfig::FileSyncServiceConfig()
{
}

void FileSyncLogFunc( const file_sync::LogLevel logLevel, const char* message )
{
	switch ( logLevel )
	{
		case file_sync::LogLevel::Spam: RED_LOG_INFO( "SPAM: FileSyncService: %s", message ); return;
		case file_sync::LogLevel::Info: RED_LOG_INFO( "FileSyncService: %s", message ); return;
		case file_sync::LogLevel::Warning: RED_LOG_WARNING( "FileSyncService: %s", message ); return;
		case file_sync::LogLevel::Error: RED_LOG_ERROR( "FileSyncService: %s", message ); return;
	}
}
	
void* FileSyncAllocFunction( size_t size )
{
	return RED_ALLOCATE( PoolFileSyncService, size );
}

void FileSyncFreeFunction( void* ptr )
{
	RED_FREE( PoolFileSyncService, ptr );
}

Bool FileSyncService::Startup( const FileSyncServiceConfig& config )
{
	if ( config.m_mode == FileSyncServiceMode::Disabled )
	{
		return true;
	}

	InitializeFileSyncMemoryPools();

	RED_ASSERT( !IsClient() && !IsServer() );

	// Set up file sync callbacks

	file_sync::SetMemoryFunctions( FileSyncAllocFunction, FileSyncFreeFunction );
	file_sync::SetLogFunction( FileSyncLogFunc );

	// Determine subdirectories to scan

	const UniChar* rootDirs[] =
	{
		config.m_engineRoot.AsChar(),
		config.m_gameRoot.AsChar(),
		nullptr
	};

	// Start file server or file client if requested

	switch ( config.m_mode )
	{
		case FileSyncServiceMode::Server:
		{
			file_sync::ServerParams params;
			params.localRootDirs = rootDirs;
			
			s_server = file_sync::Server_Create( params );
			return s_server != nullptr;
		}

		case FileSyncServiceMode::Client:
		{
			file_sync::ClientParams params;
			params.localRootDirs = rootDirs;
			params.cacheDir = config.m_client.m_cacheDir.AsChar();
			params.serverIP = config.m_client.m_serverIP;
			params.serverPort = config.m_client.m_serverPort;
			
			s_client = file_sync::Client_Create( params );
			return s_client != nullptr;
		}

	}

	return true;
}
	
void FileSyncService::Shutdown()
{
	if ( IsClient() )
	{
		file_sync::Client_Destroy( s_client );
		s_client = nullptr;
	}
	else if ( IsServer() )
	{
		file_sync::Server_Destroy( s_server );
		s_server = nullptr;
	}
}

void FileSyncService::SaveCacheInfo()
{
	if ( IsClient() )
	{
		file_sync::Client_SaveCacheInfo( s_client );
	}
	else if ( IsServer() )
	{
		file_sync::Server_SaveCacheInfo( s_server );
	}
}

void FileSyncService::OnCriticalError()
{
	s_didCriticalErrorOccur = true;
}

Bool FileSyncService::DidCriticalErrorOccur()
{
	return s_didCriticalErrorOccur;
}

Bool FileSyncService::IsClient()
{
	return s_client != nullptr;
}

Bool FileSyncService::IsServer()
{
	return s_server != nullptr;
}

file_sync::Client* FileSyncService::GetClient()
{
	RED_ASSERT( s_client );
	return s_client;
}

file_sync::Server* FileSyncService::GetServer()
{
	RED_ASSERT( s_server );
	return s_server;
}
	
#else

RED_NO_EMPTY_FILE()

#endif
