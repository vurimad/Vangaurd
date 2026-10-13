#include "build.h"
#include "meshDataBuilder.h"
#include "meshDataView.h"
#include "meshRenderBlobUtils.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"

namespace helper
{
	static void MemcpyUint32toUint16( Uint16* dest, const void* src, const Uint32 srcSize )
	{
		auto typedSrc = (const Uint32*)src;
		auto end = red::OffsetPtr( typedSrc, srcSize );
		while( typedSrc < end )
			*dest++ = (Uint16)*typedSrc++; // allows for optimization
	}

} // helper

namespace mesh
{
	DataBuilder::DataBuilder()
		: m_chunks( red::PoolBackend() )
		, m_lods( red::PoolBackend() )
		, m_bones( red::PoolBackend() )
		, m_floatTrackNames( red::PoolBackend() )
	{}

	DataBuilder::DataBuilder( const DataViewRO& sourceData )
	{
		for( Uint32 i = 0; i < sourceData.GetNumLODs(); ++i )
		{
			UnpackedLOD info;
			info.m_distance = sourceData.GetLODDistance( i );
			m_lods.PushBack( info );
		}

		for( Uint32 i = 0; i < sourceData.GetNumBones(); ++i )
		{
			UnpackedBone info;
			info.m_name = sourceData.GetBoneName( i );
			info.m_rig = sourceData.GetBoneMatrix( i );
			m_bones.PushBack( info );
		}

		for( Uint32 i = 0; i < sourceData.GetNumFloatTracks(); ++i )
		{
			m_floatTrackNames.PushBack( sourceData.GetFloatTrackName( i ) );
		}

		for( Uint32 i = 0; i < sourceData.GetNumChunks(); ++i )
		{
			UnpackedChunk info;
			info.m_vertexType = sourceData.GetChunkVertexType( i );
			info.m_flags = sourceData.GetChunkFlags( i );
			info.m_lodMask = sourceData.GetChunkLodMask( i );

			info.m_materialUUID = sourceData.GetChunkSourceMaterialUUID(i);
			info.m_meshUUID = sourceData.GetChunkSourceMeshUUID(i);

			for( Uint32 j = 0; j < sourceData.GetChunkMaterialCount( i ); ++j )
				info.m_materials.PushBack( sourceData.GetChunkMaterialName( i, j ) );

			Uint32 chunkStreamMask = (Uint32)sourceData.GetChunkStreamMask( i );

			for( Uint32 streamIndex = 0; streamIndex < MAX_MESH_STREAMS; ++streamIndex )
			{
				Uint32 streamMask = RED_FLAG( streamIndex );
				if( ( streamMask & chunkStreamMask ) == 0 )
					continue;

				EMeshStreamType streamType = (EMeshStreamType)streamMask;

				ChunkStreamDataRO streamData = sourceData.GetChunkStreamData( i, streamType );

				UnpackedStream& stream = info.m_streams.EmplaceBack();
				stream.m_type = streamType;
				stream.m_data = DataBuffer::Copy( streamData.m_data, streamData.m_count * streamData.m_stride );
			}
			m_chunks.PushBack( std::move( info ) );
		}
	}

	DataBuilder::~DataBuilder() = default;

