/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"

template< typename T >
void operator<<( IFile& file,  THandle< T >& handle )
{
	if( file.IsWriter() )
	{
		serialization::IMapper::ObjectIndex objectIndex = 0;

		if( file.HasMapper() )
		{
			file.GetMapper()->MapPointer( handle, objectIndex );
		}

		file << objectIndex;
	}
	else if( file.IsReader() )
	{
		serialization::IMapper::ObjectIndex objectIndex = 0;
		file << objectIndex;

		THandle< ISerializable > object;
		if( file.HasMapper() )
		{
			file.GetMapper()->UnmapPointer( objectIndex, object );		
		}

		handle = red::StaticCast< T >( object );
	}
}

template< typename T >
void operator<<( IFile& file,  WeakHandle< T >& weakHandle )
{
	if( file.IsWriter() )
	{
		serialization::IMapper::ObjectIndex objectIndex = 0;

		if( file.HasMapper() )
		{
			THandle< T > handle = weakHandle.ToHandle();
			if( handle )
			{
				file.GetMapper()->MapPointer( handle, objectIndex );
			}
		}

		file << objectIndex;
	}
	else if( file.IsReader() )
	{
		serialization::IMapper::ObjectIndex objectIndex = 0;
		file << objectIndex;

		THandle< ISerializable > object;
		if( file.HasMapper() )
		{
			file.GetMapper()->UnmapPointer( objectIndex, object );		
		}

		weakHandle = red::StaticCast< T >( object );
	}
}
