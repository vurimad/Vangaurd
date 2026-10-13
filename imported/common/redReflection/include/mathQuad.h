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
#include "../../redMath/include/quad.h"

namespace red
{
	class IShapeRenderer;
}

struct Quad : public math::Quad
{
public:
	RTTI_DECLARE_TYPE( Quad );

	Quad() = default;

	RED_FORCE_INLINE Quad( const math::Quad& q )
		: math::Quad{ q }
	{}

	RED_FORCE_INLINE Quad( const math::Vector4& p1, const math::Vector4& p2, const math::Vector4& p3, const math::Vector4& p4 )
		: math::Quad{ p1, p2, p3, p4 }
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;

	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Quad>	{ enum { Value = true }; };
template <> struct TCopyableType<Quad>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Quad>( const math::Quad& )
{
	return TTypeName<Quad>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Quad& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Quad& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}
