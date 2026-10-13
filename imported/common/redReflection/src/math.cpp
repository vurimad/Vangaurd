/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathUtils.h"
#include "rttiClassBuilder.h"

using red::DynArray;

RTTI_BEGIN_NODEFAULT_TYPE( Vector3 );
	RTTI_PROPERTY( X ).setName( "X" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( Y ).setName( "Y" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( Z ).setName( "Z" ).instanceEditable().replicated().persistent();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Vector2 );
	RTTI_PROPERTY( X ).setName( "X" ).instanceEditable().replicated();
	RTTI_PROPERTY( Y ).setName( "Y" ).instanceEditable().replicated();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Color );
	RTTI_PROPERTY( R ).setName( "Red" ).instanceEditable().range(0,255).replicated();
	RTTI_PROPERTY( G ).setName( "Green" ).instanceEditable().range(0,255).replicated();
	RTTI_PROPERTY( B ).setName( "Blue" ).instanceEditable().range(0,255).replicated();
	RTTI_PROPERTY( A ).setName( "Alpha" ).instanceEditable().range(0,255).replicated();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( HDRColor );
RTTI_ALIGN_TYPE( 16 );
	RTTI_PROPERTY( X ).setName( "Red" ).editable().replicated();
	RTTI_PROPERTY( Y ).setName( "Green" ).editable().replicated();
	RTTI_PROPERTY( Z ).setName( "Blue" ).editable().replicated();
	RTTI_PROPERTY( W ).setName( "Alpha" ).editable().replicated();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE(ColorBalance);
RTTI_ALIGN_TYPE(16);
	RTTI_PROPERTY(X).setName("Red").editable().replicated();
	RTTI_PROPERTY(Y).setName("Green").editable().replicated();
	RTTI_PROPERTY(Z).setName("Blue").editable().replicated();
	RTTI_PROPERTY(W).setName("Luminance").editable().replicated();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Rect );
	RTTI_PROPERTY( m_left ).setName( "m_left" ).editable();
	RTTI_PROPERTY( m_top ).setName( "m_top" ).editable();
	RTTI_PROPERTY( m_right ).setName( "m_right" ).editable();
	RTTI_PROPERTY( m_bottom ).setName( "m_bottom" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( RectF );
	RTTI_PROPERTY( m_left ).setName( "Left" ).editable();
	RTTI_PROPERTY( m_top ).setName( "Top" ).editable();
	RTTI_PROPERTY( m_right ).setName( "Right" ).editable();
	RTTI_PROPERTY( m_bottom ).setName( "Bottom" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Sphere );
	RTTI_PROPERTY( CenterRadius ).setName( "CenterRadius2" ).editable();

	// Sphere functions
	RTTI_NATIVE_STATIC_FUNCTION( "IntersectRay", funcIntersectRay );
	RTTI_NATIVE_STATIC_FUNCTION( "IntersectEdge", funcIntersectEdge );
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Plane );
	RTTI_PROPERTY( NormalDistance ).setName( "NormalDistance" ).editable();
RTTI_END_TYPE();


