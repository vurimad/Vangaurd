#include "build.h"

#include "meshVisualization.h"
#include "meshStructure.h"
#include "meshStructureVisualization.h"

#include "../../../common/renderData/include/renderObject.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/redMath/include/plane.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/resourceMesh/include/meshParamWorkspotOffsets.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"

namespace Config
{
	//TConfigVar< Float > cvMeshEditorPointSize("MeshEditor", "PointSize", 0.0018f);
	//TConfigVar< Float > cvMeshEditorVectorSize("MeshEditor", "VectorSize", 0.004f);
	extern TConfigVar< Float > cvMeshEditorPointSize;
	extern TConfigVar< Float > cvMeshEditorVectorSize;
}

namespace tools
{
	//////////////////////////////////////////////////////////////////////////////////////////////

	class Heatmap
	{
	public:
		Color m_colors[256];

		Heatmap()
		{
			for ( Uint32 i = 0; i < 64; ++i )
			{
				const Float x = (Float)i / 64.0f;
				m_colors[0 + i] = math::Lerp<Vector4>( x, Color::WHITE().ToVector(), Color::BLUE().ToVector() );
			}

			for ( Uint32 i = 0; i < 64; ++i )
			{
				const Float x = (Float)i / 64.0f;
				m_colors[64 + i] = math::Lerp<Vector4>( x, Color::BLUE().ToVector(), Color::GREEN().ToVector() );
			}

			for ( Uint32 i = 0; i < 64; ++i )
			{
				const Float x = (Float)i / 64.0f;
				m_colors[128 + i] = math::Lerp<Vector4>( x, Color::GREEN().ToVector(), Color::YELLOW().ToVector() );
			}

			for ( Uint32 i = 0; i < 64; ++i )
			{
				const Float x = (Float)i / 64.0f;
				m_colors[192 + i] = math::Lerp<Vector4>( x, Color::YELLOW().ToVector(), Color::RED().ToVector() );
			}
		}
	};

	static Heatmap theHeatmap;

	static Color ColorizeBoneInfluence( const Float weight )
	{
		const Int32 key = (Int32)math::Clamp< Float >( weight * 255.0f, 0.0f, 255.0f );
		return theHeatmap.m_colors[key];
	}

	static Color ColorizeBoneInfluence( const Color& indices, const Vector4& weights, const Int32 boneIndex )
	{
		for ( Uint32 i = 0; i < 4; ++i )
		{
			if ( indices.RGBA[i] == boneIndex )
				return ColorizeBoneInfluence( weights[i] );
		}

		return Color::WHITE();
	}

	static Color ColorizeBoneInfluenceExt( const Color& indices, const Vector4& weights, const Color& indicesExt, const Vector4& weightsExt, const Int32 boneIndex )
	{
		for ( Uint32 i = 0; i < 4; ++i )
		{
			if ( indices.RGBA[i] == boneIndex )
				return ColorizeBoneInfluence( weights[i] );
			if ( indicesExt.RGBA[i] == boneIndex )
				return ColorizeBoneInfluence( weightsExt[i] );
		}

		return Color::WHITE();
	}

	TRenderPtr<IRenderResource> CreateMeshSkinHeatMap( const THandle< CMesh >& meshData, Int32 boneIndex, Int32 chunkIndexFilter )
	{
		const ::mesh::DataViewRO sourceView(*meshData);
		if (sourceView.GetNumLODs() == 0)
			return nullptr;

		// Compute number of triangles/vertices in the source geometry
		Uint32 numSourceTriangles = 0;
		Uint32 numSourceVertices = 0;

		red::DynArray< Uint32  > sourceChunks;
		if (chunkIndexFilter == -1)
		{
			for (Uint32 i = 0; i < sourceView.GetNumChunks(); ++i )
			{
				numSourceVertices += sourceView.GetChunkVertexCount(i);
				numSourceTriangles += sourceView.GetChunkIndexCount(i) / 3;
				sourceChunks.PushBack(i);
			}
		}
		else if (chunkIndexFilter >= 0 && chunkIndexFilter < (Int32)sourceView.GetNumChunks() )
		{
			numSourceVertices += sourceView.GetChunkVertexCount(chunkIndexFilter);
			numSourceTriangles += sourceView.GetChunkIndexCount(chunkIndexFilter) / 3;
			sourceChunks.PushBack(chunkIndexFilter);
		}

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceTriangles * 3);
		debugIndices.Reserve(numSourceTriangles * 3);

