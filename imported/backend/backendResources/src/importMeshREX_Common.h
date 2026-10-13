/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/worldStatic/include/instancedMeshNode.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"


#define MESH_IMPORT_LOG( ... ) RED_LOG_INFO( __VA_ARGS__ )
#define MESH_IMPORT_LOG_INFO( ... ) RED_LOG_INFO( __VA_ARGS__ )
#define MESH_IMPORT_LOG_ERROR( ... ) RED_LOG_ERROR( __VA_ARGS__ )


enum EChunkStreams : Uint8
{
	ECS_Positions = 0,
	ECS_Normals = 1,
	ECS_Tangents = 2,
	ECS_Binormals = 3,
	ECS_UV0 = 4,
	ECS_UV1 = 5,
	ECS_Colors = 6,
	ECS_SkinningWeights = 7,
	ECS_SkinningIndices = 8,
	ECS_SkinningWeightsExt = 9,
	ECS_SkinningIndicesExt = 10,
	ECS_Indices = 11,
	ECS_DestructionIndices = 12,
	ECS_MorphOffsets = 13,
	ECS_GarmentFlags = 14,
	ECS_VehicleDamagePosition = 15,
	ECS_VehicleDmgPosSides = 16, // obsolete, unused
	ECS_VehicleDamageNormal = 17,
	ECS_VehicleDmgNormalSides = 18, // obsolete, unused
	ECS_MorphVertexData = 19,
	ECS_WindBranchData_4F = 20,
	ECS_BranchData_7F = 21,
	ECS_LightBlockerIntensity = 22,
	ECS_MAX = 23
};

static const Float c_fromReScale = 0.01f;

class BACKEND_RESOURCES_API CREXMeshResourceImportOptions : public ResourceImportOptions
{
	RTTI_DECLARE_TYPE( CREXMeshResourceImportOptions );
	RED_BASE_CLASS( ResourceImportOptions );

public:
	CREXMeshResourceImportOptions();
	CREXMeshResourceImportOptions( const CREXMeshResourceImportOptions& other );

	virtual ~CREXMeshResourceImportOptions() {}

	CREXMeshResourceImportOptions& operator=( const CREXMeshResourceImportOptions& other );

	virtual Bool NeedOptions() const override;

	virtual bool ParseFromCommandLine( const red::CommandLine& stringOptions ) override;

	void ParseRenderFlagsFromReFile( const red::AbsolutePath sourceFilePath );

	Bool m_importChunks;
	Bool m_importMaterials;
	Bool m_importUV2AsMorphVertexData;
	Bool m_resetLODDistances;
	Bool m_importCollision;
	Bool m_importSkinning;
	Bool m_createSkinningLOD;
	Bool m_importRigs;
	Bool m_importSnapPoints;
	Bool m_importOcclusion;
	Bool m_removeUnusedMaterials;
	Bool m_prepareMeshTopology;
	Bool m_importLightConeData;
	Bool m_extrudeBoundingBox;
	Bool m_cleanPhysicsImport;
	Bool m_importWorkspotOffsets;
	Bool m_importAsPhysicalClothMesh;
	Bool m_importAsGraphicalClothMesh;
	Float m_boundingBoxExtrusionFactor;
	Bool m_matchChunksByOrder;
	Bool m_overrideExistingRenderFlags;
	Bool m_forceFlagsFromReFile;

	vis::WorldOccluderType m_occluderType;
	mesh::ChunkFlags m_renderFlags;
	ERenderObjectType m_objectType;

	void OnPostLoad(const PostLoadContext& context) override;
};