	void DataBuilder::Flush( CMesh& outMesh, const FlushOptions& flushOptions /*=FlushOptions::Default()*/ )
	{
		// preserve existing LOD settings
		red::DynArray< Float > preservedLodDistances;
		if( !flushOptions.m_rebuildLODDistances )
		{
			for ( Uint32 i = 0; i < outMesh.m_lodLevelInfo.Size(); ++i )
			{
				preservedLodDistances.PushBack( outMesh.m_lodLevelInfo[ i ] );
			}
		}

		// Match old chunks with new ones, find you which ones are new. We do it now, before we delete the old chunks.
		red::DynArray< Int32 > chunksMapping = { red::PoolBackend() };
		red::Map<Int32, CName> newMaterialsNames = { red::PoolBackend() };

		if (!flushOptions.m_matchByOrder)
		{
			Uint32 numUnmappedChunks = MapNewChunksToOldChunks(chunksMapping, outMesh, m_chunks);
			MapNewChunksToOtherNewChunks(chunksMapping, m_chunks);
			MapNewChunksFallback(chunksMapping, m_chunks, newMaterialsNames);                   // We might still get some unmatched chunks.
		}
		else
		{
			Uint32 oldChunksNumber = outMesh.m_chunks.Size();
			for (Uint32 i = 0; i < m_chunks.Size(); i++)
			{
				if (i < oldChunksNumber)
				{
					chunksMapping.PushBack(i);
				}
				else
				{
					chunksMapping.PushBack(0);
				}
			}

		}

		// flush existing data
		outMesh.m_chunks.Clear();
		outMesh.m_lodLevelInfo.Clear();
		outMesh.m_boneNames.Clear();
		outMesh.m_boneRigMatrices.Clear();
		outMesh.m_boneVertexEpsilons.Clear();
		//outMesh.m_appearances.Clear();
		outMesh.m_floatTrackNames.Clear();

		for ( const auto& floatTrackName : m_floatTrackNames )
		{
			outMesh.m_floatTrackNames.PushBack( floatTrackName );
		}

		// failsafe for LODs
		for( Uint32 i : m_chunks.Indices() )
		{
			auto& srcChunk = m_chunks[ i ];
			if ( 0 == srcChunk.m_lodMask )
			{
				RED_LOG_WARNING( "Mesh: Chunk %d is not assigned to any LOD, assigning to LOD0 as fallback", i );
				srcChunk.m_lodMask = 1;
			}
		}

		// create new LODs
		for( Uint32 i = 0; i < m_lods.Size(); ++i )
		{
			const auto& src = m_lods[ i ];

			outMesh.m_lodLevelInfo.PushBack( (!flushOptions.m_rebuildLODDistances && preservedLodDistances.Size() == m_lods.Size()) ? preservedLodDistances[ i ] : src.m_distance );
		}

		// create new bones
		for( const auto& src : m_bones )
		{
			outMesh.m_boneNames.PushBack( src.m_name );
			outMesh.m_boneRigMatrices.PushBack( src.m_rig );
			outMesh.m_boneVertexEpsilons.PushBack( 0.0f ); // recomputed later
		}

		mesh::DataView view( outMesh );
		red::DynArray< CName > materials;

		// create chunks
		Uint32 currentVertexOffset = 0;
		Uint32 currentIndexOffset = 0;
		outMesh.m_chunks.Reserve( m_chunks.Size() );
		for( const auto& srcChunk : m_chunks )
		{
			SMeshChunkPacked& destChunk = outMesh.m_chunks.EmplaceBack();

			// copy chunk data
			destChunk.m_vertexType = srcChunk.m_vertexType;
			destChunk.m_materialID = srcChunk.m_materials;									/// @todo DP: remove this?

			// setup LOD mask
			destChunk.m_lodMask = srcChunk.m_lodMask;

			// setup render mask
			*(Uint16*)&destChunk.m_chunkRenderMask = 0;
			if ( srcChunk.m_flags.m_renderInScene )				*(Uint16*)&destChunk.m_chunkRenderMask |= (Uint16)MCF_RenderInScene;
			if ( srcChunk.m_flags.m_renderInShadows )			*(Uint16*)&destChunk.m_chunkRenderMask |= (Uint16)MCF_RenderInShadows;
			if ( srcChunk.m_flags.m_isTwoSided )				*(Uint16*)&destChunk.m_chunkRenderMask |= (Uint16)MCF_IsTwoSided;
			if ( srcChunk.m_flags.m_isRayTracedEmissive )		*(Uint16*)&destChunk.m_chunkRenderMask |= (Uint16)MCF_IsRayTracedEmissive;

			// computed
			destChunk.m_numBonesPerVertex = m_bones.Size() ? 4 : 1; // HACK, this param is going away

			Uint32 vertexCount = 0;
			Uint32 indexCount = 0;

			destChunk.m_streams.Reserve( srcChunk.m_streams.Size() );
			for ( const auto& srcStream : srcChunk.m_streams )
			{
				SMeshStream& dstStream = destChunk.m_streams.EmplaceBack();
				dstStream.m_data.Editor_SetContentDirect( srcStream.m_data.Data(), srcStream.m_data.Size() );
				dstStream.m_type = srcStream.m_type;

				if( srcStream.m_type == MST_Position_3F )
				{
					vertexCount = srcStream.m_data.Size() / ( 3 * sizeof( Float ) );
				}
				else if( srcStream.m_type == MST_Index_U16 )
				{
					indexCount = srcStream.m_data.Size() / sizeof( Uint16 );
				}

				destChunk.m_streamMask |= srcStream.m_type;
			}

			// put data
			destChunk.m_firstVertex = currentVertexOffset;
			destChunk.m_numVertices = vertexCount;
			currentVertexOffset += vertexCount;													/// @todo DP: why???

			// copy indices
			destChunk.m_firstIndex = currentIndexOffset;
			destChunk.m_numIndices = indexCount;
			currentIndexOffset += indexCount;

			// source data
			destChunk.m_sourceMeshUUID = srcChunk.m_meshUUID;
			destChunk.m_sourceMaterialUUID = srcChunk.m_materialUUID;

			materials.PushBack( destChunk.m_materialID );
		}

		Bool rebuildMaterials = flushOptions.m_rebuildMaterials;

		// Create default appearance, if there is none.
		// If there are appearances (we are re-importing) we need to overwrite the default one and correct the rest (number of chunks).
		if ( outMesh.m_appearances.Empty() )
		{
			rebuildMaterials = true;               // This is an empty mesh, not imported before. We have to rebuild materials.
		}

		if ( !materials.Empty() )
		{
			if ( outMesh.m_appearances.Empty() )
			{
				outMesh.CreateDefaultAppearance();
			}
			else
			{
				THandle< MeshAppearance > oldDefaultAppearance = outMesh.GetDefaultAppearance();

				if ( !rebuildMaterials )
				{
					// We try to keep the old materials. But first we need to check if the number of chunks changed.
					Uint32 newNumChunks = outMesh.m_chunks.Size();
					if ( oldDefaultAppearance->m_chunkMaterials.Size() != newNumChunks )
					{
						red::ArraySpan< const THandle< MeshAppearance > > appearances = outMesh.GetAllAppearances();

						for ( const THandle< MeshAppearance >& appearance : appearances )
						{
							CorrectNumberOfChunksInAppearance( outMesh, appearance, chunksMapping, newMaterialsNames);
						}

						RED_LOG_WARNING( "The number of chunks changed since last import of this mesh!\r\n"
						                 "Previously imported mesh had:   %u chunks.\r\n"
						                 "Newly imported mesh has:        %u chunks.\r\n"
						                 "We made an attempt to correct it, but you need to check it.",
						                 oldDefaultAppearance->m_chunkMaterials.Size(), outMesh.m_chunks.Size() );
					}
				}

				if ( rebuildMaterials )
				{
					THandle< MeshAppearance > defaultAppearance( CreateHandle< MeshAppearance >( &outMesh ) );
					for ( Uint32 i = 0; i < outMesh.m_appearances.Size(); ++i )
					{
						if (outMesh.m_appearances[ i ] == oldDefaultAppearance)
						{
							outMesh.m_appearances[ i ] = defaultAppearance;
						}
						else
						{
							if ( outMesh.m_appearances[ i ]->m_chunkMaterials.Size() != outMesh.m_chunks.Size() )
							{
								CorrectNumberOfChunksInAppearance( outMesh, outMesh.m_appearances[ i ], chunksMapping, newMaterialsNames);

								RED_LOG_WARNING( "The number of chunks changed since last import, the appearance \"%hs\" needs correcting.\r\n"
								                 "Previously imported mesh had:   %u chunks.\r\n"
								                 "Newly imported mesh has:        %u chunks.\r\n"
								                 "We made an attempt to correct it, but you need to check it.",
								                 outMesh.m_appearances[ i ]->GetName().AsChar(), oldDefaultAppearance->m_chunkMaterials.Size(), outMesh.m_chunks.Size() );
							}
						}
					}
				}
			}
		}

		// compute new bounding box
		{
			if ( flushOptions.m_rebuildBBox )
			{
				outMesh.m_boundingBox.Clear();

				mesh::DataViewRO view( outMesh );

				for ( Uint32 i = 0; i < view.GetNumChunks(); ++i )
				{
					StreamIteratorRO< Vector3 > positions( view.GetChunkStreamData( i, MST_Position_3F ) );
					if ( !positions.IsValid() )
					{
						RED_LOG_ERROR("Failed to calculate bounding box for mesh chunk, because there is no valid position stream");
						continue;
					}
					for ( ; positions; ++positions )
					{
						outMesh.m_boundingBox.AddPoint( positions.Get() );
					}
				}
			}

			if ( flushOptions.m_extrudeBBox )
			{
				Vector4 expandSize = outMesh.m_boundingBox.CalcSize() * ( flushOptions.m_boundingBoxExtrusionFactor / 100.f );
				outMesh.m_boundingBox.Expand( expandSize );
			}
		}

		// compute new vertex epsilons
		{
			mesh::DataView view( outMesh );
			view.RecomputeVertexEpsilons();
		}
		
		outMesh.ComputeSurfaceAreaPerAxis();

		// recompile render resource blob
		if ( !tools::CreateMeshRenderResourceBlob( outMesh, PLATFORM_PC, flushOptions.m_verbose ) )
		{
			RED_LOG_ERROR( "Failed to create the mesh render resource blob" );
		}

		if ( flushOptions.m_uploadResource )
		{
			// get mesh to upload it
			outMesh.RecreateInternalResources( CMesh::MeshInternalData_All, false );
		}

		if (flushOptions.m_updateStreaming)
		{
			// notify streaming manager
			outMesh.NotifyStreamingDataChanged();
		}
	}

