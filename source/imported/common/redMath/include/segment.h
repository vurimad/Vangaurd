/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	struct Vector4;

	// 3D segment first point is stored in m_origin, second point is m_origin + m_direction (it's more convenient for most of the computations)
	struct Segment
	{
		Vector4 m_origin;
		Vector4 m_direction;

		RED_FORCE_INLINE Segment() = default;
		RED_FORCE_INLINE Segment( const Segment& other );
		RED_FORCE_INLINE Segment( const Vector4& origin, const Vector4& direction );

		// Return translated by a vector
		RED_FORCE_INLINE Segment operator+( const Vector4& dir ) const;

		// Return translated by a -vector
		RED_FORCE_INLINE Segment operator-( const Vector4& dir ) const;

		// Translate by a vector
		RED_FORCE_INLINE void operator+=( const Vector4& dir );

		// Translate by a -vector
		RED_FORCE_INLINE void operator-=( const Vector4& dir );

		// Get segment starting position
		RED_FORCE_INLINE Vector4 GetPosition() const;

		// Calculate shortest distance between two segments
		REDMATH_API static Float SegmentDistance( const Segment& s0, const Segment& s1, Float& t0, Float& t1 );
	};

} // math

#include "segment.hpp"