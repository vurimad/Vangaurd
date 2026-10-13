// Code taken from: https://github.com/ivanfratric/polypartition
#pragma once

#include "../../redContainers/include/dynArray.h"

struct ConvexPartitionVertex
{
	RED_USE_MEMORY_POOL( red::PoolCore );	

	Vector2 mPoint;
	Int32 mIndex;

	ConvexPartitionVertex()
		: mPoint( Vector2( 0.0f, 0.0f ) )
		, mIndex( -1 )
	{ }
};

class RED_CORE_API ConvexPartitionPoly
{
protected:
	ConvexPartitionVertex* mPoints;
	Int32 mNumPoints;
	Bool mHole;

public:
	ConvexPartitionPoly();
	~ConvexPartitionPoly();

	ConvexPartitionPoly( const ConvexPartitionPoly& src );
	ConvexPartitionPoly& operator=( const ConvexPartitionPoly& src );

	RED_FORCE_INLINE Int32 GetNumPoints() const
	{
		return mNumPoints;
	}

	RED_FORCE_INLINE Bool IsHole() const
	{
		return mHole;
	}

	RED_FORCE_INLINE void SetHole( Bool hole )
	{
		mHole = hole;
	}

	RED_FORCE_INLINE ConvexPartitionVertex& GetPoint( Int32 i )
	{
		return mPoints[i];
	}

	RED_FORCE_INLINE const ConvexPartitionVertex& GetPoint( Int32 i ) const
	{
		return mPoints[i];
	}

	RED_FORCE_INLINE ConvexPartitionVertex* GetPoints()
	{
		return mPoints;
	}

	RED_FORCE_INLINE const ConvexPartitionVertex* GetPoints() const
	{
		return mPoints;
	}

	RED_FORCE_INLINE ConvexPartitionVertex& operator[]( Int32 i )
	{
		return mPoints[i];
	}

	RED_FORCE_INLINE const ConvexPartitionVertex& operator[]( Int32 i ) const
	{
		return mPoints[i];
	}

	void Clear();

	void Init( Int32 numpoints );

	void Triangle( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 );

	void Invert();

	Int32 GetOrientation();

	void SetOrientation( Int32 orientation );
};

typedef red::DynArray< ConvexPartitionPoly > ConvexPartitionPolyList;

class RED_CORE_API ConvexPartitionEngine
{
public:
	static Bool IsConvex( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 );
	static Bool IsReflex( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3 );
	static Bool IsInside( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3, const ConvexPartitionVertex& p );
	static Bool InCone( const ConvexPartitionVertex& p1, const ConvexPartitionVertex& p2, const ConvexPartitionVertex& p3, const ConvexPartitionVertex& p );

	// check if the polygon is valid (no crossing edges)
	static Bool VerifyPolygon( const ConvexPartitionPoly* poly );

	//!!!very important to check your polygon's winding before using these functions

	// triangulates a polygon by ear clipping
	// time complexity O(n^2), n is the number of vertices
	// space complexity: O(n)
	static Bool Triangulate( const ConvexPartitionPoly* poly, ConvexPartitionPolyList* triangles );

	// triangulates a list of polygons that may contain holes by ear clipping algorithm
	static Bool Triangulate( const ConvexPartitionPolyList* polys, ConvexPartitionPolyList* triangles );

	// Partitions a polygon into convex polygons by using Hertel-Mehlhorn algorithm
	// The algorithm gives at most four times the number of parts as the optimal algorithm
	// However, in practice it works much better than that and often gives optimal partition
	// Uses triangulation obtained by ear clipping as intermediate result
	// time complexity O(n^2), n is the number of vertices
	// space complexity: O(n)
	static Bool ConvexPartition( const ConvexPartitionPoly* poly, ConvexPartitionPolyList* parts );
	
	// Simple heuristic procedure for removing holes from a list of polygons
	// works by creating a diagonal from the rightmost hole vertex to some visible vertex
	// time complexity: O(h*(n^2)), h is the number of holes, n is the number of vertices
	// space complexity: O(n)
	static Bool RemoveHoles( const ConvexPartitionPolyList* inpolys, ConvexPartitionPolyList* outpolys );

private:
	struct PartitionVertexInternal
	{
		RED_USE_MEMORY_POOL( red::PoolCore );

		PartitionVertexInternal* previous;
		PartitionVertexInternal* next;
		ConvexPartitionVertex p;
		Float angle;
		Bool isActive;
		Bool isConvex;
		Bool isEar;
	};

	static void UpdateVertex( PartitionVertexInternal* v, PartitionVertexInternal* vertices, Int32 numvertices );
};