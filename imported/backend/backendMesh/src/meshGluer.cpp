#include "build.h"
#include "meshGluer.h"
#include "meshDataBuilder.h"
#include "meshDataView.h"

#include "../../../common/redContainers/include/dynArray.h"
#include "../../../common/redContainers/include/string/stringBuilder.h"
#include "../../../common/engine/include/physicsDecorationResource.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"

#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemJoint.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/redReflection/include/cloningUtils.h"
#include "../../../common/redReflection/include/mathUtils.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/redContainers/include/algorithms.h"

#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/visibility/include/visibilityPublic.h"
#include "../../../common/visibility/include/visRasterizerSSEFlat.h"
#include "../../../common/visibility/include/visCamera.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"

#include <random>
#include <chrono>
#include "../../../common/redJobs2/include/jobBuilder.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"

using namespace mesh;

namespace mesh
{
	struct Bone
	{
		RED_USE_MEMORY_POOL(red::PoolBackend);

		Matrix									m_matrix;
		CName									m_name;
		Float									m_vertexEpsilon;
	};

	struct Chunk
	{
		RED_USE_MEMORY_POOL(red::PoolBackend);

		ChunkFlags								m_flags;
		Uint32									m_indexCount;
		Uint8									m_lodMask;
		red::DynArray<CName>					m_materials{ red::PoolBackend() };
		red::DynArray<ChunkStreamDataRO>		m_streams{ red::PoolBackend() };
		Uint64									m_streamMask;
		Uint32									m_vertexCount;
		EMeshVertexType							m_vertexType;
		std::pair< red::String, red::String >	m_chunkUUID;
	};

	typedef red::DynArray< THandle< MeshParameter > > TParameters;
	typedef red::DynArray< THandle< physics::ISystemObject > > TSystemObjects;

	struct BodyInfo
	{
		CName m_boneName;
		Matrix m_mappedBoneToBody;
	};

	struct MeshParts
	{
		RED_USE_MEMORY_POOL(red::PoolBackend);

		red::DynArray<Chunk>					m_chunks{ red::PoolBackend() };
		red::DynArray< red::DynArray<BodyInfo> > m_bodyInfos{ red::PoolBackend() };
		red::DynArray<red::DynArray<Bone>>		m_bones  = { red::PoolBackend() };
		red::DynArray<Uint32>					m_chunkToMesh{ red::PoolBackend() };
		red::DynArray<Uint32>					m_meshBoneOffset{ red::PoolBackend() }; // an offset we need to move bone indexes of that mesh

		red::DynArray< TSystemObjects > m_physicsObjects = { red::PoolBackend() };
		red::DynArray< TParameters > m_parameters = { red::PoolBackend() };
		red::DynArray< Matrix > m_transform{ red::PoolBackend() };

		red::DynArray< THandle< IMaterial > >	m_materials{ red::PoolBackend() };
		red::DynArray<CName>					m_materialNames{ red::PoolBackend() };
		red::DynArray<CName>					m_appearanceMaterials{ red::PoolBackend() };

		red::DynArray<Float>					m_lods{ red::PoolBackend() };
	};

	struct ChunkCluster
	{
		RED_USE_MEMORY_POOL(red::PoolBackend);

		red::DynArray<CName>					m_materials{ red::PoolBackend() };
		red::DynArray<Chunk *>					m_chunks{ red::PoolBackend() };
		ChunkFlags								m_flags;
		Uint32									m_vertexCount;
		Uint32									m_streamsCount;
		Uint64									m_streamsMask;
		Uint8									m_lodMask;
		red::DynArray<Uint32>					m_chunkToMesh{ red::PoolBackend() };
		Bool									m_isTransparent;
		std::pair< red::String, red::String >	m_chunkUUID;

		ChunkCluster()
			: m_vertexCount( 0 )
			, m_isTransparent( false )
		{}
	};

	Bool CompareChunkFlags( const ChunkFlags& a, const ChunkFlags& b )
	{
		return a.m_renderInScene == b.m_renderInScene
			&& a.m_renderInShadows == b.m_renderInShadows
			&& a.m_isTwoSided == b.m_isTwoSided
			&& a.m_isRayTracedEmissive == b.m_isRayTracedEmissive;
	}
}

namespace
{
	// Cluster mesh chunks into groups that can be merged. Complexity is O(n^2).
	static void ClusterChunks(MeshPartsPtr meshParts, red::DynArray<ChunkCluster>& chunkClusters )
	{
		const Uint32 maxVertexCount = 65536;
		for( Uint32 chunkIndex : meshParts->m_chunks.Indices() )
		{
			auto& chunk = meshParts->m_chunks[chunkIndex];
			ChunkCluster* matchingCluster = nullptr;

			// Disabling clustering for now - breaks appearances
			for( auto& chunkCluster : chunkClusters )
			{			
				if( (chunkCluster.m_vertexCount + chunk.m_vertexCount > maxVertexCount)
					|| (chunk.m_lodMask != chunkCluster.m_lodMask)
					|| (chunk.m_streamMask != chunkCluster.m_streamsMask)
					|| (chunk.m_streams.Size() != chunkCluster.m_streamsCount)
					|| !CompareChunkFlags( chunk.m_flags, chunkCluster.m_flags )
					|| chunkCluster.m_materials[0] != meshParts->m_appearanceMaterials[chunkIndex] )
				{
					continue;
				}
				matchingCluster = &chunkCluster;				
			}

			if( matchingCluster )
			{
				// Add the chunk to matching cluster
				matchingCluster->m_chunks.PushBack( &chunk );
				matchingCluster->m_vertexCount += chunk.m_vertexCount;
				// remove material from appearance materials
				meshParts->m_appearanceMaterials[chunkIndex] = CName::NONE();
			}
			else
			{
				// No matching clusters, we need to create one
				ChunkCluster chunkCluster;
				chunkCluster.m_chunks.PushBack( &chunk );
				CName matName = meshParts->m_appearanceMaterials[ chunkIndex ];
				chunkCluster.m_materials.PushBack(matName);
				chunkCluster.m_vertexCount = chunk.m_vertexCount;
				chunkCluster.m_lodMask = chunk.m_lodMask;
				chunkCluster.m_streamsCount = chunk.m_streams.Size();
				chunkCluster.m_streamsMask = chunk.m_streamMask;
				chunkCluster.m_flags = chunk.m_flags;
				chunkCluster.m_chunkUUID = chunk.m_chunkUUID;

				const Uint32 numMats = meshParts->m_materials.Size();
				for( Uint32 i = 0; i < numMats; ++i )
				{
					if( meshParts->m_materialNames[i] == matName )
					{
						THandle< IMaterial >& materialHandle = meshParts->m_materials[ i ];

						if( const IMaterialDefinition* materialDefinition = materialHandle->GetMaterialDefinition() )
						{
							for( Uint32 i = 0; i < materialDefinition->GetNumTechniques(); ++i )
							{
								EngineStagePassDataCollection passData;
								materialDefinition->GetPasses(i, passData);
								for( const auto& passDataEntry : passData )
								{
									for( Uint32 rT = 0; rT < passDataEntry.m_omState.m_blendModeDesc.m_numTargets; ++rT )
									{
										auto& renderTarget = passDataEntry.m_omState.m_blendModeDesc.m_renderTarget[ rT ];

										if( renderTarget.m_blendEnable )
										{
											chunkCluster.m_isTransparent = true;
											break;
										}
									}
									if( chunkCluster.m_isTransparent )
										break;
								}
								if( chunkCluster.m_isTransparent )
									break;
							}
						}
						if( chunkCluster.m_isTransparent )
						{
							break;
						}
					}
				}
				chunkClusters.PushBack(chunkCluster);
			}
		}
	}
	
	// Creates streams with proper size in new DataBuilder chunk based on chunks in a cluster
	static Uint32 PrepareChunkStreamsFromCluster( ChunkCluster& cluster, DataBuilder::UnpackedChunk& unpackedChunk )
	{
		Uint32 vertexCount = 0;
		Uint32 indexCount = 0;
		EMeshStreamType streamTypes[ MAX_MESH_STREAMS ];
		for( Uint32 i = 0; i < MAX_MESH_STREAMS; ++i )
		{
			streamTypes[ i ] = MST_None;
		}

		for( auto chunk : cluster.m_chunks )
		{
			RED_FATAL_ASSERT( cluster.m_streamsCount == chunk->m_streams.Size(), "Number of streams declared by mask has to be equal to the number of actual streams" );
			RED_FATAL_ASSERT( cluster.m_streamsMask == chunk->m_streamMask, "Chunks from the same cluster have to have the same stream mask" );

			for( Uint32 i = 0; i < chunk->m_streams.Size(); ++i )
			{
				if( streamTypes[ i ] == MST_None )
				{
					streamTypes[ i ] = chunk->m_streams[ i ].m_stream;
				}
				else
				{
					RED_FATAL_ASSERT( streamTypes[ i ] == chunk->m_streams[ i ].m_stream, "Stream type has to match other chunks" );
				}
			}
			vertexCount += chunk->m_vertexCount;
			indexCount += chunk->m_indexCount;
		}

		unpackedChunk.m_streams.Resize( cluster.m_streamsCount );
		for( Uint32 streamIndex = 0; streamIndex < cluster.m_streamsCount; ++streamIndex )
		{
			auto& stream = unpackedChunk.m_streams[ streamIndex ];
			stream.m_type = streamTypes[ streamIndex ];
			Uint32 stride = MeshPackerUtils::GetMeshStreamSize( stream.m_type );
			if( stream.m_type == MST_Index_U16 )
			{
				stream.m_data = DataBuffer( stride * indexCount );
			}
			else if( stream.m_type == MST_SkinningIndices_4U8 || stream.m_type == MST_SkinningWeights_4F )
			{
				stream.m_data = DataBuffer( stride * vertexCount );
			}
			else
			{
				stream.m_data = DataBuffer( stride * vertexCount );
			}
		}
		return cluster.m_streamsCount;
	}

