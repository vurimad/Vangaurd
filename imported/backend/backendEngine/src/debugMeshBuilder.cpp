#include "build.h"
#include "debugMeshBuilder.h"
#include "../../../common/renderData/include/renderVertices.h"

using red::DynArray;

// that's almost the same as in CollisionShapeBuilder - let's merge it next time
namespace DebugMeshBuilder
{
	void BuildBox( const Box& bbox, DynArray< Vector4 >& vertices, DynArray< Uint32 >& indices )
	{
		vertices.Clear();
		indices.Clear();

		vertices.Resize( 8 );
		vertices[0] = Vector4( bbox.Min.X, bbox.Min.Y, bbox.Max.Z );
		vertices[1] = Vector4( bbox.Max.X, bbox.Min.Y, bbox.Max.Z );
		vertices[2] = Vector4( bbox.Min.X, bbox.Max.Y, bbox.Max.Z );
		vertices[3] = Vector4( bbox.Max.X, bbox.Max.Y, bbox.Max.Z );
		vertices[4] = Vector4( bbox.Min.X, bbox.Min.Y, bbox.Min.Z );
		vertices[5] = Vector4( bbox.Max.X, bbox.Min.Y, bbox.Min.Z );
		vertices[6] = Vector4( bbox.Min.X, bbox.Max.Y, bbox.Min.Z );
		vertices[7] = Vector4( bbox.Max.X, bbox.Max.Y, bbox.Min.Z );

		const Uint32 numIdx = 36;
		const Uint16 idx[ numIdx ] = {
			0, 1, 2, 1, 3, 2,			// top
			5, 4, 6, 5, 6, 7,			// bottom
			0, 2, 4, 4, 2, 6,			// left
			1, 5, 3, 3, 5, 7,			// right
			2, 3, 6, 3, 7, 6,			// front
			0, 4, 1, 1, 4, 5			// back
		};

		indices.Resize( numIdx );
		for ( Uint32 i = 0; i < numIdx; ++i )
		{
			indices[i] = idx[i];
		}
	}

	void VectorsToDebugVertices( const DynArray< Vector4 >& source, const Color& color, DynArray< DebugVertex >& dest )
	{
		dest.Clear();
		dest.Reserve( source.Size() );
		for ( const Vector4& vec : source )
		{
			dest.PushBack( DebugVertex( vec, color ) );
		}
	}

	void SubdivideTriProjectToSphere( Uint32 subdivisions, const Vector4& v0, const Vector4& v1, const Vector4& v2, Float radius, DynArray< Vector4 >& vertices, DynArray< Uint32 >& indices )
	{
		if ( subdivisions == 0 )
		{
			Uint32 baseIndex = vertices.Size();
			vertices.PushBack( v0.Normalized3() * radius );
			vertices.PushBack( v1.Normalized3() * radius );
			vertices.PushBack( v2.Normalized3() * radius );

			indices.PushBack( baseIndex + 0 );
			indices.PushBack( baseIndex + 1 );
			indices.PushBack( baseIndex + 2 );
		}
		else
		{
			Vector4 v01 = ( v0 + v1 ) * 0.5f;
			Vector4 v12 = ( v1 + v2 ) * 0.5f;
			Vector4 v20 = ( v2 + v0 ) * 0.5f;
			SubdivideTriProjectToSphere( subdivisions - 1, v0, v01, v20, radius, vertices, indices );
			SubdivideTriProjectToSphere( subdivisions - 1, v1, v12, v01, radius, vertices, indices );
			SubdivideTriProjectToSphere( subdivisions - 1, v2, v20, v12, radius, vertices, indices );
			SubdivideTriProjectToSphere( subdivisions - 1, v01, v12, v20, radius, vertices, indices );
		}
	}

	void GetSphereStats( Float radius, Uint32& numVertices, Uint32& numIndices )
	{
		Uint32 numSubdivisions = 2;
		// We start with a cube, 12 faces. Each subdivision subdivides 1 face into 4. Vertices are not shared
		// between adjacent triangles.
		numVertices = 36;
		numIndices = 36;
		for ( Uint32 i = 0; i < numSubdivisions; ++i )
		{
			numVertices *= 4;
			numIndices *= 4;
		}
	}



