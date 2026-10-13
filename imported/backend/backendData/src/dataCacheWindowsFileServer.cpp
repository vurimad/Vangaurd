/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCacheWindowsFileServer.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/redFileSystem/include/fileSys.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( WindowsFileServer, data, prv )
RTTI_PARENT_TYPE( data::prv::FileServer )
RTTI_END_TYPE()

namespace data
{
namespace prv
{

//------------------------------------------------------------------------------

WindowsFileServer::WindowsFileServer()
{
}

WindowsFileServer::~WindowsFileServer()
{
}

bool WindowsFileServer::Initialise( const red::String& params, job::Counter& counter )
{
	m_rootPath = red::AbsolutePath::CreateDirPath( params );
	return true;
}

void WindowsFileServer::Shutdown()
{
}

bool WindowsFileServer::IsInitialised() const
{
	return m_rootPath.IsDirectoryPath();
}


red::AbsolutePath WindowsFileServer::ComputeEncodedPath( const res::ResourcePath& path, const dependency::Hash& hash ) const
{
	const Uint64 resourceHash = path.GetHash();
	const auto extension = red::paths::ExtractExtension( path.ToStringView() );
	const auto contentHashStr = dependency::Hash::ToEncodedString( hash );

	String relativePath = String::Printf( "%.2x\\%.2x\\%.16llx_%hs.%hs",
		static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), resourceHash, contentHashStr.AsChar(), extension.AsChar() );

	return m_rootPath.AddFilePath( relativePath );
}

Cache::Result WindowsFileServer::GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder )
{
	auto remotePath = ComputeEncodedPath( path, hash );
	auto localPath = depot.TranslateResourcePath( path );

	const bool fileCopied = GFileManager->CopyFile( remotePath, localPath, true );
	if ( !fileCopied )
	{
		RED_LOG_ERROR( "WindowsFileServer: Failed to copy file from `%hs` to `%hs`", remotePath.AsChar(), localPath.AsChar() );
	}

	return Cache::Result( fileCopied );
}

//------------------------------------------------------------------------------

} // prv
} // data
