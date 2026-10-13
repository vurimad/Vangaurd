/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fileSystemFilter.h"

namespace resdb
{

bool IsTransactionArtifactPath( const red::StringView& path )
{
	return path.EndsWithIgnoreCase( ".vtxn-stage" ) ||
		path.EndsWithIgnoreCase( ".vtxn-backup" ) ||
		path.EndsWithIgnoreCase( ".vtxn.stage" ) ||
		path.EndsWithIgnoreCase( ".vtxn" );
}

FileSystemWatcher::FileSystemWatcher( CFileManager* wrappedManager, res::IBackendResourceDepot* depot )
	: m_wrapped( wrappedManager )
	, m_depot( depot )
{
}

FileSystemWatcher::~FileSystemWatcher()
{
}

CFileManager* FileSystemWatcher::GetWrappedFileManager() const
{
	return m_wrapped;
}

red::DynArray< res::ResourcePath > FileSystemWatcher::GetChangedFiles()
{
	RED_SCOPE_LOCK( m_guard );
	red::DynArray< res::ResourcePath > cleanBuffer;
	cleanBuffer.Reserve( m_resourcePaths.Size() );
	return red::Exchange( m_resourcePaths, std::move( cleanBuffer ) );
}

void FileSystemWatcher::PushResourcePath( const res::ResourcePath& resourcePath ) const
{
	if ( resourcePath.IsEmpty() || IsTransactionArtifactPath( resourcePath.ToStringView() ) )
	{
		return;
	}
	RED_SCOPE_LOCK( m_guard );
	m_resourcePaths.PushBack( resourcePath );
}

void FileSystemWatcher::PushPath( const red::AbsolutePath& absoluteFilePath ) const
{
	if ( IsTransactionArtifactPath( absoluteFilePath.AsStringView() ) )
	{
		return;
	}
	auto resPath = m_depot->TranslateAbsolutePath( absoluteFilePath );
	if ( !resPath.IsEmpty() )
	{
		PushResourcePath( resPath );
	}
}

red::UniquePtr<IFile> FileSystemWatcher::CreateFileReader( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags ) const
{
	return m_wrapped->CreateFileReader( absoluteFilePath, openFlags );
}

red::UniquePtr<IFile> FileSystemWatcher::CreateFileWriter( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags ) const
{
	auto result = m_wrapped->CreateFileWriter( absoluteFilePath, openFlags );
	if ( result )
	{
		PushPath( absoluteFilePath );
	}
	return result;
}

Uint64 FileSystemWatcher::GetFileSize( const red::AbsolutePath& absoluteFilePath ) const
{
	return m_wrapped->GetFileSize( absoluteFilePath );
}

red::DateTime FileSystemWatcher::GetFileTime( const char* absoluteFilePath ) const
{
	return m_wrapped->GetFileTime( absoluteFilePath );
}

red::DateTime FileSystemWatcher::GetFileTime( const red::AbsolutePath& absoluteFilePath ) const
{
	return m_wrapped->GetFileTime( absoluteFilePath );
}

Uint64 FileSystemWatcher::GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const
{
	return m_wrapped->GetFileTimeStamp( absoluteFilePath );
}

red::DateTime FileSystemWatcher::GetFolderTimestamp( const red::AbsolutePath& absoluteFilePath, Bool recursive ) const
{
	return m_wrapped->GetFolderTimestamp( absoluteFilePath, recursive );
}

void FileSystemWatcher::FindFiles( const red::AbsolutePath& baseDirectory, const red::String& pattern, red::DynArray<red::AbsolutePath>& absoluteFilePaths, Bool recursive )
{
	m_wrapped->FindFiles( baseDirectory, pattern, absoluteFilePaths, recursive );
}

red::DynArray<String> FileSystemWatcher::FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const
{
	return m_wrapped->FindFiles( searchAbsolutePathWithPattern );
}

red::DynArray<String> FileSystemWatcher::FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const
{
	return m_wrapped->FindDirectories( searchAbsolutePathWithoutPattern );
}

void FileSystemWatcher::FindFilesRelative( const red::AbsolutePath& rootDirectory, const red::String& subDirectory, const red::String& pattern, red::DynArray<red::String>& filePaths, Bool recursive )
{
	return m_wrapped->FindFilesRelative( rootDirectory, subDirectory, pattern, filePaths, recursive );
}

void FileSystemWatcher::FindDirectories( const red::AbsolutePath& baseDirectory, red::DynArray<red::AbsolutePath>& directoryNames ) const
{
	return m_wrapped->FindDirectories( baseDirectory, directoryNames );
}

Bool FileSystemWatcher::CopyFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath, Bool forceOverride )
{
	bool result = m_wrapped->CopyFile( sourceAbsoluteFilePath, destAbsoluteFilePath, forceOverride );
	if ( result )
	{
		PushPath( destAbsoluteFilePath );
	}
	return result;
}

Bool FileSystemWatcher::MoveFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath )
{
	bool result = m_wrapped->MoveFile( sourceAbsoluteFilePath, destAbsoluteFilePath );
	if ( result )
	{
		PushPath( sourceAbsoluteFilePath );
		PushPath( destAbsoluteFilePath );
	}
	return result;
}

Bool FileSystemWatcher::DeleteFile( const red::AbsolutePath& absoluteFilePath )
{
	bool result = m_wrapped->DeleteFile( absoluteFilePath );
	if ( result )
	{
		PushPath( absoluteFilePath );
	}
	return result;
}

Bool FileSystemWatcher::IsFileReadOnly( const red::AbsolutePath& absoluteFilePath )
{
	return m_wrapped->IsFileReadOnly( absoluteFilePath );
}

Bool FileSystemWatcher::FileExist( const red::AbsolutePath& absoluteFilePath )
{
	return m_wrapped->FileExist( absoluteFilePath );
}

Bool FileSystemWatcher::SetFileReadOnly( const red::AbsolutePath& absoluteFilePath, Bool readOnlyFlag )
{
	return m_wrapped->SetFileReadOnly( absoluteFilePath, readOnlyFlag );
}

Bool FileSystemWatcher::SetFileTime( const red::AbsolutePath& absoluteFilePath, const red::DateTime& time ) const
{
	return m_wrapped->SetFileTime( absoluteFilePath, time );
}

Bool FileSystemWatcher::CreatePath( const red::AbsolutePath& absoluteFilePath ) const
{
	return m_wrapped->CreatePath( absoluteFilePath );
}

Bool FileSystemWatcher::DeletePath( const red::AbsolutePath& absoluteFilePath ) const
{
	return m_wrapped->DeletePath( absoluteFilePath );
}

red::AbsolutePath FileSystemWatcher::GenerateTemporaryFilePath( red::StringView subdir ) const
{
	return m_wrapped->GenerateTemporaryFilePath( subdir );
}

const red::AbsolutePath& FileSystemWatcher::GetEngineRoot() const
{
	return m_wrapped->GetEngineRoot();
}

const red::AbsolutePath& FileSystemWatcher::GetGameRoot() const
{
	return m_wrapped->GetGameRoot();
}

const red::AbsolutePath & FileSystemWatcher::GetCacheDirectory() const
{
	return m_wrapped->GetCacheDirectory();
}

} // resdb
