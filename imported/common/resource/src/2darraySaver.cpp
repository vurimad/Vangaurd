/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "2darray.h"
#include "2darraySaver.h"

void Register2DArraySaver()
{
	serialization::ISaver::RegisterCustomFactory< Saver2dArray >( "csv" );
}

Saver2dArray::Saver2dArray() = default;

Bool Saver2dArray::SaveObjects( IFile& file, const serialization::SavingContext& context ) const
{
	auto arr = Cast<C2dArray>( context.m_initialExports[0] );
	if ( !arr )
		return false;

	// Prepare data to save
	const Uint32 dataLength = arr->CalculateDataLength();
	char* data = RED_NEW_ARRAY( char, dataLength, red::PoolEngine );
	red::Memset( data, 0, dataLength * sizeof( char ) );

	arr->PrepareData( data );

	String dataToSave( data );
	file.Serialize( dataToSave.AsChar(), dataToSave.Length() );

	RED_DELETE_ARRAY( data, dataLength, red::PoolEngine );
	return true;
}
