/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "dataBuffer.h"

namespace red
{
	class IShapeRenderer;
}

namespace red
{

	/// Advanced 3D Convex Hull with visualization and slitting support
	/// In addition to normal plane list it also stores the vertices, faces and edges
	struct RED_REFLECTION_API ConvexHullEx
	{
		RTTI_DECLARE_TYPE( ConvexHullEx );

	public:
		ConvexHullEx();
		ConvexHullEx( const ConvexHullEx& other );

	#pragma pack( push, 4 )
		/// inplace edge descriptor
		struct RED_REFLECTION_API Edge
		{
			Int16 m_next;
			Int16 m_reverse;
			Int16 m_targetVertex;
			Int16 m_padding; // NEEDED

			//! Get source vertex of this edge
			RED_FORCE_INLINE Int32 GetSourceVertex() const
			{
				return (this + m_reverse)->m_targetVertex;
			}

			//! Get target vertex of this edge
			RED_FORCE_INLINE Int32 GetTargetVertex() const
			{
				return m_targetVertex;
			}

			//! Counter-clockwise list of all edges of a vertex
			RED_FORCE_INLINE const Edge* GetNextEdgeOfVertex() const
			{
				return this + m_next;
			}

			//! Clockwise list of all edges of a face
			RED_FORCE_INLINE const Edge* GetNextEdgeOfFace() const
			{
				return (this + m_reverse)->GetNextEdgeOfVertex();
			}

			//! Get the same edge but from the other side
			RED_FORCE_INLINE const Edge* GetReverseEdge() const
			{
				return this + m_reverse;
			}
		};

		/// convex header
		struct RED_REFLECTION_API Header
		{
			Uint16			m_numVertices;
			Uint16			m_vertexOffset;

			Uint16			m_numEdges;
			Uint16			m_edgeOffset;

			Uint16			m_numFaces;
			Uint16			m_faceOffset;

			Uint16			m_numPlanes;
			Uint16			m_planeOffset;

			Header()
				: m_numVertices( 0 )
				, m_vertexOffset( 0 )
				, m_numEdges( 0 )
				, m_edgeOffset( 0 )
				, m_numFaces( 0 )
				, m_faceOffset( 0 )
				, m_numPlanes( 0 )
				, m_planeOffset( 0 )
			{}
			
			static const Header& EMPTY();
		};

	#pragma pack(pop)

		/// Vertices are stored as 3D points
		typedef Vector3 Vertex;

		/// Faces are stored as the indices to first edges
		typedef Uint16 Face;

		/// Is this a valid convex ?
		RED_FORCE_INLINE const Bool IsValid() const
		{
			return !m_data.Empty();
		}

		/// Get size of the data stored in the convex hull
		RED_FORCE_INLINE const Uint32 GetDataSize() const
		{
			return m_data.Size();
		}

		/// Get data header
		RED_FORCE_INLINE const Header& GetHeader() const
		{
			if ( m_data.Size() < sizeof(Header) )
				return Header::EMPTY();

			return *static_cast< const Header* >( m_data.Data() );
		}

		/// Get pointer to convex hull vertices
		RED_FORCE_INLINE const Vertex* GetVertices() const
		{
			return static_cast< const Vertex* >( red::OffsetPtr( m_data.Data(), GetHeader().m_vertexOffset ) );
		}

		/// Get pointer to convex hull edges
		RED_FORCE_INLINE const Edge* GetEdges() const
		{
			return static_cast< const Edge* >( red::OffsetPtr( m_data.Data(), GetHeader().m_edgeOffset ) );
		}

		/// Get pointer to convex hull faces
		RED_FORCE_INLINE const Face* GetFaces() const
		{
			return static_cast< const Face* >( red::OffsetPtr( m_data.Data(), GetHeader().m_faceOffset ) );
		}

		/// Get pointer to convex hull planes
		RED_FORCE_INLINE const Plane* GetPlanes() const
		{
			return static_cast< const Plane* >( red::OffsetPtr( m_data.Data(), GetHeader().m_planeOffset ) );
		}

		/// Setup convex
		void Setup( DataBuffer&& sourceData );

		/// Generate rendering geometry in form of triangles
		void RenderSolid( red::IShapeRenderer& renderer, const Bool swap = false ) const;

		/// Generate rendering geometry as wireframe mesh of polygons (cheaper)
		void RenderWireframe( red::IShapeRenderer& renderer ) const;

	public:
		// Check if this convex shape contains point
		Bool Contains( const Vector4& point ) const;

		// Intersect this convex shape with segment, returns point of entry
		Bool IntersectSegment( const Segment& segment, Vector4& enterPoint ) const;

		// Intersect this convex shape with ray, returns distance to point of entry
		Bool IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const;

		// Intersect this convex shape with ray, returns point of entry
		Bool IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const;

	public:
		// Manual serialization
		void Serialize( IFile& file );

	private:
		/// Packed topology data
		DataBuffer		m_data;
	};

} // red


// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, red::ConvexHullEx& val )
{
	val.Serialize( file );
}
