/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "cutCone.h"
#include "mathUtils.h"
#include "segment.h"

namespace math
{

	EulerAngles CutCone::GetOrientation() const
	{
		return EulerAngles(
			RAD2DEG( ::atan2f( m_normalAndRadius2.X, -m_normalAndRadius2.Y ) ),
			RAD2DEG( ::atan2f( m_normalAndRadius2.Z, (Float)::sqrt( m_normalAndRadius2.X * m_normalAndRadius2.X + m_normalAndRadius2.Y * m_normalAndRadius2.Y ) ) ),
			0.0f );
	}

	Bool CutCone::Contains( const Vector4& point ) const
	{
		Float d = Vector4::Dot3( point, m_normalAndRadius2 );
		Float p = Vector4::Dot3( m_positionAndRadius1, m_normalAndRadius2 );
		Float m = d - p;
		if ( m <= 0 || m >= GetHeight() )
			return false;

		Vector4 t = m_positionAndRadius1 - point;
		Float sd = ( t - m_normalAndRadius2 * Vector4::Dot3( t, m_normalAndRadius2 ) ).SquareMag3();
		Float r = m_positionAndRadius1.W + ( m_normalAndRadius2.W - m_positionAndRadius1.W ) * m / GetHeight();
		return sd < r * r;
	}

	Bool CutCone::IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const
	{
		Vector4 d = origin - m_positionAndRadius1;
		Vector4 t1 = Vector4::Cross( d, m_normalAndRadius2 );
		Vector4 t2 = Vector4::Cross( direction, m_normalAndRadius2 );
		Float g = ( GetRadius2() - GetRadius1() ) / m_height;
		Float t3 = GetRadius1() + g * Vector4::Dot3( m_normalAndRadius2, d );
		Float t4 = g * Vector4::Dot3( direction, m_normalAndRadius2 );

		Float x1;
		Float x2;
		if ( !utils::SolveQuadraticEquation(
			Vector4::Dot3( t2, t2 ) - t4 * t4,
			2 * ( Vector4::Dot3( t1, t2 ) - t3 * t4 ),
			Vector4::Dot3( t1, t1 ) - t3 * t3,
			x1, x2
			) || t1 == t2 )
			return false;

		Float min = Min( x1, x2 );
		Float max = Max( x1, x2 );

		Float o = Vector4::Dot3( m_positionAndRadius1, m_normalAndRadius2 );
		Float s = Vector4::Dot3( origin, m_normalAndRadius2 );
		Float v = Vector4::Dot3( direction, m_normalAndRadius2 );
		if ( v == 0 )
		{
			if ( s <= o || s >= o + m_height )
				return false;
		}
		else
		{
			Float reverse = 1.0f / v;
			x1 = ( o - s ) * reverse;
			x2 = ( o + m_height - s ) * reverse;
			min = Max( min, Min( x1, x2 ) );
			max = Min( max, Max( x1, x2 ) );
		}

		if ( min >= max || max < 0 )
			return false;

		enterDistFromOrigin = min;
		return true;
	}

	Bool CutCone::IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const
	{
		Float t;
		Bool ret = IntersectRay( origin, direction, t);
		enterPoint = origin + direction * t;
		return ret;
	}

	Bool CutCone::IntersectSegment( const Segment& segment, Vector4& enterPoint ) const
	{
		Float t;
		Vector4 normal = segment.m_direction.Normalized3();
		Bool ret = IntersectRay( segment.m_origin, normal, t );
		if ( ret && t * t <= segment.m_direction.SquareMag3() )
		{
			enterPoint = segment.m_origin + normal * t;
			return true;
		}
		return false;
	}

} // math