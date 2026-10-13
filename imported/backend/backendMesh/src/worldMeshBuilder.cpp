/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "worldMeshBuilder.h"
#include "meshDataBuilder.h"
#include "meshDataView.h"
#include "meshPhysicsUtils.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMesh/include/meshChunk.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/renderData/include/renderVertices.h"

namespace 
{
	const CName c_materialName = RED_NAME_CONSTEXPR( "default" );

	RED_INLINE Vector3 GetVector( const DebugVertex& vertex )
	{
		return { vertex.x, vertex.y, vertex.z };
	}
}

namespace tools
{
	WorldMeshBuilder::WorldMeshBuilder()
		: m_materialPath( res::ResourcePath::Build( "base\\materials\\road_debug_grid.mt" ) )
		, m_hasCollision( true )
	{
	}

	void WorldMeshBuilder::AddMesh( red::ArraySpan < DebugVertex > vertices, red::ArraySpan < Uint32 > indices, red::ArraySpan < Vector2 > uvs,
		red::ArraySpan < Vector3 > normals, red::ArraySpan < Vector3 > binormals, red::ArraySpan < Vector3 > tangents )
	{
		Uint32 oldIndicesSize = m_indices.Size();
		Uint32 oldVerticesSize = m_vertices.Size();

		m_vertices.PushBack( vertices );
		m_indices.Reserve( m_indices.Size() + indices.Size() );

		m_uvs.PushBack( uvs );
		m_normals.PushBack( normals );
		m_binormals.PushBack( binormals );
		m_tangents.PushBack( tangents );

		for( const Uint32& index : indices )
		{
			m_indices.PushBack( index + oldVerticesSize );
		}
		m_chunks.PushBack( { { oldIndicesSize, m_indices.Size() } } );
		m_chunkInfos.PushBack( { oldIndicesSize, m_indices.Size(), 0 } );
	}

	void WorldMeshBuilder::AddChunkedMesh( red::DynArray < ChunkInfo >& chunks,
		red::ArraySpan < DebugVertex > vertices, red::ArraySpan < Uint32 > indices, red::ArraySpan < Vector2 > uvs,
		red::ArraySpan < Vector3 > normals, red::ArraySpan < Vector3 > binormals, red::ArraySpan < Vector3 > tangents )
	{
		Uint32 oldIndicesSize = m_indices.Size();
		Uint32 oldVerticesSize = m_vertices.Size();

		m_vertices.PushBack( vertices );
		m_indices.Reserve( m_indices.Size() + indices.Size() );

		m_uvs.PushBack( uvs );
		m_normals.PushBack( normals );
		m_binormals.PushBack( binormals );
		m_tangents.PushBack( tangents );

		if (oldVerticesSize != 0)
		{
			for (const Uint32& index : indices)
			{
				m_indices.PushBack( index + oldVerticesSize );
			}
			for (auto chunk : chunks)
			{
				m_chunks.PushBack( { { (chunk.m_start + oldVerticesSize), (chunk.m_end + oldVerticesSize) } } );
				m_chunkInfos.PushBack( chunk );
				auto& pushed = m_chunkInfos.Back();
				pushed.m_start += oldVerticesSize;
				pushed.m_end += oldVerticesSize;
			}
		}
		else
		{
			for (auto chunk : chunks)
			{
				m_chunks.PushBack( { { 3 * chunk.m_start, 3 * chunk.m_end } } );
				m_chunkInfos.PushBack( chunk );

			}
			m_indices.PushBack( indices );
		}
	}

	void WorldMeshBuilder::SetRoadMeshes( red::DynArray < DebugVertex > vertices, red::DynArray < Uint32 > indices, const red::DynArray< red::DynArray < tools::Uint32Range > >& chunks )
	{
		m_vertices = vertices;
		m_indices = indices;
		m_chunks = chunks;
	}

	void WorldMeshBuilder::SetMaterial( const res::ResourcePath& materialPath )
	{
		m_materialPath = materialPath;
	}

	void WorldMeshBuilder::SetHaveCollision( Bool hasCollistion )
	{
		m_hasCollision = hasCollistion;
	}

	red::DynArray< THandle< CMesh > > WorldMeshBuilder::BuildMeshes()
	{
		red::DynArray< THandle< CMesh > > meshResources;
		meshResources.Reserve( m_chunks.Size() );

		if( m_chunks.Empty() )
		{
			return {};
		}

		for( Uint32 i = 0u; i < m_chunks.Front().Size(); ++i )
		{
			THandle< CMesh >& meshResource = meshResources.EmplaceBack( CreateHandle< CMesh >() );

			FillChunkData( meshResource, i );
			AddMaterial( meshResource );
			AddCollisionGeometry( meshResource );
		}

		return meshResources;
	}