		// build the mesh, encode the influences with colors
		for (const auto chunkIndex : sourceChunks)
		{
			const mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndex, MST_Index_U16));
			const mesh::StreamViewRO<Vector3> dataPositions(sourceView.GetChunkStreamData(chunkIndex, MST_Position_3F));
			const mesh::StreamViewRO<Color> dataSkinIndices(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningIndices_4U8));
			const mesh::StreamViewRO<Vector4> dataSkinWeights(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningWeights_4F));
			const mesh::StreamViewRO<Color> dataSkinIndicesExt(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningIndicesExt_4U8));
			const mesh::StreamViewRO<Vector4> dataSkinWeightsExt(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningWeightsExt_4F));
			for (Uint32 i = 0; i < dataIndices.Size(); i += 3)
			{
				const Uint16 i0 = dataIndices[i + 0];
				const Uint16 i1 = dataIndices[i + 1];
				const Uint16 i2 = dataIndices[i + 2];

				const auto& p0 = dataPositions[i0];
				const auto& p1 = dataPositions[i1];
				const auto& p2 = dataPositions[i2];

				// compute normal and color
				Color vertexColorA, vertexColorB, vertexColorC;
				if (dataSkinIndices.IsValid() && dataSkinWeights.IsValid() && dataSkinIndicesExt.IsValid() && dataSkinWeightsExt.IsValid())
				{
					vertexColorA = ColorizeBoneInfluenceExt(dataSkinIndices[i0], dataSkinWeights[i0], dataSkinIndicesExt[i0], dataSkinWeightsExt[i0], boneIndex);
					vertexColorB = ColorizeBoneInfluenceExt(dataSkinIndices[i1], dataSkinWeights[i1], dataSkinIndicesExt[i1], dataSkinWeightsExt[i1], boneIndex);
					vertexColorC = ColorizeBoneInfluenceExt(dataSkinIndices[i2], dataSkinWeights[i2], dataSkinIndicesExt[i2], dataSkinWeightsExt[i2], boneIndex);
				}
				else if (dataSkinIndices.IsValid() && dataSkinWeights.IsValid())
				{
					vertexColorA = ColorizeBoneInfluence(dataSkinIndices[i0], dataSkinWeights[i0], boneIndex);
					vertexColorB = ColorizeBoneInfluence(dataSkinIndices[i1], dataSkinWeights[i1], boneIndex);
					vertexColorC = ColorizeBoneInfluence(dataSkinIndices[i2], dataSkinWeights[i2], boneIndex);
				}
				else
				{
					vertexColorA = Color::WHITE();
					vertexColorB = Color::WHITE();
					vertexColorC = Color::WHITE();
				}

				const auto vertexBase = debugVertices.Size();
				debugVertices.EmplaceBack(p0, vertexColorA);
				debugVertices.EmplaceBack(p1, vertexColorB);
				debugVertices.EmplaceBack(p2, vertexColorC);

				debugIndices.PushBack(vertexBase + 0);
				debugIndices.PushBack(vertexBase + 1);
				debugIndices.PushBack(vertexBase + 2);
			}
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

	static Bool ChunkHasMaterial(const ::mesh::DataViewRO& sourceView, const Uint32 chunkIndex, CName materialIndexFilter)
	{
		if (!materialIndexFilter)
			return true;

		const auto numMaterials = sourceView.GetChunkMaterialCount(chunkIndex);
		for (Uint32 k = 0; k < numMaterials; ++k)
		{
			const auto materialName = sourceView.GetChunkMaterialName(chunkIndex, k);
			if (materialName == materialIndexFilter)
				return true;
		}

		return false;
	}

	TRenderPtr<IRenderResource> CreateMeshFiltered(THandle< CMesh > meshData, Int32 chunkIndexFilter, CName materialIndexFilter)
	{
		const ::mesh::DataViewRO sourceView(*meshData);
		if (sourceView.GetNumLODs() == 0)
			return nullptr;

		red::DynArray< Uint32 > sourceChunks;

		// Compute number of triangles/vertices in the source geometry
		Uint32 numSourceTriangles = 0;
		Uint32 numSourceVertices = 0;
		if (chunkIndexFilter == -1)
		{
			const auto numChunks = sourceView.GetNumChunks();
			for (Uint32 i = 0; i < numChunks; ++i)
			{
				const auto lodMask = sourceView.GetChunkLodMask(i);
				if ( (lodMask & 1) != 1 ) // LOD0 only
					continue;

				if ( !ChunkHasMaterial(sourceView, i, materialIndexFilter) )
					continue;

				numSourceVertices += sourceView.GetChunkVertexCount(i);
				numSourceTriangles += sourceView.GetChunkIndexCount(i) / 3;
				sourceChunks.PushBack(i);
			}
		}
		else if (chunkIndexFilter >= 0 && chunkIndexFilter < (Int32)sourceView.GetNumChunks())
		{
			numSourceVertices += sourceView.GetChunkVertexCount(chunkIndexFilter);
			numSourceTriangles += sourceView.GetChunkIndexCount(chunkIndexFilter) / 3;
			sourceChunks.PushBack(chunkIndexFilter);
		}

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceTriangles * 3);
		debugIndices.Reserve(numSourceTriangles * 3);

		// light direction
		const auto lightDir = Vector3(1, 2, 3).Normalized();

		struct Point3D
		{
			Vector3 m_vec;

			Point3D( const Vector3& vec )
				: m_vec( vec )
			{}

			RED_FORCE_INLINE Uint32 CalcHash() const
			{
				Uint32 x = (Uint32&)m_vec.X;
				Uint32 y = (Uint32&)m_vec.Y;
				Uint32 z = (Uint32&)m_vec.Z;
				return ( x ^ y ^ z );
			}

			RED_FORCE_INLINE Bool operator==( const Point3D& other ) const
			{
				return m_vec == other.m_vec;
			}
		};

		// build the mesh, encode the influences with colors
		for (const auto chunkIndex : sourceChunks)
		{
			const mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndex, MST_Index_U16));
			const mesh::StreamViewRO<Vector3> dataPositions(sourceView.GetChunkStreamData(chunkIndex, MST_Position_3F));

			const mesh::StreamViewRO<Vector3> dataNormals(sourceView.GetChunkStreamData(chunkIndex, MST_Normal_3F));
			red::HashMap<Point3D, Vector3> posToNormals;
			for (Uint32 i = 0; i < dataPositions.Size(); ++i)
			{
				Point3D posKey(dataPositions[i]);
				if (posToNormals.KeyExist(posKey))
				{
					Vector3& normal = posToNormals.GetRef(posKey);
					normal = (normal + (dataNormals.Size() ? dataNormals[i] : dataPositions[ i ])) / 2.f;
					normal.Normalize();
				}
				else
				{
					if( dataNormals.Size() )
						posToNormals.Insert( posKey, dataNormals[ i ].Normalized() );
					else
						posToNormals.Insert( posKey, dataPositions[ i ].Normalized() );
				}
			}

			for (Uint32 i = 0; i < dataIndices.Size(); i += 3)
			{
				const Vector3& pos0 = dataPositions[dataIndices[i + 0]],
					pos1 = dataPositions[dataIndices[i + 1]],
					pos2 = dataPositions[dataIndices[i + 2]];

				const auto& p0 = pos0 + posToNormals[Point3D(pos0)] * 0.01f;
				const auto& p1 = pos1 + posToNormals[Point3D(pos1)] * 0.01f;
				const auto& p2 = pos2 + posToNormals[Point3D(pos2)] * 0.01f;

				const auto plane = math::Plane(p0, p1, p2);
				const auto dot = 0.5f + 0.5f * std::max<Float>(0.0f, lightDir.Dot(plane.NormalDistance));
				const Color color(Vector4(0.0f, dot, 0.0f, 0.5f));

				const auto vertexBase = debugVertices.Size();
				debugVertices.EmplaceBack(p0, color);
				debugVertices.EmplaceBack(p1, color);
				debugVertices.EmplaceBack(p2, color);

				debugIndices.PushBack(vertexBase + 0);
				debugIndices.PushBack(vertexBase + 1);
				debugIndices.PushBack(vertexBase + 2);
			}
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

	TRenderPtr<IRenderResource> CreateMeshStreamPositions(THandle< CMesh > meshData, Int32 chunkIndexFilter)
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const auto data = sourceView.GetChunkStreamData(chunkIndexFilter, MST_Position_3F);
		Uint32 numSourceVertices = data.m_count;

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceVertices * 4);
		debugIndices.Reserve(numSourceVertices * 12);

		const auto color = Color::MAGENTA();
		const auto scale = Config::cvMeshEditorPointSize.Get() * meshData->GetBoundingBox().CalcSize().Mag3();

		// build the mesh, encode the influences with colors
		for (mesh::StreamIteratorRO<Vector3> it(data); it; ++it)
		{
			const auto c = *it;
			const auto base = debugVertices.Size();

			debugVertices.PushBack(DebugVertex(c + Vector3(0, 0, 1)*scale, color));
			debugVertices.PushBack(DebugVertex(c + Vector3(0.943f, 0.000f, -0.333f)*scale, color));
			debugVertices.PushBack(DebugVertex(c + Vector3(-0.471f, 0.816f, -0.333f)*scale, color));
			debugVertices.PushBack(DebugVertex(c + Vector3(-0.471f, -0.816f, -0.333f)*scale, color));

			debugIndices.PushBack(base+2);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 3);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	TRenderPtr<IRenderResource> CreateMeshStreamColor(THandle< CMesh > meshData, Int32 chunkIndexFilter)
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const ::mesh::StreamViewRO<Vector3> dataPosition(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Position_3F));
		const ::mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Index_U16));
		const ::mesh::StreamViewRO<Uint32> dataColor(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Color_U32));

		Uint32 numSourceIndices = dataIndices.Size();
		Uint32 numSourceVertices = dataPosition.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceVertices);
		debugIndices.Reserve(numSourceIndices);

		for (Uint32 i = 0; i < numSourceVertices; ++i)
		{
			const auto v = dataPosition[i];
			const auto c = dataColor[i];
			debugVertices.EmplaceBack( v, Color( c ).ToVectorLinearAccu() );
		}

		for (Uint32 i = 0; i < numSourceIndices; ++i)
		{
			debugIndices.PushBack(dataIndices[i]);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	const Float c_morphOffsetScaleOvershootLimit = 2.5f;												/// @todo DP: share this with garmentUtils code, dependencies issue

	TRenderPtr<IRenderResource> CreateMeshStreamGarmentFlags( THandle< CMesh > meshData, Int32 chunkIndexFilter, red::ArraySpan< const Uint8 > morphOffsetScales, Bool visualizeCaps )
	{
		const ::mesh::DataViewRO sourceView( *meshData );

		const ::mesh::StreamViewRO<Vector3> dataPosition( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Position_3F ) );
		const ::mesh::StreamViewRO<Vector3> dataNormal( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Normal_3F ) );
		const ::mesh::StreamViewRO<Uint16> dataIndices( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Index_U16 ) );
		const ::mesh::StreamViewRO<Uint32> dataGarmentFlags( sourceView.GetChunkStreamData( chunkIndexFilter, MST_GarmentFlags_U32 ) );
		const red::ArraySpan< const Vector3 > morphOffsets = mesh::MakeSpan< const Vector3 >( sourceView.GetChunkStreamData( chunkIndexFilter, MST_MorphOffset_3F ) );

		Uint32 numSourceIndices = dataIndices.Size();
		Uint32 numSourceVertices = dataPosition.Size();

		Bool hasNormals = (dataNormal.Size() >= numSourceVertices);
		Bool hasGarmentFlags = (dataGarmentFlags.Size() >= numSourceVertices);
		Bool hasMorphOffsets = (morphOffsets.Size() >= numSourceVertices) && (morphOffsetScales.Size() >= numSourceVertices);

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve( numSourceVertices );
		debugIndices.Reserve( numSourceIndices );

		Color colorFarFromBorder = Color::WHITE();
		Color colorCloserToBorder = Color::BLUE();
		Color colorOnBorder = Color::MAGENTA();
		Color colorOnCap = Color::RED();
																								/// @todo DP: make normal offset configurable
		for ( Uint32 i = 0; i < numSourceVertices; ++i )
		{
			const Vector3 n = hasNormals ? dataNormal[i] : Vector3( 0.f, 0.f, 1.f );
			const Uint32 flags = hasGarmentFlags ? dataGarmentFlags[i] : 0;
			const Uint32 garmentBorderProximity = (flags & 0xFF);
			const Vector3 morphOffset = hasMorphOffsets ? morphOffsets[i] : Vector3::ZEROS();
			const Float morphOffsetScale = hasMorphOffsets ? morphOffsetScales[i] : 0.0f;
			const Float unpackedMorphOffsetScale = morphOffsetScale * (c_morphOffsetScaleOvershootLimit / 255.0f);
			const Vector3 v = dataPosition[i] + morphOffset * unpackedMorphOffsetScale;
			const Vector3 p = v + n * ( hasNormals ? 0.001f : 0.f ); // Offset a little to avoid z-fighting.
			Color c;
			if ( garmentBorderProximity < 128 )
			{
				Float lerpFactor = static_cast<Float>(garmentBorderProximity) * (1.0f / 128.0f);
				lerpFactor = math::Clamp( lerpFactor, 0.f, 1.f );
				c = Color::Lerp( lerpFactor, colorFarFromBorder, colorCloserToBorder );
			}
			else
			{
				Float lerpFactor = static_cast<Float>(garmentBorderProximity - 128) * (1.0f / 127.0f);
				lerpFactor = math::Clamp( lerpFactor, 0.f, 1.f );
				c = Color::Lerp( lerpFactor, colorCloserToBorder, colorOnBorder );
			}
			if ( visualizeCaps && ((flags & 256u) != 0) )
				c = colorOnCap;

			debugVertices.EmplaceBack( p, c );
		}

		for ( Uint32 i = 0; i < numSourceIndices; ++i )
		{
			debugIndices.PushBack( dataIndices[i] );
		}

		// no data
		if ( debugVertices.Empty() || debugIndices.Empty() )
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh( debugVertices, debugIndices );
	}

	TRenderPtr<IRenderResource> CreateMeshStreamGarmentCaps( THandle< CMesh > meshData, Int32 chunkIndexFilter, red::ArraySpan< const Uint8 > morphOffsetScales )
	{
		const ::mesh::DataViewRO sourceView( *meshData );

		const ::mesh::StreamViewRO<Vector3> dataPosition( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Position_3F ) );
		const ::mesh::StreamViewRO<Vector3> dataNormal( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Normal_3F ) );
		const ::mesh::StreamViewRO<Uint16> dataIndices( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Index_U16 ) );
		const ::mesh::StreamViewRO<Uint32> dataGarmentFlags( sourceView.GetChunkStreamData( chunkIndexFilter, MST_GarmentFlags_U32 ) );
		const red::ArraySpan< const Vector3 > morphOffsets = mesh::MakeSpan< const Vector3 >( sourceView.GetChunkStreamData( chunkIndexFilter, MST_MorphOffset_3F ) );

		Uint32 numSourceIndices = dataIndices.Size();
		Uint32 numSourceVertices = dataPosition.Size();

		Bool hasNormals = (dataNormal.Size() >= numSourceVertices);
		Bool hasGarmentFlags = (dataGarmentFlags.Size() >= numSourceVertices);
		Bool hasMorphOffsets = (morphOffsets.Size() >= numSourceVertices) && (morphOffsetScales.Size() >= numSourceVertices);

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve( numSourceVertices );
		debugIndices.Reserve( numSourceIndices );

		const Vector3 lightDir = Vector3( 1.0f, 1.0f, 1.0f ).Normalized();

		Color colorFarFromBorder = Color::WHITE();
		Color colorCloserToBorder = Color::BLUE();
		Color colorOnCap = Color::RED();
																								/// @todo DP: make normal offset configurable
		for ( Uint32 i = 0; i < numSourceVertices; ++i )
		{
			const Vector3 n = hasNormals ? dataNormal[i] : Vector3( 0.f, 0.f, 1.f );
			const Uint32 flags = hasGarmentFlags ? dataGarmentFlags[i] : 0;
			const Vector3 morphOffset = hasMorphOffsets ? morphOffsets[i] : Vector3::ZEROS();
			const Float morphOffsetScale = hasMorphOffsets ? morphOffsetScales[i] : 0.0f;
			const Float unpackedMorphOffsetScale = morphOffsetScale * (c_morphOffsetScaleOvershootLimit / 255.0f);
			const Vector3 v = dataPosition[i] + morphOffset * unpackedMorphOffsetScale;
			const Vector3 p = v + n * ( hasNormals ? 0.001f : 0.f ); // Offset a little to avoid z-fighting.
			Float d = fabs( n.Dot( lightDir ) );
			d = math::Clamp( d, 0.f, 1.f );
			Color c;
			if ( (flags & 256u) != 0 )
				c = colorOnCap;
			else
				c = Color::Lerp( d, Color::GRAY(), Color::LIGHT_GRAY() );

			debugVertices.EmplaceBack( p, c );
		}

		for ( Uint32 i = 0; i < numSourceIndices; ++i )
		{
			debugIndices.PushBack( dataIndices[i] );
		}

		// no data
		if ( debugVertices.Empty() || debugIndices.Empty() )
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh( debugVertices, debugIndices );
	}

	TRenderPtr<IRenderResource> CreateMeshStreamIndices(THandle< CMesh > meshData, Int32 chunkIndexFilter)
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const ::mesh::StreamViewRO<Vector3> dataPosition(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Position_3F));
		const ::mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Index_U16));

		Uint32 numSourceIndices = dataIndices.Size();
		Uint32 numSourceVertices = dataPosition.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceIndices);
		debugIndices.Reserve(numSourceIndices);

		const Uint32 numTriangles = numSourceIndices / 3;

		for (Uint32 i = 0; i < numSourceIndices; i += 3)
		{
			Uint32 index = (i * 3 * 255) / numTriangles;

			const auto& v0 = dataPosition[dataIndices[i + 0]];
			const auto& v1 = dataPosition[dataIndices[i + 1]];
			const auto& v2 = dataPosition[dataIndices[i + 2]];

			const Color color((Uint8)index, (Uint8)index, (Uint8)index, 255);

			debugIndices.PushBack(debugVertices.Size());
			debugVertices.EmplaceBack(v0, color);
			debugIndices.PushBack(debugVertices.Size());
			debugVertices.EmplaceBack(v1, color);
			debugIndices.PushBack(debugVertices.Size());
			debugVertices.EmplaceBack(v2, color);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	TRenderPtr<IRenderResource> CreateMeshStreamTBN( THandle< CMesh > meshData, Int32 chunkIndexFilter, EMeshStreamType vectorStream, const Color& color, Float vectorLength )
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const ::mesh::StreamViewRO<Vector3> dataPosition(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Position_3F));
		const ::mesh::StreamViewRO<Vector3> dataVector(sourceView.GetChunkStreamData(chunkIndexFilter, vectorStream));

		Uint32 numSourceVertices = dataPosition.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceVertices * 2);
		debugIndices.Reserve(numSourceVertices * 2);

		const auto colorVal = color.ToUint32();
		const auto scale = (vectorLength != 0.0f) ? vectorLength : Config::cvMeshEditorVectorSize.Get() * meshData->GetBoundingBox().CalcSize().Mag3();

		// build the mesh, encode the influences with colors
		for (Uint32 i = 0; i < numSourceVertices; ++i)
		{
			const auto a = dataPosition[i];
			const auto v = dataVector[i];
			const auto b = a + v*scale;

			debugIndices.PushBack(debugVertices.Size());
			debugVertices.EmplaceBack(a, color);
			debugIndices.PushBack(debugVertices.Size());
			debugVertices.EmplaceBack(b, color);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	/*
	template<class TexCoordsType>
	static TRenderPtr<IRenderResource> CreateMeshStreamTexCoord(THandle< CMesh > meshData, Int32 chunkIndexFilter, EMeshStreamType vectorStream )
	{
		const ::mesh::DataViewRO sourceView(*meshData);

		const ::mesh::StreamViewRO<Vector3> dataPosition( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Position_3F ) );
		const ::mesh::StreamViewRO<TexCoordsType> dataTexCoord( sourceView.GetChunkStreamData( chunkIndexFilter, vectorStream ) );
		const ::mesh::StreamViewRO<Uint16> dataIndices(sourceView.GetChunkStreamData(chunkIndexFilter, MST_Index_U16));

		Uint32 numSourceVertices = dataPosition.Size();
		Uint32 numSourceIndices = dataIndices.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve(numSourceVertices * 3 + 4);
		debugIndices.Reserve(numSourceVertices * 6 + 4);

		const auto colorLinear = Color::GRAY().ToVectorLinearAccu();

		const auto scale = 512.0f;
		const auto offset = 50.0f;

		// frame
		{
			const auto base = debugVertices.Size();
			debugVertices.EmplaceBack(Vector4(offset, offset, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset + scale, offset, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset + scale, offset + scale, 0.0f), Vector4::ONES());
			debugVertices.EmplaceBack(Vector4(offset, offset + scale, 0.0f), Vector4::ONES());

			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 3);
			debugIndices.PushBack(base + 0);
		}

		for (Uint32 i = 0; i < numSourceIndices; i += 3)
		{
			const auto& v0 = dataTexCoord[dataIndices[i + 0]];
			const auto& v1 = dataTexCoord[dataIndices[i + 1]];
			const auto& v2 = dataTexCoord[dataIndices[i + 2]];

			// A uv flip (1.0f - v2.Y) is performed to avoid user's confusion when checking the UV's
			// The DirectX coordinate origin space is set to top/left. In most of the 3d applications it's set to bottom left.
			// Thanks to this flip the UV's visualization will look the same as in Maya.
			const auto base = debugVertices.Size();
			debugVertices.EmplaceBack(Vector4(offset + v2.X * scale, offset + (1.0f - v2.Y) * scale, 0.1f), colorLinear);
			debugVertices.EmplaceBack(Vector4(offset + v1.X * scale, offset + (1.0f - v1.Y) * scale, 0.1f), colorLinear);
			debugVertices.EmplaceBack(Vector4(offset + v0.X * scale, offset + (1.0f - v0.Y) * scale, 0.1f), colorLinear);

			debugIndices.PushBack(base + 0);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 1);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 2);
			debugIndices.PushBack(base + 0);
		}

		// no data
		if (debugVertices.Empty() || debugIndices.Empty())
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh(debugVertices, debugIndices);
	}

	*/

	TRenderPtr<IRenderResource> CreateMeshStreamMorphOffsetScales( THandle< CMesh > meshData, Int32 chunkIndexFilter,
	                                                               red::ArraySpan< const Uint8 > morphOffsetScales )
	{
		const ::mesh::DataViewRO sourceView( *meshData );
		red::ArraySpan< const Vector3 > verts = mesh::MakeSpan< const Vector3 >( sourceView.GetChunkStreamData( chunkIndexFilter, MST_Position_3F ) );
		red::ArraySpan< const Vector3 > morphOffsets = mesh::MakeSpan< const Vector3 >( sourceView.GetChunkStreamData( chunkIndexFilter, MST_MorphOffset_3F ) );
		Uint32 numVerts = verts.Size();

		red::DynArray< DebugVertex > debugVertices;
		red::DynArray< Uint32 > debugIndices;
		debugVertices.Reserve( numVerts * (2 + 4) );
		debugIndices.Reserve( numVerts * (2 + 12) );

		const auto pointScale = Config::cvMeshEditorVectorSize.Get() * 0.20f * meshData->GetBoundingBox().CalcSize().Mag3();

		// build the mesh, encode the influences with colors
		for ( Uint32 i = 0; i < numVerts; ++i )
		{
			const Vector3& vert = verts[i];
			const Vector3& morphOffset = morphOffsets[i];
			const Vector3 morphedVert = vert + morphOffset * c_morphOffsetScaleOvershootLimit;
			const Float morphOffsetScale = (i < morphOffsetScales.Size()) ? morphOffsetScales[i] : 0.0f;
			const Bool morphOffsetScaleIsNonZero = (morphOffsetScale != 0.0f);

			Float unpackedMorphOffsetScale = morphOffsetScale * (c_morphOffsetScaleOvershootLimit / 255.0f);
			Bool morphOffsetScaleIsExtraLong = (unpackedMorphOffsetScale > 1.0f);

			const Color color = morphOffsetScaleIsNonZero ? (morphOffsetScaleIsExtraLong ? Color::RED() : Color::YELLOW()) : Color::BLUE();

			if ( morphOffsetScaleIsNonZero )
			{
				Vector3 morphedScaledVert = vert + morphOffset * unpackedMorphOffsetScale;
				Uint32 vertIndex = debugVertices.Size();
				debugIndices.PushBack( vertIndex );
				debugVertices.EmplaceBack( vert, color );
				debugIndices.PushBack( vertIndex + 1u );
				debugVertices.EmplaceBack( morphedScaledVert, color );
			}

			const auto base = debugVertices.Size();

			debugVertices.PushBack( DebugVertex( vert + Vector3(  0,       0,       1 )      * pointScale, color ) );
			debugVertices.PushBack( DebugVertex( vert + Vector3(  0.943f,  0.000f, -0.333f ) * pointScale, color ) );
			debugVertices.PushBack( DebugVertex( vert + Vector3( -0.471f,  0.816f, -0.333f ) * pointScale, color ) );
			debugVertices.PushBack( DebugVertex( vert + Vector3( -0.471f, -0.816f, -0.333f ) * pointScale, color ) );

			debugIndices.PushBack( base + 2 );
			debugIndices.PushBack( base + 1 );
			debugIndices.PushBack( base + 0 );
			debugIndices.PushBack( base + 3 );
			debugIndices.PushBack( base + 2 );
			debugIndices.PushBack( base + 0 );
			debugIndices.PushBack( base + 1 );
			debugIndices.PushBack( base + 3 );
			debugIndices.PushBack( base + 0 );
			debugIndices.PushBack( base + 1 );
			debugIndices.PushBack( base + 2 );
			debugIndices.PushBack( base + 3 );
		}

		// no data
		if ( debugVertices.Empty() || debugIndices.Empty() )
			return nullptr;

		// create debug mesh for rendering
		return GRender->UploadDebugMesh( debugVertices, debugIndices );
	}

	//////////////////////////////////////////////////////////////////////////////////////////////

} // tools
