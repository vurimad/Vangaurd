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
#include "../../redMath/include/orientedBox.h"

namespace red
{
	class IShapeRenderer;
}

// 3D OBB
struct RED_REFLECTION_API OrientedBox : public math::OrientedBox
{
	RTTI_DECLARE_TYPE( OrientedBox );

	OrientedBox() = default;

	RED_FORCE_INLINE OrientedBox( const math::OrientedBox& cyl )
		: math::OrientedBox( cyl )
	{}

	RED_FORCE_INLINE OrientedBox( const math::Vector4& pos, const math::Vector4& forward, const math::Vector4& right, const math::Vector4& up )
		: math::OrientedBox( pos, forward, right, up )
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;

	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::OrientedBox>		{ enum { Value = true }; };
template <> struct TCopyableType<OrientedBox>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::OrientedBox>( const math::OrientedBox& )
{
	return TTypeName<OrientedBox>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, OrientedBox& val )
{
	static_assert( sizeof( val ) == 48, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::OrientedBox& val )
{
	static_assert( sizeof( val ) == 48, "" );
	file.Serialize( &val, sizeof( val ) );
}
