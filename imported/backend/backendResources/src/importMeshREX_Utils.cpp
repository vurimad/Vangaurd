#include "build.h"

#include "importMeshREX_Common.h"
#include "importMeshREX_Utils.h"


#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamImportedSnapPoint.h"
#include "../../../common/resourceMesh/include/meshParamGarmentSupport.h"
#include "../../../common/resourceMesh/include/meshParamGpuBuffer.h"
#include "../../../common/resourceMesh/include/meshParamUICollisionData.h"
#include "../../../common/resourceMesh/include/meshParamWorkspotOffsets.h"
#include "../../../common/resourceMesh/include/meshParamWaterPatchData.h"

#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"

#include "../../../backend/backendREA/include/reFileHelpers.h"
#include "../../../backend/backendREA/include/reMaterialHelpers.h"

#include "../../../backend/backendVisibility/include/visOccluderMeshCompiler.h"
#include "../../../backend/backendVisibility/include/visOccluderMeshCompilationSource.h"

#include "../../../common/renderData/include/visibilityWorldCommon.h"
#include "../../backendEngine/include/importer.h"
#include "../../../common/resourceMesh/include/meshParamOccluderData.h"

static const String c_strUI( "_UI" );
	


void ImportGarmentSupportData( THandle< CMesh >& meshData, std::vector< ReFileMeshPtr >& meshes )
{
	mesh::DataView view( *meshData );
	if( auto existingParam = meshData->GetParameter< mesh::MeshParamGarmentSupport >() )
	{
		meshData->RemoveParameter( existingParam );
	}
	auto param = meshData->CreateParameter< mesh::MeshParamGarmentSupport >();

	const Uint32 numChunks = view.GetNumChunks();
	bool hasPerfectMorph = false;
	bool hasAnyCapsData = false;
	auto& reMesh = meshes[ 0 ];
	const Uint32 numReChunks = reMesh->getChunkCount();

	for( Uint32 chunk_num = 0; chunk_num < numReChunks; ++chunk_num )
	{
		auto& reChunk = reMesh->getChunk( chunk_num );
		if( reChunk->getAttr( "re_attrgarmentPerfectMorph" ) > 0. )
		{
			param->m_customMorph = true;
			hasPerfectMorph = true;
		}

		auto& indices = param->m_chunkCapVertices.EmplaceBack();
		Int32* dataCaps = nullptr;
		Int32* dataFlags = nullptr;
		Uint32 count = 0, countFlags = 0;
		// Modify flags channel in re before channel import to merge info from caps channel there.
		if( reChunk->getDataChannel( "re_attrgarmentCaps", count, dataCaps ) )
		{
			reChunk->getDataChannel( "re_attrgarmentFlags", countFlags, dataFlags );
			for( Uint32 idx = 0; idx < count; ++idx )
			{
				if( dataCaps[ idx ] > 0 )
				{
					indices.PushBack( idx );
					hasAnyCapsData = true;
					if( dataFlags && idx < countFlags )
					{
						reChunk->setDataChannelValue( "re_attrgarmentFlags", idx, dataFlags[ idx ] | 0x100 );
					}
				}
			}
		}
	}

	if( !hasPerfectMorph && !hasAnyCapsData )
	{
		meshData->RemoveParameter( param );
	}
}

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