	void BuildBox( const Box& bbox, const Color& color, DynArray< DebugVertex >& vertices, DynArray< Uint32 >& indices )
	{
		vertices.Clear();
		indices.Clear();

		DynArray< Vector4 > tempVertices;
		BuildBox( bbox, tempVertices, indices );

		VectorsToDebugVertices( tempVertices, color, vertices );
	}

	void BuildSphere( Float radius, const Color& color, DynArray< DebugVertex >& vertices, DynArray< Uint32 >& indices, const Vector4& position /* = Vector::ZERO_3D_POINT */ )
	{
		vertices.Clear();
		indices.Clear();

		// Reserve space for mesh, to avoid extra re-allocations.
		Uint32 numVerts, numInds;
		GetSphereStats( radius, numVerts, numInds );
		vertices.Reserve( numVerts );
		indices.Reserve( numInds );

		// Make the sphere by subdividing a cube.
		DynArray< Vector4 > cubeVertices;
		DynArray< Uint32 > cubeIndices;

		Vector4 max( radius, radius, radius );
		BuildBox( Box( -max, max ), cubeVertices, cubeIndices );

		DynArray< Vector4 > tempVertices;
		tempVertices.Reserve( numVerts );
		for ( Uint32 i = 0; i < cubeIndices.Size(); i += 3 )
		{
			SubdivideTriProjectToSphere( 2, cubeVertices[cubeIndices[i]], cubeVertices[cubeIndices[i+1]], cubeVertices[cubeIndices[i+2]], radius, tempVertices, indices );
		}

		VectorsToDebugVertices( tempVertices, color, vertices );
	}

	namespace PlaneHelper
	{
		void GeneratePlaneVertices( Float density, Uint32 cols, Uint32 rows, RectF dim, DynArray< DebugVertex >& outVertices )
		{
			outVertices.Reserve( cols * rows );
			Vector2 nextPosition( dim.m_left, dim.m_top );
			for ( Uint32 i = 0; i < rows; ++i )
			{
				for ( Uint32 j = 0; j < cols; ++j )
				{
					DebugVertex newVertex( nextPosition, Color::WHITE() );
					outVertices.PushBack( newVertex );
					nextPosition.X += density;
				}
				nextPosition.Y += density;
				nextPosition.X = dim.m_left;
			}
		}
		void AddPlaneQuad( Uint32 quadIndices[ 4 ], DynArray< Uint32 >& outIndices )
		{
			outIndices.PushBack( quadIndices[ 0 ] );
			outIndices.PushBack( quadIndices[ 3 ] );
			outIndices.PushBack( quadIndices[ 1 ] );
			outIndices.PushBack( quadIndices[ 0 ] );
			outIndices.PushBack( quadIndices[ 2 ] );
			outIndices.PushBack( quadIndices[ 3 ] );
		}
		void GeneratePlaneIndices( Uint32 cols, Uint32 rows, DynArray< Uint32 >& outIndices )
		{
			Uint32 currentQuad[4];
			const Uint32 colRange = cols - 1;
			const Uint32 rowRange = rows - 1;
			outIndices.Reserve( colRange * rowRange * 6 );
			for ( Uint32 i = 0; i < rowRange; ++i )
			{
				for ( Uint32 j = 0; j < colRange; ++j )
				{
					currentQuad[ 0 ] = ( cols * i + j );
					currentQuad[ 1 ] = currentQuad[ 0 ] + 1;
					currentQuad[ 2 ] = currentQuad[ 0 ] + cols;
					currentQuad[ 3 ] = currentQuad[ 2 ] + 1;
					AddPlaneQuad( currentQuad, outIndices );
				}
			}
		}
	}
	void BuildPlane( Float density, RectF dim, DynArray< DebugVertex >& outVertices, DynArray< Uint32 >& outIndices )
	{		
		Uint32 cols = ( Uint32 )MFloor( dim.Width() / density );
		Uint32 rows = ( Uint32 )MFloor( dim.Height() / density );
		if ( cols > 0 && rows > 0 )
		{
			PlaneHelper::GeneratePlaneVertices( density, cols, rows, dim, outVertices );
			PlaneHelper::GeneratePlaneIndices( cols, rows, outIndices );
		}
	}
}