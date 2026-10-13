/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "morphTargetMesh.h"
#include "morphTargetMeshDiffBufferWriter.h"
#include "meshAppearance.h"

#include "../../renderData/include/imageRegionDetector.h"
#include "../../renderData/include/renderer.h"
#include "../../renderData/include/renderMorphTargetMeshBlob.h"
#include "../../resourceMaterial/include/materialDefinition.h"
#include "../../renderData/include/rectanglePacker.h"
#include "../../resourceMaterial/include/sourceTextureData.h"
#include "../../renderData/include/imageProcessing.h"
#include "../../redReflection/include/rttiPathParser.h"
#include "../../redReflection/include/deferredDataBuffer.h"
#include "../../redReflection/include/variant.h"
#include "../../redContainers/include/string/stringUtils.h"
#include "../../resourceMaterial/include/bitmapTexture.h"

using namespace MorphTargets;

struct SMorphTargetBuildContext
{
	const MorphTargetMesh*								resource;
	const CMesh*										baseMesh;

	red::StaticArray< const CBitmapTexture*, BTT_USED_MAX >	baseTexture { BTT_USED_MAX };
};

RTTI_BEGIN_TYPE( MorphTargetMeshEntry );
	RTTI_PROPERTY( m_name ).editable().tooltip( "Identifier for this morph target. \"None\" is reserved!" );
	RTTI_PROPERTY( m_regionName ).editable();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_mesh ).editable().notCooked().tooltip( "Mesh resource used for this morph target" );
	RTTI_PROPERTY( m_meshAppearance ).editable().notCooked().customEditor( "MeshAppearance" );
	RTTI_PROPERTY( m_maskTexture ).editable().notCooked().tooltip( "Mask texture for this morph target." );
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_faceRegion ).readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_normalmapBlendInfo ).notCooked().editable();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_boneNames );
	RTTI_PROPERTY( m_boneRigMatrices );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_preview ).editable().notSerialized();
	RTTI_PROPERTY( m_weight ).editable().notSerialized().range( 0.0f, 1.0f );
#endif // RED_CONFIGURATION_FINAL
RTTI_END_TYPE();

RED_FORCE_INLINE FaceRegion MapRegion( const CName regionName )
{
	// TODO: improve
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "eyes" ) )		return FACE_REGION_EYES;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "eye" ) )		return FACE_REGION_EYES;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "nose" ) )		return FACE_REGION_NOSE;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "mouth" ) )	return FACE_REGION_MOUTH;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "jaw" ) )		return FACE_REGION_JAW;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "ears" ) )		return FACE_REGION_EARS;
	if ( regionName == RED_NAME_CONSTEXPR_NOREG( "ear" ) )		return FACE_REGION_EARS;

	return FACE_REGION_NONE;
}


static THandle< IMaterial > GetChunkMaterial( const THandle< CMesh >& mesh, const CName appearanceName, const Uint32 chunkIndex )
{
	const THandle< mesh::MeshAppearance >& meshAppearance = mesh->GetAppearanceSafe( appearanceName );
	return meshAppearance->GetChunkMaterialData( chunkIndex );
}

static TResRef< ITexture > FindTextureInMaterial( const THandle< IMaterial >& material, CName parameterName )
{
	if ( !material )
	{
		RED_LOG_WARNING( "Null material" );
		return TResRef< ITexture >::Null();
	}

	const IMaterialDefinition* definition = material->GetMaterialDefinition();
	if ( !definition )
	{
		RED_LOG_WARNING( "Material '%hs' has no definition", material->GetFriendlyName().AsChar() );
		return TResRef< ITexture >::Null();
	}

	TResRef< ITexture > texture;
	if ( !material->ReadParameter( parameterName, texture ) )
	{
		RED_LOG_WARNING( "Could not read texture parameter '%hs' from material '%hs'", parameterName.AsChar(), material->GetFriendlyName().AsChar() );
		return TResRef< ITexture >::Null();
	}

	return texture;
}

static CName FindTextureNameInMaterial( const THandle< IMaterial >& material, const CName texNameToSearchFor )
{
	if ( !material )
	{
		RED_LOG_WARNING( "Null material" );
		return CName::NONE();
	}

	const IMaterialDefinition* definition = material->GetMaterialDefinition();
	if ( !definition )
	{
		RED_LOG_WARNING( "Material '%hs' has no definition", material->GetFriendlyName().AsChar() );
		return CName::NONE();
	}

	const IMaterialDefinition::TParameterDefinitionArray& parameterDefinitions = definition->GetParameterDefinitions( MSH_PixelShader );

	CName result = CName::NONE();
	for ( const auto& paramDef : parameterDefinitions )
	{
		if ( paramDef.m_type == IMaterialDataProviderDesc::EParameterType::PT_Texture )
		{
			if ( paramDef.m_name == texNameToSearchFor )
			{
				return paramDef.m_name;
			}
			else
			{
				red::StringView stringName = paramDef.m_name.AsStringView();
				if ( stringName.Find( texNameToSearchFor.AsChar() ) != red::StringView::npos )
				{
					result = paramDef.m_name;
					break;
				}
			}
		}
	}

	if ( !result.Empty() )
	{
		return result;
	}

	return CName::NONE();
}

MorphTargetMeshEntry::MorphTargetMeshEntry()
	: m_faceRegion( FACE_REGION_NONE )
{
}

void MorphTargetMeshEntry::CacheFaceRegion()
{
	m_faceRegion = MapRegion( m_regionName );
}

void MorphTargetMeshEntry::CacheBonesAndRigMatrices()
{
#ifndef RED_CONFIGURATION_FINAL
	if ( m_mesh )
	{
		mesh::DataViewRO view( *m_mesh.Get() );
		m_boneNames			= view.GetBoneNames();
		m_boneRigMatrices	= view.GetBoneRigMatrices();
	}
#endif // RED_CONFIGURATION_FINAL
}

#ifndef RED_CONFIGURATION_FINAL
void MorphTargetMeshEntry::SetMeshAppearance( CName meshAppearance )
{
	m_meshAppearance = meshAppearance;
}

void MorphTargetMeshEntry::SetTextureBlend( Bool blendTexture )
{
	m_normalmapBlendInfo.m_blend = blendTexture;
}
#endif // RED_CONFIGURATION_FINAL

