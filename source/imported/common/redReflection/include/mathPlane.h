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

#include "../../redMath/include/plane.h"

// 3D plane implemented using 4D vector
struct RED_REFLECTION_API Plane : public math::Plane
{
public:
	RTTI_DECLARE_TYPE( Plane );

	RED_FORCE_INLINE Plane()
		: math::Plane()
	{}

	RED_FORCE_INLINE Plane( const math::Plane& plane )
		: math::Plane( plane )
	{}

	RED_FORCE_INLINE Plane( const math::Vector4& normal, const math::Vector4& point )
		: math::Plane( normal, point )
	{}

	RED_FORCE_INLINE Plane( const math::Vector4& normal, const Float& distance )
		: math::Plane( normal, distance )
	{}

	RED_FORCE_INLINE Plane( const math::Vector4& p1, const math::Vector4& p2, const math::Vector4& p3 )
		: math::Plane( p1, p2, p3 )
	{}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Plane>	{ enum { Value = true }; };
template <> struct TCopyableType<Plane>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Plane>( const math::Plane& )
{
	return TTypeName<Plane>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Plane& val )
{
	file << val.NormalDistance;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Plane& val )
{
	file << val.NormalDistance;
}
