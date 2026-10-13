/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red { class AbsolutePath; }

namespace fs
{

// Copy content of a file
RED_FILESYSTEM_API bool CopyFileContent( IFile& srcFile, IFile& destFile );

// Copy content of a file with extra parameters
RED_FILESYSTEM_API bool CopyFileContentEx( IFile& srcFile, const Uint64 srcOffset, IFile& destFile, const Uint64 destOffset, const Uint64 bytesToCopy, Uint64& outNumCopedBytes );

// This will check to make sure the provided string is a valid filename for any supported OS
RED_FILESYSTEM_API Bool IsValidFileName( const red::StringView& filename );
RED_INLINE         Bool IsValidFileName( const AnsiChar* filename ) { return IsValidFileName( red::StringView( filename ) ); }
RED_INLINE         Bool IsValidFileName( const String& filename ) { return IsValidFileName( red::StringView( filename ) ); }

RED_FILESYSTEM_API Bool IsValidFilePath( const red::AbsolutePath& path );

} // fs
