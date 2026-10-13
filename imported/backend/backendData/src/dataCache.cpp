/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCache.h"
#include "dataCacheServer.h"
#include "dataCacheWindowsFileServer.h"
#include "dataCachePostgresDepsServer.h"

#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/app/include/appPath.h"

#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/jsonAllocator.h"

namespace data
{

namespace prv
{

red::AbsolutePath GetCacheConfigPath()
{
	return app::paths::MakeConfigPath( GFileManager->GetGameRoot() ).AddFilePath( "dataCacheConfig.json" );
}

bool FindCacheConfig( const red::String& configName, CacheConfig& result )
{
	auto configFilePath = GetCacheConfigPath();
	red::String jsonString;
	if ( !red::LoadFileToString( configFilePath, jsonString ) )
	{
		return false;
	}

	rapidjson::JSONDocument doc;
	doc.Parse( jsonString.AsChar(), jsonString.Length() );
	if ( doc.HasParseError() )
	{
		return false;
	}

	if ( !doc.HasMember( configName.AsChar() ) )
	{
		return false;
	}

	const auto& configValue = doc[ configName.AsChar() ];

	{
		const auto& jsonValue = configValue[ "depsServerType" ];
		result.depsServerType = RED_NAME( red::StringView( jsonValue.GetString(), jsonValue.GetStringLength() ) );
	}

	{
		const auto& jsonValue = configValue[ "depsServerParams" ];
		result.depsServerParams = jsonValue.GetString();
	}

	{
		const auto& jsonValue = configValue[ "fileServerType" ];
		result.fileServerType = RED_NAME( red::StringView( jsonValue.GetString(), jsonValue.GetStringLength() ) );
	}

	{
		const auto& jsonValue = configValue[ "fileServerParams" ];
		result.fileServerParams = jsonValue.GetString();
	}

	return true;
}

struct ConnectContext
{
	prv::DepsServerPtr depsServer;
	prv::FileServerPtr fileServer;
};

} // prv

void Cache::Connect( const red::String& configName, job::Counter& waitCounter, red::Function< ConnectCallback > onConnect )
{
	auto context = red::CreateSharedPtr< prv::ConnectContext, red::PoolBackend >();

	job::Builder builder;

	builder.DispatchJob( "DataCache/ConnectStart", [configName, context]( const job::RunContext& runContext )
	{
		prv::CacheConfig config;
		if ( !prv::FindCacheConfig( configName, config ) )
		{
			RED_LOG_ERROR( "Failed to find the data cache configuration named '%hs' in the configuration file '%hs'", configName.AsChar(), prv::GetCacheConfigPath().AsChar() );
			return;
		}

		job::Counter counter;

		context->depsServer = prv::DepsServer::Create( config.depsServerType );
		if ( context->depsServer )
		{
			context->depsServer->Initialise( config.depsServerParams, counter );
		}

		context->fileServer = prv::FileServer::Create( config.fileServerType );
		if ( context->fileServer )
		{
			context->fileServer->Initialise( config.fileServerParams, counter );
		}

		job::Builder builder{ runContext };
		builder.DispatchWait( counter );
	} );

	builder.DispatchJob( "DataCache/ConnectEnd", [context, onConnect]( const job::RunContext& )
	{
		red::UniquePtr< data::Cache > result;
		if ( context->depsServer && context->depsServer->IsInitialised() &&
			context->fileServer && context->fileServer->IsInitialised() )
		{
			result = red::CreateUniquePtr< Cache >( std::move( context->depsServer ), std::move( context->fileServer ) );
		}
		else
		{
			if ( context->depsServer )
			{
				context->depsServer->Shutdown();
			}
			if ( context->fileServer )
			{
				context->fileServer->Shutdown();
			}
		}
		onConnect( std::move( result ) );
	} );

	waitCounter += builder.ExtractWaitCounter();
}

Cache::Cache( prv::DepsServerPtr&& depsServer, prv::FileServerPtr&& fileServer )
	: m_depsServer( std::move( depsServer ) )
	, m_fileServer( std::move( fileServer ) )
{
}

Cache::~Cache()
{
	Shutdown();
}

void Cache::Shutdown()
{
	if ( m_depsServer )
	{
		m_depsServer->Shutdown();
		m_depsServer = nullptr;
	}
	if ( m_fileServer )
	{
		m_fileServer->Shutdown();
		m_fileServer = nullptr;
	}
}

bool Cache::IsInitialised() const
{
	return m_depsServer && m_depsServer->IsInitialised() 
		&& m_fileServer && m_fileServer->IsInitialised();
}

Cache::Result Cache::GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder )
{
	return m_depsServer->GetExactOutputList( prereqHash, outputList, builder );
}

Cache::Result Cache::GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder )
{
	return m_depsServer->GetLatestOutputList( path, outputList, builder );
}

Cache::Result Cache::GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder )
{
	return m_fileServer->GetResource( path, hash, depot, builder );
}

} // data