	// This will generate one mesh with separate chunks for all chunks added with AddMesh method, uses m_chunkInfos.
	THandle< CMesh > WorldMeshBuilder::BuildMesh()
	{
		THandle< CMesh > meshResource = CreateHandle< CMesh >();
		if (m_chunkInfos.Empty())
		{
			return meshResource;
		}

		FillChunksData( meshResource );
		AddMaterial( meshResource );
		//AddPhysics( meshResource );
		return meshResource;
	}

	void WorldMeshBuilder::AddCollisionGeometry( THandle< CMesh >& meshResource )
	{
		if( !m_hasCollision )
			return;

		THandle< physics::ICollider > collider = physics::ColliderMesh::Create( ( const Float* ) m_chunkVertices.Data(), sizeof( Vector3 ), m_vertices.Size(), m_chunkIndices.TypedData(), m_chunkIndices.Size(), true );
		THandle< physics::SystemResource > system = tools::BuildSystemFromCollider( collider );
		auto param = meshResource->CreateParameter< mesh::MeshParamPhysics >();
		param->SetPhysicsData( system );
	}
	
	void WorldMeshBuilder::AddCollisionGeometry(THandle< CMesh >& meshResource, red::DynArray< Vector3 > m_verticesCollision,
		red::DynArray< Uint16 > m_indicesCollision, CName collisionMaterial)
	{
		if (!m_hasCollision)
			return;

		THandle< physics::ICollider > collider = physics::ColliderMesh::Create((const Float*)m_verticesCollision.Data(), sizeof(Vector3), m_verticesCollision.Size(), m_indicesCollision.TypedData(), m_indicesCollision.Size(), true);
		if (!collisionMaterial.Empty())
			collider->m_material = RED_NAME(collisionMaterial);
		THandle< physics::SystemResource > system = tools::BuildSystemFromCollider(collider);
		auto param = meshResource->CreateParameter< mesh::MeshParamPhysics >();
		param->SetPhysicsData(system);
	}
	
	void WorldMeshBuilder::AddMaterial( THandle< CMesh >& meshResource )
	{
		//Actual material can't be loaded here as code here is run outside of main thread
		//Empty material is created and the path is set, so that game uses external material
		const THandle< IMaterial > roadMaterial = CreateHandle< CMaterialInstance >( nullptr, nullptr );
		roadMaterial->Internal_SetPath( m_materialPath );

		mesh::DataView view( *meshResource );
		view.RemoveMaterials();
		view.AddMaterial( roadMaterial, c_materialName );
	}

