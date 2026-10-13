#include "build.h"

#include "importMeshREX_Chunks.h"
#include "importMeshREX_Common.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshChunk.h"

#include "../../../common/resourceMesh/include/meshDataViewRO.h"

#include "../../../backend/backendMesh/include/meshDataBuilder.h"
#include "../../backendEngine/include/importer.h"
#include "../../../common/resourceMesh/include/meshParamGpuBuffer.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../backendREA/include/reMaterialHelpers.h"


using red::DynArray;
using red::String;

namespace
{

	bool NewChunkMapsToOld( const mesh::DataBuilder::UnpackedChunk& newChunk, const mesh::DataViewRO& oldMeshView, Uint32 oldChunkIndex, bool materialMatchRequired )
	{
		bool res = false;
		do
		{
			if( newChunk.m_lodMask != oldMeshView.GetChunkLodMask( oldChunkIndex ) )
			{
				break;
			}

			auto oldChunkMatUUID = oldMeshView.GetChunkSourceMaterialUUID( oldChunkIndex );
			if( materialMatchRequired && ( newChunk.m_materialUUID.Empty() || oldChunkMatUUID.Empty() ) )
			{
				break;
			}
			if( ( !newChunk.m_materialUUID.Empty() && !oldChunkMatUUID.Empty() ) && ( newChunk.m_materialUUID != oldChunkMatUUID ) )
			{
				break;
			}

			auto chunkVertexType = oldMeshView.GetChunkVertexType( oldChunkIndex );
			if( newChunk.m_vertexType != chunkVertexType )
			{
				static EMeshVertexType flags = (EMeshVertexType)( MVT_StaticMesh | MVT_DestructibleMesh | MVT_DestructibleMeshSkinned | MVT_SkinnedMesh | MVT_ExtSkinnedMesh | MVT_GarmentSkinnedMesh | MVT_ExtGarmentSkinnedMesh | MVT_SkinnedMeshLightBlocker | MVT_ExtSkinnedMeshLightBlocker | MVT_GarmentSkinnedMeshLightBlocker | MVT_ExtGarmentSkinnedMeshLightBlocker );
				if( (newChunk.m_vertexType & flags) && (chunkVertexType & flags) )
				{
					//This is still a good enough match ie. mesh got skinned or un-skinned.
				}
				else
				{
					break;
				}
			}

			res = true;
		} while( 0 );

		return res;
	}

	void SetChunkFlags( mesh::DataBuilder& buildData, const mesh::ChunkFlags& renderFlags )
	{
		for ( auto& chunk : buildData.m_chunks )
		{
			chunk.m_flags = renderFlags;
		}
	}

	void SetChunkFlags(const mesh::DataViewRO& meshView, mesh::DataBuilder& buildData, const mesh::ChunkFlags& renderFlags, const red::AbsolutePath sourceFilePath)
	{

		std::unique_ptr< ReReadStream > stream(ReReadStream::Create(sourceFilePath.AsChar()));
		if (stream == nullptr)
			return;

		std::vector< ReFileMeshPtr > meshes;
		stream->Read(meshes);

		if (meshes.size() > 0)
		{
			ReFileMeshPtr reMesh = meshes[0];
			Uint32 totalChunks = meshView.GetNumChunks();
			for (Uint32 chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex)
			{

				if (reMesh->getChunkCount() <= chunkIndex)
				{
					break;
				}
				ReFileMeshChunkPtr chunk = reMesh->getChunk(chunkIndex);
				mesh::ChunkFlags chunkFlags = renderFlags;

				if (chunk->nodeFlags().hasFlag("renderInScene"))
					chunkFlags.m_renderInScene = chunk->nodeFlags().getFlag("renderInScene");
				if (chunk->nodeFlags().hasFlag("isTwoSided"))
					chunkFlags.m_isTwoSided = chunk->nodeFlags().getFlag("isTwoSided");
				if (chunk->nodeFlags().hasFlag("isRayTracedEmissive"))
					chunkFlags.m_isRayTracedEmissive = chunk->nodeFlags().getFlag("isRayTracedEmissive");
				if (chunk->nodeFlags().hasFlag("renderInShadows"))
					chunkFlags.m_renderInShadows = chunk->nodeFlags().getFlag("renderInShadows");
				buildData.m_chunks[chunkIndex].m_flags = chunkFlags;

			}
		}
	}

	bool CopyChunkFlags( const mesh::DataViewRO& meshView, mesh::DataBuilder& buildData, const mesh::ChunkFlags& defaultFlags )
	{
		// Copy chunk flags, try to match old chunks with new ones and copy chunk flags from old to new, so they don't get reset after re-import.
		Uint32 totalOldChunks = meshView.GetNumChunks();
		if( totalOldChunks == 0 )
			return false;

		Uint32 totalNewChunks = buildData.m_chunks.Size();
		Uint32 matchedChunks = 0;

		if( totalOldChunks == totalNewChunks ) //looks similar, hoping for small changes
		{
			for( Uint32 chunkIndex = 0; chunkIndex < totalNewChunks; ++chunkIndex )
			{
				Uint32 matchesTo = chunkIndex;
				mesh::DataBuilder::UnpackedChunk& newChunk = buildData.m_chunks[ chunkIndex ];
				bool matchesOldChunk = true;
				if( !NewChunkMapsToOld( newChunk, meshView, chunkIndex, false ) )
				{
					matchesOldChunk = false;
					//Try to do rematch with other chunks but now material uids must be present and matched.
					for( Uint32 rematchChunkIndex = 0; rematchChunkIndex < totalOldChunks; ++rematchChunkIndex )
					{
						if( NewChunkMapsToOld( newChunk, meshView, rematchChunkIndex, true ) )
						{
							matchesOldChunk = true;
							matchesTo = rematchChunkIndex;
							break;
						}
					}
				}

				if ( matchesOldChunk )
				{
					newChunk.m_flags = meshView.GetChunkFlags( matchesTo );
					++matchedChunks;
				}
				else
				{
					newChunk.m_flags = defaultFlags;
				}
			}
		}
		else
		{
			for( Uint32 chunkIndex = 0; chunkIndex < totalNewChunks; ++chunkIndex )
			{
				mesh::DataBuilder::UnpackedChunk& newChunk = buildData.m_chunks[ chunkIndex ];

				bool matchesOldChunk = false;
				Uint32 matchesTo = 0;
				for( Uint32 matchChunkIndex = 0; matchChunkIndex < totalOldChunks; ++matchChunkIndex )
				{
					if( NewChunkMapsToOld( newChunk, meshView, matchChunkIndex, true ) )
					{
						matchesOldChunk = true;
						matchesTo = matchChunkIndex;
						break;
					}
				}
				if( matchesOldChunk )
				{
					newChunk.m_flags = meshView.GetChunkFlags( matchesTo );
					matchedChunks++;
				}
				else
				{
					newChunk.m_flags = defaultFlags;
				}
			}
		}

		// we want to set all flags to those from import options only if there were no matches, otherwise only those without matches were set to those from import options
		// if you want to set all regardless use `m_overrideExistingRenderFlags`
		return matchedChunks > 0;
	};


