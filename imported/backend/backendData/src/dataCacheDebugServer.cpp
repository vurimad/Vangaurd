/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataCacheDebugServer.h"

#include "dependencyDatabase.h"

#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/redFileSystem/include/fileSys.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugFileServer, data, prv )
RTTI_PARENT_TYPE( data::prv::FileServer )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( DebugDepsServer, data, prv )
RTTI_PARENT_TYPE( data::prv::DepsServer )
RTTI_END_TYPE()

namespace data
{
namespace prv
{

//------------------------------------------------------------------------------

DebugFileServer::DebugFileServer()
{
}

DebugFileServer::~DebugFileServer()
{
}

bool DebugFileServer::Initialise( const red::String& params, job::Counter& counter )
{
	m_rootPath = red::AbsolutePath::CreateDirPath( params );
	m_depot = red::CreateUniquePtr< res::prv::SingleTreeResourceDepot >( m_rootPath );
	return true;
}

void DebugFileServer::Shutdown()
{
	m_depot = nullptr;
}

bool DebugFileServer::IsInitialised() const
{
	return m_rootPath.IsDirectoryPath() && m_depot;
}

Cache::Result DebugFileServer::GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder )
{
	// TODO: Enable this version once CL 2382118 is going in
#if 0
	auto remotePath = m_depot->TranslateResourcePath( path );
	auto localPath = depot.TranslateResourcePath( path );

	return Cache::Result( GFileManager->CopyFile( remotePath, localPath, true ) );
#else
	Cache::Result result{ false };

	auto localFile = depot.CreateResourceWriter( path );
	if ( !localFile )
	{
		RED_LOG_ERROR( "WindowsFileServer: Failed to open local file '%hs' for writing", path.ToDebugString() );
		return result;
	}

	auto remoteFilePath = m_depot->TranslateResourcePath( path );
	auto remoteFile = GFileManager->CreateFileReader( remoteFilePath, FOF_AbsolutePath );
	if ( !remoteFile )
	{
		RED_LOG_ERROR( "WindowsFileServer: Failed to open remote file '%hs' for reading", remoteFilePath.AsChar() );
		return result;
	}

	red::Blob buffer;
	if ( !red::LoadFileToBuffer( *remoteFile, buffer ) )
	{
		RED_LOG_ERROR( "WindowsFileServer: Failed to read from remote file '%hs'", remoteFilePath.AsChar() );
		return result;
	}

	localFile->Serialize( buffer.Data(), buffer.Size() );
	result.SetValue( !localFile->HasErrors() );
	return result;
#endif
}

//------------------------------------------------------------------------------

DebugDepsServer::DebugDepsServer()
{
}

DebugDepsServer::~DebugDepsServer()
{
}

bool DebugDepsServer::Initialise( const red::String& params, job::Counter& counter )
{
	if ( params.EndsWith( ".binary_deps" ) )
	{
		m_depsPath = red::AbsolutePath::ParseFilePath( params );
	}
	else
	{
		m_depsPath = red::AbsolutePath::ParseDirPath( params );
	}
	m_database = dependency::Database::Create( m_depsPath, 0 );
	
	if ( m_database && m_database->Load() )
	{
		red::DynArray< const dependency::ResourceInfo* > allResourceInfos = m_database->GetAllResourceInfos();
		m_prereqMap.Reserve( allResourceInfos.Size() );

		for ( const dependency::ResourceInfo* resourceInfo : allResourceInfos )
		{
			m_prereqMap.Insert( resourceInfo->GetPrerequisiteHash(), resourceInfo );
		}

		return true;
	}

	RED_LOG_ERROR( "DebugDepsServer: Failed to load dependencies from disk with path %hs", m_depsPath.AsChar() );
	return false;
}

void DebugDepsServer::Shutdown()
{
	m_database = nullptr;
}

bool DebugDepsServer::IsInitialised() const
{
	return m_database;
}

Cache::Result DebugDepsServer::GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder )
{
	Cache::Result result{ false };

	auto iter = m_prereqMap.Find( prereqHash );
	if ( iter != m_prereqMap.End() )
	{
		const auto* info = iter.Value();
		dependency::OutputFileInfo resource;
		resource.path = info->GetPath();
		resource.timestamp = info->GetTimeStamp();
		resource.hash = info->GetHash();
		resource.depot = data::DepotLocation::Source;
		outputList.PushBack( resource );
		outputList.PushBack( info->GetOutputList() );
		result.SetValue( true );
	}
	else
	{
		RED_LOG_ERROR( "DebugDepsServer: Failed to find entry for prerequisite hash '%hs'", prereqHash.ToString().AsChar() );
	}

	return result;
}

Cache::Result DebugDepsServer::GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder )
{
	Cache::Result result{ false };

	const dependency::ResourceInfo* info = m_database->GetResourceInfo( path );
	if ( info != nullptr )
	{
		dependency::OutputFileInfo resource;
		resource.path = info->GetPath();
		resource.timestamp = info->GetTimeStamp();
		resource.hash = info->GetHash();
		resource.depot = data::DepotLocation::Source;
		outputList.PushBack( resource );
		outputList.PushBack( info->GetOutputList() );
		result.SetValue( true );
	}
	else
	{
		RED_LOG_ERROR( "DebugDepsServer: Failed to find entry for path '%hs'", path.ToDebugString() );
	}

	return result;
}

} // prv
} // data
