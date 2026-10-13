/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCacheWebServiceDepsServer.h"

#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/jsonAllocator.h"

 // TODO: Make a wrapper library for CURL and use that instead of this
#include "../../../common/networkServices/include/webRequest.h"

#include <curl/curl.h>

using RapidJsonDocument = rapidjson::JSONDocument;
using RapidJsonValue = rapidjson::JSONValue;

RTTI_BEGIN_TYPE_IN_NAMESPACE( WebServiceDepsServer, data, prv )
	RTTI_PARENT_TYPE( data::prv::DepsServer )
RTTI_END_TYPE()

namespace data
{
namespace prv
{

//------------------------------------------------------------------------------

class WebServiceQuerier
{
public:
	WebServiceQuerier();
	~WebServiceQuerier();

	WebServiceQuerier( const WebServiceQuerier& ) = delete;
	WebServiceQuerier( WebServiceQuerier&& ) = delete;
	WebServiceQuerier& operator=( const WebServiceQuerier& ) = delete;
	WebServiceQuerier& operator=( WebServiceQuerier&& ) = delete;

	void InitConnection();
	void ShutConnection();

	bool Query( const red::String& queryUrl, dependency::OutputFileList& outputList );

private:
	static size_t WriteCallback( void* ptr, size_t size, size_t nmemb, void* userdata );

	CURL* m_easyHandle;

	red::DynArray< Uint8 > m_buffer;
};

WebServiceQuerier::WebServiceQuerier()
{
	InitConnection();
}

WebServiceQuerier::~WebServiceQuerier()
{
	ShutConnection();
}

void WebServiceQuerier::InitConnection()
{
	m_easyHandle = curl_easy_init();
	RED_FATAL_ASSERT( m_easyHandle != nullptr );

	curl_easy_setopt( m_easyHandle, CURLOPT_FAILONERROR, 1 );
	curl_easy_setopt( m_easyHandle, CURLOPT_HTTPAUTH, CURLAUTH_NTLM );
	curl_easy_setopt( m_easyHandle, CURLOPT_USERPWD, ":" );
	curl_easy_setopt( m_easyHandle, CURLOPT_WRITEFUNCTION, WriteCallback );
	curl_easy_setopt( m_easyHandle, CURLOPT_WRITEDATA, this );
	curl_easy_setopt( m_easyHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0" );
}

void WebServiceQuerier::ShutConnection()
{
	if ( m_easyHandle != nullptr )
	{
		curl_easy_cleanup( m_easyHandle );
	}
}

size_t WebServiceQuerier::WriteCallback( void* ptr, size_t size, size_t nmemb, void* userdata )
{
	auto self = static_cast< WebServiceQuerier* >( userdata );
	auto& buffer = self->m_buffer;
	const Uint32 realsize = static_cast< Uint32 >( size * nmemb );
	const Uint32 offset = buffer.Size();

	buffer.Resize( offset + realsize );
	red::Memcpy( buffer.TypedData() + offset, ptr, realsize );

	return realsize;
}

bool WebServiceQuerier::Query( const red::String& queryUrl, dependency::OutputFileList& outputList )
{
	m_buffer.Clear();

	curl_easy_setopt( m_easyHandle, CURLOPT_URL, queryUrl.AsChar() );

	auto result = curl_easy_perform( m_easyHandle );
	if ( result != CURLE_OK )
	{
		if ( result == CURLE_HTTP_RETURNED_ERROR )
		{
			// Recreate the CURL handle because getting a HTTP error code >= 400 will close the connection
			ShutConnection();
			InitConnection();
		}
		RED_LOG_ERROR( "WebServiceDepsServer: Failed to query '%hs' error: %hs", queryUrl.AsChar(), curl_easy_strerror( result ) );
		return false;
	}

	// TODO: This should be in another function

	RapidJsonDocument doc;
	doc.Parse( static_cast<const char*>( m_buffer.Data() ), m_buffer.Size() );
	if ( doc.HasParseError() )
	{
		RED_LOG_ERROR( "WebServiceDepsServer: Failed to parse returned JSON for query '%hs'", queryUrl.AsChar() );
		return false;
	}

	if ( !doc.IsArray() )
	{
		RED_LOG_ERROR( "WebServiceDepsServer: Invalid JSON returned for query '%hs', root is not an array", queryUrl.AsChar() );
		return false;
	}

	{
		const auto& resultArray = doc.GetArray();
		outputList.Reserve( outputList.Size() + resultArray.Size() );

		if ( resultArray.Empty() )
		{
			RED_LOG( "WebServiceDepsServer: Empty result for '%hs", queryUrl.AsChar() );
		}

		for ( const auto& elem : resultArray )
		{
			if ( !elem.HasMember( "path" ) || !elem.HasMember( "resourceTimestamp" ) || !elem.HasMember( "resourceContentHash" ) || !elem.HasMember( "resourceDepot" ) )
			{
				RED_LOG_ERROR( "WebServiceDepsServer: Invalid result object returned for query '%hs'", queryUrl.AsChar() );
				return false;
			}

			dependency::OutputFileInfo& info = outputList.EmplaceBack();

			const auto& pathValue = elem[ "path" ];
			info.path = res::ResourcePath::Build( pathValue.GetString() );

			const auto& timestampValue = elem[ "resourceTimestamp" ];
			info.timestamp = timestampValue.GetUint64();

			const auto& hashValue = elem[ "resourceContentHash" ];
			info.hash = dependency::Hash::FromEncodedString( hashValue.GetString() );

			const auto& depotValue = elem[ "resourceDepot" ];
			info.depot = data::DepotLocation::Source;
			if ( depotValue.GetString() == red::StringView( "cooked" ) )
			{
				info.depot = data::DepotLocation::Cooked;
			}
		}
	}

	return true;
}

//------------------------------------------------------------------------------

WebServiceDepsServer::WebServiceDepsServer()
{
}

WebServiceDepsServer::~WebServiceDepsServer()
{
}

bool WebServiceDepsServer::Initialise( const red::String& params, job::Counter& counter )
{
	net::services::WebRequest::InitCurl();
	m_requestRootUrl = params;
	return IsInitialised();
}

void WebServiceDepsServer::Shutdown()
{
	// TODO cleanup all per-thread downloaders here, must register them in a dynarray first though (pointers to UniquePtr?)
}

bool WebServiceDepsServer::IsInitialised() const
{
	return net::services::WebRequest::IsInitialised() && !m_requestRootUrl.Empty();
}

thread_local red::UniquePtr< WebServiceQuerier, red::PoolBackend > st_querier;

Cache::Result WebServiceDepsServer::GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder )
{
	if ( !st_querier )
	{
		st_querier = red::UniquePtr< WebServiceQuerier, red::PoolBackend >::Create();
	}

	const auto queryUrl = red::StrCat( m_requestRootUrl, "exact/", dependency::Hash::ToEncodedString( prereqHash ) );
	return Cache::Result( st_querier->Query( queryUrl, outputList ) );
}

Cache::Result WebServiceDepsServer::GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder )
{
	if ( !st_querier )
	{
		st_querier = red::UniquePtr< WebServiceQuerier, red::PoolBackend >::Create();
	}

	const auto queryUrl = String::Printf( "%hslatest/%016llX", m_requestRootUrl.AsChar(), path.GetHash() );
	return Cache::Result( st_querier->Query( queryUrl, outputList ) );
}

//------------------------------------------------------------------------------

} // prv
} // data