void ImportMeshDataParameters( std::vector< ReFileMeshPtr >& meshes, THandle<CMesh>& meshData )
{
	// TODO remove type prefixes
	const ReFileString dataChannelType( "re_buffDatas1_Type" );
	const ReFileString dataChannelValue( "re_buffDataf1_Value" );
	const ReFileString dataChannelStride( "re_buffDatai1_Stride" );

	// RED_ASSERT( meshes.Size() == 1 );
	if( meshes.size() > 1 )
	{
		RED_LOG_ERROR( "Source .re file has more than one mesh defined. Only 1 mesh per .re file supported." );
		return;
	}

	Uint32 meshesSize = (Uint32)meshes.size();
	for( Uint32 i = 0; i < meshesSize; i++ )
	{
		ReFileMeshPtr mesh = meshes[ i ];
		Uint32 outParam_DataChSize = 0;

		Bool hasValidChannels = mesh->hasDataChannel( dataChannelType );
		hasValidChannels &= mesh->hasDataChannel( dataChannelValue );

		Uint32 paramTypes_DataChSize, paramValues_DataChSize = 0;

		if( hasValidChannels )
		{
			mesh->getDataChannelSize( dataChannelType, paramTypes_DataChSize );
			mesh->getDataChannelSize( dataChannelValue, paramValues_DataChSize );

			if( paramTypes_DataChSize > 0 )
			{
				ReFileDataChannel::ReDataChannelType dataType;
				mesh->getDataChannelType( dataChannelValue, dataType );

				void* data = nullptr;
				Uint32 elemStride = 0;

				switch( dataType )
				{
				case ReFileDataChannel::RE_FLOAT_CHANNEL_TYPE:
				{
					Float* dataF1 = nullptr;
					elemStride = sizeof( Float );
					mesh->getDataChannel( dataChannelValue, paramValues_DataChSize, dataF1 );
					data = dataF1;
				}
				break;
				case ReFileDataChannel::RE_VECTOR2_CHANNEL_TYPE:
				{
					reFloat2* dataF2 = nullptr;
					elemStride = sizeof( reFloat2 );
					mesh->getDataChannel( dataChannelValue, paramValues_DataChSize, dataF2 );
					data = dataF2;
				}
				break;
				case ReFileDataChannel::RE_VECTOR3_CHANNEL_TYPE:
				{
					reFloat3* dataF3 = nullptr;
					elemStride = sizeof( reFloat3 );
					mesh->getDataChannel( dataChannelValue, paramValues_DataChSize, dataF3 );
					data = dataF3;
				}
				break;
				case ReFileDataChannel::RE_VECTOR4_CHANNEL_TYPE:
				{
					reFloat4* dataF4 = nullptr;
					elemStride = sizeof( reFloat4 );
					mesh->getDataChannel( dataChannelValue, paramValues_DataChSize, dataF4 );
					data = dataF4;
				}
				break;
				default:
				{
					RED_ASSERT( false, "Unsupported buffer data type." );
					return;
				}
				break;
				}

				reString dataName;
				mesh->getDataChannelValue( dataChannelType, i, dataName );

				if( strcmp( dataName.getData(), "destruction_buffer" ) == 0 )
				{
					THandle< mesh::MeshParamGpuBuffer > existingParam = meshData->GetParameter< mesh::MeshParamGpuBuffer >();
					if( existingParam != nullptr )
					{
						meshData->RemoveParameter( existingParam );
					}
					elemStride = sizeof( reFloat3 );
					meshData->AddParameter( CreateHandle< mesh::MeshParamGpuBuffer >( elemStride, elemStride * paramValues_DataChSize, data ) );
				}
				else if( strcmp( dataName.getData(), "water_patch_buffer" ) == 0 )
				{
					THandle< mesh::MeshParamWaterPatchData > existingParam = meshData->GetParameter< mesh::MeshParamWaterPatchData >();
					if( existingParam != nullptr )
					{
						meshData->RemoveParameter( existingParam );
					}
					meshData->AddParameter( CreateHandle< mesh::MeshParamWaterPatchData >( paramValues_DataChSize, elemStride, (const Float*)data ) );
				}
			}
		}

		const ReFileMesh& reMesh = *meshes[ i ];
		Int32 numChunks = reMesh.getNumChunks();
		if( numChunks == 0 )
			continue;
		for( Int32 c = 0; c < numChunks; c++ )
		{
			ReFileMeshChunkPtr reChunk = reMesh.getMeshChunk( c );
			String matName;
			ReFileHelpers::GetMaterialName( *reChunk.get(), matName );
			if( ( (String)reChunk->getOriginalName() ).EndsWith( c_strUI ) || matName.EndsWith( c_strUI ) )
			{
				THandle< mesh::MeshParamUICollisionData > existingParam = meshData->GetParameter< mesh::MeshParamUICollisionData >();
				if( existingParam != nullptr )
				{
					meshData->RemoveParameter( existingParam );
				}

				reFloat2* uv1Data = nullptr;
				unsigned int uv1DataCount = 0;
				reChunk->getDataChannel( "UV1", uv1DataCount, uv1Data );

				red::DynArray< Vector2 > uvs;
				for( unsigned int i = 0; i < uv1DataCount; i++ )
				{
					uvs.PushBack( Vector2( uv1Data[ i ].x, uv1Data[ i ].y ) );
				}

				// --- Importing Points
				unsigned int pDataCount = 0;
				reFloat3* pData = nullptr;
				reChunk->getDataChannel( "P", pDataCount, pData );

				red::DynArray< Vector3 > _positions;
				for( Int32 v = 0; v < reChunk->getNumVertices(); v++ )
				{
					_positions.PushBack( Vector3( pData[ v ].x * c_fromReScale, pData[ v ].y * c_fromReScale, pData[ v ].z * c_fromReScale ) );
				}

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
				{
					RED_LOG_ERROR( "Problems while generating UI mesh param for imported mesh - inconsistent face data in UI mesh chunk." );
					continue;
				}

				red::DynArray< Uint16 > indices;
				indices.Reserve( 3 * fv0iCount );

				for( unsigned int i = 0; i < fv2iCount; i++ )
				{
					indices.PushBack( fv0Data[ i ] );
					indices.PushBack( fv1Data[ i ] );
					indices.PushBack( fv2Data[ i ] );
				}

				meshData->AddParameter( CreateHandle< mesh::MeshParamUICollisionData >( uvs, indices, _positions ) );
				//One UI mesh parameter per mesh
				break;
			}
		}
	}
}

