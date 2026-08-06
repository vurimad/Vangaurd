/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathConvexHullEx.h"
#include "shapeRenderer.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ConvexHullEx, red );
	RTTI_PROPERTY( m_data );
RTTI_END_TYPE();


namespace red
{
	

	const ConvexHullEx::Header& ConvexHullEx::Header::EMPTY() 
	{
		static const ConvexHullEx::Header EMPTY;
		return EMPTY;
	}

	ConvexHullEx::ConvexHullEx()
	{
	}

	ConvexHullEx::ConvexHullEx( const ConvexHullEx& other )
		: m_data( DataBuffer::Copy( other.m_data ) )
	{
	}

	void ConvexHullEx::Setup( DataBuffer&& sourceData )
	{
		m_data = std::move( sourceData );
	}

	void ConvexHullEx::RenderSolid( class red::IShapeRenderer& renderer, const Bool swap /*= false*/ ) const
	{
		Vector3 center(0, 0, 0);

		// bounds of all the points
		{
			const auto* vertices = GetVertices();
			auto numVertices = GetHeader().m_numVertices;
			for (Uint32 i = 0; i < numVertices; ++i)
				center += vertices[i];
		}

		// center of the points, needed to determine if we need to swap triangles
		center /= (const Float)GetHeader().m_numVertices;

		// generate triangles
		auto numFaces = GetHeader().m_numFaces;
		const auto* face = GetFaces();
		const auto* vertices = GetVertices();
		while ( numFaces-- > 0 )
		{
			const auto* edge = GetEdges() + *face;

			const auto* firstEdge = edge;
			const auto& a = vertices[ edge->GetSourceVertex() ];

			edge = edge->GetNextEdgeOfFace();

			do
			{
				const auto& b = vertices[ edge->GetSourceVertex() ];
				const auto& c = vertices[ edge->GetTargetVertex() ];

				const Plane p(a, b, c);
				const Bool genSwap = (p.DistanceTo(center) < 0.0f);

				if (swap ^ genSwap)
					renderer.AddTriangle( c, b, a );
				else
					renderer.AddTriangle( a, b, c );

				edge = edge->GetNextEdgeOfFace();
			}
			while ( edge->GetNextEdgeOfFace() != firstEdge );

			++face;
		}
	}

	void ConvexHullEx::RenderWireframe( class red::IShapeRenderer& renderer ) const
	{
		auto numFaces = GetHeader().m_numFaces;
		const auto* face = GetFaces();
		const auto* vertices = GetVertices();
		while ( numFaces-- > 0 )
		{
			const auto* edge = GetEdges() + *face;
			const auto* firstEdge = edge;
			do
			{
				const auto& a = vertices[ edge->GetSourceVertex() ];
				const auto& b = vertices[ edge->GetTargetVertex() ];

				renderer.AddLine( a, b );				

				edge = edge->GetNextEdgeOfFace();
			}
			while ( edge != firstEdge );

			++face;
		}
	}

	Bool ConvexHullEx::Contains( const Vector4& point ) const
	{
		const auto numPlanes = GetHeader().m_numPlanes;
		const auto* planes = GetPlanes();
		for ( Uint32 i=0; i<numPlanes; ++i )
		{
			// is point in front of the plane ?
			if ( planes[i].DistanceTo( point ) >= 0.0f )
				return false;
		}

		// point is behind all of the planes
		return true;
	}

	Bool ConvexHullEx::IntersectSegment( const Segment& segment, Vector4& enterPoint ) const
	{
		RED_FATAL( "Not implemented" );
		return false;
	}

	Bool ConvexHullEx::IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const
	{
		enterDistFromOrigin = -RED_FLT_MAX;

		const auto numPlanes = GetHeader().m_numPlanes;
		const auto* planes = GetPlanes();
		for ( Uint32 i=0; i<numPlanes; ++i )
		{
			const auto& plane = planes[i];
			const Float proj = -Vector4::Dot3( plane.NormalDistance, direction );

			if ( proj > 0.0f )
			{
				Float intersectionDistance = Vector4::Dot3( origin, plane.NormalDistance );
				intersectionDistance += plane.NormalDistance[3];
				intersectionDistance /= proj;

				if ( intersectionDistance > enterDistFromOrigin )
					enterDistFromOrigin = intersectionDistance;
			}
		}

		const Vector4 enterPoint = origin + ( direction * enterDistFromOrigin );
		for ( Uint32 i=0; i<numPlanes; ++i )
		{
			const auto& plane = planes[i];
			Float dist = Vector4::Dot3( enterPoint, plane.NormalDistance );
			dist += plane.NormalDistance.W;

			if ( dist > 1e-6f )
				return false;
		}

		return enterDistFromOrigin > 0 ;
	}

	Bool ConvexHullEx::IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const
	{
		Float t;
		Bool ret = IntersectRay( origin, direction, t);
		enterPoint = origin + direction * t;
		return ret;
	}

	void ConvexHullEx::Serialize( IFile& file )
	{
		file << m_data;
	}

} // red