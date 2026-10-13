// Code taken from: https://github.com/ivanfratric/polypartition

#include "build.h"

#include "../../common/redReflection/include/mathUtils.h"
#include "polygonTriangulation.h"

#define TPPL_VERTEXTYPE_REGULAR 0
#define TPPL_VERTEXTYPE_START	1
#define TPPL_VERTEXTYPE_END		2
#define TPPL_VERTEXTYPE_SPLIT	3
#define TPPL_VERTEXTYPE_MERGE	4

#define TPPL_CCW 1
#define TPPL_CW -1

ConvexPartitionPoly::ConvexPartitionPoly()
{
	mHole = false;
	mNumPoints = 0;
	mPoints = nullptr;
}

ConvexPartitionPoly::~ConvexPartitionPoly()
{
	if ( nullptr != mPoints )
	{
		RED_DELETE_ARRAY( mPoints, mNumPoints );
	}
}

void ConvexPartitionPoly::Clear()
{
	if ( nullptr != mPoints )
	{
		RED_DELETE_ARRAY( mPoints, mNumPoints );
	}

	mHole = false;
	mNumPoints = 0;
	mPoints = nullptr;
}

void ConvexPartitionPoly::Init( Int32 numpoints )
{
	Clear();
	mNumPoints = numpoints;
	mPoints = RED_NEW_ARRAY( ConvexPartitionVertex, numpoints );
}

void ConvexPartitionPoly::Triangle( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 )
{
	Init( 3 );
	mPoints[0] = p1;
	mPoints[1] = p2;
	mPoints[2] = p3;
}

ConvexPartitionPoly::ConvexPartitionPoly( const ConvexPartitionPoly& src )
{
	mHole = src.mHole;
	mNumPoints = src.mNumPoints;

	mPoints = RED_NEW_ARRAY( ConvexPartitionVertex, mNumPoints );
	red::Memcpy( mPoints, src.mPoints, mNumPoints * sizeof( ConvexPartitionVertex ) );
}

ConvexPartitionPoly& ConvexPartitionPoly::operator=( const ConvexPartitionPoly& src )
{
	if( this != &src )
	{
		Clear();
		mHole = src.mHole;
		mNumPoints = src.mNumPoints;

		mPoints = RED_NEW_ARRAY( ConvexPartitionVertex, mNumPoints );
		red::Memcpy( mPoints, src.mPoints, mNumPoints * sizeof( ConvexPartitionVertex ) );
		
	}

	return *this;
}

Int32 ConvexPartitionPoly::GetOrientation()
{
	Int32 i1, i2;
	Float area = 0;
	for( i1 = 0; i1 < mNumPoints; i1++ )
	{
		i2 = i1 + 1;
		if ( i2 == mNumPoints )
		{
			i2 = 0;
		}

		area += mPoints[i1].mPoint.X * mPoints[i2].mPoint.Y - mPoints[i1].mPoint.Y * mPoints[i2].mPoint.X;
	}

	if( area > 0 ) { return TPPL_CCW; }
	if( area < 0 ) { return TPPL_CW; }

	return 0;
}

void ConvexPartitionPoly::SetOrientation( Int32 orientation )
{
	Int32 polyorientation = GetOrientation();
	if ( polyorientation && ( polyorientation != orientation ) )
	{
		Invert();
	}
}

void ConvexPartitionPoly::Invert()
{
	Int32 i;
	ConvexPartitionVertex* invpoints;

	invpoints = RED_NEW_ARRAY( ConvexPartitionVertex, mNumPoints );
	for ( i = 0; i < mNumPoints; i++ )
	{
		invpoints[i] = mPoints[mNumPoints - i - 1];
	}

	RED_DELETE_ARRAY( mPoints, mNumPoints );
	mPoints = invpoints;
}

Bool ConvexPartitionEngine::IsConvex( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 )
{
	Float tmp;
	tmp = ( p3.mPoint.Y - p1.mPoint.Y ) * ( p2.mPoint.X - p1.mPoint.X ) - ( p3.mPoint.X - p1.mPoint.X ) * ( p2.mPoint.Y - p1.mPoint.Y );
	return tmp > 0;
}

Bool ConvexPartitionEngine::IsReflex( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 )
{
	Float tmp;
	tmp = ( p3.mPoint.Y - p1.mPoint.Y ) * ( p2.mPoint.X - p1.mPoint.X ) - ( p3.mPoint.X - p1.mPoint.X ) * ( p2.mPoint.Y - p1.mPoint.Y );
	return tmp < 0;
}

