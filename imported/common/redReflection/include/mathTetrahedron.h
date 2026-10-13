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
#include "../../redMath/include/tetrahedron.h"

namespace red
{
	class IShapeRenderer;
}

struct RED_REFLECTION_API Tetrahedron : public math::Tetrahedron
{
	RTTI_DECLARE_TYPE( Tetrahedron );

	Tetrahedron() = default;

	RED_FORCE_INLINE Tetrahedron( const math::Tetrahedron& tetra )
		: math::Tetrahedron( tetra )
	{}

	RED_FORCE_INLINE Tetrahedron( const Vector4& pos1, const Vector4& pos2, const Vector4& pos3, const Vector4& pos4 )
		: math::Tetrahedron( pos1, pos2, pos3, pos4 )
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;

	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Tetrahedron>		{ enum { Value = true }; };
template <> struct TCopyableType<Tetrahedron>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Tetrahedron>( const math::Tetrahedron& )
{
	return TTypeName<Tetrahedron>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Tetrahedron& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Tetrahedron& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}
