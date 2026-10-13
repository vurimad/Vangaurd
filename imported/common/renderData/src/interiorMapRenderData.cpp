/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "interiorMapRenderData.h"


void RendCopyCellData( RendInteriorMapData* output, Uint32 x, Uint32 y, Uint32 z, red::ArraySpan<const Uint32> input )
{
	RED_FATAL_ASSERT( input.SizeInBytes() == sizeof( output->cellData[x][y][z] ) );
	RED_FATAL_ASSERT( x < 3 && y < 3 && z < 3 );

	red::Memcpy( output->cellData[x][y][z], input.Data(), input.SizeInBytes() );
}
