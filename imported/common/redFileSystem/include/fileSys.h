/*
 * Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "file.h"
#include "filePaths.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redContainers/include/blob.h"

namespace red
{
class String;
} 

// File open flags
enum EFileOpenFlags
{
	FOF_Buffered		= RED_FLAG( 0 ),
	FOF_Compressed		= RED_FLAG( 1 ),
	FOF_Encrypted		= RED_FLAG( 2 ),
	FOF_Append			= RED_FLAG( 3 ),
	FOF_AbsolutePath	= RED_FLAG( 5 ),
	FOF_MapToMemory		= RED_FLAG( 6 ),
	FOF_SafeWrite		= RED_FLAG( 7 ),		// write to a temporary file first and only then copy to the actual file
};

/************************************************************************/
/* File manager															*/
/************************************************************************/
class RED_FILESYSTEM_API CFileManager
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	static const AnsiChar* UNIFYING_DIRECTORY_ROOT;
	static const red::CodePoint DIRECTORY_SEPARATOR;
	static const red::CodePoint DIRECTORY_SEPARATOR_STRING[];

	static const red::CodePoint ALTERNATIVE_DIRECTORY_SEPARATOR;

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
#	define DIRECTORY_SEPARATOR_LITERAL						'/'
#	define ALTERNATIVE_DIRECTORY_SEPARATOR_LITERAL			'\\'

#	define DIRECTORY_SEPARATOR_LITERAL_STRING				"/"
#	define ALTERNATIVE_DIRECTORY_SEPARATOR_LITERAL_STRING	"\\"
#else
#	define DIRECTORY_SEPARATOR_LITERAL						'\\'
#	define ALTERNATIVE_DIRECTORY_SEPARATOR_LITERAL			'/'

#	define DIRECTORY_SEPARATOR_LITERAL_STRING				"\\"
#	define ALTERNATIVE_DIRECTORY_SEPARATOR_LITERAL_STRING	"/"
#endif

	// Is given character a directory separator
	RED_FORCE_INLINE static Bool IsDirectorySeparator( red::CodePoint c ) { return c == DIRECTORY_SEPARATOR || c == ALTERNATIVE_DIRECTORY_SEPARATOR; }

	CFileManager();
	CFileManager( const red::AbsolutePath& engineRoot, const red::AbsolutePath& gameRoot, const red::AbsolutePath& cachePath );
	virtual ~CFileManager();

	// Open a file and create file reader, does not open empty files
	virtual red::UniquePtr<IFile> CreateFileReader( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags = 0 ) const;

	// Open a file and create file writer
	virtual red::UniquePtr<IFile> CreateFileWriter( const red::AbsolutePath& absoluteFilePath, Uint32 openFlags = 0 ) const;

	// Get size of the file
	virtual Uint64 GetFileSize( const red::AbsolutePath& absoluteFilePath ) const;

	// Get file timestamp
	virtual red::DateTime GetFileTime( const char* absoluteFilePath ) const;

	// Get file timestamp
	virtual red::DateTime GetFileTime( const red::AbsolutePath& absoluteFilePath ) const;

	// Get last modified file time as a count of 10ns ticks since 1/1/1601
	virtual Uint64 GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const;

	// Get latest timestamp in folder
	virtual red::DateTime GetFolderTimestamp( const red::AbsolutePath& absoluteFilePath, Bool recursive ) const;

	// Find files at given directory using given pattern
	virtual void FindFiles( const red::AbsolutePath& baseDirectory, const red::String& pattern, red::DynArray< red::AbsolutePath >& absoluteFilePaths, Bool recursive );

	// Gets names (not full paths!) of files at given location
	virtual red::DynArray< String > FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const;

	// Gets names (not full paths!) of directories at given location
	virtual red::DynArray< String > FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const;

	// Find files relative to rootDirectory at given directory (root + sub) using given pattern
	virtual void FindFilesRelative( const red::AbsolutePath& rootDirectory, const red::String& subDirectory, const red::String& pattern, red::DynArray< red::String >& filePaths, Bool recursive );

	// Find directories at given directory
	virtual void FindDirectories( const red::AbsolutePath& baseDirectory, red::DynArray< red::AbsolutePath >& directoryNames ) const;

	// Copy file from source location to destination location
	virtual Bool CopyFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath, Bool forceOverride );

	// Move file from source location to destination location
	virtual Bool MoveFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath );

	//! Delete file
	virtual Bool DeleteFile( const red::AbsolutePath& absoluteFilePath );

	// Check if the file is read only
	virtual Bool IsFileReadOnly( const red::AbsolutePath& absoluteFilePath );

	// Check if the file exist
	virtual Bool FileExist( const red::AbsolutePath& absoluteFilePath );

	// Set or remove read only flag from file
	virtual Bool SetFileReadOnly( const red::AbsolutePath& absoluteFilePath, Bool readOnlyFlag );

	// Set file write time
	virtual Bool SetFileTime( const red::AbsolutePath& absoluteFilePath, const red::DateTime& time ) const;

	// Create path
	virtual Bool CreatePath( const red::AbsolutePath& absoluteFilePath ) const;

	// Delete path
	virtual Bool DeletePath( const red::AbsolutePath& absoluteFilePath ) const;

	//! Generate temporary file path
	virtual red::AbsolutePath GenerateTemporaryFilePath( red::StringView subdir = red::StringView() ) const;

	// Get engine root directory, NOT THE DEPOT DIRECTORY!!!
	virtual const red::AbsolutePath& GetEngineRoot() const;

	// Get game root directory (r6), IT'S NOT NOT THE DEPOT DIRECTORY!!! 
	virtual const red::AbsolutePath& GetGameRoot() const;

	// Get cache directory
	virtual const red::AbsolutePath& GetCacheDirectory() const;

