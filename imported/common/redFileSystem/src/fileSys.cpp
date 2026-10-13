/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "fileSys.h"
#include "filePaths.h"
#include "rawFileReader.h"
#include "rawFileWriter.h"
#include "bufferedReader.h"
#include "bufferedWriter.h"
#include "bufferedTempFile.h"
#include "memoryFileReader.h"
#include "../../redCore/include/ioProfiler.h"
#include "../../redSystem/include/guid.h"

#if defined( RED_PLATFORM_WINPC )
#	include <ShlObj.h>
#endif


using red::DynArray;
using red::String;
using red::Utf16String;


CFileManager* GFileManager;

const AnsiChar* CFileManager::UNIFYING_DIRECTORY_ROOT = "CD Projekt Red" DIRECTORY_SEPARATOR_LITERAL_STRING "Red Engine";
const red::CodePoint CFileManager::DIRECTORY_SEPARATOR( DIRECTORY_SEPARATOR_LITERAL );
const red::CodePoint CFileManager::ALTERNATIVE_DIRECTORY_SEPARATOR( ALTERNATIVE_DIRECTORY_SEPARATOR_LITERAL );
const red::CodePoint CFileManager::DIRECTORY_SEPARATOR_STRING[] = { CFileManager::DIRECTORY_SEPARATOR, '\0' };

namespace
{
	static constexpr Uint32 g_bufferSize = RED_KILO_BYTE( 64 );
	static constexpr Uint32 g_bufferAlignment = RED_KILO_BYTE( 4 );
}

const red::AbsolutePath & CFileManager::GetEngineRoot() const
{
	return m_engineRoot;
}

const red::AbsolutePath & CFileManager::GetGameRoot() const
{
	return m_gameRoot;
}

const red::AbsolutePath & CFileManager::GetCacheDirectory() const
{
	return m_cacheDirectory;
}

CFileManager::CFileManager()
{
#ifdef RED_PROFILE_FILE_SYSTEM
	// initialize profiling
	GIOProfiler.Initilize( red::AbsolutePath() );
#endif // RED_PROFILE_FILE_SYSTEM
}

CFileManager::CFileManager( const red::AbsolutePath& engineRoot, const red::AbsolutePath& gameRoot, const red::AbsolutePath& cachePath )
	: m_cacheDirectory( cachePath )
	, m_engineRoot( engineRoot )
	, m_gameRoot( gameRoot )
{
#ifdef RED_PROFILE_FILE_SYSTEM
	// initialize profiling
	GIOProfiler.Initilize( red::AbsolutePath() );
#endif // RED_PROFILE_FILE_SYSTEM
}


CFileManager::~CFileManager()
{
#ifdef RED_PROFILE_FILE_SYSTEM
	// close profiler (if not closed already)
	GIOProfiler.Shutdown();
#endif // RED_PROFILE_FILE_SYSTEM
}

red::UniquePtr<IFile> CFileManager::CreateFileReader( const red::AbsolutePath& resolvedPath, Uint32 openFlags ) const
{
	// Create read interface, IO Managed or raw
	red::UniquePtr<IFile> readerInterface = fs::RawFileReader::Create( resolvedPath );
	if ( readerInterface ) 
	{
		// Add buffering
		if ( openFlags & FOF_Buffered )
		{
			// Do not create a buffering wrapper on files with direct memory access, that's pointless
			readerInterface = red::CreateUniquePtr< fs::BufferedReader >( std::move( readerInterface ), g_bufferSize, g_bufferAlignment );
		}

		// Pre-read
		if ( openFlags & FOF_MapToMemory )
		{
			RED_ASSERT( readerInterface->GetSize() < std::numeric_limits<Uint32>::max(), "Unexpectedly large file '%hs'", readerInterface->GetFileNameForDebug() );

			Uint32 size = static_cast<Uint32>( readerInterface->GetSize() );
			auto newReaderInterface = red::CreateUniquePtr< CMemoryFileReaderWithBuffer >( size );
			readerInterface->Serialize( newReaderInterface->GetData(), size );
			readerInterface = std::move( newReaderInterface );
		}
	}

	return readerInterface;
}

