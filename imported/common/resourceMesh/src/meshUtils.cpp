#include "build.h"
#include "mesh.h"
#include "meshDataViewRO.h"
#include "meshUtils.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "meshParamPhysics.h"
#include "meshParamOccluderData.h"
#include "meshParamDestructionStepsData.h"

#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderVertices.h"

#include "../../physics/include/physicsSystemResource.h"
#include "../../physics/include/physicsSystemBody.h"
#include "../../physics/include/physicsDestructionStructs.h"
#endif

namespace mesh
{
	namespace utils
	{
		Float GetGlobalAutoHideDistance( const Box& bounds, const Vector3& surfaceAreaPerAxis, const Vector3& scale)
		{
			const Box scaledBox = bounds * scale;
			Float bboxDiagonal = scaledBox.CalcSize().Mag3();
			Float offset = 0.0f;

			if(surfaceAreaPerAxis.X > 0.0f || surfaceAreaPerAxis.Y > 0.0f || surfaceAreaPerAxis.Z > 0.0f )
			{
				const Vector3 scaledSurfaceArea = surfaceAreaPerAxis * Vector3( scale.Y * scale.Z, scale.X * scale.Z, scale.X * scale.Y );

				// Note: usually the auto hide distance calculated based only on triangle area is too low, so bumping it a bit
				const Float triangleAreaFactor = 1.5f;
				const Float virtualDiagonal = triangleAreaFactor * sqrtf( scaledSurfaceArea.X + scaledSurfaceArea.Y + scaledSurfaceArea.Z );

				bboxDiagonal = Min(bboxDiagonal, virtualDiagonal );

				// ctremblay: if one axis is almost zero, we most likely got a long object, like a cable. I'm adding radius to keep consistent autohide from side and in front.
				// It could be argued that the problem will still be there as front and side will never match. For cable it looks pretty as they are too tin pass that distance.
				// michal.witanowski got some other idea using sqrt or pow algorithm. However it too much work short term to tweak on all case.
				// Most likely Quad and Cable are ok to add this simple offset. To be revisted after E3 2020
				if( surfaceAreaPerAxis.X < 1e-5f || surfaceAreaPerAxis.Y < 1e-5f || surfaceAreaPerAxis.Z < 1e-5f )
				{
					offset = bboxDiagonal / 2;
				}
			}

			
			// ctremblay: Disabled for now. Too little time to fix. Might be check again post release.
			//const Float distance = Min( ( 64.0f * MLog10( 2.4f * bboxDiagonal + 1.0f ) + MPow( bboxDiagonal + 2.0f, 1.5f ) ), 16000.0f );
		
			Float distance = 0.0f;

			// using combination of logarithmic and exp functions,
			// so the small objects (less than 1m) would not disappear
			// that fast, also this function aligns to linear
			// after approx. value below
			if( bboxDiagonal < 28.6788f )
			{
				distance = 70.0f * MLog10( 2.2f * bboxDiagonal + 1.0f ) + MPow( bboxDiagonal + 1.0f, 1.8f );
			}
			else
			{
				// original function
				// this will make a 1x1x1 meter cube disappear when viewed from ~35 meters
				// (it covers 2.5% of screen height at 90 degree FoV then)
				distance = 20.0f * bboxDiagonal;
			}
			
			return Max(1.0f, distance) + offset;
		}

		void IterateGeometry( const DataViewRO& view, const IGeometryIterator* iterator, const Uint32 lodIndex /*= 0*/ )
		{
			PC_SCOPE( IterateGeometry ); // for those wishing to do it during the game :P

			TChunkIndices chunks;
			GetLODChunks( view, lodIndex, chunks );

			for( const auto chunkIndex : chunks )
			{
				const auto positionStream	= view.GetChunkStreamData( chunkIndex, MST_Position_3F );
				const auto indexStream		= view.GetChunkStreamData( chunkIndex, MST_Index_U16 );

				auto tFlags = view.GetChunkFlags( chunkIndex );

				if( !positionStream.Empty() && !indexStream.Empty() && tFlags.m_renderInScene )
				{
					StreamIteratorRO<Vector3> posVec( positionStream );

					constexpr Uint32 MAX_BATCHED_TRIS = 64;
					Vector3 tris[3 * MAX_BATCHED_TRIS];
					static_assert( sizeof(tris) < 4096, "Stack too big" );

					Uint32 trisInBatch = 0;

					for( StreamIteratorRO<Uint16> it( indexStream ); it; it += 3 )
					{
						// Do the geometry that overlaps with the Bbox only
						Box tIsTriangleOverlappingWithTheDFVolume = Box::EMPTY();

						for ( Uint32 indexID = 0; indexID < 3; indexID++ )
						{
							tIsTriangleOverlappingWithTheDFVolume.AddPoint( posVec[it[indexID]] );
						}

						if ( !iterator->Touches( tIsTriangleOverlappingWithTheDFVolume ) )
							continue;

						tris[trisInBatch * 3 + 0] = posVec[it[0]];
						tris[trisInBatch * 3 + 1] = posVec[it[1]];
						tris[trisInBatch * 3 + 2] = posVec[it[2]];

						if( ++trisInBatch == MAX_BATCHED_TRIS )
						{
							iterator->OnTriangle( tris, trisInBatch );

							trisInBatch = 0;
						}
					}

					if( trisInBatch )
					{
						iterator->OnTriangle( tris, trisInBatch );
					}
				}
			}
		}