Bool MorphTargetMeshEntry::Build( const SMorphTargetBuildContext& context, TargetVertexDiffsData& targetVertexDiffsData, red::StaticArray< DataBuffer, BTT_MAX >& targetTextureDiffs, red::StaticArray< Uint16, BTT_MAX >& targetTextureDiffsWidth, red::StaticArray< Uint8, MorphTargets::BTT_MAX >& targetTextureDiffsMipLevelOffsets, red::StaticArray< Vector4, BTT_MAX >& targetTextureQuantizationScale, red::StaticArray< Vector4, BTT_MAX >& targetTextureQuantizationOffset )
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	Bool success = true;

	// validate this entry
	if ( m_name.Empty() )
	{
		RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target has empty name" );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
		RED_DATA_ERROR_END();
		success = false;
	}
	if ( !m_mesh.IsValid() )
	{
		RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs'] - target mesh is invalid.", m_name.AsChar() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
		RED_DATA_ERROR_END();
		success = false;
	}
	if ( !m_maskTexture.IsValid() )
	{
		RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs'] - mask texture is invalid.", m_name.AsChar() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
		RED_DATA_ERROR_END();
		success = false;
	}

	// validate context
	if ( !context.resource )
	{
		success = false;
	}
	if ( !context.baseMesh )
	{
		success = false;
	}

	// Build vertex diffs 
	if ( success )
	{
		const Bool vertexDifferenceStatus = Build_VertexDifferences( context, targetVertexDiffsData );
		success = success && vertexDifferenceStatus;
	}

	if ( success )
	{
		// Build textures diffs
		//TODO - support multiple chunks
		// There can be outdated appearance name set in the morph target and in such case we want to replace it with the default one (as returned from GetAppearanceSafe)
		SetMeshAppearance( m_mesh->GetAppearanceSafe( m_meshAppearance )->GetName() );
		const THandle< IMaterial > material = GetChunkMaterial( m_mesh.Get(), m_meshAppearance, 0 );
		const BlendedTextureType texType = BTT_Normal;
		if ( m_normalmapBlendInfo.m_blend && context.baseTexture[ texType ] )
		{
			do
			{
				const CName texMatParam = FindTextureNameInMaterial( material, GetTextureName( texType ) );
				if ( texMatParam.Empty() )
				{
					RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Rendering, "Morph target ['%hs'] - couldn't find texture ['%hs']. Therefore this will not be morphed. ['%hs']", m_name.AsChar(), GetTextureName( texType ).AsChar(), context.resource->GetPath().ToDebugString() );
					break;
				}
				//TODO - support multiple chunks with different materials during texture blending, not just 0 one like below
				TResRef< ITexture > targetTextureRef = FindTextureInMaterial( material, texMatParam );
				if ( !targetTextureRef.IsValid() )
				{
					RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Rendering, "Morph target ['%hs'] - invalid texture ['%hs']. Therefore this will not be morphed.", m_name.AsChar(), GetTextureName( texType ).AsChar(), context.resource->GetPath().ToDebugString() );
					break;
				}

				THandle< CBitmapTexture > targetTexture = SafeCast< CBitmapTexture >( targetTextureRef.Get() );
				if ( !targetTexture )
				{
					RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Rendering, "Morph target ['%hs'] - could not extract texture from resource ['%hs']. Therefore this will not be morphed.", m_name.AsChar(), GetTextureName( texType ).AsChar(), context.resource->GetPath().ToDebugString() );
					break;
				}

				if ( targetTexture.Get() == context.baseTexture[ texType ] )
				{
					RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Rendering, "Morph target ['%hs'] - base texture and target texture ['%hs'] are the same. Therefore this will not be morphed.", m_name.AsChar(), GetTextureName( texType ).AsChar(), context.resource->GetPath().ToDebugString() );
					break;
				}

				const Bool textureDiffsStatus = Build_TextureDifferences( context.baseTexture[ texType ], targetTexture.Get(), m_normalmapBlendInfo, targetTextureDiffs[ texType ], targetTextureDiffsWidth[ texType ], targetTextureDiffsMipLevelOffsets[ texType ], targetTextureQuantizationScale[ texType ], targetTextureQuantizationOffset[ texType ] );
				if ( !textureDiffsStatus )
				{
					RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs'] - error building texture ['%hs'] differences.", m_name.AsChar(), GetTextureName( texType ).AsChar() );
					RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
					RED_DATA_ERROR_END();
				}
				success = success && textureDiffsStatus;
			}
			while ( false );
		}
	}

	// If any step failed, clear out any built data.
	if ( !success )
	{
		targetVertexDiffsData.m_numVertexDiffsInEachChunk.Clear();
	}

	return success;
#endif // RED_CONFIGURATION_FINAL
}

Vector3 PackPositionDiff( const Vector3& posDiff, const Vector4& scale, const Vector4& bias )
{
	const Vector3& s3 = scale.AsVector3();
	const Vector3& b3 = bias.AsVector3();
	return ( posDiff - b3 ) / s3;
}