red::UniquePtr<IFile> CFileManager::CreateFileWriter( const red::AbsolutePath& resolvedPath, Uint32 openFlags ) const
{
	red::UniquePtr<IFile> file;

	// Make sure path exists
	if ( !CreatePath( resolvedPath ) )
	{
		return file;
	}

	// Create the safe writer if required
	if ( openFlags & FOF_SafeWrite )
	{
		file = red::CreateUniquePtr< fs::BufferedTempFile >( resolvedPath );

		if ( file->HasErrors() )
		{
			file.Reset();
		}

		return file;
	}
	
	// Create raw writer
	bool append = ( openFlags & FOF_Append ) ? true : false;
	file = fs::RawFileWriter::Create( resolvedPath, append );
	if ( file )
	{
		// Appending, move to the file end
		if ( openFlags & FOF_Append )
		{
			file->Seek( file->GetSize() );
		}

		// Add buffering
		if ( openFlags & FOF_Buffered )
		{
			file = red::CreateUniquePtr< fs::BufferedWriter >( std::move( file ), g_bufferSize, g_bufferAlignment );
		}
	}

	return file;
}

Uint64 CFileManager::GetFileSize( const red::AbsolutePath& absoluteFilePath ) const
{
	return CSystemIO::GetFileSize( absoluteFilePath.AsChar() );
}

red::DateTime CFileManager::GetFileTime( const char* absoluteFilePath ) const
{
	return CSystemIO::GetFileTime( absoluteFilePath );
}

Bool CFileManager::SetFileTime( const red::AbsolutePath& absoluteFilePath, const red::DateTime& time ) const
{
	return CSystemIO::SetFileTime( absoluteFilePath.AsChar(), time );
}

red::DateTime CFileManager::GetFileTime( const red::AbsolutePath& absoluteFilePath ) const
{
	return CSystemIO::GetFileTime( absoluteFilePath.AsChar() );
}

Uint64 CFileManager::GetFileTimeStamp( const red::AbsolutePath& absoluteFilePath ) const
{
	return CSystemIO::GetFileTimestamp( absoluteFilePath.AsChar() );
}

red::DynArray< String > CFileManager::FindFiles( const red::AbsolutePath& searchAbsolutePathWithPattern ) const
{
	red::DynArray< String > result{ red::PoolEngine() };
	for ( CSystemFindFile findFile = searchAbsolutePathWithPattern.AsChar(); findFile; ++findFile )
	{
		// Important on the PS4 when running from local disk!
		// Otherwise infinite recursion on the current directory.
		if ( red::Strcmp( findFile.GetFileName(), "." ) == 0 || red::Strcmp( findFile.GetFileName(), ".." ) == 0 )
		{
			continue;
		}

		if ( !findFile.IsDirectory() )
		{
			result.PushBack( String( findFile.GetFileName() ) );
		}
	}
	return result;
}

red::DynArray< String > CFileManager::FindDirectories( const red::AbsolutePath& searchAbsolutePathWithoutPattern ) const
{
	const red::AbsolutePath searchDirAbsolutePathWithPattern = searchAbsolutePathWithoutPattern.AddFilePath( "*." );

	red::DynArray< String > result{ red::PoolEngine() };
	for ( CSystemFindFile findFile = searchDirAbsolutePathWithPattern.AsChar(); findFile; ++findFile )
	{
		// Important on the PS4 when running from local disk!
		// Otherwise infinite recursion on the current directory.
		if ( ( red::Strcmp( findFile.GetFileName(), "." ) == 0 ) || ( red::Strcmp( findFile.GetFileName(), ".." ) == 0 ) )
		{
			continue;
		}

		if ( findFile.IsDirectory() )
		{
			// Unique cause on PS4 sceKernelGetdents returns duplicated directories when patch (like overlay APP_HOME) is applied
			red::alg::PushBackUnique( result, String( findFile.GetFileName() ) );
		}
	}
	return result;
}

