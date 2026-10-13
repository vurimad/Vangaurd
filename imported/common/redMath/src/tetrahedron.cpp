#include "build.h"
#include "tetrahedron.h"
#include "segment.h"

namespace math
{
	Bool Tetrahedron::Contains( const Vector4& point ) const
	{
		Vector4 planes[4];
		CalculatePlanes( planes );

		for ( Uint32 i=0; i<RED_ARRAY_COUNT(planes); ++i )
			if ( Vector4::Dot3( point, planes[i] ) + planes[i].W >= 0 )
				return false;

		return true;
	}

	Bool Tetrahedron::IntersectSegment( const Segment& segment, Vector4& enterPoint ) const
	{
		Float t;
		const Vector4 normal = segment.m_direction.Normalized3();
		Bool ret = IntersectRay( segment.m_origin, normal, t );
		if ( ret && t * t <= segment.m_direction.SquareMag3() )
		{
			enterPoint = segment.m_origin + normal * t;
			return true;
		}

		return false;
	}

	Bool Tetrahedron::IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const
	{
		Vector4 planes[4];
		CalculatePlanes( planes );

		enterDistFromOrigin = -RED_FLT_MAX;
		for ( Uint32 i=0; i<RED_ARRAY_COUNT(planes); ++i )
		{
			const Vector4& plane = planes[i];

			Float proj = -Vector4::Dot3( plane, direction );
			if ( proj > 0.0f )
			{
				Float intersectionDistance = Vector4::Dot3( origin, plane );
				intersectionDistance += plane[3];
				intersectionDistance /= proj;

				if ( intersectionDistance > enterDistFromOrigin ) 
					enterDistFromOrigin = intersectionDistance;
			}
		}

		Vector4 enterPoint = origin + direction * enterDistFromOrigin;
		for ( Uint32 i=0; i<RED_ARRAY_COUNT(planes); ++i )
		{
			const Vector4& plane = planes[i];			
			Float dist = Vector4::Dot3( enterPoint, plane );
			dist += plane.W;
			if ( dist > 1e-6f )
				return false;
		}

		return enterDistFromOrigin > 0 ;
	}

	Bool Tetrahedron::IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const
	{
		Float t;
		Bool ret = IntersectRay( origin, direction, t);
		enterPoint = origin + direction * t;
		return ret;
	}

	Float Tetrahedron::CalcVolume() const
	{
		Matrix delta = {
			1.f, m_points[0].X, m_points[0].Y, m_points[0].Z,
			1.f, m_points[1].X, m_points[1].Y, m_points[1].Z,
			1.f, m_points[2].X, m_points[2].Y, m_points[2].Z,
			1.f, m_points[3].X, m_points[3].Y, m_points[3].Z,
		};

		return fabsf( delta.Det() / 6.f );
	}

	void Tetrahedron::CalculatePlanes( Vector4* planes ) const
	{
		planes[0] = Vector4::Cross( m_points[2] - m_points[0], m_points[1] - m_points[0] ).Normalized3();
		planes[0].W = -Vector4::Dot3( m_points[0], planes[0] );

		planes[1] = Vector4::Cross( m_points[3] - m_points[1], m_points[2] - m_points[1] ).Normalized3();
		planes[1].W = -Vector4::Dot3( m_points[1], planes[1] );

		planes[2] = Vector4::Cross( m_points[0] - m_points[2], m_points[3] - m_points[2] ).Normalized3();
		planes[2].W = -Vector4::Dot3( m_points[2], planes[2] );

		planes[3] = Vector4::Cross( m_points[1] - m_points[3], m_points[0] - m_points[3] ).Normalized3();
		planes[3].W = -Vector4::Dot3( m_points[3], planes[3] );
	}

} // math