// TODO: move to backend
Bool MorphTargetMeshEntry::Build_VertexDifferences( const SMorphTargetBuildContext& context, TargetVertexDiffsData& outVertexDiffsData ) const
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	THandle< CMesh > thisMesh = m_mesh.Get();
	THandle< CBitmapTexture > maskTexture = m_maskTexture.Get();

	RED_FATAL_ASSERT( thisMesh != nullptr, "Could not get this mesh! Caller should have already checked!" );
	RED_FATAL_ASSERT( maskTexture != nullptr, "Could not get mask texture! Caller should have already checked!" );
	RED_FATAL_ASSERT( context.baseMesh != nullptr, "Base mesh is null. Caller needs to check this!" );

	img::ImagePtr imagePtr = maskTexture->GetImage();
	img::Image::SizeInfo imgSize = imagePtr->GetSize();
	img::ImageAccess imageAccess( *imagePtr );

	const mesh::DataViewRO thisDataView( *thisMesh );
	const mesh::DataViewRO baseDataView( *context.baseMesh );
	
	struct SMorphVertexDiff
	{
		Vector3 m_position;
		Vector3 m_normal;
		Vector3 m_tangent;
		// Don't care about binormal. UVs are the same, so binormal sign should be consistent. And vector itself is calculated when rendering.
	};

	Vector4 minPosDiff( RED_FLT_MAX, RED_FLT_MAX, RED_FLT_MAX, 0.f );
	Vector4 maxPosDiff( -RED_FLT_MAX, -RED_FLT_MAX, -RED_FLT_MAX, 0.f );

	Uint32 meshVertexCount = 0;
	for ( Uint32 chunk_i = 0; chunk_i < thisDataView.GetNumChunks(); ++chunk_i )
	{
		meshVertexCount += thisDataView.GetChunkVertexCount( chunk_i );
	}

	red::DynArray< Uint16 > diffsMapping { rend::PoolMesh() };

	red::DynArray< SMorphVertexDiff > differences { rend::PoolMesh() };
	differences.Reserve( meshVertexCount );

	outVertexDiffsData.m_numVertexDiffsInEachChunk.Clear();
	outVertexDiffsData.m_numVertexDiffsInEachChunk.Reserve( thisDataView.GetNumChunks() );
	outVertexDiffsData.m_numVertexDiffsMappingInEachChunk.Reserve( thisDataView.GetNumChunks() );
	
	constexpr Float zeroThreshold = 1e-6f;

	Uint8 numInserted0Mappings = 0;

	const auto& chunks = thisMesh->DEPRECATED_GetChunks();
	for ( Uint32 chunk_i = 0; chunk_i < thisDataView.GetNumChunks(); ++chunk_i )
	{
		const auto& chunk = chunks[ chunk_i ];

		Uint32 thisVertexCount = thisDataView.GetChunkVertexCount( chunk_i );
		Uint32 baseVertexCount = baseDataView.GetChunkVertexCount( chunk_i );
		if ( thisVertexCount != baseVertexCount )
		{
			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs'] - Vertex count mismatch between '%hs' (%u) and '%hs' (%u). Cannot generate morph target data!", m_name.AsChar(), thisMesh->GetFriendlyName().AsChar(), thisVertexCount, context.baseMesh->GetFriendlyName().AsChar(), baseVertexCount );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
			RED_DATA_ERROR_END();
			return false;
		}
		
		mesh::ChunkStreamDataRO thisPositionStream = thisDataView.GetChunkStreamData( chunk_i, MST_Position_3F );
		mesh::ChunkStreamDataRO basePositionStream = baseDataView.GetChunkStreamData( chunk_i, MST_Position_3F );
		mesh::ChunkStreamDataRO thisNormalsStream = thisDataView.GetChunkStreamData( chunk_i, MST_Normal_3F );
		mesh::ChunkStreamDataRO baseNormalsStream = baseDataView.GetChunkStreamData( chunk_i, MST_Normal_3F );
		mesh::ChunkStreamDataRO thisTangentStream = thisDataView.GetChunkStreamData( chunk_i, MST_Tangent_3F );
		mesh::ChunkStreamDataRO baseTangentStream = baseDataView.GetChunkStreamData( chunk_i, MST_Tangent_3F );

		mesh::ChunkStreamDataRO thisUVStream = thisDataView.GetChunkStreamData( chunk_i, MST_TexCoord0_2F );

		//getting the morph vertex weights only from the target mesh
		mesh::ChunkStreamDataRO morphVertexDataStream = thisDataView.GetChunkStreamData( chunk_i, MST_MorphVertexData_3F );
		Bool useMorphVertexStreamData = !morphVertexDataStream.Empty();

		if ( !thisPositionStream.m_buffer.Data() || !basePositionStream.m_buffer.Data() )
		{
			return false;
		}
		const Uint32 numVertices = thisVertexCount;

		const red::ArraySpan< Vector3 > thisPositions = red::MakeArraySpan( (Vector3*)thisPositionStream.m_data, numVertices );
		const red::ArraySpan< Vector3 > basePositions = red::MakeArraySpan( (Vector3*)basePositionStream.m_data, numVertices );

		const red::ArraySpan< Vector3 > thisNormals = red::MakeArraySpan( (Vector3*)thisNormalsStream.m_data, numVertices );
		const red::ArraySpan< Vector3 > baseNormals = red::MakeArraySpan( (Vector3*)baseNormalsStream.m_data, numVertices );

		const red::ArraySpan< Vector3 > thisTangents = red::MakeArraySpan( (Vector3*)thisTangentStream.m_data, numVertices );
		const red::ArraySpan< Vector3 > baseTangents = red::MakeArraySpan( (Vector3*)baseTangentStream.m_data, numVertices );

		const red::ArraySpan< Vector2 > thisUVs = red::MakeArraySpan( (Vector2*)thisUVStream.m_data, numVertices );

		red::ArraySpan< Vector3 > morphVertexData;
		if ( useMorphVertexStreamData )
		{
			morphVertexData = red::MakeArraySpan( (Vector3*)morphVertexDataStream.m_data, numVertices );
		}

		struct VertexWeight
		{
			Vector3 pos;
			Vector2 uv;
			Float	weight;
		};

		red::DynArray< VertexWeight > vertices { red::PoolFrame() };
		vertices.Reserve( numVertices );

		// first pass - store position, uv and weight
		for ( Uint32 i = 0; i < numVertices; ++i )
		{
			const Vector2& uv = thisUVs[ i ];
			Vector2 clampedUV = uv;

			if ( useMorphVertexStreamData )
			{
				clampedUV = Vector2( morphVertexData[i].X, morphVertexData[i].Y );
			}
			
			// UVs can actually fall outside of range [0, 1] for eyes, where we mark left / right eye with negative UVs or UVs between 1.f and 2.f
			if ( clampedUV.X > 1.0f )	clampedUV.X -= 1.0f;
			if ( clampedUV.X < 0.0f )	clampedUV.X += 1.0f;

			clampedUV.X = math::Clamp( clampedUV.X, 0.f, 1.f );
			clampedUV.Y = math::Clamp( clampedUV.Y, 0.f, 1.f );

			Float fU = clampedUV.X * imgSize.m_width;
			Float fV = clampedUV.Y * imgSize.m_height;

			Uint16 uU = Uint16( fU );
			Uint16 uV = Uint16( fV );
			img::ImageIterator it( uU, uV, 0 );
			img::ImagePixel pixel;
			img::utils::UnpackPixel_SLOW( *imagePtr.Get(), imageAccess, it, pixel );

			Float pixelValue[ 4 ];
			pixel.Store( pixelValue );
			Float mask = pixelValue[0];

			const Vector3& position = thisPositions[ i ];

			vertices.PushBack( { position, uv, mask } );
		}

		static constexpr Float DISTANCE_THRESHOLD = 0.000001f;

		// second pass - fixup weights
		for ( Uint32 i = 0; i < numVertices; ++i )
		{
			const VertexWeight& currentVertex = vertices[ i ];

			Float maxWeight = currentVertex.weight;

			red::DynArray< Uint32 > matchingVertexIDs { rend::PoolMesh() };
			for ( Uint32 j = 0; j < numVertices; ++j )
			{
				const VertexWeight& otherVertex = vertices[ j ];
				if ( currentVertex.pos.DistanceTo( otherVertex.pos ) <= DISTANCE_THRESHOLD )
				{
					matchingVertexIDs.PushBack( j );
					maxWeight = Max( maxWeight, otherVertex.weight );
				}
			}

			for ( const Uint32 indexToFix : matchingVertexIDs )
			{
				vertices[ indexToFix ].weight = maxWeight;
			}
		}

		Uint32 diffsInChunk = 0;
		for ( Uint32 i = 0; i < numVertices; ++i )
		{
			const Float mask = vertices[ i ].weight;
			if ( mask <= zeroThreshold )
			{
				continue;
			}

			SMorphVertexDiff diff;

			diff.m_position = thisPositions[ i ] - basePositions[ i ];

			// Normal and tangent diffs have possible range of [-2,2], but when packed will be effectively clamped to [-1,1]. So we'll
			// scale the diffs here to still allow the full range, and then expand them when applying the diff.
			diff.m_normal = ( thisNormals[ i ] - baseNormals[ i ] ) * 0.5f;
			diff.m_tangent = ( thisTangents[ i ] - baseTangents[ i ] ) * 0.5f;

			if ( !diff.m_position.IsAlmostZero( zeroThreshold )
				|| !diff.m_normal.IsAlmostZero( zeroThreshold )
				|| !diff.m_tangent.IsAlmostZero( zeroThreshold ) )
			{
				diff.m_position *= mask;
				diff.m_tangent *= mask;
				diff.m_normal *= mask;

				differences.PushBack( diff );

				diffsMapping.PushBack( i );

				minPosDiff.X = Min( minPosDiff.X, diff.m_position.X );
				minPosDiff.Y = Min( minPosDiff.Y, diff.m_position.Y );
				minPosDiff.Z = Min( minPosDiff.Z, diff.m_position.Z );
								    							    
				maxPosDiff.X = Max( maxPosDiff.X, diff.m_position.X );
				maxPosDiff.Y = Max( maxPosDiff.Y, diff.m_position.Y );
				maxPosDiff.Z = Max( maxPosDiff.Z, diff.m_position.Z );

				++diffsInChunk;
			}
		}
		outVertexDiffsData.m_numVertexDiffsInEachChunk.PushBack( diffsInChunk );
		outVertexDiffsData.m_numVertexDiffsMappingInEachChunk.PushBack( red::DivideRoundingUp( diffsInChunk, 2 ) );
		if ( diffsInChunk % 2 != 0 )
		{
			diffsMapping.PushBack( 0 );
			++numInserted0Mappings;
		}
	}

	outVertexDiffsData.m_quantizationScale = maxPosDiff - minPosDiff;
	outVertexDiffsData.m_quantizationOffset = minPosDiff;

	//We should always rebuild the vertex differences because previously stored data might no longer be up to date 
	//(meshes might have changed and previous diffs might be non zero while current diff is 0)

	GrowableMemoryBuffer packedVertexData;
	MorphTargetMeshDiffBufferWriter writer( packedVertexData );

	RED_ASSERT( numInserted0Mappings <= thisDataView.GetNumChunks() );
	RED_ASSERT( differences.Size() == diffsMapping.Size() || differences.Size() + numInserted0Mappings == diffsMapping.Size() ); // take into account ending "0"
	for ( Uint32 i = 0; i < differences.Size(); ++i )
	{
		const auto& vD = differences[ i ];

		const Vector3 packedPositionDiff = PackPositionDiff( vD.m_position, outVertexDiffsData.m_quantizationScale, outVertexDiffsData.m_quantizationOffset );
		writer.WriteDec3N( packedPositionDiff.X, packedPositionDiff.Y, packedPositionDiff.Z );
		writer.WriteDec3N( vD.m_normal.X * .5f + .5f, vD.m_normal.Y * .5f + .5f, vD.m_normal.Z * .5f + .5f );
		writer.WriteDec3N( vD.m_tangent.X * .5f + .5f, vD.m_tangent.Y * .5f + .5f, vD.m_tangent.Z * .5f + .5f );
	}

	outVertexDiffsData.m_vertexDiffs = DataBuffer::Copy( packedVertexData.Data(), packedVertexData.Size() );
	outVertexDiffsData.m_vertexDiffsMapping = DataBuffer::Copy( diffsMapping.Data(), diffsMapping.DataSize() );

	return true;
