/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamDestructionStepsData.h"
#include "../../redReflection/include/variant.h"
#include "../../physics/include/physicsDestructionStructs.h"
#include "../../resourceMesh/include/mesh.h"
#include "../../resourceMesh/include/meshDataViewRO.h"
#include "../../redReflection/include/dataBuffer.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamDestructionStepData, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_offsets ).readOnly();
RTTI_PROPERTY( m_isInstantRemovable ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( DestructionBond, mesh );
RTTI_PROPERTY( m_bondIndex ).readOnly();
RTTI_PROPERTY( m_bondHealth ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkIndicesOffset, mesh );
RTTI_PROPERTY( m_start ).readOnly();
RTTI_PROPERTY( m_count ).readOnly();
RTTI_PROPERTY( m_boneIndex ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamDestructionBonds, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_bonds ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamDestructionBoneChunkMapping, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_boneChunkMasks ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamDestructionChunkIndicesOffsets, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_offsets ).readOnly();
RTTI_PROPERTY( m_chunkOffsets ).readOnly();
RTTI_PROPERTY( m_indices ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ChunkOffset, mesh );
RTTI_PROPERTY( m_chunkIndex ).readOnly();
RTTI_PROPERTY( m_start ).readOnly();
RTTI_PROPERTY( m_count ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RegionData, mesh );
RTTI_PROPERTY( m_chunkDataIntact ).readOnly();
RTTI_PROPERTY( m_chunkDataFractured ).readOnly();
RTTI_PROPERTY( m_chunkMaskIntact ).readOnly();
RTTI_PROPERTY( m_chunkMaskFractured ).readOnly(); 
RTTI_PROPERTY( m_isStaticRemains ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamBakedDestructionData, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_regionData ).readOnly();
RTTI_PROPERTY( m_indices ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamDeformableShapesData, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter );
RTTI_PROPERTY( m_ownerIndex ).editable();
RTTI_PROPERTY( m_startingPose ).readOnly();
RTTI_PROPERTY( m_finalPose ).readOnly();
RTTI_END_TYPE();

#ifdef PHYSICS_DEBUG
#pragma optimize("", off)
#endif

namespace mesh
{

	MeshParamDestructionStepData::MeshParamDestructionStepData()
	{}

	void MeshParamDestructionStepData::OnMeshChanged( const CMesh& mesh )
	{}

	Bool MeshParamDestructionStepData::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
	{
		if( propertyName == RED_NAME_CONSTEXPR_NOREG( "offsets" ) )
		{
			red::DynArray<Uint32> offsets = {red::PoolBackend()};
			readValue.Get( offsets );
			m_offsets.Reserve( offsets.Size() );
			for( Uint32 combined : offsets )
			{
				m_offsets.PushBack( physics::DestructionHierarchyOffset( combined ) );
			}
			return true;
		}

		return MeshParameter::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
	}

	MeshParamDestructionBonds::MeshParamDestructionBonds()
	{}

	void MeshParamDestructionBonds::SetDestructionBonds( const DestructionBonds& bonds )
	{
		m_bonds = bonds;
	}

	const mesh::DestructionBonds& MeshParamDestructionBonds::GetDestructionBonds()
	{
		return m_bonds;
	}

	void MeshParamDestructionBonds::OnMeshChanged( const CMesh& mesh )
	{

	}

	MeshParamDestructionBoneChunkMapping::MeshParamDestructionBoneChunkMapping()
	{
		
	}

	const mesh::DestructionBoneChunkMapping& MeshParamDestructionBoneChunkMapping::GetDestructionBoneChunkMapping()
	{
		return m_boneChunkMasks;
	}

	void MeshParamDestructionBoneChunkMapping::OnMeshChanged( const CMesh& mesh )
	{
		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( mesh );
		Uint32 numChunks = meshDataViewRO.GetNumChunks();
		Uint32 numBones = meshDataViewRO.GetNumBones();
		m_boneChunkMasks.Clear();
		m_boneChunkMasks.Resize( numBones );
		for( Uint32 i = 0; i < numChunks; ++i )
		{
			const mesh::StreamViewRO< Color > dataBoneIndices( meshDataViewRO.GetChunkStreamData( i, MST_SkinningIndices_4U8 ) );
			const mesh::StreamViewRO< Vector4 > dataBoneWeights( meshDataViewRO.GetChunkStreamData( i, MST_SkinningWeights_4F ) );
			if( dataBoneWeights.IsValid() && dataBoneIndices.IsValid() )
			{
				for( Uint32 j = 0; j < dataBoneIndices.Size(); ++j )
				{
					auto indices = dataBoneIndices[ j ];
					auto weights = dataBoneWeights[ j ];

					if( weights[ 0 ] > 0.f && numBones > indices.RGBA[ 0 ] )
					{
						m_boneChunkMasks[ indices.RGBA[ 0 ] ] |= 1ull << i;
					}
				}
			}
		}
	}

	Bool MeshParamDestructionBoneChunkMapping::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
	{
		if( propertyName == RED_NAME_CONSTEXPR_NOREG( "boneToChunk" ) )
		{
			red::DynArray<Uint8> boneToChunk = { red::PoolBackend() };
			readValue.Get( boneToChunk );
			m_boneChunkMasks.Reserve( boneToChunk.Size() );
			for( Uint64 chunkIndex : boneToChunk )
			{
				m_boneChunkMasks.PushBack( 1ull << chunkIndex );
			}
			return true;
		}

		return MeshParameter::OnPropertyMissing( propertyName, readValue );
	}

	MeshParamDestructionChunkIndicesOffsets::MeshParamDestructionChunkIndicesOffsets()
	{}

	const red::ArraySpan<mesh::ChunkIndicesOffset > MeshParamDestructionChunkIndicesOffsets::GetChunkIndicesOffset( Uint32 chunkIndex )
	{
		Uint32 chunkOffsetsSize = m_chunkOffsets.Size();
		if( chunkIndex > chunkOffsetsSize )
			return red::ArraySpan<mesh::ChunkIndicesOffset>();
		Uint32 startIndex = m_chunkOffsets[ chunkIndex ];
		Uint32 endIndex = ( chunkIndex + 1 ) < chunkOffsetsSize ? m_chunkOffsets[ chunkIndex + 1 ] : m_offsets.Size();
		return red::MakeArraySpan( m_offsets.TypedData() + startIndex, endIndex - startIndex );
	}

	template<typename T>
	red::ArraySpan<T> MakeSpan( red::BlobSpan span )
	{
		if( !span.Empty() )
		{
			return red::MakeArraySpan( span.Pointer<T>(), (T*)( span.Pointer<Uint8>() + span.Size() ) );
		}

		return {};
	}

	red::ArraySpan< Uint16> MeshParamDestructionChunkIndicesOffsets::GetIndices( Uint16 chunk )
	{
		if( m_indices.Size() > chunk )
			return MakeSpan<Uint16>( m_indices[ chunk ].GetSpan() );
		return {};
	}

	void MeshParamDestructionChunkIndicesOffsets::OnMeshChanged( const CMesh& mesh )
	{
		Clear();
		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( mesh );
		Uint32 chunkCount = meshDataViewRO.GetNumChunks();
		Uint8 currentBoneIndex = 0;
		for( Uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
		{
			mesh::DestructionChunkIndicesOffsets newOffsets = { red::PoolBackend() };
			mesh::ChunkStreamDataRO skinningIndicesStream = meshDataViewRO.GetChunkStreamData( chunkIndex, EMeshStreamType::MST_SkinningIndices_4U8 );
			Uint8* skinningIndices = (Uint8*)skinningIndicesStream.m_data;
			Uint32 skinningIndicesCount = skinningIndicesStream.m_count * 4;
			mesh::ChunkStreamDataRO indicesStream = meshDataViewRO.GetChunkStreamData( chunkIndex, EMeshStreamType::MST_Index_U16 );
			Uint16* indices = (Uint16 *)indicesStream.m_data;
			Uint32 indicesCount = indicesStream.m_count;
			Uint32 start = 0;
			if ( indices != nullptr )
			{
				currentBoneIndex = skinningIndices[4 * indices[0]];
				for ( Uint32 i = 0; i < indicesCount; ++i )
				{
					if ( skinningIndices[4 * indices[i]] != currentBoneIndex )
					{
						newOffsets.PushBack( { start, i - start, currentBoneIndex } );
						start = i;
						currentBoneIndex = skinningIndices[4 * indices[i]];
					}
				}
			}
			newOffsets.PushBack( { start, indicesCount - start, currentBoneIndex } );
			m_chunkOffsets.PushBack( m_offsets.Size() );
			m_offsets.PushBack( newOffsets );
			m_indices.PushBack( DataBuffer::Copy( indices, indicesCount * sizeof( Uint16), rend::PoolMesh() ) );
		}
	}

	void MeshParamDestructionChunkIndicesOffsets::Clear()
	{
		m_offsets.Clear();
		m_chunkOffsets.Clear();
		m_indices.Clear();
	}

	MeshParamBakedDestructionData::MeshParamBakedDestructionData()
	{}

	void MeshParamBakedDestructionData::AppendRegionData( Uint32 regionIndeks, Uint32 chunkIndex, Uint16 start, Uint16 count, Uint16 dataType )
	{
		if( regionIndeks >= m_regionData.Size() )
		{
			m_regionData.Resize( regionIndeks + 1 ); 
		}
		if( dataType > 0 )
		{
			if( count ) 
			{
				m_regionData[ regionIndeks ].m_chunkDataFractured.PushBack( { chunkIndex, start, count } );
			}
			m_regionData[ regionIndeks ].m_chunkMaskFractured |= 1ull << chunkIndex;
			m_regionData[ regionIndeks ].m_isStaticRemains = (RegionDataType)dataType == RegionDataType::Remains;
		}
		else
		{
			if( count )
			{
				m_regionData[ regionIndeks ].m_chunkDataIntact.PushBack( { chunkIndex, start, count } );
			}
			m_regionData[ regionIndeks ].m_chunkMaskIntact |= 1ull << chunkIndex;
		}
	}

	void MeshParamBakedDestructionData::Clear()
	{
		m_regionData.Clear();
		m_indices.Clear();
	}

	void MeshParamBakedDestructionData::OnMeshChanged( const CMesh& mesh )
	{
	}

	red::ArraySpan< Uint16> MeshParamBakedDestructionData::GetIndices( Uint16 chunk )
	{
		if( m_indices.Size() > chunk )
			return MakeSpan<Uint16>( m_indices[ chunk ].GetSpan() );
		return {};
	}

	MeshParamDeformableShapesData::MeshParamDeformableShapesData()
	{

	}

	void MeshParamDeformableShapesData::OnMeshChanged( const CMesh& mesh )
	{
	}

} // mesh

#ifdef PHYSICS_DEBUG
#pragma optimize("", on)
#endif