private:
	// generic cache directory
	red::AbsolutePath m_cacheDirectory;

	// engine root directory
	red::AbsolutePath m_engineRoot;

	// game root directory
	red::AbsolutePath m_gameRoot;
};

// File manager instance
extern RED_FILESYSTEM_API CFileManager* GFileManager;

namespace red
{

// Resizes the provided string instance.
RED_FILESYSTEM_API bool LoadFileToString( const red::AbsolutePath& absolutePath, red::String& string );

// Resizes the provided string instance.
RED_FILESYSTEM_API bool LoadFileToString( IFile& file, red::String& string );

// Resizes the provided buffer instance. Requires MaxSize(), Resize(), Size(), Data().
template< class TBuffer >
bool LoadFileToBuffer( const red::AbsolutePath& absolutePath, TBuffer& buffer, Uint32 readSizeMax = std::numeric_limits< Uint32 >::max() );

// Resizes the provided buffer instance. Requires MaxSize(), Resize(), Size(), Data().
template< class TBuffer >
bool LoadFileToBuffer( IFile& file, TBuffer& buffer, Uint32 readSizeMax = std::numeric_limits< Uint32 >::max() );

// Does not resize the provided buffer instance. Requires Size(), Data(), Empty().
template< class TBuffer, typename TFunc >
bool LoadFileToBufferChunked( const red::AbsolutePath& path, TBuffer& buffer, TFunc&& func, Uint64 readSizeMax = std::numeric_limits< Uint64 >::max() );

// Does not resize the provided buffer instance. Requires Size(), Data(), Empty().
template< class TBuffer, typename TFunc >
bool LoadFileToBufferChunked( IFile& file, TBuffer& buffer, TFunc&& func, Uint64 readSizeMax = std::numeric_limits< Uint64 >::max() );

RED_FILESYSTEM_API bool SaveStringToFile( const red::AbsolutePath& absolutePath, const red::String& string );

RED_FILESYSTEM_API bool SaveStringToFile( IFile& file, const red::String& string );

RED_FILESYSTEM_API bool AppendStringToFile( const red::AbsolutePath& absolutePath, const red::String& string );

RED_FILESYSTEM_API bool AppendStringToFile( IFile& file, const red::String& string );

} // red

#include "fileSys.inl"
