#include "build.h"
#include "sphere.h"

namespace math
{
	Uint32 Sphere::IntersectLineParametric( const Vector4& origin, const Vector4& direction, Float& out_t1, Float& out_t2 ) const
	{
		const Float A = direction.Dot3( direction );
		const Float B = 2.0f * ( origin.Dot3( direction ) - direction.Dot3( CenterRadius ) );
		const Float C = origin.Dot3( origin ) + CenterRadius.Dot3( CenterRadius ) - 2.0f * origin.Dot3( CenterRadius ) - CenterRadius.W * CenterRadius.W;
    
		const Float D = B * B - 4.0f * A * C;
    
		if( D < 0.0f )
		{
			return 0;
		}

		out_t1 = ( -B - std::sqrt( D ) ) / ( 2.0f * A );

		if( std::abs( D ) < std::numeric_limits< Float >::epsilon() )
		{
			return 1;
		}

		out_t2 = ( -B + std::sqrt( D ) ) / ( 2.0f * A );

		RED_ASSERT( out_t1 < out_t2 );
		return 2;
	}

	Uint32 Sphere::IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint, Vector4& exitPoint ) const
	{
		Float t1, t2;
		const Uint32 intersectionsCount = IntersectLineParametric( origin, direction, t1, t2 );

		if( intersectionsCount == 0 )
		{
			return 0;
		}
		else if( intersectionsCount == 1 )
		{
			if( t1 < 0.0f )
			{
				return 0;
			}
			else
			{
				enterPoint = origin + direction * t1;
				return 1;
			}
		}
		else
		{
			RED_ASSERT( intersectionsCount == 2 );

			if( t1 < 0.0f && t2 < 0.0f )
			{
				return 0;
			}
			else
			{
				t1 = std::max( t1, 0.0f );
				enterPoint = origin + direction * t1;
				exitPoint = origin + direction * t2;
				return 2;
			}
		}
	}

	Uint32 Sphere::IntersectEdge( const Vector4& a, const Vector4& b, Vector4& intersectionPoint0, Vector4& intersectionPoint1 ) const
    {
        const Vector4 dir = b - a;
		Float t1, t2;
		const Uint32 intersectionsCount = IntersectLineParametric( a, dir, t1, t2 );

		if( intersectionsCount == 0 )
		{
			return 0;
		}
		else if( intersectionsCount == 1 )
		{
			if( t1 < 0.0f && t1 > 1.0f )
			{
				return 0;
			}
			else
			{
				intersectionPoint0 = a + dir * t1;
				return 1;
			}
		}
		else
		{
			RED_ASSERT( intersectionsCount == 2 );

			if( ( t1 < 0.0f && t2 < 0.0f ) || ( t1 > 1.0f && t2 > 1.0f ) )
			{
				return 0;
			}

			t1 = math::Clamp( t1, 0.0f, 1.0f );
			t2 = math::Clamp( t2, 0.0f, 1.0f );
			intersectionPoint0 = a + dir * t1;
			intersectionPoint1 = a + dir * t2;
			return 2;
		}
    }

	void Sphere::AddPoint( const Vector4& point )
	{
		if ( Contains( point ) )
			return;

		Vector4 vec = GetCenter() - point;
		const Float distanceCenterToPoint = vec.Normalize3();
		const Float newDiameter = distanceCenterToPoint + GetRadius();
		vec = vec * ( newDiameter ) * 0.5f;
		CenterRadius = point + vec;
		CenterRadius.W = newDiameter * 0.5f;
	}

} // math