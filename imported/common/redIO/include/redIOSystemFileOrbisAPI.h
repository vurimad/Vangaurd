/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "redIOCommon.h"

//////////////////////////////////////////////////////////////////////////
// Forward Declarations
//////////////////////////////////////////////////////////////////////////
#if defined( RED_PLATFORM_ORBIS )

namespace io 
{

Uint32 LastError();

namespace orbis
{


//////////////////////////////////////////////////////////////////////////
// SystemFile
//////////////////////////////////////////////////////////////////////////
class SystemFile
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

private:
	Int32 m_sceFileHandle;

public:
											SystemFile();
											SystemFile( SystemFile&& other );
											SystemFile& operator=( SystemFile&& rhs );
											~SystemFile();

	Bool									Open( const char* path, Uint32 openFlags );
	Bool									Close();

public:
	Int32 GetFileDescriptor() const	{ return m_sceFileHandle; }

	Bool									Read( void* dest, Uint32 length, Uint32& outNumberOfBytesRead );
	Bool									Write( const void* src, Uint32 length, Uint32& outNumberOfBytesWritten );
	Bool									Seek( Int64 offset, ESeekOrigin seekOrigin );
	Int64									Tell() const;
	Bool									Truncate( Uint64 totalFileSize );

public:
	Bool									Flush();

public:
	Bool									IsValid() const;

public:
	Uint64									GetFileSize() const;
	Uint64									GetFileTimestamp() const;
	Uint32									GetFileID() const;
};

} // orbis

namespace prv
{
	using SystemFile = ::io::orbis::SystemFile;
}

} // io
#endif // #if defined( RED_PLATFORM_ORBIS )
