#include "build.h"
#include "optimizeRoadMeshCommand.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../backendEngine/include/backendEngine.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceOptions.h"
#include "../../backendEngine/include/houdiniEngineUtils.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendMesh/include/meshDataBuilder.h"
#include "../../backendMesh/include/meshBackendUtils.h"

#include "../../../common/physics/include/physicsSystemResource.h"	
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physicsFilterData.h"
#include "../../../common/physics/include/physicsQueryFilter.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( OptimizeRoadMeshCommand, tools );
	RTTI_PARENT_TYPE( tools::GeneralResourceCommand );
RTTI_END_TYPE();

namespace tools
{
	class OptimizeRoadMeshOptions : public ResourceCommandOptions
	{
		RTTI_DECLARE_TYPE( OptimizeRoadMeshOptions );

	public:
		virtual Bool NeedOptions() const override
		{
			return false;
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////

	Bool OptimizeRoadMeshCommand::SupportsResource( const rtti::ClassType* resourceClass )
	{
		return resourceClass == ClassID< CMesh >();
	}

	CName OptimizeRoadMeshCommand::GetName() const
	{
		return RED_NAME_CONSTEXPR( "optimizeRoadMesh" );
	}

	ResourceCommandResult OptimizeRoadMeshCommand::Execute( const THandle<ResourceCommandOptions>& options )
	{
		return Optimize();
	}

	THandle< ResourceCommandOptions > OptimizeRoadMeshCommand::CreateOptions( const THandle< ResourceCommandOptions > suggestedOptions /*= nullptr */ ) const
	{
		return CreateHandle< OptimizeRoadMeshOptions >();
	}

	ResourceCommandResult OptimizeRoadMeshCommand::Optimize() const
	{
		res::LoadingOptions loadingOptions;
		loadingOptions.skipPostLoad = false;
		
		THandle< CMesh > inOutMesh = GResourceLoader->IssueLoadingRequest( Convert( GetResourcePath(), loadingOptions ) )->WaitUntilLoaded< CMesh >();
		auto resPath = GetResourcePath();
		if (!inOutMesh)
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to load %hs", resPath.ToDebugString() ) );
		}
		else
		{
			bool pathChk = false;
			auto path = resPath.ToString();
			if( path.Contains("sectors\\_external\\road_meshes\\")) pathChk = true;
			if (path.Contains( "sectors\\_external\\road_meshes\\bended\\" )) pathChk = false; //but not bent meshes (already optimized)

			//if (!pathChk)
			//{
			//	return ResourceCommandResult::Error( String::Printf( "This does not look like a road mesh to me: %hs", resPath.ToDebugString() ) );
			//}
		}

		THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( GetResourcePath(), true );
		if ( metadata && metadata->GetTags().Exist( RED_NAME_CONSTEXPR( "roadMeshOptimized2" ) ) )
		{
			return ResourceCommandResult::Success( inOutMesh, String::Printf( "already optimized %hs", GetResourcePath().ToDebugString() ) );
		}


		/****/
		bool ok = true;
		mesh::DataView meshDataView = mesh::DataView( *inOutMesh.Get() );
		houdini_legacy::GeometryBuffer optimizedMeshGeom;
		houdini_legacy::HoudiniEngineSession* session = nullptr;
		auto* backendEngine = Cast<CBackendEngine>( GEngine );
		if (session = backendEngine->GetHoudiniSession())
		{
			if (!houdini_legacy::IsSessionValid( session ))
			{
				if (!backendEngine->StartHoudiniEngine())
				{
					RED_LOG_WARNING( "Initializing FAILED - unable to start Houdini session using backend engine!" );
					session = nullptr;
				}
				else
				{
					session = backendEngine->GetHoudiniSession();
					if (!houdini_legacy::IsSessionValid( session ))
					{
						RED_LOG_WARNING( "Initializing FAILED - no Houdini session created!" );
						session = nullptr;
					}
				}
			}
		}
		
		if (!session)
		{
			return ResourceCommandResult::Error( String::Printf( "Couldn't get houdini engine session: %hs", GetResourcePath().ToDebugString() ) );
		}

		
		if (!houdini_legacy::RemoveFacingFaces( session, inOutMesh, optimizedMeshGeom, 1 ) || optimizedMeshGeom.m_vertices.Size() < 3)
		{
			ok=false;
		}
		else
		{

			String errorMsg;
			Int32 currentChunkID = 0;
			mesh::DataView meshDataView( *inOutMesh );
			mesh::DataBuilder buildData( *inOutMesh );
			Uint8 lodsCount = meshDataView.GetNumLODs();
			if (lodsCount > optimizedMeshGeom.m_lods.Size())
			{
				lodsCount = optimizedMeshGeom.m_lods.Size();
			}

			for (Uint8 lod = 0; lod < lodsCount; lod++)
			{
				Uint32 numChunksInLOD = optimizedMeshGeom.m_lods[lod].m_chunks.Size();
				auto lodChunkIndices = meshDataView.GetLODChunkIndices( lod );

				for (Uint32 chunkInLOD = 0; chunkInLOD < numChunksInLOD; ++chunkInLOD)
				{
					if (chunkInLOD >= lodChunkIndices.Size())
					{
						RED_LOG_ERROR( "RemoveFacingFacesInMesh, aborted - unrealiable results" );
						ok = false;
						break;
					}
					currentChunkID = lodChunkIndices[chunkInLOD];
					mesh::DataBuilder::UnpackedChunk& currentChunk = buildData.m_chunks[currentChunkID];
					auto& houdiniChunk = optimizedMeshGeom.m_lods[lod].m_chunks[chunkInLOD];

					Uint32 numTriangles = houdiniChunk.m_end - houdiniChunk.m_start;
					if (numTriangles == 0)
					{
						RED_LOG_ERROR( "RemoveFacingFacesInMesh, aborted - unrealiable results" );
						ok = false;
						break;
					}
					constexpr Uint32 VERTS_IN_TRIANGLE = 3u;

					red::DynArray< Uint32 > uniqueIndexes{ red::PoolBackend() };
					red::DynArray< Uint32 > chunkVertexIndexes{ red::PoolBackend() };
					// These arrays contain per-chunk streams
					red::DynArray< DebugVertex> chunkPositionsAndColors{ red::PoolBackend() };
					red::DynArray< Vector3 > chunkNormals{ red::PoolBackend() };
					red::DynArray< Vector2 > chunkUvs{ red::PoolBackend() };
					red::DynArray< Vector3 > chunkTangents{ red::PoolBackend() };
					red::DynArray< Vector3 > chunkBinormals{ red::PoolBackend() };

					// Check which streams should be imported
					EMeshVertexType oldVertexType = currentChunk.m_vertexType;
					EMeshVertexType newVertexType = oldVertexType;
					currentChunk.m_vertexType = newVertexType;


					Uint32 triangleInChunk = 0u;
					Uint32 idxInMesh = 0u;
					for (Uint32 triangleInChunk = 0u; triangleInChunk < numTriangles; ++triangleInChunk)
					{
						for (Uint8 vert = 0u; vert < VERTS_IN_TRIANGLE; ++vert)
						{
							Uint32 indexInChunkUnshared = triangleInChunk * VERTS_IN_TRIANGLE + vert;
							Uint32 indexHoudiniUnshared = ( triangleInChunk + houdiniChunk.m_start ) * VERTS_IN_TRIANGLE + vert;
							Uint32 idxHoudini = optimizedMeshGeom.m_indices[indexHoudiniUnshared];

							Int32 uniqueIndex = uniqueIndexes.GetIndex( idxHoudini );

							if (uniqueIndex == -1)
							{
								// Add a new vertex if a shared vertex hasn't existed yet
								uniqueIndexes.PushBack( idxHoudini );

								chunkPositionsAndColors.PushBack( optimizedMeshGeom.m_vertices[idxHoudini] );
								chunkNormals.PushBack( optimizedMeshGeom.m_normals[idxHoudini] );
								chunkUvs.PushBack( optimizedMeshGeom.m_uvs[idxHoudini] );
								chunkTangents.PushBack( optimizedMeshGeom.m_tangents[idxHoudini] );
								chunkBinormals.PushBack( optimizedMeshGeom.m_binormals[idxHoudini] );
								chunkVertexIndexes.PushBack( idxInMesh );
								idxInMesh++;
							}
							else
							{
								Uint32 val = static_cast<Uint32>( uniqueIndex );
								chunkVertexIndexes.PushBack( val );
							}
						}
					}

					Uint32 numUniqueIndexesSet = uniqueIndexes.Size();
					currentChunk.m_streams.Clear();
					mesh::WriteToStream< Uint32, Uint16 >( currentChunk.m_streams, MST_Index_U16, chunkVertexIndexes.Size(), chunkVertexIndexes, true );

					mesh::UnpackedStream* stream = &currentChunk.m_streams.EmplaceBack();
					stream->m_type = MST_Position_3F;

					mesh::StreamWriter< Vector3 > writer( stream->m_data, chunkPositionsAndColors.Size() );
					for (const auto& val : chunkPositionsAndColors)
					{
						writer.Write( Vector3( val.x, val.y, val.z ) );
					}

					mesh::WriteToStream< Vector3 >( currentChunk.m_streams, MST_Normal_3F, chunkNormals.Size(), chunkNormals, true );
					mesh::WriteToStream< Vector2 >( currentChunk.m_streams, MST_TexCoord0_2F, chunkUvs.Size(), chunkUvs, true );
					mesh::WriteToStream< Vector3 >( currentChunk.m_streams, MST_Tangent_3F, chunkTangents.Size(), chunkTangents, true );
					mesh::WriteToStream< Vector3 >( currentChunk.m_streams, MST_Binormal_3F, chunkBinormals.Size(), chunkBinormals, true );
				}
			}
			// Update the mesh
			mesh::DataBuilder::FlushOptions flushOptions;
			flushOptions.m_rebuildMaterials = false;
			flushOptions.m_matchByOrder = true;
			flushOptions.m_uploadResource = true;
			flushOptions.m_updateStreaming = false;
			buildData.Flush( *inOutMesh, flushOptions );

			////

			res::ResourceMetadata::TFileTags& tags = metadata->GetTags();
			tags.PushBack( RED_NAME_CONSTEXPR( "roadMeshOptimized2" ) );
			res::ResourceMetadata::Set( inOutMesh, metadata );

			meshDataView.Apply();
		}


		// **** opt colls part **** //
		if (auto param = inOutMesh->GetParameter< mesh::MeshParamPhysics >())
		{
			using MergedColl = struct
			{
				String hash;
				red::DynArray< Uint16 > inds;
				red::DynArray < Vector3> pos;
				String matName;
				String presetName;
				red::DynArray< Uint16 > matInds;
				red::DynArray< red::String > matNames;
			};
			red::DynArray< MergedColl > mergedCollisions{ red::PoolBackend() };

			Uint32 collisionTriangles = 0;
			Box bounds = Box::EMPTY();

			red::DynArray< std::tuple<String, physics::ExtractedCollisionGeometry, String, String, bool, bool>> collGeos{ red::PoolBackend() };
			// 0 hash, 1 geometry, 2 material name, 3 collision preset, 4 can skip bool

			auto& inputMesh = *inOutMesh;

			THandle< physics::SystemResource > physicsData = param->GetPhysicsData();

			for (THandle< physics::SystemBody > body : physicsData->GetBodies())
			{
				const auto bodyToWorld = body->m_localToModel.ToMatrix();
				bool hasComplexColider = false;
				bool hasSimpleColider = false;
				for (auto shape : body->m_collisionShapes)
				{
					String physMatFilter = "World Static";
					if (shape->m_filterData)
					{
						physMatFilter = shape->m_filterData->GetPreset().AsChar();
					}

					if (physMatFilter == "World Static" || physMatFilter == "Complex Environment Collision")
					{
						hasComplexColider = true;
					}

					if (physMatFilter == "Simple Environment Collision")
					{
						hasSimpleColider = true;
					}
				}

				for (auto shape : body->m_collisionShapes)
				{
					String physMatFileName = shape->m_material.AsChar();
					String physMatFilter = "World Static";
					bool isSimpleCollider = false;
					if (shape->m_filterData)
					{
						physMatFilter = shape->m_filterData->GetPreset().AsChar();
					}
					if (physMatFilter == "Simple Environment Collision")
					{
						isSimpleCollider = true;
					}
					String physMatName;
					if (!physMatFileName.Split( red::DynArray< String >{"."}, &physMatName, nullptr ))
					{
						physMatName = "default";
					}
					physMatName += ".physmat";

					if (auto cm = Cast<physics::ColliderMesh>( shape )) //This is because ExtractGeometry does weird things for ColliderMesh (duplicates faces, flips faces, etc)
					{
						auto cMesh = Cast<physics::ColliderMesh>( shape );
						const Matrix localToModel = cMesh->m_localToBody.ToMatrix() * bodyToWorld;

						if (cm->m_faceMaterials.Size() > 1)
						{
							for (Uint32 m = 0; m < cm->m_faceMaterials.Size(); m++)
							{
								String physMatName2 = cm->m_faceMaterials[m].AsStringView().ToString();
								physics::ExtractedCollisionGeometry outGeometry;
								outGeometry.m_vertices.Reserve( cMesh->m_points.Size() );
								outGeometry.m_indices.Reserve( cMesh->m_indices.Size() );
								for (Uint32 i : cMesh->m_points.Indices())
								{
									const auto& sourcePoint = cMesh->m_points[i];
									outGeometry.m_vertices.PushBack( localToModel.TransformPoint( sourcePoint ) );
								}

								for (Uint32 i = 0; i < cMesh->m_indices.Size(); i++)
								{
									if (cm->m_faceMaterialIndices[i / 3] == m)
									{
										outGeometry.m_indices.PushBack( cMesh->m_indices[i] );
									}
								}
								collGeos.PushBack( { physMatFilter + physMatName2, outGeometry, physMatName2, physMatFilter, isSimpleCollider && hasComplexColider, !isSimpleCollider && hasSimpleColider } );
								collisionTriangles += outGeometry.m_indices.Size() / 3;
							}
						}
						else
						{
							physics::ExtractedCollisionGeometry outGeometry;
							outGeometry.m_vertices.Reserve( cMesh->m_points.Size() );
							outGeometry.m_indices.Reserve( cMesh->m_indices.Size() );

							for (Uint32 i : cMesh->m_points.Indices())
							{
								const auto& sourcePoint = cMesh->m_points[i];
								outGeometry.m_vertices.PushBack( localToModel.TransformPoint( sourcePoint ) );
							}
							for (Uint32 i : cMesh->m_indices)
							{
								outGeometry.m_indices.PushBack( i );
							}
							collGeos.PushBack( { physMatFilter + physMatName, outGeometry, physMatName, physMatFilter, isSimpleCollider && hasComplexColider, !isSimpleCollider && hasSimpleColider } );
							collisionTriangles += outGeometry.m_indices.Size() / 3;
						}

					}
					else if ((Int32)shape->IsA< physics::ICollider >())
					{
						physics::ExtractedCollisionGeometry outGeometry;
						shape->ExtractGeometry( bodyToWorld, outGeometry );
						collGeos.PushBack( { physMatFilter + physMatName, outGeometry, physMatName, physMatFilter, isSimpleCollider && hasComplexColider, !isSimpleCollider && hasSimpleColider } );
						collisionTriangles += outGeometry.m_indices.Size() / 3;
					}
				}
			}

			//Merge collisions per preset and material 
			auto sortingPredicate = []( const std::tuple<String, physics::ExtractedCollisionGeometry, String, String, bool, bool >& first,
				const std::tuple<String, physics::ExtractedCollisionGeometry, String, String, bool, bool >& second )
			{
				const char* cFirst = std::get<0>( first ).AsChar();
				const char* cSecond = std::get<0>( second ).AsChar();
				return std::strcmp( cFirst, cSecond ) < 0;
			};
			std::sort( collGeos.Begin(), collGeos.End(), sortingPredicate );
			String collHash = "";
			Uint32 indOffset = 0, indCnt = 0, vertCnt = 0;

			session->m_saveDebugScene = true;

			// houdini collision optimizer 
			houdini_legacy::GeometryBuffer optimizedCollisionGeom;
			red::DynArray<houdini_legacy::GeometryBuffer> geometry;
			houdini_legacy::GeometryBuffer *geom = nullptr;

			for (auto& coll : collGeos)
			{
				//skip simple that have also complex
				if (std::get<4>( coll )) continue;

				if (std::get<0>( coll ) != collHash)
				{
					geom = &geometry.EmplaceBack();

					collHash = std::get<0>( coll );
					indOffset = 0; indCnt = 0; vertCnt = 0;
					geom->m_materialName = std::get<2>( coll );
				}
				else
				{
					geom = &geometry[geometry.Size() - 1];
				}

				for (auto i : std::get<1>( coll ).m_indices)
				{
					geom->m_indicesCollision.PushBack( indOffset + i );
				}

				for (auto v : std::get<1>( coll ).m_vertices)
				{
					geom->m_verticesCollision.PushBack( v );
				}

				indOffset += std::get<1>( coll ).m_vertices.Size();
			}

			Bool success = houdini_legacy::SimpleMergeCollisions( session, geometry, 30, {}, optimizedCollisionGeom, false, 1 );
			houdini_legacy::Close( session );
			if (!success || optimizedCollisionGeom.m_lods.Empty())
			{
				return ResourceCommandResult::Error( String::Printf( "Unable to optimize collision for '%hs' on houdini partd", GetResourcePath().ToDebugString() ) );
			}
			else
			{
				auto& mergedColl = mergedCollisions.EmplaceBack();
				mergedColl.presetName = "World Static";
				mergedColl.inds = std::move( optimizedCollisionGeom.m_indicesCollision );
				mergedColl.pos = std::move( optimizedCollisionGeom.m_verticesCollision );
				mergedColl.matNames.Reserve( optimizedCollisionGeom.m_lods[0].m_chunks.Size() );
				mergedColl.matInds.Resize( optimizedCollisionGeom.m_indices.Size() / 3 );
				for (Uint32 idx : optimizedCollisionGeom.m_lods[0].m_chunks.Indices())
				{
					const auto& chunk = optimizedCollisionGeom.m_lods[0].m_chunks[idx];
					mergedColl.matNames.PushBack( chunk.m_material );

					for (Uint32 i = chunk.m_start; i < chunk.m_end; ++i)
					{
						mergedColl.matInds[i] = idx;
					}
				}
			}

			if (mergedCollisions.Size() > 0)
			{
				if (auto param = inOutMesh->GetParameter< mesh::MeshParamPhysics >())
				{
					inOutMesh->RemoveParameter( param );
				}
				auto param = inOutMesh->CreateParameter< mesh::MeshParamPhysics >();
				THandle< physics::SystemResource > physics = param->GetPhysicsData();
				if (physics == nullptr)
				{
					physics = CreateHandle< physics::SystemResource >();
				}

				physics::SystemBodyBuildData bodyInfo;
				bodyInfo.m_name = RED_NAME_CONSTEXPR( "MergedColls" );
				bodyInfo.m_params.m_simulationType = physics::SimulationType::Static;
				bodyInfo.m_localToModel = Transform::IDENTITY();

				for (auto& mergedColl : mergedCollisions)
				{
					auto shape = physics::ColliderMesh::Create( mergedColl.pos.TypedData(), sizeof( Vector3 ), mergedColl.pos.Size(),
						mergedColl.inds.TypedData(), mergedColl.inds.Size(), false );

					if (!shape)
					{
						return ResourceCommandResult::Error( String::Printf( "Unable to create collider shape for '%hs'", GetResourcePath().ToDebugString() ) );
					}
					auto& faceMaterials = shape->m_faceMaterials;
					auto& faceMaterialIndices = shape->m_faceMaterialIndices;
					faceMaterials.Reserve( mergedColl.matNames.Size() );
					faceMaterialIndices.Reserve( mergedColl.matInds.Size() );

					for (Uint32 idx : mergedColl.matInds)
					{
						faceMaterialIndices.PushBack( idx );
					}
					for (const auto& matname : mergedColl.matNames)
					{
						faceMaterials.PushBack( RED_NAME( matname ) );
					}
					shape->m_filterData = CreateHandle< physics::FilterData >();
					shape->m_filterData->ApplyPreset( RED_NAME( mergedColl.presetName ) );

					bodyInfo.m_collisionShapes.PushBack( shape );
				}

				auto body = physics::SystemBody::Create( bodyInfo );
				if (body)
				{
					physics->AttachObject( body );
				}
				param->SetPhysicsData( physics );
			}
		}

		////*******************
		
		if(!ok)
		{
			return ResourceCommandResult::Error( String::Printf( "Couldn't optimize %hs", GetResourcePath().ToDebugString() ) );
		}

		if ( !res::SaveResource( inOutMesh ) )
		{
			return ResourceCommandResult::Error( String::Printf( "Unable to optimize '%hs' because saving failed", GetResourcePath().ToDebugString() ) );
		}

		return ResourceCommandResult::Success( inOutMesh );
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( OptimizeRoadMeshOptions, tools );
	RTTI_PARENT_TYPE( ResourceCommandOptions );
RTTI_END_TYPE();