#endif // RED_CONFIGURATION_FINAL
}

Uint8 CalculateMipBiasForMorphDiffTexture( Uint32 width, Uint8 mipBias, DiffTextureSize diffTextureSize )
{
	Uint32 maxDiffWidth = GetMaxDiffWidth( diffTextureSize );

	if ( width <= maxDiffWidth )
	{
		return mipBias;
	}

	Uint32 tempWidth = width;
	Uint8 resultMipBias = 0;

	while ( tempWidth > maxDiffWidth )
	{
		++resultMipBias;
		tempWidth /= 2;
	}

	return Max( mipBias, resultMipBias );
}

Bool MorphTargetMeshEntry::Build_TextureDifferences( const CBitmapTexture* baseTexture, const CBitmapTexture* targetTexture, const TextureBlendInfo& textureBlendInfo, DataBuffer& outTextureDiff, Uint16& outTextureDiffWidth, Uint8& outTextureDiffMipLevelCount, Vector4& outQuantizationScale, Vector4& outQuantizationBias ) const
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	RED_ASSERT( baseTexture, "Base texture is null. Caller needs to check this!" );
	RED_ASSERT( targetTexture, "Target texture is null. Caller needs to check this!" );

	RED_ASSERT( baseTexture != targetTexture, "Textures are the same. Caller needs to check this!" );

	ITexture::SizeDesc baseSizeDesc = baseTexture->GetSizeDesc();
	ITexture::SizeDesc targetSizeDesc = targetTexture->GetSizeDesc();

	RED_ASSERT( baseSizeDesc.width == baseSizeDesc.height, "Texture: %ls is not rectangular [%d x %d]", targetTexture->GetFriendlyName().AsChar(), baseSizeDesc.width, baseSizeDesc.height );

	if ( targetSizeDesc.width != baseSizeDesc.width || targetSizeDesc.height != baseSizeDesc.height )
	{
		RED_LOG_WARNING( "MorphTarget: Texture size mismatch between base (%ux%u) and target (%ux%u) for base '%hs' and target '%hs'", baseSizeDesc.width, baseSizeDesc.height, targetSizeDesc.width, targetSizeDesc.height, baseTexture->GetPath().ToDebugString(), targetTexture->GetPath().ToDebugString() );
	}

	img::ImagePtr targetImg = targetTexture->GetImage();
	img::ImagePtr baseImg = baseTexture->GetImage();
	if ( !targetImg || !baseImg )
	{
		return false;
	}

	targetImg->SetGammaExp( targetTexture->GetTextureSetup().m_isGamma ? 2.2f : 1.0f );
	baseImg->SetGammaExp( baseTexture->GetTextureSetup().m_isGamma ? 2.2f : 1.0f );

	Uint8 mipBias = CalculateMipBiasForMorphDiffTexture( baseSizeDesc.width, baseTexture->GetTextureSetup().GetPlatformMipBias( ETexturePlatform::PC ), textureBlendInfo.m_diffSize );
	for ( Uint8 i = 0; i < mipBias; ++i )
	{
		img::Image::SizeInfo baseSize = baseImg->GetSize();
		img::Image::SizeInfo targetSize = targetImg->GetSize();

		baseImg = img::utils::DownscaleBox( baseImg );

		if ( targetSize.m_width >= baseSize.m_width )
		{
			targetImg = img::utils::DownscaleBox( targetImg );
		}
	}

	Vector4 minDiff = Vector4( FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX );
	Vector4 maxDiff = Vector4( -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX );
	constexpr Float zeroThreshold = 1e-3f;
	
	outTextureDiffMipLevelCount = 0;

	const img::PixelFormat pixelFormat = baseImg->GetPixelFormat();

	img::Image::SizeInfo baseSize = baseImg->GetSize();
	img::Image::SizeInfo targetSize = targetImg->GetSize();
	outTextureDiffWidth = baseSize.m_width;

	Uint32 resultDataSize = 0;

	Uint32 baseTempWidth = baseSize.m_width;
	Uint32 targetTempWidth = targetSize.m_width;
	while ( baseTempWidth >= 4 && targetTempWidth >= 4 )
	{
		++outTextureDiffMipLevelCount;
		baseTempWidth = Max( 1u, baseTempWidth / 2 );
		targetTempWidth = Max( 1u, targetTempWidth / 2 );
	}

	struct TmpImage
	{
		red::DynArray< Vector4 > data { rend::PoolTexture() };
		img::Image::SizeInfo baseSize;

		TmpImage()
			: baseSize( img::Image::SizeInfo() )
			, data( rend::PoolTexture() )
		{}

		TmpImage( img::Image::SizeInfo size, Uint32 dataSize )
			: baseSize( size )
			, data( dataSize, rend::PoolTexture() )
		{}
	};

	red::DynArray< TmpImage > tempImg( outTextureDiffMipLevelCount, rend::PoolTexture() );

	for ( Uint32 mipI = 0; mipI < outTextureDiffMipLevelCount; ++mipI )
	{
		baseSize = baseImg->GetSize();
		targetSize = targetImg->GetSize();

		img::ImagePtr maskPtr = m_maskTexture->GetImage();
		img::Image::SizeInfo maskSize = maskPtr->GetSize();
		img::ImageAccess maskAccess(*maskPtr);

		img::ImageAccess targetAccess(*targetImg);
		img::ImageAccess baseAccess(*baseImg);

		tempImg[ mipI ] = TmpImage( baseSize, baseSize.m_width * baseSize.m_width );

		Uint32 imageDataIndex = 0;

		// figure out min / max difference
		for ( Uint32 y = 0; y < baseSize.m_width; ++y )
		{
			for ( Uint32 x = 0; x < baseSize.m_width; ++x )
			{
				Float u = Float(x) / baseSize.m_width;
				Float v = Float(y) / baseSize.m_width;

				Uint16 maskU = Uint16(u * maskSize.m_width);
				Uint16 maskV = Uint16(v * maskSize.m_height);
				img::ImageIterator maskIterator(maskU, maskV, 0);
				img::ImagePixel maskPixel;
				img::utils::UnpackPixel_SLOW( *maskPtr.Get(), maskAccess, maskIterator, maskPixel );

				Float maskValue[4];
				maskPixel.Store( maskValue );
				const Float mask = maskValue[ 0 ];

				Vector4 diff = Vector4::ZEROS();

				if ( mask > zeroThreshold )
				{
					img::ImageIterator targetIterator( (Uint16)( u * targetSize.m_width ), (Uint16)( v * targetSize.m_height ), 0 );
					img::ImageIterator baseIterator( (Uint16)( u * baseSize.m_width ), (Uint16)( v * baseSize.m_height ), 0 );

					// get difference
					img::ImagePixel targetPixel;
					img::utils::UnpackPixel_SLOW( *targetImg.Get(), targetAccess, targetIterator, targetPixel );
					if ( targetImg->IsInGammaSpace() )
					{
						targetPixel.GammaToLinearInplace( targetImg->GetGammaExp() );
					}

					img::ImagePixel basePixel;
					img::utils::UnpackPixel_SLOW( *baseImg.Get(), baseAccess, baseIterator, basePixel );
					if ( baseImg->IsInGammaSpace() )
					{
						basePixel.GammaToLinearInplace( baseImg->GetGammaExp() );
					}

					math::Vector4 targetColor;
					math::Vector4 baseColor;

					targetPixel.Store( targetColor.AsFloat() );
					basePixel.Store( baseColor.AsFloat() );

					diff = targetColor - baseColor;
					diff.W = mask;
					minDiff = Vector4::Min4( minDiff, diff );
					maxDiff = Vector4::Max4( maxDiff, diff );
					minDiff.W = 0.f;
					maxDiff.W = 0.f;
				}

				tempImg[ mipI ].data[ imageDataIndex ] = diff;
				++imageDataIndex;
			}
		}

		baseImg = img::utils::DownscaleBox( baseImg );
		targetImg = img::utils::DownscaleBox( targetImg );
	}

	outQuantizationBias = minDiff;
	outQuantizationScale = maxDiff - minDiff;

	const Vector3& diffScale3 = outQuantizationScale.AsVector3();
	if ( diffScale3.IsAlmostZero( zeroThreshold ) )
	{
		// the textures do not differ
		return true;
	}

	outQuantizationBias.W = 0.f;
	outQuantizationScale.W = 1.f;

	red::DynArray< red::UniqueBuffer > imgMips( outTextureDiffMipLevelCount, rend::PoolTexture() );
	for ( Uint32 mipI = 0; mipI < outTextureDiffMipLevelCount; ++mipI )
	{
		const img::Image::SizeInfo baseSize = tempImg[ mipI ].baseSize;

		img::ImagePtr result = img::Image::CreateEmpty( pixelFormat, baseSize );
		img::ImageAccess resultAccess( *result );
		img::ImageIterator resultIterator( 0, 0, 0, baseSize.m_width, baseSize.m_height, 1 );
		Uint32 imageDataIndex = 0;
		for ( Uint32 y = 0; y < baseSize.m_height; ++y )
		{
			for ( Uint32 x = 0; x < baseSize.m_width; ++x )
			{
				Vector4 diff = tempImg[ mipI ].data[ imageDataIndex ];
				if ( diff.W > zeroThreshold )
				{
					diff = ( diff - outQuantizationBias ) / outQuantizationScale;
				}

				img::ImagePixel pixel;
				pixel.Load( diff.AsFloat() );
				img::utils::PackPixel_SLOW( *result.Get(), resultAccess, resultIterator, pixel );

				++imageDataIndex;
				resultIterator.Next();
			}
		}
		
		GpuWrapApi::TextureDataDesc srcDesc;
		srcDesc.format		= GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
		srcDesc.width		= baseSize.m_width;
		srcDesc.height		= baseSize.m_height;
		srcDesc.depth		= 1;
		srcDesc.numSlices	= 1;
		srcDesc.rowPitch	= result->GetLayout().m_rowPitch;
		srcDesc.slicePitch	= result->GetLayout().m_layerPitch;
		srcDesc.dataPtr = result->GetPixelBuffer();

		imgMips[ mipI ] = red::CreateUniqueBuffer< rend::PoolTexture >( 0, 16 );

		GpuWrapApi::TextureDataDesc compressedDesc;
		if ( !GpuWrapApi::CompressImage( srcDesc, GpuWrapApi::TEXFMT_BC7, compressedDesc, imgMips[ mipI ], GpuWrapApi::CIH_None ) )
		{
			return false;
		}
		resultDataSize += imgMips[ mipI ].GetSize();
	}

	red::DynArray< Uint8 > resultBuffer( resultDataSize, red::PoolFrame() );
	Uint8* resultPtr = resultBuffer.TypedData();

	for ( const auto& imgMip : imgMips )
	{
		red::Memcpy( resultPtr, imgMip.Data(), imgMip.GetSize() );
		resultPtr = red::OffsetPtr( resultPtr, imgMip.GetSize() );
	}

	outTextureDiff = DataBuffer::Copy( resultBuffer.Data(), resultBuffer.DataSize() );

	return true;
