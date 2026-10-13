/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class CMesh;

namespace tools
{
	struct Uint32Range
	{
		Uint32 m_start;
		Uint32 m_end;
	};

	struct ChunkInfo
	{
		Uint32 m_start = 0u;
		Uint32 m_end = 0u;
		Uint32 m_lod = 0u;
		String m_material = String();
		red::StaticArray<Bool, 10> m_flags;
	};

	class BACKEND_MESH_API WorldMeshBuilder
	{
	public:
		WorldMeshBuilder();
		

		void AddMesh(red::ArraySpan < DebugVertex > vertices, red::ArraySpan < Uint32 > indices, 
			red::ArraySpan < Vector2 > uvs = {}, red::ArraySpan < Vector3 > normals = {}, 
			red::ArraySpan < Vector3 > binormals = {}, red::ArraySpan < Vector3 > tangents = {} );

		void AddChunkedMesh( red::DynArray < ChunkInfo >& chunks, red::ArraySpan < DebugVertex > vertices, red::ArraySpan < Uint32 > indices,
			red::ArraySpan < Vector2 > uvs = {}, red::ArraySpan < Vector3 > normals = {},
			red::ArraySpan < Vector3 > binormals = {}, red::ArraySpan < Vector3 > tangents = {} );

		// Road tool specific, don't mix with AddMesh
		void SetRoadMeshes( red::DynArray < DebugVertex > vertices, red::DynArray < Uint32 > indices, const red::DynArray< red::DynArray < tools::Uint32Range > >& chunks );

		void SetMaterial( const res::ResourcePath& materialPath );
		void SetHaveCollision( Bool hasCollistion );

		red::DynArray< THandle< CMesh > > BuildMeshes();
		THandle< CMesh > BuildMesh();

		void FillChunksData(THandle< CMesh >& meshResource);
		void AddCollisionGeometry(THandle< CMesh >& meshResource, red::DynArray< Vector3 > m_verticesCollision,
			red::DynArray< Uint16 > m_indicesCollision, CName collisionMaterial);

	private:
		void FillChunkData( THandle< CMesh >& meshResource, const Uint32& range );
		void AddMaterial( THandle< CMesh >& meshResource );
		void AddCollisionGeometry(THandle< CMesh >& meshResource);

		red::DynArray < DebugVertex > m_vertices;
		red::DynArray < Uint32 > m_indices;
		red::DynArray < red::DynArray < Uint32Range > > m_chunks;
		red::DynArray < ChunkInfo > m_chunkInfos;

		red::DynArray < Vector2 > m_uvs;
		red::DynArray < Vector3 > m_normals;
		red::DynArray < Vector3 > m_tangents;
		red::DynArray < Vector3 > m_binormals;

		red::DynArray < Uint16 > m_chunkIndices;
		red::DynArray < Vector3 > m_chunkVertices;

		res::ResourcePath m_materialPath;
		Bool m_hasCollision;
	};
}