	// Copies stream data from cluster chunks to DataBuilder chunk
	static void CopyStreamDataFromCluster( ChunkCluster& cluster, DataBuilder::UnpackedChunk& chunk, Uint32 streamCount )
	{
		Uint32 vertexOffset = 0;
		Uint32 indexOffset = 0;
		Uint32 chunkInd = 0;
		for( auto& sourceChunk : cluster.m_chunks )
		{
			for( Uint32 streamIndex = 0; streamIndex < streamCount; ++streamIndex )
			{
				auto& sourceStream = sourceChunk->m_streams[ streamIndex ];
				auto& destinationStream = chunk.m_streams[ streamIndex ];

				RED_FATAL_ASSERT( sourceStream.m_stream == destinationStream.m_type, "Stream types must be matching" );

				Uint32 stride = sourceStream.m_stride;
				Uint32 size = stride * sourceStream.m_count;
				Uint32 offset = stride * ( ( sourceStream.m_stream == MST_Index_U16 ) ? indexOffset : vertexOffset );

				const void* sourceData = sourceStream.m_data;

				void* destinationData = static_cast<Uint8 *>( destinationStream.m_data.Data() ) + offset;

				red::Memcpy( destinationData, sourceData, size );

				// Adjust indices to correctly point to coalesced vertex arrays
				if( sourceStream.m_stream == MST_Index_U16 )
				{
					Uint16* indices = (Uint16 *)destinationData;
					Uint32 indexCount = sourceChunk->m_indexCount;

					for( Uint32 i = 0; i < indexCount; ++i )
					{
						indices[ i ] += vertexOffset;
					}
				}
			}
			vertexOffset += sourceChunk->m_vertexCount;
			indexOffset += sourceChunk->m_indexCount;
			chunkInd++;
		}
	}

	// Construct a new bone name
	static CName RemapBoneName( const CName& boneName, Uint32 meshIndex )
	{
		red::StringBuilder<String> str;
		str.Appendf( "m%d_%hs", meshIndex, boneName.AsChar() );
		String remappedName = str.ToString();
		return RED_NAME( remappedName );
	}

	// Offset bone indices so that vertices in coalesced chunks point to correct positions
	// in bone array
	static void ReindexBones( MeshPartsPtr meshParts )
	{
		for( Uint32 chunkIndex = 0; chunkIndex < meshParts->m_chunks.Size(); ++chunkIndex )
		{
			Chunk& chunk = meshParts->m_chunks[ chunkIndex ];

			// There is no need to re-index static chunks
			if( chunk.m_vertexType != MVT_SkinnedMesh )
				continue;

			// Find bone index stream
			ChunkStreamDataRO* stream = mesh::utils::FindStream( chunk.m_streams, MST_SkinningIndices_4U8 );
			if( !stream )
			{
				RED_FATAL( "Chunk declares skinning support but bone index stream is missing" );
				return;
			}

			Uint32 meshIndex = meshParts->m_chunkToMesh[ chunkIndex ];

			Uint8* skinningIndices = (Uint8 *)stream->m_data;
			Uint32 skinningIndicesCount = 4 * stream->m_count;

			Uint32 boneOffset = meshParts->m_meshBoneOffset[ meshIndex ];

			// Offset bone indices 
			for( Uint32 i = 0; i < skinningIndicesCount; i += 4 )
			{
				skinningIndices[ i + 0 ] += boneOffset;
				skinningIndices[ i + 1 ] += boneOffset;
				skinningIndices[ i + 2 ] += boneOffset;
				skinningIndices[ i + 3 ] += boneOffset;
			}
		}
	}

	// Create a new bone array, that consists of coalesced bones from multiple input chunks
	static void RemapBones(MeshPartsPtr meshParts, DataBuilder& builder )
	{
		for( Uint32 meshIndex = 0; meshIndex < meshParts->m_bones.Size(); ++meshIndex )
		{
			// Select bone list for current mesh
			auto& boneCluster = meshParts->m_bones[ meshIndex ];
			for( Uint32 boneIndex = 0; boneIndex < boneCluster.Size(); ++boneIndex )
			{
				Bone& bone = boneCluster[ boneIndex ];

				// Set new name for created bone
				CName remappedName = RemapBoneName( bone.m_name, meshIndex );

				// Add bone to list
				DataBuilder::UnpackedBone newBone;
				newBone.m_name = remappedName;
				newBone.m_rig = bone.m_matrix;
				builder.m_bones.PushBack( newBone );
			}
		}
	}

	static void RemapPhysicsBindings( const THandle<physics::SystemBody>& sysBody, CName name, const Matrix& transform = Matrix::IDENTITY(), const Matrix& boneMat = Matrix::IDENTITY() )
	{	
		sysBody->m_mappedBoneName = name;
		
		Transform meshTransform = transform.ToXform();
		Transform boneRig = boneMat.ToXform();
		Transform bonePosL2W = boneRig.GetInverse();

		sysBody->m_mappedBoneToBody = bonePosL2W.GetInverse();
		sysBody->m_localToModel = sysBody->m_localToModel * meshTransform;
	}

	static void MergePhysicsSystemResources( const THandle< physics::SystemResource >& output,MeshPartsPtr meshParts, Uint32 meshIndex, physics::SystemBodyBuildData& staticSystemBodyInfo, Bool skinningDeleted )
	{
		red::DynArray< mesh::Bone >& boneCluster = meshParts->m_bones[ meshIndex ];
		Matrix& transform = meshParts->m_transform[ meshIndex ];
		red::DynArray< THandle< physics::ISystemObject > >& objects = meshParts->m_physicsObjects[ meshIndex ];
		Int32 size = objects.Size();

		for( Int32 i = 0; i < size; ++i )
		{
			THandle< physics::ISystemObject >& physObj = objects[ i ];
			Bool attachObj = true;
			if( THandle<physics::SystemBody> sysBody = Cast<physics::SystemBody>( physObj ) )
			{
				CName name = sysBody->m_mappedBoneName;
				Matrix boneMatrix = Matrix::IDENTITY();
				if( boneCluster.Size() )
				{
					auto iter = std::find_if( boneCluster.Begin(), boneCluster.End(), [name]( const mesh::Bone& bone )
					{
						return bone.m_name == name;
					} );

					if( iter == boneCluster.End() )
					{
						RED_LOG_WARNING( "SystemBody mapped bone is not in the cluster! Searched bone name: %hs", name.AsChar() );
						continue;
					}
					boneMatrix = (*iter).m_matrix;
				}

				if( skinningDeleted )
				{
					attachObj = false;
					Transform meshTransform = transform.ToXform() * sysBody->m_localToModel;
					Transform boneRigInv = boneMatrix.ToXform().GetInverse();
					Transform bodyL2w = boneRigInv * meshTransform;

					for( const THandle<physics::ICollider>& collisionItem : sysBody->m_collisionShapes )
					{
						collisionItem->m_localToBody = (collisionItem->m_localToBody * bodyL2w);						
						staticSystemBodyInfo.m_collisionShapes.PushBack( collisionItem );
					}
				}
				else
				{
					RemapPhysicsBindings( sysBody, RemapBoneName( name, meshIndex ), transform, boneMatrix );
				}
			}
			if( attachObj )
				output->AttachObject( physObj );
		}
	}

	typedef THandle< physics::SystemResource > TSysRes;
	static void MergeParameters(MeshPartsPtr meshParts, CMesh* output, const MeshGluerInput& input )
	{
		//TODO: merge other parameters - how to handle that? Generic or case by case like with physics

		// Merge physics systems
		TSysRes mergedSystem = nullptr;
		physics::SystemBodyBuildData staticSystemBodyInfo;
		staticSystemBodyInfo.m_name = RED_NAME_CONSTEXPR( "Imported collisions" );
		THandle<physics::SystemBody> notskinnedBody = nullptr;
		for( Uint32 meshIndex = 0; meshIndex < meshParts->m_bones.Size(); ++meshIndex )
		{
			if( mergedSystem == nullptr )
			{
				mergedSystem = CreateHandle< physics::SystemResource >();
			}
			MergePhysicsSystemResources( mergedSystem, meshParts, meshIndex, staticSystemBodyInfo, input.m_deleteSkinning );
		}
		if( staticSystemBodyInfo.m_collisionShapes.Size() > 0 )
		{
			staticSystemBodyInfo.m_params.m_simulationType = physics::SimulationType::Static;
			auto body = physics::SystemBody::Create( staticSystemBodyInfo );
			if( body )
				mergedSystem->AttachObject( body );
		}

		if( mergedSystem )
		{
			auto meshPhysics = output->CreateParameter< mesh::MeshParamPhysics >();
			if( meshPhysics )
			{
				meshPhysics->SetPhysicsData( mergedSystem );
			}
		}		
	}
}

Bool MeshGluer::Process( const MeshGluerInput& input, CMesh* output, job::Builder& builder)
{
	if( !VerifyInput(input) )
	{
		return false;
	}

	MeshPartsPtr meshParts = red::CreateSharedPtr<MeshParts>();
	meshParts->m_bones.Clear();
	meshParts->m_bodyInfos.Clear();
	meshParts->m_parameters.Clear();
	meshParts->m_physicsObjects.Clear();

	// Allocate bone mapping table
	meshParts->m_meshBoneOffset.Resize(input.m_meshes.Size());

	// Break down input meshes
	DisassembleMeshes(input.m_meshes, meshParts, input.m_lodToMerge);

	if( !VerifyMeshParts(meshParts) )
	{
		return false;
	}

	if( input.m_transform )
	{
		TransformChunks(input.m_meshes, meshParts);
	}

	if( input.m_deleteSkinning )
	{
		DeleteSkinning(input.m_meshes, meshParts);
	}
	else if( input.m_generateSkinning )
	{
		GenerateSkinning(input.m_meshes, meshParts);
	}

	AssembleMesh( meshParts, output, input, builder);
	return true;
}

