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

/********************************/
/* FixedCapsule					*/
/*	__							*/
/* /  \							*/
/* |  |	<--- Point B			*/
/* |  |							*/
/* |  |	<--- Point A			*/
/* \__/							*/
/*	 <--- Position				*/
/********************************/

#include "../../redMath/include/fixedCapsule.h"

namespace red
{
	class IShapeRenderer;
}

struct RED_REFLECTION_API FixedCapsule : public math::FixedCapsule
{
	RTTI_DECLARE_TYPE(FixedCapsule);

public:
	//Number of lines for render debug vertex generation
	static constexpr Uint32 s_horizontalLines = 6;
	static constexpr Uint32 s_verticalLines = 10;
	static constexpr Uint32 s_nbGeneratedDebugVertices = (s_horizontalLines) * (s_verticalLines) + 2;

	FixedCapsule() = default;

	RED_FORCE_INLINE FixedCapsule( const math::Vector4& point, Float radius, Float height )
		: math::FixedCapsule( point, radius, height )
	{}

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer ) const;

	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;

	red::StaticArray< Vector4, s_nbGeneratedDebugVertices > GenerateDebugVertices() const;

private:
	void GenerateVertices( Vector4* verticesUpper, Vector4* verticesLower) const;

};

// allow simplified copying of the type
template <> struct TCopyableType<math::FixedCapsule>	{ enum { Value = true }; };
template <> struct TCopyableType<FixedCapsule>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::FixedCapsule>( const math::FixedCapsule& )
{
	return TTypeName<FixedCapsule>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, FixedCapsule& val )
{
	// TODO bulk serialisation
	//static_assert( sizeof( val ) == 20, "" );
	file << val.PointRadius;
	file << val.Height;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::FixedCapsule& val )
{
	// TODO bulk serialisation
	//static_assert( sizeof( val ) == 20, "" );
	file << val.PointRadius;
	file << val.Height;
}

