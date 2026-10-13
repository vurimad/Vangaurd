/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _ENGINE_MESH_VERTEX_H_
#define _ENGINE_MESH_VERTEX_H_

enum EMeshVertexType : Int32
{
	MVT_StaticMesh,							//!< Static mesh without skinning data
	MVT_ProceduralMesh,						//!< Static mesh without skinning data with instances generated procedurally
	MVT_SkinnedMesh,						//!< Skinned mesh
	MVT_ExtSkinnedMesh,						//!< Extended skinned mesh (8 bones per vertex, double weights & indices data)
	MVT_GarmentSkinnedMesh,					//!< Garment skinned mesh (morph offset channel added)
	MVT_ExtGarmentSkinnedMesh,				//!< Extended garment skinned mesh (morph offset channel, 8 bones per vertex, double weights & indices data)
	MVT_SpeedTreeMesh,						//!< Speed tree mesh
	MVT_StaticMeshVehicle,					//!< Vehicle (extra channels for post-damage position and normals)
	MVT_SkinnedMeshVehicle,					//!< Vehicle (extra channels for post-damage position and normals)
	MVT_Terrain,							//!< Thin terrain vertex
	MVT_DestructibleMesh,					//!< Static mesh with destruction streams (i.e. pre-fractured)
	MVT_DestructibleMeshSkinned,			//!< Static mesh with destruction streams (i.e. pre-fractured). Uses skinning data to reduce transform texture size.
	MVT_SkinnedMeshLightBlocker,			//!< Skinned mesh with light blocker intensity per vertex
	MVT_ExtSkinnedMeshLightBlocker,			//!< Extended skinned mesh (8 bones per vertex) with light blocker intensity per vertex
	MVT_GarmentSkinnedMeshLightBlocker,		//!< Garment skinned mesh (morph offset channel added) with light blocker intensity per vertex
	MVT_ExtGarmentSkinnedMeshLightBlocker,	//!< Extended garment skinned mesh (morph offset channel, 8 bones per vertex, double weights & indices data) with light blocker intensity per vertex
	MVT_SkinnedMeshSingleBone,				//!< Skinned mesh with a single bone skinning (i.e. physical destruction)
	MVT_ProxyMesh,							//!< Super simple mesh for proxy, no tangents, no normal maps, no vertex color, no extra streams
	MVT_ProxyWindowMesh,					//!< Super simple mesh for window proxy, no tangents, no normal maps, no vertex color, no extra streams except for window informations

	MVT_Max
};

RTTI_DECLARE_ENUM( EMeshVertexType );

// Mesh vertex, bulk serialized
//CM: please make IPlatformsUtils.h in sync with this
struct SMeshVertex
{
	Float	m_position[3];				// Vertex position
	Uint8	m_indices[4];				// Skinning indices
	Float	m_weights[4];				// Skinning weights
	Uint8	m_indicesExt[4];			// Extended skinning indices
	Float	m_weightsExt[4];			// Extended skinning weights
	Float	m_normal[3];				// Vertex normal
	Uint32	m_color;					// Vertex color
	Float	m_uv0[2];					// First uv set
	Float	m_uv1[2];					// Second uv set
	Float	m_tangent[3];				// Vertex tangent vector
	Float	m_binormal[3];				// Vertex binormal vector
	Uint16	m_destruction[2];			// Destruction indices
	Float	m_multilayer;				// Multilayer paint
	Float	m_extraData0[4];			// Extra data
	Float	m_extraData1[4];			// Extra data
	Float	m_extraData2[4];			// Extra data
	Float	m_extraData3[4];			// Extra data
	Float   m_vehicleDamageNormal[3];	// Vehicle post-destruction normals
	Float   m_vehicleDmgNormalSides[3];	// obsolete, unused
	Float   m_vehicleDamagePosition[3];	// Vehicle post-destruction position
	Float   m_vehicleDmgPosSides[3];	// obsolete, unused
	Float   m_lightBlockerData;			// Vehicle post-destruction position

	RED_INLINE Vector4 GetPosition() const
	{
		return Vector4( m_position[0], m_position[1], m_position[2], 1.0f );
	}
};

void operator<<( IFile& file, SMeshVertex& v );

RED_INLINE Bool IsCollisionMeshVertexType( EMeshVertexType vertexType )
{
	return ( MVT_StaticMesh == vertexType ) 
		|| ( MVT_DestructibleMesh == vertexType )
		|| ( MVT_DestructibleMeshSkinned == vertexType )
		|| ( MVT_ProceduralMesh == vertexType )
		|| ( MVT_SkinnedMesh == vertexType )
		|| ( MVT_ExtSkinnedMesh == vertexType )
		|| ( MVT_GarmentSkinnedMesh == vertexType )
		|| ( MVT_ExtGarmentSkinnedMesh == vertexType )
		|| ( MVT_StaticMeshVehicle == vertexType )
		|| ( MVT_SkinnedMeshVehicle == vertexType )
		|| ( MVT_SkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_GarmentSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtGarmentSkinnedMeshLightBlocker == vertexType )
		;
}

RED_INLINE Bool IsSkinnedMeshVertexType( EMeshVertexType vertexType )
{
	return ( MVT_SkinnedMesh == vertexType )
		|| ( MVT_ExtSkinnedMesh == vertexType )
		|| ( MVT_GarmentSkinnedMesh == vertexType)
		|| ( MVT_ExtGarmentSkinnedMesh == vertexType)
		|| ( MVT_SkinnedMeshVehicle == vertexType )
		|| ( MVT_SkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_GarmentSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtGarmentSkinnedMeshLightBlocker == vertexType )
		;
}

RED_INLINE Bool IsLightBlockerVertexType( EMeshVertexType vertexType )
{
	return ( MVT_SkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_GarmentSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtGarmentSkinnedMeshLightBlocker == vertexType )
		;
}

RED_INLINE Bool IsGarmentVertexType( EMeshVertexType vertexType )
{
	return ( MVT_GarmentSkinnedMesh == vertexType )
		|| ( MVT_ExtGarmentSkinnedMesh == vertexType )
		|| ( MVT_GarmentSkinnedMeshLightBlocker == vertexType )
		|| ( MVT_ExtGarmentSkinnedMeshLightBlocker == vertexType )
		;
}

RED_INLINE EMeshVertexType SelectSkinnedVertexType( const Bool usesGarmentMorphOffsets, const Bool usesExtendedSkinning, const Bool usesLightBlockers, const Bool usesVehicleDamage, const Bool usesDestruction )
{
	if ( usesVehicleDamage )
	{
		return MVT_SkinnedMeshVehicle;
	}
	else if ( usesDestruction )
	{
		return MVT_DestructibleMeshSkinned;
	}
	else if ( usesExtendedSkinning )
	{
		if ( usesGarmentMorphOffsets )
		{
			return usesLightBlockers ? MVT_ExtGarmentSkinnedMeshLightBlocker : MVT_ExtGarmentSkinnedMesh;
		}
		else
		{
			return usesLightBlockers ? MVT_ExtSkinnedMeshLightBlocker : MVT_ExtSkinnedMesh;
		}
	}
	else
	{
		if ( usesGarmentMorphOffsets )
		{
			return usesLightBlockers ? MVT_GarmentSkinnedMeshLightBlocker : MVT_GarmentSkinnedMesh;
		}
		else
		{
			return usesLightBlockers ? MVT_SkinnedMeshLightBlocker : MVT_SkinnedMesh;
		}
	}
}

#endif
