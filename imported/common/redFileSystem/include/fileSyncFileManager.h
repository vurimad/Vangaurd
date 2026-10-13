#pragma once

#include "system.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

#include "../../redFileSystem/include/fileSys.h"

class IFile;

// File manager override for use by clients who want to synchronize their files with the server
class RED_FILESYSTEM_API FileSyncFileManager : public CFileManager
{
public:
	FileSyncFileManager(
		const red::AbsolutePath& engineRoot, const red::AbsolutePath& gameRoot,
		const red::AbsolutePath& cachePath );
	red::UniquePtr< IFile > CreateFileReader( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags ) const override;
	Uint64 GetFileSize( const red::AbsolutePath& absoluteFilePath ) const override;
	red::DateTime GetFileTime( const char* absoluteFilePath ) const override;
	red::DateTime GetFileTime( const red::AbsolutePath& absoluteFilePath ) const override;
	Uint64 GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const override;
	Bool FileExist( const red::AbsolutePath& absoluteFilePath ) override;
	red::DynArray< String > FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const override;
	red::DynArray< String > FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const override;

	// Helper function for client side path translation (translation from desired-by-the-user to actual-file-path-to-read)
	static Bool TranslateFilePath( AnsiChar* path, const Uint32 pathSize );

private:
	red::DynArray< String > FindFilesOrDirectories( const red::AbsolutePath& searchAbsolutePath, const Bool findDirectories ) const;
	static Bool IsExcludedPath( const AnsiChar* path );
};

#endif // RED_FILE_SYNC_SERVICE_ENABLED