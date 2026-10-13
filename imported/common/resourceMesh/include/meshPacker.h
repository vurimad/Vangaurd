#pragma once

enum EMeshStreamType : Uint32;

struct SMeshStream;
struct SMeshStreamChunk;

enum ERenderStreamType : Uint64
{
	RST_Position_4US			= RED_FLAG64( 0 ),
	RST_Position_4F				= RED_FLAG64( 1 ),

	RST_Normal_Dec4				= RED_FLAG64( 2 ),
	RST_Tangent_Dec4			= RED_FLAG64( 3 ),
	RST_Binormal_Dec4			= RED_FLAG64( 4 ),

	RST_TexCoord0_2F16			= RED_FLAG64( 5 ),
	RST_TexCoord1_2F16			= RED_FLAG64( 6 ),

	RST_Color_U32				= RED_FLAG64( 7 ),

	RST_SkinningIndices_4U8N	= RED_FLAG64( 8 ),
	RST_SkinningWeights_4U8		= RED_FLAG64( 9 ),
	RST_SkinningIndicesExt_4U8N	= RED_FLAG64( 10 ),
	RST_SkinningWeightsExt_4U8	= RED_FLAG64( 11 ),

	RST_DestructionIndices_2U16 = RED_FLAG64( 12 ),
	RST_Multilayer_1F			= RED_FLAG64( 13 ),

	RST_VehicleDmgNormalFront_Dec4 = RED_FLAG64( 14 ),
	RST_VehicleDmgNormalSides_Dec4 = RED_FLAG64( 15 ), // obsolete, unused
	RST_VehicleDmgPosFront_4F	= RED_FLAG64( 16 ),
	RST_VehicleDmgPosSides_4F	= RED_FLAG64( 17 ), // obsolete, unused

	RST_LightBlockerIntensity_1F	= RED_FLAG64( 18 ),

	RST_Index_U16				= RED_FLAG64( 19 )
};

typedef Uint64 RenderStreamType;

struct SMeshStreamMapping
{
	EMeshStreamType				m_inputStreamType;
	ERenderStreamType			m_outputStreamType;

	SMeshStreamMapping( EMeshStreamType inputStreamType, ERenderStreamType outputStreamType )
		: m_inputStreamType( inputStreamType )
		, m_outputStreamType( outputStreamType )
	{}
};

struct SMeshStreamPart
{
	red::DynArray< SMeshStreamMapping > m_streamMappings{ rend::PoolMesh() };
};

typedef red::DynArray< SMeshStreamPart > MeshStreamParts;

struct SStreamChunk
{
	MeshStreamParts m_parts = { rend::PoolMesh() };
	red::DynArray< DataBuffer > m_data = red::DynArray< DataBuffer >( rend::PoolMesh() );
};

struct SQuantizationParams
{
	math::Vector4 m_scale;
	math::Vector4 m_offset;

	SQuantizationParams()
		: m_scale( math::Vector4::ONES() )
		, m_offset( math::Vector4::ZEROS() )
	{
	}

	SQuantizationParams( const Box& box )
	{
		m_scale = box.CalcSize();

		m_scale.X = ( m_scale.X > 0.0001f ) ? ( 1.0f / m_scale.X ) : 0.0f;
		m_scale.Y = ( m_scale.Y > 0.0001f ) ? ( 1.0f / m_scale.Y ) : 0.0f;
		m_scale.Z = ( m_scale.Z > 0.0001f ) ? ( 1.0f / m_scale.Z ) : 0.0f;

		m_offset = box.Min;
	}
};

struct SPackedMeshPart
{
	RenderStreamType			m_renderFormat;
	Uint32						m_offset;
	Uint32						m_size;
};

typedef red::DynArray< SPackedMeshPart > PackedMeshParts;

struct SPackedMesh
{
	PackedMeshParts				m_parts{ rend::PoolMesh() };
	DataBuffer					m_data;
};

class CMesh;
struct SMeshChunkPacked;

class RESOURCEMESH_API CMeshPacker
{
public:
	Bool Pack( const CMesh& mesh, SPackedMesh& outPackedMesh );

private:
	Bool ValidateMesh( const CMesh& mesh );
	Bool ProcessMesh( const CMesh& mesh, SPackedMesh& outPackedMesh );
	
	void ProcessChunk( const SMeshChunkPacked& chunk, red::DynArray< SStreamChunk >& streamChunks );
	void ComputeStreamMapping( Uint64 meshStreamMask, MeshStreamParts& parts );
	void PackSingleStream( const SMeshStream* stream, ERenderStreamType renderStreamType, DataBuffer* outBuffer );
	void PackOutputStreamData( const SMeshChunkPacked& chunk, const MeshStreamParts& streamParts, red::DynArray< DataBuffer >& outputStreamData );
	void MergeOutputStreamData( const MeshStreamParts& streamParts, const red::DynArray< DataBuffer >& outputStreamData, void* output );

	SQuantizationParams m_quantizationParameters;
};

namespace MeshPackerUtils
{
	RESOURCEMESH_API Uint32 GetMeshStreamSize( EMeshStreamType streamType );
	RESOURCEMESH_API Uint32 GetMeshRenderStreamSize( ERenderStreamType streamType );
	RESOURCEMESH_API Uint32 GetFatVertexElementOffset( EMeshStreamType streamType );
	RESOURCEMESH_API Uint32 MeshStreamTypeToIndex( EMeshStreamType streamType );
}