#endif // RED_CONFIGURATION_FINAL
}

//////////////////////////////////////////////////////////////////////////

namespace Version
{
	enum Enum
	{
		BASE,
		NORMALMAP_BLEND_ONLY,				// removed diffuse and roughness blending
		CACHE_NORMALMAP_BLEND,				// 

		NEWEST = CACHE_NORMALMAP_BLEND
	};
}

RTTI_BEGIN_TYPE( MorphTargetMesh );
	RTTI_PARENT_TYPE( res::StreamedResource );
	RTTI_PROPERTY( m_baseMesh ).editable();
	RTTI_PROPERTY( m_targets ).editable().resizable();
	RTTI_PROPERTY( m_boundingBox ).readOnly();
	RTTI_PROPERTY( m_baseTextureParamName ).readOnly();
	RTTI_PROPERTY( m_blob );
	RTTI_PROPERTY( m_baseMeshAppearance ).editable().customEditor( "MeshAppearance" );
	RTTI_PROPERTY( m_baseTexture ).readOnly();

#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_resourceVersion );
	RTTI_PROPERTY( m_blendTexture ).editable().notCooked().tooltip( "Setting a value here will set blending for all targets!!" );
#endif // RED_CONFIGURATION_FINAL
RTTI_END_TYPE();


MorphTargetMesh::MorphTargetMesh()
#ifndef RED_CONFIGURATION_FINAL
	: m_resourceVersion( Version::BASE )
#endif // RED_CONFIGURATION_FINAL
{}

const MorphTargetMeshEntry* MorphTargetMesh::GetTargetEntry( CName name ) const
{
	for ( const auto& target : m_targets )
	{
		if ( target.GetName() == name )
		{
			return &target;
		}
	}
	return nullptr;
}

const MorphTargetMeshEntry& MorphTargetMesh::GetTargetEntry( TMorphTargetTargetIndex index ) const
{
	RED_ASSERT( index < m_targets.Size(), "Invalid index: %d (numTargets: %d)", index, m_targets.Size() );
	return m_targets[ index ];
}

