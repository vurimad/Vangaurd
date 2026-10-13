/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once
#include "../../redMath/include/segment.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

/// 3D segment
struct RED_REFLECTION_API Segment : public math::Segment
{
	RTTI_DECLARE_TYPE( Segment );

	RED_FORCE_INLINE Segment() = default;

	RED_FORCE_INLINE Segment( const math::Segment& other )
		: math::Segment( other )
	{}

	RED_FORCE_INLINE Segment( const math::Vector4& origin, const math::Vector4& direction )
		: math::Segment( origin, direction )
	{}

};

// allow simplified copying of the type
template <> struct TCopyableType<math::Segment>	{ enum { Value = true }; };
template <> struct TCopyableType<Segment>		{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Segment>( const math::Segment& )
{
	return TTypeName<Segment>::GetTypeName();
}