/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/backendResourceDepot.h"

namespace resdb
{

bool IsTransactionArtifactPath( const red::StringView& path );

class FileSystemWatcher : public CFileManager
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
public:
	FileSystemWatcher( CFileManager* wrappedManager, res::IBackendResourceDepot* depot );
	virtual ~FileSystemWatcher();

	CFileManager* GetWrappedFileManager() const;

	red::DynArray< res::ResourcePath > GetChangedFiles();
	void PushResourcePath( const res::ResourcePath& resourcePath ) const;

	virtual red::UniquePtr<IFile> CreateFileReader( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags = 0 ) const override;
	virtual red::UniquePtr<IFile> CreateFileWriter( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags = 0 ) const override;
	virtual Uint64 GetFileSize( const red::AbsolutePath& absoluteFilePath ) const override;
	virtual red::DateTime GetFileTime( const char* absoluteFilePath ) const override;
	virtual red::DateTime GetFileTime( const red::AbsolutePath& absoluteFilePath ) const override;
	virtual Uint64 GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const override;
	virtual red::DateTime GetFolderTimestamp( const red::AbsolutePath& absoluteFilePath, Bool recursive ) const override;
	virtual void FindFiles( const red::AbsolutePath& baseDirectory, const red::String& pattern, red::DynArray< red::AbsolutePath >& absoluteFilePaths, Bool recursive ) override;
	virtual red::DynArray< String > FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const override;
	virtual red::DynArray< String > FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const override;
	virtual void FindFilesRelative( const red::AbsolutePath& rootDirectory, const red::String& subDirectory, const red::String& pattern, red::DynArray< red::String >& filePaths, Bool recursive ) override;
	virtual void FindDirectories( const red::AbsolutePath& baseDirectory, red::DynArray< red::AbsolutePath >& directoryNames ) const override;
	virtual Bool CopyFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath, Bool forceOverride ) override;
	virtual Bool MoveFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath ) override;
	virtual Bool DeleteFile( const red::AbsolutePath& absoluteFilePath ) override;
	virtual Bool IsFileReadOnly( const red::AbsolutePath& absoluteFilePath ) override;
	virtual Bool FileExist( const red::AbsolutePath& absoluteFilePath ) override;
	virtual Bool SetFileReadOnly( const red::AbsolutePath& absoluteFilePath, Bool readOnlyFlag ) override;
	virtual Bool SetFileTime( const red::AbsolutePath& absoluteFilePath, const red::DateTime& time ) const override;
	virtual Bool CreatePath( const red::AbsolutePath& absoluteFilePath ) const override;
	virtual Bool DeletePath( const red::AbsolutePath& absoluteFilePath ) const override;
	virtual red::AbsolutePath GenerateTemporaryFilePath( red::StringView subdir = red::StringView() ) const override;

	virtual const red::AbsolutePath& GetEngineRoot() const override;
	virtual const red::AbsolutePath& GetGameRoot() const override;
	virtual const red::AbsolutePath& GetCacheDirectory() const override;

private:
	void PushPath( const red::AbsolutePath& absoluteFilePath ) const;

	CFileManager* m_wrapped;
	res::IBackendResourceDepot* m_depot;
	mutable red::SpinLock m_guard;
	mutable red::DynArray< res::ResourcePath > m_resourcePaths;
};

} // resdb