	void DataBuilder::CorrectNumberOfChunksInAppearance( const CMesh& mesh, const THandle<MeshAppearance>& appearance )
	{
		Uint32 newNumChunks = mesh.m_chunks.Size();
		if ( appearance->m_chunkMaterials.Size() > newNumChunks )
		{
			appearance->m_chunkMaterials.Resize( newNumChunks );        // Some chunks were removed in the new import data.
		}
		else if ( appearance->m_chunkMaterials.Size() < newNumChunks )
		{
			Uint32 oldNumChunks = appearance->m_chunkMaterials.Size();
			appearance->m_chunkMaterials.Resize( newNumChunks );        // Some chunks were added in the new import data.
			for ( Uint32 i = oldNumChunks; i < newNumChunks; ++i )
			{
				appearance->m_chunkMaterials[i] = appearance->m_chunkMaterials[oldNumChunks - 1];      // Just copy the last chunk material.
			}
		}
	}

	void DataBuilder::CorrectNumberOfChunksInAppearance( const CMesh& mesh, const THandle<MeshAppearance>& appearance, const red::DynArray<Int32>& chunksMapping, red::Map<Int32, CName>& newMaterialsNames)
	{
		Uint32 newNumChunks = mesh.m_chunks.Size();
		RED_FATAL_ASSERT( newNumChunks == chunksMapping.Size() );

		red::DynArray< CName > newChunkMaterials = { rend::PoolMesh() };
		newChunkMaterials.Resize( newNumChunks );

		for ( Uint32 chunkIndex = 0; chunkIndex < chunksMapping.Size(); ++chunkIndex )
		{
			Int32 mapping = chunksMapping[chunkIndex];
			if (mapping >= 0)
			{
				newChunkMaterials[chunkIndex] = appearance->m_chunkMaterials[static_cast<Uint32>(mapping)];
			}
			else
			{
				RED_FATAL_ASSERT( newMaterialsNames.KeyExist( chunkIndex ) );
				newChunkMaterials[ chunkIndex ] = newMaterialsNames[ chunkIndex ];
			}
		}

		appearance->m_chunkMaterials.Swap( newChunkMaterials );
	}

