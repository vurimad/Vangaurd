/*
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathUtils.h"

using red::DynArray;
using std::numeric_limits;

namespace MathUtils
{
	Float Sign( const Float x )
	{
		if ( x < 0.0f )
			return -1.0f;
		if ( x > 0.0f )
			return 1.0f;
		return 0.0f;
	}

	Float CyclicDistance( Float a, Float b, Float range /*= 1.f */ )
	{
		const Float delta = b - a;

		// Get shortest distance
		if ( delta < -range )
		{
			return delta + range * 2.f;
		}
		else if ( delta > range )
		{
			return delta - range * 2.f;
		}
		else
		{
			return delta;
		}
	}

	void CartesianFromSpherical(Vector3& sphericalInCartesianOut)
	{
		const Float rad = sphericalInCartesianOut.X;
		const Float lon = sphericalInCartesianOut.Y;
		const Float lat = sphericalInCartesianOut.Z;
		const Float rct = rad * MCos(lat);

		sphericalInCartesianOut.Set(
			rct * MCos(lon),
			rct * MSin(lon),
			rad * MSin(lat));
	}

	void SphericalFromCartesian(Vector3& cartesianInSphericalOut)
	{
		const Float eps = 1.0e-4f;

		const Float rad = cartesianInSphericalOut.Mag();

		if (rad < eps)
		{
			cartesianInSphericalOut = Vector3::ZEROS();
			return;
		}

		const Float x = cartesianInSphericalOut.X;
		const Float y = cartesianInSphericalOut.Y;
		const Float z = cartesianInSphericalOut.Z;

		const Float lat = MAsin_safe(z / rad);
		const Float lon = MATan2(y, x);

		cartesianInSphericalOut.Set(rad, lon, lat);
	}

	namespace VectorUtils
	{
		Float GetAngleRadBetweenVectors( const Vector4& from, const Vector4& to )
		{
			return acosf( Clamp( Vector4::Dot3( from.Normalized3(), to.Normalized3() ), -1.f, 1.f ) );
		}

		Float GetAngleDegBetweenVectors( const Vector4& from, const Vector4& to )
		{
			return RAD2DEG( GetAngleRadBetweenVectors( from, to ) );
		}

		Float GetAngleRadAroundAxis( const Vector4& dirA, const Vector4& dirB, const Vector4& axis ) 
		{
			Vector4 _dirA = dirA - Vector4::Project( dirA, axis );
			Vector4 _dirB = dirB - Vector4::Project( dirB, axis );

			Float angle = GetAngleRadBetweenVectors( _dirA, _dirB );

			return angle * ( Vector4::Dot3( axis, Vector4::Cross( _dirA, _dirB ) ) < 0 ? -1 : 1 );
		}

		Float GetAngleDegAroundAxis( const Vector4& dirA, const Vector4& dirB, const Vector4& axis ) 
		{
			return RAD2DEG( GetAngleRadAroundAxis( dirA, dirB, axis ) );
		}
	}

	namespace GeometryUtils
	{

		void GenerateOrthogonalVectors( const Vector3& original, Vector3& a, Vector3& b )
		{
			const Vector3 c = original.Normalized();

			// select arbitrary vector that is not parallel to "original"
			Vector3 tmp = Vector3( 0.0f, 0.0f, 1.0f );
			if ( math::Abs( tmp.Dot( c ) ) > 0.9f )
			{
				tmp = Vector3( 0.0f, 1.0f, 0.0f );
			}

			a = c.Cross( tmp ).Normalized();
			b = c.Cross( a ).Normalized();
		}

		Sphere GetSmallestEnclosingSphere( const void* points, Uint32 stride, Uint32 nbPoints )
		{
			PC_SCOPE_FUNC();

			//Ritter's algorithm
			RED_FATAL_ASSERT( points != nullptr );
			RED_FATAL_ASSERT( stride >= sizeof(Float) * 3 );
			RED_FATAL_ASSERT( nbPoints > 0 );

			const Uint8* const bytePtr = reinterpret_cast<const Uint8*>( points );

			const Vector3* startingPoint = reinterpret_cast<const Vector3*>( points );

			Float largestDistanceSqr = 0.f;
			Uint32 bestPointId = std::numeric_limits<Uint32>::max();
			for ( Uint32 i = 1; i < nbPoints; ++i )
			{
				const Vector3* testedPoint = reinterpret_cast<const Vector3*>(bytePtr + stride * i);
				const Float distanceSqr = startingPoint->DistanceSquaredTo( *testedPoint );
				if ( distanceSqr > largestDistanceSqr )
				{
					largestDistanceSqr = distanceSqr;
					bestPointId = i;
				}
			}

			RED_FATAL_ASSERT( bestPointId != std::numeric_limits<Uint32>::max() );
			const Vector3* yPoint = reinterpret_cast<const Vector3*>( bytePtr + bestPointId * stride );

			largestDistanceSqr = 0.f;
			bestPointId = std::numeric_limits<Uint32>::max();

			for ( Uint32 i = 0; i < nbPoints; ++i )
			{
				const Vector3* testedPoint = reinterpret_cast<const Vector3*>(bytePtr + stride * i);
				if ( yPoint != testedPoint )
				{
					const Float distanceSqr = yPoint->DistanceSquaredTo( *testedPoint );
					if ( distanceSqr > largestDistanceSqr )
					{
						bestPointId = i;
						largestDistanceSqr = distanceSqr;
					}
				}
			}
			RED_FATAL_ASSERT( bestPointId != std::numeric_limits<Uint32>::max() );

			const Vector3* zPoint = reinterpret_cast<const Vector3*>(bytePtr + bestPointId * stride);

			const Vector3 center = (*yPoint + *zPoint) * 0.5f;
			Sphere returnSphere( center, 0.5f * sqrtf( largestDistanceSqr ) );

			for ( Uint32 i = 0; i < nbPoints; ++i )
			{
				const Vector3* testedPoint = reinterpret_cast<const Vector3*>(bytePtr + stride * i);

				if ( yPoint != testedPoint && zPoint != testedPoint )
				{
					returnSphere.AddPoint( *testedPoint );
				}
			}

			return returnSphere;
		}

		Bool IsPointInsideTriangle( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& point )
		{
			const Vector4 u = p2 - p1;
			const Vector4 v = p3 - p1;
			const Vector4 w = point - p1;

			const Float uu = u.Dot3( u );
			const Float uv = u.Dot3( v );
			const Float vv = v.Dot3( v );
			const Float wu = w.Dot3( u );
			const Float wv = w.Dot3( v );
			const Float d = uv * uv - uu * vv;

			const Float invD = 1.f / d;
			const Float s = ( uv * wv - vv * wu ) * invD;

			if ( s < 0.f || s > 1.f )
			{
				return false;
			}

			const Float t = ( uv * wu - uu * wv ) * invD;

			if ( t < 0.f || ( s + t ) > 1.f )
			{
				return false;
			}

			return true;
		}

		Bool IsPointInsideTriangle_UV( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& point, Float& s, Float& t )
		{
			const Vector4 u = p2 - p1;
			const Vector4 v = p3 - p1;
			const Vector4 w = point - p1;

			const Float uu = u.Dot3( u );
			const Float uv = u.Dot3( v );
			const Float vv = v.Dot3( v );
			const Float wu = w.Dot3( u );
			const Float wv = w.Dot3( v );
			const Float d = uv * uv - uu * vv;

			Float invD = 1.f / d;

			s = ( uv * wv - vv * wu ) * invD;
			t = ( uv * wu - uu * wv ) * invD;

			if ( s < 0.f || s > 1.f || t < 0.f || ( s + t ) > 1.f )
			{
				return false;
			}

			return true;
		}

		Bool IsPointInsideTriangle2D_UV( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& point, Float& s, Float& t )
		{
			const Vector2 u = p2 - p1;
			const Vector2 v = p3 - p1;
			const Vector2 w = point - p1;

			const Float uu = u.Dot( u );
			const Float uv = u.Dot( v );
			const Float vv = v.Dot( v );
			const Float wu = w.Dot( u );
			const Float wv = w.Dot( v );
			const Float d = uv * uv - uu * vv;

			Float invD = 1.f / d;

			s = ( uv * wv - vv * wu ) * invD;
			t = ( uv * wu - uu * wv ) * invD;

			if ( s < 0.f || s > 1.f || t < 0.f || ( s + t ) > 1.f )
			{
				return false;
			}

			return true;
		}

		Bool IsPointInsideTriangle2D( const Vector2& p1, const Vector2& p2, const Vector2& p3, const Vector2& point )
		{
			Float d0 = (p2.X-p1.X) * (point.Y-p1.Y) - (p2.Y-p1.Y) * (point.X-p1.X);
			if (d0 >= 0.f)
			{
				Float d1 = (p3.X-p2.X) * (point.Y-p2.Y) - (p3.Y-p2.Y) * (point.X-p2.X);
				if (d1 >= 0.f)
				{
					Float d2 = (p1.X-p3.X) * (point.Y-p3.Y) - (p1.Y-p3.Y) * (point.X-p3.X);
					return d2 >= 0.f;
				}
			}
			else
			{
				Float d1 = (p3.X-p2.X) * (point.Y-p2.Y) - (p3.Y-p2.Y) * (point.X-p2.X);
				if (d1 <= 0.f)
				{
					Float d2 = (p1.X-p3.X) * (point.Y-p3.Y) - (p1.Y-p3.Y) * (point.X-p3.X);
					return d2 <= 0.f;
				}
			}
			return false;
		}

		Float TriangleArea2D( const Vector2& p1, const Vector2& p2, const Vector2& p3 )
		{
			Vector2 edge1 = p2 - p1;
			Vector2 edge2 = p3 - p1;
			return 0.5f * edge1.CrossZ( edge2 );
		}

		Float DistancePointToTriangleSqr( const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& point )
		{
			// taken from:
			// http://iquilezles.org/www/articles/distfunctions/distfunctions.htm

			const Vector3 ba = v1 - v0;
			const Vector3 pa = point - v0;
			const Vector3 cb = v2 - v1;
			const Vector3 pb = point - v1;
			const Vector3 ac = v0 - v2;
			const Vector3 pc = point - v2;
			const Vector3 norm = ba.Cross( ac );

			const Float ta = MathUtils::Sign( pa.Dot( ba.Cross( norm ) ) );
			const Float tb = MathUtils::Sign( pb.Dot( cb.Cross( norm ) ) );
			const Float tc = MathUtils::Sign( pc.Dot( ac.Cross( norm ) ) );

			if ( ta + tb + tc < 2.0f )
			{
				const Float da = ( ba * Clamp( ba.Dot( pa ) / ba.SquareMag(), 0.0f, 1.0f ) - pa ).SquareMag();
				const Float db = ( cb * Clamp( cb.Dot( pb ) / cb.SquareMag(), 0.0f, 1.0f ) - pb ).SquareMag();
				const Float dc = ( ac * Clamp( ac.Dot( pc ) / ac.SquareMag(), 0.0f, 1.0f ) - pc ).SquareMag();
				return Min( da, db, dc );
			}

			return norm.Dot( pa ) * norm.Dot( pa ) / norm.SquareMag();
		}

		Vector4 GetTriangleNormal( const Vector4& p1, const Vector4& p2, const Vector4& p3 )
		{
			Vector4 v1 = p2 - p1;
			Vector4 v2 = p3 - p1;

			Vector4 n = Vector4::Cross( v1, v2 );

			return n.Normalized3();
		}
		void ClosestPointsLineLine2D( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2, Float& ratio1, Float& ratio2 )
		{
			Vector2 u = a2 - a1;
			Vector2 v = b2 - b1;
			Vector2 w = a1 - b1;
			Float a = u.SquareMag(); // u.Dot( u )
			Float b = u.Dot(v);
			Float c = v.SquareMag(); // v.Dot( v )
			Float D = a*c - b*b; // always >= 0

			// compute the line parameters of the two closest points
			if ( D < std::numeric_limits< Float >::epsilon() )		 // the lines are almost parallel
			{
				if ( c < std::numeric_limits< Float >::epsilon() )
				{
					if ( a < std::numeric_limits< Float >::epsilon() )
					{
						ratio1 = 1.f;
						ratio2 = 1.f;
						return;
					}
					// actually thats viable case. one of lines is almost nullified
					Vector2 diff = b2 - a1;
					Float t = diff.Dot( u ) / a;

					ratio1 = Clamp( t, 0.f, 1.f );
					ratio2 = 1.f;
					return;
				}
				else if ( a < std::numeric_limits< Float >::epsilon() )
				{
					Vector2 diff = a2 - b1;
					Float t = diff.Dot( v ) / c;

					ratio1 = 1.f;
					ratio2 = Clamp( t, 0.f, 1.f );
					return;
				}

				Vector2 q = a2 - b1;

				Float dotA1B = w.Dot( v );
				Float dotA2B = q.Dot( v );

				Bool bNeg = dotA1B > dotA2B;

				Float dotMin = bNeg ? dotA2B : dotA1B;
				Float dotMax = bNeg ? dotA1B : dotA2B;

				// range overlap if-ladder
				if ( dotMax <= 0.f )			// no-overlap
				{
					ratio1 = bNeg ? 0.f : 1.f;
					ratio2 = 0.f;
				}
				else if ( dotMin >= c )			// no overlap
				{
					ratio1 = bNeg ? 1.f : 0.f;
					ratio2 = 1.f;
				}
				else
				{
					// there is an overlap
					if ( dotMin >= 0.f )
					{
						ratio1 = bNeg ? 1.f : 0.f;
						ratio2 = dotMin / c;
					}
					else
					{
						ratio1 = dotMin / (dotMin - dotMax);
						if ( bNeg )
						{
							ratio1 = 1.f - ratio1;
						}
						ratio2 = 0.f;
					}
				}

				return;
			}

			Float d = u.Dot(w);
			Float e = v.Dot(w);
			Float sN = b*e - c*d;
			Float sD = D; // sc = sN / sD, default sD = D >= 0
			Float tN = a*e - b*d;
			Float tD = D; // tc = tN / tD, default tD = D >= 0

			if (sN < 0.0) { // sc < 0 => the s=0 edge is visible
				sN = 0.0;
				tN = e;
				tD = c;
			}
			else if (sN > sD) { // sc > 1 => the s=1 edge is visible
				sN = sD;
				tN = e + b;
				tD = c;
			}

			if (tN < 0.0) { // tc < 0 => the t=0 edge is visible
				tN = 0.0;
				// recompute sc for this edge
				if (-d < 0.0)
					sN = 0.0;
				else if (-d > a)
					sN = sD;
				else {
					sN = -d;
					sD = a;
				}
			}
			else if (tN > tD) { // tc > 1 => the t=1 edge is visible
				tN = tD;
				// recompute sc for this edge
				if ((-d + b) < 0.0)
					sN = 0;
				else if ((-d + b) > a)
					sN = sD;
				else {
					sN = (-d + b);
					sD = a;
				}
			}
			// finally do the division to get sc and tc
			ratio1 = (fabs(sN) < std::numeric_limits< Float >::epsilon() ? 0.0f : sN / sD);
			ratio2 = (fabs(tN) < std::numeric_limits< Float >::epsilon() ? 0.0f : tN / tD);
		}
		Float TestDistanceSqrLineLine2D( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2 )
		{
			Vector2 u = a2 - a1;
			Vector2 v = b2 - b1;
			Vector2 w = a1 - b1;
			Float a = u.SquareMag(); // u.Dot( u )
			Float b = u.Dot(v);
			Float c = v.SquareMag(); // v.Dot( v )
			Float D = a*c - b*b; // always >= 0

			// compute the line parameters of the two closest points
			if ( D < std::numeric_limits< Float >::epsilon() )		 // the lines are almost parallel
			{
				if ( c < std::numeric_limits< Float >::epsilon() )
				{
					if ( a < std::numeric_limits< Float >::epsilon() )
					{
						return ( b2 - a2 ).SquareMag();
					}
					// actually thats viable case. one of lines is almost nullified
					Vector2 diff = b2 - a1;
					Float t = diff.Dot( u ) / a;

					t = Clamp( t, 0.1f, 1.f );
					Vector2 dist = b2 - a1 - u * t;
					return (diff - u * t).SquareMag();
				}
				else if ( a < std::numeric_limits< Float >::epsilon() )
				{
					Vector2 diff = a2 - b1;
					Float t = diff.Dot( v ) / c;
					
					t = Clamp( t, 0.1f, 1.f );
					return (diff - v * t).SquareMag();
				}

				Vector2 q = a2 - b1;

				Float dotA1B = w.Dot( v );
				Float dotA2B = q.Dot( v );

				Bool bNeg = dotA1B > dotA2B;

				Float dotMin = bNeg ? dotA2B : dotA1B;
				Float dotMax = bNeg ? dotA1B : dotA2B;

				// range overlap if-ladder
				if ( dotMax <= 0.f )			// no-overlap
				{
					return
						( b1
						- ( bNeg ? a1 : a2 )
						).SquareMag();
				}
				else if ( dotMin >= c )			// no overlap
				{
					return
						( b2
						- ( bNeg ? a2 : a1 )
						).SquareMag();
				}
				else							// overlap
				{
					Vector2 perpedicular = PerpendicularL( u );
					Float dot = perpedicular.Dot( q );
					return dot * dot / a;
				}
			}

			Float d = u.Dot(w);
			Float e = v.Dot(w);
			Float sN = b*e - c*d;
			Float sD = D; // sc = sN / sD, default sD = D >= 0
			Float tN = a*e - b*d;
			Float tD = D; // tc = tN / tD, default tD = D >= 0

			if (sN < 0.0) { // sc < 0 => the s=0 edge is visible
				sN = 0.0;
				tN = e;
				tD = c;
			}
			else if (sN > sD) { // sc > 1 => the s=1 edge is visible
				sN = sD;
				tN = e + b;
				tD = c;
			}

			if (tN < 0.0) { // tc < 0 => the t=0 edge is visible
				tN = 0.0;
				// recompute sc for this edge
				if (-d < 0.0)
					sN = 0.0;
				else if (-d > a)
					sN = sD;
				else {
					sN = -d;
					sD = a;
				}
			}
			else if (tN > tD) { // tc > 1 => the t=1 edge is visible
				tN = tD;
				// recompute sc for this edge
				if ((-d + b) < 0.0)
					sN = 0;
				else if ((-d + b) > a)
					sN = sD;
				else {
					sN = (-d + b);
					sD = a;
				}
			}
			// finally do the division to get sc and tc
			Float sc = (fabs(sN) < std::numeric_limits< Float >::epsilon() ? 0.0f : sN / sD);
			Float tc = (fabs(tN) < std::numeric_limits< Float >::epsilon() ? 0.0f : tN / tD);

			// get the difference of the two closest points
			Vector2 dP = w + (u * sc) - (v * tc); // = S1(sc) - S2(tc)

			return dP.SquareMag(); // return the closest distance
		}

		void ClosestPointsLineLine2D( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2, Vector2& aOut, Vector2& bOut )
		{
			Vector2 u = a2 - a1;
			Vector2 v = b2 - b1;
			Vector2 w = a1 - b1;
			Float a = u.SquareMag(); // u.Dot( u )
			Float b = u.Dot(v);
			Float c = v.SquareMag(); // v.Dot( v )
			Float D = a*c - b*b; // always >= 0

			// compute the line parameters of the two closest points
			if ( D < std::numeric_limits< Float >::epsilon() )		 // the lines are almost parallel
			{
				if ( c < std::numeric_limits< Float >::epsilon() )
				{
					if ( a < std::numeric_limits< Float >::epsilon() )
					{
						aOut = a2;
						bOut = b2;
						return;
					}
					// actually thats viable case. one of lines is almost nullified
					Vector2 diff = b2 - a1;
					Float t = diff.Dot( u ) / a;
					t = Clamp( t, 0.f, 1.f );
					aOut = a1 + u * t;
					bOut = b2;
					return;
				}
				else if ( a < std::numeric_limits< Float >::epsilon() )
				{
					Vector2 diff = a2 - b1;
					Float t = diff.Dot( v ) / c;
					t = Clamp( t, 0.f, 1.f );
					aOut = a2;
					bOut = b1 + v * t;
					return;
				}

				Vector2 q = a2 - b1;

				Float dotA1B = w.Dot( v );
				Float dotA2B = q.Dot( v );

				Bool bNeg = dotA1B > dotA2B;

				Float dotMin = bNeg ? dotA2B : dotA1B;
				Float dotMax = bNeg ? dotA1B : dotA2B;

				Float ratio1, ratio2;

				// range overlap if-ladder
				if ( dotMax <= 0.f )			// no-overlap
				{
					ratio1 = bNeg ? 0.f : 1.f;
					ratio2 = 0.f;
				}
				else if ( dotMin >= c )			// no overlap
				{
					ratio1 = bNeg ? 1.f : 0.f;
					ratio2 = 1.f;
				}
				else
				{
					// there is an overlap
					if ( dotMin >= 0.f )
					{
						ratio1 = bNeg ? 1.f : 0.f;
						ratio2 = dotMin / c;
					}
					else
					{
						ratio1 = dotMin / (dotMin - dotMax);
						if ( bNeg )
						{
							ratio1 = 1.f - ratio1;
						}
						ratio2 = 0.f;
					}
				}

				aOut = a1 + (u * ratio1);
				bOut = b1 + (v * ratio2);

				return;
			}

			Float d = u.Dot(w);
			Float e = v.Dot(w);
			Float sN = b*e - c*d;
			Float sD = D; // sc = sN / sD, default sD = D >= 0
			Float tN = a*e - b*d;
			Float tD = D; // tc = tN / tD, default tD = D >= 0

			if (sN < 0.0) { // sc < 0 => the s=0 edge is visible
				sN = 0.0;
				tN = e;
				tD = c;
			}
			else if (sN > sD) { // sc > 1 => the s=1 edge is visible
				sN = sD;
				tN = e + b;
				tD = c;
			}

			if (tN < 0.0) { // tc < 0 => the t=0 edge is visible
				tN = 0.0;
				// recompute sc for this edge
				if (-d < 0.0)
					sN = 0.0;
				else if (-d > a)
					sN = sD;
				else {
					sN = -d;
					sD = a;
				}
			}
			else if (tN > tD) { // tc > 1 => the t=1 edge is visible
				tN = tD;
				// recompute sc for this edge
				if ((-d + b) < 0.0)
					sN = 0;
				else if ((-d + b) > a)
					sN = sD;
				else {
					sN = (-d + b);
					sD = a;
				}
			}
			// finally do the division to get sc and tc
			Float ratio1 = (fabs(sN) < std::numeric_limits< Float >::epsilon() ? 0.0f : sN / sD);
			Float ratio2 = (fabs(tN) < std::numeric_limits< Float >::epsilon() ? 0.0f : tN / tD);

			aOut = a1 + (u * ratio1);
			bOut = b1 + (v * ratio2);
		}
		
		Bool TestIntersectionLineLine2D( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2 )
		{
			Float a1yb1y, a1xb1x, a2xa1x, a2ya1y, b2xb1x, b2yb1y;
			Float crossa, crossb, denominator;

			//----------------------------------------------------------------------

			a1yb1y = a1.Y-b1.Y;
			a1xb1x = a1.X-b1.X;
			a2xa1x = a2.X-a1.X;
			a2ya1y = a2.Y-a1.Y;
			b2xb1x = b2.X-b1.X;
			b2yb1y = b2.Y-b1.Y;

			//----------------------------------------------------------------------

			crossa = a1yb1y * b2xb1x - a1xb1x * b2yb1y;
			denominator = a2xa1x * b2yb1y - a2ya1y * b2xb1x;

			//----------------------------------------------------------------------

			if ( denominator == 0 )
			{
				return false;			// Parallel lines
			}
			else if ( fabs( crossa ) > fabs( denominator ) || crossa * denominator < 0.0f )
			{
				return false;
			}
			else
			{
				crossb = a1yb1y * a2xa1x - a1xb1x * a2ya1y;

				if ( fabs( crossb ) > fabs( denominator ) || crossb * denominator< 0.0f )
				{
					return false;
				}
			}

			//----------------------------------------------------------------------
			// Optional - intersection point calculation
			//Float fRatio = crossa/denominator;

			//intersectionX = a1.x + fRatio * (a2.x - a1.x);
			//intersectionY = a1.y + fRatio * (a2.y - a1.y);

			return true;
		}
		Bool TestIntersectionLineLine2DInfinite( const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2, Float& ratioA, Float& ratioB )
		{
			Float a1yb1y, a1xb1x, a2xa1x, a2ya1y, b2xb1x, b2yb1y;
			Float crossa, crossb, denominator;

			//----------------------------------------------------------------------

			a1yb1y = a1.Y-b1.Y;
			a1xb1x = a1.X-b1.X;
			a2xa1x = a2.X-a1.X;
			a2ya1y = a2.Y-a1.Y;
			b2xb1x = b2.X-b1.X;
			b2yb1y = b2.Y-b1.Y;

			//----------------------------------------------------------------------

			crossa = a1yb1y * b2xb1x - a1xb1x * b2yb1y;
			crossb = a1yb1y * a2xa1x - a1xb1x * a2ya1y;
			denominator = a2xa1x * b2yb1y - a2ya1y * b2xb1x;

			//----------------------------------------------------------------------

			if ( math::EqualsZero(denominator) )
			{
				return false;			// Parallel lines
			}

			//----------------------------------------------------------------------

			ratioA = crossa/denominator;
			ratioB = crossb/denominator;

			return true;
		}
		Bool TestIntersectionLineLine2D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t )
		{
			Vector4 d1 = p2 - p1;
			Vector4 d2 = p3 - p4;

			Float dn = d2[ sY ] * d1[ sX ] - d2[ sX ] * d1[ sY ];
			if ( !dn )
			{
				return false;
			}

			t = d2[ sX ] * ( p1[ sY ] - p3[ sY ] ) - d2[ sY ] * ( p1[ sX ] - p3[ sX ] );
			t /= dn;

			return true;
		}

		Bool TestIntersectionLineLine2D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t1, Float& t2 )
		{
			Vector4 d1 = p2 - p1;
			Vector4 d2 = p3 - p4;

			Float dn = d2[ sY ] * d1[ sX ] - d2[ sX ] * d1[ sY ];
			if ( !dn )
			{
				return false;
			}

			t1 = d2[ sX ] * ( p1[ sY ] - p3[ sY ] ) - d2[ sY ] * ( p1[ sX ] - p3[ sX ] );
			t1 /= dn;

			t2 = d1[ sX ] * ( p2[ sY ] - p4[ sY ] ) - d1[ sY ] * ( p2[ sX ] - p4[ sX ] );
			t2 /= dn;

			return true;
		}

		// inspired by voronoi regions. Basically we want to simplify this test to line-line based on what voronoi region p1 is in.
		//      I   I
		//    --rrrrr--
		//      r   r
		//      r   r
		//    --rrrrr--
		//      I   I
		void ClosestPointLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax, Vector2& pointLineOut, Vector2& pointRectangleOut )
		{
			auto funCornerTest =
				[ &p1, &p2, &pointLineOut, &pointRectangleOut ] ( const Vector2& cornerPoint, const Vector2& leftPoint, const Vector2& rightPoint )
			{
				Vector2 diff = p2 - p1;
				Vector2 p1ToCorner = cornerPoint - p1;
				Float dot = diff.Dot( p1ToCorner );
				if ( dot < 0.f )
				{

					pointRectangleOut = cornerPoint;
					TestClosestPointOnLine2D( cornerPoint, p1, p2, pointLineOut );
				}
				else
				{
					Float crossZ = diff.CrossZ( p1ToCorner );
					const Vector2& otherPoint = crossZ < 0.f ? rightPoint : leftPoint;
					ClosestPointsLineLine2D( p1, p2, cornerPoint, otherPoint, pointLineOut, pointRectangleOut );
				}
			};
			// if-ladder
			if ( p1.Y > rectangleMax.Y )
			{
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					funCornerTest( rectangleMax, Vector2( rectangleMin.X, rectangleMax.Y ), Vector2( rectangleMax.X, rectangleMin.Y ) );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					funCornerTest( Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin, rectangleMax );
				}
				else
				{
					// y-top edge
					ClosestPointsLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMax, pointLineOut, pointRectangleOut );
				}
			}
			else if ( p1.Y < rectangleMin.Y )
			{
				// symetric to above test
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					funCornerTest( Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax, rectangleMin );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					funCornerTest( rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ), Vector2( rectangleMin.X, rectangleMax.Y ) );
				}
				else
				{
					// y-bottom edge
					ClosestPointsLineLine2D( p1, p2, rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ), pointLineOut, pointRectangleOut );
				}
			}
			else if ( p1.X > rectangleMax.X )
			{
				ClosestPointsLineLine2D( p1, p2, Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax, pointLineOut, pointRectangleOut );
			}
			else if ( p1.X < rectangleMin.X )
			{
				ClosestPointsLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin, pointLineOut, pointRectangleOut );
			}
			else
			{
				pointLineOut = p1;
				pointRectangleOut = p1;
			}
		}

		Float TestDistanceSqrLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax )
		{
			auto funCornerTest =
				[ &p1, &p2 ]( const Vector2& cornerPoint, const Vector2& leftPoint, const Vector2& rightPoint ) -> Float
			{
				Vector2 diff = p2 - p1;
				Vector2 p1ToCorner = cornerPoint - p1;
				Float dot = diff.Dot( p1ToCorner );
				if ( dot < 0.f )
				{
					return p1ToCorner.SquareMag();
				}
				Float crossZ = diff.CrossZ( p1ToCorner );
				const Vector2& otherPoint = crossZ < 0.f ? rightPoint : leftPoint;
				return TestDistanceSqrLineLine2D( p1, p2, cornerPoint, otherPoint );
			};
			// if-ladder
			if ( p1.Y > rectangleMax.Y )
			{
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					return funCornerTest( rectangleMax, Vector2( rectangleMin.X, rectangleMax.Y ), Vector2( rectangleMax.X, rectangleMin.Y ) );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					return funCornerTest( Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin, rectangleMax );
				}
				else
				{
					// y-top edge
					return TestDistanceSqrLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMax );
				}
			}
			else if ( p1.Y < rectangleMin.Y )
			{
				// symetric to above test
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					return funCornerTest( Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax, rectangleMin );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					return funCornerTest( rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ), Vector2( rectangleMin.X, rectangleMax.Y ) );
				}
				else
				{
					// y-bottom edge
					return TestDistanceSqrLineLine2D( p1, p2, rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ) );
				}
			}
			else if ( p1.X > rectangleMax.X )
			{
				return TestDistanceSqrLineLine2D( p1, p2, Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax );
			}
			else if ( p1.X < rectangleMin.X )
			{
				return TestDistanceSqrLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin );
			}

			// p1 is inside rectangle
			return 0.f;
		}

		// simple implementation - far from optimal
		Bool TestIntersectionLineRectangle2D( const Vector2& p1, const Vector2& p2, const Vector2& rectangleMin, const Vector2& rectangleMax )
		{
			auto funCornerTest =
				[ &p1, &p2 ]( const Vector2& cornerPoint, const Vector2& leftPoint, const Vector2& rightPoint ) -> Bool
			{
				Vector2 diff = p2 - p1;
				Vector2 p1ToCorner = cornerPoint - p1;
				Float dot = diff.Dot( p1ToCorner );
				if ( dot < 0.f )
				{
					return false;
				}
				Float crossZ = diff.CrossZ( p1ToCorner );
				const Vector2& otherPoint = crossZ < 0.f ? rightPoint : leftPoint;
				return TestIntersectionLineLine2D( p1, p2, cornerPoint, otherPoint );
			};
			// if-ladder
			if ( p1.Y > rectangleMax.Y )
			{
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					return funCornerTest( rectangleMax, Vector2( rectangleMin.X, rectangleMax.Y ), Vector2( rectangleMax.X, rectangleMin.Y ) );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					return funCornerTest( Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin, rectangleMax );
				}
				else
				{
					if ( p2.Y > rectangleMax.Y )
					{
						return false;
					}
					// y-top edge
					return TestIntersectionLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMax );
				}
			}
			else if ( p1.Y < rectangleMin.Y )
			{
				// symetric to above test
				if ( p1.X > rectangleMax.X )
				{
					// corner area
					return funCornerTest( Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax, rectangleMin );
				}
				else if ( p1.X < rectangleMin.X )
				{
					// corner area
					return funCornerTest( rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ), Vector2( rectangleMin.X, rectangleMax.Y ) );
				}
				else
				{
					if ( p2.Y < rectangleMin.Y )
					{
						return false;
					}
					// y-bottom edge
					return TestIntersectionLineLine2D( p1, p2, rectangleMin, Vector2( rectangleMax.X, rectangleMin.Y ) );
				}
			}
			else if ( p1.X > rectangleMax.X )
			{
				if ( p2.X > rectangleMax.X )
				{
					return false;
				}
				return TestIntersectionLineLine2D( p1, p2, Vector2( rectangleMax.X, rectangleMin.Y ), rectangleMax );
			}
			else if ( p1.X < rectangleMin.X )
			{
				if ( p2.X < rectangleMin.X )
				{
					return false;
				}
				return TestIntersectionLineLine2D( p1, p2, Vector2( rectangleMin.X, rectangleMax.Y ), rectangleMin );
			}

			// p1 is inside rectangle
			return true;
		}

		void ClosestPointToRectangle2D( const Vector2& rectangleMin, const Vector2& rectangleMax, const Vector2& point, Vector2& outClosestPoint )
		{
			// if-ladder that determines voronoi region. In each region problem is trivial.
			if ( point.Y > rectangleMax.Y )
			{
				// point is above rectangle
				if ( point.X > rectangleMax.X )
				{
					outClosestPoint = rectangleMax;
				}
				else if ( point.X < rectangleMin.X )
				{
					outClosestPoint = Vector2( rectangleMin.X, rectangleMax.Y );
				}
				else
				{
					outClosestPoint = Vector2( point.X, rectangleMax.Y );
				}
			}
			else if ( point.Y < rectangleMin.Y )
			{
				// symetric to above test
				if ( point.X > rectangleMax.X )
				{
					outClosestPoint = Vector2(rectangleMax.X,rectangleMin.Y);
				}
				else if ( point.X < rectangleMin.X )
				{
					outClosestPoint = rectangleMin;
				}
				else
				{
					outClosestPoint = Vector2( point.X, rectangleMin.Y );
				}
			}
			else if ( point.X > rectangleMax.X )
			{
				outClosestPoint = Vector2( rectangleMax.X, point.Y );
			}
			else if ( point.X < rectangleMin.X )
			{
				outClosestPoint = Vector2( rectangleMin.X, point.Y );
			}
			else
			{
				// point inside rectangle
				outClosestPoint = point;
			}
		}

		// inspired by sat theory and voronoi regions
		Bool TestIntersectionCircleRectangle2D( const Vector2& rectangleMin, const Vector2& rectangleMax, const Vector2& circleCenter, Float radius )
		{
			// if-ladder
			if ( circleCenter.Y > rectangleMax.Y )
			{
				// circle is above rectangle
				if ( circleCenter.Y > rectangleMax.Y + radius )
				{
					return false;
				}
				if ( circleCenter.X > rectangleMax.X )
				{
					// point distance test
					Float squareDist = (rectangleMax - circleCenter).SquareMag();
					return squareDist < radius*radius;
				}
				else if ( circleCenter.X < rectangleMin.X )
				{
					// point distance test
					Float squareDist = (Vector2(rectangleMin.X,rectangleMax.Y) - circleCenter).SquareMag();
					return squareDist < radius*radius;
				}
				else
				{
					// intersection
					return true;
				}
			}
			else if ( circleCenter.Y < rectangleMin.Y )
			{
				// symetric to above test
				// circle is above rectangle
				if ( circleCenter.Y < rectangleMin.Y - radius )
				{
					return false;
				}
				if ( circleCenter.X > rectangleMax.X )
				{
					// point distance test
					Float squareDist = (Vector2(rectangleMax.X,rectangleMin.Y) - circleCenter).SquareMag();
					return squareDist < radius*radius;
				}
				else if ( circleCenter.X < rectangleMin.X )
				{
					// point distance test
					Float squareDist = (rectangleMin - circleCenter).SquareMag();
					return squareDist < radius*radius;
				}
				else
				{
					// intersection
					return true;
				}
			}
			else if ( circleCenter.X > rectangleMax.X )
			{
				if ( circleCenter.X > rectangleMax.X + radius )
				{
					return false;
				}
				// intersection
				return true;
			}
			else if ( circleCenter.X < rectangleMin.X )
			{
				if ( circleCenter.X < rectangleMin.X - radius )
				{
					return false;
				}
				return true;
			}

			// circle inside rectangle
			return true;
		}

		void ClosestPointToBox3D( const Box& rect, const Vector4& point, Vector4& outClosestPoint )
		{
            ClosestPointToRectangle2D( Vector2{ rect.Min }, Vector2{ rect.Max }, Vector2{ point }, reinterpret_cast<Vector2&>(outClosestPoint) );

			if ( point.Z < rect.Min.Z )
			{
				outClosestPoint.Z = rect.Min.Z ;
			}
			else if ( point.Z > rect.Max.Z )
			{
				outClosestPoint.Z = rect.Max.Z;
			}
			else
			{
				outClosestPoint.Z = point.Z;
			}
		}

		Bool TestIntersectionLineLine2DClamped( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t )
		{
			TestIntersectionLineLine2D( p1, p2, p3, p4, sX, sY, t );
			return t <= 1.f && t >= 0.f ? true : false;
		}

		Bool TestIntersectionLineLine2DClamped( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Int32 sX , Int32 sY, Float& t1, Float& t2 )
		{
			TestIntersectionLineLine2D( p1, p2, p3, p4, sX, sY, t1, t2 );
			return t1 <= 1.f && t1 >= 0.f && t2 <= 1.f && t2 >= 0.f ? true : false;
		}

		Vector4 ProjectPointOnLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB )
		{
			Vector4 u = lineB - lineA;
			Vector4 v = point - lineA;

			u.Normalize3();
			return lineA + u * v.Dot3(u);
		}

		Vector4 ProjectPointOnLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& ratio )
		{
			Vector4 u = lineB - lineA;
			Vector4 v = point - lineA;

			u.Normalize3();
			ratio = ( v.Dot3( u ) );

			return lineA + u * ratio;
		}

		Float ProjectVecOnEdge( const Vector4& vec, const Vector4& a, const Vector4& b )
		{
			Vector4 d = b - a;
			const Float len = d.Normalize3();

			const Vector4 v( vec.X - a.X, vec.Y - a.Y, vec.Z - a.Z );

			Float proj = Clamp( Vector4::Dot3( v, d ) / len, 0.f, 1.f );
			return proj;
		}

		Float ProjectVecOnEdgeUnclamped( const Vector4& vec, const Vector4& a, const Vector4& b )
		{
			Vector4 d = b - a;
			const Float len = d.Normalize3();

			const Vector4 v( vec.X - a.X, vec.Y - a.Y, vec.Z - a.Z );

			Float proj = Vector4::Dot3( v, d ) / len;
			return proj;
		}

		void GetPointFromEdge( Float p, const Vector4& a, const Vector4& b, Vector4& point )
		{
			point = b * p + a * ( 1 - p );
		}

		Float DistancePointToLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint )
		{
			Vector4 v = point - lineA;
			Vector4 s = lineB - lineA;

			Float lenSq = s.SquareMag3();
			Float dot = v.Dot3( s ) / lenSq;
			Vector4 disp = s * dot;

			linePoint = lineA + disp;
			v -= disp;

			return v.Mag3();
		}

		Float DistanceSqrPointToLine( const Vector4& point, const Vector4& lineA, const Vector4& lineB )
		{
			Vector4 v = point - lineA;
			Vector4 s = lineB - lineA;

			Float lenSq = s.SquareMag3();
			Float dot = v.Dot3( s ) / lenSq;
			Vector4 disp = s * dot;

			v -= disp;

			return v.SquareMag3();
		}

		Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint )
		{
			const Vector4 v = point - lineA;
			const Vector4 s = lineB - lineA;

			const Float dot = Clamp( v.Dot3( s ) / s.Dot3( s ), 0.f, 1.f );

			linePoint = lineA + s * dot;

			return linePoint.DistanceSquaredTo( point );
		}

		Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& lineRatio )
		{
			const Vector4 v = point - lineA;
			const Vector4 s = lineB - lineA;

			lineRatio = Clamp( v.Dot3( s ) / s.Dot3( s ), 0.f, 1.f );

			const Vector4 linePoint = lineA + s * lineRatio;

			return linePoint.DistanceSquaredTo( point );
		}

		Float DistanceSqrPointToLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Vector4& linePoint, Float& lineRatio )
		{
			const Vector4 v = point - lineA;
			const Vector4 s = lineB - lineA;

			lineRatio = Clamp( v.Dot3( s ) / s.Dot3( s ), 0.f, 1.f );

			linePoint = lineA + s * lineRatio;

			return linePoint.DistanceSquaredTo( point );
		}

		Vector4 GetClosestPointOnLineSeg( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& lineRatio )
		{
			const Vector4 v = point - lineA;
			const Vector4 s = lineB - lineA;

			lineRatio = Clamp( v.Dot3( s ) / s.Dot3( s ), 0.f, 1.f );

			return lineA + s * lineRatio;
		}

		Float DistanceSqrPointToLineSegNoClamp( const Vector4& point, const Vector4& lineA, const Vector4& lineB, Float& lineRatio )
		{
			const Vector4 v = point - lineA;
			const Vector4 s = lineB - lineA;

			lineRatio = v.Dot3( s ) / s.Dot3( s );

			const Vector4 linePoint = lineA + s * lineRatio;

			return linePoint.DistanceSquaredTo( point );
		}

		Float DistancePointToLine2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB, Vector2& linePoint )
		{
			Vector2 v = point - lineA;
			const Vector2 s = lineB - lineA;

			const Float lenSq = s.SquareMag();
			const Float dot = v.Dot( s ) / lenSq;
			const Vector2 disp = s * dot;

			linePoint = lineA + disp;
			v -= disp;

			return v.Mag();
		}

		Float DistanceSqrPointToLine2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB )
		{
			Vector2 v = point - lineA;
			const Vector2 s = lineB - lineA;

			const Float lenSq = s.SquareMag();
			const Float dot = v.Dot( s ) / lenSq;
			const Vector2 disp = s * dot;

			v -= disp;

			return v.SquareMag();
		}

		Float DistanceSqrPointToLineSeg2D( const Vector2& point, const Vector2& lineA, const Vector2& lineB, Vector2& lineSegPoint )
		{
			Vector2 v = point - lineA;
			const Vector2 s = lineB - lineA;

			const Float lenSq = s.SquareMag();
			const Float dot = v.Dot( s ) / lenSq;

			if ( dot <= 0.f )
			{
				// closest point is lineA
				lineSegPoint = lineA;
			}
			else if ( dot >= 1.f )
			{
				// closest point is lineB
				lineSegPoint = lineB;
				v = point - lineB;
			}
			else
			{
				const Vector2 disp = s * dot;
				lineSegPoint = lineA + disp;
				v -= disp;
			}

			return v.SquareMag();
		}

		Bool TestIntersectionSphereLine( const Sphere& sphere, const Vector4& pt0, const Vector4& pt1, Int32& nbInter, Float& inter1, Float& inter2 )
		{
			Float a, b, c, i;

			const Vector4 v = pt1 - pt0;
			const Vector4 center = sphere.GetCenter();
			const Float radius = sphere.GetRadius();

			a = v.SquareMag3();

			b =  2 * ( v.X * ( pt0.X - center.X )
				+ v.Y * ( pt0.Y - center.Y )
				+ v.Z * ( pt0.Z - center.Z ) ) ;

			c = center.X * center.X + center.Y * center.Y + center.Z * center.Z +
				pt0.SquareMag3() -
				2 * ( center.X * pt0.X + center.Y * pt0.Y + center.Z * pt0.Z ) - radius * radius;

			i =  b * b - 4 * a * c;

			if ( i < 0 )
			{
				return false;
			}

			if ( math::EqualsZero(i) ) 
			{
				nbInter = 1;
				inter1 = -b / (2 * a);
			}
			else 
			{
				nbInter = 2;
				inter1 = ( -b + MSqrt( b*b - 4*a*c ) ) / (2 * a);
				inter2 = ( -b - MSqrt( b*b - 4*a*c ) ) / (2 * a);
			}

			return true;
		}

		Bool TestIntersectionCircleLine2D( const Vector2& c, Float r, const Vector2& p1, const Vector2& p2)
		{
			Vector2 dir = p2 - p1;
			Vector2 diff = c - p1;
			Float t = diff.Dot(dir) / dir.SquareMag();
			Float distsqr;
			if (t < 0.0f)
			{
				distsqr = (p1 - c).SquareMag();
			}
			else if (t > 1.0f)
			{
				distsqr = (p2 - c).SquareMag();
			}
			else
			{
				Vector2 closest = p1 + (dir * t);
				Vector2 d = c - closest;
				distsqr = d.SquareMag();
			}
			return distsqr <= r*r;
		}

		void TestClosestPointOnLine2D( const Vector2& c, const Vector2& p1, const Vector2& p2, Vector2& outPoint )
		{
			Vector2 dir = p2 - p1;
			Vector2 diff = c - p1;
			Float t = diff.Dot(dir) / dir.SquareMag();
			if (t < 0.0f)
			{
				outPoint = p1;
			}
			else if (t > 1.0f)
			{
				outPoint = p2;
			}
			else
			{
				outPoint = p1 + (dir * t);
			}
		}

		void TestClosestPointOnLine2D( const Vector2& c, const Vector2& p1, const Vector2& p2, Float& outRatio, Vector2& outPoint )
		{
			Vector2 dir = p2 - p1;
			Vector2 diff = c - p1;
			Float t = diff.Dot(dir) / dir.SquareMag();
			if (t < 0.0f)
			{
				outPoint = p1;
				outRatio = 0.f;
			}
			else if (t > 1.0f)
			{
				outPoint = p2;
				outRatio = 1.f;
			}
			else
			{
				outPoint = p1 + (dir * t);
				outRatio = t;
			}
		}

		Bool TestIntersectionCircleTriangle2D( const Circle2D& circle, const Vector2& v1, const Vector2& v2, const Vector2& v3 )
		{
			const Vector2 c1 = circle.m_center - v1;
			const Vector2 c2 = circle.m_center - v2;
			const Vector2 c3 = circle.m_center - v3;

			const Float sqRadius = circle.m_radius * circle.m_radius;

			// Check if any of the triangle's vertex is inside the circle
			if( c1.SquareMag() <= sqRadius || c2.SquareMag() <= sqRadius || c3.SquareMag() <= sqRadius )
			{
				return true;
			}

			const Vector2 e1 = v2 - v1;
			const Vector2 e2 = v3 - v2;
			const Vector2 e3 = v1 - v3;

			// Check if circle center is inside the triangle
			if ( e1.X * c1.Y - e1.Y * c1.X >= 0.f )
			{
				// Counterclockwise triangle
				if ( e2.X * c2.Y - e2.Y * c2.X >= 0.f && e3.X * c3.Y - e3.Y * c3.X >= 0.f )
				{
					return true;
				}
			} // Clockwise triangle
			else if ( e2.X * c2.Y - e2.Y * c2.X <= 0.f && e3.X * c3.Y - e3.Y * c3.X <= 0.f )
			{
				return true;
			}

			// Finally check if any edge intersects the circle

			//        C - circle center
			//       /|
			//     p/ |d
			//     /  |
			//  V1/___|_____V2
			//      k
			//      
			// c1 dot e1 = |c1| * |e1| * MCos(angle)
			// c1 dot e1 = |c1| * |e1| * (k/p)
			// c1 dot e1 = ( p * |e1| * k ) / p    => since |c1| = p
			// k = ( c1 dot e1 ) / |e1|
			
			Float k = c1.Dot( e1 );

			// if dot < 0 then it means that the perpendicular line is beyond the edge
			if( k > 0.f )
			{
				const Float sqLen = e1.SquareMag();
				k = k*k / sqLen;

				// k > |e1| means that we are beyond the edge aswell
				if( k < sqLen )
				{
					// from picture above => p*p = k*k + d*d => d*d = p*p - k*k
					if( c1.SquareMag() - k <= sqRadius )
					{
						return true;
					}
				}
			}

			// Second edge
			k = c2.Dot( e2 );
			if( k > 0.f )
			{
				const Float sqLen = e2.SquareMag();
				k = k*k / sqLen;

				if( k < sqLen && c2.SquareMag() - k <= sqRadius )
				{
					return true;
				}
			}

			// Third edge
			k = c3.Dot( e3 );
			if( k > 0.f )
			{
				const Float sqLen = e3.SquareMag();
				k = k*k / sqLen;

				if( k < sqLen && c3.SquareMag() - k <= sqRadius )
				{
					return true;
				}
			}

			return false;
		}


		Bool TestIntersectionTriAndBox( const Vector4& tri0, const Vector4& tri1, const Vector4& tri2, const Box& box )
		{
			struct Local
			{
				static RED_INLINE Bool AxisTest( const Vector4& v0, const Vector4& v1, Float a, Float b, Float rad )
				{
					Float p0 = a*v0.Y - b*v0.Z;
					Float p1 = a*v1.Y - b*v1.Z;
					Float min, max;

					if( p0<p1 )
					{
						min = p0;
						max = p1;
					}
					else
					{
						min = p1;
						max = p0;
					}

					return ( min<=rad && max>=-rad );
				}
			
			};

			//    use separating axis theorem to test overlap between triangle and box
			//    need to test for overlap in these directions:
			//
			//    1) the {x,y,z}-directions (actually, since we use the AABB of the triangle
			//       we do not even need to test these)
			//
			//    2) normal of the triangle 
			//
			//    3) crossproduct(edge from tri, {x,y,z}-directin)
			//       this gives 3x3=9 more tests


			// move everything so that the boxcenter is in (0,0,0)
			Vector4 boxSize = box.Max - box.Min;
			Vector4 boxHalfSize = boxSize * 0.5f;
			Vector4 boxCenter = box.Min + boxHalfSize;

			Vector4 v0 = tri0 - boxCenter;
			Vector4 v1 = tri1 - boxCenter;
			Vector4 v2 = tri2 - boxCenter;


			// Test 1:

			//  first test overlap in the {x,y,z}-directions
			//  find min, max of the triangle each direction, and test for overlap in
			//  that direction -- this is equivalent to testing a minimal AABB around
			//  the triangle against the AABB


			for ( Uint32 i = 0; i < 3; ++i )
			{
				Float min = Min( v0[ i ], v1[ i ], v2[ i ] );
				Float max = Max( v0[ i ], v1[ i ], v2[ i ] );

				if ( min > boxHalfSize[ i ] || max < -boxHalfSize[ i ] )
				{
					return false;
				}
			}

			// compute triangle edges

			Vector4 e0 = v1 - v0;		// tri edge 0
			Vector4 e1 = v2 - v1;		// tri edge 1
			Vector4 e2 = v0 - v2;		// tri edge 2


			// Test 2:

			//  test the 9 tests first (this was faster)
			Float fex,fey,fez;

			fex = fabsf(e0.X);
			fey = fabsf(e0.Y);
			fez = fabsf(e0.Z);

			if (
				!Local::AxisTest( v0, v2, e0.Z, e0.Y, fez * boxHalfSize.Y + fey * boxHalfSize.Z ) ||
				!Local::AxisTest( v0, v2,-e0.Z, e0.X, fez * boxHalfSize.X + fex * boxHalfSize.Z ) ||
				!Local::AxisTest( v1, v2, e0.Y, e0.X, fey * boxHalfSize.X + fex * boxHalfSize.Y ) )
			{
				return false;
			}
			
			fex = fabsf(e1.X);
			fey = fabsf(e1.Y);
			fez = fabsf(e1.Z);

			if ( 
				!Local::AxisTest( v0, v2, e1.Z, e1.Y, fez * boxHalfSize.Y + fey * boxHalfSize.Z ) ||
				!Local::AxisTest( v0, v2,-e1.Z, e1.X, fez * boxHalfSize.X + fex * boxHalfSize.Z ) ||
				!Local::AxisTest( v0, v1, e1.Y, e1.X, fey * boxHalfSize.X + fex * boxHalfSize.Y ) )
			{
				return false;
			}

			fex = fabsf(e2.X);
			fey = fabsf(e2.Y);
			fez = fabsf(e2.Z);

			if ( 
				!Local::AxisTest( v0, v1, e2.Z, e2.Y, fez * boxHalfSize.Y + fey * boxHalfSize.Z ) ||
				!Local::AxisTest( v0, v1,-e2.Z, e2.X, fez * boxHalfSize.X + fex * boxHalfSize.Z ) ||
				!Local::AxisTest( v1, v2, e2.Y, e2.X, fey * boxHalfSize.X + fex * boxHalfSize.Y ) )
			{
				return false;
			}


			// Test 3:

			//  test if the box intersects the plane of the triangle
			//  compute plane equation of triangle: normal*x+d=0 

			Vector4 normal = Vector4::Cross( e0, e1 );


			Vector4 vmin, vmax;
			for( Uint32 dim = 0; dim <= 3; ++dim )
			{

				Float v=v0[ dim ];

				if( normal[dim] > 0.0f )
				{
					vmin[ dim ] =-boxHalfSize[ dim ] - v;
					vmax[ dim ] = boxHalfSize[ dim ] - v;
				}
				else

				{
					vmin[ dim ] = boxHalfSize[ dim ] - v;
					vmax[ dim ] =-boxHalfSize[ dim ] - v;
				}
			}

			if( normal.Dot3( vmin ) > 0.f || normal.Dot3( vmax ) < 0.f )
			{
				// plane and box don't overlap
				return false;
			}

			return true;
		}

		Bool TestIntersectionLineLine3D( const Vector4& p1A, const Vector4& p2A, const Vector4& p1B, const Vector4& p2B )
		{
			const Vector4 t1 = p1B - p1A;
			const Vector4 m1 = Vector4::Cross( p1A, p1B );
			const Vector4 t2 = p2B - p2A;
			const Vector4 m2 = Vector4::Cross( p2A, p2B );

			if( t1.Dot3( m2 ) + t2.Dot3( m1 ) < 0.01 )
			{
				return true;
			}

			return false;
		}

		Bool DistanceLineLine3D( const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4, Vector4& outP1, Vector4& outP2 )
		{
			/*	Pa = P1 + mua(P2 - P1)	-> 'Pa' point on line P1P2
			 *	Pb = P3 + mub(P4 - P3)	-> 'Pb' point on line P3P4
			 *	Pb - Pa					-> shortest line segment between the two lines
			 *	
			 *	Substituting 'Pb - Pa' gives:
			 *	P1 - P3 + mua(P2 - P1) - mub(P4 - P3)
			 *	
			 *	The shortest line segment between the two lines will be perpendicular to the two lines
			 *	(Pa - Pb) dot (P2 - P1) = 0
			 *	(Pa - Pb) dot (P4 - P3) = 0
			 *	
			 *	Expanding these given the equation of the lines
			 *
			 *	( P1 - P3 + mua(P2 - P1) - mub(P4 - P3) ) dot (P2 - P1) = 0
			 *	( P1 - P3 + mua(P2 - P1) - mub(P4 - P3) ) dot (P4 - P3) = 0
			 *	
			 *	Expanding further gives
			 *	
			 *	(P1-P3)dot(P2-P1) + mua(P2-P1)dot(P2-P1) - mub(P4-P3)dot(P2-P1) = 0
			 *	(P1-P3)dot(P4-P3) + mua(P2-P1)dot(P4-P3) - mub(P4-P3)dot(P4-P3) = 0
			 *	
			 *	Finally, solving for mua gives:
			 *	
			 *	mua = ( d1343 d4321 - d1321 d4343 ) / ( d2121 d4343 - d4321 d4321 )
			 */

			static const Float epsilon = 0.001f;

			const Vector4 v13 = p1 - p3;
			const Vector4 v21 = p2 - p1;
			const Vector4 v43 = p4 - p3;

			if( v43.SquareMag3() < epsilon || v21.SquareMag3() < epsilon )
			{
				return false;
			}

			const Float d1343 = v13.Dot3( v43 );
			const Float d4321 = v43.Dot3( v21 );
			const Float d1321 = v13.Dot3( v21 );
			const Float d4343 = v43.SquareMag3();
			const Float d2121 = v21.SquareMag3();

			const Float denom = d2121 * d4343 - d4321 * d4321;
			if( MAbs( denom ) < epsilon )
			{
				return false;
			}

			const Float mua = (d1343 * d4321 - d1321 * d4343) / denom;
			const Float mub = (d1343 + d4321 * mua) / d4343;

			outP1 = p1 + v21 * mua;
			outP2 = p3 + v43 * mub;

			return true;
		}

		Bool TestIntersectionRayLine2D( const Vector4& rayDir, const Vector4& rayOrigin, const Vector4& a, const Vector4& b, Int32 sX , Int32 sY, Float& t )
		{
			Float t0, t1;

			TestIntersectionRayLine2D( rayDir, rayOrigin, a, b, sX, sY, t0, t1 );

			t = t1;

			return t0 > 0.f && t <= 1.f && t >= 0.f;
		}

		void TestIntersectionRayLine2D( const Vector4& rayDir, const Vector4& rayOrigin, const Vector4& a, const Vector4& b, Int32 sX , Int32 sY, Float& t0, Float& t1 )
		{
			const Float d1x = rayDir[ sX ];
			const Float d1y = rayDir[ sY ];

			const Float d0x = b[ sX ] - a[ sX ];
			const Float d0y = b[ sY ] - a[ sY ];

			const Float p1x = rayOrigin[ sX ];
			const Float p1y = rayOrigin[ sY ];

			const Float p0x = a[ sX ];
			const Float p0y = a[ sY ];

			const Float qx = p1x - p0x;
			const Float qy = p1y - p0y;

			const Float m = d0x * d1y - d0y * d1x;

			if ( MAbs( m ) < std::numeric_limits< Float >::epsilon() )
			{
				t0 = 0.f;
				t1 = 0.f;
				return;
			}

			const Float l0 = qx * d0y - qy * d0x;
			const Float l1 = qx * d1y - qy * d1x;

			t0 = l0 / m;
			t1 = l1 / m;
		}

		template < class V >
		Bool IsPolygonConvex2D( const V* polyVertexes, Uint32 vertsCount )
		{
			if ( vertsCount <= 3 )
			{
				return true;
			}

			Int32 sgn = 0;

			Vector2 lastDiff = polyVertexes[ vertsCount-1 ] - polyVertexes[vertsCount - 2];

			for ( Uint32 j = 0, i = vertsCount-1; j < vertsCount; i = j++ )
			{
				Vector2 newDiff = polyVertexes[ j ] - polyVertexes[ i ];

				Float crossZ = newDiff.CrossZ( lastDiff );

				if ( Abs( crossZ ) < std::numeric_limits< Float >::epsilon() )
				{
					// ignore segment
					continue;
				}

				if ( sgn == 0 )
				{
					sgn = Sgn( crossZ );
				}
				else
				{
					Int32 testSgn = Sgn( crossZ );
					if ( testSgn != sgn )
					{
						return false;
					}
				}
				lastDiff = newDiff;
			}

			return true;
		}
		template
		Bool IsPolygonConvex2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount );
		template
		Bool IsPolygonConvex2D< Vector2 >(const red::DynArray< Vector2 >& polyVertexes);
		template
		Bool IsPolygonConvex2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount );
		template
		Bool IsPolygonConvex2D< Vector4 >( const red::DynArray< Vector4 >& polyVertexes );


		// http://www.gamedev.net/topic/371458-2d-polygon---polygon-intersection-optimization-solved/
		template < class V >
		Bool IsPointInPolygon2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& point )
		{
			Bool inside = false;
			for ( Uint32 j = 0, i = vertsCount-1; j < vertsCount; i = j++ )
			{
				Vector2 v1 = static_cast<Vector2>(polyVertexes[i]);
				Vector2 v2 = static_cast<Vector2>(polyVertexes[j]);
				if (v1.X >= point.X || v2.X >= point.X)
				{ // At least one endpoint >= px
					if (v1.Y < point.Y)
					{ // y1 below ray
						if (v2.Y >= point.Y)
						{ // y2 on or above ray, edge going 'up'
							if ((point.Y-v1.Y)*(v2.X-v1.X) >= (point.X-v1.X)*(v2.Y-v1.Y))
							{
								inside = !inside;
							}
						}
					}
					else if (v2.Y < point.Y)
					{ // y1 on or above ray, y2 below ray, edge going 'down'
						if ((point.Y-v1.Y)*(v2.X-v1.X) <= (point.X-v1.X)*(v2.Y-v1.Y))
						{
							inside = !inside;
						}
					}
				}
			}
			return inside;
		}

		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& point );
		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& point );
		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& point );

		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector2 >( const red::DynArray< Vector2 >& polyVertexes, const Vector2& point );
		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector3 >( const red::DynArray< Vector3 >& polyVertexes, const Vector2& point );
		template
		RED_REFLECTION_API Bool IsPointInPolygon2D< Vector4 >( const red::DynArray< Vector4 >& polyVertexes, const Vector2& point );
		
		template < class V >
		RED_INLINE Bool TestIntersectionPolygonCircle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, circleCenter ) )
			{
				return true;
			}

			Vector2 bboxTest[2];
			bboxTest[0] = circleCenter - Vector2( circleRadius, circleRadius );
			bboxTest[1] = circleCenter + Vector2( circleRadius, circleRadius );

			auto functor = 
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
				{
					return TestIntersectionCircleLine2D( circleCenter,circleRadius, vertCurr, vertPrev );
				};

			return PolygonTest2D< V >( polyVertexes, vertsCount, bboxTest, functor );
		}

		template
		RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
		template
		RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
		template
		RED_REFLECTION_API Bool TestIntersectionPolygonCircle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );


		template < class V >
		Bool TestEncapsulationPolygonCircle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius )
		{
			if ( !IsPointInPolygon2D( polyVertexes, vertsCount, circleCenter ) )
			{
				return false;
			}

			Vector2 bboxTest[2];
			bboxTest[0] = circleCenter - Vector2( circleRadius, circleRadius );
			bboxTest[1] = circleCenter + Vector2( circleRadius, circleRadius );

			auto functor = 
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
			{
				return TestIntersectionCircleLine2D( circleCenter,circleRadius, vertCurr, vertPrev );
			};

			return !PolygonTest2D< V >( polyVertexes, vertsCount, bboxTest, functor );
		}

		template
		RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
		template
		RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );
		template
		RED_REFLECTION_API Bool TestEncapsulationPolygonCircle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& circleCenter, Float circleRadius );

		template < class V >
		Bool TestIntersectionPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, v2 ) )
			{
				return true;
			}
			Vector2 bboxSegment[2];
			bboxSegment[0].X = Min(v1.X,v2.X);
			bboxSegment[1].X = Max(v1.X,v2.X);
			bboxSegment[0].Y = Min(v1.Y,v2.Y);
			bboxSegment[1].Y = Max(v1.Y,v2.Y);

			auto functor =
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
				{
					return TestIntersectionLineLine2D( v1, v2, vertCurr, vertPrev );
				};

			return PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );
		}

		template
		RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );
		template
		RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );

		template < class V >
		Bool TestIntersectionPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, v2 ) )
			{
				return true;
			}

			Float radiusSq = radius*radius;
			Vector2 bboxSegment[2];
			bboxSegment[0].X = Min(v1.X,v2.X) - radius;
			bboxSegment[1].X = Max(v1.X,v2.X) + radius;
			bboxSegment[0].Y = Min(v1.Y,v2.Y) - radius;
			bboxSegment[1].Y = Max(v1.Y,v2.Y) + radius;

			auto functor =
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
				{
					return TestDistanceSqrLineLine2D( v1, v2, vertCurr, vertPrev ) <= radiusSq;
				};

			return PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );
		};

		template
		RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius );
		template
		RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius );
		template
		RED_REFLECTION_API Bool TestIntersectionPolygonLine2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float radius );

		//////////////////////////////////////////////////////////////////////////
		template < class V >
		Bool TestIntersectionPolylineLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 )
		{
			Vector2 bboxSegment[2];
			bboxSegment[0].X = Min( v1.X, v2.X );
			bboxSegment[1].X = Max( v1.X, v2.X );
			bboxSegment[0].Y = Min( v1.Y, v2.Y );
			bboxSegment[1].Y = Max( v1.Y, v2.Y );

			auto functor =
				[&]( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
			{
				return TestIntersectionLineLine2D( v1, v2, vertCurr, vertPrev );
			};

			return PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );
		}

		template
		RED_REFLECTION_API Bool TestIntersectionPolylineLine2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );
		template
		RED_REFLECTION_API Bool TestIntersectionPolylineLine2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2 );

		//////////////////////////////////////////////////////////////////////////
		template < class V >
		Bool TestIntersectionPolygonRectangle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, ( rectangleMin + rectangleMax ) / 2.f ) )
			{
				return true;
			}
			Vector2 bboxSegment[2];
			bboxSegment[0] = rectangleMin - Vector2( distance, distance );
			bboxSegment[1] = rectangleMax + Vector2( distance, distance );
			Float distanceSq = distance * distance;

			auto functor =
				[&]( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
			{
				return TestDistanceSqrLineRectangle2D( vertCurr, vertPrev, rectangleMin, rectangleMax ) <= distanceSq;
			};

			return PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );
		}


		template
		Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance );
		template
		Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance );
		template
		Bool RED_REFLECTION_API TestIntersectionPolygonRectangle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax, Float distance );

		template < class V >
		Bool TestPolygonContainsRectangle2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax )
		{
			if ( !IsPointInPolygon2D( polyVertexes, vertsCount, (rectangleMin + rectangleMax) / 2.f ) )
			{
				return false;
			}
			Vector2 bboxSegment[2];
			bboxSegment[ 0 ] = rectangleMin;
			bboxSegment[ 1 ] = rectangleMax;

			auto functor =
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
			{
				return TestIntersectionLineRectangle2D( vertCurr, vertPrev, rectangleMin, rectangleMax );
			};

			return !PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );
		}

		template
		Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );
		template
		Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );
		template
		Bool RED_REFLECTION_API TestPolygonContainsRectangle2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& rectangleMin, const Vector2& rectangleMax );



		// Closest point test. Returns square distance of point found or RED_FLT_MAX if there are no intersection in given maxDist
		template < class V >
		Float ClosestPointPolygonPoint2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, v ) )
			{
				outClosestPointOnPolygon = v;
				return 0.f;
			}

			Vector2 bboxSegment[2];
			bboxSegment[ 0 ] = v - Vector2( maxDist, maxDist );
			bboxSegment[ 1 ] = v + Vector2( maxDist, maxDist );

			Float closestDistSq = maxDist*maxDist;
			Bool foundIntersection = false;
			auto functor =
				[ &v, &outClosestPointOnPolygon, &closestDistSq, &foundIntersection ] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
				{
					Vector2 foundPoint;
					TestClosestPointOnLine2D( v, vertCurr, vertPrev, foundPoint );
					Float distSq = (v - foundPoint).SquareMag();
					if ( distSq < closestDistSq )
					{
						outClosestPointOnPolygon = foundPoint;
						closestDistSq = distSq;
						foundIntersection = true;
					}
					return false;
				};

			PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );

			return foundIntersection ? closestDistSq : RED_FLT_MAX;
		}

		template
		RED_REFLECTION_API Float ClosestPointPolygonPoint2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon );
		template
		RED_REFLECTION_API Float ClosestPointPolygonPoint2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v, Float maxDist, Vector2& outClosestPointOnPolygon );

		// Closest point on line and on poly. Returns square distance of points found or RED_FLT_MAX if there are no intersections in maxDist
		template < class V >
		Float ClosestPointPolygonLine2D( const V* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine )
		{
			if ( IsPointInPolygon2D( polyVertexes, vertsCount, v1 ) )
			{
				outClosestPointOnPolygon = v1;
				outClosestPointOnLine = v1;
				return 0.f;
			}

			Vector2 bboxSegment[2];
			bboxSegment[0].X = Min(v1.X,v2.X) - maxDist;
			bboxSegment[1].X = Max(v1.X,v2.X) + maxDist;
			bboxSegment[0].Y = Min(v1.Y,v2.Y) - maxDist;
			bboxSegment[1].Y = Max(v1.Y,v2.Y) + maxDist;

			Float closestDistSq = maxDist*maxDist;
			Bool foundIntersection = false;
			auto functor =
				[&] ( const Vector2& vertCurr, const Vector2& vertPrev ) -> Bool
				{
					Vector2 polyPoint;
					Vector2 segmentPoint;
					ClosestPointsLineLine2D( vertCurr, vertPrev, v1, v2, polyPoint, segmentPoint );
					Float distSq = (segmentPoint - polyPoint).SquareMag();
					if ( distSq < closestDistSq )
					{
						outClosestPointOnPolygon = polyPoint;
						outClosestPointOnLine = segmentPoint;
						closestDistSq = distSq;
						foundIntersection = true;
					}
					return false;
				};

			PolygonTest2D< V >( polyVertexes, vertsCount, bboxSegment, functor );

			return foundIntersection ? closestDistSq : RED_FLT_MAX;
		}

		template
		Float RED_REFLECTION_API ClosestPointPolygonLine2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine );
		template
		Float RED_REFLECTION_API ClosestPointPolygonLine2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine );
		template
		Float RED_REFLECTION_API ClosestPointPolygonLine2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount, const Vector2& v1, const Vector2& v2, Float maxDist, Vector2& outClosestPointOnPolygon, Vector2& outClosestPointOnLine );

		template < class V >
		Float GetClockwisePolygonArea2D( const V* polyVertexes, Uint32 vertsCount )
		{
			Float area = 0;				// accumulated area

			for ( Uint32 i=0, j = vertsCount-1; i<vertsCount; i++ )
			{
				area += (polyVertexes[j].X + polyVertexes[i].X) * (polyVertexes[j].Y - polyVertexes[i].Y);
				j = i;
			}

			return area/2;
		}

		template
		RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector4 >( const Vector4* polyVertexes, Uint32 vertsCount );
		template
		RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector3 >( const Vector3* polyVertexes, Uint32 vertsCount );
		template
		RED_REFLECTION_API Float GetClockwisePolygonArea2D< Vector2 >( const Vector2* polyVertexes, Uint32 vertsCount );

		// Code based on SAT (separate axis theorem)
		// Quite readable, but it has performance cost of (n1*n2) - couldn't that work a little better?
		// source: http://forums.gentoo.org/viewtopic-t-872347-start-0.html
		// Almost exact solutions (just rewritten) are available all over net.
		template < class Verts >
		Bool IsPolygonsIntersecting2D( const Verts& polyVertexes1, const Verts& polyVertexes2 )
		{
			struct Local
			{
				RED_INLINE static void ProjectPolygonOnAxis(const Vector2 &vecAxis, const Verts&polyVertexes, Float &valMin, Float &valMax) 
				{ 
					Float dotValue = static_cast<Vector2>(polyVertexes[0]).Dot(vecAxis); 
					valMin = valMax = dotValue; 
					for(Uint32 i = 1, n = polyVertexes.Size(); i < n; ++i) 
					{ 
						dotValue = static_cast<Vector2>(polyVertexes[i]).Dot(vecAxis); 
						if(dotValue < valMin) valMin = dotValue; 
						else if(dotValue > valMax) valMax = dotValue; 
					} 
				}
				RED_INLINE static Bool HandleEdge( Vector2& vecEdge, const Verts& polyVertexes1, const Verts& polyVertexes2 )
				{
					if ( vecEdge.IsAlmostZero() )
						return true;
					Vector2 vecAxis(-vecEdge.Y, vecEdge.X);
					// NOTE: everyone is doing axis normalization. But we are using it only for dot products that just scale lineary with axis length (min and max will be scaled) - so what's that big deal?

					Float valMinA, valMaxA, valMinB, valMaxB;

					ProjectPolygonOnAxis( vecAxis, polyVertexes1, valMinA, valMaxA );
					ProjectPolygonOnAxis( vecAxis, polyVertexes2, valMinB, valMaxB );

					if( !RangeOverlap1D(valMinA, valMaxA, valMinB, valMaxB) )		// >= ?
					{ 
						return false;
					}
					return true;
				}
			};
			Uint32 pointsCount1 = polyVertexes1.Size();
			Uint32 pointsCount2 = polyVertexes2.Size(); 

			for( Uint32 i = 0, j = pointsCount1-1; i != pointsCount1; j = i++ ) 
			{ 
				// Get the current edge
				Vector2 vecEdge = static_cast<Vector2>(polyVertexes1[ i ]) - static_cast<Vector2>(polyVertexes1[ j ]);
				if ( !Local::HandleEdge( vecEdge, polyVertexes1, polyVertexes2 ) )
					return false;
			}
			for( Uint32 i = 0, j = pointsCount2-1; i != pointsCount2; j = i++ ) 
			{ 
				// Get the current edge
				Vector2 vecEdge = static_cast<Vector2>(polyVertexes2[ i ]) - static_cast<Vector2>(polyVertexes2[ j ]);
				if ( !Local::HandleEdge( vecEdge, polyVertexes1, polyVertexes2 ) )
					return false;
			}


			return true; 
		}

		template
		RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector2 > >( const DynArray< Vector2 >& polyVertexes1, const DynArray< Vector2 >& polyVertexes2 );
		template
		RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector3 > >( const DynArray< Vector3 >& polyVertexes1, const DynArray< Vector3 >& polyVertexes2 );
		template
		RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::ArraySpan< const Vector2 > >( const red::ArraySpan< const Vector2 >& polyVertexes1, const red::ArraySpan< const Vector2 >& polyVertexes2 );
		template
		RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::DynArray< Vector4 > >( const DynArray< Vector4 >& polyVertexes1, const DynArray< Vector4 >& polyVertexes2 );
		template
		RED_REFLECTION_API Bool IsPolygonsIntersecting2D< red::ArraySpan< Vector4 > >( const red::ArraySpan< Vector4 >& polyVertexes1, const red::ArraySpan< Vector4 >& polyVertexes2 );

		// Copyright 2001, softSurfer (www.softsurfer.com)
		// This code may be freely used and modified for any purpose
		// providing that this copyright notice is included with it.
		// SoftSurfer makes no warranty for this code, and cannot be held
		// liable for any real or imagined damage resulting from its use.
		// Users of this code must verify correctness for their application.
		//===================================================================
		// chainHull_2D(): Andrew's monotone chain 2D convex hull algorithm
		//     Input:  P[] = an array of 2D points
		//                   presorted by increasing x- and y-coordinates
		//             n = the number of points in P[]
		//     Output: H[] = an array of the convex hull vertices (max is n)
		//     Return: the number of points in H[]
		void ComputeConvexHull2D( DynArray< Vector2 > &P, DynArray< Vector2 > &H )
		{
			struct OrderX
			{
				RED_INLINE Bool operator()( const Vector2& v1, const Vector2& v2 )  const { return v1.X < v2.X || ( v1.X == v2.X && v1.Y < v2.Y ); }
			};
			std::sort( P.Begin(), P.End(), OrderX() );
			// remove non-unique points
			for ( Int32 i = P.Size()-2; i >= 0; --i )
			{
				for ( Int32 j = i+1; j < Int32( P.Size() ); )
				{
					if ( P[ j ].X - P[ i ].X > std::numeric_limits< Float >::epsilon() )
					{
						break;
					}
					if ( (P[ j ] - P[ i ]).IsAlmostZero( std::numeric_limits< Float >::epsilon() ) )
					{
						P.RemoveAt( j );
					}
					else
					{
						++j;
					}
				}
			}
			Int32 n = P.Size();
			H.Resize( n + 1 );

			// the output array H[] will be used as the stack
			Int32    bot=0, top=(-1);  // indices for bottom and top of the stack
			Int32    i;                // array scan index

			// Get the indices of points with min x-coord and min|max y-coord
			Int32 minmin = 0, minmax;
			Float xmin = P[0].X;
			for (i=1; i<n; i++)
				if (P[i].X != xmin) break;
			minmax = i-1;
			if (minmax == n-1) {       // degenerate case: all x-coords == xmin
				H[++top] = P[minmin];
				if (P[minmax].Y != P[minmin].Y) // a nontrivial segment
					H[++top] = P[minmax];
				H[++top] = P[minmin];           // add polygon endpoint

				H.Resize( top );
				return;
			}

			// Get the indices of points with max x-coord and min|max y-coord
			Int32 maxmin, maxmax = n-1;
			Float xmax = P[n-1].X;
			for (i=n-2; i>=0; i--)
				if (P[i].X != xmax) break;
			maxmin = i+1;

			// Compute the lower hull on the stack H
			H[++top] = P[minmin];      // push minmin point onto stack
			i = minmax;
			while (++i <= maxmin)
			{
				// the lower line joins P[minmin] with P[maxmin]
				if (PointIsLeftOfSegment2D( P[minmin], P[maxmin], P[i]) >= 0 && i < maxmin)
					continue;          // ignore P[i] above or on the lower line

				while (top > 0)        // there are at least 2 points on the stack
				{
					// test if P[i] is left of the line at the stack top
					if (PointIsLeftOfSegment2D( H[top-1], H[top], P[i]) > 0)
						break;         // P[i] is a new hull vertex
					else
						top--;         // pop top point off stack
				}
				H[++top] = P[i];       // push P[i] onto stack
			}

			// Next, compute the upper hull on the stack H above the bottom hull
			if (maxmax != maxmin)      // if distinct xmax points
				H[++top] = P[maxmax];  // push maxmax point onto stack
			bot = top;                 // the bottom point of the upper hull stack
			i = maxmin;
			while (--i >= minmax)
			{
				// the upper line joins P[maxmax] with P[minmax]
				if (PointIsLeftOfSegment2D( P[maxmax], P[minmax], P[i]) >= 0 && i > minmax)
					continue;          // ignore P[i] below or on the upper line

				while (top > bot)    // at least 2 points on the upper stack
				{
					// test if P[i] is left of the line at the stack top
					if (PointIsLeftOfSegment2D( H[top-1], H[top], P[i]) > 0)
						break;         // P[i] is a new hull vertex
					else
						top--;         // pop top point off stack
				}
				H[++top] = P[i];       // push P[i] onto stack
			}
			if (minmax != minmin)
				H[++top] = P[minmin];  // push joining endpoint onto stack

			H.Resize( top );
		}

		// https://www.sciencedirect.com/science/article/pii/S0925772109001448
		Float SignedVolume( Vector4 a, Vector4 b, Vector4 c, Vector4 d )
		{
			const Float oneSixth = 1.0f / 6.0f;
			return oneSixth * ( Vector4::Cross( b - a, c - a ).Dot4( d - a ) );
		}

		Bool TestLineTriangleNoIntersectionPoint3D( const Vector4& lineStart, const Vector4& lineEnd, const Vector4& t0, const Vector4& t1, const Vector4& t2 )
		{
			Bool firstCondition = math::Sgn(SignedVolume(lineStart, t0, t1, t2)) != math::Sgn(SignedVolume(lineEnd, t0, t1, t2));
			Int32 sgnVol0 = math::Sgn( SignedVolume(lineStart, lineEnd, t0, t1));
			Int32 sgnVol1 = math::Sgn( SignedVolume(lineStart, lineEnd, t1, t2));
			Int32 sgnVol2 = math::Sgn( SignedVolume(lineStart, lineEnd, t2, t0));

			Bool secondCondition = (sgnVol0 == sgnVol1) && (sgnVol1 == sgnVol2);
			return firstCondition && secondCondition;
		}

		//https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
		Bool TestRayTriangleIntersection3D( const Vector4& rayOrigin, const Vector4& rayVector, const Vector4& t0, const Vector4& t1, const Vector4& t2, Vector4& outPos, Float& intersectionDistance )
		{
			const Float EPSILON = 0.0000001f;
			const Vector4& vertex0 = t0;
			const Vector4& vertex1 = t1;
			const Vector4& vertex2 = t2;
			Vector4 edge1, edge2, h, s, q;
			Float a, f, u, v;
			edge1 = vertex1 - vertex0;
			edge2 = vertex2 - vertex0;
			h = math::Vector4::Cross( rayVector, edge2, 0.f );
			a = edge1.Dot3( h );

			if ( a > -EPSILON && a < EPSILON )
				return false;

			f = 1.f / a;
			s = rayOrigin - vertex0;
			u = f * (s.Dot3( h ));
			if ( u < 0.0f || u > 1.0f )
				return false;

			q = math::Vector4::Cross( s, edge1, 0.f );
			v = f * rayVector.Dot3( q );
			if ( v < 0.0f || u + v > 1.0f )
				return false;

			// At this stage we can compute t to find out where the intersection point is on the line.
			intersectionDistance = f * edge2.Dot3( q );

			if ( intersectionDistance > EPSILON ) // ray intersection
			{
				outPos = rayOrigin + rayVector * intersectionDistance;
				return true;
			}
			else // This means that there is a line intersection but not a ray intersection.
				return false;
		}

		Float OrientedBoxSquaredDistance( const Matrix &obbLocalToWorld, const Vector4 &point )
		{
			const Vector4 scale = Vector4::Max4( Vector4::ONES() * 0.0001f, obbLocalToWorld.GetScale33() );
			const Matrix mat = Matrix(obbLocalToWorld).SetScale33( Vector4::ONES() / scale ).OrthonormInverted();
			const Vector4 tformPoint = mat.TransformPoint( point );
			return Box( -scale, scale ).SquaredDistance( tformPoint );
		}

	}
	

	//////////////////////////////////////////////////////////////////////////

	namespace InterpolationUtils
	{
		Float Interpolate( Float from, Float to, Float t )
		{
			return from + ( to - from ) * t;
		}

		Vector4 HermiteInterpolate( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, Float t )
		{
			const Vector4 tangent2 = ( v3 - v1 ).Normalized3();
			const Vector4 tangent3 = ( v4 - v2 ).Normalized3();

			return HermiteInterpolateWithTangents(v2, tangent2, v3, tangent3, t);
		}

		Vector4 HermiteInterpolateWithTangents( const Vector4 & v1, const Vector4 & t1, const Vector4 & v2, const Vector4 & t2, Float t )
		{
			const Float t_Sqr = t * t;
			const Float t_Cub = t * t_Sqr;

			const Float h01 = 3*t_Sqr - 2*t_Cub;
			const Float h00 = 1 - h01;
			const Float h10 = t_Cub - 2*t_Sqr + t;
			const Float h11 = t_Cub - t_Sqr;

			const Float h = ( v2 - v1 ).Mag3();

			return v1 * h00 + t1 * ( h10 * h ) + v2 * h01 + t2 * ( h11 * h );
		}

		Vector4 CubicInterpolate( const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, Float t )
		{
			Float  t_Sqr = t * t;

			Vector4 a0 = v4 - v3 - v1 + v2;
			Vector4 a1 = v1 - v2 - a0;
			Vector4 a2 = v3 - v1;
			Vector4 a3 = v2;

			return a0*t*t_Sqr + a1*t_Sqr + a2*t + a3;
		}

		void CatmullRomBuildTauMatrix( Matrix& outMat, Float tau )
		{
			outMat[0].X = 0.0f;
			outMat[0].Y = 1.0f;
			outMat[0].Z = 0.0f;
			outMat[0].W = 0.0f;

			outMat[1].X = -tau;
			outMat[1].Y = 0.0f;
			outMat[1].Z = tau;
			outMat[1].W = 0.0f;


			outMat[2].X = 2.0f * tau;
			outMat[2].Y = tau - 3.0f;
			outMat[2].Z = 3.0f - 2.0f*tau;
			outMat[2].W = -tau;

			outMat[3].X = -tau;
			outMat[3].Y = 2.0f - tau;
			outMat[3].Z = tau - 2.0f;
			outMat[3].W = tau;
		}

		Vector4 CatmullRomInterpolate( const Vector4 &v1, const Vector4 &v2, const Vector4 &v3, const Vector4 &v4, Float t, const Matrix& tauMatrix ) 
		{
			Float t2 = t*t;
			Vector4 tVec( 1.0f, t, t2, t2*t );
			Matrix pointsMatrix( v1, v2, v3, v4 );
			Vector4 vec = tauMatrix.TransformVectorWithW( tVec );
			vec = pointsMatrix.TransformVectorWithW( vec );
			return vec;
		}

		void DistanceToEdge( const Vector4 &a, const Vector4 &b, const Vector4 &pt, Float &dist, Float &alpha )
		{
			Vector4 edge = (b - a).Normalized3();
			Float ta = Vector4::Dot3( edge, a );
			Float tb = Vector4::Dot3( edge, b );
			Float p = Vector4::Dot3( edge, pt );
			if ( p >= ta && p <= tb )
			{
				alpha = p - ta;
				Vector4 projected = a + edge * alpha;
				alpha /= tb - ta;
				dist  = pt.DistanceTo( projected );
			}
			else if ( p < ta )
			{
				alpha = 0.f;
				dist  = pt.DistanceTo( a );
			}
			else
			{
				alpha = 1.f;
				dist  = pt.DistanceTo( b );
			}
		}

		Float HermiteClosestPoint(  const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, const Vector4 & p, Vector4 &res, Float epsilon )
		{
			return HermiteClosestPointWithTangent( v2, ( v3 - v1 ).Normalized3(), v3, ( v4 - v2 ).Normalized3(), p, res, epsilon );
		}

		Float HermiteClosestPointWithTangent(  const Vector4 & p1, const Vector4 & t1, const Vector4 & p2, const Vector4 & t2, const Vector4 & p, Vector4 &res, Float epsilon )
		{
			Float distAlpha, distBest;
			DistanceToEdge( p1, p2, p, distBest, distAlpha );

			Float deltaAlpha  = 0.25f;

			Float h = ( p2 - p1 ).Mag3() * 2.f;
			Vector4 tangent2times2 = t1;
			Vector4 tangent3times2 = t2;

			Uint32 iterations = 0;
			for ( ;; )
			{
				++iterations;

				Float  t = distAlpha;

				Float t_Sqr = t * t;
				Float t_Cub = t * t_Sqr;
				Float h01 = -2*t_Cub + 3*t_Sqr;
				Float h00 = 1 - h01;
				Float h10 = t_Cub - 2*t_Sqr + t;
				Float h11 = t_Cub - t_Sqr;
				Vector4 pLine0 = p1 * h00 + tangent2times2 * ( h10 * 0.5f * h ) + p2 * h01 + tangent3times2 * ( h11 * 0.5f * h );

				t = distAlpha + 0.01f;

				t_Sqr = t * t;
				t_Cub = t * t_Sqr;
				h01 = -2*t_Cub + 3*t_Sqr;
				h00 = 1 - h01;
				h10 = t_Cub - 2*t_Sqr + t;
				h11 = t_Cub - t_Sqr;
				Vector4 pLine1 = p1 * h00 + tangent2times2 * ( h10 * 0.5f * h ) + p2 * h01 + tangent3times2 * ( h11 * 0.5f * h );

				Vector4 vecToLine  = p - pLine0;
				Vector4 vecTangent = pLine1 - pLine0;

				Float cosAngle = Vector4::Dot3( vecToLine, vecTangent );

				// 90degs?
				if ( MAbs(cosAngle) < epsilon )
				{
					//RED_LOG( "Core: Dist to Path iterations: %d (SUCCESS) error = %f"), iterations, cosAngle );
					res = pLine0;
					return distAlpha;
				}

				// Move towards 
				if ( cosAngle < 0 )
				{
					if ( distAlpha <= 0.f )
					{
						//RED_LOG( "Core: Dist to Path iterations: %d (Less than 0.f) error = %f"), iterations, cosAngle );
						res = p1;
						return distAlpha;
					}
					distAlpha -= Min( deltaAlpha, distAlpha );
				}
				else
				{
					if ( distAlpha >= 1.f )
					{
						//RED_LOG( "Core: Dist to Path iterations: %d (More than 1.f) error = %f"), iterations, cosAngle );
						res = p2;
						return distAlpha;
					}
					distAlpha += Min( deltaAlpha, 1.f - distAlpha );
				}
				// Make steps smaller and smaller
				deltaAlpha *= 0.5f;

				// Limit total no of iterations
				if ( iterations > 10 )
				{
					//RED_LOG( "Core: Dist to Path iterations: %d (Max iterations) error = %f"), iterations, cosAngle );
					res = pLine0;
					return distAlpha;
				}
			}
		}

		Float CubicClosestPoint(  const Vector4 & v1, const Vector4 & v2, const Vector4 & v3, const Vector4 & v4, const Vector4 & p, Vector4 &res, Float epsilon )
		{
			Vector4 a0 = v4 - v3 - v1 + v2;
			Vector4 a1 = v1 - v2 - a0;
			Vector4 a2 = v3 - v1;
			Vector4 a3 = v2;

			Float distAlpha, distBest;
			DistanceToEdge( v2, v3, p, distBest, distAlpha );

			Float deltaAlpha  = 0.25f;

			Uint32 iterations = 0;
			for ( ;; )
			{
				++iterations;

				Float  t     = distAlpha;
				Float  t_Sqr = t * t;
				Vector4 pLine0 = a0*t*t_Sqr + a1*t_Sqr + a2*t + a3;

				t     = distAlpha + 0.01f;
				t_Sqr = t * t;
				Vector4 pLine1 = a0*t*t_Sqr + a1*t_Sqr + a2*t + a3;

				Vector4 vecToLine  = p - pLine0;
				Vector4 vecTangent = pLine1 - pLine0;

				Float cosAngle = Vector4::Dot3( vecToLine, vecTangent );

				// 90degs?
				if ( MAbs(cosAngle) < epsilon )
				{
					//RED_LOG( "Core: Dist to Path iterations: %d (SUCCESS) error = %f"), iterations, cosAngle );
					res = pLine0;
					return distAlpha;
				}

				// Move towards 
				if ( cosAngle < 0 )
				{
					if ( distAlpha <= 0.f )
					{
						//RED_LOG( "Core: Dist to Path iterations: %d (Less than 0.f) error = %f"), iterations, cosAngle );
						res = v2;
						return distAlpha;
					}
					distAlpha -= Min( deltaAlpha, distAlpha );
				}
				else
				{
					if ( distAlpha >= 1.f )
					{
						//RED_LOG( "Core: Dist to Path iterations: %d (More than 1.f) error = %f"), iterations, cosAngle );
						res = v3;
						return distAlpha;
					}
					distAlpha += Min( deltaAlpha, 1.f - distAlpha );
				}
				// Make steps smaller and smaller
				deltaAlpha *= 0.5f;

				// Limit total no of iterations
				if ( iterations > 10 )
				{
					//RED_LOG( "Core: Dist to Path iterations: %d (Max iterations) error = %f"), iterations, cosAngle );
					res = pLine0;
					return distAlpha;
				}
			}
		}

		void InterpolatePoints( const DynArray< Vector4 > &keyPoints, DynArray< Vector4 > &outPoints, Float pointsDist, Bool closed )
		{
			RED_ASSERT( pointsDist > 0.f );
			RED_ASSERT( keyPoints.Size() > 0 );

			Uint32 nVertices = keyPoints.Size();
			closed         = closed && nVertices > 2;
			Uint32 nEdges    = closed ? nVertices : nVertices - 1;

			for ( Uint32 i = 0; i < nEdges; ++i )
			{
				const Vector4 &p1 = i > 0 
					? keyPoints[ i - 1 ]
				: closed ? keyPoints[ nVertices-1 ] : keyPoints[ 0 ];
				const Vector4 &p2 = keyPoints[ i                   ];
				const Vector4 &p3 = keyPoints[ (i + 1) % nVertices ];
				const Vector4 &p4 = closed
					? keyPoints[ (i + 2) % nVertices ]
				: keyPoints[ Min<Int32>( nVertices-1, i+2 )];

				Float dist   = p2.DistanceTo( p3 );
				Uint32  numPts = Min<Uint32>( 15, Max<Uint32>( 8, static_cast<Uint32>( MCeil( dist / pointsDist ) ) ) );
				Float step   = 1.f / ( numPts + 1 );
				Float alpha  = step;

				outPoints.PushBack( p2 );

				for ( Uint32 i = 0; i < numPts; ++i )
				{
					outPoints.PushBack( HermiteInterpolate( p1,p2,p3,p4, alpha ) );
					alpha += step;
				}
			}

			outPoints.PushBack( closed ? keyPoints[ 0 ] : keyPoints[ nVertices-1 ] );
		}
	} // Interpolation Utils

} // Math Utils
