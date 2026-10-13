/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshChunk.h"
#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/fileVersionList.h"

RTTI_BEGIN_ENUM( EMeshVertexType );
	RTTI_ENUM_OPTION( MVT_StaticMesh );
	RTTI_ENUM_OPTION( MVT_ProceduralMesh );
	RTTI_ENUM_OPTION( MVT_SkinnedMesh );
	RTTI_ENUM_OPTION( MVT_ExtSkinnedMesh );
	RTTI_ENUM_OPTION( MVT_GarmentSkinnedMesh );
	RTTI_ENUM_OPTION( MVT_ExtGarmentSkinnedMesh );
	RTTI_ENUM_OPTION( MVT_SpeedTreeMesh );
	RTTI_ENUM_OPTION( MVT_StaticMeshVehicle );
	RTTI_ENUM_OPTION( MVT_SkinnedMeshVehicle );
	RTTI_ENUM_OPTION( MVT_Terrain );
	RTTI_ENUM_OPTION( MVT_DestructibleMesh );
	RTTI_ENUM_OPTION( MVT_DestructibleMeshSkinned );
	RTTI_ENUM_OPTION( MVT_SkinnedMeshLightBlocker );
	RTTI_ENUM_OPTION( MVT_ExtSkinnedMeshLightBlocker );
	RTTI_ENUM_OPTION( MVT_GarmentSkinnedMeshLightBlocker );
	RTTI_ENUM_OPTION( MVT_ExtGarmentSkinnedMeshLightBlocker );
	RTTI_ENUM_OPTION( MVT_SkinnedMeshSingleBone );
	RTTI_ENUM_OPTION( MVT_ProxyMesh );
	RTTI_ENUM_OPTION( MVT_ProxyWindowMesh );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE( SMeshTopology );
	RTTI_PROPERTY( m_data );
	RTTI_PROPERTY( m_metadata );
RTTI_END_TYPE();

RTTI_BEGIN_ENUM( EMeshStreamType );
	RTTI_ENUM_OPTION( MST_Position_3F );
	RTTI_ENUM_OPTION( MST_SkinningIndices_4U8 );
	RTTI_ENUM_OPTION( MST_SkinningWeights_4F );
	RTTI_ENUM_OPTION( MST_SkinningIndicesExt_4U8 );
	RTTI_ENUM_OPTION( MST_SkinningWeightsExt_4F );
	RTTI_ENUM_OPTION( MST_Color_U32 );
	RTTI_ENUM_OPTION( MST_TexCoord0_2F );
	RTTI_ENUM_OPTION( MST_TexCoord1_2F );
	RTTI_ENUM_OPTION( MST_Normal_3F );
	RTTI_ENUM_OPTION( MST_Tangent_3F );
	RTTI_ENUM_OPTION( MST_Binormal_3F );
	RTTI_ENUM_OPTION( MST_DestructionIndices_2U16 );
	RTTI_ENUM_OPTION( MST_Multilayer_1F );
	RTTI_ENUM_OPTION( MST_Index_U16 );
	RTTI_ENUM_OPTION( MST_GarmentFlags_U32 );
	RTTI_ENUM_OPTION( MST_MorphOffset_3F );
	RTTI_ENUM_OPTION( MST_VehicleDmgNormalFront_3F );
	RTTI_ENUM_OPTION( MST_VehicleDmgNormalSides_3F );
	RTTI_ENUM_OPTION( MST_VehicleDmgPosFront_3F );
	RTTI_ENUM_OPTION( MST_VehicleDmgPosSides_3F );
	RTTI_ENUM_OPTION( MST_WindBranchData_4F );
	RTTI_ENUM_OPTION( MST_BranchData_7F );
	RTTI_ENUM_OPTION( MST_MorphVertexData_3F );
	RTTI_ENUM_OPTION( MST_FoliageBoneId_I16 );
	RTTI_ENUM_OPTION( MST_LightBlockerIntensity_1F );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE( SMeshStream );
	RTTI_PROPERTY( m_data );
	RTTI_PROPERTY( m_type ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( SMeshChunkPacked );
	RTTI_PROPERTY( m_vertexType ).readOnly();
	RTTI_PROPERTY( m_materialID ).readOnly();
	RTTI_PROPERTY( m_numBonesPerVertex ).readOnly();
	RTTI_PROPERTY( m_numVertices ).readOnly();
	RTTI_PROPERTY( m_numIndices ).readOnly();
	RTTI_PROPERTY( m_firstVertex ).readOnly();
	RTTI_PROPERTY( m_firstIndex ).readOnly();
	RTTI_PROPERTY_BITFIELD( m_renderMask, EMeshChunkRenderMask ).readOnly();
	RTTI_PROPERTY_BITFIELD( m_chunkRenderMask, EMeshChunkFlags ).readOnly();
	RTTI_PROPERTY( m_useForShadowmesh ).readOnly();
	RTTI_PROPERTY( m_streams ).readOnly();
	RTTI_PROPERTY( m_streamMask ).readOnly();
	RTTI_PROPERTY( m_lodMask ).readOnly();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_sourceMeshUUID ).readOnly().notCooked();
	RTTI_PROPERTY( m_sourceMaterialUUID ).readOnly().notCooked();