	Int32 DataBuilder::FindOldChunkByParams( const CMesh& oldMesh, const red::DynArray< Bool >& oldChunkMappedFlags, Uint8 newLODMask, Uint32 numNewIndices, Uint32 numNewVerts )
	{
		mesh::DataViewRO view( oldMesh );
		Int32 oldChunkMapping = -1;

		for ( Uint32 oldChunkIndex = 0; oldChunkIndex < oldMesh.m_chunks.Size(); ++oldChunkIndex )
		{
			if ( oldChunkMappedFlags[oldChunkIndex] )
				continue;

			Uint8 oldLODMask = view.GetChunkLodMask( oldChunkIndex );															/// @todo DP: store & use materialUUID in output meshes
			Uint32 numOldIndices = view.GetChunkIndexCount( oldChunkIndex );
			Uint32 numOldVerts = view.GetChunkVertexCount( oldChunkIndex );

			if ( (oldLODMask == newLODMask) && (numOldIndices == numNewIndices) && (numOldVerts == numNewVerts) )      // We just try to match number of vertices and indices.
			{
				oldChunkMapping = static_cast<Int32>(oldChunkIndex);
				break;                                      // We pick the first match.
			}
		}

		return oldChunkMapping;
	}

	Int32 DataBuilder::FindOldChunkByUUID( const CMesh& oldMesh, const red::DynArray< Bool >& oldChunkMappedFlags, const String& meshUUID, const String& materialUUID, const Uint8& lodMask )
	{
		mesh::DataViewRO view( oldMesh );

		String correctedMaterialUUID = materialUUID;
		/*
		This is not pretty. Please change this code if you've got better idea..
		On import existing materials are assigned to new chunks based on material UUID.
		A combination of mesh UUID and material UUID need to be unique so there cannot be two materials in content creation software (maya) that share the same ID used on one mesh.
		But artists want to be able to have two chunks with the same material assigned from one mesh. 
		So two chunks with the same meshID and different material ID need to know that they're using the same material.
		As the same ID of material cannot be used in both of them a suffix is assigned: Everything after "___" (3 * "_") in material ID is ignored during matching IDs on import.
		Start of hacky code:
		*/
		if (materialUUID.Contains("___"))
		{
			correctedMaterialUUID = materialUUID.StringBefore("___");
		}
		/*
		End OF hacky code.
		*/

		for ( Uint32 oldChunkIndex = 0; oldChunkIndex < oldMesh.m_chunks.Size(); ++oldChunkIndex )
		{
			if ( oldChunkMappedFlags[oldChunkIndex] )
				continue;

			if (view.GetChunkLodMask(oldChunkIndex) != lodMask)
				continue;

			const String& oldMeshUUID = view.GetChunkSourceMeshUUID( oldChunkIndex );
			const String& oldMaterialUUID = view.GetChunkSourceMaterialUUID( oldChunkIndex );

			String correctedOldMaterialUUID = oldMaterialUUID;
			/*
			The same story as earlier:
			Start of hacky code :
			*/
			if (oldMaterialUUID.Contains("___"))
			{
				correctedOldMaterialUUID = oldMaterialUUID.StringBefore("___");
			}
			/*
			End OF hacky code.
			*/

			if ( (oldMeshUUID == meshUUID) && (correctedOldMaterialUUID == correctedMaterialUUID) )
				return static_cast<Int32>(oldChunkIndex);
		}

		return -1;
	}

