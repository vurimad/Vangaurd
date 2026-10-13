#pragma once

#include "../../../common/renderData/include/renderVertices.h"

namespace DebugMeshBuilder
{
	void BuildBox( const Box& bbox, const Color& color, red::DynArray< DebugVertex >& vertices, red::DynArray< Uint32 >& indices );
	void BuildSphere( Float radius, const Color& color, red::DynArray< DebugVertex >& vertices, red::DynArray< Uint32 >& indices, const Vector4& position = Vector4::ZERO_3D_POINT() );
	void BuildPlane( Float density, RectF dim, red::DynArray< DebugVertex >& outVertices, red::DynArray< Uint32 >& outIndices );
};

