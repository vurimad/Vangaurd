/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "renderMorphTargetMeshBlob.h"
#include "../../redMemory/include/poolRoot.h"
#include "../../redReflection/include/variant.h"

//////////////////////////////////////////////////////////////////////////

const Uint32 rend::RenderMorphTargetMeshBlob::FILE_VERSION = RenderMorphTargetMeshBlobVersions::VER_MAX - 1;

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMorphTargetMeshBlobTextureData, rend );
	RTTI_PROPERTY( m_targetDiffScale );
	RTTI_PROPERTY( m_targetDiffOffset );
	RTTI_PROPERTY( m_targetDiffsDataOffset );
	RTTI_PROPERTY( m_targetDiffsDataSize );
	RTTI_PROPERTY( m_targetDiffsWidth );
	RTTI_PROPERTY( m_targetDiffsMipLevelCounts );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMorphTargetMeshBlobHeader, rend );
	RTTI_PROPERTY( m_version );
	RTTI_PROPERTY( m_numDiffs );
	RTTI_PROPERTY( m_numDiffsMapping );
	RTTI_PROPERTY( m_numTargets );
	RTTI_PROPERTY( m_targetStartsInVertexDiffs );
	RTTI_PROPERTY( m_targetStartsInVertexDiffsMapping );
	RTTI_PROPERTY( m_targetPositionDiffScale );
	RTTI_PROPERTY( m_targetPositionDiffOffset );
	RTTI_PROPERTY( m_numVertexDiffsInEachChunk );
	RTTI_PROPERTY( m_numVertexDiffsMappingInEachChunk );
	RTTI_PROPERTY( m_targetTextureDiffsData );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMorphTargetMeshBlob, rend );
	RTTI_PARENT_TYPE( IRenderResourceBlob );
	RTTI_PROPERTY( m_header );
	RTTI_PROPERTY( m_diffsBuffer );
	RTTI_PROPERTY( m_mappingBuffer );
	RTTI_PROPERTY( m_textureDiffsBuffer );
	RTTI_PROPERTY( m_baseBlob );
RTTI_END_TYPE();	


rend::RenderMorphTargetMeshBlob::RenderMorphTargetMeshBlob()
	: m_diffsBuffer( rend::PoolRenderBlobs() )
	, m_mappingBuffer( rend::PoolRenderBlobs() )
	, m_textureDiffsBuffer( rend::PoolRenderBlobs(), 16, serialization::DDBF_AutoLoadAll )
	, m_baseBlob( nullptr )
{
}

rend::RenderMorphTargetMeshBlob::~RenderMorphTargetMeshBlob()
{
}

Uint32 rend::RenderMorphTargetMeshBlob::GetVersion() const
{
	return m_header.m_version;
}

Bool rend::RenderMorphTargetMeshBlob::IsVersionCurrent() const
{
	return m_header.m_version == FILE_VERSION;
}

Bool rend::RenderMorphTargetMeshBlob::IsVersionValid() const
{
	return m_header.m_version == FILE_VERSION;
}

Uint32 rend::RenderMorphTargetMeshBlob::GetDataSize() const
{
	return m_diffsBuffer.Size() + m_mappingBuffer.Size() + m_textureDiffsBuffer.GetSize();
}

void rend::RenderMorphTargetMeshBlob::OnSerialize( class IFile& file )
{
	IRenderResourceBlob::OnSerialize( file );

	// HACK : This should probably actually be done in OnPostLoad, but that can result in the CMesh getting OnPostLoad
	// before us, and so it tries to use the blob before we have a chance to do any updates.
	if ( file.IsReader() )
	{
		if ( m_header.m_version != FILE_VERSION )
		{
			UpgradeVersion();
		}
	}
}