	Uint32 DataBuilder::MapNewChunksToOldChunks( red::DynArray<Int32>& chunksMapping, const CMesh& oldMesh, const red::DynArray< UnpackedChunk >& newChunks )
	{
		red::DynArray< Bool > oldChunkMappedFlags = { red::PoolBackend() };
		chunksMapping.Resize( newChunks.Size(), -1 );
		oldChunkMappedFlags.Resize( oldMesh.m_chunks.Size(), false );

		Uint32 numUnmappedChunks = 0;

		// First try matching by UUID, as exact and full proof matching type.
		for ( Uint32 newChunkIndex = 0; newChunkIndex < newChunks.Size(); ++newChunkIndex )
		{
			const UnpackedChunk& newChunk = newChunks[newChunkIndex];

			if ( !newChunk.m_meshUUID.Empty() && !newChunk.m_materialUUID.Empty() )
			{
				Int32 oldChunkMapping = FindOldChunkByUUID( oldMesh, oldChunkMappedFlags, newChunk.m_meshUUID, newChunk.m_materialUUID, newChunk.m_lodMask);
				if ( oldChunkMapping != -1 )
				{
					oldChunkMappedFlags[oldChunkMapping] = true;
					chunksMapping[newChunkIndex] = oldChunkMapping;
				}
			}
		}

		// Now scan all unmatched chunks and try less robust matching by num verts & tris etc.
		for ( Uint32 newChunkIndex = 0; newChunkIndex < newChunks.Size(); ++newChunkIndex )
		{
			const UnpackedChunk& newChunk = newChunks[newChunkIndex];
			Int32 oldChunkMapping = chunksMapping[newChunkIndex];
			if ( oldChunkMapping == -1 )                                                      // That matching failed, try matching by num verts & tris etc.
			{
				Uint8 newLODMask = newChunk.m_lodMask;
				Uint32 numNewIndices = 0;
				Uint32 numNewVerts = 0;

				for ( const UnpackedStream& newStream : newChunk.m_streams )
				{
					if ( newStream.m_type == MST_Index_U16 )
						numNewIndices = newStream.m_data.Size() / sizeof( Uint16 );
					else if ( newStream.m_type == MST_Position_3F )
						numNewVerts = newStream.m_data.Size() / sizeof( Vector3 );
				}

				oldChunkMapping = FindOldChunkByParams( oldMesh, oldChunkMappedFlags, newLODMask, numNewIndices, numNewVerts );
				if ( oldChunkMapping != -1 )
				{
					oldChunkMappedFlags[oldChunkMapping] = true;
					chunksMapping[newChunkIndex] = oldChunkMapping;
				}
				else
					++numUnmappedChunks;
			}
		}

		return numUnmappedChunks;
	}

