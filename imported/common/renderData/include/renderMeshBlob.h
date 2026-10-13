/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderResourceBlob.h"
#include "gpuWrapApiVertexFormats.h"
#include "renderingMemory.h"
#include "renderPublicEnums.h"
#include "bufferChunkType.h"
#include "meshEnum.h"

static constexpr Uint32 RENDER_MESH_NUM_STREAMS		 = ERenderMeshStreams::RMS_NUM_STREAMS;

namespace rend
{
	struct VertexBufferChunk_DEPRECATED
	{
		VertexBufferChunk_DEPRECATED()
			: type_DEPRECATED( GpuWrapApi::BCT_Max )
		{
			red::Memzero( m_byteOffsets, sizeof( m_byteOffsets ) );
		}
		VertexBufferChunk_DEPRECATED( const VertexBufferChunk_DEPRECATED& other )
			: type_DEPRECATED( other.type_DEPRECATED )
		{
			m_vertexLayout = other.m_vertexLayout;
			red::Memcpy( m_byteOffsets, other.m_byteOffsets, sizeof( m_byteOffsets ) );
		}

		VertexBufferChunk_DEPRECATED& operator =( const VertexBufferChunk_DEPRECATED& other )
		{
			if ( this != &other )
			{
				type_DEPRECATED = other.type_DEPRECATED;
				m_vertexLayout = other.m_vertexLayout;
				red::Memcpy( m_byteOffsets, other.m_byteOffsets, sizeof( m_byteOffsets ) );
			}

			return *this;
		}

		GpuWrapApi::eBufferChunkType	type_DEPRECATED;	// #TODO: remove after global render mesh blobs resave
		GpuWrapApi::VertexLayoutDesc	m_vertexLayout;
		Uint32							m_byteOffsets[ RENDER_MESH_NUM_STREAMS ];		//!< Offset into each vertex buffer where this chunk starts.
	};

	struct IndexBufferChunk_DEPRECATED
	{
		IndexBufferChunk_DEPRECATED()
			: type_DEPRECATED( GpuWrapApi::BCT_Max )
			, type( GpuWrapApi::IBCT_Max )
			, byteOffset( 0 )
		{
		}

		GpuWrapApi::eBufferChunkType		type_DEPRECATED;	// #TODO: remove after global render mesh blobs resave
		GpuWrapApi::eIndexBufferChunkType	type;
		Uint32								byteOffset;
	};

	struct VertexBufferChunk
	{
		RTTI_DECLARE_TYPE( VertexBufferChunk );

		VertexBufferChunk()
		{
		}
		VertexBufferChunk( const VertexBufferChunk& other )
		{
			m_vertexLayout = other.m_vertexLayout;
			m_byteOffsets = other.m_byteOffsets;
		}

		VertexBufferChunk& operator =( const VertexBufferChunk& other )
		{
			if ( this != &other )
			{
				m_vertexLayout	= other.m_vertexLayout;
				m_byteOffsets = other.m_byteOffsets;
			}


			return *this;
		}

		GpuWrapApi::VertexLayoutDesc m_vertexLayout;
		red::StaticArray< Uint32, RENDER_MESH_NUM_STREAMS >	m_byteOffsets;		//!< Offset into each vertex buffer where this chunk starts.
	};

	struct IndexBufferChunk
	{
		RTTI_DECLARE_TYPE( IndexBufferChunk );

		IndexBufferChunk()
			: type( GpuWrapApi::IBCT_Max )
			, byteOffset( 0 )
		{
		}

		GpuWrapApi::eIndexBufferChunkType	type;
		Uint32								byteOffset;
	};