Bool MeshGluer::Process_Sync(const MeshGluerInput& input, CMesh* output)
{
	if( !VerifyInput(input) )
	{
		return false;
	}
	RED_ASSERT(!input.m_optimizeMesh, "Mesh optimization is only available in async mode.");

	MeshPartsPtr meshParts = red::CreateSharedPtr<MeshParts>();
	meshParts->m_bones.Clear();
	meshParts->m_bodyInfos.Clear();
	meshParts->m_parameters.Clear();
	meshParts->m_physicsObjects.Clear();
	// Allocate bone mapping table
	meshParts->m_meshBoneOffset.Resize(input.m_meshes.Size());

	// Break down input meshes
	DisassembleMeshes(input.m_meshes, meshParts, input.m_lodToMerge );

	if( !VerifyMeshParts(meshParts) )
	{
		return false;
	}

	if( input.m_deleteSkinning )
	{
		DeleteSkinning(input.m_meshes, meshParts);
	}
	else if( input.m_generateSkinning )
	{
		GenerateSkinning(input.m_meshes, meshParts);
	}

	if( input.m_transform )
	{
		TransformChunks( input.m_meshes, meshParts );
	}

	AssembleMesh_Sync(meshParts, output, input);
	return true;
}

Bool MeshGluer::VerifyInput( const MeshGluerInput& input )
{
	if( input.m_transform )
	{
		for( auto& mesh : input.m_meshes )
		{
			if( !mesh.m_transform.IsOk() )
			{
				RED_LOG_ERROR( "Invalid transform passed to mesh merger" );
				return false;
			}
		}
	}

	if( input.m_meshes.Empty() )
	{
		return false;
	}

	return true;
}

Bool MeshGluer::VerifyMeshParts(MeshPartsPtr meshParts )
{
	Uint32 bonesCount = 0;
	for( const auto& boneArray : meshParts->m_bones )
	{
		bonesCount += boneArray.Size();
		if( bonesCount > 255 )
		{
			RED_LOG_ERROR( "Resulting mesh would have too many bones to index with 8-bit bone indices" );
			return false;
		}
	}

	return true;
}

void MeshGluer::DisassembleMeshes( const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts, Int32 lodToMerge )
{
	red::DynArray< const rtti::ClassType* > meshParamClasses;
	GetRttiSystem().EnumClasses( ClassID< mesh::MeshParameter >(), meshParamClasses );

	for( Uint32 meshIndex = 0; meshIndex < meshes.Size(); ++meshIndex )
	{
		const MeshInput* mesh = &meshes[ meshIndex ];

		DisassembleChunks( mesh, meshIndex, meshParts, lodToMerge );
		DisassembleLODs( mesh, meshIndex, meshParts, lodToMerge );
		DisassembleBones( mesh, meshIndex, meshParts );

		// parameters
		DisassembleParameters( mesh, meshParamClasses, meshParts );

		// materials
		DisassembleMaterials( mesh, meshParts, lodToMerge );
	}
}