RTTI_BEGIN_NODEFAULT_TYPE( ConvexHull );
	RTTI_PROPERTY( m_planes ).setName( "planes" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( OrientedBox );
	RTTI_PROPERTY( m_position ).setName( "position" ).editable();
	RTTI_PROPERTY( m_edge1 ).setName( "edge 1" ).editable();
	RTTI_PROPERTY( m_edge2 ).setName( "edge 2" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Box );
	RTTI_PROPERTY( Min ).setName( "Min" ).editable();
	RTTI_PROPERTY( Max ).setName( "Max" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Cylinder );
	RTTI_PROPERTY( m_positionAndRadius ).setName( "positionAndRadius" ).editable();
	RTTI_PROPERTY( m_normalAndHeight ).setName( "normalAndHeight" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Tetrahedron );
	RTTI_PROPERTY( m_points[0] ).setName( "point1" ).editable();
	RTTI_PROPERTY( m_points[1] ).setName( "point2" ).editable();
	RTTI_PROPERTY( m_points[2] ).setName( "point3" ).editable();
	RTTI_PROPERTY( m_points[3] ).setName( "point4" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( CutCone );
	RTTI_PROPERTY( m_positionAndRadius1 ).setName( "positionAndRadius1" ).editable();
	RTTI_PROPERTY( m_normalAndRadius2 ).setName( "normalAndRadius2" ).editable();
	RTTI_PROPERTY( m_height ).setName( "height" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( FixedCapsule );
	RTTI_PROPERTY( PointRadius ).setName( "PointRadius" ).editable();
	RTTI_PROPERTY( Height ).setName( "Height" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Segment );
	RTTI_PROPERTY( m_origin ).setName( "origin" ).editable();
	RTTI_PROPERTY( m_direction ).setName( "direction" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Quad );
	RTTI_PROPERTY( m_points[0] ).setName( "p1" ).editable();
	RTTI_PROPERTY( m_points[1] ).setName( "p2" ).editable();
	RTTI_PROPERTY( m_points[2] ).setName( "p3" ).editable();
	RTTI_PROPERTY( m_points[3] ).setName( "p4" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Point );
	RTTI_PROPERTY( x ).setName( "x" ).editable();
	RTTI_PROPERTY( y ).setName( "y" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( Point3D );
	RTTI_PROPERTY( x ).setName( "x" ).editable();
	RTTI_PROPERTY( y ).setName( "y" ).editable();
	RTTI_PROPERTY( z ).setName( "z" ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( QsTransform );
	RTTI_PROPERTY( Translation ).setName( "Translation" ).editable();
	RTTI_PROPERTY( Rotation ).setName( "Rotation" ).editable();
	RTTI_PROPERTY( Scale ).setName( "Scale" ).editable();
RTTI_END_TYPE();

RED_INLINE Float GetOXAngle( const Vector4& p )
{
	return atan2f( p.Y, p.X ) * 180.0f / 3.14159265f + 180.0f;
}

void Compute2DConvexHull( DynArray<Vector4>& points )
{
	// Find the rightmost point with the lowest y coordinate
	Uint32 minY = 0;
	for ( Uint32 i=0; i<points.Size(); ++i )
	{
		if ( points[i].Y < points[minY].Y )
		{
			minY = i;
		}
		else if  ( points[i].Y == points[minY].Y && points[i].X > points[minY].X )
		{
			minY = i;
		}
	}

	// add it to the end so we will see if we are finished collecting convex hull
	Vector4 minYPoint = points[ minY ];
	points.PushBack( minYPoint );

	const Uint32 N = points.Size()-1;

	// package wrapping algorithm
	Uint32 min = minY;
	Float lastAngle;
	Float currentMinAngle = -1.0f;
	Float currentMaxDistance = 0.0f;
	for ( Uint32 i=0; i<points.Size()-1; ++i )
	{
		lastAngle = currentMinAngle;
		currentMinAngle = 360.0f;
		currentMaxDistance = 0.0f;
		std::swap( points[min], points[i] );
		if ( i > 0 && points[i].Y == points[0].Y ) 
		{
			min = N;
		}
		else for ( Uint32 j=i+1; j<points.Size(); ++j )
		{
			Vector4 offsetPos = points[j] - points[i];
			offsetPos.X *= -1.0f;
			if ( offsetPos == Vector4::ZEROS() ) continue;
			const Float angle = ( 360.0f - GetOXAngle( offsetPos ) ); 
			const Float distance = offsetPos.AsVector2().SquareMag();
			if ( ( ( angle < currentMinAngle ) || ( MAbs( angle - currentMinAngle ) < 0.01f ) ) && angle > lastAngle )
			{
				if( MAbs( angle - currentMinAngle ) > 0.01f || currentMaxDistance < distance )
				{
					currentMaxDistance = distance;
					currentMinAngle = angle;
					min = j;
				}
			}
		}
		if ( min == N )
		{
			points.Resize( i+1 );
			return;
		}
	}
}


Bool IsPointInsideConvexShape( const Vector4 & point, const DynArray< Vector4 > & shape )
{
	RED_ASSERT( shape.Size() > 2 );

	Uint32   lastVert  = shape.Size() - 1;
	Vector4 lastCross = Vector4::Cross( shape[0]-shape[lastVert], point-shape[lastVert] );

	for ( Uint32 i = 0; i < lastVert; ++i )
	{
		const Vector4 & p0 = shape[ i ];
		const Vector4 & p1 = shape[ i + 1 ];

		Vector4 cross = Vector4::Cross( p1-p0, point-p0 );

		if ( Vector4::Dot3( cross, lastCross ) < 0 )
			return false;
	}

	return true;
}

void RegisterMathTypeAliases()
{
	RTTI_REGISTER_TYPE_WRAPPER( math::Box, Box ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Color, Color ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::CutCone, CutCone ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Cylinder, Cylinder ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::EulerAngles, EulerAngles ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::FixedCapsule, FixedCapsule ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Matrix, Matrix ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::OrientedBox, OrientedBox ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Plane, Plane ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Point, Point ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Quad, Quad ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Quaternion, Quaternion ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Rect, Rect ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::RectF, RectF ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Segment, Segment ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Sphere, Sphere ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Tetrahedron, Tetrahedron ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Vector2, Vector2 ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Vector3, Vector3 ); 
	RTTI_REGISTER_TYPE_WRAPPER( math::Vector4, Vector4 );
	RTTI_REGISTER_TYPE_WRAPPER( math::Transform, Transform );

	RTTI_REGISTER_TYPE_WRAPPER( math::WorldPosition::FixedPoint, FixedPoint );
	RTTI_REGISTER_TYPE_WRAPPER( math::WorldPosition, WorldPosition );
	RTTI_REGISTER_TYPE_WRAPPER( math::WorldTransform, WorldTransform );

	RTTI_REGISTER_TYPE_WRAPPER( simd::Vector4, Vector4 );	// TODO: this is workaround becuase simd::Vector4 is not in rtti system, it should be added to that system in the future
	RTTI_REGISTER_TYPE_WRAPPER( simd::QsTransform, QsTransform );
}