		void ExtractGeometry(const DataViewRO& view, red::DynArray< Vector3 >& outVertices, red::DynArray< Uint32 >* outIndices, const Matrix& localToWorld /*= Matrix::IDENTITY()*/, const Uint32 lodIndex /*= 0*/)
		{
			PC_SCOPE(ExtractGeometry); // for those wishing to do it during the game :P

			Uint32 numVertices = 0;
			Uint32 numIndices = 0;

			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);
			for (const auto chunkIndex : chunks)
			{
				numVertices += view.GetChunkVertexCount(chunkIndex);
				numIndices += view.GetChunkIndexCount(chunkIndex);
			}

			const bool isIdentity = Matrix::Equal(localToWorld, Matrix::IDENTITY());

			outVertices.Reserve(outVertices.Size() + numVertices);

			if (outIndices != nullptr)
			{
				const auto indexBase = outIndices->Size();
				outIndices->Reserve(indexBase + numIndices);
			}

			for (const auto chunkIndex : chunks )
			{
				const auto vertexBase = outVertices.Size();

				// extract vertices
				const auto positionStream = view.GetChunkStreamData(chunkIndex, MST_Position_3F);
				if (isIdentity)
				{
					for (StreamIteratorRO<Vector3> it(positionStream); it; ++it)
						outVertices.PushBack(it.Get()); // NOTE: we should get a version of PushBack that does not have any checks nor resizing, this would speed up code like that a lot, Havok has pushBackUnsafe AFAIR
				}
				else
				{
					for (StreamIteratorRO<Vector3> it(positionStream); it; ++it)
						outVertices.PushBack(localToWorld.TransformPoint(it.Get()));
				}

				// extract indices
				if (outIndices != nullptr)
				{
					const auto indexStream = view.GetChunkStreamData(chunkIndex, MST_Index_U16);
					for (StreamIteratorRO<Uint16> it(indexStream); it; ++it)
						outIndices->PushBack(vertexBase + it.Get());
				}
			}
		}

        void ExtractNormals(const DataViewRO& view, red::DynArray< Vector3 >& outNormals, const Matrix& localToWorld, const Uint32 lodIndex)
		{
			PC_SCOPE(ExtractNormals);

			Uint32 numNormals = 0;

			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			for (const auto chunkIndex : chunks)
			{
				numNormals += view.GetChunkVertexCount(chunkIndex);
			}

			const bool isIdentity = Matrix::Equal(localToWorld, Matrix::IDENTITY());

			outNormals.Reserve(outNormals.Size() + numNormals);

			for (const auto chunkIndex : chunks )
			{
				const auto streamData = view.GetChunkStreamData(chunkIndex, MST_Normal_3F);
				if (isIdentity)
				{
					for (StreamIteratorRO<Vector3> it(streamData); it; ++it)
						outNormals.PushBack(it.Get());
				}
				else
				{
					for (StreamIteratorRO<Vector3> it(streamData); it; ++it)
						outNormals.PushBack(localToWorld.TransformPoint(it.Get()));
				}
			}
		}

        void ExtractUVs(const DataViewRO& view, red::DynArray< Vector2 >& outUVs, const Uint32 lodIndex)
        {
            PC_SCOPE(ExtractUVs);

			Uint32 numNormals = 0;

			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			for (const auto chunkIndex : chunks)
			{
				numNormals += view.GetChunkVertexCount(chunkIndex);
			}

			outUVs.Reserve(outUVs.Size() + numNormals);

			for (const auto chunkIndex : chunks )
			{
				const auto streamData = view.GetChunkStreamData(chunkIndex, MST_TexCoord0_2F);
				for (StreamIteratorRO<Vector2> it(streamData); it; ++it)
					outUVs.PushBack(it.Get());
			}
        }