void CFileManager::FindFiles( const red::AbsolutePath& baseDirectory, const red::String& pattern, DynArray< red::AbsolutePath >& absoluteFilePaths, Bool recursive )
{
	// Search for files and directories under current search path
	const red::DynArray< String > fileSearchResult = FindFiles( baseDirectory.AddFilePath( pattern ) );

	// Grab file names
	for ( Uint32 i = 0; i < fileSearchResult.Size(); ++i )
	{
		const String& fileName = fileSearchResult[i];
		absoluteFilePaths.PushBack( baseDirectory.AddFilePath( fileName ) );
	}

	// Recurse to subdirectories directories
	if ( recursive )
	{
		// Grab list of subdirectories
		const red::DynArray< String > dirSearchResult = FindDirectories( baseDirectory );

		// Recurse down the tree
		for ( Uint32 i = 0; i < dirSearchResult.Size(); ++i )
		{
			const String& dirName = dirSearchResult[i];
			const red::AbsolutePath subdir = baseDirectory.AddDirPath( dirName );

			FindFiles( subdir, pattern, absoluteFilePaths, recursive );
		}
	}
}

void CFileManager::FindFilesRelative( const red::AbsolutePath& rootDirectory, const String& subDirectory, const String& pattern, DynArray< String >& filePaths, Bool recursive )
{
	const red::AbsolutePath basePath( rootDirectory.AddDirPath( subDirectory ) );
	const char separator = red::utils::LastSeparatorUsed( basePath );

	// Search for files and directories under current search path
	const red::DynArray< String > fileSearchResult = FindFiles( basePath.AddFilePath( pattern ) );

	// Grab file names
	for ( Uint32 i = 0; i < fileSearchResult.Size(); ++i )
	{
		const String& fileName = fileSearchResult[i];
		filePaths.PushBack( red::utils::ConcatRelativePath( subDirectory, fileName, separator ) );
	}

	// Recurse to subdirectories directories
	if ( recursive )
	{
		// Grab list of subdirectories
		const red::DynArray< String > dirSearchResult = FindDirectories( basePath );

		// Recurse down the tree
		for ( Uint32 i = 0; i < dirSearchResult.Size(); ++i )
		{
			const String& dirName = dirSearchResult[i];
			String subdir = red::utils::ConcatRelativePath( subDirectory, dirName, separator );

			FindFilesRelative( rootDirectory, subdir, pattern, filePaths, recursive );
		}
	}
}

void CFileManager::FindDirectories( const red::AbsolutePath& baseDirectory, DynArray< red::AbsolutePath >& directoryNames ) const
{
	// Grab list of subdirectories
	const red::DynArray< String > dirSearchResult = FindDirectories( baseDirectory );
	for ( const String& dirName : dirSearchResult )
	{
		directoryNames.PushBack( baseDirectory.AddDirPath( dirName ) );
	}
}

Bool CFileManager::CopyFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath, Bool forceOverride )
{
	return CSystemIO::CopyFile( sourceAbsoluteFilePath.AsChar(), destAbsoluteFilePath.AsChar(), !forceOverride );
}

Bool CFileManager::MoveFile( const red::AbsolutePath& sourceAbsoluteFilePath, const red::AbsolutePath& destAbsoluteFilePath )
{
	CSystemIO::DeleteFile( destAbsoluteFilePath.AsChar() );
	return CSystemIO::MoveFile( sourceAbsoluteFilePath.AsChar(), destAbsoluteFilePath.AsChar() );
}

Bool CFileManager::DeleteFile( const red::AbsolutePath& absoluteFilePath )
{
	return CSystemIO::DeleteFile( absoluteFilePath.AsChar() );
}

Bool CFileManager::IsFileReadOnly( const red::AbsolutePath& absoluteFilePath )
{
	return CSystemIO::IsFileReadOnly( absoluteFilePath.AsChar() );
}

Bool CFileManager::FileExist( const red::AbsolutePath& absoluteFilePath )
{
	return CSystemIO::FileExist( absoluteFilePath.AsChar() );
}

