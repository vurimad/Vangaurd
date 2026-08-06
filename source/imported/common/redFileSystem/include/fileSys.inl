/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace red
{

// Resizes the provided buffer instance. Requires class with MaxSize(), Resize(), Size(), Data().
template< class TBuffer >
bool LoadFileToBuffer( const AbsolutePath& absolutePath, TBuffer& buffer, const Uint32 readSizeMax )
{
	const auto file = GFileManager->CreateFileReader( absolutePath );
	if( !file )
	{
		return false;
	}

	return LoadFileToBuffer( *file, buffer, readSizeMax );
}

// Resizes the provided buffer instance. Requires class with MaxSize(), Resize(), Size(), Data().
template< class TBuffer >
bool LoadFileToBuffer( IFile& file, TBuffer& buffer, const Uint32 readSizeMax )
{
	RED_FATAL_ASSERT( file.IsReader(), "Expected reader." );

	const auto fileSize = file.GetSize();
	const auto readSize = static_cast< Uint32 >( math::Min( fileSize, static_cast< Uint64 >( readSizeMax ) ) );
	if( buffer.MaxSize() < readSize )
	{
		RED_FATAL( "Cannot load file of size %llu into buffer of max size %u", fileSize, buffer.MaxSize() );
		return false;
	}

	buffer.Resize( readSize );
	// NOTE extra check here before potentially stomping memory
	if( buffer.Size() != readSize )
	{
		RED_FATAL( "Could not resize buffer to %u", readSize );
		return false;
	}

	file.Seek( 0 );
	file.Serialize( buffer.Data(), readSize );

	return !file.HasErrors();
}

// Does not resize the provided buffer instance. Requires class with Size(), Data(), Empty().
template< class TBuffer, typename TFunc >
bool LoadFileToBufferChunked( const AbsolutePath& path, TBuffer& buffer, TFunc&& func, const Uint64 readSizeMax )
{
	const auto file = GFileManager->CreateFileReader( path );
	if( !file )
	{
		return false;
	}

	return LoadFileToBufferChunked( *file, buffer, func, readSizeMax );
}

// Does not resize the provided buffer instance. Requires class with Size(), Data(), Empty().
template< class TBuffer, typename TFunc >
bool LoadFileToBufferChunked( IFile& file, TBuffer& buffer, TFunc&& func, const Uint64 readSizeMax )
{
	RED_FATAL_ASSERT( file.IsReader(), "Expected reader." );
	
	if( buffer.Empty() )
	{
		RED_FATAL( "Provided buffer must not be empty." );
		return false;
	}

	const auto fileSize = file.GetSize();
	const auto readSize = math::Min( fileSize, readSizeMax );
	
	file.Seek( 0 );

	// NOTE take care not to overflow and spin here forever
	Uint64 read{};
	while( read < readSize )
	{
		const auto remaining = readSize - read;
		const auto chunkSize = math::Min( remaining, static_cast< Uint64 >( buffer.Size() ) );
		if( chunkSize <= 0 )
		{
			// NOTE avoid infinite loop
			return false;
		}

		file.Serialize( buffer.Data(), chunkSize );
		if( file.HasErrors() )
		{
			return false;
		}

		read += chunkSize;
		func( chunkSize );
	}

	return true;
}

} // red
