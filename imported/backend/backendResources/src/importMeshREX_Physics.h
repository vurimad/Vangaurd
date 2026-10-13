/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"

#include "../../../common/physics/include/physicsSceneStructs.h"

namespace physics
{
	class ICollider;
	class ColliderMesh;
	
}


class CMesh;

namespace mesh
{
	class DataBuilder;
}


struct CollShapeImportData
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	CName m_name;
	THandle< physics::ICollider > m_shape;
	physics::SimulationType m_simulationType;
};


struct ReCollisionsImportContext
{
	std::shared_ptr< ReFileMesh > reMesh;

	std::vector< ReFileCollisionConvexPtr > convexCollisions;
	std::vector< ReFileCollisionBoxPtr > boxes;
	std::vector< ReFileCollisionSpherePtr > spheres;
	std::vector< ReFileCollisionCapsulePtr > capsules;
	std::vector< ReFileTriMeshPtr > triMeshes;
};


void SetCollisionFilterData( const ReFileBaseCollisionNode& collisionNode, physics::ICollider& collisionItem );
void GenerateDestructionHierarchy( ReFileTransformPtr rootNode, CMesh* meshData, Uint32 numBones, red::DynArray< CName >& boneNames );
String ImportPhysicsDefinition( const ReFilePhysics& rePhysics, THandle< physics::SystemResource > physicsRes, const THandle< CREXMeshResourceImportOptions >& options );
void ReadCollisionsFromMeshes( const std::vector< ReFileMeshPtr > meshes, ReCollisionsImportContext& collContext );

String ImportCollisionBoxes( const std::vector< ReFileCollisionBoxPtr >& boxes, red::DynArray< CollShapeImportData >& collisionItems );
String ImportCollisionCapsules( const std::vector< ReFileCollisionCapsulePtr >& capsules, red::DynArray< CollShapeImportData >& collisionItems );
String ImportCollisionConvexes( const std::vector< ReFileCollisionConvexPtr >& convexCollisions, red::DynArray< CollShapeImportData >& collisionItems );
String ImportCollisionSpheres( const std::vector< ReFileCollisionSpherePtr >& spheres, red::DynArray< CollShapeImportData >& collisionItems );
String ImportCollisionTriMeshes( const std::vector< ReFileTriMeshPtr >& trimeshes, red::DynArray< CollShapeImportData >& collisionItems );

ImportResult ImportCollisionData( CMesh* meshData, const THandle< CREXMeshResourceImportOptions >& options, const ReCollisionsImportContext& collContext, mesh::DataBuilder& builder );
void ImportDestructionHierarchy( CMesh* meshData, ReFileSkeletonPtr skeleton, mesh::DataBuilder& builder );
ImportResult ClearDestructionData( CMesh* meshData );