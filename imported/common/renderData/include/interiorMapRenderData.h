/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../world/include/interiorMapCommon.h"

struct RendInteriorMapData
{
    static constexpr Uint32 c_cellDataRes = world::interior_map::c_bitsInCellXY;
    static constexpr Uint32 c_cellDataLengthU32 = c_cellDataRes * c_cellDataRes;
    
	Float areaBoundsMin[3];
    Float areaBoundsMax[3];
	Float areaCellSize[3];
	Float areaCellSizeRcp[3];
    Float bitGridSizeRcp[3];
	Uint32 bitsInCellXY;

    Uint32 cellData[3][3][3][c_cellDataLengthU32];
};
RENDER_DATA_API void RendCopyCellData( RendInteriorMapData* output, Uint32 x, Uint32 y, Uint32 z, red::ArraySpan<const Uint32> input );

using RendInteriorMapDataPtr = red::UniquePtr<RendInteriorMapData, rend::PoolEnvironment>;
