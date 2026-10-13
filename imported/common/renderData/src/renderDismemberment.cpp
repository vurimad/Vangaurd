#include "build.h"
#include "renderDismemberment.h"
#include "../../redMath/include/float16compressor.h"

static void SetPlane( Uint16 out[4], const Vector4& in )
{
	const __m128i simd = Float16Compressor::CompressSSE( in.vec );
	// actually there are no alignment requirements for this instruction
	_mm_storel_epi64( (__m128i*)out, simd );
}

void RenderDismCullObject::SetPlaneA( const Vector4& plane )
{
    SetPlane( planeA16, plane );
}
void RenderDismCullObject::SetPlaneB( const Vector4& plane )
{
    SetPlane( planeB16, plane );
}
void RenderDismCullObject::SetCapR( Float value )
{
    capR = value;
}
void RenderDismCullObject::SetGarmentMorphStrength( Float value )
{
    garmentMorphStrength = value;
}

Uint32 RenderDismGetCount( const RenderDismCullObjectCB& data )
{
	static_assert( sizeof( RenderDismCullObject ) == 64, "" );
	return data.numCullObjects;
}

RenderDismCullObject* RenderDismAddObject( RenderDismCullObjectCB* data )
{
	if( data->numCullObjects >= RenderDismCullObjectCB::MAX_CULL_OBJECTS )
		return nullptr;

	return &data->cullObjects[data->numCullObjects++];
}

Vector4 RenderDismPackParams( const RenderDismCullObjectCB& data )
{
	const Float isGarmentOnly = data.optionOnlyGarment ? 1.f : 0.f;
	const Float isGarmentOnlyOnSkinned = data.optionGarmentMorphSkinnedVertices ? 1.f : 0.f;
	return Vector4( (Float)data.numCullObjects, isGarmentOnly, isGarmentOnlyOnSkinned, 0.f );
}
