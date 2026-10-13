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

#include "../../redMath/include/sphere.h"

namespace red
{
	class IShapeRenderer;
}

// 3D sphere
struct RED_REFLECTION_API Sphere : public math::Sphere
{
private:
	//Number of lines for render debug vertex generation
	static constexpr Uint32 s_horizontalLines = 10;
	static constexpr Uint32 s_verticalLines = 18;
	static constexpr Uint32 s_nbGeneratedDebugVertices = (s_horizontalLines - 1) * (s_verticalLines) + 2;

public:

	RTTI_DECLARE_TYPE( Sphere );

	Sphere() = default;

	RED_FORCE_INLINE Sphere( const math::Sphere& other )
		: math::Sphere( other )
	{}

	RED_FORCE_INLINE Sphere( const math::Vector4& geometry )
		: math::Sphere( geometry )
	{}

	RED_FORCE_INLINE Sphere( const math::Vector4& center, Float radius )
		: math::Sphere( center, radius )
	{}

	RED_FORCE_INLINE Sphere( Float c0, Float c1, Float c2, Float radius )
		: math::Sphere( c0, c1, c2, radius )
	{}

	static void funcIntersectRay( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcIntersectEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	/// Generate rendering geometry in form of triangles
	void RenderSolid( red::IShapeRenderer& renderer, const Bool swap = false ) const;

	/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
	void RenderWireframe( red::IShapeRenderer& renderer ) const;

	red::StaticArray< Vector4, s_nbGeneratedDebugVertices > GenerateDebugVertices() const;

private:
	void GenerateVertices( Vector4* arrayToFill ) const;
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Sphere>	{ enum { Value = true }; };
template <> struct TCopyableType<Sphere>		{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Sphere>( const math::Sphere& )
{
	return TTypeName<Sphere>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Sphere& val )
{
	file << val.CenterRadius;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Sphere& val )
{
	file << val.CenterRadius;
}