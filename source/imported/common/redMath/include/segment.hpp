/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	RED_FORCE_INLINE Segment::Segment( const Segment& other )
		: Segment{ other.m_origin, other.m_direction }
	{}

	RED_FORCE_INLINE Segment::Segment( const Vector4& origin, const Vector4& direction )
		: m_origin( origin )
		, m_direction( direction )
	{}

	// Return translated by a vector
	RED_FORCE_INLINE Segment Segment::operator+( const Vector4& dir ) const
	{
		return { m_origin + dir, m_direction };
	}

	// Return translated by a -vector
	RED_FORCE_INLINE Segment Segment::operator-( const Vector4& dir ) const
	{
		return { m_origin - dir, m_direction };
	}

	// Translate by a vector
	RED_FORCE_INLINE void Segment::operator+=( const Vector4& dir )
	{
		m_origin += dir;
	}

	// Translate by a -vector
	RED_FORCE_INLINE void Segment::operator-=( const Vector4& dir )
	{
		m_origin -= dir;
	}

	// Get segment starting position
	RED_FORCE_INLINE Vector4 Segment::GetPosition() const
	{
		return m_origin;
	};

} // math