	void WorldMeshBuilder::FillChunkData( THandle< CMesh >& meshResource, const Uint32& chunkIndex )
	{
		mesh::DataBuilder builder{};
		mesh::DataBuilder::UnpackedChunk& chunk = builder.m_chunks.EmplaceBack();

		chunk.m_lodMask = 1 << 0;
		chunk.m_materials.PushBack( c_materialName );
		chunk.m_vertexType = MVT_StaticMesh;
		chunk.m_flags.m_isTwoSided = true;

		m_chunkIndices.Clear();
		m_chunkVertices.Clear();

		red::Set< Uint16 > uniqueIndices;
		for( Uint32 j = 0u; j < m_chunks.Size(); ++j )
		{
			for( Uint32 i = m_chunks[ j ][ chunkIndex ].m_start; i < m_chunks[ j ][ chunkIndex ].m_end; ++i )
			{
				uniqueIndices.Insert( m_indices[ i ] );
			}
		}

		for( Uint32 j = 0u; j < m_chunks.Size(); ++j )
		{
			for( Uint32 i = m_chunks[ j ][ chunkIndex ].m_start; i < m_chunks[ j ][ chunkIndex ].m_end; ++i )
			{
				Uint16 index = static_cast< Uint16 >( uniqueIndices.Find( m_indices[ i ] ) - uniqueIndices.Begin() );
				m_chunkIndices.PushBack( index );
			}
		}

		Bool addUVs = m_vertices.Size() == m_uvs.Size();
		Bool addN = m_vertices.Size() == m_normals.Size();
		Bool addBN = m_vertices.Size() == m_binormals.Size();
		Bool addT = m_vertices.Size() == m_tangents.Size();

		chunk.m_streams.Resize( 4 + addUVs + addT + addBN );
		chunk.m_streams[0].m_type = MST_Index_U16;
		chunk.m_streams[1].m_type = MST_Position_3F;
		chunk.m_streams[2].m_type = MST_Color_U32;
		chunk.m_streams[3].m_type = MST_Normal_3F;

		Uint16 stream_idx = 4;

		m_chunkVertices.Reserve( uniqueIndices.Size() );
		mesh::StreamWriter< Uint16 > indicesWriter( chunk.m_streams[ 0 ].m_data, m_chunkIndices.Size() );
		mesh::StreamWriter< Vector3 > positionsWriter( chunk.m_streams[ 1 ].m_data, uniqueIndices.Size() );
		mesh::StreamWriter< Uint32 > colorsWriter( chunk.m_streams[ 2 ].m_data, uniqueIndices.Size() );
		mesh::StreamWriter< Vector3 > normalsWriter( chunk.m_streams[ 3 ].m_data, uniqueIndices.Size() );

		red::UniquePtr< mesh::StreamWriter< Vector2 >, red::PoolBackend > uvsWriter;
		red::UniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend > tangentWriter;
		red::UniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend > binormalWriter;

		if (addUVs)
		{
			auto& stream = chunk.m_streams[stream_idx++];
			stream.m_type = MST_TexCoord0_2F;
			uvsWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector2 >, red::PoolBackend  >( stream.m_data, uniqueIndices.Size() );
		}

		if (addT)
		{
			auto& stream = chunk.m_streams[stream_idx++];
			stream.m_type = MST_Tangent_3F;
			tangentWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend >( stream.m_data, uniqueIndices.Size() );
		}
		if (addBN)
		{
			auto& stream = chunk.m_streams[stream_idx++];
			stream.m_type = MST_Binormal_3F;
			binormalWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend >( stream.m_data, uniqueIndices.Size() );
		}


		for( const Uint16& index : uniqueIndices )
		{
			const Color color = Color::FromVectorLinearAccu( m_vertices[ index ].colorLinearSpace.ToVector4() );
			colorsWriter.Write( color.ToUint32() );

			Vector3 vertex = GetVector( m_vertices[ index ] );

			positionsWriter.Write( vertex );
			m_chunkVertices.PushBack( vertex );

			if (addN)
			{
				normalsWriter.Write( m_normals[index] );
			}
			else
			{
				normalsWriter.Write( { 0.0f, 0.0f, 1.0f } );
			}

			if (addUVs)
			{
				uvsWriter->Write( m_uvs[index] );
			}

			if (addT)
			{
				tangentWriter->Write( m_tangents[index] );
			}

			if (addBN)
			{
				binormalWriter->Write( m_binormals[index] );
			}
		}

		for( const auto& index : m_chunkIndices )
		{
			indicesWriter.Write( index );
		}

		const auto lodIndex = builder.m_lods.Size();
		auto& newLevel = builder.m_lods.EmplaceBack();

		mesh::DataBuilder::FlushOptions flushOptions;
		flushOptions.m_verbose = false;
		flushOptions.m_rebuildMaterials = false;
		flushOptions.m_rebuildLODDistances = false;
		flushOptions.m_rebuildBBox = true;
		flushOptions.m_uploadResource = false;
		builder.Flush( *meshResource, flushOptions );

		if ( meshResource )
		{
			Float hideDistance = meshResource->GetAutoHideDistance();
			RED_FATAL_ASSERT( !std::isinf( hideDistance ) && !std::isnan( hideDistance ) && ( hideDistance >= 0.0f ) );
		}
	}
	