void ImportBBoxData( const ReFileMeshPtr& mesh, THandle< CMesh >& meshData, Bool& rebuildBBox )
{
	if( mesh->hasDataChannel( "re_bbox" ) )
	{
		float* bboxData = nullptr;
		unsigned int bboxDataCount = 0;
		mesh->getDataChannel( "re_bbox", bboxDataCount, bboxData );
		if( bboxDataCount == 6 )
		{
			Vector4 bbox_min( bboxData[ 0 ], bboxData[ 1 ], bboxData[ 2 ] );
			Vector4 bbox_max( bboxData[ 3 ], bboxData[ 4 ], bboxData[ 5 ] );
			Box newBBox( bbox_min, bbox_max );
#ifndef RED_CONFIGURATION_FINAL
			meshData->Editor_SetBoundingBox( newBBox );
			rebuildBBox = false;
#endif // RED_CONFIGURATION_FINAL
		}
	}
}

void ImportSnapFeatures( std::vector< ReFileMeshPtr >& meshes, THandle< CMesh >& meshData )
{
	red::DynArray< mesh::MeshImportedSnapPointPtr > snapFeatures;
	Uint32 meshesSize = (Uint32)meshes.size();
	auto param = meshData->CreateParameter< mesh::MeshParamImportedSnapPoint >();
	for( auto mesh : meshes )
	{
		for( auto& el : mesh->mSnapFeatures )
		{
			Matrix snapFeatureMatrix = ReFileHelpers::GetReTransformMatrix( el );
			mesh::ImportedSnapTags importedTags;
			for( auto& tag : el.mIncludeTags )
			{
				importedTags.m_includeTags.PushBack( RED_NAME( tag.getData() ) );
			}

			for( auto& tag : el.mExcludeTags )
			{
				importedTags.m_excludeTags.PushBack( RED_NAME( tag.getData() ) );
			}

			mesh::MeshImportedSnapPointPtr snap( RED_NEW( mesh::MeshImportedSnapPoint, red::PoolBackend() )( snapFeatureMatrix, el.getRadius() * c_fromReScale, el.getRotationSteps(), importedTags ) );
			snapFeatures.PushBack( snap );
		}
	
	}
	if( snapFeatures.Size() > 0 )
		param->SetSnapPoints( snapFeatures );
}

class ReOccluderMeshCompilationSource : public tools::vis::IOccluderMeshCompilationSource
{
public:
	ReOccluderMeshCompilationSource( std::vector< ReFileOcclusionMeshPtr >& reOccluders, const red::StringView& sourceName )
		: m_reOccluders( reOccluders )
		, m_sourceName( sourceName )
	{
	}

	virtual String GetMeshName() const override
	{
		return m_sourceName.ToString();
	}

	virtual Uint32 GetNumChunks() const override
	{
		return (Uint32)m_reOccluders.size();
	}