Bool ConvexPartitionEngine::IsInside( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3, const ConvexPartitionVertex& p )
{
	return !IsConvex( p1, p, p2 ) && !IsConvex( p2, p, p3 ) && !IsConvex( p3, p, p1 );
}

Bool ConvexPartitionEngine::InCone( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3, const ConvexPartitionVertex& p )
{
	Bool convex = IsConvex( p1, p2, p3 );
	if ( convex )
	{
		if ( !IsConvex( p1, p2, p ) )

			return false;
		if ( !IsConvex( p2, p3, p ) )

			return false;
		return true;
	}
	else
	{
		if ( IsConvex( p1, p2, p ) )
			return true;

		if ( IsConvex( p2, p3, p ) )
			return true;

		return false;
	}
}

void ConvexPartitionEngine::UpdateVertex( PartitionVertexInternal* v, PartitionVertexInternal* vertices, Int32 numvertices )
{
	Int32 i;
	PartitionVertexInternal* v1, *v3;
	Vector2 vec1, vec3;

	v1 = v->previous;
	v3 = v->next;

	v->isConvex = IsConvex( v1->p, v->p, v3->p );

	vec1 = ( v1->p.mPoint - v->p.mPoint ).Normalized();
	vec3 = ( v3->p.mPoint - v->p.mPoint ).Normalized();
	v->angle = vec1.X * vec3.X + vec1.Y * vec3.Y;

	if ( v->isConvex )
	{
		v->isEar = true;
		for ( i = 0; i < numvertices; i++ )
		{
			if ( ( vertices[i].p.mPoint.X == v->p.mPoint.X ) && ( vertices[i].p.mPoint.Y == v->p.mPoint.Y ) )
				continue;

			if ( ( vertices[i].p.mPoint.X == v1->p.mPoint.X ) && ( vertices[i].p.mPoint.Y == v1->p.mPoint.Y ) )
				continue;

			if ( ( vertices[i].p.mPoint.X == v3->p.mPoint.X ) && ( vertices[i].p.mPoint.Y == v3->p.mPoint.Y ) )
				continue;

			if ( IsInside( v1->p, v->p, v3->p, vertices[i].p ) )
			{
				v->isEar = false;
				break;
			}
		}
	}
	else
	{
		v->isEar = false;
	}
}

Bool ConvexPartitionEngine::VerifyPolygon( const ConvexPartitionPoly* poly )
{
	if ( poly->GetNumPoints() < 3 )
	{
		return false;
	}

	if ( poly->GetNumPoints() == 3 )
	{
		return true;
	}

	for ( Int32 i = 0; i < poly->GetNumPoints() - 1; ++i )
	{
		const Vector2& p1 = poly->GetPoint( i ).mPoint;
		const Vector2& p2 = poly->GetPoint( i + 1 ).mPoint;

		for ( Int32 j = i + 2; j < poly->GetNumPoints(); ++j )
		{
			const Int32 k = ( j < poly->GetNumPoints() - 1 ) ? ( j + 1 ) : 0;
			if ( k == i )
			{
				continue;
			}

			const Vector2& p3 = poly->GetPoint( j ).mPoint;
			const Vector2& p4 = poly->GetPoint( k ).mPoint;

			if ( MathUtils::GeometryUtils::TestIntersectionLineLine2D( p1, p2, p3, p4 ) )
			{
				return false;
			}
		}
	}

	return true;
}