	//! Rendering mesh chunk
	struct RENDER_DATA_API Chunk_DEPRECATED
	{
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		VertexBufferChunk_DEPRECATED	m_chunkVertices;				//!< Buffer data with vertices
		IndexBufferChunk_DEPRECATED 	m_chunkIndices;					//!< Buffer data with indices
		Uint16					m_numVertices;					//!< Number of vertices in this chunk
		Uint32					m_numIndices;					//!< Number of indices in this chunk
		red::DynArray< CName >	m_materialId{rend::PoolMesh()}; //!< Material index
		Uint8					m_vertexFactory;				//!< Base vertex factory to use for this chunk
		Uint8					m_baseRenderMask;				//!< Render mask of this chunk - telling us where should we render this chunk (scene, cascades, etc)
		Uint8					m_mergedRenderMask;				//!< In case this is part of a merged content here we have the merged render mask
		Uint8					m_lodMask;						//!< Chunk LOD mask - in which LODs do we exist

		// Serialize buffer type
		static void SerializeVertexBufferChunk( IFile& file, VertexBufferChunk_DEPRECATED& b );
		static void SerializeIndexBufferChunk( IFile& file, IndexBufferChunk_DEPRECATED& b );
		static void SerializeMaterialVertexFactory( IFile& file, Uint8& vertexFactory );
	};
	RENDER_DATA_API void operator<<( IFile& file, Chunk_DEPRECATED& chunk );

	struct RENDER_DATA_API Chunk
	{
		RED_USE_MEMORY_POOL( rend::PoolMesh );
		RTTI_DECLARE_TYPE( Chunk );

		VertexBufferChunk		m_chunkVertices;				//!< Buffer data with vertices
		IndexBufferChunk		m_chunkIndices;					//!< Buffer data with indices
		red::DynArray< CName >	m_materialId{ rend::PoolMesh() }; //!< Material index
		Uint32					m_numIndices;					//!< Number of indices in this chunk
		Uint16					m_numVertices;					//!< Number of vertices in this chunk
		Uint16					m_baseRenderMask;//	DEPRECATED --- !< Render mask of this chunk !< --- DEPRECATED
		Uint16					m_mergedRenderMask;//	DEPRECATED --- /!< Render mask of this chunk !< --- DEPRECATED
		EMeshChunkFlags			m_renderMask;					//!< Render mask of this chunk
		Uint8					m_vertexFactory;				//!< Base vertex factory to use for this chunk
		Uint8					m_lodMask;						//!< Chunk LOD mask - in which LODs do we exist
	};

	struct RENDER_DATA_API TopologyData
	{
		RED_USE_MEMORY_POOL( rend::PoolMesh );
		RTTI_DECLARE_TYPE( TopologyData );

		Uint32					m_dataStride;							// Topology data element stride
		Uint32					m_metadataStride;						// Topology metadata element stride
		red::DynArray< Uint8 >	m_data { rend::PoolRenderBlobs() };		// Cooked topology data
		red::DynArray< Uint8 >	m_metadata { rend::PoolRenderBlobs() };	// Cooked topology metadata
	};

	///! Rendering mesh LOD
	struct LODLevel
	{
		Float			m_distance;
	};

	struct RenderMeshBlobHeader
	{
		RTTI_DECLARE_TYPE( RenderMeshBlobHeader );

