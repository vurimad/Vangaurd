/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _ENGINE_MESH_CHUNK_H_
#define _ENGINE_MESH_CHUNK_H_

#include "meshVertex.h"

#include "../../../common/renderData/include/meshEnum.h"
#include "../../../common/renderData/include/renderingMemory.h"
#include "../../../common/redReflection/include/deferredDataBuffer.h"

enum EMeshStreamType : Uint32
{
	MST_Position_3F			= RED_FLAG( 0 ),

	MST_SkinningIndices_4U8 = RED_FLAG( 1 ),
	MST_SkinningWeights_4F	= RED_FLAG( 2 ),

	MST_Color_U32			= RED_FLAG( 3 ),

	MST_TexCoord0_2F		= RED_FLAG( 4 ),
	MST_TexCoord1_2F		= RED_FLAG( 5 ),

	MST_Normal_3F			= RED_FLAG( 6 ),
	MST_Tangent_3F			= RED_FLAG( 7 ),
	MST_Binormal_3F			= RED_FLAG( 8 ),

	MST_Index_U16			= RED_FLAG( 9 ),

	MST_WindBranchData_4F	= RED_FLAG( 10 ),
	MST_LeafLod_1F			= RED_FLAG( 11 ),
	MST_LeafCardCorner_3F	= RED_FLAG( 12 ),
	MST_LodPosition_3F		= RED_FLAG( 13 ),
	MST_BranchData_7F		= RED_FLAG( 14 ),
	MST_FrondData_4F		= RED_FLAG( 15 ),
	MST_LeafAnchors_3F		= RED_FLAG( 16 ),
	MST_LeafWindData_4F		= RED_FLAG( 17 ),

	// Maybe these should be moved by the other skinning? Left it here, because it seems the stream mask is being stored
	// as integer instead of RTTI bitfield. So let's not break things!
	MST_SkinningIndicesExt_4U8		= RED_FLAG( 18 ),
	MST_SkinningWeightsExt_4F		= RED_FLAG( 19 ),

	MST_DestructionIndices_2U16		= RED_FLAG( 20 ),
	MST_Multilayer_1F				= RED_FLAG( 21 ), // obsolete, unused

	MST_GarmentFlags_U32			= RED_FLAG( 22 ),
	MST_MorphOffset_3F				= RED_FLAG( 23 ),

	MST_VehicleDmgNormalFront_3F			= RED_FLAG( 24 ),
	MST_VehicleDmgNormalSides_3F	= RED_FLAG( 25 ), // obsolete, unused
	MST_VehicleDmgPosFront_3F		= RED_FLAG( 26 ),
	MST_VehicleDmgPosSides_3F		= RED_FLAG( 27 ), // obsolete, unused

	MST_MorphVertexData_3F 			= RED_FLAG( 28 ),

	MST_FoliageBoneId_I16			= RED_FLAG( 29 ),

	MST_LightBlockerIntensity_1F	= RED_FLAG( 30 ),

	MST_None						= RED_FLAG( 31 )
};

RTTI_DECLARE_ENUM( EMeshStreamType );

static const Uint32 MAX_MESH_STREAMS = 29;

struct SMeshStream : red::NonCopyable
{
	RTTI_DECLARE_TYPE( SMeshStream );
	RED_USE_MEMORY_POOL( rend::PoolMesh );

	serialization::DeferredDataBuffer		m_data;
	EMeshStreamType							m_type;

	SMeshStream()
		: m_data( rend::PoolMesh() )
		, m_type( MST_None )
	{}
};

struct SMeshTopology
{
	RTTI_DECLARE_TYPE( SMeshTopology );

	DataBuffer		m_data;
	DataBuffer		m_metadata;

	SMeshTopology()
	{}
};

struct SRawVertexDesc
{
	EMeshStreamType							m_type;					// Component type
	Uint32									m_offset;				// Component offset inside of the interleaved structure

	SRawVertexDesc( EMeshStreamType type, Uint32 offset )
		: m_type( type )
		, m_offset( offset )
	{}
};

/// Source mesh chunk - used to initialize mesh
// All uses of this struct seem #ifdefed out at the moment
struct RESOURCEMESH_API SMeshChunk
{
	SMeshChunk();
	SMeshChunk( const SMeshChunk& other );
	SMeshChunk( SMeshChunk&& other );
	~SMeshChunk();

	SMeshChunk& operator=( const SMeshChunk& other );
	SMeshChunk& operator=( SMeshChunk&& other );


	EMeshVertexType							m_vertexType;			// Mesh vertex type
	red::DynArray< Uint32 >					m_materialID{ red::PoolBackend() };			// Material indices
	Uint8									m_numBonesPerVertex;	// Number of bones used per vertex
	Uint32									m_numVertices;			// Number of vertices
	Uint32									m_numIndices;			// Number of indices
	red::DynArray< SMeshVertex >			m_vertices{ red::PoolBackend() };				// Vertices
	red::DynArray< Uint16 >					m_indices{ red::PoolBackend() };				// Indices
	Uint16									m_renderMask;			// Where should we render this chunk ?
};

extern void operator<<( class IFile& file, SMeshChunk& c );

//---------------------------------------------------------------

struct RESOURCEMESH_API SMeshChunkPacked : red::NonCopyable
{
	RTTI_DECLARE_TYPE( SMeshChunkPacked );
	RED_USE_MEMORY_POOL( rend::PoolMesh );

public:
	SMeshChunkPacked();
	SMeshChunkPacked( SMeshChunkPacked&& other );
	~SMeshChunkPacked();
	SMeshChunkPacked& operator=( SMeshChunkPacked&& other );


	EMeshVertexType							m_vertexType;			// Mesh vertex type
	Uint16									m_renderMask;			// Where should we render this chunk ? DEPRECATED
	EMeshChunkFlags							m_chunkRenderMask;		// RenderMask of this chunk
	Uint8									m_numBonesPerVertex;	// Number of bones used per vertex
	Bool									m_useForShadowmesh;		// Use this chunk for shadowmesh
	Uint32									m_numVertices;			// Number of vertices
	Uint32									m_numIndices;			// Number of indices
	Uint32									m_firstVertex;			// First vertex in the chunk array
	Uint32									m_firstIndex;			// First vertex in the chunk array
	Uint64									m_streamMask;			// TODO: Remove?
																							/// @todo DP: remove m_materialID, use MeshAppearance
	red::DynArray< CName >					m_materialID{ rend::PoolMesh() };			// Material index
	red::DynArray< SMeshStream >			m_streams{ rend::PoolMesh() };				// List of streams for this chunk

	Uint8									m_lodMask;				// Bit mask of LOD levels this chunk belongs to

#ifndef RED_CONFIGURATION_FINAL
	String									m_sourceMeshUUID;       // From Maya exporter, this is so we can match new vs old chunks when the mesh re-imported.
	String									m_sourceMaterialUUID;   // From Maya exporter, this is so we can match new vs old chunks when the mesh re-imported.
#endif // RED_CONFIGURATION_FINAL
};

#endif