TMorphTargetTargetIndex MorphTargetMesh::GetTargetIndex( const MorphTargetMeshEntry& target ) const
{
	const MorphTargetMeshEntry* targetPtr = &target;
	const ptrdiff_t ptrDiff = targetPtr - m_targets.TypedData();
	if ( ptrDiff >= 0 && ptrDiff < m_targets.Size() )
	{
		RED_ASSERT( ptrDiff < MORPH_TARGET_INVALID_TARGET );
		return (TMorphTargetTargetIndex)ptrDiff;
	}

	return MORPH_TARGET_INVALID_TARGET;
}

Bool MorphTargetMesh::Rebuild()
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	THandle< CMesh > baseMesh = m_baseMesh.Get();
	if ( !baseMesh )
	{
		RED_LOG_ERROR( "%ls has no base mesh. Can't create MorphTarget.", GetFriendlyName().AsChar() );
		return false;
	}

	for ( Uint32 iTex = 0; iTex < BTT_USED_MAX; ++iTex )
	{
		const BlendedTextureType texType = (BlendedTextureType)iTex;
		RED_ASSERT( MorphTargets::ShouldProcess( texType ) );
		Rebuild_ExtractBaseTexture( texType );
	}
	Rebuild_BoundingBox();
	return Rebuild_Targets();
#endif // RED_CONFIGURATION_FINAL
}

void MorphTargetMesh::AppendMorphTargetIDSet( MorphTargets::MorphTargetIDSet& morphTargetIDSet )
{
	morphTargetIDSet.Reserve( morphTargetIDSet.Size() + m_targets.Size() );
	for ( const auto& target : m_targets )
	{
		morphTargetIDSet.PushBack( { target.GetName(), target.GetRegionName() } );
	}
}

void MorphTargetMesh::Rebuild_BoundingBox()
{
#ifndef RED_CONFIGURATION_FINAL
	THandle< CMesh > baseMesh = m_baseMesh.Get();
	RED_FATAL_ASSERT( baseMesh, "Cannot get baseMesh. But this should have been checked before even getting here!" );

	Box combinedBox = baseMesh->GetBoundingBox();
	for ( const auto& target : m_targets )
	{
		if ( const auto& targetMesh = target.GetMesh() )
		{
			combinedBox.AddBox( targetMesh->GetBoundingBox() );
		}
	}
	m_boundingBox = combinedBox;
	NotifyPropertyChanged( rtti::AccessPath( "boundingBox" ) );
#endif // RED_CONFIGURATION_FINAL
}

CName MorphTargetMesh::Rebuild_ExtractBaseTexture( const BlendedTextureType texType )
{
#ifdef RED_CONFIGURATION_FINAL
	return CName();
#else
	RED_ASSERT( texType == BTT_Normal );

	//TODO, support multiple chunks
	auto chunkMaterial = GetChunkMaterial( m_baseMesh.Get(), m_baseMeshAppearance, 0 );
	m_baseTextureParamName = FindTextureNameInMaterial( chunkMaterial, GetTextureName( texType ) );
	m_baseTexture = FindTextureInMaterial( chunkMaterial, m_baseTextureParamName );
	NotifyPropertyChanged( rtti::AccessPath( "baseTexture" ) );

	return m_baseTextureParamName;
#endif // RED_CONFIGURATION_FINAL
}

