/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/core/include/memoryBufferFile.h"

/// This file contains code to pack the engine mesh CMesh into the buffers suitable for rendering
/// It handles both the runtime (in editor) and cooking cases using the same code

#include "../../../common/renderData/include/gpuWrapApiVertexPacking.h"
#include "../../../common/renderData/include/renderMeshBlob.h"

typedef red::DynArray< GpuWrapApi::VertexPacking::PackingElement > GpuPacking;

struct Box;
class CMesh;

/// Chunk lister
class CMeshPackingSource
{
public:
	struct PackedLod
	{
		Float							m_distance;
	};

	struct PackedBuffer
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		GrowableMemoryBuffer*	m_data;			// raw data
		Uint32					m_placement;	// placement offset
		Uint64					m_hash;

		PackedBuffer()
			: m_placement(0)
			, m_hash(0)
			, m_data(nullptr)
		{}

		~PackedBuffer()
		{
			RED_DELETE( m_data );
		}
	};

	struct PackedStreams
	{
		PackedBuffer*			m_vertices[ RENDER_MESH_NUM_STREAMS ];
		PackedBuffer*			m_indices;

		PackedStreams(); // zero
		~PackedStreams(); // no free
	};

	struct SourceChunk
	{
		Uint32					m_chunkIndex;
		Uint32					m_baseVertexIndex;
	};

	struct PackedChunk
	{
		red::DynArray< SourceChunk > m_sourceChunks;
		red::DynArray< CName >		 m_materialId;

		Uint8						m_vertexFactory;
		GpuPacking					m_vertexPacking;

		Uint32						m_numVertices;
		Uint32						m_numIndices;

		EMeshChunkFlags				m_renderMask;				//!< Render mask of this chunk - telling us where should we render this chunk (scene, cascades, etc)
		Uint8						m_lodMask;
		Uint16						m_layerOffset;
		PackedStreams				m_streams;

		PackedChunk();
	};

	red::DynArray< PackedBuffer* >	m_vertices; // all packed vertices
	red::DynArray< PackedBuffer* >	m_indices; // all packed indices

	red::DynArray< PackedBuffer* >	m_topologyData;
	red::DynArray< PackedBuffer* >	m_topologyMetadata;

	red::DynArray< PackedChunk >	m_chunks; // final packed chunks (may be aliased)
	red::DynArray< PackedLod >		m_lods; // final packed LODs

	CMeshPackingSource();
	~CMeshPackingSource();
	
	// pack mesh 
	Bool Pack( const CMesh* mesh, Uint32& outVertexDataSize, Uint32& outIndexDataSize, Uint32& outTopologyDataSize, Uint32& outTopologyMetadataSize, const Box* boundingBoxOverride = nullptr, Bool verbose = false );

	// copy ALL vertex data into specified memory, requires layout to be computed first
	void CopyVertexData( void* outputMemory, const Uint32 bufferSize ) const;

	// copy ALL index data into specified memory, requires layout to be computed first
	void CopyIndexData( void* outputMemory, const Uint32 bufferSize ) const;

	// deal with topology data and metadata
	void CopyTopologyData( void* outputMemory, const Uint32 bufferSize ) const;
	void CopyTopologyMetadata( void* outputMemory, const Uint32 bufferSize ) const;

private:
	// compute final buffer layout
	void ComputeLayout( const Uint32 vertexDataAlignment, const Uint32 indexDataAlignment, Uint32& outVertexDataSize, Uint32& outIndexDataSize );

	// Create vertex buffer wrapper from given data
	PackedBuffer* CreateVertexBuffer( GrowableMemoryBuffer*& memory );

	// Create inex buffer wrapper from given data
	PackedBuffer* CreateIndexBuffer( GrowableMemoryBuffer*& memory );
};
