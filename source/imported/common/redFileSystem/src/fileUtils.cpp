/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "fileUtils.h"
#include "file.h"

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redContainers/include/string/stringUtils.h"

namespace fs
{

bool CopyFileContent( IFile& srcFile, IFile& destFile )
{
	const Uint64 size = srcFile.GetSize();
	Uint64 numCopied = 0;
	return CopyFileContentEx( srcFile, 0, destFile, 0, size, numCopied );
}

bool CopyFileContentEx( IFile& srcFile, const Uint64 srcOffset, IFile& destFile, const Uint64 destOffset, const Uint64 bytesToCopy, Uint64& outNumCopedBytes )
{
	RED_ASSERT( srcFile.IsReader(), "Source file should be a reader" );
	RED_ASSERT( destFile.IsWriter(), "Destination file should be a writer" );

	// Most optimal to copy in 64kb chunks, aligning them to page boundary
	// See https://technet.microsoft.com/en-us/library/cc938632.aspx
	red::UniqueBuffer buffer = red::CreateUniqueBuffer<red::PoolEngine>(RED_KILO_BYTE(64), RED_KILO_BYTE(4));
	RED_FATAL_ASSERT( buffer, "Failed to create temporary buffer for copying a file." );

	const Uint64 savedSrcOffset = srcFile.GetOffset();
	srcFile.Seek( srcOffset );

	destFile.Seek( destOffset );

	Uint64 left = bytesToCopy;
	Uint64 copied = 0;
	while ( (left != 0) && !srcFile.HasErrors() && !destFile.HasErrors() )
	{
		Uint64 maxToCopy = math::Min< Uint64 >( buffer.GetSize(), left );
		srcFile.Serialize( buffer.Get(), maxToCopy );

		if ( !srcFile.HasErrors() )
		{
			destFile.Serialize( buffer.Get(), maxToCopy );
		}

		left -= maxToCopy;
	}

	srcFile.Seek( savedSrcOffset );

	outNumCopedBytes = copied;
	return (left == 0) && !srcFile.HasErrors() && !destFile.HasErrors();
}

Bool IsValidFileName( const red::StringView& filename )
{
	// A blank filename is invalid
	if ( filename.Length() == 0 )
		return false;

	// Filenames cannot end with whitespace
	red::StringView trimmed = red::TrimRight( filename );
	if ( trimmed != filename )
		return false;

	// Filenames cannot end with '.'
	if ( filename.EndsWith( '.' ) )
		return false;

	// https://docs.microsoft.com/en-us/windows/desktop/fileio/naming-a-file
	const AnsiChar* invalidPathCharacters = R"(<>:"/\|?*)";

	Bool hasInvalidChars = filename.FindAnyOf( invalidPathCharacters ) != red::StringView::npos;

	// Potential future improvements (depends on how thorough we want to be):
	// * Ensure ascii values 0-31 do not appear as characters
	// * Check to make sure the filename is not one of windows reserved names like COM0, LPT4 etc

	return !hasInvalidChars;
}

Bool IsValidFilePath( const red::AbsolutePath& path )
{
	if ( !path.IsDirectoryPath() && !IsValidFileName( red::paths::GetFileName( path ) ) )
		return false;

	if ( path.Length() > MAX_PATH )
		return false;

	return true;
}

} // fs