Bool MorphTargetMesh::Rebuild_Targets()
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	if ( m_targets.Empty() )
	{
		return false;
	}
	
	THandle< CMesh > baseMeshHandle = m_baseMesh.Get();

	SMorphTargetBuildContext context;
	context.resource = this;
	context.baseMesh = baseMeshHandle.Get();
	for ( Uint32 iTex = 0; iTex < BTT_USED_MAX; ++iTex )
	{
		const BlendedTextureType texType = static_cast< BlendedTextureType >( iTex );
		context.baseTexture[ texType ] = nullptr;
		RED_ASSERT( MorphTargets::ShouldProcess( texType ) );
		if ( m_baseTexture.Get() )
		{
			THandle< CBitmapTexture > baseNormalMapHandle = SafeCast< CBitmapTexture >( m_baseTexture.Get() );
			context.baseTexture[ texType ] = baseNormalMapHandle.Get();
		}
	}

	THandle< rend::RenderMorphTargetMeshBlob > blob( RED_NEW( rend::RenderMorphTargetMeshBlob ) );
	blob->m_header.m_version = rend::RenderMorphTargetMeshBlob::FILE_VERSION;

	const Uint32 numTargets = m_targets.Size();

	blob->m_header.m_numTargets = numTargets;
	blob->m_header.m_targetStartsInVertexDiffs.Resize( numTargets );
	blob->m_header.m_targetStartsInVertexDiffsMapping.Resize( numTargets );
	blob->m_header.m_numVertexDiffsInEachChunk.Resize( numTargets, { rend::PoolMesh() } );
	blob->m_header.m_numVertexDiffsMappingInEachChunk.Resize( numTargets, { rend::PoolMesh() } );
	blob->m_header.m_targetPositionDiffScale.Resize( numTargets );
	blob->m_header.m_targetPositionDiffOffset.Resize( numTargets );

	blob->m_header.m_targetTextureDiffsData.Resize( numTargets );
	for ( Uint32 i = 0; i < numTargets; ++i )
	{
		blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsDataOffset.Resize( BTT_MAX );
		blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsDataSize.Resize( BTT_MAX );
		blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffScale.Resize( BTT_MAX );
		blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffOffset.Resize( BTT_MAX );
		blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsWidth.Resize( BTT_MAX );
	}

	red::DynArray< DataBuffer > targetVertexDiffs( numTargets, rend::PoolMesh() );
	red::DynArray< DataBuffer > targetVertexDiffsMappings( numTargets, rend::PoolMesh() );

	red::DynArray< red::StaticArray< DataBuffer, BTT_MAX > >	targetTextureDiffs { red::PoolFrame() };
	red::DynArray< red::StaticArray< Uint16, BTT_MAX > >		targetTextureDiffsWidth { red::PoolFrame() };
	red::DynArray< red::StaticArray< Uint8, BTT_MAX > >			targetTextureDiffsMipLevelCounts { red::PoolFrame() };
	red::DynArray< red::StaticArray< Vector4, BTT_MAX > >		targetTextureQuantizationScale { red::PoolFrame() };
	red::DynArray< red::StaticArray< Vector4, BTT_MAX > >		targetTextureQuantizationOffset { red::PoolFrame() };

	targetTextureDiffs.Reserve( numTargets );
	targetTextureDiffsWidth.Reserve( numTargets );
	targetTextureDiffsMipLevelCounts.Reserve( numTargets );
	targetTextureQuantizationScale.Reserve( numTargets );
	targetTextureQuantizationOffset.Reserve( numTargets );

	for ( Uint32 i = 0; i < numTargets; ++i )
	{
		targetTextureDiffs.PushBack(				red::StaticArray< DataBuffer,	BTT_MAX >( BTT_MAX ) );
		targetTextureDiffsWidth.PushBack(			red::StaticArray< Uint16,		BTT_MAX >( BTT_MAX ) );
		targetTextureDiffsMipLevelCounts.PushBack(	red::StaticArray< Uint8,		BTT_MAX >( BTT_MAX ) );
		targetTextureQuantizationScale.PushBack(	red::StaticArray< Vector4,		BTT_MAX >( BTT_MAX ) );
		targetTextureQuantizationOffset.PushBack(	red::StaticArray< Vector4,		BTT_MAX >( BTT_MAX ) );

		for ( Uint32 iTex = 0; iTex < BTT_MAX; ++iTex )
		{
			targetTextureDiffsWidth[ i ][ iTex ]			= 0;
			targetTextureDiffsMipLevelCounts[ i ][ iTex ]	= 0;
			targetTextureQuantizationScale[ i ][ iTex ]		= Vector4::ZEROS();
			targetTextureQuantizationOffset[i][ iTex ]		= Vector4::ZEROS();
		}
	}
	
	Uint32 totalVertexDiffsDataSize			= 0;
	Uint32 totalVertexDiffMappingDataSize	= 0;
	Uint32 offsetInVertexDiffs				= 0;
	Uint32 offsetInVertexDiffsMapping		= 0;
	Uint32 totalNumVertexDiffs				= 0;
	Uint32 totalNumVertexDiffsMappings		= 0;
	Uint32 totalTextureDiffsDataSize		= 0;

	for ( Uint32 i = 0; i < numTargets; ++i )
	{
		MorphTargetMeshEntry& target = m_targets[ i ];

		target.PrepareForCalculation();

		if ( !target.IsValid() )
		{
			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs'] is invalid, it will be skipped. Name valid: %d, Mesh valid: %d, mask valid: %d", target.GetName().AsChar(), target.IsNameValid(), target.IsMeshValid(), target.IsMaskValid() );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
			RED_DATA_ERROR_END();
			return false;
		}

		TargetVertexDiffsData targetVertexDiffsData( targetVertexDiffs[ i ], targetVertexDiffsMappings[ i ] );

		blob->m_header.m_targetStartsInVertexDiffs[ i ] = offsetInVertexDiffs;
		blob->m_header.m_targetStartsInVertexDiffsMapping[ i ] = offsetInVertexDiffsMapping;

		if ( !target.Build( context, targetVertexDiffsData, targetTextureDiffs[ i ], targetTextureDiffsWidth[ i ], targetTextureDiffsMipLevelCounts[ i ], targetTextureQuantizationScale[ i ], targetTextureQuantizationOffset[ i ] ) )
		{
			RED_DATA_ERROR_BEGIN( red::LoggerCategory_Rendering, 0, "Morph target ['%hs']: error buiding Target %d", target.GetName().AsChar(), i );
			RED_DATA_ERROR_ACTION_SHOW_ASSET( context.resource->GetPath().ToDebugString() );
			RED_DATA_ERROR_END();
			return false;
		}

		for ( const auto& numDiffsInChunk : targetVertexDiffsData.m_numVertexDiffsInEachChunk )
		{
			totalNumVertexDiffs += numDiffsInChunk;
			offsetInVertexDiffs	+= numDiffsInChunk;
		}
		for ( const auto& numDiffsMappingInChunk : targetVertexDiffsData.m_numVertexDiffsMappingInEachChunk )
		{
			totalNumVertexDiffsMappings += numDiffsMappingInChunk;
			offsetInVertexDiffsMapping	+= numDiffsMappingInChunk;
		}

		blob->m_header.m_numVertexDiffsMappingInEachChunk[ i ]	= targetVertexDiffsData.m_numVertexDiffsMappingInEachChunk;
		blob->m_header.m_numVertexDiffsInEachChunk[ i ]			= targetVertexDiffsData.m_numVertexDiffsInEachChunk;
		blob->m_header.m_targetPositionDiffScale[ i ]			= targetVertexDiffsData.m_quantizationScale;
		blob->m_header.m_targetPositionDiffOffset[ i ]			= targetVertexDiffsData.m_quantizationOffset;

		totalVertexDiffsDataSize		+= targetVertexDiffs[ i ].Size();
		totalVertexDiffMappingDataSize	+= targetVertexDiffsMappings[ i ].Size();

		for ( Uint32 texType = 0; texType < BTT_MAX; ++texType )
		{
			totalTextureDiffsDataSize += targetTextureDiffs[ i ][ texType ].Size();
		}
	}

	blob->m_header.m_numDiffs = totalNumVertexDiffs;
	blob->m_header.m_numDiffsMapping = totalNumVertexDiffsMappings;

	// Combine all vertex buffer diffs into one
	DataBuffer vertexDiffs( totalVertexDiffsDataSize );
	DataBuffer vertexDiffsMapping( totalVertexDiffMappingDataSize );

	void* diffsPtr = vertexDiffs.Data();
	void* mappingPtr = vertexDiffsMapping.Data();
	for ( Uint32 i = 0; i < numTargets; ++i )
	{
		red::Memcpy(					diffsPtr,	targetVertexDiffs[ i ].Data(),			targetVertexDiffs[ i ].Size()			);
		red::Memcpy(					mappingPtr,	targetVertexDiffsMappings[ i ].Data(),	targetVertexDiffsMappings[ i ].Size()	);
	
		diffsPtr	= red::OffsetPtr(	diffsPtr,	targetVertexDiffs[ i ].Size()			);
		mappingPtr	= red::OffsetPtr(	mappingPtr,	targetVertexDiffsMappings[ i ].Size()	);
	}

	blob->m_baseBlob				= baseMeshHandle->GetRenderResourceBlob();
	blob->m_diffsBuffer				= DataBuffer::Copy( vertexDiffs );
	blob->m_mappingBuffer			= DataBuffer::Copy( vertexDiffsMapping );

	// Combine all texture buffer diffs into one
	DataBuffer textureDiffs( totalTextureDiffsDataSize );
	void* textureDiffsPtr = textureDiffs.Data();

	Uint32 currentOffsetInTextureDiffsBuffer = 0;

	for ( Uint32 i = 0; i < numTargets; ++i )
	{
		for ( Uint32 iTex = 0; iTex < BTT_MAX; ++iTex )
		{
			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffOffset[ iTex ]				= targetTextureQuantizationOffset[ i ][ iTex ];
			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffScale[ iTex ]				= targetTextureQuantizationScale[ i ][ iTex ];
			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsDataSize[ iTex ]			= targetTextureDiffs[ i ][ iTex ].Size();
			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsDataOffset[ iTex ]		= currentOffsetInTextureDiffsBuffer;

			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsWidth[ iTex ]				= targetTextureDiffsWidth[ i ][ iTex ];
			blob->m_header.m_targetTextureDiffsData[ i ].m_targetDiffsMipLevelCounts[ iTex ]	= targetTextureDiffsMipLevelCounts[ i ][ iTex ];

			red::Memcpy( textureDiffsPtr, targetTextureDiffs[ i ][ iTex ].Data(), targetTextureDiffs[ i ][ iTex ].Size() );
			textureDiffsPtr	= red::OffsetPtr( textureDiffsPtr, targetTextureDiffs[ i ][ iTex ].Size() );

			currentOffsetInTextureDiffsBuffer += targetTextureDiffs[ i ][ iTex ].Size();
		}
	}

	blob->m_textureDiffsBuffer.Convert_SetContentDirect( textureDiffs );

	//////////////////////////////////////////////////////////////////////////

	m_blob = blob;
	m_renderResource = nullptr;

	//refresh the mesh appearance if changed to default one because couldn't find specified one
	NotifyPropertyChanged( rtti::AccessPath( "targets" ) );

	return true;
