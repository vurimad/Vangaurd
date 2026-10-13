/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "hashHelper.h"
#include "../../redFileSystem/include/fileSys.h"

namespace
{
	// NOTE 4k aligned based on file size (up to 256k),
	// NOTE 256k read buffer outperforming 64k and 128k for Win10/SSD 
	static constexpr Uint32 g_bufferSize = RED_KILO_BYTE( 256 );
	static constexpr Uint32 g_bufferAlignment = RED_KILO_BYTE( 4 );
}

namespace red {

THash64 CalculateHash64ForFile( const red::AbsolutePath& path, THash64 hash )
{
	const auto file = GFileManager->CreateFileReader( path ); // NOTE reading entire file, buffering not required
	if( !file )
	{
		RED_LOG_ERROR( "Could not create file reader for path '%hs'.", path.AsChar() );
		return hash;
	}

	const auto fileSize = file->GetSize();
	if( fileSize <= 0 )
	{
		return hash;
	}

	const auto bufferSize = static_cast< Uint32 >( math::Min( red::AlignUp( fileSize, static_cast< Uint64 >( g_bufferAlignment ) ), static_cast< Uint64 >( g_bufferSize ) ) );
	red::Blob buffer{ bufferSize, g_bufferAlignment };

	hash = CalculateHash64ForFile( path, buffer, hash );
	return hash;
}

THash64 CalculateHash64ForFile( const red::AbsolutePath& path, red::Blob& buffer, THash64 hash )
{
	LoadFileToBufferChunked( path, buffer, [&buffer, &hash]( const auto readSize ) {
		hash = ACalcBufferHash64Merge( buffer.Data(), readSize, hash );
	} );
	return hash;
}

THash64 CalculateHash64ForDirectory( const red::AbsolutePath& path, const THash64 hash )
{
	return CalculateHash64ForDirectory( path, "*.*", hash );
}

THash64 CalculateHash64ForDirectory( const red::AbsolutePath& path, const char* filter, THash64 hash )
{
	// Get shader files
	red::DynArray< red::AbsolutePath > files{ red::PoolEngine() };
	GFileManager->FindFiles( path, red::String{ filter }, files, true );

	// Sort files to ensure determinism
	// Case insensitive, currently for SN-DBS which can lower case file names when sandboxed, but possibly for other tools in the future
	std::sort( files.Begin(), files.End(), []( const red::AbsolutePath& a, const red::AbsolutePath& b ) {
		return StrcmpNC( a.AsChar(), b.AsChar() ) < 0;
	} );

	// Check for accidental non-determinism
	for( Uint32 i = 1; i < files.Size(); ++i )
	{
		RED_FATAL_ASSERT(red::StrcmpNC(files[i - 1].AsChar(), files[i].AsChar()) != 0, "File names '%s' and '%s' are not unique (case insensitive match)",
			files[i - 1].AsChar(), files[i].AsChar());
	}

	// Calculate merged Hash
	Blob buffer{ g_bufferSize, g_bufferAlignment };
	for( auto& filePath : files )
	{
		hash = CalculateHash64ForFile( filePath, buffer, hash );
	}

	return hash;
}

Bool WriteHash64ToFile( const red::AbsolutePath& destination, THash64 hash )
{
	auto file = GFileManager->CreateFileWriter( destination );

	if( file )
	{
		file->Serialize( &hash, sizeof( THash64 ) );

		return !file->HasErrors();
	}

	return false;
}

Bool ReadHash64FromFile( const red::AbsolutePath& source, THash64& hash )
{
	auto file = GFileManager->CreateFileReader( source );

	if( file )
	{
		file->Serialize( &hash, sizeof( THash64 ) );

		return !file->HasErrors();
	}

	return false;
}

} // namespace red
