#include "build.h"
#include "mesh.h"
#include "meshDataViewRO.h"
#include "meshAppearance.h"
#include "meshPacker.h"

#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/redReflection/include/resourceReference.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkFlags, mesh );
RTTI_PROPERTY( m_renderInScene ).editable();
RTTI_PROPERTY( m_renderInShadows ).editable();
RTTI_PROPERTY( m_isTwoSided ).editable();
RTTI_PROPERTY( m_isRayTracedEmissive ).editable();
RTTI_END_TYPE();

namespace
{
	// Used to avoid returning THandle< IMaterial > by value, and using const& instead.
	const THandle< IMaterial > s_nullMaterialPtr( nullptr );

	// Used to avoid returning THandle< MeshAppearance > by value, and using const& instead.
	const THandle< mesh::MeshAppearance > s_nullAppearancePtr( nullptr );
}

namespace mesh
{

	DataViewRO::DataViewRO( const CMesh& mesh )
		: m_meshRO( &mesh )
	{
	}

	DataViewRO::~DataViewRO() = default;

	LODIndex DataViewRO::GetNumLODs() const
	{
		return m_meshRO->m_lodLevelInfo.Size();
	}

	ChunkIndex DataViewRO::GetNumChunks() const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		return m_meshRO->m_chunks.Size();
#endif // RED_CONFIGURATION_FINAL
	}

	MaterialIndex DataViewRO::GetNumMaterials() const
	{
		return m_meshRO->m_materialEntries.Size();
	}

	BoneIndex DataViewRO::GetNumBones() const
	{
		return m_meshRO->m_boneNames.Size();
	}

	BoneIndex DataViewRO::GetNumBoneRigMatrices() const
	{
		return m_meshRO->m_boneRigMatrices.Size();
	}

	Uint32 DataViewRO::GetNumAppearances() const
	{
		return m_meshRO->m_appearances.Size();
	}

	Uint32 DataViewRO::GetNumFloatTracks() const
	{
		return m_meshRO->m_floatTrackNames.Size();
	}

	Float DataViewRO::GetLODDistance( LODIndex lodIndex ) const
	{
		if( lodIndex >= m_meshRO->m_lodLevelInfo.Size() )
			return 0.0f;

		return m_meshRO->m_lodLevelInfo[ lodIndex ];
	}

	Uint32 DataViewRO::GetLODChunkCount( LODIndex lodIndex ) const
	{
		if( lodIndex >= m_meshRO->m_lodLevelInfo.Size() )
			return 0;

		Uint32 count = 0;
#ifndef RED_CONFIGURATION_FINAL
		const auto lodMask = 1 << lodIndex;
		for( const auto& chunkInfo : m_meshRO->m_chunks )
		{
			if( chunkInfo.m_lodMask == (Uint8)lodMask )
				++count;
		}
#endif // RED_CONFIGURATION_FINAL

		return count;
	}

	const red::DynArray< Uint16 > DataViewRO::GetLODChunkIndices( LODIndex lodIndex ) const
	{
		if( lodIndex >= m_meshRO->m_lodLevelInfo.Size() )
		{
			static red::DynArray< Uint16 > emptyArray{rend::PoolMesh()};
			return emptyArray;
		}

		red::DynArray< Uint16 > chunks{rend::PoolMesh()};
#ifndef RED_CONFIGURATION_FINAL
		const auto lodMask = 1 << lodIndex;
		for( auto index : m_meshRO->m_chunks.Indices() )
		{
			const auto& chunkInfo = m_meshRO->m_chunks[ index ];
			if( chunkInfo.m_lodMask == (Uint8)lodMask )
				chunks.PushBack( index );
		}
#endif // RED_CONFIGURATION_FINAL

		return chunks;
	}

	Float DataViewRO::GetAutoHideDistance( const Vector3& scale ) const
	{
		return m_meshRO->GetAutoHideDistance( scale );
	}

	Uint8 DataViewRO::GetChunkLodMask( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_lodMask;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint64 DataViewRO::GetChunkStreamMask( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_streamMask;
#endif // RED_CONFIGURATION_FINAL
	}

	EMeshVertexType DataViewRO::GetChunkVertexType( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		// does not seem to be called in game in final configuration
		return MVT_Max;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return MVT_StaticMesh;

		return m_meshRO->m_chunks[ chunkIndex ].m_vertexType;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint32 DataViewRO::GetChunkVertexCount( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_numVertices;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint32 DataViewRO::GetChunkIndexCount( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_numIndices;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint32 DataViewRO::GetChunkTriangleCount( ChunkIndex chunkIndex ) const
	{
		Uint32 twoSidedMultiplier = GetChunkFlags( chunkIndex ).m_isTwoSided ? 2u : 1u;
		return ( GetChunkIndexCount( chunkIndex ) * twoSidedMultiplier ) / 3u;
	}

	ChunkStreamDataRO DataViewRO::GetChunkStreamData( ChunkIndex chunkIndex, EMeshStreamType meshStream ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return ChunkStreamDataRO();
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return ChunkStreamDataRO();

		auto& chunk = m_meshRO->m_chunks[ chunkIndex ];

		const SMeshStream* stream = nullptr;
		for( auto& currentStream : chunk.m_streams )
		{
			if( currentStream.m_type == meshStream )
			{
				stream = &currentStream;
				break;
			}
		}

		if( !stream )
			return ChunkStreamDataRO();

		auto buffer = stream->m_data.Editor_LoadContentDirect();
		if( !buffer.Data() )
			return ChunkStreamDataRO();

		ChunkStreamDataRO data;
		data.m_stream = meshStream;
		data.m_data = buffer.Data();
		data.m_buffer = buffer;
		data.m_stride = MeshPackerUtils::GetMeshStreamSize( stream->m_type );
		data.m_count = buffer.GetSize() / data.m_stride;

		return data;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint8 DataViewRO::GetChunkNumBonesPerVertex( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_numBonesPerVertex;
#endif // RED_CONFIGURATION_FINAL
	}

	Uint8 DataViewRO::GetChunkMaterialCount( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0;

		return m_meshRO->m_chunks[ chunkIndex ].m_materialID.Size();
#endif // RED_CONFIGURATION_FINAL
	}

	CName DataViewRO::GetChunkMaterialName( ChunkIndex chunkIndex, ChunkMaterialIndex index ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return CName();
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return CName();

		const auto& chunk = m_meshRO->m_chunks[ chunkIndex ];
		if( index >= chunk.m_materialID.Size() )
			return CName();

		return chunk.m_materialID[ index ];
#endif // RED_CONFIGURATION_FINAL
	}

	const red::DynArray< CName >& DataViewRO::GetChunkMaterialNames( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		static red::DynArray< CName > emptyNames{ rend::PoolMesh() };
		return emptyNames;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
		{
			static red::DynArray< CName > emptyNames{rend::PoolMesh()};
			return emptyNames;
		}

		return m_meshRO->m_chunks[ chunkIndex ].m_materialID;
#endif // RED_CONFIGURATION_FINAL
	}

	const String& DataViewRO::GetChunkSourceMeshUUID( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return String::EMPTY();
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return String::EMPTY();

		const auto& chunk = m_meshRO->m_chunks[ chunkIndex ];
		return chunk.m_sourceMeshUUID;
#endif // RED_CONFIGURATION_FINAL
	}

	const String& DataViewRO::GetChunkSourceMaterialUUID( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return String::EMPTY();
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return String::EMPTY();

		const auto& chunk = m_meshRO->m_chunks[ chunkIndex ];
		return chunk.m_sourceMaterialUUID;
#endif // RED_CONFIGURATION_FINAL
	}

	ChunkFlags DataViewRO::GetChunkFlags( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return ChunkFlags();
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return ChunkFlags();

		const EMeshChunkFlags renderMask = m_meshRO->m_chunks[ chunkIndex ].m_chunkRenderMask;

		ChunkFlags flags;
		flags.m_renderInScene = ( renderMask & MCF_RenderInScene ) != 0;
		flags.m_renderInShadows = ( renderMask & MCF_RenderInShadows ) != 0;
		flags.m_isTwoSided = ( renderMask & MCF_IsTwoSided ) != 0;
		flags.m_isRayTracedEmissive = ( renderMask & MCF_IsRayTracedEmissive ) != 0;
		return flags;
#endif // RED_CONFIGURATION_FINAL
	}

	Double DataViewRO::GetChunkSurfaceArea( ChunkIndex chunkIndex ) const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0.0;
#else
		if( chunkIndex >= m_meshRO->m_chunks.Size() )
			return 0.0;

		const mesh::StreamViewRO< Vector3 > dataPosition( GetChunkStreamData( chunkIndex, MST_Position_3F ) );
		const mesh::StreamViewRO< Uint16 > dataIndices( GetChunkStreamData( chunkIndex, MST_Index_U16 ) );

		Double surfaceArea = 0.0;

		for( Uint32 i = 0; i < dataIndices.Size(); i += 3 )
		{
			const Vector4 vertex0 = dataPosition[ dataIndices[ i + 0 ] ];
			const Vector4 vertex1 = dataPosition[ dataIndices[ i + 1 ] ];
			const Vector4 vertex2 = dataPosition[ dataIndices[ i + 2 ] ];

			const Vector4 cross = Vector4::Cross( vertex0 - vertex1, vertex0 - vertex2, 0.0f );
			const Float triangleArea = cross.Mag3() / 2.0f;
			surfaceArea += triangleArea;
		}

		return surfaceArea;
#endif // RED_CONFIGURATION_FINAL
	}

	namespace
	{
		static void ExtractBytes( Uint32 value, Uint8* outBytes )
		{
			outBytes[ 0 ] = static_cast< Uint8 >( value & 0xFF ); // Little endian.
			outBytes[ 1 ] = static_cast< Uint8 >( ( value >> 8 ) & 0xFF );
			outBytes[ 2 ] = static_cast< Uint8 >( ( value >> 16 ) & 0xFF );
			outBytes[ 3 ] = static_cast< Uint8 >( ( value >> 24 ) & 0xFF );
		}

		red::THash64 CalculateHash64( Uint32 value, red::THash64 baseHash ) // Has of 32bit value, for convenience.
		{
			Uint8 data[ 4 ];
			ExtractBytes( value, data );
			return red::CalculateHash64( data, sizeof( data ), baseHash );
		}
	}

	Uint64 DataViewRO::CalculateGeometryHash() const
	{
#ifdef RED_CONFIGURATION_FINAL
		return 0;
#else
		Uint64 hash = RED_FNV_OFFSET_BASIS64;

		red::DynArray< EMeshStreamType > streamTypes{rend::PoolMesh()};
		streamTypes.Reserve( MAX_MESH_STREAMS );

		Uint32 numChunks = GetNumChunks();

		hash = CalculateHash64( numChunks, hash );
		for( const SMeshChunkPacked& chunk : m_meshRO->m_chunks ) // For better collision detection it's best to first hash numbers of streams.
		{
			const Uint8 isTwoSided = ( chunk.m_chunkRenderMask & MCF_IsTwoSided ) != 0;
			hash = CalculateHash64( chunk.m_streams.Size(), hash );
			hash = red::CalculateHash64( &isTwoSided, sizeof( Uint8 ), hash ); // We want two-sided flags to be included in the hash.
		}

		for( Uint32 chunkIndex = 0; chunkIndex < numChunks; ++chunkIndex )
		{
			const SMeshChunkPacked& chunk = m_meshRO->m_chunks[ chunkIndex ];

			streamTypes.Clear();
			for( const SMeshStream& stream : chunk.m_streams )
				streamTypes.PushBack( stream.m_type );

			std::sort( streamTypes.Begin(), streamTypes.End() ); // We want to be independent of the order of streams.

			for( EMeshStreamType streamType : streamTypes )
			{
				ChunkStreamDataRO streamData = GetChunkStreamData( chunkIndex, streamType );
				Uint32 streamDataSize = streamData.m_buffer.GetSize();

				hash = CalculateHash64( static_cast< Uint32 >( streamType ), hash );
				hash = CalculateHash64( streamDataSize, hash );
				hash = red::CalculateHash64( streamData.m_buffer.Data(), streamDataSize, hash );
			}
		}

		return hash;
#endif // RED_CONFIGURATION_FINAL
	}

	CName DataViewRO::GetBoneName( BoneIndex boneIndex ) const
	{
		if( boneIndex >= m_meshRO->m_boneNames.Size() )
			return CName();

		return m_meshRO->m_boneNames[ boneIndex ];
	}

	Matrix DataViewRO::GetBoneMatrix( BoneIndex boneIndex ) const
	{
		if( boneIndex >= m_meshRO->m_boneRigMatrices.Size() )
			return Matrix::IDENTITY();

		return m_meshRO->m_boneRigMatrices[ boneIndex ];
	}

	Bool DataViewRO::FindBoneIndex( CName boneName, BoneIndex& boneIndex ) const
	{
		boneIndex = m_meshRO->m_boneNames.GetIndex( boneName );
		return boneIndex != (BoneIndex)red::INVALID_INDEX;
	}

	Float DataViewRO::GetBoneVertexEpsilon( BoneIndex boneIndex ) const
	{
		if( boneIndex >= m_meshRO->m_boneVertexEpsilons.Size() )
			return 0.0f;

		return m_meshRO->m_boneVertexEpsilons[ boneIndex ];
	}

	const red::DynArray< CName >& DataViewRO::GetBoneNames() const
	{
		return m_meshRO->m_boneNames;
	}

	const red::DynArray< Matrix >& DataViewRO::GetBoneRigMatrices() const
	{
		return m_meshRO->m_boneRigMatrices;
	}

	const red::DynArray< Float >& DataViewRO::GetBoneVertexEpsilons() const
	{
		return m_meshRO->m_boneVertexEpsilons;
	}

	CName DataViewRO::GetFloatTrackName( FloatTrackIndex trackIndex ) const
	{
		if( trackIndex >= m_meshRO->m_floatTrackNames.Size() )
			return CName::NONE();

		return m_meshRO->m_floatTrackNames[ trackIndex ];
	}

	const red::DynArray< CName >& DataViewRO::GetFloatTrackNames() const
	{
		return m_meshRO->m_floatTrackNames;
	}

	void DataViewRO::CalcChunkSkinningHistogram( ChunkIndex chunkIndex, red::StaticArray< Uint32, 8 + 1 >& skinningHistogram, red::DynArray< Uint32 >* boneUseHistogram ) const
	{
		mesh::ChunkStreamDataRO vertexWeightsStream = GetChunkStreamData( chunkIndex, MST_SkinningWeights_4F );
		if( vertexWeightsStream.Empty() )
			return; // Without skinning weights we can't do anything.

		mesh::ChunkStreamDataRO vertexWeightsExtStream = GetChunkStreamData( chunkIndex, MST_SkinningWeightsExt_4F );

		mesh::ChunkStreamDataRO vertexIndicesStream, vertexIndicesExtStream;
		if( boneUseHistogram != nullptr ) // Let's not load bone indices arrays if they are not needed.
		{
			vertexIndicesStream = GetChunkStreamData( chunkIndex, MST_SkinningIndices_4U8 );
			vertexIndicesExtStream = GetChunkStreamData( chunkIndex, MST_SkinningIndicesExt_4U8 );
		}

		red::StaticArray< Float, 8u > combinedWeights; // At max 8 weights per vertex.
		combinedWeights.Resize( 8u );

		Uint32 numVertices = GetChunkVertexCount( chunkIndex );
		const Uint8* vertexWeightsData = reinterpret_cast< const Uint8* >( vertexWeightsStream.m_data );
		const Uint8* vertexWeightsExtData = reinterpret_cast< const Uint8* >( vertexWeightsExtStream.m_data );
		const Uint8* vertexIndicesData = reinterpret_cast< const Uint8* >( vertexIndicesStream.m_data );
		const Uint8* vertexIndicesExtData = reinterpret_cast< const Uint8* >( vertexIndicesExtStream.m_data );

		for( Uint32 vertexIndex = 0; vertexIndex < numVertices; vertexIndex++ )
		{
			const Float* weights = reinterpret_cast< const Float* >( vertexWeightsData );
			const Float* weightsExt = reinterpret_cast< const Float* >( vertexWeightsExtData );

			for( Uint32 i = 0; i < 4u; ++i ) // Prepare combined array of vertex weights, to simplify code below.
				combinedWeights[ i ] = weights[ i ];
			for( Uint32 i = 0; i < 4u; ++i )
				combinedWeights[ i + 4u ] = ( weightsExt != nullptr ) ? weightsExt[ i ] : 0.0f;

			// Calculate number of used bone weights, update skinning histogram.
			Uint32 numUsedWeights = 0;
			while( ( numUsedWeights < 8u ) && ( combinedWeights[ numUsedWeights ] > 0.0f ) )
			{
				++numUsedWeights;
			}

			while( skinningHistogram.Size() <= numUsedWeights ) // Resize the histogram if needed.
			{
				skinningHistogram.PushBack( 0u );
			}

			skinningHistogram[ numUsedWeights ]++;

			// Update bone usage histogram.
			if( vertexIndicesData != nullptr ) // This will be non nullptr only when boneUseHistogram is also not nullptr.
			{
				const Uint8* boneIndices = vertexIndicesData;
				for( Uint32 i = 0; i < numUsedWeights; ++i )
				{
					if( i == 4u ) // Switch to ext indices array for the next 4 bone indices.
					{
						if( vertexIndicesExtData == nullptr )
							break;
						boneIndices = vertexIndicesExtData - 4u; // We need to offset by -4 so the indexing from 0 to 8 works fine.
					}
					if( combinedWeights[ i ] > 0.0f )
					{
						const Uint32 boneIndex = boneIndices[ i ];
						RED_ASSERT( boneIndex < boneUseHistogram->Size(), "Vertex has bone index outside of range: %u (num all bones: %u). "
																		  "This was found in mesh \"%hs\", chunk %u",
									boneIndex, boneUseHistogram->Size(), m_meshRO->GetPath().ToString().AsChar(), chunkIndex );
						if( boneIndex < boneUseHistogram->Size() ) // We don't want corrupted data to half us here.
							( *boneUseHistogram )[ boneIndex ]++;
					}
				}
			}

			vertexWeightsData += vertexWeightsStream.m_stride;
			vertexWeightsExtData += vertexWeightsExtStream.m_stride; // This will be ok for NULL vertexStreamExt, since stride will be 0 in such case.
			vertexIndicesData += vertexIndicesStream.m_stride;
			vertexIndicesExtData += vertexIndicesExtStream.m_stride;
		}
	}

	CName DataViewRO::GetMaterialName_NoLock( MaterialIndex index ) const
	{
		if( index >= m_meshRO->m_materialEntries.Size() )
			return CName();

		return m_meshRO->m_materialEntries[ index ].GetName();
	}

	CName DataViewRO::GetMaterialName( MaterialIndex index ) const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		return GetMaterialName_NoLock( index );
	}

	THandle< IMaterial > DataViewRO::GetMaterialData( MaterialIndex index ) const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		if( index >= m_meshRO->m_materialEntries.Size() )
			return s_nullMaterialPtr;

		return m_meshRO->m_materialEntries[ index ].GetMaterial();
	}

	THandle< IMaterial > DataViewRO::GetMaterialDataByName( CName materialName ) const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		for( const CMesh::MaterialEntry& materialEntry : m_meshRO->m_materialEntries )
		{
			if( materialEntry.GetName() == materialName )
				return materialEntry.GetMaterial();
		}
		return s_nullMaterialPtr;
	}

	Int32 DataViewRO::GetMaterialIndex( CName materialName ) const
	{
		for( Uint32 i = 0; i < GetNumMaterials(); i++ )
		{
			if( materialName == GetMaterialName( i ) )
				return i;
		}
		return -1;
	}

	Int32 DataViewRO::GetMaterialIndex( const IMaterial* material ) const
	{
		for( Uint32 i = 0; i < GetNumMaterials(); i++ )
		{
			if( material == GetMaterialData( i ).Get() )
				return i;
		}
		return -1;
	}

	Bool DataViewRO::IsMaterialLocalInstance( MaterialIndex index ) const
	{
		if( index >= m_meshRO->m_materialEntries.Size() )
			return false;
		const CMesh::MaterialEntry& materialEntry = m_meshRO->m_materialEntries[ index ];
		return materialEntry.IsLocalInstance();
	}

	Bool DataViewRO::IsMaterialLocalInstance( const IMaterial* material ) const
	{
		// This does not test if the material is present on the meshes list, it rather test resource ownership, type and files.
		// This is so we can used this with undo/redo actions etc.
		Bool materialIsLocalInstance = !material->HasValidPath();
		if( materialIsLocalInstance )
		{
			RED_FATAL_ASSERT( material->IsA< CMaterialInstance >(), "Mesh material has no resource proxy (as instance), "
																	"but it is not of CMaterialInstance class.\r\nThe actual class of this material is: '%hs'",
							  material->GetClass()->GetName().AsChar() );
		}

		return materialIsLocalInstance;
	}

	Bool DataViewRO::IsMaterialTemplate( MaterialIndex index ) const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		if( index >= m_meshRO->m_materialEntries.Size() )
			return false;
		const CMesh::MaterialEntry& materialEntry = m_meshRO->m_materialEntries[ index ];
		Bool isTemplate = materialEntry.GetMaterial()->IsA< CMaterialTemplate >();
		RED_FATAL_ASSERT( isTemplate ? !materialEntry.IsLocalInstance() : true );
		return isTemplate;
	}

	red::DynArray< THandle< IMaterial > > DataViewRO::GetMaterials() const
	{
		return m_meshRO->GetMaterials();
	}

	red::DynArray< CName > DataViewRO::GetMaterialNames() const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		red::DynArray< CName > materialNames{rend::PoolMesh()};
		materialNames.Reserve( m_meshRO->m_materialEntries.Size() );
		for( const CMesh::MaterialEntry& materialEntry : m_meshRO->m_materialEntries )
			materialNames.PushBack( materialEntry.GetName() );
		return materialNames;
	}

	const red::DynArray< TResAsyncRef< IMaterial > >& DataViewRO::GetExternalMaterials() const
	{
		return m_meshRO->m_externalMaterials;
	}

	red::DynArray< THandle< CMaterialInstance > > DataViewRO::GetLoadedLocalMaterialInstances() const
	{
		auto& lock = m_meshRO->m_materialEntriesLock;
		RED_SCOPE_SHARED_LOCK( lock );

		red::DynArray< THandle< CMaterialInstance > > ret{rend::PoolMesh()};
		for( const CMesh::MaterialEntry& materialEntry : m_meshRO->m_materialEntries )
		{
			if( materialEntry.IsLocalInstance() )
			{
				auto material = Cast< CMaterialInstance >( materialEntry.GetMaterialNoFallbackIfNull() );
				if( material )
				{
					ret.PushBack( material );
				}
			}
		}

		return ret;
	}

	CName DataViewRO::GetAppearanceName( Uint32 appearanceIndex ) const
	{
		return m_meshRO->m_appearances[ appearanceIndex ]->GetName();
	}

	Int32 DataViewRO::GetAppearanceIndex( CName appearanceName ) const
	{
		for( Uint32 index = 0; index < m_meshRO->m_appearances.Size(); ++index )
		{
			if( m_meshRO->m_appearances[ index ]->GetName() == appearanceName )
			{
				return static_cast< Int32 >( index );
			}
		}
		if( appearanceName.Empty() || ( appearanceName == MeshAppearance::c_DefaultAppearanceName ) ) // We allow empty string as default for more robustness.
		{
			return 0; // If there is no appearance actually called "default", appearance with index 0 is assumed to be the default one.
		}
		return -1;
	}

	Int32 DataViewRO::GetAppearanceIndex( const String& appearanceName ) const
	{
		return GetAppearanceIndex( RED_NAME_NOREG( appearanceName ) );
	}

	ChunkIndex DataViewRO::GetAppearanceChunkCount( Uint32 appearanceIndex, Int32 lodIndex ) const
	{
		if( appearanceIndex >= m_meshRO->m_appearances.Size() )
			return 0;

		if( lodIndex < 0 )
		{
			return static_cast< ChunkIndex >( m_meshRO->m_appearances[ appearanceIndex ]->GetNumChunks() );
		}

		if( lodIndex >= (Int32)m_meshRO->m_lodLevelInfo.Size() )
			return 0;

		return static_cast< ChunkIndex >( GetLODChunkCount( lodIndex ) );
	}

	Uint8 DataViewRO::GetAppearanceChunkMaterialCount( Uint32 appearanceIndex, ChunkIndex chunkIndex ) const
	{
		if( appearanceIndex >= m_meshRO->m_appearances.Size() )
			return 0;
		if( chunkIndex >= m_meshRO->m_appearances[ appearanceIndex ]->GetNumChunks() )
			return 0;
		return static_cast< Uint8 >( 1 );
	}

	CName DataViewRO::GetAppearanceMaterialName( Uint32 appearanceIndex, ChunkIndex chunkIndex, ChunkMaterialIndex materialIndex ) const
	{
		if( appearanceIndex >= m_meshRO->m_appearances.Size() )
			return CName::NONE();
		if( chunkIndex >= m_meshRO->m_appearances[ appearanceIndex ]->GetNumChunks() )
			return CName::NONE();
		if( materialIndex >= 1 )
			return CName::NONE();
		return m_meshRO->m_appearances[ appearanceIndex ]->GetChunkMaterialName( chunkIndex );
	}

	THandle< IMaterial > DataViewRO::GetAppearanceMaterialData( Uint32 appearanceIndex, ChunkIndex chunkIndex, ChunkMaterialIndex materialIndex ) const
	{
		CName materialName = GetAppearanceMaterialName( appearanceIndex, chunkIndex, materialIndex );
		return GetMaterialDataByName( materialName );
	}

	const THandle< MeshAppearance >& DataViewRO::GetAppearanceData( Uint32 appearanceIndex ) const
	{
		if( appearanceIndex >= m_meshRO->m_appearances.Size() )
			return s_nullAppearancePtr;
		return m_meshRO->m_appearances[ appearanceIndex ];
	}

	const THandle< MeshAppearance >& DataViewRO::GetDefaultAppearanceData() const
	{
		return m_meshRO->GetDefaultAppearance();
	}

	Bool DataViewRO::HasParameter(const rtti::ClassType* type) const
	{
		return m_meshRO->GetParameter( type ) != nullptr;
	}

	THandle< MeshParameter > DataViewRO::GetParameter( const rtti::ClassType* type ) const
	{
		return m_meshRO->GetParameter( type );
	}

    red::ArraySpan<const Uint8> DataViewRO::GetBonesLODMask() const
    {
        return m_meshRO->m_lodBoneMask;
    }

    const Uint32 DataViewRO::PackFlags( const mesh::ChunkFlags& chunkFlags )
	{
		Uint32 packedFlags = 0;
		packedFlags |= ( chunkFlags.m_renderInScene ) ? MCF_RenderInScene : 0;
		packedFlags |= ( chunkFlags.m_renderInShadows ) ? MCF_RenderInShadows : 0;
		packedFlags |= ( chunkFlags.m_isTwoSided ) ? MCF_IsTwoSided : 0;
		packedFlags |= ( chunkFlags.m_isRayTracedEmissive ) ? MCF_IsRayTracedEmissive : 0;

		return packedFlags;
	}

	const mesh::ChunkFlags DataViewRO::UnpackFlags( const Int32& packedFlags )
	{
		mesh::ChunkFlags unpackedFlags;

		unpackedFlags.m_renderInScene = ( ( packedFlags & MCF_RenderInScene ) != 0 );
		unpackedFlags.m_renderInShadows = ( ( packedFlags & MCF_RenderInShadows ) != 0 );
		unpackedFlags.m_isTwoSided = ( ( packedFlags & MCF_IsTwoSided ) != 0 );
		unpackedFlags.m_isRayTracedEmissive = ( ( packedFlags & MCF_IsRayTracedEmissive ) != 0 );

		return unpackedFlags;
	}

	DataViewRO MakeView( CMesh* mesh )
	{
		return DataViewRO( *mesh );
	}

	DataViewRO MakeView( const CMesh& mesh )
	{
		return DataViewRO( mesh );
	}

	DataViewRO MakeView( CMesh& mesh )
	{
		return DataViewRO( mesh );
	}

	DataViewRO MakeView( const THandle< CMesh >& mesh )
	{
		return DataViewRO( *mesh );
	}

	DataViewRO MakeView( const TResRef< CMesh >& mesh )
	{
		return DataViewRO( *mesh.Get() );
	}

} // mesh
