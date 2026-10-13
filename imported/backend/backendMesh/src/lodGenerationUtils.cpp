/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "lodGenerationUtils.h"

#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/resourceMesh/include/meshVertex.h"
#include "../../backendEngine/include/houdiniEngineUtils.h"

#include "meshDataBuilder.h"
#include "meshDataView.h"

namespace mesh
{
	const String LODGenerationUtils::s_hdaPath{ "internal\\lod_mesh_generator" };
	const LODGenerationUtils::HDAParamNames LODGenerationUtils::s_hdaParamNames{
		"fusedistance",
		"hardsplitangle",
		"materials_intact",
		"materials_o1",
		"materials_o2",
		"materials_o3",
		"mergecoplanar",
		"normals_autosmooth",
		"polycountlimit",
		"preservelod1",
		"preservenormals",
		"skinningtype",
		"splitchunk",
		"usehardsurfacemode",
		"uselod1skinning",
		"pushdiodes",
		"pushdiodesvalue"
	};
	
	Bool LODGenerationUtils::GenerateLODsHE( const GenerationInfo& generationInfo, houdini_legacy::HoudiniEngineSession* session, const CName& appearanceName )
	{
		if ( session == nullptr )
		{
			RED_LOG_ERROR( "Invalid Houdini Engine session. Unable to generate lod's." );
			return false;
		}

		if ( !generationInfo.mesh )
		{
			RED_LOG_ERROR( "Unable to generate lod's for mesh: %hs", generationInfo.mesh->GetPath().ToDebugString() );
			return false;
		}

		houdini_legacy::GeometryBuffer geometry;
		if ( !CookLODs( session, generationInfo.mesh->GetPath().ToString(), geometry, appearanceName ) )
		{
			RED_LOG_ERROR( "Unable to cook lod's for mesh: %hs", generationInfo.mesh->GetPath().ToDebugString() );
			return false;
		}

		DataBuilder buildData( *generationInfo.mesh );
		DataView meshDataView( *generationInfo.mesh );

		const Uint8 startingLod = ShouldSkipLOD1( session ) ? 1 : 0;

		red::DynArray< std::pair< red::String, red::String > > uuids;
		for( red::Uint32 i = 0; i < buildData.m_chunks.Size() ; i++ )
		{
			uuids.PushBack( std::make_pair( meshDataView.GetChunkSourceMaterialUUID( i ), meshDataView.GetChunkSourceMeshUUID( i ) ) );
		}

		// remove lods greater than 0 (or 1, if "Preserve LOD1" flag is selected)
		if ( buildData.m_lods.Size() > 1 )
		{
			red::SortedArray<Uint16> chunksToRemove;
			for ( Uint8 l = startingLod + 1; l < buildData.m_lods.Size(); ++l )
			{
				chunksToRemove.PushBack( meshDataView.GetLODChunkIndices( l ) );
			}

			chunksToRemove.Sort();
			for ( Uint16 ch : chunksToRemove.Reverse() )
			{
				buildData.m_chunks.RemoveAt( ch );
			}

			if(auto param = meshDataView.GetParameter<MeshParamCloth_Graphical>())
			{
				for(Uint16 ch : chunksToRemove.Reverse())
				{
					param->m_chunks.RemoveAt(ch);
				}
			}

			buildData.m_lods.RemoveAt( startingLod + 1, buildData.m_lods.Size() );

			DataBuilder::FlushOptions flushOptions;
			flushOptions.m_rebuildMaterials = false;
			flushOptions.m_uploadResource = !generationInfo.skipUploadRenderData;
			buildData.Flush( *generationInfo.mesh, flushOptions );
		}


		// pair map with new chunk number and original chunk number that was used to generate it,
		// some lods can have different number of chunks and wee need to figure out
		// how to map materials between them
		red::DynArray< std::pair<Uint16, Uint16> > chunkMap;

		const Uint8 lodsCount = geometry.m_lods.Size();

		// Calculate the visibility distance for each level
		const Float autoMaxDistance = generationInfo.mesh->GetAutoHideDistance();
		constexpr Float LOD_DISTRIBUTION_EXP = 1.8f;
		Float autoDistances[ 3 ] = { 0.f };

		for ( Uint8 lod = startingLod; lod < lodsCount; lod++ )
		{
			Float factor = Float( lod + 1 ) / Float( lodsCount + 1 );
			factor = MPow( factor, LOD_DISTRIBUTION_EXP );
			autoDistances[ lod ] = math::Lerp( factor, 0.f, autoMaxDistance );
		}

		// Import geometry, set parameters
		for ( Uint8 lod = startingLod; lod < lodsCount; lod++ )
		{
			DataBuilder::UnpackedLOD newLevel;

			if( lod < generationInfo.distances.Size() && generationInfo.distances[ lod ] != 0 )
			{
				newLevel.m_distance = generationInfo.distances[ lod ];
			}
			else
			{
				newLevel.m_distance = autoDistances[ lod ];
			}

			buildData.m_lods.PushBack( newLevel );

			for ( Uint8 c = 0; c < geometry.m_lods[lod].m_chunks.Size(); c++ )
			{
				DataBuilder::UnpackedChunk &newChunk = buildData.m_chunks.EmplaceBack();
				Uint16 current_chunk_id = buildData.m_chunks.Size() - 1;

				houdini_legacy::ChunkInfo chunk = geometry.m_lods[lod].m_chunks[c];
				chunkMap.PushBack( std::make_pair( current_chunk_id, chunk.m_originalChunkId ) );

				newChunk.m_materialUUID = uuids[ chunk.m_originalChunkId ].first;
				newChunk.m_meshUUID = uuids[ chunk.m_originalChunkId ].second;

				// adding LODs greater than 0
				newChunk.m_lodMask = 1 << ( lod + 1 );
				newChunk.m_vertexType = EMeshVertexType::MVT_StaticMesh;

				//standard streams
				newChunk.m_streams.Resize( 7 );
				newChunk.m_streams[ 0 ].m_type = MST_Index_U16;
				newChunk.m_streams[ 1 ].m_type = MST_Position_3F;
				newChunk.m_streams[ 2 ].m_type = MST_Normal_3F;
				newChunk.m_streams[ 3 ].m_type = MST_TexCoord0_2F;
				newChunk.m_streams[ 4 ].m_type = MST_Tangent_3F;
				newChunk.m_streams[ 5 ].m_type = MST_Binormal_3F;
				newChunk.m_streams[ 6 ].m_type = MST_Color_U32;

				const Bool hasSkinning = IsSkinnedMeshVertexType( chunk.m_chunkType );
				const Bool hasGarment = IsGarmentVertexType( chunk.m_chunkType );
				const Bool hasExtendedSkinning = !IsVectorStreamEmpty( geometry.m_ewts );
				const Bool hasVehicleDamage = !IsVectorStreamEmpty( geometry.m_vehicleDamagePosition );
				const Bool hasDestruction = !IsStreamEmpty( geometry.m_destruction );
				const Bool hasSimulation = !IsStreamEmpty( geometry.m_simulation );

				// Determine vertex type
				if ( hasSkinning )
				{
					const Bool isLightBlocker = false; // TODO: Support character light blocker streams
					newChunk.m_vertexType = SelectSkinnedVertexType(
						hasGarment,
						hasExtendedSkinning,
						isLightBlocker,
						hasVehicleDamage,
						hasDestruction
					);
				}
				else if ( hasVehicleDamage )
				{
					newChunk.m_vertexType = EMeshVertexType::MVT_StaticMeshVehicle;
				}
				else if ( hasDestruction )
				{
					newChunk.m_vertexType = EMeshVertexType::MVT_DestructibleMesh;
				}
				else
				{
					newChunk.m_vertexType = EMeshVertexType::MVT_StaticMesh;
				}

				// Prepare streams for writing
				if ( hasVehicleDamage )
				{
					newChunk.m_streams.EmplaceBack().m_type = MST_VehicleDmgPosFront_3F;
					newChunk.m_streams.EmplaceBack().m_type = MST_VehicleDmgNormalFront_3F;
				}
				
				if ( hasSkinning )
				{
					newChunk.m_streams.EmplaceBack().m_type = MST_SkinningIndices_4U8;
					newChunk.m_streams.EmplaceBack().m_type = MST_SkinningWeights_4F;

					if ( hasExtendedSkinning )
					{
						newChunk.m_streams.EmplaceBack().m_type = MST_SkinningIndicesExt_4U8;
						newChunk.m_streams.EmplaceBack().m_type = MST_SkinningWeightsExt_4F;
					}

					if ( hasGarment )
					{
						newChunk.m_streams.EmplaceBack().m_type = MST_MorphOffset_3F;
						newChunk.m_streams.EmplaceBack().m_type = MST_GarmentFlags_U32;
					}
				}
				
				if ( hasDestruction )
				{
					newChunk.m_streams.EmplaceBack().m_type = MST_DestructionIndices_2U16;
				}
				
				if ( hasSimulation && ( lod + 1 < static_cast< Int32 >( generationInfo.numClothLODs ) ) )
				{
					newChunk.m_vertexType = EMeshVertexType::MVT_StaticMesh;
				}

				newChunk.m_flags = meshDataView.UnpackFlags( chunk.m_flags );

				Uint32 chunk_start = chunk.m_start;
				Uint32 chunk_end = chunk.m_end;

				red::DynArray<Uint32> uniqueIdicies;
				red::DynArray<Uint32> chunkIndicies;
				red::DynArray<DebugVertex> chunkPositionsAndColors;
				red::DynArray<Vector3> chunkNormals;
				red::DynArray<Vector2> chunkUvs;
				red::DynArray<Vector3> chunkTangents;
				red::DynArray<Vector3> chunkBinormals;
				red::DynArray<Uint16> chunkSimulation;
				red::DynArray<Vector3> chunkVehicleDamagePosition;
				red::DynArray<Vector3> chunkVehicleDamageNormal;
				red::DynArray<Vector3> chunkVehicleDamagePositionSides;
				red::DynArray<Vector3> chunkVehicleDamageNormalSides;
				red::DynArray<Color> chunkWI;
				red::DynArray<Vector4> chunkWT;
				red::DynArray<Color> chunkEWI;
				red::DynArray<Vector4> chunkEWT;
				red::DynArray<Vector3> chunkGM;
				red::DynArray<Uint32> chunkGF;
				red::DynArray<Uint32> chunkDestro;

				Uint32 new_index = 0;
				for ( Uint32 i = chunk_start; i < chunk_end; ++i )
				{
					for ( Uint8 j = 0; j < 3; ++j )
					{
						Uint32 index = geometry.m_indices[i * 3 + j];
						if ( !uniqueIdicies.Exist( index ) )
						{
							uniqueIdicies.PushBack( index );
							chunkPositionsAndColors.PushBack( geometry.m_vertices[index] );
							chunkNormals.PushBack( geometry.m_normals[index] );
							chunkUvs.PushBack( geometry.m_uvs[index] );
							chunkTangents.PushBack( geometry.m_tangents[index] );
							chunkBinormals.PushBack( geometry.m_binormals[index] );

							if ( hasSkinning )
							{
								chunkWI.PushBack( geometry.m_wis[index] );
								chunkWT.PushBack( geometry.m_wts[index] );

								if ( hasExtendedSkinning )
								{
									chunkEWI.PushBack( geometry.m_ewis[index] );
									chunkEWT.PushBack( geometry.m_ewts[index] );
								}

								if ( hasGarment )
								{
									chunkGM.PushBack( geometry.m_garmentmorphs[index] );
									chunkGF.PushBack( geometry.m_garmentflags[index] );
								}
							}

							if ( hasDestruction )
							{
								chunkDestro.PushBack( geometry.m_destruction[index] );
							}

							if ( hasVehicleDamage )
							{
								chunkVehicleDamagePosition.PushBack( geometry.m_vehicleDamagePosition[ index ] );
								chunkVehicleDamageNormal.PushBack( geometry.m_vehicleDamageNormal[ index ] );
							}

							new_index = uniqueIdicies.Size() - 1;
						}
						else
						{
							new_index = uniqueIdicies.GetIndex( index );
						}
						chunkIndicies.PushBack( new_index );

						if (hasSimulation)
						{
							if (geometry.m_simulation[index] == 1 && !chunkSimulation.Exist(new_index)) chunkSimulation.PushBack(new_index);
						}
					}
				}

				red::HashMap< Uint32, UnpackedStream* > outStreamPointers;
				for( auto& stream : newChunk.m_streams )
				{
					outStreamPointers.Emplace( stream.m_type, &stream );
				}

				const Uint32 indiciesCount = ( chunk_end - chunk_start ) * 3;
				const Uint32 verticesCount = indiciesCount;
				const Uint32 numUniqueIndices = uniqueIdicies.Size();

				UnpackedStream* positionsStream;
				UnpackedStream* colorsStream;
				outStreamPointers.Find( MST_Position_3F, positionsStream );
				outStreamPointers.Find( MST_Color_U32, colorsStream );
				StreamWriter< Vector3 > positionsWriter( positionsStream->m_data, numUniqueIndices );
				StreamWriter< Uint32 > colorsWriter( colorsStream->m_data, numUniqueIndices );
				for ( const auto& pos : chunkPositionsAndColors )
				{
					positionsWriter.Write( Vector3( pos.x, pos.y, pos.z ) );
					
					const Color col = Color::FromVectorLinearAccu( pos.colorLinearSpace.ToVector4() );
					colorsWriter.Write( col.ToUint32() );
				}

				UnpackedStream* normalsStream;
				outStreamPointers.Find( MST_Normal_3F, normalsStream );
				StreamWriter< Vector3 > normalsWriter( normalsStream->m_data, numUniqueIndices );
				for ( const auto& normal : chunkNormals )
				{
					normalsWriter.Write( normal );
				}

				UnpackedStream* uvsStream;
				outStreamPointers.Find( MST_TexCoord0_2F, uvsStream );
				StreamWriter< Vector2 > uvsWriter( uvsStream->m_data, numUniqueIndices );
				for ( const auto& uv : chunkUvs )
				{
					uvsWriter.Write( uv );
				}

				UnpackedStream* tangentsStream;
				outStreamPointers.Find( MST_Tangent_3F, tangentsStream );
				StreamWriter< Vector3 > tangentsWriter( tangentsStream->m_data, numUniqueIndices );
				for ( const auto& tangent : chunkTangents )
				{
					tangentsWriter.Write( tangent );
				}

				UnpackedStream* binormalsStream;
				outStreamPointers.Find( MST_Binormal_3F, binormalsStream );
				StreamWriter< Vector3 > binormalsWriter( binormalsStream->m_data, numUniqueIndices );
				for ( const auto& binormal : chunkBinormals )
				{
					binormalsWriter.Write( binormal );
				}

				UnpackedStream* indicesStream;
				outStreamPointers.Find( MST_Index_U16, indicesStream );
				StreamWriter< Uint16 > indicesWriter( indicesStream->m_data, indiciesCount );
				for ( const auto& index : chunkIndicies )
				{
					indicesWriter.Write( index );
				}

				if ( hasSkinning )
				{
					UnpackedStream* weightIndexStream;
					UnpackedStream* weightValueStream;
					outStreamPointers.Find( MST_SkinningIndices_4U8, weightIndexStream );
					outStreamPointers.Find( MST_SkinningWeights_4F, weightValueStream );

					if ( weightIndexStream && weightValueStream )
					{
						StreamWriter< Uint8 > wiWriter( weightIndexStream->m_data, chunkWI.Size() * 4 );
						StreamWriter< Float > wtWriter( weightValueStream->m_data, chunkWT.Size() * 4 );

						for( const auto& wi : chunkWI )
						{
							wiWriter.Write( (Uint8)wi.R );
							wiWriter.Write( (Uint8)wi.G );
							wiWriter.Write( (Uint8)wi.B );
							wiWriter.Write( (Uint8)wi.A );
						}
						for( const auto& wt : chunkWT )
						{
							wtWriter.Write( (Float)wt.X );
							wtWriter.Write( (Float)wt.Y );
							wtWriter.Write( (Float)wt.Z );
							wtWriter.Write( (Float)wt.W );
						}
					}

					if ( hasExtendedSkinning )
					{
						UnpackedStream* extBoneIndexStream;
						UnpackedStream* extBoneWeightStream;
						outStreamPointers.Find( MST_SkinningIndicesExt_4U8, extBoneIndexStream );
						outStreamPointers.Find( MST_SkinningWeightsExt_4F, extBoneWeightStream );

						if ( extBoneIndexStream && extBoneWeightStream )
						{
							StreamWriter< Uint8 > ewiWriter( extBoneIndexStream->m_data, chunkEWI.Size() * 4 );
							StreamWriter< Float > ewtWriter( extBoneWeightStream->m_data, chunkEWT.Size() * 4 );

							for( const auto& ewi : chunkWI )
							{
								ewiWriter.Write( (Uint8)ewi.R );
								ewiWriter.Write( (Uint8)ewi.G );
								ewiWriter.Write( (Uint8)ewi.B );
								ewiWriter.Write( (Uint8)ewi.A );
							}
							for( const auto& ewt : chunkWT )
							{
								ewtWriter.Write( (Float)ewt.X );
								ewtWriter.Write( (Float)ewt.Y );
								ewtWriter.Write( (Float)ewt.Z );
								ewtWriter.Write( (Float)ewt.W );
							}
						}
					}

					if ( hasGarment )
					{
						UnpackedStream* garmentMorphStream;
						UnpackedStream* garmentFlagsStream;
						outStreamPointers.Find( MST_MorphOffset_3F, garmentMorphStream );
						outStreamPointers.Find( MST_GarmentFlags_U32, garmentFlagsStream );
						if ( garmentMorphStream && garmentFlagsStream )
						{
							StreamWriter< Vector3 > gmWriter( garmentMorphStream->m_data, chunkGM.Size() );
							for( const auto& offset : chunkGM )
							{
								gmWriter.Write( offset );
							}

							StreamWriter< Uint32 > gfWriter( garmentFlagsStream->m_data, chunkGF.Size() );
							for( const auto& flag : chunkGF )
							{
								gfWriter.Write( flag );
							}
						}
					}
				}

				if ( hasDestruction )
				{
					UnpackedStream* destroStream;
					outStreamPointers.Find( MST_DestructionIndices_2U16, destroStream );
					if ( destroStream )
					{
						StreamWriter< Uint32 > destroWriter( destroStream->m_data, chunkDestro.Size() );
						for( const auto& dstr : chunkDestro )
						{
							destroWriter.Write( dstr );
						}
					}
				}

				if ( hasSimulation )
				{
					THandle<MeshParamCloth_Graphical> param = meshDataView.GetParameter<MeshParamCloth_Graphical>();
					auto& chunk = param->m_chunks.EmplaceBack();
					chunk.m_simulation.PushBack( chunkSimulation );
				}

				if ( hasVehicleDamage )
				{
					UnpackedStream* vehDmgPosStream;
					UnpackedStream* vehDmgNormalStream;
					outStreamPointers.Find( MST_VehicleDmgPosFront_3F, vehDmgPosStream );
					outStreamPointers.Find( MST_VehicleDmgNormalFront_3F, vehDmgNormalStream );

					if ( vehDmgPosStream && vehDmgNormalStream )
					{
						StreamWriter< Vector3 > vehDmgPosWriter( vehDmgPosStream->m_data, chunkVehicleDamagePosition.Size() );
						for( const auto& val : chunkVehicleDamagePosition )
						{
							vehDmgPosWriter.Write( val );
						}

						StreamWriter< Vector3 > vehDmgNormalWriter( vehDmgNormalStream->m_data, chunkVehicleDamageNormal.Size() );
						for( const auto& val : chunkVehicleDamageNormal )
						{
							vehDmgNormalWriter.Write( val );
						}
					}
				}
			}
		}

		DataBuilder::FlushOptions flushOptions;
		flushOptions.m_rebuildMaterials = false;
		flushOptions.m_uploadResource = !generationInfo.skipUploadRenderData;
		buildData.Flush( *generationInfo.mesh, flushOptions );

		// cloth
		if( auto param = generationInfo.mesh->GetParameter<MeshParamCloth_Graphical>() )
		{
			DataView view(*generationInfo.mesh);
			param->m_lodChunkIndices.Clear();

			for( LODIndex i = 0; i < view.GetNumLODs(); ++i )
            {
                auto lodChunkIndices = view.GetLODChunkIndices(i);
                param->m_lodChunkIndices.EmplaceBack(lodChunkIndices);

				for( auto idx : lodChunkIndices )
				{
					auto positionsStream = MakeSpan<Vector3>(view,idx,MST_Position_3F);
					auto indicesStream = MakeSpan<Uint16>(view,idx,MST_Index_U16);
					auto skinWeightsStream = MakeSpan<Vector4>(view,idx,MST_SkinningWeights_4F);
					auto skinIndicesStream = MakeSpan<Uint32>(view,idx,MST_SkinningIndices_4U8);
					auto skinWeightsExtStream = MakeSpan<Vector4>(view,idx,MST_SkinningWeightsExt_4F);
					auto skinIndicesExtStream = MakeSpan<Uint32>(view,idx,MST_SkinningIndicesExt_4U8);

					auto& chunk = param->m_chunks[idx];

					chunk.m_positions = DataBuffer::Copy(positionsStream.Data(), positionsStream.SizeInBytes(), 4);
					chunk.m_indices = DataBuffer::Copy(indicesStream.Data(), indicesStream.SizeInBytes(), 4);
					chunk.m_skinWeights = DataBuffer::Copy(skinWeightsStream.Data(), skinWeightsStream.SizeInBytes(), 16);
					chunk.m_skinIndices = DataBuffer::Copy(skinIndicesStream.Data(), skinIndicesStream.SizeInBytes(), 4);

					if(!skinWeightsExtStream.Empty() && !skinIndicesExtStream.Empty())
					{
						chunk.m_skinWeightsExt = DataBuffer::Copy(skinWeightsExtStream.Data(), skinWeightsExtStream.SizeInBytes(), 16);
						chunk.m_skinIndicesExt = DataBuffer::Copy(skinIndicesExtStream.Data(), skinIndicesExtStream.SizeInBytes(), 4);
					}
				}
            }
		}

		// copy materials from original chunks to corresponding chunks in new lod's
		// do it for each appearance
		for ( Uint32 a = 0; a < meshDataView.GetNumAppearances(); ++a )
		{
			const THandle< MeshAppearance >& appearance = meshDataView.GetAppearanceData( a );
			for ( Uint16 c = 0; c < chunkMap.Size(); ++c )
			{
				// get material from original chunk
				CName material = appearance->GetChunkMaterialName( chunkMap[c].second );
				meshDataView.SetChunkMaterial( material, chunkMap[c].first, a );
			}
		}

		//not sure about these two
		meshDataView.ForceRecreateRenderResource();
		meshDataView.ForceRecreateBlob();
		meshDataView.ForceRecalculateGeometryHash();

		meshDataView.MarkLODAsGenerated();

		//apply
		meshDataView.Apply();

		const THandle<MeshParamDestructionBoneChunkMapping>& meshChunkMapParam = generationInfo.mesh->GetParameter<MeshParamDestructionBoneChunkMapping>();
		if ( meshChunkMapParam )
		{
			meshChunkMapParam->OnMeshChanged( *generationInfo.mesh );
		}

		return true;
	}

	Bool LODGenerationUtils::ShouldSkipLOD1( houdini_legacy::HoudiniEngineSession* session )
	{
		THandle<houdini_legacy::IHDAInterfaceWrapper> wrapper = session->GetInterfaceWrapper();
		if ( wrapper == nullptr )
		{
			return false;
		}

		String value = wrapper->GetPropertyValue( RED_NAME_CONSTEXPR( "preservelod1" ) );
		return value.EqualsNC( "true" );
	}

	Bool LODGenerationUtils::IsStreamEmpty( const red::DynArray< Uint32 >& values )
	{
		return red::alg::NoneOf( values, []( auto val )
		{ 
			return val != 0u; }
		);
	}
} // mesh
