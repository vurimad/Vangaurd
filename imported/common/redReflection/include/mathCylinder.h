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
/* 3D oriented Cylinder                                                 */
/************************************************************************/
#include "../../redMath/include/cylinder.h"

namespace red
{
	class IShapeRenderer;
}

struct RED_REFLECTION_API Cylinder : public math::Cylinder
{
	RTTI_DECLARE_TYPE( Cylinder );

	Cylinder() =  default;

	RED_FORCE_INLINE Cylinder( const math::Cylinder& cyl )
		: math::Cylinder( cyl )
	{}

	RED_FORCE_INLINE Cylinder( const math::Vector4& pos1, const math::Vector4& pos2, Float radius)
		: math::Cylinder( pos1, pos2, radius )
	{}

	RED_FORCE_INLINE Cylinder( const math::Vector4& pos, const math::Vector4& normal, Float radius, Float height ) //<! normal length has to be 1
		: math::Cylinder( pos, normal, radius, height )
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;
	
	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;

	void GenerateVertices(const Uint16 numPoints, Vector4* vertices) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Cylinder>	{ enum { Value = true }; };
template <> struct TCopyableType<Cylinder>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Cylinder>( const math::Cylinder& )
{
	return TTypeName<Cylinder>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Cylinder& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Cylinder& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}