Bool ConvexPartitionEngine::Triangulate( const ConvexPartitionPoly* poly, ConvexPartitionPolyList* triangles )
{
	long numvertices;
	PartitionVertexInternal *vertices = nullptr;
	PartitionVertexInternal *ear = nullptr;
	ConvexPartitionPoly triangle;
	long i, j;
	bool earfound;

	if ( poly->GetNumPoints() < 3 )
	{
		return 0;
	}

	if ( poly->GetNumPoints() == 3 )
	{
		triangles->PushBack( *poly );
		return 1;
	}

	numvertices = poly->GetNumPoints();
	vertices = RED_NEW_ARRAY( PartitionVertexInternal, numvertices );
	for ( i = 0; i < numvertices; i++ )
	{
		vertices[i].isActive = true;
		vertices[i].p = poly->GetPoint( i );

		if ( i == ( numvertices - 1 ) )
		{
			vertices[i].next = &( vertices[0] );
		}
		else
		{
			vertices[i].next = &( vertices[i + 1] );
		}

		if ( i == 0 )
		{
			vertices[i].previous = &( vertices[numvertices - 1] );
		}
		else
		{
			vertices[i].previous = &( vertices[i - 1] );
		}
	}

	for ( i = 0; i < numvertices; i++ )
	{
		UpdateVertex( &vertices[i], vertices, numvertices );
	}

	for ( i = 0; i < numvertices - 3; i++ )
	{
		earfound = false;
		//find the most extruded ear
		for ( j = 0; j < numvertices; j++ )
		{
			if ( !vertices[j].isActive )
				continue;

			if ( !vertices[j].isEar )
				continue;

			if ( !earfound )
			{
				earfound = true;
				ear = &( vertices[j] );
			}
			else if ( vertices[j].angle > ear->angle )
			{
				ear = &( vertices[j] );
			}
		}

		if ( !earfound )
		{
			RED_DELETE_ARRAY( vertices, numvertices );
			return 0;
		}

		triangle.Triangle( ear->previous->p, ear->p, ear->next->p );
		triangles->PushBack( triangle );

		ear->isActive = false;
		ear->previous->next = ear->next;
		ear->next->previous = ear->previous;

		if ( i == numvertices - 4 )
		{
			break;
		}

		UpdateVertex( ear->previous, vertices, numvertices );
		UpdateVertex( ear->next, vertices, numvertices );
	}

	for ( i = 0; i < numvertices; i++ )
	{
		if ( vertices[i].isActive )
		{
			triangle.Triangle( vertices[i].previous->p, vertices[i].p, vertices[i].next->p );
			triangles->PushBack( triangle );
			break;
		}
	}

	RED_DELETE_ARRAY( vertices, numvertices );
	return 1;
}

Bool ConvexPartitionEngine::Triangulate( const ConvexPartitionPolyList* polys, ConvexPartitionPolyList* triangles )
{
	ConvexPartitionPolyList outpolys{ red::PoolEngine() };

	if ( !RemoveHoles( polys, &outpolys ) )
	{
		return false;
	}

	for ( const auto& poly : outpolys )
	{
		if ( !Triangulate( &poly, triangles ) )
		{
			return false;
		}
	}

	return true;
}

