/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

/************************************************************************/
/* CutCone                                                              */
/************************************************************************/
#include "../../redMath/include/cutCone.h"

namespace red
{
	class IShapeRenderer;
}

struct RED_REFLECTION_API CutCone : public math::CutCone
{
	RTTI_DECLARE_TYPE( CutCone );

	CutCone() = default;

	RED_FORCE_INLINE CutCone( const math::CutCone& cone )
		: math::CutCone( cone )
	{}

	RED_FORCE_INLINE CutCone( const math::Vector4& pos1, const math::Vector4& pos2, Float radius1, Float radius2 )
		: math::CutCone( pos1, pos2, radius1, radius2 )
	{}

	RED_FORCE_INLINE CutCone( const math::Vector4& pos, const math::Vector4& normal, Float radius1, Float radius2, Float height )
		: math::CutCone( pos, normal, radius1, radius2, height )
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;
	
	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;

	void GenerateVertices(const Uint16 numPoints, Vector4* vertices) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::CutCone>		{ enum { Value = true }; };
template <> struct TCopyableType<CutCone>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::CutCone>( const math::CutCone& )
{
	return TTypeName<CutCone>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, CutCone& val )
{
	// TODO bulk serialisation
	//static_assert( sizeof( val ) == 36, "" );
	file << val.m_positionAndRadius1;
	file << val.m_normalAndRadius2;
	file << val.m_height;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::CutCone& val )
{
	// TODO bulk serialisation
	//static_assert( sizeof( val ) == 36, "" );
	file << val.m_positionAndRadius1;
	file << val.m_normalAndRadius2;
	file << val.m_height;
}