	void DataBuilder::MapNewChunksToOtherNewChunks( red::DynArray<Int32>& chunksMapping, const red::DynArray< UnpackedChunk >& newChunks )
	{
		RED_FATAL_ASSERT( chunksMapping.Size() == newChunks.Size() );

		// Some chunks may be unmatched. This might mean they are new chunks added in this import, or they just might be changed.
		// Try to do some fallback matching for them.
		for ( Uint32 newChunkIndex = 0; newChunkIndex < newChunks.Size(); ++newChunkIndex )
		{
			if ( chunksMapping[newChunkIndex] == -1 )
			{
				for ( Uint32 i = 0; i < newChunks.Size(); ++i )
				{
					if ( newChunks[i].m_materialUUID == newChunks[newChunkIndex].m_materialUUID )
					{
						chunksMapping[newChunkIndex] = chunksMapping[i];												/// @todo DP: maybe we need to add second mapping array, for new chunks?
						break;
					}
				}
			}
		}
	}

	void DataBuilder::MapNewChunksFallback( red::DynArray<Int32>& chunksMapping, const red::DynArray< UnpackedChunk >& newChunks, red::Map<Int32, CName>& newMaterialsNames)
	{
		for ( Uint32 newChunkIndex = 0; newChunkIndex < newChunks.Size(); ++newChunkIndex )
		{
			if ( chunksMapping[newChunkIndex] == -1 )
			{
				newMaterialsNames[newChunkIndex] = newChunks[newChunkIndex].m_materials[0];
			}
		}
	}

} // mesh