void rend::RenderMorphTargetMeshBlob::UpgradeVersion()
{
	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER1_NORMALMAP_SIZE )
	{
		// do something with the data
		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER1_NORMALMAP_SIZE;
	}
	
	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER2_ALBEDO_AND_ROUGHNESS_MORPHING_ADDED )
	{ 
		m_header.m_targetTextureDiffsData.Resize( m_header.m_numTargets );
		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER2_ALBEDO_AND_ROUGHNESS_MORPHING_ADDED;
	}

	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER3_MIPS_ADDED )
	{
		for ( auto& diffsData : m_header.m_targetTextureDiffsData )
		{
			for ( auto& it : diffsData.m_targetDiffsMipLevelCounts )
			{
				it = 1;
			}
		}
		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER3_MIPS_ADDED;
	}
	
	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER4_COMPRESSED_TEXTURES )
	{
		for ( auto& diffsData : m_header.m_targetTextureDiffsData )
		{
			for ( Uint32 i = 0; i < (Uint32)MorphTargets::BTT_MAX; ++i )
			{
				diffsData.m_targetDiffScale[ i ]			= Vector4::ZEROS();
				diffsData.m_targetDiffOffset[ i ]			= Vector4::ZEROS();
				diffsData.m_targetDiffsDataOffset[ i ]		= 0;
				diffsData.m_targetDiffsDataSize[ i ]		= 0;
				diffsData.m_targetDiffsWidth[ i ]			= 0;
				diffsData.m_targetDiffsMipLevelCounts[ i ]	= 0;
			}
		}
		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER4_COMPRESSED_TEXTURES;
	}

	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER5_NORMAL_BLEND_ONLY )
	{
		// only keep normalmap data, cleanup the rest
		for ( auto& diffsData : m_header.m_targetTextureDiffsData )
		{
			for ( Uint32 i = 0; i < (Uint32)MorphTargets::BTT_MAX; ++i )
			{
				if ( !MorphTargets::ShouldProcess( (MorphTargets::BlendedTextureType)i ) )
				{
					diffsData.m_targetDiffScale[ i ]			= Vector4::ZEROS();
					diffsData.m_targetDiffOffset[ i ]			= Vector4::ZEROS();
					diffsData.m_targetDiffsDataOffset[ i ]		= 0;
					diffsData.m_targetDiffsDataSize[ i ]		= 0;
					diffsData.m_targetDiffsWidth[ i ]			= 0;
					diffsData.m_targetDiffsMipLevelCounts[ i ]	= 0;
				}
			}
		}
		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER5_NORMAL_BLEND_ONLY;
	}

	if ( m_header.m_version < RenderMorphTargetMeshBlobVersions::VER6_COMPRESSED_DATA )
	{
		red::DynArray< Uint32 > newMappings { rend::PoolMesh() };

		const Uint32 numTargets = m_header.m_targetStartsInVertexDiffs.Size();
		const Uint32 newMappingsCount = m_header.m_numDiffsMapping;
		newMappings.Reserve( newMappingsCount + numTargets );
		const Uint32* oldMappings = (Uint32*)m_mappingBuffer.Data();
		const Uint32 oldMappingsSize = m_mappingBuffer.Size() / sizeof( Uint32 );

		Uint32 currentMappingOffset = 0;		
		m_header.m_targetStartsInVertexDiffsMapping.Resize( numTargets );
		m_header.m_numVertexDiffsMappingInEachChunk.Resize( numTargets, { rend::PoolMesh() } );
		
		for ( Uint32 target_i = 0; target_i < numTargets; ++target_i )
		{
			const Uint32 numChunks = m_header.m_numVertexDiffsInEachChunk[ target_i ].Size();
			m_header.m_numVertexDiffsMappingInEachChunk[ target_i ].Resize( numChunks );
			for ( Uint32 chunk_i = 0; chunk_i < numChunks; ++chunk_i )
			{
				m_header.m_numVertexDiffsMappingInEachChunk[ target_i ][ chunk_i ] = red::DivideRoundingUp( m_header.m_numVertexDiffsInEachChunk[ target_i ][ chunk_i ], 2 );
			}
			
			m_header.m_targetStartsInVertexDiffsMapping[ target_i ] = newMappings.Size();

			Uint32 currentMappingIndex = m_header.m_targetStartsInVertexDiffs[ target_i ];

			for ( Uint32 chunk_i = 0; chunk_i < numChunks; ++chunk_i )
			{
				if ( m_header.m_targetStartsInVertexDiffs[ target_i ] + m_header.m_numVertexDiffsInEachChunk[ target_i ][ chunk_i ] > m_header.m_numDiffsMapping )
				{
					// get rid of invalid entries that point past the buffer size
					m_header.m_numVertexDiffsMappingInEachChunk[ target_i ][ chunk_i ] = 0;
					continue;
				}

				const Uint32 numDiffsInCurrentChunk = m_header.m_numVertexDiffsInEachChunk[ target_i ][ chunk_i ];
				for ( Uint32 i = 0; i < numDiffsInCurrentChunk; i += 2 )
				{
					Uint32 currentMappingIncrease = 1;
					Uint32 oldMapping0 = oldMappings[ currentMappingIndex ];
					Uint32 oldMapping1 = 0;
					if ( i + 1 < numDiffsInCurrentChunk && currentMappingIndex + 1 < oldMappingsSize )
					{
						++currentMappingIncrease;
						oldMapping1 = oldMappings[ currentMappingIndex + 1 ];
					}
				
					RED_ASSERT( oldMapping0 < std::numeric_limits< Uint16 >::max() );
					RED_ASSERT( oldMapping1 < std::numeric_limits< Uint16 >::max() );
				
					Uint32 newMapping = oldMapping1;
					newMapping <<= 16;
					newMapping |= (Uint16)oldMapping0;
					newMappings.PushBack( newMapping );

					currentMappingIndex += currentMappingIncrease;
				}
			}
		}

		m_header.m_numDiffsMapping = newMappings.Size();
		m_mappingBuffer = DataBuffer::Copy( newMappings.TypedData(), newMappings.DataSize() );

		m_header.m_version = RenderMorphTargetMeshBlobVersions::VER6_COMPRESSED_DATA;
	}

	// TODO : Handle further incremental upgrades here
	
	static_assert( rend::RenderMorphTargetMeshBlob::FILE_VERSION == RenderMorphTargetMeshBlobVersions::VER6_COMPRESSED_DATA, "MorphTarget file version was bumped, handle the upgrade code!" );
}