// Hertel-Mehlhorn algorithm
Bool ConvexPartitionEngine::ConvexPartition( const ConvexPartitionPoly* poly, ConvexPartitionPolyList* parts )
{
	ConvexPartitionPolyList triangles{ red::PoolEngine() };
	ConvexPartitionPoly* poly1, *poly2;
	ConvexPartitionPoly newpoly;
	ConvexPartitionVertex d1, d2, p1, p2, p3;
	Int32 i11, i12, i21, i22, i13, i23, j, k;
	Bool isdiagonal;
	Int32 numreflex;

	//check if the poly is already convex
	numreflex = 0;
	for ( i11 = 0; i11 < poly->GetNumPoints(); i11++ )
	{
		if ( i11 == 0 )
		{
			i12 = poly->GetNumPoints() - 1;
		}
		else
		{
			i12 = i11 - 1;
		}

		if ( i11 == ( poly->GetNumPoints() - 1 ) )
		{
			i13 = 0;
		}
		else
		{
			i13 = i11 + 1;
		}

		if ( IsReflex( poly->GetPoint( i12 ), poly->GetPoint( i11 ), poly->GetPoint( i13 ) ) )
		{
			numreflex = 1;
			break;
		}
	}

	if( numreflex == 0 )
	{
		parts->PushBack( *poly );
		return 1;
	}

	if ( !Triangulate( poly, &triangles ) )
	{
		return 0;
	}

	Uint32 secondIndex = 0;
	for( Uint32 firstIndex = 0; firstIndex < triangles.Size(); firstIndex++ )
	{
		poly1 = &triangles[firstIndex];

		for ( i11 = 0; i11 < poly1->GetNumPoints(); i11++ )
		{
			d1 = poly1->GetPoint( i11 );
			i12 = ( i11 + 1 ) % ( poly1->GetNumPoints() );
			d2 = poly1->GetPoint( i12 );

			isdiagonal = false;
			for ( secondIndex = firstIndex; secondIndex < triangles.Size(); secondIndex++ )
			{
				if ( firstIndex == secondIndex ) { continue; }
				poly2 = &triangles[secondIndex];

				for ( i21 = 0; i21 < poly2->GetNumPoints(); i21++ )
				{
					if ( ( d2.mPoint.X != poly2->GetPoint( i21 ).mPoint.X ) || ( d2.mPoint.Y != poly2->GetPoint( i21 ).mPoint.Y ) )
					{
						continue;
					}

					i22 = ( i21 + 1 ) % ( poly2->GetNumPoints() );
					if ( ( d1.mPoint.X != poly2->GetPoint( i22 ).mPoint.X ) || ( d1.mPoint.Y != poly2->GetPoint( i22 ).mPoint.Y ) )
					{
						continue;
					}

					isdiagonal = true;
					break;
				}

				if ( isdiagonal )
				{
					break;
				}
			}

			if ( !isdiagonal )
			{
				continue;
			}

			p2 = poly1->GetPoint( i11 );
			if ( i11 == 0 )
			{
				i13 = poly1->GetNumPoints() - 1;
			}
			else
			{
				i13 = i11 - 1;
			}

			p1 = poly1->GetPoint( i13 );
			if ( i22 == ( poly2->GetNumPoints() - 1 ) )
			{
				i23 = 0;
			}
			else
			{
				i23 = i22 + 1;
			}
			p3 = poly2->GetPoint( i23 );

			if ( !IsConvex( p1, p2, p3 ) )
			{
				continue;
			}

			p2 = poly1->GetPoint( i12 );
			if ( i12 == ( poly1->GetNumPoints() - 1 ) )
			{
				i13 = 0;
			}
			else
			{
				i13 = i12 + 1;
			}

			p3 = poly1->GetPoint( i13 );
			if ( i21 == 0 )
			{
				i23 = poly2->GetNumPoints() - 1;
			}
			else
			{
				i23 = i21 - 1;
			}
			p1 = poly2->GetPoint( i23 );

			if ( !IsConvex( p1, p2, p3 ) )
			{
				continue;
			}

			newpoly.Init( poly1->GetNumPoints() + poly2->GetNumPoints() - 2 );
			k = 0;
			for ( j = i12; j != i11; j = ( j + 1 ) % ( poly1->GetNumPoints() ) )
			{
				newpoly[k] = poly1->GetPoint( j );
				k++;
			}
			for ( j = i22; j != i21; j = ( j + 1 ) % ( poly2->GetNumPoints() ) )
			{
				newpoly[k] = poly2->GetPoint( j );
				k++;
			}

			triangles.RemoveAt( secondIndex );
			triangles[ firstIndex ] = newpoly;
			poly1 = &triangles[ firstIndex ];
			i11 = -1;

			continue;
		}
	}

	for ( const ConvexPartitionPoly& poly : triangles )
	{
		parts->PushBack( poly );
	}

	return 1;
}

int Intersects( Vector2 &p11, Vector2 &p12, Vector2 &p21, Vector2 &p22 )
{
	if ( ( p11.X == p21.X ) && ( p11.Y == p21.Y ) ) return 0;
	if ( ( p11.X == p22.X ) && ( p11.Y == p22.Y ) ) return 0;
	if ( ( p12.X == p21.X ) && ( p12.Y == p21.Y ) ) return 0;
	if ( ( p12.X == p22.X ) && ( p12.Y == p22.Y ) ) return 0;

	Vector2 v1ort, v2ort, v;
	Float dot11, dot12, dot21, dot22;

	v1ort.X = p12.Y - p11.Y;
	v1ort.Y = p11.X - p12.X;

	v2ort.X = p22.Y - p21.Y;
	v2ort.Y = p21.X - p22.X;

	v = p21 - p11;
	dot21 = v.X*v1ort.X + v.Y*v1ort.Y;
	v = p22 - p11;
	dot22 = v.X*v1ort.X + v.Y*v1ort.Y;

	v = p11 - p21;
	dot11 = v.X*v2ort.X + v.Y*v2ort.Y;
	v = p12 - p21;
	dot12 = v.X*v2ort.X + v.Y*v2ort.Y;

	if ( dot11*dot12 > 0 ) return 0;
	if ( dot21*dot22 > 0 ) return 0;

	return 1;
}