#endif // RED_CONFIGURATION_FINAL
RTTI_END_TYPE();

SMeshChunk::SMeshChunk()
	: m_vertexType( MVT_StaticMesh )
	, m_numBonesPerVertex( 0 )
	, m_numVertices( 0 )
	, m_numIndices( 0 )
	, m_renderMask( MCF_RenderInScene | MCF_RenderInShadows )
{
}

SMeshChunk::SMeshChunk( const SMeshChunk& other ) = default;
SMeshChunk::SMeshChunk( SMeshChunk&& other ) = default;

SMeshChunk::~SMeshChunk()
{
}

SMeshChunk& SMeshChunk::operator=( const SMeshChunk& other ) = default;
SMeshChunk& SMeshChunk::operator=( SMeshChunk&& other ) = default;

SMeshChunkPacked::SMeshChunkPacked()
	: m_vertexType( MVT_StaticMesh )
	, m_renderMask( EMeshChunkFlags( MCF_RenderInScene | MCF_RenderInShadows ) ) // DEPRECATED
	, m_chunkRenderMask( EMeshChunkFlags( MCF_RenderInScene | MCF_RenderInShadows ) )
	, m_numBonesPerVertex( 0 )
	, m_useForShadowmesh( false )
	, m_numVertices( 0 )
	, m_numIndices( 0 )
	, m_firstVertex( 0 )
	, m_firstIndex( 0 )
	, m_streamMask( 0 )
	, m_lodMask( 0 )
{
}

SMeshChunkPacked::SMeshChunkPacked( SMeshChunkPacked&& other ) = default;

SMeshChunkPacked::~SMeshChunkPacked()
{
}

SMeshChunkPacked& SMeshChunkPacked::operator=( SMeshChunkPacked&& other ) = default;


// Old function to support legacy crap
void SerializeVertexType( IFile& file, EMeshVertexType& vt )
{
	// Read the string value
	String valueStr;
	file << valueStr;

	// Find the value
	CName valueName = RED_NAME( valueStr );
	Int64 value = 0;

	static const rtti::EnumType* enumType = GetRttiSystem().FindEnum( GetTypeName< EMeshVertexType >() );
	if ( enumType->FindValue( valueName, value ) )
		vt = (EMeshVertexType) value;
}

void operator<<( IFile& file, SMeshChunk& c )
{
	// Special code to serialize vertex type, a lot of old legacy...
	SerializeVertexType( file, c.m_vertexType );

	file << c.m_materialID;
	file << c.m_numBonesPerVertex;
	file << c.m_numVertices;
	file << c.m_numIndices;
	file << c.m_renderMask;

	// Serialize as bulk data
	file << BulkSerialization( c.m_vertices );
	file << BulkSerialization( c.m_indices );

	// Restore tables sizes
	if ( file.IsReader() )
	{
		c.m_numVertices = c.m_vertices.Size();
		c.m_numIndices = c.m_indices.Size();
	}

	// Loaded
}