/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "redIOCommon.h"

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
# include "redIOSystemFileWinAPI.h"
#elif defined( RED_PLATFORM_ORBIS )
# include "redIOSystemFileOrbisAPI.h"
#elif defined( RED_PLATFORM_LINUX )
# include "redIOSystemFileLinuxAPI.h"
#else
# error Platform unsupported
#endif

namespace io
{
/**
 * This is unbuffered, synchronous I/O. If you can use IFile, use IFile.
 * E.g., could become a drop-in replacement internal to systemWin32.cpp (and PS4).
 */

//////////////////////////////////////////////////////////////////////////
// NativeFileHandle
//////////////////////////////////////////////////////////////////////////
class REDIO_API NativeFileHandle
{
private:
	prv::SystemFile							m_file;
	Bool									m_async;

public:
											NativeFileHandle();
											NativeFileHandle( NativeFileHandle&& other );
											~NativeFileHandle();

	NativeFileHandle&						operator = ( NativeFileHandle&& other );

public:
	//! Open file for blocking I/O. Returns true upon success.
	Bool									Open( const char* path, Uint32 openFlags );

	//! Close file. Returns true upon success.
	Bool									Close();

public:
	//! Blocking read. Returns true upon success.
	Bool									Read( void* dest, Uint32 length, Uint32& /*[out]*/ outNumberOfBytesRead );

	//! Blocking write. Returns true upon success.
	Bool									Write( const void* src, Uint32 length, Uint32& /*[out]*/ outNumberOfBytesWritten );

	//! Seek to file position. Returns true upon success.
	Bool									Seek( Int64 offset, ESeekOrigin seekOrigin );

	//! Get file offset. Returns -1 upon error.
	Int64									Tell() const;

	//! Truncates the file to the requested size. Returns true upon success.
	Bool									Truncate( Uint64 totalFileSize );

public:
	//! Is the file valid
	Bool									IsValid() const;

	//! Flush to disk. Returns true upon success.
	Bool									Flush();

	//! Get file size. Returns zero upon error.
	Uint64									GetFileSize() const;

	//! Get the file timestamp. Returns zero upon error.
	Uint64									GetFileTimestamp() const;
};

} // io

