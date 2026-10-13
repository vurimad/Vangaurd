/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "mathCommon.h"

namespace MathUtils
{
	extern RED_REFLECTION_API Float Sign( const Float x );
	extern RED_REFLECTION_API Float CyclicDistance( Float a, Float b, Float range = 1.f );

	extern RED_REFLECTION_API void CartesianFromSpherical(Vector3& sphericalInCartesianOut);
	extern RED_REFLECTION_API void SphericalFromCartesian(Vector3& cartesianInSphericalOut);

namespace VectorUtils
{
	extern RED_REFLECTION_API Float GetAngleRadBetweenVectors( const Vector4& from, const Vector4& to );

	extern RED_REFLECTION_API Float GetAngleDegBetweenVectors( const Vector4& from, const Vector4& to );

	extern RED_REFLECTION_API Float GetAngleRadAroundAxis( const Vector4& dirA, const Vector4& dirB, const Vector4& axis );

	extern RED_REFLECTION_API Float GetAngleDegAroundAxis( const Vector4& dirA, const Vector4& dirB, const Vector4& axis );
}

namespace GeometryUtils
{
	// taken "original" vector as input, generates two other vectors so that all are orthogonal to each other
	extern RED_REFLECTION_API void GenerateOrthogonalVectors( const Vector3& original, Vector3& a, Vector3& b );

	extern RED_REFLECTION_API Sphere GetSmallestEnclosingSphere( const void* points, Uint32 stride, Uint32 nbPoints );

	// triangle tests (2d & 3d)
	extern RED_REFLECTION_API Bool IsPointInsideTriangle( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& point );
	extern RED_REFLECTION_API Bool IsPointInsideTriangle_UV( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& point, Float& u, Float& v );
	extern RED_REFLECTION_API Bool IsPointInsideTriangle2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& point );
	extern RED_REFLECTION_API Bool IsPointInsideTriangle2D_UV( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& point, Float& u, Float& v );
	extern RED_REFLECTION_API Vector4 GetTriangleNormal( const Vector4& p1, const Vector4& p2, const Vector4& p3 );
	extern RED_REFLECTION_API Float TriangleArea2D( const Vector2& p1, const Vector2& p2, const Vector2& p3 );

	// nearest squared distance between triangle and a point (3d)
	extern RED_REFLECTION_API Float DistancePointToTriangleSqr( const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& point );

	// 3d triangle-line, no intersection
	extern RED_REFLECTION_API Bool TestLineTriangleNoIntersectionPoint3D(const Vector4& lineStart, const Vector4& lineEnd, const Vector4& t0, const Vector4& t1, const Vector4& t2);

	// 3d triangle-ray
	extern RED_REFLECTION_API Bool TestRayTriangleIntersection3D( const Vector4& rayOrigin, const Vector4& rayVector, const Vector4& t0, const Vector4& t1, const Vector4& t2, Vector4& outPos, Float& intersectionDistance );

	// 3d triangle-box
	extern RED_REFLECTION_API Bool TestIntersectionTriAndBox( const Vector4& v0, const Vector4& v1, const Vector4& v2, const Box& box );