		void GetBonePositions(const DataViewRO& view, red::DynArray< Vector3 >& outBonePositions)
		{
			const auto numBones = view.GetNumBones();

			outBonePositions.Reserve(outBonePositions.Size() + numBones);

			for (Uint32 i = 0; i < numBones; ++i)
			{
				const auto m = view.GetBoneMatrix(i);

				// This is extracted from doing matrix.Inverted().GetTranslation()
				Vector3 pos;
				pos.X = -(m[3][0] * m[0][0] + m[3][1] * m[0][1] + m[3][2] * m[0][2]);
				pos.Y = -(m[3][0] * m[1][0] + m[3][1] * m[1][1] + m[3][2] * m[1][2]);
				pos.Z = -(m[3][0] * m[2][0] + m[3][1] * m[2][1] + m[3][2] * m[2][2]);
				outBonePositions.PushBack(pos);
			}
		}

#if !defined( VANGUARD_ASSET_HOST )
			TRenderPtr<IRenderResource> CompileDebugMesh(const DataViewRO& view, const MeshDebugStyle renderStyle, const Uint32 lodLevel, const Color& debugColor)
		{
			TChunkIndices chunks;
			GetLODChunks(view, lodLevel, chunks);

			// No renderer
			if (!GRender)
				return nullptr;

			// Compute number of triangles/vertices in the source geometry
			Uint32 numSourceTriangles = 0;
			Uint32 numSourceVertices = 0;
			for (const auto chunkIndex : chunks)
			{
				numSourceVertices += view.GetChunkVertexCount(chunkIndex);
				numSourceTriangles += view.GetChunkIndexCount(chunkIndex) / 3;
			}

			// prepare arrays
			red::DynArray< DebugVertex > debugVertices{ red::PoolBackend() };
			red::DynArray< Uint32 > debugIndices{ red::PoolBackend() };
			debugVertices.Reserve(numSourceTriangles * 3);
			debugIndices.Reserve(numSourceTriangles * 3);

			// assemble debug mesh
			if (renderStyle == MeshDebugStyle::FaceNormals || renderStyle == MeshDebugStyle::FaceFakeLighting)
			{
				Vector4 lAmbient(40.0f, 40.0f, 40.0f);
				Vector4 lDir1(1.0f, 2.0f, 3.0f);
				Vector4 lColor1(debugColor.R, debugColor.G, debugColor.B, 255);
				Vector4 lDir2(-1.0f, -1.0f, -2.0f);
				Vector4 lColor2(128, 128, 128);
				lDir1.Normalize3();
				lDir2.Normalize3();

				for (const auto chunkIndex : chunks)
				{
					const StreamViewRO< Vector3 > vertexPositions(view.GetChunkStreamData(chunkIndex, MST_Position_3F));

					for (StreamIteratorRO<Uint16> it(view.GetChunkStreamData(chunkIndex, MST_Index_U16)); it; )
					{
						const auto& v0 = vertexPositions[*it++];
						const auto& v1 = vertexPositions[*it++];
						const auto& v2 = vertexPositions[*it++];

						// compute normal and color
						Color vertexColor(0, 0, 0);
						{
							const Vector4 vba = v1 - v0;
							const Vector4 vca = v2 - v0;
							Vector4 n = Vector4::Cross(vca, vba);
							if (n.Normalize3() > 0.0000001f)
							{
								if (renderStyle == MeshDebugStyle::FaceNormals)
								{
									vertexColor.R = (Uint8)Clamp< Float >(127.5f + 127.5f*n.X, 0.0f, 255.0f);
									vertexColor.G = (Uint8)Clamp< Float >(127.5f + 127.5f*n.X, 0.0f, 255.0f);
									vertexColor.B = (Uint8)Clamp< Float >(127.5f + 127.5f*n.X, 0.0f, 255.0f);
									vertexColor.A = debugColor.A;
								}
								else if (renderStyle == MeshDebugStyle::FaceFakeLighting)
								{
									const Float nDotL1 = Clamp< Float >(Vector4::Dot3(n, lDir1), 0.0f, 1.0f);
									const Float nDotL2 = Clamp< Float >(Vector4::Dot3(n, lDir2), 0.0f, 1.0f);
									vertexColor.R = (Uint8)Clamp< Float >(lAmbient.X + lColor1.X * nDotL1 + lColor2.X * nDotL2, 0.0f, 255.0f);
									vertexColor.G = (Uint8)Clamp< Float >(lAmbient.Y + lColor1.Y * nDotL1 + lColor2.Y * nDotL2, 0.0f, 255.0f);
									vertexColor.B = (Uint8)Clamp< Float >(lAmbient.Z + lColor1.Z * nDotL1 + lColor2.Z * nDotL2, 0.0f, 255.0f);
									vertexColor.A = debugColor.A;
								}
							}
						}

						const auto base = debugVertices.Size();
						debugVertices.EmplaceBack(v0, vertexColor);
						debugVertices.EmplaceBack(v1, vertexColor);
						debugVertices.EmplaceBack(v2, vertexColor);

						debugIndices.PushBack(base + 0);
						debugIndices.PushBack(base + 1);
						debugIndices.PushBack(base + 2);
					}
				}
			}
			else if (renderStyle == MeshDebugStyle::SolidColor)
			{
				const auto colorLinear = debugColor.ToVectorLinearAccu();
				for (const auto chunkIndex : chunks)
				{
					const StreamViewRO< Vector3 > vertexPositions(view.GetChunkStreamData(chunkIndex, MST_Position_3F));

					for (StreamIteratorRO<Uint16> it(view.GetChunkStreamData(chunkIndex, MST_Index_U16)); it; )
					{
						const auto& v0 = vertexPositions[*it++];
						const auto& v1 = vertexPositions[*it++];
						const auto& v2 = vertexPositions[*it++];

						const auto base = debugVertices.Size();
						debugVertices.EmplaceBack(v0, colorLinear);
						debugVertices.EmplaceBack(v1, colorLinear);
						debugVertices.EmplaceBack(v2, colorLinear);

						debugIndices.PushBack(base + 0);
						debugIndices.PushBack(base + 1);
						debugIndices.PushBack(base + 2);
					}
				}
			}
			else if (renderStyle == MeshDebugStyle::Wireframe)
			{
				const auto colorLinear = debugColor.ToVectorLinearAccu();
				for (const auto chunkIndex : chunks)
				{
					const StreamViewRO< Vector3 > vertexPositions(view.GetChunkStreamData(chunkIndex, MST_Position_3F));

					for (StreamIteratorRO<Uint16> it(view.GetChunkStreamData(chunkIndex, MST_Index_U16)); it; )
					{
						const auto& v0 = vertexPositions[*it++];
						const auto& v1 = vertexPositions[*it++];
						const auto& v2 = vertexPositions[*it++];

						const auto base = debugVertices.Size();
						debugVertices.EmplaceBack(v0, colorLinear);
						debugVertices.EmplaceBack(v1, colorLinear);
						debugVertices.EmplaceBack(v2, colorLinear);

						debugIndices.PushBack(base + 0);
						debugIndices.PushBack(base + 1);

						debugIndices.PushBack(base + 1);
						debugIndices.PushBack(base + 2);

						debugIndices.PushBack(base + 2);
						debugIndices.PushBack(base + 0);
					}
				}
			}

			// no data
			if (debugVertices.Empty() || debugIndices.Empty())
				return nullptr;

			// create debug mesh for rendering
				return GRender->UploadDebugMesh(debugVertices, debugIndices);
			}
#endif