void MeshGluer::DisassembleChunks( const MeshInput* mesh, Uint32 meshIndex, MeshPartsPtr meshParts, Int32 lodToMerge )
{
	const DataViewRO view( *mesh->m_mesh );

	Uint32 chunkCount = view.GetNumChunks();

	for( Uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
	{
		Int8 lodMask = view.GetChunkLodMask( chunkIndex );
		if( lodToMerge != -1 && lodMask != lodToMerge )
			continue;

		Chunk& chunk = meshParts->m_chunks.EmplaceBack();
		chunk.m_flags = view.GetChunkFlags( chunkIndex );
		chunk.m_indexCount = view.GetChunkIndexCount( chunkIndex );
		chunk.m_lodMask = view.GetChunkLodMask( chunkIndex );
		chunk.m_streamMask = view.GetChunkStreamMask( chunkIndex );
		chunk.m_chunkUUID = std::make_pair( view.GetChunkSourceMaterialUUID( chunkIndex ), view.GetChunkSourceMeshUUID( chunkIndex ) );

		for( Uint32 streamIndex = 0; streamIndex < MAX_MESH_STREAMS; ++streamIndex )
		{
			Uint32 streamMask = RED_FLAG( streamIndex );
			if( streamMask & chunk.m_streamMask )
			{
				EMeshStreamType streamType = (EMeshStreamType)streamMask;
				ChunkStreamDataRO originalData = view.GetChunkStreamData( chunkIndex, streamType );
				ChunkStreamDataRO copiedData;
				copiedData.m_stream = originalData.m_stream;
				Uint32 size = originalData.m_stride * originalData.m_count;

				copiedData.m_buffer = serialization::BufferHandleRO( originalData.m_buffer.Data(), originalData.m_buffer.GetSize(), originalData.m_buffer.GetAlignment(), red::PoolBackend() );
				copiedData.m_data = copiedData.m_buffer.Data();
				copiedData.m_stride = originalData.m_stride;
				copiedData.m_count = originalData.m_count;

				chunk.m_streams.PushBack( copiedData );
			}
		}

		chunk.m_vertexCount = view.GetChunkVertexCount( chunkIndex );
		chunk.m_vertexType = view.GetChunkVertexType( chunkIndex );

		meshParts->m_chunkToMesh.PushBack( meshIndex );
	}
}

void MeshGluer::DisassembleLODs( const MeshInput* mesh, Uint32 meshIndex, MeshPartsPtr meshParts, Int32 lodToMerge )
{
	const DataViewRO view( *mesh->m_mesh );

	Uint8 lodCount = view.GetNumLODs();
	if( lodToMerge == -1 )
	{
		Uint32 currentSize = meshParts->m_lods.Size();
		if( currentSize < lodCount )
		{
			meshParts->m_lods.Resize( lodCount );
			for( Uint32 i = currentSize; i < lodCount; ++i )
			{
				meshParts->m_lods[ i ] = RED_FLT_MAX;
			}
		}

		for( Uint32 i = 0; i < lodCount; ++i )
		{
			meshParts->m_lods[ i ] = std::min( meshParts->m_lods[ i ], view.GetLODDistance( i ) );
		}
	}
	else if( lodCount > (Uint8)lodToMerge )
	{
		if( meshParts->m_lods.Empty() )
		{
			meshParts->m_lods.PushBack( view.GetLODDistance( lodToMerge ) );
		}
		else
		{
			meshParts->m_lods[ 0 ] = std::min( meshParts->m_lods[ 0 ], view.GetLODDistance( lodToMerge ) );
		}
	}
}

void MeshGluer::DisassembleBones( const MeshInput* mesh, Uint32 meshIndex,MeshPartsPtr meshParts )
{
	const DataViewRO view( *mesh->m_mesh );

	red::DynArray<Bone>& bones = meshParts->m_bones.EmplaceBack();
	for( Uint32 boneIndex = 0; boneIndex < view.GetNumBones(); ++boneIndex )
	{
		Bone& bone = bones.EmplaceBack();
		bone.m_matrix = view.GetBoneMatrix( boneIndex );
		bone.m_name = view.GetBoneName( boneIndex );
		bone.m_vertexEpsilon = view.GetBoneVertexEpsilon( boneIndex );
	}

	Uint32 boneCount = view.GetNumBones();

	if( boneCount > 0 )
	{
		Uint32 boneOffset = 0;

		// Calculate bone offset
		for( Uint32 i = 0; i < meshIndex; ++i )
		{
			boneOffset += meshParts->m_bones[ i ].Size();
		}

		meshParts->m_meshBoneOffset[ meshIndex ] = boneOffset;
	}
	else
	{
		// Static meshes have no bone offset
		meshParts->m_meshBoneOffset[ meshIndex ] = 0;
	}
}

void MeshGluer::DisassembleParameters( const MeshInput* mesh, red::DynArray<const rtti::ClassType *> &meshParamClasses, MeshPartsPtr meshParts )
{
	TParameters parameters = {red::PoolBackend() };
	TSystemObjects sysObjects = { red::PoolBackend() };
	Vector3 scaleVec = mesh->m_transform.GetScale33();
	Bool notScaled = (scaleVec - Vector3::ONES()).IsAlmostZero();
	for( Uint32 paramInd = 0; paramInd < meshParamClasses.Size(); ++paramInd )
	{
		mesh::MeshParameter* parameter = meshParamClasses[ paramInd ]->CreateObject< mesh::MeshParameter >();
		if( const auto param = mesh->m_mesh->GetParameter( parameter->GetClass() ) )
		{
			if( mesh->m_customPhysics == nullptr && param->IsA<mesh::MeshParamPhysics>())
			{
				// if we haven't provided custom physics system, get the one from the mesh
				const TSysRes& sysres = mesh::utils::GetActivePhysics( *mesh->m_mesh );				
				if( sysres )
				{
					const physics::Bodies& bodies = sysres->GetBodies();
					const physics::Joints& joints = sysres->GetJoints();

					sysObjects.Reserve( sysres->GetNumObjects() );
					for( auto& body : bodies )
					{
						auto clonedBody = Cast<physics::SystemBody>(tools::CloneObject(body));
						if( notScaled )
						{
							sysObjects.PushBack(clonedBody);
						}
						else
						{

							for( const THandle<physics::ICollider >& collider : clonedBody->m_collisionShapes )
							{
								collider->ScaleGeometry(scaleVec);
							}							
							sysObjects.PushBack(clonedBody);
						}
					}

					for( auto& joint : joints )
					{
						sysObjects.PushBack(Cast<physics::SystemJoint>(tools::CloneObject(joint)));
					}

					parameters.PushBack( param );
				}				
			}			
		}
	}

	if( mesh->m_customPhysics )
	{
		const physics::Bodies& bodies = mesh->m_customPhysics->GetBodies();
		const physics::Joints& joints = mesh->m_customPhysics->GetJoints();

		Int32 size = mesh->m_customPhysics->GetNumBodies();
		for( Int32 i = size - 1; i >= 0; --i )
		{
			// There should be no need to clone custom physics, in fact it shouldn't be done
			// because of possible joints from merged mesh editor between bodies from previously separate physics system resources
			if( notScaled )
			{
				sysObjects.PushBack(bodies[ i ]);
			}
			else
			{

				for( const THandle<physics::ICollider >& collider : bodies[ i ]->m_collisionShapes )
				{
					collider->ScaleGeometry(scaleVec);
				}
				sysObjects.PushBack(bodies[ i ]);
			}
			// Detach the object
			mesh->m_customPhysics->DetachObject(bodies[ i ] );
		}
		size = mesh->m_customPhysics->GetNumJoints();
		for( Int32 i = size - 1; i >= 0; --i )
		{
			// There should be no need to clone custom physics, in fact it shouldn't be done
			// because of possible joints from merged mesh editor between bodies from previously separate physics system resources
			sysObjects.PushBack(joints[ i ]);
			// Detach the object
			mesh->m_customPhysics->DetachObject(joints[ i ]);
		}
	}

	red::DynArray<BodyInfo> bdyInfos{ red::PoolBackend() };
	bdyInfos.Reserve( sysObjects.Size() );
	for( auto sysObj : sysObjects )
	{
		if( auto sysBody = Cast<physics::SystemBody>( sysObj ) )
		{
			bdyInfos.PushBack( { sysBody->m_mappedBoneName, sysBody->m_mappedBoneToBody.ToMatrix() } );
		}
	}
	meshParts->m_bodyInfos.PushBack( bdyInfos );

	meshParts->m_physicsObjects.PushBack( sysObjects );
	meshParts->m_parameters.PushBack( parameters );
	meshParts->m_transform.PushBack( mesh->m_transform );
}

void MeshGluer::DisassembleMaterials( const MeshInput* mesh, MeshPartsPtr meshParts, Int32 lodToMerge )
{
	const DataViewRO view( *mesh->m_mesh );

	Uint32 materialNum = view.GetNumMaterials();
	meshParts->m_materialNames.Reserve( materialNum );
	meshParts->m_materials.Reserve( materialNum );
	red::Map< CName, CName > changedMatNames;
	for( Uint32 i = 0; i < materialNum; ++i )
	{
		Bool isDifferent = true;
		const THandle< IMaterial >& currentMaterial = view.GetMaterialData( i );

		for( Uint32 j : meshParts->m_materialNames.Indices() )
		{
			const THandle< IMaterial >& existingMaterial = meshParts->m_materials[j ];

			if (existingMaterial != currentMaterial)
			{
				if (existingMaterial->GetDefinitionID() != currentMaterial->GetDefinitionID())
				{
					continue;
				}

				if (existingMaterial->GetClass() != currentMaterial->GetClass())
				{
					continue;
				}

				if (auto currenTemp = Cast<CMaterialTemplate>( currentMaterial ))
				{
					auto existingTemp = Cast<CMaterialTemplate>( existingMaterial );

					if (existingTemp->GetPath() != currenTemp->GetPath())
						continue;
				}
				else
				{
					auto existingInst = Cast<CMaterialInstance>( existingMaterial );
					auto currenInst = Cast<CMaterialInstance>( currentMaterial );

					const auto& paramsEx = existingInst->GetParameters();
					const auto& paramsCurr = currenInst->GetParameters();

					Uint32 count = paramsEx.Size();
					Uint32 countCurr = paramsCurr.Size();
					if (!count || count != countCurr)
						continue;

					Bool isSame = true;
					for (Uint32 p = 0; p < count; ++p)
					{
						if (paramsEx[p].GetName() != paramsCurr[p].GetName()
							|| paramsEx[p].GetType() != paramsCurr[p].GetType())
						{
							isSame = false;
							break;
						}
						if (!paramsEx[p].GetType()->Compare( paramsEx[p].GetData(), paramsCurr[p].GetData(), 0 ))
						{
							isSame = false;
							break;
						}
					}

					if (!isSame)
						continue;
				}
			}

			isDifferent = false;
			changedMatNames.Insert( view.GetMaterialName( i ), meshParts->m_materialNames[j] );
			break;
		}

		if( isDifferent )
		{
			CName materialName = view.GetMaterialName( i );
			if( meshParts->m_materialNames.FindPtr( materialName ) == nullptr )
			{
				meshParts->m_materialNames.PushBack( materialName );
			}
			else
			{
				Uint32 index = 0;
				String newNameString = String::Printf( "%hs_%u", materialName.AsChar(), index );
				CName newName = RED_NAME( newNameString );
				while( meshParts->m_materialNames.FindPtr( newName ) != nullptr )
				{
					++index;
					newNameString = String::Printf( "%hs_%u", materialName.AsChar(), index );
					newName = RED_NAME( newNameString );
				}
				meshParts->m_materialNames.PushBack( newName );				
				changedMatNames.Insert( materialName, newName );
			}

			meshParts->m_materials.PushBack( view.GetMaterialData( i ) );
		}
	}

	// appearances
	Int32 appearanceNum = view.GetAppearanceIndex( mesh->m_meshAppearanceToMerge );
	if (appearanceNum == -1) //Mesh vs Prefab appearance name mismatch happens...
	{
		appearanceNum = 0; 
	}
	const THandle<mesh::MeshAppearance>& appearance = view.GetAppearanceData( appearanceNum );
	Uint32 chunkCount = appearance->GetNumChunks();
	for( Uint32 i = 0; i < chunkCount; ++i )
	{
		if( lodToMerge != -1 && view.GetChunkLodMask( i ) != lodToMerge )
			continue;

		CName matName = appearance->GetChunkMaterialName( i );
		if( changedMatNames.KeyExist( matName ) )
		{
			meshParts->m_appearanceMaterials.PushBack( changedMatNames[ matName ] );
		}
		else
		{
			meshParts->m_appearanceMaterials.PushBack( matName );
		}
	}
}

void MeshGluer::TransformChunks( const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts )
{
	for( Uint32 chunkIndex = 0; chunkIndex < meshParts->m_chunks.Size(); ++chunkIndex )
	{
		Uint32 meshIndex = meshParts->m_chunkToMesh[ chunkIndex ];

		Chunk& chunk = meshParts->m_chunks[ chunkIndex ];
		if( meshParts->m_bones[ meshIndex ].Size() == 0 )
		{
			ChunkStreamDataRO* positionStream = mesh::utils::FindStream( chunk.m_streams, MST_Position_3F );

			if( positionStream == nullptr ) 
				continue;

			Uint32 positionCount = positionStream->m_count;

			Matrix transform = meshes[ meshIndex ].m_transform;
			if( meshParts->m_bodyInfos[ meshIndex ].Size() > 0 )
			{
				transform = transform * meshParts->m_bodyInfos[ meshIndex ][0].m_mappedBoneToBody;
			}

			Vector3* positions = (Vector3 *)positionStream->m_data;
			for( Uint32 i = 0; i < positionCount; ++i )
			{
				positions[ i ] = transform.TransformPoint( positions[ i ] );
			}

			Matrix normalTransform( transform );
			normalTransform.OrthonormInvert();
			normalTransform.Transpose();
			for( auto& streamType : red::StaticArray<EMeshStreamType, 3>{ MST_Normal_3F, MST_Binormal_3F, MST_Tangent_3F } )
			{
				if( ChunkStreamDataRO* stream = mesh::utils::FindStream( chunk.m_streams, streamType ) )
				{
					Vector3* streamData = (Vector3 *)stream->m_data;
					for( Uint32 i = 0; i < stream->m_count; ++i )
					{
						streamData[ i ] = normalTransform.TransformPoint( streamData[ i ] );
					}
				}
			}
		}
		else
		{
			ChunkStreamDataRO* positionStream = mesh::utils::FindStream( chunk.m_streams, MST_Position_3F );
			if( positionStream == nullptr ) 
				continue;
			ChunkStreamDataRO* skinningStream = mesh::utils::FindStream( chunk.m_streams, MST_SkinningIndices_4U8 );
			ChunkStreamDataRO* normalsStream = mesh::utils::FindStream( chunk.m_streams, MST_Normal_3F );
			ChunkStreamDataRO* binormalsStream = mesh::utils::FindStream( chunk.m_streams, MST_Binormal_3F );
			ChunkStreamDataRO* tangentStream = mesh::utils::FindStream( chunk.m_streams, MST_Tangent_3F );
			
			Vector3* normals = (Vector3 *)normalsStream->m_data;
			Vector3* binormals = (Vector3 *)binormalsStream->m_data;
			Vector3* tangents = (Vector3 *)tangentStream->m_data;
			Vector3* positions = (Vector3 *)positionStream->m_data;
			Uint8* boneIndices = (Uint8 *)skinningStream->m_data;

			Uint32 positionCount = positionStream->m_count;
			const auto indices = meshParts->m_bones[ meshIndex ].Indices();
			for( auto boneIndex : indices )
			{
				auto bone = meshParts->m_bones[ meshIndex ][ boneIndex ];

				Matrix mappedBoneToBody = Matrix::IDENTITY();

				for( auto bodyInfo : meshParts->m_bodyInfos[ meshIndex ] )
				{
					if( bodyInfo.m_boneName == bone.m_name )
					{
						mappedBoneToBody = bodyInfo.m_mappedBoneToBody;
						break;
					}
				}

				const Matrix& transform = meshes[ meshIndex ].m_transform * mappedBoneToBody;
				Matrix normalTransform( transform );
				normalTransform.OrthonormInvert();
				normalTransform.Transpose();
				for( Uint32 i = 0; i < positionCount; ++i )
				{
					if( boneIndices[ 4 * i ] == boneIndex )
					{
						positions[ i ] = transform.TransformPoint( positions[ i ] );
						normals[ i ] = normalTransform.TransformPoint( normals[ i ] );
						binormals[ i ] = normalTransform.TransformPoint( binormals[ i ] );
						tangents[ i ] = normalTransform.TransformPoint( tangents[ i ] );
					}
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
////////////////// Mesh optimization /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// Uncomment to enable debug logs & timing
#define MESH_OPTIMIZATION_DEBUG 
// Uncomment to enable dumping rasterization to file
//#define MESH_OPTIMIZATION_DEBUG_RENDER_DUMP

const Uint32 c_samplingPoints = 1024;
const Uint32 c_depthBufferWidth = 682;
const Uint32 c_depthBufferHeight = 512;
const Int32 c_rasterizersPerJob = 8;

union FaceIDHelper
{
	Uint32 m_combined;
	struct
	{
		Uint16 m_chunk;
		Uint16 m_face;
	};
};

struct ChunkData
{
	RED_USE_MEMORY_POOL(red::PoolBackend);
	red::DynArray< Vector3 > m_vertices;
	red::DynArray< Uint16 > m_indices;
	red::DynArray< Bool > m_facesVisibility;
	Bool m_isTransparent;
	red::Atomic<Uint32> m_visibleIndicesCount;

	ChunkData()
		: m_isTransparent( false )
	{}

	ChunkData(ChunkData& data)
		: m_vertices(data.m_vertices)
		, m_indices(data.m_indices)
		, m_facesVisibility(data.m_facesVisibility)
		, m_isTransparent(data.m_isTransparent)
	{
		m_visibleIndicesCount.SetValue(data.m_visibleIndicesCount.GetValue());
	}
};

#ifdef MESH_OPTIMIZATION_DEBUG_RENDER_DUMP

void DebugDumpToPNGFile(const vis::rast::RasterizerSSEFlat& rast, const String& fileName)
{
	constexpr Uint32 idsAmount = c_depthBufferWidth * c_depthBufferHeight;
	red::DynArray<Uint32> idBuffer = { red::PoolBackend() };
	idBuffer.Resize(idsAmount);
	rast.GetDebugIDBuffer(&idBuffer[ 0 ]);
	FILE *imageFile;
	imageFile = fopen(fileName.AsChar(), "wb");
	if( imageFile == NULL ) {
		perror("ERROR: Cannot open output file");
		return;
	}

	fprintf(imageFile, "P6\n");               // P6 filetype
	fprintf(imageFile, "%d %d\n", c_depthBufferWidth, c_depthBufferHeight);   // dimensions
	fprintf(imageFile, "255\n");              // Max pixel

	red::DynArray<Uint8> pixBuffer = { red::PoolBackend() };;
	pixBuffer.Resize(idsAmount * 3);
	for( Uint32 i = 0; i < idsAmount; ++i )
	{
		pixBuffer[ 3 * i ] = idBuffer[ i ];
		pixBuffer[ 3 * i + 1] = idBuffer[ i ] >> 8;
		pixBuffer[ 3 * i  + 2] = idBuffer[ i ] >> 16;
	}

	fwrite(pixBuffer.TypedData(), 1, idsAmount * 3, imageFile);
	fclose(imageFile);
}

#endif

void GenerateUniformSamplingPointsOnSphereRNGUniformDistribution(red::DynArray<Vector3>& samplingPoints, const Float radius, const Vector3& boundsCenter)
{
	Uint32 seed = (Uint32)std::chrono::system_clock::now().time_since_epoch().count();
	std::mt19937 generator(seed);
	std::uniform_real_distribution<Float> uniform01(0.0, 1.0);

	for( Uint32 i = 0; i < c_samplingPoints; ++i )
	{
		Float theta = 2 * RED_PI * uniform01(generator);
		Float phi = acos(1 - 2 * uniform01(generator));
		Float x = radius * MSin(phi) * MCos(theta);
		Float y = radius * MSin(phi) * MSin(theta);
		Float z = radius * MCos(phi);
		samplingPoints[ i ] = boundsCenter + Vector3(x, y, z);
	}
}

// https://stackoverflow.com/questions/9600801/evenly-distributing-n-points-on-a-sphere
void GenerateUniformSamplingPointsOnSphereFibonacci(red::DynArray<Vector3>& samplingPoints, const Float radius, const Vector3& boundsCenter)
{
	Float offset = 2.0f / (Float)c_samplingPoints;
	Float offsetDiv2 = offset / 2.0f;
	Float increment = RED_PI * 6.7082f; // 3 - sqrt(5) ~= 6.7082;
	for( Uint32 i = 0; i < c_samplingPoints; ++i )
	{
		Float iF = (Float)i;
		Float y = ((iF * offset) - 1.0f) + offsetDiv2;
		Float r = sqrtf(1.0f - pow(y, 2));
		Float phi = ((Float)((i + 1) % c_samplingPoints)) * increment;

		Float x = MCos(phi) * r;
		Float z = MSin(phi) * r;
		samplingPoints[ i ] = boundsCenter + (Vector3(x, y, z) * radius);
	}
}

Vector3 GetUniformSamplingPointOnSphereFibonacci(const Float radius, const Vector3& boundsCenter, Uint32 i)
{
	Float offset = 2.0f / (Float)c_samplingPoints;
	Float offsetDiv2 = offset / 2.0f;
	Float increment = RED_PI * 6.7082f; // 3 - sqrt(5) ~= 6.7082;

	Float iF = (Float)i;
	Float y = ((iF * offset) - 1.0f) + offsetDiv2;
	Float r = sqrtf(1.0f - pow(y, 2));
	Float phi = ((Float)((i + 1) % c_samplingPoints)) * increment;

	Float x = MCos(phi) * r;
	Float z = MSin(phi) * r;
	return boundsCenter + (Vector3(x, y, z) * radius);

}

void FinalizeMeshOptimization(red::DynArray<DataBuilder::UnpackedChunk> &chunks, red::SharedPtr<red::DynArray<ChunkData>, red::PoolBackend > chunkData, Uint32 numAllFaces, Uint32& notVisibleFaces, Uint32& removedVertices)
{
	red::DynArray< red::DynArray< Uint16 > > chunkVisibleIndices = { red::PoolBackend() };
	red::DynArray< Uint16 > chunkVisibleVerticesCount = { red::PoolBackend() };
	red::DynArray< red::DynArray< Bool > > chunkVertexVisibility = { red::PoolBackend() };
	chunkVisibleIndices.Resize(chunks.Size());
	chunkVertexVisibility.Resize(chunks.Size());
	chunkVisibleVerticesCount.Resize(chunks.Size());
	removedVertices = 0;
	notVisibleFaces = 0;

	for( auto index : chunks.Indices() )
	{
		const auto& chunkIndices = (*chunkData)[ index ].m_indices;
		chunkVisibleIndices[ index ].Reserve((*chunkData)[ index ].m_visibleIndicesCount.GetValue());
		Uint32 facesCount = (*chunkData)[ index ].m_facesVisibility.Size();

		Uint32 verticesCount = (*chunkData)[ index ].m_vertices.Size();
		chunkVertexVisibility[ index ].Resize(verticesCount);
		for( Uint32 i = 0; i < verticesCount; ++i )
		{
			chunkVertexVisibility[ index ][i] = false;
		}

		for( Uint32 i = 0; i < facesCount; ++i )
		{
			Uint32 aInd = chunkIndices[ i * 3 ];
			Uint32 bInd = chunkIndices[ i * 3 + 1 ];
			Uint32 cInd = chunkIndices[ i * 3 + 2 ];

			if( (*chunkData)[ index ].m_facesVisibility[ i ] )
			{
				chunkVisibleIndices[ index ].PushBack(aInd);
				chunkVisibleIndices[ index ].PushBack(bInd);
				chunkVisibleIndices[ index ].PushBack(cInd);

				chunkVertexVisibility[ index ][ aInd ] = true;
				chunkVertexVisibility[ index ][ bInd ] = true;
				chunkVertexVisibility[ index ][ cInd ] = true;
			}
			else
			{
				++notVisibleFaces;
			}
		}
	}

	if( !notVisibleFaces )
		return;


	for( auto index : chunks.Indices() )
	{
		Uint32 verticesCount = (*chunkData)[ index ].m_vertices.Size();
		red::DynArray< Uint16 > offsets;
		offsets.Resize(verticesCount);
		Uint16 offsetAccumulator = 0;
		chunkVisibleVerticesCount[ index ] = 0;
		for( Uint32 i = 0; i < verticesCount; ++i )
		{
			if( !chunkVertexVisibility[ index ][ i ] )
			{
				++offsetAccumulator;
				++removedVertices;
			}
			else
			{
				chunkVisibleVerticesCount[ index ]++;
				offsets[ i ] = offsetAccumulator;
			}
		}

		for( Uint32 i = 0; i < chunkVisibleIndices[index].Size(); ++i )
		{
			chunkVisibleIndices[ index ][ i ] -= offsets[ chunkVisibleIndices[ index ][ i ] ];
		}

	}
	const Uint32 vertexDataStreamsMask = MST_Position_3F | MST_Color_U32 | MST_Normal_3F | MST_Tangent_3F | MST_Binormal_3F | MST_TexCoord0_2F | MST_TexCoord1_2F | MST_SkinningIndices_4U8 |
		MST_SkinningWeights_4F | MST_Multilayer_1F | MST_SkinningIndicesExt_4U8 | MST_SkinningWeightsExt_4F;

	for( auto index : chunks.Indices() )
	{
		auto& chunk = chunks[ index ];

		for( Uint32 streamInd : chunk.m_streams.Indices() )
		{
			RED_LOG("Chunk%d stream %d, size: %d", index, chunk.m_streams[ streamInd ].m_type, chunk.m_streams[ streamInd ].m_data.Size() / MeshPackerUtils::GetMeshStreamSize(chunk.m_streams[ streamInd ].m_type));
			if( chunk.m_streams[ streamInd ].m_type == MST_Index_U16 )
			{
				auto& destinationStream = chunk.m_streams[ streamInd ];

				Uint32 stride = MeshPackerUtils::GetMeshStreamSize(MST_Index_U16);
				Uint32 size = stride * chunkVisibleIndices[ index ].Size();
				destinationStream.m_data.Clear();
				destinationStream.m_data = DataBuffer(size);
				const void* sourceData = chunkVisibleIndices[ index ].TypedData();
				void* destinationData = static_cast<Uint8 *>(destinationStream.m_data.Data());
				red::Memcpy(destinationData, sourceData, size);
			}
			else if( (chunk.m_streams[ streamInd ].m_type & vertexDataStreamsMask) != 0 )
			{
				Uint32 stride = MeshPackerUtils::GetMeshStreamSize(chunk.m_streams[ streamInd ].m_type);

				Uint32 streamSize = chunk.m_streams[ streamInd ].m_data.Size() / stride;
				Uint32 verticesCount = (*chunkData)[ index ].m_vertices.Size();
				Uint32 visibleVerticesCount = chunkVisibleVerticesCount[ index ];

				if( verticesCount == visibleVerticesCount )
					continue;

				RED_ASSERT(verticesCount == streamSize, "Per vertex data stream has different size than position stream!");
				auto& stream = chunk.m_streams[ streamInd ];
				DataBuffer newData = DataBuffer(visibleVerticesCount * stride);

				Uint32 startIndexDest = 0;
				Uint32 startIndexSrc = 0;
				Uint32 elementsToCopy = 0;

				for( Uint32 i = 0; i < verticesCount; ++i )
				{
					if( !chunkVertexVisibility[ index ][ i ] )
					{
						if( elementsToCopy > 0 )
						{
							void* streamData = static_cast<Uint8 *>(stream.m_data.Data()) + (startIndexSrc * stride);
							void* destinationData = static_cast<Uint8 *>(newData.Data()) + (startIndexDest * stride );
							Uint32 size = elementsToCopy * stride;
							red::Memcpy(destinationData, streamData, size );
							startIndexSrc = i + 1;
							startIndexDest += elementsToCopy;
							elementsToCopy = 0;
						}
						else
						{
							++startIndexSrc;
						}
					}
					else
					{
						elementsToCopy++;
					}
				}

				if( elementsToCopy > 0 )
				{
					void* streamData = static_cast<Uint8 *>(stream.m_data.Data()) + (startIndexSrc * stride);
					void* destinationData = static_cast<Uint8 *>(newData.Data()) + (startIndexDest * stride);
					Uint32 size = elementsToCopy * stride;
					red::Memcpy(destinationData, streamData, size);
				}
				stream.m_data.Clear();
				stream.m_data = std::move(newData);
			}
		}
	}
	
}

static void OptimizeMeshGeometry(red::DynArray< DataBuilder::UnpackedChunk >& chunks, const red::DynArray<ChunkCluster>& chunkClusters, job::Builder& builder)
{
	red::SharedPtr< red::DynArray< ChunkData >, red::PoolBackend > chunkData( RED_NEW(red::DynArray< ChunkData >, red::PoolBackend)() );
	chunkData->Resize( chunks.Size() );
	Uint32 numAllVerts = 0;
	Uint32 numAllFaces = 0;

	// Get stream data
	Box boundingBox = Box::EMPTY();
	for( auto index : chunks.Indices() )
	{
		auto& chunk = chunks[index];
		Int32 positionStream = -1;
		Int32 indicesStream = -1;

		for( Uint32 streamInd : chunk.m_streams.Indices() )
		{
			if( chunk.m_streams[streamInd].m_type == MST_Position_3F )
			{
				positionStream = streamInd;
			}
			else if( chunk.m_streams[streamInd].m_type == MST_Index_U16 )
			{
				indicesStream = streamInd;
			}
		}

		RED_ASSERT( positionStream >= 0, "Clustered chunk doesn't have position stream!" );
		RED_ASSERT( indicesStream >= 0, "Clustered chunk doesn't have index stream!" );

		Uint32 vertexCount = chunk.m_streams[positionStream].m_data.Size() / (3 * sizeof( float ));
		Uint32 indexCount = chunk.m_streams[indicesStream].m_data.Size() / ( sizeof( Uint16 ));
		Uint32 facesCount = indexCount / 3;
		
		numAllVerts += vertexCount;
		numAllFaces += facesCount;

		(*chunkData)[index].m_vertices.Resize( vertexCount );
		Vector3* typedData = (Vector3*)chunk.m_streams[positionStream].m_data.Data();
		for( Uint32 i = 0; i < vertexCount; ++i )
		{
			boundingBox.AddPoint( typedData[i] );
			(*chunkData)[ index ].m_vertices[i] = typedData[i];
		}

		(*chunkData)[ index ].m_indices.Resize( indexCount );
		(*chunkData)[ index ].m_facesVisibility.Resize(indexCount/3);
		(*chunkData)[ index ].m_visibleIndicesCount.SetValue( 0 );
		(*chunkData)[ index ].m_isTransparent = chunkClusters[ index ].m_isTransparent;
		Uint16* indicesData = (Uint16*)chunk.m_streams[indicesStream].m_data.Data();
		for( Uint32 i = 0; i < facesCount; ++i )
		{
			Uint32 aInd = indicesData[i * 3];
			Uint32 bInd = indicesData[i * 3 + 1];
			Uint32 cInd = indicesData[i * 3 + 2];

			(*chunkData)[ index ].m_indices[i * 3] = aInd;
			(*chunkData)[ index ].m_indices[i * 3 + 1] = bInd;
			(*chunkData)[ index ].m_indices[i * 3 + 2] = cInd;
			(*chunkData)[ index ].m_facesVisibility[ i ] = false;
		}
	}

	Float radius = boundingBox.CalcExtents().Mag3() * 1.25f;
	Vector3 boundsCenter = boundingBox.CalcCenter();
	
	const auto& indexRange = chunks.Indices();
	Int32 jobsToRun = c_samplingPoints;
	Uint32 startIndex = 0;
	while( jobsToRun > 0 )
	{
		Uint32 rastAmount = (Uint32)Min(c_rasterizersPerJob, jobsToRun);
		builder.DispatchJob< job::Fence::None >("CheckCameraAngle", [ indexRange, chunkData,
			radius, boundsCenter, startIndex, rastAmount, numAllFaces, numAllVerts ]
			(const job::RunContext& runContext)
		{
			Matrix projection;
			projection.BuildPerspectiveLH(DEG2RAD(100.0f), 1.0f, 0.01f, 1000.0f);

			for( Uint32 j = 0; j < rastAmount; ++j )
			{
				Vector3 cameraPosition = GetUniformSamplingPointOnSphereFibonacci(radius, boundsCenter, startIndex + j);
				Vector3 forward = boundsCenter - cameraPosition;
				forward.Normalize();
				Quaternion rotation;
				rotation.SetShortestRotation(Vector3::EZ(), forward);
				rotation.Normalize();
				Transform transform = Transform::IDENTITY();
				transform.SetOrientation(rotation);
				transform.SetPosition(cameraPosition);
				Matrix worldToView = transform.ToInvMatrix();

				vis::rast::RasterizerSSEFlat rast{ red::PoolBackend() };
				vis::Camera camera;
				camera.Set(worldToView, projection);

				// Rasterizer works with reversed depth projection, so need to do that.
				camera.ReverseDepth();

				vis::rast::RasterizerSSEFlat::InitInfo rastInit;
				rastInit.m_depthBufferWidth = c_depthBufferWidth;
				rastInit.m_depthBufferHeight = c_depthBufferHeight;
				rastInit.m_camera = camera;
				rastInit.m_allowDrawIDs = true;
				rast.Initialize(rastInit);
				rast.ClearDepthBuffer();

				// Solid triangles pass
				for( auto index : indexRange )
				{
					if( (*chunkData)[ index ].m_isTransparent )
						continue;

					auto& vertices = (*chunkData)[ index ].m_vertices;
					auto& indices = (*chunkData)[ index ].m_indices;

					Uint32 vertexCount = vertices.Size();
					Uint32 indexCount = indices.Size();
					Uint32 facesCount = indexCount / 3;

					for( Uint32 i = 0; i < facesCount; ++i )
					{
						Uint16 aInd = indices[ i * 3 ];
						Uint16 bInd = indices[ i * 3 + 1 ];
						Uint16 cInd = indices[ i * 3 + 2 ];
						Uint16 indices[ 3 ] = { 2, 1, 0 };
						Vector4 trianglePoints[ 3 ] = { vertices[ aInd ], vertices[ bInd ], vertices[ cInd ] };
						Uint32 drawFlags = vis::rast::IRasterizer::DRAW_Default;
						rast.DrawTriangles( &trianglePoints[ 0 ], 3, &indices[ 0 ], 3, Matrix::IDENTITY(), drawFlags, (index | (i << 16)));
					}
				}

				// Transparent chunks pass
				for( auto index : indexRange )
				{
					if( !(*chunkData)[ index ].m_isTransparent )
						continue;

					auto& vertices = (*chunkData)[ index ].m_vertices;
					auto& indices = (*chunkData)[ index ].m_indices;

					Uint32 vertexCount = vertices.Size();
					Uint32 indexCount = indices.Size();
					Uint32 facesCount = indexCount / 3;

					for( Uint32 i = 0; i < facesCount; ++i )
					{
						if( (*chunkData)[ index ].m_facesVisibility[ i ] )
							continue;

						Uint16 aInd = indices[ i * 3 ];
						Uint16 bInd = indices[ i * 3 + 1 ];
						Uint16 cInd = indices[ i * 3 + 2 ];
						Uint16 indices[ 3 ] = { 2, 1, 0 };
						Vector4 trianglePoints[ 3 ] = { vertices[ aInd ], vertices[ bInd ], vertices[ cInd ] };
						Uint32 drawFlags = vis::rast::IRasterizer::DRAW_Default;
						if( rast.TestTriangles( &trianglePoints[ 0 ], 3, &indices[ 0 ], 3, Matrix::IDENTITY(), drawFlags) )
						{
							(*chunkData)[ index ].m_facesVisibility[ i ] = true;
							(*chunkData)[ index ].m_visibleIndicesCount.Increment();
						}
					}
				}

#ifdef MESH_OPTIMIZATION_DEBUG_RENDER_DUMP
				String debugDumpName = String::Printf(".//rasterizerDump//mesh(%d,%d)_rasterizer%d_(%.2f, %.2f, %.2f)_(%.2f, %.2f, %.2f).ppm", 
					numAllVerts, numAllFaces, startIndex + j, cameraPosition.X, cameraPosition.Y, cameraPosition.Z, forward.X, forward.Y, forward.Z);
				DebugDumpToPNGFile(rast, debugDumpName );
#endif
				FaceIDHelper helper;
				red::DynArray< Uint32 > visibleIds;
				rast.GetVisibleIDs(visibleIds);

				for( Uint32 id : visibleIds )
				{
					helper.m_combined = id;
					if( !(*chunkData)[ helper.m_chunk ].m_facesVisibility[ helper.m_face ] )
					{
						(*chunkData)[ helper.m_chunk ].m_facesVisibility[ helper.m_face ] = true;
						(*chunkData)[ helper.m_chunk ].m_visibleIndicesCount.Increment();
					}
				}
				
			}
		});
		startIndex += rastAmount;
		jobsToRun -= c_rasterizersPerJob;
	}

	builder.DispatchFenceExplicitly();

	builder.DispatchJob("FinalizeMeshOptimization", [ &chunks, chunkData, numAllFaces, numAllVerts ]
		(const job::RunContext& runContext)
	{
			Uint32 notVisibleFaces = 0;
			Uint32 removedVertices = 0;
			FinalizeMeshOptimization(chunks, chunkData, numAllFaces, notVisibleFaces, removedVertices);

#ifdef MESH_OPTIMIZATION_DEBUG
		RED_LOG("Mesh with %d vertices and %d triangles - rasterizing algorithm with %d sampling points (%i rasterizers per job), buffer size: %dx%d found %d invisible faces (removed %d verts from output mesh).",
			numAllVerts, numAllFaces, c_samplingPoints, c_rasterizersPerJob, c_depthBufferWidth, c_depthBufferHeight, notVisibleFaces, removedVertices);
#endif
	});
}

//////////////////////////////////////////////////////////////////////////

void MeshGluer::AssembleMesh(MeshPartsPtr meshParts, CMesh* output, const MeshGluerInput& input, job::Builder& builder)
{
	red::SharedPtr<red::DynArray< DataBuilder::UnpackedChunk >> builderChunks( RED_NEW( red::DynArray< DataBuilder::UnpackedChunk >, red::PoolBackend));

	builder.DispatchJob("PrepareBuilderChunks", [ meshParts, input, builderChunks ](const job::RunContext& runContext)
	{
		red::DynArray<ChunkCluster> chunkClusters;
		ClusterChunks(meshParts, chunkClusters);

		// We need to reindex bones before merging, because it also changes skinningIndices streams
		ReindexBones(meshParts);

		builderChunks->Reserve(chunkClusters.Size());
		for( auto& cluster : chunkClusters )
		{
			DataBuilder::UnpackedChunk chunk;
			chunk.m_lodMask = input.m_lodToMerge == -1 ? cluster.m_lodMask : 1;
			chunk.m_materials = cluster.m_materials;
			chunk.m_vertexType = cluster.m_chunks[ 0 ]->m_vertexType;
			chunk.m_flags = cluster.m_flags;
			chunk.m_flags.m_renderInScene = true;
			chunk.m_materialUUID = cluster.m_chunkUUID.first;
			chunk.m_meshUUID = cluster.m_chunkUUID.second;

			PrepareChunkStreamsFromCluster(cluster, chunk);
			CopyStreamDataFromCluster(cluster, chunk, cluster.m_streamsCount);

			builderChunks->PushBack(std::move(chunk));
		}

		if( input.m_optimizeMesh )
		{
			job::Builder depBuilder{ runContext };
			OptimizeMeshGeometry(*builderChunks, chunkClusters, depBuilder);
		}
	});	

	builder.DispatchJob("ProcessBuilderChunks", [ meshParts, input, builderChunks, output ](const job::RunContext&)
	{
		DataBuilder builder;
		for( auto& chunk : *builderChunks )
		{
			builder.m_chunks.PushBack(std::move(chunk));
		}

		for( Float distane : meshParts->m_lods )
		{
			DataBuilder::UnpackedLOD lod;
			lod.m_distance = distane;
			builder.m_lods.PushBack( lod );
		}

		if( !input.m_deleteSkinning )
			RemapBones(meshParts, builder);
		MergeParameters(meshParts, output, input);

		mesh::DataView view(*output);
		view.RemoveMaterials();
		for( Uint32 i = 0; i < meshParts->m_materials.Size(); ++i )
		{
			if( !meshParts->m_materials[ i ]->IsA<CMaterialTemplate>() )
			{
				THandle< IMaterial > clonedMaterial = Cast<IMaterial>(tools::CloneObject(meshParts->m_materials[ i ]));
				clonedMaterial->SetParent(output);
				view.AddMaterial(clonedMaterial, meshParts->m_materialNames[ i ]);
			}
			else
			{
				view.AddMaterial(meshParts->m_materials[ i ], meshParts->m_materialNames[ i ]);
			}
		}

		CName defaultApp = RED_NAME_CONSTEXPR("default");
		meshParts->m_appearanceMaterials.Remove( std::remove_if( meshParts->m_appearanceMaterials.Begin(), meshParts->m_appearanceMaterials.End(),
			[]( CName matName )
		{
			return matName == CName::NONE();
		} ), meshParts->m_appearanceMaterials.End() );

		CName defaultTemp = RED_NAME_CONSTEXPR( "defaultTemp" );
		THandle< mesh::MeshAppearance > appearance = CreateHandle< mesh::MeshAppearance>( meshParts->m_appearanceMaterials, output, defaultTemp );
		view.AddAppearance( appearance );
		view.RemoveAppearance( defaultApp );
		view.RenameAppearance( 0, defaultApp );
		// cleanup of temp stuff
		view.RemoveUnusedBones();
		view.RemoveUnusedMaterials();
		view.Apply();

		mesh::DataBuilder::FlushOptions flushOptions;
		flushOptions.m_verbose = false;
		flushOptions.m_rebuildMaterials = true;
		builder.Flush( *output, flushOptions );

		builderChunks->Clear();
	});
}

void MeshGluer::AssembleMesh_Sync(MeshPartsPtr meshParts, CMesh* output, const MeshGluerInput& input)
{
	DataBuilder builder;
	red::DynArray<ChunkCluster> chunkClusters;
	ClusterChunks( meshParts, chunkClusters );

	// We need to reindex bones before merging, because it also changes skinningIndices streams
	ReindexBones( meshParts );

	for( auto& cluster : chunkClusters )
	{
		DataBuilder::UnpackedChunk chunk;
		chunk.m_lodMask = input.m_lodToMerge == -1 ? cluster.m_lodMask : 1;
		chunk.m_materials = cluster.m_materials;
		chunk.m_vertexType = cluster.m_chunks[ 0 ]->m_vertexType;
		chunk.m_flags = cluster.m_flags;
		chunk.m_flags.m_renderInScene = true;

		PrepareChunkStreamsFromCluster( cluster, chunk );
		CopyStreamDataFromCluster( cluster, chunk, cluster.m_streamsCount );

		builder.m_chunks.PushBack( std::move( chunk ) );
	}

	for( Float distane : meshParts->m_lods )
	{
		DataBuilder::UnpackedLOD lod;
		lod.m_distance = distane;
		builder.m_lods.PushBack( lod );
	}

	MergeParameters( meshParts, output, input );
	if( !input.m_deleteSkinning )
		RemapBones( meshParts, builder );

	mesh::DataView view( *output );
	view.RemoveMaterials();
	for( Uint32 i = 0; i < meshParts->m_materials.Size(); ++i )
	{
		if( !meshParts->m_materials[ i ]->IsA<CMaterialTemplate>() )
		{
			THandle< IMaterial > clonedMaterial = Cast<IMaterial>( tools::CloneObject( meshParts->m_materials[i] ) );
			clonedMaterial->SetParent( output );
			view.AddMaterial( clonedMaterial, meshParts->m_materialNames[i] );
		}
		else
		{
			view.AddMaterial( meshParts->m_materials[i], meshParts->m_materialNames[i] );
		}
	}

	CName defaultApp = RED_NAME_CONSTEXPR( "default" );
	meshParts->m_appearanceMaterials.Remove( std::remove_if( meshParts->m_appearanceMaterials.Begin(), meshParts->m_appearanceMaterials.End(),
		[]( CName matName )
	{
		return matName == CName::NONE();
	} ), meshParts->m_appearanceMaterials.End() );

	CName defaultTemp = RED_NAME_CONSTEXPR( "defaultTemp" );

	THandle< mesh::MeshAppearance > appearance = CreateHandle< mesh::MeshAppearance>( meshParts->m_appearanceMaterials, output, defaultTemp );
	view.AddAppearance( appearance );
	view.RemoveAppearance( defaultApp );
	view.RenameAppearance( 0, defaultApp );

	// cleanup of temp stuff
	view.RemoveUnusedBones();
	view.RemoveUnusedMaterials();
	view.Apply();

	mesh::DataBuilder::FlushOptions flushOptions;
	flushOptions.m_rebuildMaterials = false;
	builder.Flush( *output, flushOptions );
}

void MeshGluer::GenerateSkinning( const red::DynArray<MeshInput>& meshes,MeshPartsPtr meshParts )
{
	if( !meshParts->m_bones.Empty() )
	{
		// TODO: Maybe change so it doesn't lose the already skinned mesh skinning? 
		RED_LOG_WARNING( "Requested mesh merger with skinning regeneration, but one of the source meshes has skinning already. It will be replaced." );
	}

	meshParts->m_bones.Clear();
	meshParts->m_meshBoneOffset.Clear();

	for( Uint32 meshIndex = 0; meshIndex < meshes.Size(); ++meshIndex )
	{
		String meshIndexString;
		ToString( meshIndexString, meshIndex );

		CName name = RED_NAME( ( "auto_" + meshIndexString ) );

		Bone bone;
		bone.m_name = name;
		bone.m_matrix = Matrix::IDENTITY();
		bone.m_vertexEpsilon = 0.0f;

		const TSystemObjects& objects = meshParts->m_physicsObjects[ meshIndex ];
		// Remap physics bindings to set proper mesh
		Uint32 bodyIndex = 0;
		for( Uint32 j = 0; j < objects.Size(); ++j )
		{
			if( THandle<physics::SystemBody> sysBody = Cast<physics::SystemBody>( objects[ j ] ) )
			{
				meshParts->m_bodyInfos[ meshIndex ][ bodyIndex ].m_boneName = name;
				++bodyIndex;
				RemapPhysicsBindings( sysBody, name ); // Same as above, we just create a default mesh binding, which will be remapped when merging to final mesh
			}
		}

		red::DynArray<Bone>& bones = ( meshParts->m_bones.EmplaceBack() );

		meshParts->m_meshBoneOffset.EmplaceBack( meshIndex ); 
		bones.PushBack( bone );
	}

	Uint32 chunkIndex = 0;

	for( auto& chunk : meshParts->m_chunks )
	{
		ChunkStreamDataRO* skinningIndexStream = nullptr;
		ChunkStreamDataRO* skinningWeightStream = nullptr;

		// We need to convert each static chunk into skinned one
		if( chunk.m_vertexType != EMeshVertexType::MVT_SkinnedMesh )
		{
			Uint32 skinnedStreamCount = chunk.m_vertexCount;

			if( !( chunk.m_streamMask & MST_SkinningIndices_4U8 ) )
			{
				Uint32 index = red::BitUtils::BitScanForward( (Uint32)MST_SkinningIndices_4U8 );
				auto containerOpResult = chunk.m_streams.EmplaceAt( index );
				if( containerOpResult.IsSuccessful() )
				{
					Uint32 skinningIndexStride = MeshPackerUtils::GetMeshStreamSize( MST_SkinningIndices_4U8 );

					skinningIndexStream = &chunk.m_streams[ index ];
					skinningIndexStream->m_buffer = serialization::BufferHandleRO( skinningIndexStride * skinnedStreamCount );
					skinningIndexStream->m_count = skinnedStreamCount;
					skinningIndexStream->m_data = skinningIndexStream->m_buffer.Data();
					skinningIndexStream->m_stream = MST_SkinningIndices_4U8;
					skinningIndexStream->m_stride = skinningIndexStride;
					chunk.m_streamMask |= MST_SkinningIndices_4U8;
				}
			}

			if( !( chunk.m_streamMask & MST_SkinningWeights_4F ) )
			{
				Uint32 index = red::BitUtils::BitScanForward( (Uint32)MST_SkinningWeights_4F );
				auto containerOpResult = chunk.m_streams.EmplaceAt( index );
				if( containerOpResult.IsSuccessful() )
				{
					Uint32 skinningWeightStride = MeshPackerUtils::GetMeshStreamSize( MST_SkinningWeights_4F );
					skinningWeightStream = &chunk.m_streams[ index ];
					skinningWeightStream->m_buffer = serialization::BufferHandleRO( skinningWeightStride * skinnedStreamCount );
					skinningWeightStream->m_count = skinnedStreamCount;
					skinningWeightStream->m_data = skinningWeightStream->m_buffer.Data();
					skinningWeightStream->m_stream = MST_SkinningWeights_4F;
					skinningWeightStream->m_stride = skinningWeightStride;
					chunk.m_streamMask |= MST_SkinningWeights_4F;
				}
			}

			chunk.m_vertexType = EMeshVertexType::MVT_SkinnedMesh;
		}

		skinningIndexStream = mesh::utils::FindStream( chunk.m_streams, MST_SkinningIndices_4U8 );
		skinningWeightStream = mesh::utils::FindStream( chunk.m_streams, MST_SkinningWeights_4F );

		Uint32 meshIndex = meshParts->m_chunkToMesh[ chunkIndex ];

		// Set skinning weights
		Float* skinningWeights = static_cast< Float* >( const_cast< void* >( skinningWeightStream->m_data ) );
		for( Uint32 i = 0; i < 4 * skinningWeightStream->m_count; i += 4 )
		{
			skinningWeights[ i ] = 1.0f;
			skinningWeights[ i + 1 ] = 0.f;
			skinningWeights[ i + 2 ] = 0.f;
			skinningWeights[ i + 3 ] = 0.f;
		}

		// Set skinning indices
		Uint8* skinningIndices = (Uint8 *)skinningIndexStream->m_data;
		for( Uint32 i = 0; i < 4 * skinningIndexStream->m_count; i += 4 )
		{
			skinningIndices[ i + 0 ] = 0;
			skinningIndices[ i + 1 ] = 0;
			skinningIndices[ i + 2 ] = 0;
			skinningIndices[ i + 3 ] = 0;
		}
		chunkIndex++;
	}
}

void MeshGluer::DeleteSkinning( const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts )
{
	for( Uint32 chunkIndex : meshParts->m_chunks.Indices() )
	{
		Uint32 meshIndex = meshParts->m_chunkToMesh[ chunkIndex ];
		if( meshParts->m_bones[ meshIndex ].Size() == 0 )
			continue;

		Chunk& chunk = meshParts->m_chunks[ chunkIndex ];
		ChunkStreamDataRO* positionStream = mesh::utils::FindStream( chunk.m_streams, MST_Position_3F );
		if( positionStream == nullptr ) 
			continue;
		ChunkStreamDataRO* skinningStream = mesh::utils::FindStream( chunk.m_streams, MST_SkinningIndices_4U8 );
		ChunkStreamDataRO* normalsStream = mesh::utils::FindStream( chunk.m_streams, MST_Normal_3F );
		ChunkStreamDataRO* binormalsStream = mesh::utils::FindStream( chunk.m_streams, MST_Binormal_3F );
		ChunkStreamDataRO* tangentStream = mesh::utils::FindStream( chunk.m_streams, MST_Tangent_3F );

		Vector3* normals = (Vector3 *)normalsStream->m_data;
		Vector3* binormals = (Vector3 *)binormalsStream->m_data;
		Vector3* tangents = (Vector3 *)tangentStream->m_data;
		Vector3* positions = (Vector3 *)positionStream->m_data;
		Uint8* boneIndices = (Uint8 *)skinningStream->m_data;

		Uint32 positionCount = positionStream->m_count;
		const auto indices = meshParts->m_bones[ meshIndex ].Indices();
		for( auto boneIndex : indices )
		{
			auto bone = meshParts->m_bones[ meshIndex ][ boneIndex ];

			Matrix transform = Matrix::IDENTITY();

			for( auto bodyInfo : meshParts->m_bodyInfos[ meshIndex ] )
			{
				if( bodyInfo.m_boneName == bone.m_name )
				{
					transform = bodyInfo.m_mappedBoneToBody;
					break;
				}
			}

			Matrix normalTransform( transform );
			normalTransform.OrthonormInvert();
			normalTransform.Transpose();
			for( Uint32 i = 0; i < positionCount; ++i )
			{
				if( boneIndices[ 4 * i ] == boneIndex )
				{
					positions[ i ] = transform.TransformPoint( positions[ i ] );
					normals[ i ] = normalTransform.TransformPoint( normals[ i ] );
					binormals[ i ] = normalTransform.TransformPoint( binormals[ i ] );
					tangents[ i ] = normalTransform.TransformPoint( tangents[ i ] );
				}
			}
		}
	

		meshParts->m_bones[ meshIndex ].Clear();
		meshParts->m_bodyInfos[ meshIndex ].Clear();
		red::DynArray<BodyInfo>& bodyInfos = meshParts->m_bodyInfos[ meshIndex ];
		BodyInfo bodyInfo = { CName::NONE(), Matrix::IDENTITY() };	
		bodyInfos.PushBack( bodyInfo );
		Uint32 indexSkinInd = red::BitUtils::BitScanForward( (Uint32)MST_SkinningIndices_4U8 );
		Uint32 indexSkinWeight = red::BitUtils::BitScanForward( (Uint32)MST_SkinningWeights_4F );

		if( chunk.m_streams.Size() > indexSkinWeight )
			chunk.m_streams.RemoveAt( indexSkinWeight );
		if( chunk.m_streams.Size() > indexSkinInd )
			chunk.m_streams.RemoveAt( indexSkinInd );

		chunk.m_streamMask &= ~MST_SkinningIndices_4U8;
		chunk.m_streamMask &= ~MST_SkinningIndices_4U8;

		chunk.m_vertexType = MVT_StaticMesh;
	}
}