	class StreamTranslatorBase
	{
	public:
		virtual Uint32 GetNumWrittenElements() const = 0;
	};

	template< class T >
	class StreamTranslator : public StreamTranslatorBase
	{
	public:
		StreamTranslator( mesh::UnpackedStream& destStream, Uint32 numVerts, EMeshStreamType streamType )
			: m_writer( destStream.m_data, numVerts )
		{

			destStream.m_type = streamType;
		}

		RED_INLINE void Write( const T& value )
		{
			m_writer.Write( value );
		}

		virtual Uint32 GetNumWrittenElements() const override
		{
			return m_writer.GetNumWrittenElements();
		}

	public: // Public, because this is local & low-level. It will make it easier to customize some stream generation (i.e. destruction indices).
		mesh::StreamWriter< T > m_writer;
	};


	struct Triangle
	{
		// we only need 2 vertices, because the third one is always the same and it will be available higher in the structure
		Uint16 idx0;
		Uint16 idx1;
		Uint16 idx2;
	};
	struct AdjacentTriangles
	{
		red::DynArray< Triangle > triangles;
	};
	typedef red::HashMap< Uint16, AdjacentTriangles > IndexTriangleMapping;

	typedef red::SortedArray< Uint16 > DuplicateIndexArray;
	typedef red::HashMap< Uint16, DuplicateIndexArray > VertexDuplicates;

	void MapTriangleToIndex( Uint16 srcVertex, Uint16 dst1, Uint16 dst2, IndexTriangleMapping& mapping, const VertexDuplicates& vertexDuplicates )
	{
		Triangle t { srcVertex, dst1, dst2 };
		if( !mapping.KeyExist( srcVertex ) )
		{
			AdjacentTriangles newSet;
			mapping.Insert( srcVertex, newSet );
		}
		mapping[ srcVertex ].triangles.PushBack( t );

		// fill in data for duplicates
		if( vertexDuplicates.KeyExist( srcVertex ) )
		{
			const DuplicateIndexArray& duplicateIndexArray = vertexDuplicates[ srcVertex ];
			for( const Uint16 duplicateIndex : duplicateIndexArray )
			{
				if( !mapping.KeyExist( duplicateIndex ) )
				{
					AdjacentTriangles newSet;
					mapping.Insert( duplicateIndex, newSet );
				}
				mapping[ duplicateIndex ].triangles.PushBack( t );
			}
		}
	}

	void InsertVertexDuplicate( Uint16 vertex0, Uint16 vertex1, VertexDuplicates& duplicates )
	{
		if( !duplicates.KeyExist( vertex0 ) )
		{
			red::SortedArray< Uint16 > newList;
			duplicates.Insert( vertex0, newList );
		}
		duplicates[ vertex0 ].InsertUnique( vertex1 );
	}

	ImportResult PrepareTopology( mesh::DataBuilder::UnpackedChunk& newChunk )
	{
		const mesh::UnpackedStream& indicesStream = newChunk.m_streams[ ECS_Indices ];
		RED_ASSERT( indicesStream.m_type == MST_Index_U16, "MeshTopologyCalculation: Incorrect source stream type passed" );

		Uint32 dataSize = indicesStream.m_data.Size();
		Uint32 elementCount = dataSize / sizeof( Uint16 );

		if( elementCount % 3 != 0 )
		{
			return ImportResult::Error( String::Printf( "Indices count is incorrect (expected number divisible by 3, got %d instead).", elementCount ) );
		}

		Uint32 triangleCount = elementCount / 3;
		const Uint16* indices = static_cast< const Uint16* >( indicesStream.m_data.Data() );

		IndexTriangleMapping mapping;

		Uint16 minIndex = 65535;
		Uint16 maxIndex = 0;

		// check vertices
		const mesh::UnpackedStream& positionsStream = newChunk.m_streams[ ECS_Positions ];
		const Vector3* vertexPositions = static_cast< const Vector3* >( positionsStream.m_data.Data() );
		Uint32 positionsDataSize = positionsStream.m_data.Size();
		Uint16 vertsCount = ( Uint16 )( positionsDataSize / sizeof( Vector3 ) );
		static constexpr Float POS_EPS = 0.0001f;

		const mesh::UnpackedStream& normalsStream = newChunk.m_streams[ ECS_Normals ];
		const Vector3* normals = static_cast< const Vector3* >( normalsStream.m_data.Data() );
		static constexpr Float NORMAL_ANGLE_EPS = 15.f; // 15 degrees difference

		const mesh::UnpackedStream& uvsStream = newChunk.m_streams[ ECS_UV0 ];
		const Vector2* uvs = static_cast< const Vector2* >( uvsStream.m_data.Data() );
		static constexpr Float UV_DIFF_EPS = 0.001f;

		// looping over vertices to figure out the pairs
		VertexDuplicates vertexDuplicates;
		for ( Uint16 i = 0; i < vertsCount; ++i )
		{
			const Vector3& pos_i = vertexPositions[ i ];
			const Vector3 n_i = normals[ i ].Normalized();
			const Vector2& uv_i = uvs[ i ];

			for( Uint16 j = i + 1; j < vertsCount; ++j )
			{
				const Vector3& pos_j = vertexPositions[ j ];
				const Vector3 n_j = normals[ j ].Normalized();
				const Float posisionDifference = pos_i.DistanceTo( pos_j );
				const Float normalsAngle = RAD2DEG( MAcos_safe( n_i.Dot( n_j ) ) );
				const Vector2& uv_j = uvs[ j ];
				const Float uvDifference = uv_i.DistanceTo( uv_j );

				if ( posisionDifference <= POS_EPS && normalsAngle <= NORMAL_ANGLE_EPS && uvDifference <= UV_DIFF_EPS )
				{
					//OutputDebugStringA( String::Printf( "Duplicate (positionDiff = %1.9f, uvDiff: %1.9f, normalsAngle: %1.9f):\n", posisionDifference, uvDifference, normalsAngle ).AsChar() );
					//OutputDebugStringA( String::Printf( "  V1 #%d: pos[%1.8f, %1.8f, %1.8f] uv[%1.8f, %1.8f] normal[%1.8f, %1.8f, %1.8f]\n", i, pos_i.X, pos_i.Y, pos_i.Z, uv_i.X, uv_i.Y, n_i.X, n_i.Y, n_i.Z ).AsChar() );
					//OutputDebugStringA( String::Printf( "  V2 #%d: pos[%1.8f, %1.8f, %1.8f] uv[%1.8f, %1.8f] normal[%1.8f, %1.8f, %1.8f]\n", j, pos_j.X, pos_j.Y, pos_j.Z, uv_j.X, uv_j.Y, n_j.X, n_j.Y, n_j.Z ).AsChar() );

					InsertVertexDuplicate( i, j, vertexDuplicates );
					InsertVertexDuplicate( j, i, vertexDuplicates );
				}
			}
		}

		// looping over triangles
		for( Uint32 i = 0; i < triangleCount; ++i )
		{
			Uint16 idx0 = indices[ i * 3 + 0 ];
			Uint16 idx1 = indices[ i * 3 + 1 ];
			Uint16 idx2 = indices[ i * 3 + 2 ];

			minIndex = Min< Uint16 >( minIndex, idx0 );
			minIndex = Min< Uint16 >( minIndex, idx1 );
			minIndex = Min< Uint16 >( minIndex, idx2 );
			maxIndex = Max< Uint16 >( maxIndex, idx0 );
			maxIndex = Max< Uint16 >( maxIndex, idx1 );
			maxIndex = Max< Uint16 >( maxIndex, idx2 );

			MapTriangleToIndex( idx0, idx1, idx2, mapping, vertexDuplicates );
			MapTriangleToIndex( idx1, idx2, idx0, mapping, vertexDuplicates );
			MapTriangleToIndex( idx2, idx0, idx1, mapping, vertexDuplicates );
		}

		red::DynArray< Uint32 > localTopologyData;
		red::DynArray< Uint32 > localTopologyMetadata;

		for( Uint16 i = minIndex; i <= maxIndex; ++i )
		{
			RED_ASSERT( mapping.KeyExist( i ) );
			Uint32 currentDataOffset = localTopologyData.Size();

			const red::DynArray< Triangle >& triangles = mapping[ i ].triangles;

			Uint32 currentIdxListLength = triangles.Size();

			// push a pair of metadata per vertex
			localTopologyMetadata.PushBack( currentDataOffset );
			localTopologyMetadata.PushBack( currentIdxListLength );

			for( Uint32 j = 0; j < triangles.Size(); ++j )
			{
				const Triangle& t = triangles[ j ];

				localTopologyData.PushBack( (Uint32)t.idx0 );
				localTopologyData.PushBack( (Uint32)t.idx1 );
				localTopologyData.PushBack( (Uint32)t.idx2 );
			}
		}

		// fill buffers
		newChunk.m_topologyData = DataBuffer( localTopologyData.DataSize() );
		newChunk.m_topologyMetadata = DataBuffer( localTopologyMetadata.DataSize() );
		red::Memcpy( newChunk.m_topologyData.Data(), localTopologyData.Data(), localTopologyData.DataSize() );
		red::Memcpy( newChunk.m_topologyMetadata.Data(), localTopologyMetadata.Data(), localTopologyMetadata.DataSize() );

		return ImportResult::Success( nullptr );
	}
}