		void GetLODChunks(const DataViewRO& view, const Uint32 lodIndex, TChunkIndices& outChunks)
		{
			if (lodIndex >= (Int32)view.GetNumLODs())
				return; // no geometry to extract from a LOD that is not there

			const auto numChunks = view.GetNumChunks();
			const auto lodMask = 1 << lodIndex;
			for (mesh::ChunkIndex i = 0; i < numChunks; ++i)
			{
				const auto chunkLodMask = view.GetChunkLodMask(i);
				if (0 != (chunkLodMask & lodMask))
				{
					outChunks.PushBack(i);
				}
			}
		}

		const Uint32 CountLODTriangles(const DataViewRO& view, const Uint32 lodIndex)
		{
			Uint32 numTriangles = 0;

			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			for (const auto chunkIndex : chunks)
				numTriangles += view.GetChunkIndexCount(chunkIndex) / 3;

			return numTriangles;
		}

		const Uint32 CountLODVertices(const DataViewRO& view, const Uint32 lodIndex)
		{
			Uint32 numVertices = 0;

			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			for (const auto chunkIndex : chunks)
				numVertices += view.GetChunkVertexCount(chunkIndex);

			return numVertices;
		}

		const Uint32 CountLODChunks(const DataViewRO& view, const Uint32 lodIndex)
		{
			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			return chunks.Size();
		}