	// All chunks to one mesh
	void WorldMeshBuilder::FillChunksData( THandle< CMesh >& meshResource )
	{
		mesh::DataBuilder builder{};

		for (Uint32 chunkIndex = 0; chunkIndex < m_chunkInfos.Size(); chunkIndex++)
		{
			mesh::DataBuilder::UnpackedChunk& chunk = builder.m_chunks.EmplaceBack();

			chunk.m_lodMask = 1 << m_chunkInfos[chunkIndex].m_lod;
			chunk.m_materials.PushBack( c_materialName );
			chunk.m_vertexType = MVT_StaticMesh;
			chunk.m_flags.m_isTwoSided = true;

			m_chunkIndices.Clear();
			m_chunkVertices.Clear();

			red::Set< Uint16 > uniqueIndices;

			for (Uint32 i = m_chunkInfos[chunkIndex].m_start; i < m_chunkInfos[chunkIndex].m_end; ++i)
			{
				uniqueIndices.Insert( m_indices[i] );
			}

			for (Uint32 i = m_chunkInfos[chunkIndex].m_start; i < m_chunkInfos[chunkIndex].m_end; ++i)
			{
				Uint16 index = static_cast<Uint16>( uniqueIndices.Find( m_indices[i] ) - uniqueIndices.Begin() );
				m_chunkIndices.PushBack( index );
			}

			Bool addUVs = m_vertices.Size() == m_uvs.Size();
			Bool addN = m_vertices.Size() == m_normals.Size();
			Bool addBN = m_vertices.Size() == m_binormals.Size();
			Bool addT = m_vertices.Size() == m_tangents.Size();

			chunk.m_streams.Resize( 3 + addUVs + addN + addT + addBN );
			chunk.m_streams[0].m_type = MST_Index_U16;
			chunk.m_streams[1].m_type = MST_Position_3F;
			chunk.m_streams[2].m_type = MST_Color_U32;
			Uint16 stream_idx = 3;


			m_chunkVertices.Reserve( uniqueIndices.Size() );
			mesh::StreamWriter< Uint16 > indicesWriter( chunk.m_streams[0].m_data, m_chunkIndices.Size() );
			mesh::StreamWriter< Vector3 > positionsWriter( chunk.m_streams[1].m_data, uniqueIndices.Size() );
			mesh::StreamWriter< Uint32 > colorsWriter( chunk.m_streams[2].m_data, uniqueIndices.Size() );

			red::UniquePtr< mesh::StreamWriter< Vector2 >, red::PoolBackend > uvsWriter;
			red::UniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend > normalsWriter;
			red::UniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend > tangentWriter;
			red::UniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend > binormalWriter;

			if (addUVs)
			{
				auto& stream = chunk.m_streams[stream_idx++];
				stream.m_type = MST_TexCoord0_2F;
				uvsWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector2 >, red::PoolBackend  >( stream.m_data, uniqueIndices.Size() );
			}
			if (addN)
			{
				auto& stream = chunk.m_streams[stream_idx++];
				stream.m_type = MST_Normal_3F;
				normalsWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend >( stream.m_data, uniqueIndices.Size() );
			}
			if (addT)
			{
				auto& stream = chunk.m_streams[stream_idx++];
				stream.m_type = MST_Tangent_3F;
				tangentWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend >( stream.m_data, uniqueIndices.Size() );
			}
			if (addBN)
			{
				auto& stream = chunk.m_streams[stream_idx++];
				stream.m_type = MST_Binormal_3F;
				binormalWriter = red::CreateUniquePtr< mesh::StreamWriter< Vector3 >, red::PoolBackend >( stream.m_data, uniqueIndices.Size() );
			}


			for (const Uint16& index : uniqueIndices)
			{
				const Color color = Color::FromVectorLinearAccu( m_vertices[ index ].colorLinearSpace.ToVector4() );
				colorsWriter.Write( color.ToUint32() );

				Vector3 vertex = GetVector( m_vertices[index] );

				positionsWriter.Write( vertex );
				m_chunkVertices.PushBack( vertex );

				if (addUVs)
				{
					uvsWriter->Write( m_uvs[index] );
				}
				if (addN)
				{
					normalsWriter->Write( m_normals[index] );
				}
				if (addT)
				{
					tangentWriter->Write( m_tangents[index] );
				}
				if (addBN)
				{
					binormalWriter->Write( m_binormals[index] );
				}
			}

			for (const auto& index : m_chunkIndices)
			{
				indicesWriter.Write( index );
			}
		}

		int lastLod = -1;
		for (auto chunkInfo : m_chunkInfos)
		{
			if (lastLod != chunkInfo.m_lod)
			{
				lastLod = chunkInfo.m_lod;
				//TODO: Add proper copy of lod ranges
				builder.m_lods.EmplaceBack().m_distance = lastLod * 2.f;
			}
		}

		mesh::DataBuilder::FlushOptions flushOptions;
		flushOptions.m_verbose = false;
		flushOptions.m_rebuildMaterials = false;
		flushOptions.m_rebuildLODDistances = true;
		flushOptions.m_rebuildBBox = true;
		flushOptions.m_uploadResource = false;
		builder.Flush( *meshResource, flushOptions );

		if (meshResource)
		{
			Float hideDistance = meshResource->GetAutoHideDistance();
			RED_FATAL_ASSERT( !std::isinf( hideDistance ) && !std::isnan( hideDistance ) && ( hideDistance >= 0.0f ) );
		}
	}
}
