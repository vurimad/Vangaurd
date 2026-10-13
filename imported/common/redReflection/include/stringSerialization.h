/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
* DrUiD
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "../../redFileSystem/include/compressedNumSerializer.h"
#include "../../redFileSystem/include/file.h"
#include "../../redContainers/include/string/stringBuffer.h"
#include "../../redContainers/include/arraySpan.h"

//////////////////////////////////////////////////////////////////////////
//
// serialize string
RED_REFLECTION_API void operator<<( IFile& file, red::Utf16String& str );

//////////////////////////////////////////////////////////////////////////
//
// serialize string ansi
RED_REFLECTION_API void operator<<( IFile& file, red::String& str );

// In order to avoid buffer overruns, the values are calculated in such a way as
// to avoid overflows (eg. divide by element size first, apply limits, and then
// multiply back up to determine number of bytes to read).
template < typename T >
void SerialiseString( IFile &file, red::ArraySpan< char > name, const Int32 readCountReq )
{
	const Int32 capacityCount = ( name.SizeInBytes() / sizeof( T ) ) - 1;
	RED_ASSERT( capacityCount > 0 );
	
	// handle empty destination and source
	const auto readCount = math::Min( capacityCount, readCountReq );
	if( readCount <= 0 )
	{
		return;
	}

	const auto readBytes = readCount * sizeof( T );
	file.Serialize( name.Data(), readBytes );

	// null terminate
	reinterpret_cast< T* >( name.Data() )[ readCount ] = T{ 0 };

	// seek past any remaining bytes that did not fit within destination buffer
	const auto remainderCount = ( readCountReq - readCount );
	if( remainderCount > 0 )
	{
		file.Seek( file.GetOffset() + ( remainderCount * sizeof( T ) ) );
	}
}

//////////////////////////////////////////////////////////////////////////
//
// serialize ArraySpan< char > (previously used StringBuffer< N > directly
// TODO disable deprecated Unicode serialisation
inline void operator<<( IFile &file, red::ArraySpan< char > name )
{
	RED_ASSERT( !file.IsWriter() );

	const auto readSizeAndType = ReadVarint_LEB128_Signed( file );
	const auto readCountReq = math::Abs( readSizeAndType );
	const auto useOldSerializationUsingUnicode = ( readSizeAndType > 0 ); 

	if( !useOldSerializationUsingUnicode )
	{
		SerialiseString< AnsiChar >( file, name, readCountReq );
	}
	else
	{
		SerialiseString< UniChar >( file, name, readCountReq );
	}
}