	virtual ChunkData GetChunkData( Uint32 chunkIndex ) const override
	{
		ChunkData data;

		ReFileOcclusionMeshPtr reMesh = m_reOccluders[ chunkIndex ];

		Int32 numVertices = reMesh->getNumVertices();
		data.m_vertices.Resize( numVertices );
		for( Int32 i = 0; i < numVertices; ++i )
		{
			data.m_vertices[ i ].X = reMesh->mVertices[ i * 3 + 0 ] * c_fromReScale;
			data.m_vertices[ i ].Y = reMesh->mVertices[ i * 3 + 1 ] * c_fromReScale;
			data.m_vertices[ i ].Z = reMesh->mVertices[ i * 3 + 2 ] * c_fromReScale;
		}

		Int32 numFaces = reMesh->getNumFaces();
		data.m_indices.Resize( numFaces * 3 );
		for( Int32 i = 0; i < numFaces; ++i )
		{
			// NOTE: Reversed winding
			data.m_indices[ i * 3 + 0 ] = reMesh->mIndices[ i * 3 + 2 ];
			data.m_indices[ i * 3 + 1 ] = reMesh->mIndices[ i * 3 + 1 ];
			data.m_indices[ i * 3 + 2 ] = reMesh->mIndices[ i * 3 + 0 ];
		}

		data.m_twoSided = reMesh->isDoubleSided();

		return data;
	}

	virtual Bool GetChunkTwoSided( Uint32 chunkIndex ) const override
	{
		ReFileOcclusionMeshPtr reMesh = m_reOccluders[ chunkIndex ];
		return reMesh->isDoubleSided();
	}

private:
	std::vector< ReFileOcclusionMeshPtr >& m_reOccluders;
	red::StringView m_sourceName;
};

Bool ImportOccluder( std::vector< ReFileOcclusionMeshPtr >& reOccluders, const red::StringView& sourceName, THandle< vis::IOccluderResource >& outOccluderResource )
{
	ReOccluderMeshCompilationSource source( reOccluders, sourceName );

	tools::vis::OccluderMeshCompiler::Settings settings;
	tools::vis::OccluderMeshCompiler compiler( settings );

	if( !compiler.Compile( source, outOccluderResource ) )
	{
		RED_LOG_ERROR( "MeshImport: Failed to compile occluder mesh: %hs", compiler.GetStatusMessage().AsChar() );
		return false;
	}

	return true;
}

Bool ImportFloatTrackNames( ReFileMeshPtr mesh, mesh::DataBuilder& buildData )
{
	ReFileString* trackNamesData;
	unsigned int trackNamesCount = 0;
	mesh->getDataChannel( "channels", trackNamesCount, trackNamesData );

	if( trackNamesCount == 0 )
	{
		return false;
	}

	for( unsigned int i = 0; i < trackNamesCount; i++ )
	{
		CName floatTrackName = RED_NAME( trackNamesData[ i ].getData() );

		if( !floatTrackName.Empty() )
			buildData.m_floatTrackNames.PushBack( floatTrackName );
	}

	return true;
}


void ImportWorkspotOffsets( const std::vector< ReFileMeshPtr >& meshes, THandle< CMesh >& meshData )
{
	red::DynArray< CName > names;
	red::DynArray< Matrix > offsets;

	for( auto el : meshes )
	{
		const auto offsetNodes = el->getWorkspotOffsets();

		for( auto el : offsetNodes )
		{
			names.PushBack( RED_NAME( el->getName().getData() ) );
			offsets.PushBack( ReFileHelpers::GetReTransformMatrix( el ) );
		}
	}

	if( names.Size() > 0 && offsets.Size() > 0 )
	{
		THandle< mesh::MeshParamWorkspotOffsets > parameter = meshData->CreateParameter< mesh::MeshParamWorkspotOffsets >();
		parameter->SetWorkspotOffsets( names, offsets );
	}
}


ImportResult ImportOcclusionData( std::vector< ReFileOcclusionMeshPtr >& reOccluders, const red::AbsolutePath sourceFilePath, THandle< CREXMeshResourceImportOptions >& options, THandle< CMesh >& meshData )
{
	if( options->m_occluderType != vis::WorldOccluderType::None && !reOccluders.empty() )
	{
		THandle< vis::IOccluderResource > occResource;
		if( !ImportOccluder( reOccluders, sourceFilePath.AsStringView(), occResource ) )
		{
			return ImportResult::Error( "Failed to import occluder from mesh" );
		}

		auto param = meshData->CreateParameter< mesh::MeshParamOccluderData >();
		param->SetOccluderResource( occResource );
		vis::WorldOccluderType occType = options->m_occluderType;
		if( occType == vis::WorldOccluderType::Default )
		{
			occType = vis::WorldOccluderType::Exterior;
		}
		param->SetDefaultOccluderType( occType );
	}
	else
	{
		meshData->RemoveParameter( mesh::MeshParamOccluderData::GetStaticClass() );
	}
	return ImportResult::Success( meshData );
}