struct VertexTypeGenerationFlags
{
	Bool usesLightBlockers			: 1;
	Bool usesVehicleDamage			: 1;
	Bool usesDestruction			: 1;
	Bool usesSkinningWeights		: 1;
	Bool usesSkinningWeightsExt		: 1;
	Bool usesGarmentMorphOffsets	: 1;
	Bool usesSpeedTreeWind			: 1;
	Bool usesSingleBoneSkinning		: 1;
	Bool isStaticProxyMesh			: 1;
};

EMeshVertexType GetChunkVertexType( const VertexTypeGenerationFlags flags )
{
	// Calculate proper vertex format.
	EMeshVertexType result = MVT_StaticMesh;

	if ( flags.usesVehicleDamage )
	{
		result = flags.usesSkinningWeights ? MVT_SkinnedMeshVehicle : MVT_StaticMeshVehicle;
	}
	else if ( flags.usesSingleBoneSkinning )
	{
		result = MVT_SkinnedMeshSingleBone;
	}
	else if ( flags.usesDestruction )
	{
		result = flags.usesSkinningWeights ? MVT_DestructibleMeshSkinned : MVT_DestructibleMesh;
	}
	else if ( flags.usesSkinningWeights )
	{
		// TODO: Replace with SelectSkinnedVertexType from meshVertex.h
		if ( flags.usesSkinningWeightsExt )
		{
			if ( flags.usesGarmentMorphOffsets )
			{
				result = flags.usesLightBlockers ? MVT_ExtGarmentSkinnedMeshLightBlocker : MVT_ExtGarmentSkinnedMesh;
			}
			else
			{
				result = flags.usesLightBlockers ? MVT_ExtSkinnedMeshLightBlocker : MVT_ExtSkinnedMesh;
			}
		}
		else
		{
			if ( flags.usesGarmentMorphOffsets )
			{
				result = flags.usesLightBlockers ? MVT_GarmentSkinnedMeshLightBlocker : MVT_GarmentSkinnedMesh;
			}
			else
			{
				result = flags.usesLightBlockers ? MVT_SkinnedMeshLightBlocker : MVT_SkinnedMesh;
			}
		}
	}
	else if ( flags.usesSpeedTreeWind && !flags.usesDestruction )
	{
		result = MVT_SpeedTreeMesh;
	}

	if ( result == MVT_StaticMesh && flags.isStaticProxyMesh )
	{
		result = MVT_ProxyMesh;
	}
	else if ( result == MVT_DestructibleMesh && flags.isStaticProxyMesh )
	{
		result = MVT_ProxyWindowMesh;
	}

	return result;
}