Bool ConvexPartitionEngine::RemoveHoles( const ConvexPartitionPolyList* inpolys, ConvexPartitionPolyList* outpolys )
{
	ConvexPartitionPolyList polys{ red::PoolEngine() };
	ConvexPartitionPolyList::const_iterator holeiter, iter, iter2;
	ConvexPartitionPolyList::const_iterator polyiter;
	long i, i2, holepointindex, polypointindex;
	ConvexPartitionVertex holepoint, polypoint, bestpolypoint;
	ConvexPartitionVertex linep1, linep2;
	ConvexPartitionVertex v1, v2;
	ConvexPartitionPoly newpoly;
	bool hasholes;
	bool pointvisible;
	bool pointfound;

	//check for trivial case (no holes)
	hasholes = false;
	for ( iter = inpolys->Begin(); iter != inpolys->End(); iter++ )
	{
		if ( iter->IsHole() )
		{
			hasholes = true;
			break;
		}
	}

	if ( !hasholes )
	{
		for ( iter = inpolys->Begin(); iter != inpolys->End(); iter++ ) 
		{
			outpolys->PushBack( *iter );
		}
		return true;
	}

	polys = *inpolys;

	for ( ;; )
	{
		// find the hole point with the largest x
		hasholes = false;
		for ( iter = polys.Begin(); iter != polys.End(); iter++ )
		{
			if ( !iter->IsHole() )
				continue;

			if ( !hasholes )
			{
				hasholes = true;
				holeiter = iter;
				holepointindex = 0;
			}

			for ( i = 0; i < iter->GetNumPoints(); i++ )
			{
				if ( iter->GetPoint( i ).mPoint.X > holeiter->GetPoint( holepointindex ).mPoint.X )
				{
					holeiter = iter;
					holepointindex = i;
				}
			}
		}

		if ( !hasholes )
			break;

		holepoint = holeiter->GetPoint( holepointindex );

		pointfound = false;
		for ( iter = polys.Begin(); iter != polys.End(); iter++ )
		{
			if ( iter->IsHole() )
				continue;

			for ( i = 0; i < iter->GetNumPoints(); i++ )
			{
				if ( iter->GetPoint( i ).mPoint.X <= holepoint.mPoint.X )
					continue;

				if ( !InCone( iter->GetPoint( ( i + iter->GetNumPoints() - 1 ) % ( iter->GetNumPoints() ) ),
							  iter->GetPoint( i ),
							  iter->GetPoint( ( i + 1 ) % ( iter->GetNumPoints() ) ),
							  holepoint ) )
				{
					continue;
				}

				polypoint = iter->GetPoint( i );

				if ( pointfound )
				{
					v1.mPoint = ( polypoint.mPoint - holepoint.mPoint ).Normalized();
					v2.mPoint = ( bestpolypoint.mPoint - holepoint.mPoint ).Normalized();
					if ( v2.mPoint.X > v1.mPoint.X )
						continue;
				}

				pointvisible = true;
				for ( iter2 = polys.Begin(); iter2 != polys.End(); iter2++ )
				{
					if ( iter2->IsHole() )
						continue;

					for ( i2 = 0; i2 < iter2->GetNumPoints(); i2++ )
					{
						linep1 = iter2->GetPoint( i2 );
						linep2 = iter2->GetPoint( ( i2 + 1 ) % ( iter2->GetNumPoints() ) );

						if ( Intersects( holepoint.mPoint, polypoint.mPoint, linep1.mPoint, linep2.mPoint ) )
						//if ( MathUtils::GeometryUtils::TestIntersectionLineLine2D( holepoint.mPoint, polypoint.mPoint, linep1.mPoint, linep2.mPoint ) )
						{
							pointvisible = false;
							break;
						}
					}
					if ( !pointvisible ) break;
				}

				if ( pointvisible )
				{
					pointfound = true;
					bestpolypoint = polypoint;
					polyiter = iter;
					polypointindex = i;
				}
			}
		}

		if ( !pointfound )
			return false;

		newpoly.Init( holeiter->GetNumPoints() + polyiter->GetNumPoints() + 2 );
		i2 = 0;
		for ( i = 0; i <= polypointindex; i++ )
		{
			newpoly[i2] = polyiter->GetPoint( i );
			i2++;
		}

		for ( i = 0; i <= holeiter->GetNumPoints(); i++ )
		{
			newpoly[i2] = holeiter->GetPoint( ( i + holepointindex ) % holeiter->GetNumPoints() );
			i2++;
		}

		for ( i = polypointindex; i < polyiter->GetNumPoints(); i++ )
		{
			newpoly[i2] = polyiter->GetPoint( i );
			i2++;
		}

		RED_FATAL_ASSERT( holeiter != polyiter );
		if ( holeiter > polyiter )
		{
			polys.Remove( holeiter );
			polys.Remove( polyiter );
		}
		else
		{
			polys.Remove( polyiter );
			polys.Remove( holeiter );
		}

		polys.PushBack( newpoly );
	}

	for ( iter = polys.Begin(); iter != polys.End(); iter++ )
	{
		outpolys->PushBack( *iter );
	}

	return true;
}