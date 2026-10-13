/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderResourceBlob.h"
#include "renderingMemory.h"
#include "morphTargetCommon.h"
#include "../../redReflection/include/deferredDataBuffer.h"
#include "../../redContainers/include/fixedArray.h"

namespace rend
{
	struct RenderMorphTargetMeshBlobTextureData
	{
		RTTI_DECLARE_TYPE( RenderMorphTargetMeshBlobTextureData );
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		red::StaticArray< Vector4,	MorphTargets::BTT_MAX >			m_targetDiffScale;
		red::StaticArray< Vector4,	MorphTargets::BTT_MAX >			m_targetDiffOffset;
		
		red::StaticArray< Uint32,	MorphTargets::BTT_MAX >			m_targetDiffsDataOffset;
		red::StaticArray< Uint32,	MorphTargets::BTT_MAX >			m_targetDiffsDataSize;
		
		red::StaticArray< Uint16,	MorphTargets::BTT_MAX >			m_targetDiffsWidth;

		red::StaticArray< Uint8,	MorphTargets::BTT_MAX >			m_targetDiffsMipLevelCounts;

		RenderMorphTargetMeshBlobTextureData()
			: m_targetDiffScale( MorphTargets::BTT_MAX )
			, m_targetDiffOffset( MorphTargets::BTT_MAX )
			, m_targetDiffsDataOffset( MorphTargets::BTT_MAX )
			, m_targetDiffsDataSize( MorphTargets::BTT_MAX )
			, m_targetDiffsWidth( MorphTargets::BTT_MAX )
			, m_targetDiffsMipLevelCounts( MorphTargets::BTT_MAX )
		{}
	};

	struct RenderMorphTargetMeshBlobHeader
	{
		RTTI_DECLARE_TYPE( RenderMorphTargetMeshBlobHeader );
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		Uint32											m_version					{ 0 };						// version of the format
		Uint32											m_numDiffs					{ 0 };
		Uint32											m_numDiffsMapping			{ 0 };
		Uint32											m_numTargets				{ 0 };

		red::DynArray< Uint32 >							m_targetStartsInVertexDiffs;
		red::DynArray< Uint32 >							m_targetStartsInVertexDiffsMapping;
		red::DynArray< Vector4 >						m_targetPositionDiffScale;
		red::DynArray< Vector4 >						m_targetPositionDiffOffset;
		red::DynArray< red::DynArray< Uint32 > >		m_numVertexDiffsInEachChunk;
		red::DynArray< red::DynArray< Uint32 > >		m_numVertexDiffsMappingInEachChunk;

		red::DynArray< RenderMorphTargetMeshBlobTextureData > m_targetTextureDiffsData;

		RenderMorphTargetMeshBlobHeader()
			: m_targetStartsInVertexDiffs( rend::PoolMesh() )
			, m_targetStartsInVertexDiffsMapping( rend::PoolMesh() )
			, m_targetPositionDiffScale( rend::PoolMesh() )
			, m_targetPositionDiffOffset( rend::PoolMesh() )
			, m_numVertexDiffsInEachChunk( rend::PoolMesh() )
			, m_numVertexDiffsMappingInEachChunk( rend::PoolMesh() )
			, m_targetTextureDiffsData( rend::PoolMesh() )
		{
		}
	};

	class RENDER_DATA_API RenderMorphTargetMeshBlob : public IRenderResourceBlob
	{
		RTTI_DECLARE_TYPE( RenderMorphTargetMeshBlob );
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		struct RenderMorphTargetMeshBlobVersions
		{
			enum : Uint32
			{
				VER0_BASE,
				VER1_NORMALMAP_SIZE,
				VER2_ALBEDO_AND_ROUGHNESS_MORPHING_ADDED,
				VER3_MIPS_ADDED,
				VER4_COMPRESSED_TEXTURES,
				VER5_NORMAL_BLEND_ONLY,
				VER6_COMPRESSED_DATA,
				VER_MAX,
			};
		};

	public:
		typedef RenderMorphTargetMeshBlobHeader Header;

	public:
		RenderMorphTargetMeshBlob();
		~RenderMorphTargetMeshBlob();

		static const Uint32 FILE_VERSION;

		virtual void OnSerialize( class IFile& file ) override;

		RED_INLINE virtual ERenderResourceType GetType() const override { return RRT_MorphTargetMesh; }

		virtual Uint32 GetVersion() const override;

		virtual Bool IsVersionCurrent() const override;
		virtual Bool IsVersionValid() const override;

		virtual Uint32 GetDataSize() const override;

	private:
		void UpgradeVersion();

	public:
		THandle< IRenderResourceBlob >		m_baseBlob;
		Header								m_header;
		DataBuffer							m_diffsBuffer;				// vertex diffs
		DataBuffer							m_mappingBuffer;			// vertex diffs mapping
		serialization::DeferredDataBuffer	m_textureDiffsBuffer;		// all texture diffs packed together
	};
} //rend
