/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// must match CullObject in shader (currently in include_dismemberment.fx)
struct RenderDismCullObject
{
    Uint16 planeA16[4] = {};
    Uint16 planeB16[4] = {};
    Float  capA[3] = {};
    Float  capR = 0.f;
    Float  capB[3] = {};
    Float  garmentMorphStrength = 0.f;
    Float  collapsePos[3] = {};
    Uint32 cullType = 0;

    RENDER_DATA_API void SetPlaneA( const Vector4& plane );
    RENDER_DATA_API void SetPlaneB( const Vector4& plane );
    RENDER_DATA_API void SetCapR( Float value );
    RENDER_DATA_API void SetGarmentMorphStrength( Float value );
};

struct RenderDismCullObjectCB
{
	static constexpr Uint32 MAX_CULL_OBJECTS = 8;
	RenderDismCullObject cullObjects[MAX_CULL_OBJECTS] = {};
	Uint32 numCullObjects = 0;
	Uint32 optionOnlyGarment = 0;
	Uint32 optionGarmentMorphSkinnedVertices = 0;
};

RENDER_DATA_API Uint32 RenderDismGetCount( const RenderDismCullObjectCB& data );
RENDER_DATA_API RenderDismCullObject* RenderDismAddObject( RenderDismCullObjectCB* data );
RENDER_DATA_API Vector4 RenderDismPackParams( const RenderDismCullObjectCB& data );