#endif // RED_CONFIGURATION_FINAL
}

Bool MorphTargetMesh::ShouldRebuild( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
{
	// no automatic rebuilding of morphs, always do it manually
	return false;

#if 0
	rtti::PathParser parser( path );

	CName propertyName;
	parser.EatName( propertyName );

	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "baseMesh" ) )
	{
		return true;
	}
	else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "baseMeshAppearance" ) )
	{
		return false;
	}

	RED_ASSERT( propertyName == RED_NAME_CONSTEXPR_NOREG( "targets" ), "Editable property '%hs' added to MorphTargetMesh, decide if changing it should trigger rebuilding of MorphTargetMesh.", propertyName.AsChar() );

	Int32 index;
	parser.EatIndex( index );

	CName targetPropertyName;
	parser.EatName( targetPropertyName );

	if ( targetPropertyName.Empty() )
	{
		// we're changing the targets array (adding/removing stuff) -> do a rebuild
		return true;
	}

	if ( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "mesh" ) )
	{
		// changing target mesh -> rebuild
		MorphTargetMeshEntry& target = m_targets[ index ];
		if ( target.GetMesh() )
		{
			target.SetMeshAppearance( target.GetMesh()->GetDefaultAppearance()->GetName() );
		}
		return true;
	}

	if ( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "meshAppearance" ) )
	{
		// don't rebuild on mesh appearance change because it could be too frequent
		return false;
	}

	if ( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "maskTexture" ) )
	{
		// changing target mask texture -> rebuild
		return true;
	}

	//TODO - maybe we would like to trigger Rebuild when face region changes? (because vertex differences for beards might change according to their region)
	/*if ( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "regionName" ) )
	{
		red::String newRegionName = newValue->GetSingleValue();
		MorphTarget_FaceRegion faceRegion = MapRegion( RED_NAME_CONSTEXPR(newRegionName.AsChar()) );
		if ( faceRegion == FACE_REGION_JAW || faceRegion == FACE_REGION_MOUTH || faceRegion == FACE_REGION_NOSE || faceRegion == FACE_REGION_EARS )
		{
			return true;
		}
	}*/

	if ( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "textureBlendInfo" ) )
	{
		// don't rebuild on switching texture blends, because it could be too frequent
		return false;
	}

	RED_ASSERT( targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "name" ) || targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "regionName" ) || targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "faceRegion" ) || targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "preview" ) || targetPropertyName == RED_NAME_CONSTEXPR_NOREG( "weight" ),
				"Editable property '%hs' added to MorphTargetMeshEntry, decide if changing it should trigger rebuilding of MorphTargetMesh.", targetPropertyName.AsChar() );

	return false;
#endif
}

void MorphTargetMesh::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
{
	TBaseClass::OnPropertyPostChange( path, oldValue, newValue );

	rtti::PathParser parser( path );
	CName propertyName;
	parser.EatName( propertyName );

#ifndef RED_CONFIGURATION_FINAL
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "blendTexture" ) )
	{
		for ( MorphTargetMeshEntry& morphEntry : m_targets )
		{
			morphEntry.SetTextureBlend( m_blendTexture );
		}

		NotifyPropertyChanged( rtti::AccessPath( "targets" ) );
	}
#endif // RED_CONFIGURATION_FINAL

	//update morph entries
	for ( MorphTargetMeshEntry& morphEntry : m_targets )
	{
		morphEntry.CacheFaceRegion();
		morphEntry.CacheBonesAndRigMatrices();
	}

	if ( ShouldRebuild( path, oldValue, newValue ) )
	{
		Rebuild();
	}
}

Bool MorphTargetMesh::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR( "baseTexture" ) )
	{
		auto baseTextures = readValue.Get< red::StaticArray< TResRef< ITexture >, MorphTargets::BTT_MAX > >();
		m_baseTexture = baseTextures[ BTT_Normal ];
		return true;
	}

	if ( propertyName == RED_NAME_CONSTEXPR( "baseTextureParamName" ) )
	{
		auto baseTextureParamNames = readValue.Get< red::StaticArray< CName, MorphTargets::BTT_MAX > >();
		m_baseTextureParamName = baseTextureParamNames[ BTT_Normal ];
		return true;
	}
	return TBaseClass::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}

void MorphTargetMesh::OnPreSave( const PreSaveContext& context )
{
#ifndef RED_CONFIGURATION_FINAL
	m_resourceVersion = Version::NEWEST;
#endif // RED_CONFIGURATION_FINAL

	TBaseClass::OnPreSave( context );
}

void MorphTargetMesh::OnPostLoad( const PostLoadContext& context )
{
	for ( MorphTargetMeshEntry& morphEntry : m_targets )
	{
		morphEntry.CacheFaceRegion();
		morphEntry.CacheBonesAndRigMatrices();
	}

	if ( GRender && !GRender->IsDeviceLost() && m_blob && m_blob->IsVersionValid() && !context.flags.skipUploadRenderData )
	{
#ifdef USE_RESOURCE_METRICS_BANK
		auto* resourceMetricsBank = GResourceLoader->GetResourceMetricsBank();

		if ( m_renderResource )
		{
			if ( GetPath().IsValid() )
			{
				resourceMetricsBank->OnUnloadMesh( GetPath() );
			}
		}
#endif // USE_RESOURCE_METRICS_BANK

		m_renderResource = GRender->UploadBlobMesh( SRenderDataSource{ GetPath() }, m_blob );

#ifdef USE_RESOURCE_METRICS_BANK
		if ( m_renderResource && GetPath().IsValid() )
		{
			res::SingleResourceMetrics meshMemoryMetrics{};
			meshMemoryMetrics.path = GetPath();
			meshMemoryMetrics.gpuBytesUsed = m_renderResource->GetUsedVideoMemory();
			meshMemoryMetrics.mesh.isSkinned = false;
			meshMemoryMetrics.mesh.hasPhysics = false;

			resourceMetricsBank->OnLoadMesh( meshMemoryMetrics );
		}
#endif // USE_RESOURCE_METRICS_BANK
	}

	for ( Uint32 i = 0; i < MorphTargets::BTT_USED_MAX; ++i )
	{
		const BlendedTextureType texType = (BlendedTextureType)i;
		RED_ASSERT( MorphTargets::ShouldProcess( texType ) );
		if ( m_baseTextureParamName.Empty() || !m_baseTexture.IsValid() )
		{
			auto chunkMaterial = GetChunkMaterial( m_baseMesh.Get(), m_baseMeshAppearance, 0 );
			if ( m_baseTextureParamName.Empty() )
			{
				m_baseTextureParamName = FindTextureNameInMaterial( chunkMaterial, GetTextureName( texType ) );
			}
			m_baseTexture = FindTextureInMaterial( chunkMaterial, m_baseTextureParamName );
		}
	}

	UpgradeVersion();
}

void MorphTargetMesh::UpgradeVersion()
{
#ifndef RED_CONFIGURATION_FINAL
	if ( m_resourceVersion < Version::NORMALMAP_BLEND_ONLY )
	{
		// deprecated
	}

	if ( m_resourceVersion < Version::CACHE_NORMALMAP_BLEND )
	{
		// deprecated
	}

	static_assert( Version::NEWEST == Version::CACHE_NORMALMAP_BLEND, "Version has been bumped. Need to make upgrades?" );
#endif // RED_CONFIGURATION_FINAL
}