	// 3d line-line
	extern RED_REFLECTION_API Bool TestIntersectionLineLine3D( const Vector4& p1A, const Vector4& p2A, const Vector4& p1B, const Vector4& p2B );
	// returns the shortest line segment between the two lines
	extern RED_REFLECTION_API Bool DistanceLineLine3D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Vector4& outP1, Vector4& outP2 );

	// 3d line-point
	extern RED_REFLECTION_API Float DistancePointToLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint );
	extern RED_REFLECTION_API Float DistanceSqrPointToLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB );
	extern RED_REFLECTION_API Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint );
	extern RED_REFLECTION_API Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint, Float& lineRatio );
	extern RED_REFLECTION_API Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& lineRatio );
	extern RED_REFLECTION_API Float DistanceSqrPointToLineSegNoClamp( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& lineRatio );

	// 2d line-point
	extern RED_REFLECTION_API Float DistancePointToLine2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB, Vector2& linePoint );
	extern RED_REFLECTION_API Float DistanceSqrPointToLine2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB );
	extern RED_REFLECTION_API Float DistanceSqrPointToLineSeg2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB, Vector2& linePoint );

	// 2d line-circle
	extern RED_REFLECTION_API Bool TestIntersectionCircleLine2D( const Vector2& c, Float r, const Vector2& p1, const Vector2& p2);
	extern RED_REFLECTION_API void TestClosestPointOnLine2D( const Vector2& c, const Vector2& p1, const Vector2& p2, Vector2& outPoint );
	extern RED_REFLECTION_API void TestClosestPointOnLine2D( const Vector2& c, const Vector2& p1, const Vector2& p2, Float& outRatio, Vector2& outPoint );

	extern RED_REFLECTION_API Vector4	ProjectPointOnLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB );
	extern RED_REFLECTION_API Vector4	ProjectPointOnLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& ratio );
	extern RED_REFLECTION_API Float	ProjectVecOnEdge( const Vector4& vec, const Vector4& a, const Vector4& b );
	extern RED_REFLECTION_API Float	ProjectVecOnEdgeUnclamped( const Vector4& vec, const Vector4& a, const Vector4& b );
	extern RED_REFLECTION_API void	GetPointFromEdge( Float p, const Vector4& a, const Vector4& b, Vector4& point );

	// 3d sphere-line
	extern RED_REFLECTION_API Bool TestIntersectionSphereLine( const Sphere& sphere, const Vector4& pt0, const Vector4& pt1, Int32& nbInter, Float& inter1, Float& inter2 );


	// 2d line-line
	extern RED_REFLECTION_API Float TestDistanceSqrLineLine2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& p4);
	extern RED_REFLECTION_API void ClosestPointsLineLine2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& p4, Float& ratio1, Float& ratio2 );
	extern RED_REFLECTION_API void ClosestPointsLineLine2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& p4, Vector2& point1Out, Vector2& point2Out );
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& p4 );
	
	/// Returns parameters (ratioA, ratioB) for both lines for more precise checking of lineline intersection. 
	/// Return value is true if lines possibly intersect (they do if 0.0f < ratioA < 1.0f && 0.0f < ratioB < 1.0f) and false if they are parallel.
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2DInfinite( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2, Float& ratioA, Float& ratioB );
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t );
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t1, Float& t2 );
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2DClamped( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t );
	extern RED_REFLECTION_API Bool TestIntersectionLineLine2DClamped( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t1, Float& t2 );

	// 2d line-ray
	extern RED_REFLECTION_API Bool TestIntersectionRayLine2D( const Vector4& rayDir, const Vector4& rayOrigin, const Vector4& p1, const Vector4& p2, Int32 sX , Int32 sY, Float& t );
	extern RED_REFLECTION_API void TestIntersectionRayLine2D( const Vector4& rayDir, const Vector4& rayOrigin, const Vector4& p1, const Vector4& p2, Int32 sX , Int32 sY, Float& t1, Float& t2 );

	// 2d line-circle(or point)


	struct Circle2D
	{
		Vector2 m_center;
		Float	m_radius;
	};

	// 2d circle-triangle intersection test
	extern RED_REFLECTION_API Bool TestIntersectionCircleTriangle2D( const Circle2D& circle, const Vector2& v1, const Vector2& v2, const Vector2& v3 );

	enum ENumberOfResults
	{
		RESNUM_Zero = 0,
		RESNUM_One,
		RESNUM_Two,
		RESNUM_Infinite
	};

	// 2d aabb rectangle
	extern RED_REFLECTION_API void ClosestPointLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax, Vector2& pointLineOut, Vector2& pointRectangleOut );
	extern RED_REFLECTION_API Float TestDistanceSqrLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax );
	extern RED_REFLECTION_API Bool TestIntersectionLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax );
	extern RED_REFLECTION_API void ClosestPointToRectangle2D( const Vector2& rectangleMin, const Vector2& rectangleMax, const Vector2& point, Vector2& outClosestPoint );
	extern RED_REFLECTION_API Bool TestIntersectionCircleRectangle2D( const Vector2& rectangleMin, const Vector2& rectangleMax, const Vector2& circleCenter, Float radius );

	// 3d aabb box
	extern RED_REFLECTION_API void ClosestPointToBox3D( const Box& rect, const Vector4& point, Vector4& outClosestPoint );

	// 2d polygon
	template < class V >
	Bool IsPolygonConvex2D( const V* polyVertexes, Uint32 vertsCount );

	template < class V >
	Bool IsPointInPolygon2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& point );

	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API
	Bool IsPointInPolygon2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& point );

	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API
	Bool IsPointInPolygon2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& point );

	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API
	Bool IsPointInPolygon2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& point );

	template < class V, class Functor >
	RED_INLINE Bool PolygonTest2D( const V* polyVertexes, Uint32 vertsCount, const Vector2* bboxTest, Functor& functor );

	template < class V >
	Bool TestIntersectionPolygonCircle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector3 >(const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius);
	
	template < class V >
	Bool TestEncapsulationPolygonCircle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
	
	template < class V >
	Bool TestIntersectionPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2);

	template < class V >
	Bool TestIntersectionPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius);

	template < class V >
	Bool TestIntersectionPolylineLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolylineLine2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool TestIntersectionPolylineLine2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );


	template < class V >
	Bool TestIntersectionPolygonRectangle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance );
	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance);
	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance );
	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance);
	
	template < class V >
	Bool TestPolygonContainsRectangle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );

	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );
	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );
	RED_REFLECTION_API_TEMPLATE template Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );


	template < class V >
	Float ClosestPointPolygonPoint2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float ClosestPointPolygonPoint2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float ClosestPointPolygonPoint2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon);

	template < class V >
	Float ClosestPointPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float ClosestPointPolygonLine2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float ClosestPointPolygonLine2D< Vector3 >(const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float ClosestPointPolygonLine2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine);
	
	template < class V >
	Float GetClockwisePolygonArea2D( const V* polyVertexes, Uint32 vertsCount );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector4 >(const Vector4* polyVertexes, Uint32 vertsCount);
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector2 >(const Vector2* polyVertexes, Uint32 vertsCount);

	extern RED_REFLECTION_API void ComputeConvexHull2D( red::DynArray< Vector2 > &input, red::DynArray< Vector2 > &output );

	template < class Verts >
	Bool IsPolygonsIntersecting2D( const Verts& polyVertexes1, const Verts& polyVertexes2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector2 > >( const red::DynArray< Vector2 >& polyVertexes1, const red::DynArray< Vector2 >& polyVertexes2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector3 > >( const red::DynArray< Vector3 >& polyVertexes1, const red::DynArray< Vector3 >& polyVertexes2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::ArraySpan< const Vector2 > >( const red::ArraySpan< const Vector2 >& polyVertexes1, const red::ArraySpan< const Vector2 >& polyVertexes2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector4 > >( const red::DynArray< Vector4 >& polyVertexes1, const red::DynArray< Vector4 >& polyVertexes2 );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::ArraySpan< Vector4 > >( const red::ArraySpan< Vector4 >& polyVertexes1, const red::ArraySpan< Vector4 >& polyVertexes2 );
	
	template < class V >
	Bool IsPolygonConvex2D( const red::DynArray< V >& polyVertexes )																	{ return IsPolygonConvex2D( polyVertexes.TypedData(), polyVertexes.Size() ); }
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonConvex2D< Vector4 >( const red::DynArray< Vector4 >& polyVertexes );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPolygonConvex2D< Vector2 >(const red::DynArray< Vector2 >& polyVertexes);
	
	template < class V >
	Bool IsPointInPolygon2D( const red::DynArray< V >& polyVertexes, const Vector2& point )												{ return IsPointInPolygon2D( polyVertexes.TypedData(), polyVertexes.Size(), point ); }
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPointInPolygon2D< Vector2 >( const red::DynArray< Vector2 >& polyVertexes, const Vector2& point );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPointInPolygon2D< Vector3 >( const red::DynArray< Vector3 >& polyVertexes, const Vector2& point );
	RED_REFLECTION_API_TEMPLATE template RED_REFLECTION_API Bool IsPointInPolygon2D< Vector4 >( const red::DynArray< Vector4 >& polyVertexes, const Vector2& point );

	//this is a more general implementation that does not depend on the source of vertexes being a DynArray
	template < typename POLY > Bool IsPointInPolygon2D( const POLY& vertexes, const Vector2& point )
	{
		// http://www.gamedev.net/topic/371458-2d-polygon---polygon-intersection-optimization-solved/

		Bool inside = false;
		auto vertsCount = vertexes.Size();

		for ( auto j = 0u, i = vertsCount - 1; j < vertsCount; i = j++ )
		{
			Vector2 v1 = Vector2( vertexes[i] );
			Vector2 v2 = Vector2( vertexes[j] );
			if ( v1.X >= point.X || v2.X >= point.X )
			{ // At least one endpoint >= px
				if ( v1.Y < point.Y )
				{ // y1 below ray
					if ( v2.Y >= point.Y )
					{ // y2 on or above ray, edge going 'up'
						if ( ( point.Y - v1.Y )*( v2.X - v1.X ) >= ( point.X - v1.X )*( v2.Y - v1.Y ) )
						{
							inside = !inside;
						}
					}
				}
				else if ( v2.Y < point.Y )
				{ // y1 on or above ray, y2 below ray, edge going 'down'
					if ( ( point.Y - v1.Y )*( v2.X - v1.X ) <= ( point.X - v1.X )*( v2.Y - v1.Y ) )
					{
						inside = !inside;
					}
				}
			}
		}
		return inside;
	}

	template < class V, class Functor >
	RED_INLINE Bool PolygonTest2D( const red::DynArray< V >& polyVertexes, const Vector2* bboxTest, Functor& functor ) 				{ return PolygonTest2D( polyVertexes.TypedData(), polyVertexes.Size(), bboxTest, functor ); }
	template < class V >
	Bool TestIntersectionPolygonCircle2D( const red::DynArray< V >& polyVertexes, const Vector2& circleCenter, Float circleRadius )		{ return TestIntersectionPolygonCircle2D( polyVertexes.TypedData(), polyVertexes.Size(), circleCenter, circleRadius ); }
	template < class V >
	Bool TestEncapsulationPolygonCircle2D( const red::DynArray< V >& polyVertexes, const Vector2& circleCenter, Float circleRadius )	{ return TestEncapsulationPolygonCircle2D( polyVertexes.TypedData(), polyVertexes.Size(), circleCenter, circleRadius ); }
	template < class V >
	Bool TestIntersectionPolygonLine2D( const red::DynArray< V >& polyVertexes, const Vector2& v1, const Vector2& v2 )					{ return TestIntersectionPolygonLine2D( polyVertexes.TypedData(), polyVertexes.Size(), v1, v2 ); }
	template < class V >
	Bool TestIntersectionPolygonLine2D( const red::DynArray< V >& polyVertexes, const Vector2& v1, const Vector2& v2, Float radius )	{ return TestIntersectionPolygonLine2D( polyVertexes.TypedData(), polyVertexes.Size(), v1, v2, radius ); }
	template < class V >
	Bool TestIntersectionPolylineLine2D( const red::DynArray< V >& polyVertexes, const Vector2& v1, const Vector2& v2 )					{ return TestIntersectionPolylineLine2D( polyVertexes.TypedData(), polyVertexes.Size(), v1, v2 ); }
	template < class V >
	Bool TestIntersectionPolygonRectangle2D( const red::DynArray< V >& polyVertexes, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance ) { return TestIntersectionPolygonRectangle2D( polyVertexes.TypedData(), polyVertexes.Size(), rectangleMin, rectangleMax, distance ); }
	template < class V >
	Bool TestPolygonContainsRectangle2D( const red::DynArray< V >& polyVertexes, const Vector2& rectangleMin, const Vector2& rectangleMax ) { return TestPolygonContainsRectangle2D( polyVertexes.TypedData(), polyVertexes.Size(), rectangleMin, rectangleMax );  }
	template < class V >
	Float ClosestPointPolygonPoint2D( const red::DynArray< V >& polyVertexes, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon ) { return ClosestPointPolygonPoint2D( polyVertexes.TypedData(), polyVertexes.Size(), v, maxDist, outClosestPointOnPolygon ); }
	template < class V >
	Float ClosestPointPolygonLine2D( const red::DynArray< V >& polyVertexes, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine ) { return ClosestPointPolygonLine2D( polyVertexes.TypedData(), polyVertexes.Size(), v1, v2, maxDist, outClosestPointOnPolygon, outClosestPointOnLine ); }
	template < class V >
	Float GetClockwisePolygonArea2D( const red::DynArray< V >& polyVertexes )															{ return GetClockwisePolygonArea2D( polyVertexes.TypedData(), polyVertexes.Size() ); }

	// 2d helpers
	//PointIsLeftOfSegment2D(): tests if a point is Left|On|Right of an infinite line.
	RED_INLINE Float PointIsLeftOfSegment2D( const Vector2& v1, const Vector2& v2, const Vector2& p ) { return (v2.X - v1.X)*(p.Y - v1.Y) - (p.X - v1.X)*(v2.Y - v1.Y); }

	RED_INLINE Vector2 Rotate2D(const Vector2& v, Float fSin, Float fCos )
	{
		return Vector2( v.X * fCos - v.Y * fSin, v.X * fSin + v.Y * fCos );
	}
	RED_INLINE Vector2 Rotate2D(const Vector2& v, Float radians )
	{
		Float fCos = MCos(radians);
		Float fSin = MSin(radians);
		return Rotate2D( v, fSin, fCos );
	}
	RED_INLINE Vector2 PerpendicularL(const Vector2& v) { return Vector2(-v.Y,v.X); }
	RED_INLINE Vector2 PerpendicularR(const Vector2& v) { return Vector2(v.Y,-v.X); }

	RED_INLINE Float ClampRadians( Float radians )
	{
		radians = fmod( radians + RED_PI, RED_PI_TWO );
		if( radians < 0.0f )
		{
			radians += RED_PI_TWO;
		}

		return radians - RED_PI;
	}

	RED_INLINE Float ClampDegrees( Float degrees )
	{
		degrees = fmod( degrees + 180.0f, 360.0f );
		if( degrees < 0.0f )
		{
			degrees += 360.0f;
		}

		return degrees - 180.0f;
	}

	RED_INLINE Bool RangeOverlap1D(Float a1, Float a2, Float b1, Float b2) { return a1 >= b1 ? a1 <= b2 : a2 >= b1; }
	RED_INLINE Float DistanceRanges1D(Float a1, Float a2, Float b1, Float b2) { return (a1 < b1) ? b1 - a2 : a1 - b2;  }

	// Oriented box
	// Matrix based obb is given by matrix and implicit [-1,-1,-1] - [1,1,1] box.
	extern RED_REFLECTION_API Float OrientedBoxSquaredDistance( const Matrix &obbLocalToWorld, const Vector4 &point );


	template< typename T = Vector2 >
	struct ProjectionResult
	{
		//source -> what is projected onto Target
		//target -> this is being projected onto

		T resolution; //colinear with Target vector
		T rejection; //perpendicular to Target vector
		Float targetLength = 0.0f; //the length of the vector we project onto (Target)
	};

	template<typename T>
	RED_INLINE ProjectionResult<T> VectorProjection( const T& source, const T& target)
	{
		auto a = source;
		auto bN = target;
		auto len = bN.Normalize();
		
		auto r1 = bN * a.Dot( bN );
		return { r1, a - r1, len };
	}
}

//////////////////////////////////////////////////////////////////////////

namespace InterpolationUtils
{
	extern RED_REFLECTION_API Float Interpolate( Float from, Float to, Float t );
	
	template<typename T> RED_INLINE auto Interpolate( T&& from, T&& to, Float t ) { return from + ( to - from ) * t; }

	extern RED_REFLECTION_API Vector4 HermiteInterpolate( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, Float t );
	extern RED_REFLECTION_API Vector4 HermiteInterpolateWithTangents( const Vector4 & v1, const Vector4 & t1, const Vector4 & v2, const Vector4 & t2, Float t );

	RED_INLINE Float Hermite1D( const Float& t, const Float& p0, const Float& t0, const Float& t1, const Float& p1 )
	{
		RED_ASSERT( t >= 0 && t <= 1.0f );
		Float h1 =  2.0f*t*t*t - 3.0f*t*t + 1.0f;
		Float h2 = -2.0f*t*t*t + 3.0f*t*t;
		Float h3 =  1.0f*t*t*t - 2.0f*t*t + t;
		Float h4 =  1.0f*t*t*t - 1.0f*t*t;
		return h1*p0 + h3*t0 + h4*t1 + h2*p1;
	}
	extern RED_REFLECTION_API Vector4 CubicInterpolate( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, Float t );

	extern RED_REFLECTION_API void CatmullRomBuildTauMatrix( Matrix& outMat, Float tau = 0.5f );
	extern RED_REFLECTION_API Vector4 CatmullRomInterpolate( const Vector4 &v1, const Vector4 &v2, const Vector4 &v3, const Vector4 &v4, Float t, const Matrix& tauMatrix );

	extern RED_REFLECTION_API Float HermiteClosestPoint( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, const Vector4 & p, Vector4 &res, Float epsilon = 0.001f );
	extern RED_REFLECTION_API Float HermiteClosestPointWithTangent( const Vector4 & p1, const Vector4 & t1, const Vector4 & p2, const Vector4 & t2, const Vector4 & p, Vector4 &res, Float epsilon = 0.001f );
	extern RED_REFLECTION_API Float CubicClosestPoint( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, const Vector4 & p, Vector4 &res, Float epsilon = 0.001f );

	extern RED_REFLECTION_API void DistanceToEdge( const Vector4 &a, const Vector4 &b, const Vector4 &pt, Float &dist, Float &alpha );
	extern RED_REFLECTION_API void InterpolatePoints( const red::DynArray< Vector4 > &keyPoints, red::DynArray< Vector4 > &outPoints, Float pointsDist, Bool closed );
}

//////////////////////////////////////////////////////////////////////////

template < class V, class Functor >
RED_INLINE Bool GeometryUtils::PolygonTest2D( const V* polyVertexes, Uint32 vertsCount, const Vector2* bboxTest, Functor& functor )
{
	Uint32 lastIt = vertsCount - 1;
	for ( Uint32 it = 0; it != vertsCount; it++ )
	{
		const Vector2& vertCurr = static_cast<Vector2>(polyVertexes[it]);
		const Vector2& vertPrev = static_cast<Vector2>(polyVertexes[lastIt]);
		lastIt = it;

		Vector2 bbox[2];
		bbox[0].X = Min(vertCurr.X,vertPrev.X);
		bbox[1].X = Max(vertCurr.X,vertPrev.X);
		bbox[0].Y = Min(vertCurr.Y,vertPrev.Y);
		bbox[1].Y = Max(vertCurr.Y,vertPrev.Y);
		if (RangeOverlap1D(bboxTest[0].X,bboxTest[1].X,bbox[0].X,bbox[1].X) &&
			RangeOverlap1D(bboxTest[0].Y,bboxTest[1].Y,bbox[0].Y,bbox[1].Y))
		{
			if ( functor( vertCurr, vertPrev ) )
			{
				return true;
			}
		}
	}
	return false;
}


} // Math Utils