		const Uint32 CountLODMaterials(const DataViewRO& view, const Uint32 lodIndex)
		{
			TChunkIndices chunks;
			GetLODChunks(view, lodIndex, chunks);

			red::StaticArray< CName, 1024 > usedMaterials;

			for (const auto chunkIndex : chunks)
			{
				const auto numMaterials = view.GetChunkMaterialCount(chunkIndex);
				for (Uint32 i = 0; i < numMaterials; ++i)
				{
					const auto materialName = view.GetChunkMaterialName(chunkIndex, i);
					red::alg::PushBackUnique(usedMaterials, materialName); // we don't have a lot of data in the mesh so this is acceptable
				}
			}

			return usedMaterials.Size(); // a waste
		}

#if !defined( VANGUARD_ASSET_HOST )
			THandle< physics::SystemResource > GetActivePhysics(const CMesh& meshData)
		{
			{
				auto param = meshData.GetParameter< mesh::MeshParamPhysics >();
				if( param )
					return param->GetPhysicsData();
			}

			return nullptr;
		}

		THandle< physics::ICollider > GetObstacle(const CMesh& meshData)
		{
			if(auto data = mesh::utils::GetActivePhysics(meshData))
			{
				for(auto& body : data->GetBodies())
				{
					for(auto& shape : body->m_collisionShapes)
					{
						if(shape->IsObstacle())
						{
							return shape;
						}
					}
				}
			}

			return nullptr;
		}

			Bool HasDestructibleData(const CMesh& meshData)
		{
			auto view = mesh::MakeView(meshData);
			if( view.GetNumBones() > 0 )
			{
				return view.HasParameter<mesh::MeshParamDestructionStepData>()  
					   && view.HasParameter<mesh::MeshParamDestructionBonds>()
					   && view.HasParameter<mesh::MeshParamDestructionBoneChunkMapping>();
			}

				return false;
			}
#endif

		const Int32 FindFirstNonEmptyLOD(const DataViewRO& view)
		{
			Uint32 chunkLodMask = 0;

			const Uint32 numChunks = view.GetNumChunks();
			for (Uint32 i = 0; i < numChunks; ++i)
				chunkLodMask |= view.GetChunkLodMask(i);

			const Uint32 numLODs = view.GetNumLODs();
			for (Uint32 i = 0; i < numLODs; ++i)
			{
				const auto lodMask = 1 << i;
				if (0 != (chunkLodMask & lodMask))
					return i;
			}

			return -1;
		}

		const Int32 FindLastNonEmptyLOD(const DataViewRO& view)
		{
			Uint32 chunkLodMask = 0;

			const Uint32 numChunks = view.GetNumChunks();
			for (Uint32 i = 0; i < numChunks; ++i)
				chunkLodMask |= view.GetChunkLodMask(i);

			const Uint32 numLODs = view.GetNumLODs();
			for (Int32 i=(Int32)numLODs-1; i >= 0; --i)
			{
				const auto lodMask = 1 << i;
				if (0 != (chunkLodMask & lodMask))
					return i;
			}

			return -1;
		}

		const Int32 FindMaterialIndex(const DataViewRO& view, const CName materialName)
		{
			const auto numMaterials = view.GetNumMaterials();
			for (Uint32 i = 0; i < numMaterials; ++i)
				if (view.GetMaterialName(i) == materialName)
					return i;

			return -1;
		}


#if !defined( VANGUARD_ASSET_HOST )
			THandle< vis::IOccluderResource > GetOccluderResource(const CMesh& meshData)
		{
			auto param = meshData.GetParameter< mesh::MeshParamOccluderData >();
			if ( param != nullptr )
			{
				return param->GetOccluderResource();
			}
				return nullptr;
			}
#endif

		// Find and return stream by type
		ChunkStreamDataRO* FindStream( red::DynArray<ChunkStreamDataRO>& streams, EMeshStreamType type )
		{
			ChunkStreamDataRO* stream = nullptr;

			for( auto& currentStream : streams )
			{
				if( currentStream.m_stream == type )
				{
					stream = &currentStream;
					break;
				}
			}
			return stream;
		}

        Bool HasSkinnedStreams( const DataViewRO& view )
        {
            Bool result = false;
            for( Uint32 ichunk = 0; ichunk < view.GetNumChunks(); ++ichunk )
            {
                const EMeshVertexType vtype = view.GetChunkVertexType( ichunk );
                if( MeshUtilities::HasSkinning( vtype ) )
                {
                    result = true;
                    break;
                }
            }
            return result;
        }

    } // utils
	
} // mesh