		Uint32									m_version = 0;						// version of the format
		Uint32									m_dataProcessing = 0;				// Data processed on the mesh
		red::DynArray< Vector4 >				m_bonePositions{ rend::PoolRenderBlobs() };		// Model-space positions of each bone
		red::DynArray< Float >					m_renderLODs{ rend::PoolRenderBlobs() };		// Cooked LOD distances
		red::DynArray< Uint8 >					m_renderChunks{ rend::PoolRenderBlobs() };		// Cooked render chunk data (DEPRECATED, present for compatibility)
		red::DynArray< Chunk >					m_renderChunkInfos{ rend::PoolRenderBlobs() };	// Cooked render chunk data (with offsets into streams)
		red::DynArray< Uint8 >					m_speedTreeWind{ rend::PoolRenderBlobs() };		// Cooked wind params
		red::DynArray< Uint8 >					m_customData{ rend::PoolRenderBlobs() };		// Cooked data (e.g.: destruction poses)
		red::DynArray< Uint8 >					m_topologyData{ rend::PoolRenderBlobs() };		// Cooked topology data
		red::DynArray< Uint8 >					m_topologyMetadata{ rend::PoolRenderBlobs() };	// Cooked topology metadata
		red::DynArray< TopologyData >			m_topology { rend::PoolRenderBlobs() };			// Cooked topology data per chunk
		Uint32									m_customDataElemStride = 0;		// Custom data element stride
		Uint32									m_topologyDataStride = 0;		// Topology data element stride
		Uint32									m_topologyMetadataStride = 0;	// Topology metadata element stride
		Uint32									m_vertexBufferSize = 0;			// Size of vertex data
		Uint32									m_indexBufferSize = 0;			// Size of index data
		Uint32									m_indexBufferOffset = 0;		// Offset of index data in the render data (vertex data has implicit offset of 0)
		Vector4									m_quantizationScale{};			// Quantization scale for vertices
		Vector4									m_quantizationOffset{};			// Quantization offsets for vertices
	};
	
	class RENDER_DATA_API IMeshBlobIndexProcess
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderBlobs );

	public:
		virtual ~IMeshBlobIndexProcess() = default;
		virtual void ProcessIndices( red::ArraySpan<Uint16> indices, Uint32 numVertices ) = 0;
		virtual void ProcessIndices( red::ArraySpan<Uint32> indices, Uint32 numVertices ) = 0;
	};

	class RENDER_DATA_API RenderMeshBlob : public IRenderResourceBlob
	{
		RTTI_DECLARE_TYPE( RenderMeshBlob );
		RED_USE_MEMORY_POOL( rend::PoolRenderBlobs );

		enum class RenderMeshBlobVersions : Uint32
		{
			VER0,
			VER1,
			VER2,
			VER3,
			VER4_8BoneSkinning,
			VER5_LocalShadows,
			VER6_Garment,
			VER7_InstancedOnly,
			VER8_BufferChunkTypeReplacedWithVertexLayout,
			VER9_AddedDrawBufferVertexFactory,
			VER10_SNormPositions,
			VER11_RTTIChunks,
			VER12_MeshTopologyPerChunk,
			VER13_TopologyDataMismatch,
			VER14_RenderInCascadesFlagAdded,
			VER15_VertecCacheOptimization,
			VER16_VertexFactoriesCleanup,
			VER17_IndexBuffersAlignedTo4,
			VER18_MeshChunkFlagsCleanup,

			VER_MAX
		};

		enum class RenderMeshBlobProcess : Uint16
		{
			VertexCacheOptimization = RED_FLAG(0)
		};

	public:
		typedef RenderMeshBlobHeader Header;

	public:
		RenderMeshBlob();
		~RenderMeshBlob();

		static const Uint32 FILE_VERSION;

		virtual void OnSerialize( class IFile& file ) override;
		virtual void OnPreSave( const PreSaveContext& context ) override;

		RED_INLINE virtual ERenderResourceType GetType() const override { return RRT_Mesh; }

		virtual Uint32 GetVersion() const override;

		virtual Bool IsVersionCurrent() const override;
		virtual Bool IsVersionValid() const override;

		virtual Uint32 GetDataSize() const override;

		Bool HasTopologyData() const;

		static void RegisterIndexProcessor( red::UniquePtr<IMeshBlobIndexProcess>&& ptr );

		void PreprocessData();

	private:
		// Upgrade the blob in-place to the latest version.
		void UpgradeVersion( Bool allowMeshOptimization );

		// Upgrade the blob (no versioning)
		void PostProcessBlob();

	public:
		Header				m_header;
		DataBuffer			m_renderBuffer;					// Cooked vertex & index data (vertex first, index after)
	};
}//rnd