ImportResult ImportChunks( CMesh* meshData, const std::vector< ReFileMeshPtr >& meshes, const red::DynArray< red::DynArray< Int32 > >& rigMapping,
						   const mesh::DataViewRO& meshView, const mesh::ChunkFlags& chunkFlags, const Bool importUV2AsMorphVertexData, const Bool prepareTopology,
						   THandle<CREXMeshResourceImportOptions>& options, mesh::DataBuilder& buildData, const Bool importDestructionData )
{
	//-----
	const ReFileString destructionBigChunkIDName( "re_attrbig_chunk_id" );
	const ReFileString destructionSmallChunkIDName( "re_attrsmall_chunk_id" );
	const ReFileString lightBlockerDataChannelName( "re_lightCone" );
	const ReFileString speedTreeWindBranchPosAndWeightChannelName("WindBranchPosAndWeight");
	const ReFileString speedTreeWindBranchDirChannelName("WindBranchDir");	
	//-----

	String warningMessage = String::EMPTY();
	if( !prepareTopology )
	{
		meshData->RemoveParameter( ClassID< mesh::MeshParamTopologyData >() );
		meshData->RemoveParameter( ClassID< mesh::MeshParamTopologyMetadata >() );
	}

	std::vector< float > lodDistances = {0.0, 0.0, 0.0, 0.0};

	if( meshes.size() > 0 )
	{
		meshes[ 0 ]->getLodDistances( lodDistances );
	}

	MESH_IMPORT_LOG( "   Importing chunks from %u RE meshes...", static_cast< Uint32 >( meshes.size() ) );

	Uint32 meshesSize = (Uint32)meshes.size();

	if( auto paramBDRD = meshData->GetParameter< mesh::MeshParamBakedDestructionData >() )
	{
		paramBDRD->Clear();
	}

	for( Uint32 i = 0; i < meshesSize; i++ )
	{
		ReFileMesh& reMesh = *meshes[ i ];
		Int32 numChunks = reMesh.getNumChunks();

		if( numChunks == 0 )
			continue;

		MESH_IMPORT_LOG( "      Importing chunks from mesh %u, found %u chunks", i, numChunks );

		for( Int32 c = 0; c < numChunks; c++ )
		{
			ReFileMeshChunkPtr reChunk = reMesh.getMeshChunk( c );
			reString reMaterialUUID = reChunk->getMaterialUuid();
			reString reMehsUUID = reChunk->getMeshUuid();
			String materialUUID = String( reMaterialUUID.getData(), reMaterialUUID.getLength() );
			String meshUUID = String( reMehsUUID.getData(), reMehsUUID.getLength() );

			auto lodIndex = reChunk->getLod();
			if( i > 0 && lodIndex == 0 )
			{
				lodIndex = i;
			}

			// This is needed to prevent some old Re files from crashing.
			if( lodIndex < 0 || lodIndex >= lodDistances.size() )
			{
				lodIndex = i;
			}

			Float newLevelDistance = 0.0f + lodIndex * 20.0f;
			if( lodIndex < 4 )
			{
				if( lodDistances[ lodIndex ] != 0.0f )
				{
					newLevelDistance = lodDistances[ lodIndex ];
				}
			}
			Bool addNewLodLevel = true;
			for( auto& lod : buildData.m_lods )
			{
				if( lod.m_distance == newLevelDistance )
				{
					addNewLodLevel = false;
					break;
				}
			}

			MESH_IMPORT_LOG( "         Importing chunk %u:   LOD %u (org %u),   material UUID '%hs',   mesh UUID '%hs'", c, lodIndex, reChunk->getLod(), materialUUID.AsChar(), meshUUID.AsChar() );

			if( addNewLodLevel )
			{
				MESH_IMPORT_LOG( "            Adding new LOD level" );

				mesh::DataBuilder::UnpackedLOD newLevel;
				newLevel.m_distance = newLevelDistance;
				buildData.m_lods.PushBack( newLevel );
			}

			//maintain backwards compatibility old . re file
			if( lodIndex == 0 && i > 0 )
			{
				lodIndex = i;
			}
			/// @todo: importing LOD distances from RE...

			CName matName;
			String matNameTemp;

			ReFileHelpers::GetMaterialName( reChunk.get(), matNameTemp );
			matName = RED_NAME( matNameTemp );

			if( matName.Empty() )
				matName = RED_NAME_CONSTEXPR( "default" );

			if( ReFileHelpers::JSONMaterialInstanceSerializer::HasMaterialInstanceData( reChunk ) )
			{
				std::string materialNameBlob;
				std::string materialDataBlob;
				reChunk->getTextBlob( "originalMaterialName", materialNameBlob );
				reChunk->getTextBlob( "originalMaterialData", materialDataBlob );
				matName = RED_NAME( materialNameBlob.c_str() );
				THandle< CMaterialInstance > matInstance = ReFileHelpers::JSONMaterialInstanceSerializer::Deserialize( meshData, materialDataBlob, matName );
			}

			Uint32 chunkIndex = buildData.m_chunks.Size();
			mesh::DataBuilder::UnpackedChunk& newChunk = buildData.m_chunks.EmplaceBack();
			newChunk.m_lodMask = 1 << lodIndex;
			newChunk.m_materials.PushBack( matName );
			newChunk.m_flags = chunkFlags;
			newChunk.m_materialUUID = materialUUID;
			newChunk.m_meshUUID = meshUUID;

			Uint32 numVerts = static_cast< Uint32 >( reChunk->getNumVertices() );
			Uint32 numTris = static_cast< Uint32 >( reChunk->getNumFaces() );

			// --- Prepare the streams for data
			newChunk.m_streams.Resize( ECS_MAX );

			red::DynArray< StreamTranslatorBase* > translators;
			translators.Resize( ECS_MAX, nullptr );

			StreamTranslator< Vector3 > positions( newChunk.m_streams[ ECS_Positions ], numVerts, MST_Position_3F );
			translators[ ECS_Positions ] = &positions;
			StreamTranslator< Vector3 > normals( newChunk.m_streams[ ECS_Normals ], numVerts, MST_Normal_3F );
			translators[ ECS_Normals ] = &normals;
			StreamTranslator< Vector3 > tangents( newChunk.m_streams[ ECS_Tangents ], numVerts, MST_Tangent_3F );
			translators[ ECS_Tangents ] = &tangents;
			StreamTranslator< Vector3 > binormals( newChunk.m_streams[ ECS_Binormals ], numVerts, MST_Binormal_3F );
			translators[ ECS_Binormals ] = &binormals;
			StreamTranslator< Vector2 > uv0( newChunk.m_streams[ ECS_UV0 ], numVerts, MST_TexCoord0_2F );
			translators[ ECS_UV0 ] = &uv0;
			StreamTranslator< Vector2 > uv1( newChunk.m_streams[ ECS_UV1 ], numVerts, MST_TexCoord1_2F );
			translators[ ECS_UV1 ] = &uv1;
			StreamTranslator< Uint32 > colors( newChunk.m_streams[ ECS_Colors ], numVerts, MST_Color_U32 );
			translators[ ECS_Colors ] = &colors;
			StreamTranslator< Float > skinningWeights( newChunk.m_streams[ ECS_SkinningWeights ], numVerts * 4, MST_SkinningWeights_4F );
			translators[ ECS_SkinningWeights ] = &skinningWeights;
			StreamTranslator< Uint8 > skinningIndices( newChunk.m_streams[ ECS_SkinningIndices ], numVerts * 4, MST_SkinningIndices_4U8 );
			translators[ ECS_SkinningIndices ] = &skinningIndices;
			StreamTranslator< Float > skinningWeightsExt( newChunk.m_streams[ ECS_SkinningWeightsExt ], numVerts * 4, MST_SkinningWeightsExt_4F );
			translators[ ECS_SkinningWeightsExt ] = &skinningWeightsExt;
			StreamTranslator< Uint8 > skinningIndicesExt( newChunk.m_streams[ ECS_SkinningIndicesExt ], numVerts * 4, MST_SkinningIndicesExt_4U8 );
			translators[ ECS_SkinningIndicesExt ] = &skinningIndicesExt;
			StreamTranslator< Uint16 > indices( newChunk.m_streams[ ECS_Indices ], numTris * 3, MST_Index_U16 );
			translators[ ECS_Indices ] = &indices;
			StreamTranslator< Uint32 > destructionIndices( newChunk.m_streams[ ECS_DestructionIndices ], numVerts, MST_DestructionIndices_2U16 );
			translators[ ECS_DestructionIndices ] = &destructionIndices;
			StreamTranslator< Vector3 > morphOffsets( newChunk.m_streams[ ECS_MorphOffsets ], numVerts, MST_MorphOffset_3F );
			translators[ ECS_MorphOffsets ] = &morphOffsets;
			StreamTranslator< Uint32 > garmentFlags( newChunk.m_streams[ ECS_GarmentFlags ], numVerts, MST_GarmentFlags_U32 );
			translators[ ECS_GarmentFlags ] = &garmentFlags;
			StreamTranslator< Vector3 > vehicleDmgPosition( newChunk.m_streams[ ECS_VehicleDamagePosition ], numVerts, MST_VehicleDmgPosFront_3F );
			translators[ ECS_VehicleDamagePosition ] = &vehicleDmgPosition;
			StreamTranslator< Vector3 > vehicleDmgNormal( newChunk.m_streams[ ECS_VehicleDamageNormal ], numVerts, MST_VehicleDmgNormalFront_3F );
			translators[ ECS_VehicleDamageNormal ] = &vehicleDmgNormal;
			translators[ ECS_VehicleDmgPosSides ] = nullptr; // Obsolete, unused
			translators[ ECS_VehicleDmgNormalSides ] = nullptr; // Obsolete, unused
			StreamTranslator< Vector3 > morphVertexDataStream( newChunk.m_streams[ ECS_MorphVertexData ], numVerts, MST_MorphVertexData_3F );
			translators[ ECS_MorphVertexData ] = &morphVertexDataStream;
			StreamTranslator< Float > lightBlockerIntensityStream( newChunk.m_streams[ ECS_LightBlockerIntensity ], numVerts, MST_LightBlockerIntensity_1F );
			translators[ ECS_LightBlockerIntensity ] = &lightBlockerIntensityStream;

			StreamTranslator< Vector4 > windBranchData(newChunk.m_streams[ECS_WindBranchData_4F], numVerts, MST_WindBranchData_4F);
			translators[ECS_WindBranchData_4F] = &windBranchData;
			StreamTranslator< Float > branchData(newChunk.m_streams[ECS_BranchData_7F], numVerts * 7, MST_BranchData_7F);
			translators[ECS_BranchData_7F] = &branchData;

			// --- Get the data channels direct access.
			unsigned int pDataCount = 0;
			unsigned int tDataCount = 0;
			unsigned int bDataCount = 0;
			unsigned int nDataCount = 0;
			unsigned int uv1DataCount = 0;
			unsigned int uv2DataCount = 0;
			unsigned int cDataCount = 0;
			unsigned int wtDataCount = 0;
			unsigned int wiDataCount = 0;
			unsigned int ewtDataCount = 0;
			unsigned int ewiDataCount = 0;
			unsigned int morphOffsetsDataCount = 0;
			unsigned int garmentFlagsDataCount = 0;
			unsigned int vDmgNFDataCount = 0; // Vehicle damage normal
			unsigned int vDmgPFDataCount = 0; // Vehicle damage position
			unsigned int morphVertexDataCount = 0;
			unsigned int windBranchPosAndWeightDataCount = 0;
			unsigned int windBranchDirDataCount = 0;
			unsigned int lightBlockerDataCount = 0;

			reFloat3* pData = nullptr;
			reFloat3* tData = nullptr;
			reFloat3* bData = nullptr;
			reFloat3* nData = nullptr;
			reFloat2* uv1Data = nullptr;
			reFloat2* uv2Data = nullptr;
			reFloat4* cData = nullptr;
			reFloat4* wtData = nullptr;
			reFloat4* wiData = nullptr;
			reFloat4* ewtData = nullptr;
			reFloat4* ewiData = nullptr;
			reFloat3* morphOffsetsData = nullptr;
			int* garmentFlagsData = nullptr;
			reFloat3* vDmgNFData = nullptr;
			reFloat3* vDmgPFData = nullptr;
			reFloat3* morphVertexData = nullptr;
			reFloat4* windBranchPosAndWeightData = nullptr;
			reFloat4* windBranchDirData = nullptr;
			reFloat3* vLightBlockerData = nullptr;

			reChunk->getDataChannel( "P", pDataCount, pData );
			reChunk->getDataChannel( "T", tDataCount, tData );
			reChunk->getDataChannel( "B", bDataCount, bData );
			reChunk->getDataChannel( "N", nDataCount, nData );
			reChunk->getDataChannel( "UV1", uv1DataCount, uv1Data );
			reChunk->getDataChannel( "UV2", uv2DataCount, uv2Data );
			reChunk->getDataChannel( "C", cDataCount, cData );
			reChunk->getDataChannel( "WT", wtDataCount, wtData );
			reChunk->getDataChannel( "WI", wiDataCount, wiData );
			reChunk->getDataChannel( "EWT", ewtDataCount, ewtData );
			reChunk->getDataChannel( "EWI", ewiDataCount, ewiData );
			reChunk->getDataChannel( "re_attrv3morph_target", morphOffsetsDataCount, morphOffsetsData ); // The "re_attr" prefix is added by the RE Writer plugin in Houdini
			reChunk->getDataChannel( "re_morphVertexData", morphVertexDataCount, morphVertexData );
			reChunk->getDataChannel( "re_attrgarmentFlags", garmentFlagsDataCount, garmentFlagsData );
			reChunk->getDataChannel( "re_attrv3VDNF", vDmgNFDataCount, vDmgNFData );
			reChunk->getDataChannel( "re_attrv3VDPF", vDmgPFDataCount, vDmgPFData );
			reChunk->getDataChannel( "WindBranchPosAndWeight", windBranchPosAndWeightDataCount, windBranchPosAndWeightData );
			reChunk->getDataChannel( "WindBranchDir", windBranchDirDataCount, windBranchDirData );
			reChunk->getDataChannel( "re_lightCone", lightBlockerDataCount, vLightBlockerData );

			Bool vertexDataConsistent = ( pDataCount == tDataCount && tDataCount == bDataCount && bDataCount == nDataCount && nDataCount == wtDataCount && wtDataCount == wiDataCount && wiDataCount == ewtDataCount && ewtDataCount == ewiDataCount && ewiDataCount == ewtDataCount && ewtDataCount == numVerts );

			if( !vertexDataConsistent )
				return ImportResult::Error( String( "This mesh is corrupted!" ) );

			const Bool isVehicleDamageValid = ( pDataCount == vDmgNFDataCount && vDmgNFDataCount == vDmgPFDataCount );
			const Bool usesVehicleDamage = isVehicleDamageValid;
			Bool usesSkinningWeights = false;
			Bool usesSkinningWeightsExt = false;
			Bool usesGarmentMorphOffsets = false;
			Bool usesDestruction = false;
			Bool usesSpeedTreeWind = false;
			Bool usesLightBlockers = false;

			//Because Maya has no option to export UVs into custom vertex stream, we do a copy of UV2 into re_morphVertexData. 
			//re_morphVertexData is never bound on GPU and is only needed for the editor on CPU when building morphs.
			if ( importUV2AsMorphVertexData )
			{
				if ( uv2DataCount == 0 )
				{
					return ImportResult::Error( "Selected to importUV2AsMorphVertexData but UV2 vertex stream has no data" );
				}

				if ( morphVertexDataCount > 0 )
				{
					return ImportResult::Error( "Selected to importUV2AsMorphVertexData but re_morphVertexData vertex stream is already filled with data" );
				}

				//copy UV2 into re_morphVertexData
				morphVertexDataCount = uv2DataCount;
				morphVertexData = RED_NEW_ARRAY( reFloat3, morphVertexDataCount, red::PoolBackend );
				for ( Uint32 i = 0; i < morphVertexDataCount; ++i )
				{
					morphVertexData[i] = reFloat3( uv2Data[i].r, uv2Data[i].g, 0.0f );
				}

				//discard UV2 stream
				uv2DataCount = 0;
				uv2Data = nullptr;
			}
			Bool usesMorphVertexWeights = morphVertexDataCount > 0;


			// --- destruction attributes
			if (reChunk->hasDataChannel(destructionBigChunkIDName) && reChunk->hasDataChannel(destructionSmallChunkIDName))
			{
				for (Int32 v = 0; v < reChunk->getNumVertices(); v++)
				{
					if (wtData[v].x > 0.f || wtData[v].y > 0.f || wtData[v].z > 0.f || wtData[v].z > 0.f)
						usesSkinningWeights |= true;
				}
				usesDestruction = true;
			}




			for( Int32 v = 0; v < reChunk->getNumVertices(); v++ )
			{
				positions.Write( Vector3( pData[ v ].x * c_fromReScale, pData[ v ].y * c_fromReScale, pData[ v ].z * c_fromReScale ) );
				normals.Write( Vector3( nData[ v ].x, nData[ v ].y, nData[ v ].z ) );

				BYTE ccc[ 4 ];
				ccc[ 0 ] = ( BYTE )( cData[ v ].x * 255 );
				ccc[ 1 ] = ( BYTE )( cData[ v ].y * 255 );
				ccc[ 2 ] = ( BYTE )( cData[ v ].z * 255 );
				ccc[ 3 ] = ( BYTE )( cData[ v ].w * 255 );
				colors.Write( *( (Uint32*)ccc ) );

				tangents.Write( Vector3( tData[ v ].x, tData[ v ].y, tData[ v ].z ) );
				binormals.Write( Vector3( bData[ v ].x, bData[ v ].y, bData[ v ].z ) );

				if( v < (Int32)uv1DataCount )
					uv0.Write( Vector2( uv1Data[ v ].x, uv1Data[ v ].y ) );
				if( v < (Int32)uv2DataCount )
					uv1.Write( Vector2( uv2Data[ v ].x, uv2Data[ v ].y ) );

				// --- light blockers
				if ( options->m_importLightConeData && reChunk->hasDataChannel( lightBlockerDataChannelName ) )
				{
					lightBlockerIntensityStream.Write( vLightBlockerData[ v ].x );
					usesLightBlockers = true;
				}

				// --- destruction attributes
				if (usesDestruction)
				{
					Int32 big, small;
					reChunk->getDataChannelValue(destructionBigChunkIDName, v, big);
					reChunk->getDataChannelValue(destructionSmallChunkIDName, v, small);
					Uint32 packed = small | (big << 16);
					destructionIndices.Write(packed);
				}

				// --- SpeedTree attributes
				if ( reChunk->hasDataChannel( speedTreeWindBranchPosAndWeightChannelName ) && reChunk->hasDataChannel( speedTreeWindBranchDirChannelName ) )
				{
					windBranchData.Write( Vector4( 0, 0, 0, windBranchPosAndWeightData[ v ].w ) );
					branchData.Write(windBranchPosAndWeightData[v].x);
					branchData.Write(windBranchPosAndWeightData[v].y);
					branchData.Write(windBranchPosAndWeightData[v].z);
					branchData.Write(windBranchDirData[v].x);
					branchData.Write(windBranchDirData[v].y);
					branchData.Write(windBranchDirData[v].z);
					branchData.Write(0);

					usesSpeedTreeWind = true;
				}

				skinningWeights.Write( wtData[ v ].x );
				skinningWeights.Write( wtData[ v ].y );
				skinningWeights.Write( wtData[ v ].z );
				skinningWeights.Write( wtData[ v ].w );
				skinningWeightsExt.Write( ewtData[ v ].x );
				skinningWeightsExt.Write( ewtData[ v ].y );
				skinningWeightsExt.Write( ewtData[ v ].z );
				skinningWeightsExt.Write( ewtData[ v ].w );

				// --- Determine what type of mesh this chunk is
				if( wtData[ v ].x > 0.f || wtData[ v ].y > 0.f || wtData[ v ].z > 0.f || wtData[ v ].z > 0.f )
					usesSkinningWeights = true;

				if( ewtData[ v ].x > 0.f || ewtData[ v ].y > 0.f || ewtData[ v ].z > 0.f || ewtData[ v ].w > 0.f )
					usesSkinningWeightsExt = true;

				// --- Assign bone indices to vertices
				if( usesDestruction && usesSkinningWeights )
				{
					skinningIndices.Write( (Uint8)wiData[ v ].x );
					skinningIndices.Write( (Uint8)wiData[ v ].y );
					skinningIndices.Write( (Uint8)wiData[ v ].z );
					skinningIndices.Write( (Uint8)wiData[ v ].w );
				}
				else if( rigMapping[ i ].Size() )
				{
					skinningIndices.Write( rigMapping[ i ][ (Uint8)wiData[ v ].x ] );
					skinningIndices.Write( rigMapping[ i ][ (Uint8)wiData[ v ].y ] );
					skinningIndices.Write( rigMapping[ i ][ (Uint8)wiData[ v ].z ] );
					skinningIndices.Write( rigMapping[ i ][ (Uint8)wiData[ v ].w ] );
					skinningIndicesExt.Write( rigMapping[ i ][ (Uint8)ewiData[ v ].x ] );
					skinningIndicesExt.Write( rigMapping[ i ][ (Uint8)ewiData[ v ].y ] );
					skinningIndicesExt.Write( rigMapping[ i ][ (Uint8)ewiData[ v ].z ] );
					skinningIndicesExt.Write( rigMapping[ i ][ (Uint8)ewiData[ v ].w ] );
				}
				else
				{
					skinningIndices.Write( 0 );
					skinningIndices.Write( 0 );
					skinningIndices.Write( 0 );
					skinningIndices.Write( 0 );
					skinningIndicesExt.Write( 0 );
					skinningIndicesExt.Write( 0 );
					skinningIndicesExt.Write( 0 );
					skinningIndicesExt.Write( 0 );
				}


				// --- garment attributes
				if( morphOffsetsDataCount >= numVerts )
				{
					Vector3 morphOffset( morphOffsetsData[ v ].x * c_fromReScale, morphOffsetsData[ v ].y * c_fromReScale, morphOffsetsData[ v ].z * c_fromReScale );
					morphOffsets.Write( morphOffset );
					usesGarmentMorphOffsets |= !morphOffset.IsZero();
				}

				if( garmentFlagsDataCount >= numVerts )
				{
					garmentFlags.Write( garmentFlagsData[ v ] );
				}

				// --- vehicle damage attributes
				if( usesVehicleDamage )
				{
					const Vector3 normal = Vector3( vDmgNFData[ v ].x, vDmgNFData[ v ].y, vDmgNFData[ v ].z );
					const Vector3 position = Vector3( vDmgPFData[ v ].x * c_fromReScale, vDmgPFData[ v ].y * c_fromReScale, vDmgPFData[ v ].z * c_fromReScale );
					vehicleDmgNormal.Write( normal );
					vehicleDmgPosition.Write( position );
				}

				//	--- Morph vertex attributes
				if( usesMorphVertexWeights )
				{
					morphVertexDataStream.Write( Vector3( morphVertexData[ v ].x, morphVertexData[ v ].y, morphVertexData[ v ].z ) );
				}
			}

			VertexTypeGenerationFlags flags;
			flags.usesLightBlockers			= usesLightBlockers;
			flags.usesVehicleDamage			= usesVehicleDamage;
			flags.usesDestruction			= usesDestruction;
			flags.usesSkinningWeights		= usesSkinningWeights;
			flags.usesSkinningWeightsExt	= usesSkinningWeightsExt;
			flags.usesGarmentMorphOffsets	= usesGarmentMorphOffsets;
			flags.usesSpeedTreeWind			= usesSpeedTreeWind;
			flags.usesSingleBoneSkinning	= importDestructionData; // currently destruction only
			flags.isStaticProxyMesh = false;
			if ( reMesh.nodeFlags().hasFlag( "staticProxyMesh" ) )
			{
				flags.isStaticProxyMesh = true;
			}

			newChunk.m_vertexType = GetChunkVertexType( flags );

			// --- Importing faces
			unsigned int fv0iCount = 0;
			int* fv0Data = nullptr;
			reChunk->getDataChannel( "FV0I", fv0iCount, fv0Data );

			unsigned int fv1iCount = 0;
			int* fv1Data = nullptr;
			reChunk->getDataChannel( "FV1I", fv1iCount, fv1Data );

			unsigned int fv2iCount = 0;
			int* fv2Data = nullptr;
			reChunk->getDataChannel( "FV2I", fv2iCount, fv2Data );

			bool faceDataConsistent = fv0iCount == fv1iCount && fv1iCount == fv2iCount && fv2iCount;
			if( !faceDataConsistent )
				return ImportResult::Error( String( "This mesh is corrupted!" ) );

			for( Int32 ff = 0, end = fv0iCount; ff < end; ++ff )
			{
				Uint32 x = fv0Data[ ff ];
				Uint32 y = fv1Data[ ff ];
				Uint32 z = fv2Data[ ff ];

				indices.Write( (Uint16)z );
				indices.Write( (Uint16)y );
				indices.Write( (Uint16)x );
			}

			if( lodIndex == 0 && prepareTopology )
			{
				if( !IsSkinnedMeshVertexType( newChunk.m_vertexType ) )
				{
					return ImportResult::Error( String::Printf( "Importing mesh that is not skinned with \"Prepare Mesh Topology\" flag turned on, please disable \"Prepate Mesh Topology\" flag!" ) );
				}

				ImportResult topologyResult = PrepareTopology( newChunk );
				if( topologyResult.HasFailed() )
				{
					return topologyResult;
				}
			}
			else
			{
				newChunk.m_topologyMetadata.Clear();
			}

			if( reChunk->hasDataChannel( "destruction_data" ) )
			{
				reFloat4* regionData = nullptr;
				Uint32 regionDataCount = 0;

				reChunk->getDataChannel( "destruction_data", regionDataCount, regionData );
				auto paramBDRD = meshData->CreateParameter< mesh::MeshParamBakedDestructionData >();
				Uint32 totalIndices = indices.GetNumWrittenElements();
				for( Uint32 rd = 0; rd < regionDataCount; ++rd )
				{
					Float firstIndexFloat = regionData[ rd ].y;
					Uint16 firstIndex = 0;
					Uint32 count = 0;
					Uint16 lastIndex = (Uint16)( totalIndices - 1 );
					if( firstIndexFloat >= 0.f  )
					{
						firstIndex = (Uint16)(3 * regionData[ rd ].y);
						Uint32 nextRd = rd + 1;
						count = nextRd < regionDataCount ? (Uint16)(3 * regionData[ nextRd ].y) : totalIndices;
						count -= firstIndex;
					}
					if( firstIndex > lastIndex )
					{
						return ImportResult::Error( String::Printf( "Importing mesh that has corrupted baked destruction data - region %d index offsets for chunk %d \
							are wrong - start index %d while chunk has %d total indices!", rd, chunkIndex, firstIndex, totalIndices ) );
					}
					if( firstIndex + count > totalIndices )
					{
						return ImportResult::Error( String::Printf( "Importing mesh that has incorrect baked destruction data - region %d index offsets for chunk %d \
							are wrong - span [%d, %d] exceeds chunk total indices (%d)!", rd, chunkIndex, firstIndex, firstIndex + count, totalIndices ) );
					}
					paramBDRD->AppendRegionData( (Uint32)regionData[ rd ].x, chunkIndex, firstIndex, count, (Uint16)regionData[ rd ].w );
				}
				paramBDRD->m_indices.EmplaceBack( DataBuffer::Copy( newChunk.m_streams[ ECS_Indices ].m_data, rend::PoolMesh() ) );
			}

			// Remove unused streams
			RED_FATAL_ASSERT( newChunk.m_streams.Size() == translators.Size() );

			for( Uint32 i = newChunk.m_streams.Size() - 1; i > 0; --i ) // Stream 0 is positions, we never remove that.
			{
				if ( translators[ i ] == nullptr )
				{
					newChunk.m_streams.RemoveAt( i );
					continue;
				}

				mesh::UnpackedStream& stream = newChunk.m_streams[ i ];
				Uint32 streamDataSize = Uint32( stream.m_data.Size() );
				Uint32 numWrittenElements = Uint32( translators[ i ]->GetNumWrittenElements() );

				if( ( streamDataSize == 0 ) || ( numWrittenElements == 0 ) )
				{
					newChunk.m_streams.RemoveAt( i );
				}
			}
		}

		if ( prepareTopology )
		{
			DynArray< Uint32 > topologyDataOffsets;
			DynArray< Uint32 > topologyDataSizes;

			DynArray< Uint32 > topologyMetadataOffsets;
			DynArray< Uint32 > topologyMetadataSizes;


			topologyDataOffsets.Resize( buildData.m_chunks.Size() );
			topologyDataSizes.Resize( buildData.m_chunks.Size() );
			topologyMetadataOffsets.Resize( buildData.m_chunks.Size() );
			topologyMetadataSizes.Resize( buildData.m_chunks.Size() );

			// create single topology buffer
			Uint32 currentTopologyDataOffset = 0;
			Uint32 totalTopologyDataSize = 0;

			Uint32 currentTopologyMetadataOffset = 0;
			Uint32 totalTopologyMetadataSize = 0;
			for ( Uint32 i = 0; i < buildData.m_chunks.Size(); ++i )
			{
				const Uint32 currentTopologyDataSize		= buildData.m_chunks[ i ].m_topologyData.Size();
				const Uint32 currentTopologyMetadataSize	= buildData.m_chunks[ i ].m_topologyMetadata.Size();

				topologyDataOffsets[ i ]					= currentTopologyDataOffset;
				topologyDataSizes[ i ]						= currentTopologyDataSize;
				topologyMetadataOffsets[ i ]				= currentTopologyMetadataOffset;
				topologyMetadataSizes[ i ]					= currentTopologyMetadataSize;
				
				currentTopologyDataOffset					+= currentTopologyDataSize;
				totalTopologyDataSize						+= currentTopologyDataSize;
				currentTopologyMetadataOffset				+= currentTopologyMetadataSize;
				totalTopologyMetadataSize					+= currentTopologyMetadataSize;
			}

			DataBuffer totalTopologyData = DataBuffer( totalTopologyDataSize );
			void* totalDataPtr = totalTopologyData.Data();
			for ( Uint32 i = 0; i < buildData.m_chunks.Size(); ++i )
			{
				red::Memcpy( totalDataPtr, buildData.m_chunks[ i ].m_topologyData.Data(), buildData.m_chunks[ i ].m_topologyData.Size() );
				totalDataPtr = red::OffsetPtr( totalDataPtr, topologyDataSizes[ i ] );
			}

			DataBuffer totalTopologyMetadata = DataBuffer( totalTopologyMetadataSize );
			void* totalMetadataPtr = totalTopologyMetadata.Data();
			for ( Uint32 i = 0; i < buildData.m_chunks.Size(); ++i )
			{
				red::Memcpy( totalMetadataPtr, buildData.m_chunks[ i ].m_topologyMetadata.Data(), buildData.m_chunks[ i ].m_topologyMetadata.Size() );
				totalMetadataPtr = red::OffsetPtr( totalMetadataPtr, topologyMetadataSizes[ i ] );
			}

			{
				if ( THandle< mesh::MeshParamTopologyData > existingTopologyDataParam = meshData->GetParameter< mesh::MeshParamTopologyData >() )
				{
					meshData->RemoveParameter( existingTopologyDataParam );
				}
				meshData->AddParameter( CreateHandle< mesh::MeshParamTopologyData >( totalTopologyData, topologyDataOffsets, topologyDataSizes ) );
			}
			{
				if ( THandle< mesh::MeshParamTopologyMetadata > existingTopologyMetadataParam = meshData->GetParameter< mesh::MeshParamTopologyMetadata >() )
				{
					meshData->RemoveParameter( existingTopologyMetadataParam );
				}

				meshData->AddParameter( CreateHandle< mesh::MeshParamTopologyMetadata >( totalTopologyMetadata, topologyMetadataOffsets, topologyMetadataSizes ) );
			}
		}
	}

	// Copy chunk flags, try to match old chunks with new ones and copy chunk flags from old to new, so they don't get reset after re-import.
	// Matching is based on number of chunks, vertex type and LOD mask.
	
	Bool copyFlagsRes = true;

	if ( options->m_forceFlagsFromReFile )
	{
		options->ParseRenderFlagsFromReFile( options->GetSourcePath() );
		SetChunkFlags( buildData, options->m_renderFlags );
		// do it again but for every chunk separately
		SetChunkFlags(	meshView, buildData, options->m_renderFlags, options->GetSourcePath());
	}
	else if ( options->m_overrideExistingRenderFlags || !CopyChunkFlags(meshView, buildData, options->m_renderFlags) )
	{
		SetChunkFlags( buildData, options->m_renderFlags ); 
	}
	
	MESH_IMPORT_LOG( "   Importing chunks from %u RE meshes finished.", static_cast< Uint32 >( meshes.size() ) );

	return ImportResult::Success( nullptr, warningMessage );

}
