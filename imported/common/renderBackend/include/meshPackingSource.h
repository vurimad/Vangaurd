/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../renderData/include/gpuWrapApiVertexFormats.h"
#include "../../renderData/include/gpuWrapApiVertexPacking.h"
#include "../../renderData/include/renderMeshBlob.h"
#include "renderBackendApi.h"

typedef red::DynArray< GpuWrapApi::VertexPacking::PackingElement > GpuPacking;

namespace tools
{
/// Chunk lister
class RENDERBACKEND_API MeshPackingSource
{
public:
	struct PackedLod
	{
		Float							m_distance;
	};

	struct RENDERBACKEND_API PackedStreams
	{
		Uint32			m_vertices[ RENDER_MESH_NUM_STREAMS ];
		Uint32			m_indices;
		Uint32			m_topologyData;
		Uint32			m_topologyMetadata;

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
		red::DynArray< SourceChunk >	m_sourceChunks;

		Uint8							m_vertexFactory;
		GpuPacking						m_vertexPacking;

		Uint32							m_numVertices;
		Uint32							m_numIndices;

		EMeshChunkFlags					m_renderMask;				//!< Render mask of this chunk - telling us where should we render this chunk (scene, cascades, etc)
		Uint8							m_lodMask;
		PackedStreams					m_streams;

		PackedChunk()
			: m_numVertices( 0 )
			, m_numIndices( 0 )
			, m_renderMask( EMeshChunkFlags(0) )
			, m_lodMask( 1 )
			, m_vertexFactory( 0 )
		{
		}
	};

	red::DynArray< PackedChunk >	m_chunks; // final packed chunks (may be aliased)
	red::DynArray< PackedLod >		m_lods; // final packed LODs

	Uint32							m_vertexDataSize;
	Uint32							m_indexDataSize;
	Uint32							m_topologyDataSize;
	Uint32							m_topologyMetadataSize;

	red::DynArray< Uint8 >			m_renderData;

	MeshPackingSource();
	~MeshPackingSource();
};
}//::tools