Bool CFileManager::SetFileReadOnly( const red::AbsolutePath& absoluteFilePath, Bool readOnlyFlag )
{
	return CSystemIO::SetFileReadOnly( absoluteFilePath.AsChar(), readOnlyFlag );
}

Bool CFileManager::CreatePath( const red::AbsolutePath& absoluteFilePath ) const
{
	return CSystemIO::CreatePath( absoluteFilePath.AsChar() );
}

Bool CFileManager::DeletePath( const red::AbsolutePath& absoluteFilePath ) const
{
	return CSystemIO::RemoveDirectory( absoluteFilePath.AsChar() );
}

red::DateTime CFileManager::GetFolderTimestamp( const red::AbsolutePath& absoluteFilePath, Bool recursive ) const
{
	red::DateTime latestTimestamp;

	// Grab list of subdirectories
	const red::DynArray< String > fileSearchResult = FindFiles( absoluteFilePath.AddFilePath( "*" ) );

	const Uint32 dirFilesCount = fileSearchResult.Size();
	for ( Uint32 i = 0; i < dirFilesCount; ++i )
	{
		red::DateTime fileTime = GetFileTime( absoluteFilePath.AddFilePath( fileSearchResult[ i ] ) );
		
		if ( fileTime > latestTimestamp )
		{
			latestTimestamp = fileTime;
		}
	}

	if( recursive )
	{
		const red::DynArray< String > dirSearchResult = FindDirectories( absoluteFilePath );
		for ( Uint32 i = 0; i < dirSearchResult.Size(); ++i )
		{
			const red::AbsolutePath folder = absoluteFilePath.AddDirPath( dirSearchResult[ i ] );

			red::DateTime folderTime( GetFolderTimestamp( folder, recursive ) );

			if ( folderTime > latestTimestamp )
			{
				latestTimestamp = folderTime;
			}
		}
	}

	return latestTimestamp;
}

red::AbsolutePath CFileManager::GenerateTemporaryFilePath( red::StringView subdir ) const
{
	// generate random file name (fast)
	red::GUID guid = red::GUID::Create();
	char guidStr[ RED_GUID_STRING_BUFFER_SIZE ];
	guid.ToString( guidStr, RED_GUID_STRING_BUFFER_SIZE );

	// assemble full output path
	red::AbsolutePath fullPath = red::paths::GetTempDirectory();

	if( !subdir.Empty() )
	{
		fullPath.AppendDirPath( subdir );
	}

	CSystemIO::CreateDirectory( fullPath.AsChar() );

	// add file name with .tmp extension
	String filename( guidStr, red::PoolEngine() );
	filename += ".tmp";
	fullPath.AppendFilePath( filename );
	return fullPath;
}


namespace red
{

bool LoadFileToString( const red::AbsolutePath& absolutePath, red::String& string )
{
	return LoadFileToBuffer( absolutePath, string );
}

bool LoadFileToString( IFile& file, red::String& string )
{
	return LoadFileToBuffer( file, string );
}

bool SaveStringToFile( const red::AbsolutePath& absolutePath, const red::String& string )
{
	auto file = GFileManager->CreateFileWriter( absolutePath );
	return file && SaveStringToFile( *file, string );
}

bool SaveStringToFile( IFile& file, const red::String& string )
{
	RED_FATAL_ASSERT( file.IsWriter(), "Expected writer." );

	const Uint32 size = string.Length();
	file.Serialize( const_cast< void* >( string.Data() ), size );

	return !file.HasErrors();
}

bool AppendStringToFile( const red::AbsolutePath& absolutePath, const red::String& string )
{
	auto file = GFileManager->CreateFileWriter( absolutePath, FOF_Append );
	return file && AppendStringToFile( *file, string );
}

bool AppendStringToFile( IFile& file, const red::String& string )
{
	RED_FATAL_ASSERT( file.IsWriter(), "Expected writer." );

	const Uint32 size = string.Length();
	file.Serialize( const_cast< void* >( string.Data() ), size );

	return !file.HasErrors();
}


} // red
