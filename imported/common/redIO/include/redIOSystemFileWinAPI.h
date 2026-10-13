/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "redIOCommon.h"

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )

namespace io
{

Uint32 LastError();

namespace win32
{

//////////////////////////////////////////////////////////////////////////
// SystemFile
//////////////////////////////////////////////////////////////////////////
class SystemFile
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

private:
	HANDLE									m_hFile;

public:
											SystemFile();
											SystemFile( SystemFile&& other );
											SystemFile& operator=( SystemFile&& other );
											~SystemFile();

	Bool									Open( const char* path, Uint32 openFlags );
	Bool									Close();
	HANDLE									GetFileHandle()	const { return m_hFile; }

public:
	Bool									Read( void* dest, Uint32 length, Uint32& outNumberOfBytesRead );
	Bool									Write( const void* src, Uint32 length, Uint32& outNumberOfBytesWritten );
	Bool									Seek( Int64 offset, ESeekOrigin seekOrigin );
	Int64									Tell() const;
	Bool									Truncate( Uint64 totalFileSize );

public:
	Bool									Flush();

public:
	Bool									IsValid() const { return m_hFile != INVALID_HANDLE_VALUE; }

public:
	Uint64									GetFileSize() const;
	Uint64									GetFileTimestamp() const;
	Uint32									GetFileID() const;
};

} // win32

namespace prv
{
	using SystemFile = ::io::win32::SystemFile;
}

} // io
#endif // #if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
