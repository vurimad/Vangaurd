/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dataCacheHttpFileServer.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redFileSystem/include/system.h"

// TODO: Make a wrapper library for CURL and use that instead of this
#include "../../../common/networkServices/include/webRequest.h"

#include <curl/curl.h>

RTTI_BEGIN_TYPE_IN_NAMESPACE( HttpFileServer, data, prv )
RTTI_PARENT_TYPE( data::prv::FileServer )
RTTI_END_TYPE()

namespace data
{
namespace prv
{

//------------------------------------------------------------------------------

class HttpDownloader
{
public:
	HttpDownloader();
	~HttpDownloader();

	HttpDownloader( const HttpDownloader& ) = delete;
	HttpDownloader( HttpDownloader&& ) = delete;
	HttpDownloader& operator=( const HttpDownloader& ) = delete;
	HttpDownloader& operator=( HttpDownloader&& ) = delete;

	void InitConnection();
	void ShutConnection();

	bool Download( const red::String& remoteUrl, const red::AbsolutePath& localPath );

private:
	static size_t WriteCallback( void* ptr, size_t size, size_t nmemb, void* userdata );

	CURL* m_easyHandle;

	red::DynArray< Uint8 > m_buffer;
};

HttpDownloader::HttpDownloader()
	: m_easyHandle( nullptr )
{
	InitConnection();
}

HttpDownloader::~HttpDownloader()
{
	ShutConnection();
}

void HttpDownloader::InitConnection()
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

void HttpDownloader::ShutConnection()
{
	if ( m_easyHandle != nullptr )
	{
		curl_easy_cleanup( m_easyHandle );
	}
}

size_t HttpDownloader::WriteCallback( void* ptr, size_t size, size_t nmemb, void* userdata )
{
	auto self = static_cast< HttpDownloader* >( userdata );
	auto& buffer = self->m_buffer;
	const Uint32 realsize = static_cast< Uint32 >( size * nmemb );
	const Uint32 offset = buffer.Size();

	buffer.Resize( offset + realsize );
	red::Memcpy( buffer.TypedData() + offset, ptr, realsize );

	return realsize;
}

bool HttpDownloader::Download( const red::String& remoteUrl, const red::AbsolutePath& localPath )
{
	m_buffer.Clear();

	curl_easy_setopt( m_easyHandle, CURLOPT_URL, remoteUrl.AsChar() );

	auto result = curl_easy_perform( m_easyHandle );
	if ( result != CURLE_OK )
	{
		if ( result == CURLE_HTTP_RETURNED_ERROR )
		{
			// Recreate the CURL handle because getting a HTTP error code >= 400 will close the connection
			ShutConnection();
			InitConnection();
		}
		RED_LOG_ERROR( "HttpFileServer: Failed to download '%hs' from '%hs' error: %hs", localPath.AsChar(), remoteUrl.AsChar(), curl_easy_strerror( result ) );
		return false;
	}

	auto file = GFileManager->CreateFileWriter( localPath, FOF_AbsolutePath );
	if ( !file )
	{
		RED_LOG_ERROR( "HttpFileServer: Failed to open '%hs' for writing", localPath.AsChar() );
		return false;
	}

	file->Serialize( m_buffer.TypedData(), m_buffer.DataSize() );
	if ( file->HasErrors() )
	{
		RED_LOG_ERROR( "HttpFileServer: Failed to write to file '%hs'", localPath.AsChar() );
		return false;
	}

	file.Reset();
	return true;
}


//------------------------------------------------------------------------------

HttpFileServer::HttpFileServer()
{
}

HttpFileServer::~HttpFileServer()
{
}

bool HttpFileServer::Initialise( const red::String& params, job::Counter& counter )
{
	net::services::WebRequest::InitCurl();
	m_rootUrl = params;
	if ( !m_rootUrl.EndsWith( "/" ) )
	{
		m_rootUrl.Append( '/' );
	}
	return IsInitialised();
}

void HttpFileServer::Shutdown()
{
	// TODO cleanup all per-thread downloaders here, must register them in a dynarray first though (pointers to UniquePtr?)
}

bool HttpFileServer::IsInitialised() const
{
	return net::services::WebRequest::IsInitialised() && !m_rootUrl.Empty();
}


red::String HttpFileServer::ComputeRemoteUrl( const res::ResourcePath& path, const dependency::Hash& hash ) const
{
	const Uint64 resourceHash = path.GetHash();
	const auto extension = red::paths::ExtractExtension( path.ToStringView() );
	const auto contentHashStr = dependency::Hash::ToEncodedString( hash );

	return String::Printf( "%hs%.2x/%.2x/%.16llx_%hs.%hs", 
		m_rootUrl.AsChar(),
		static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), 
		resourceHash, contentHashStr.AsChar(), extension.AsChar() );
}

thread_local red::UniquePtr< HttpDownloader, red::PoolBackend > st_downloader;

Cache::Result HttpFileServer::GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder )
{
	const auto remoteUrl = ComputeRemoteUrl( path, hash );
	const auto localPath = depot.TranslateResourcePath( path );

	if ( !st_downloader )
	{
		st_downloader = red::UniquePtr< HttpDownloader, red::PoolBackend >::Create();
	}

	Cache::Result result( st_downloader->Download( remoteUrl, localPath ) );
	return result;
}

//------------------------------------------------------------------------------

} // prv
} // data
