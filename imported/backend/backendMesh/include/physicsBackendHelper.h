/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/types.h"
#include "../../../common/redMemory/include/sharedPtr.h"
#include "../../../common/redContainers/include/dynArray.h"

class CMesh;

namespace physics
{
	class SystemResource;
	class ICollider;
	class SystemJoint;
	class ISystemObject;

	class PhysicsEditorScene;
	class PhysicsBase;
	class PhysicsActor;
	class PhysicsShape;
	class PhysicsJoint;
	class PhysicsMesh;
	class ShapeGeometryBase;
	class EditorActorCOM;

	using PhysicsBaseID = tools::EditorObjectInternalID;
	using PhysicsBasePtr = THandle< PhysicsBase >;
	using PhysicsActorPtr = THandle< PhysicsActor >;
	using PhysicsShapePtr = THandle< PhysicsShape >;
	using PhysicsJointPtr = THandle< PhysicsJoint >;
	using PhysicsMeshPtr = THandle< PhysicsMesh >;
	using ShapeGeometryPtr = THandle< ShapeGeometryBase >;

	typedef red::HashMap< physics::PhysicsBaseID, THandle< physics::ISystemObject > > TSystemObjectsMap;

	class BACKEND_MESH_API PhysicsBackendHelper
	{
	public:
		static void 										GenerateSkinningFromPhysicsObjects( PhysicsMeshPtr mesh, THandle<CMesh> meshHandle, PhysicsBasePtr meshActor, red::DynArray<PhysicsActorPtr>& actors );
		static Bool											CheckPointInGeometry( THandle< ShapeGeometryBase > geom, const Vector4& posToCheck, const Vector4& positionOfShape );

		static THandle< physics::SystemResource >			ConvertToPhysicsSystemFromEditor( red::DynArray< PhysicsBasePtr >& basePtrs, TSystemObjectsMap& actorMap, PhysicsMeshPtr mesh );
		static void			                                CreateJointsForPhysicsSystemFromEditor( red::DynArray< PhysicsBasePtr >& basePtrs, TSystemObjectsMap& actorMap, THandle< physics::SystemResource > psResource, PhysicsMeshPtr mesh = nullptr );
		static red::DynArray< PhysicsBasePtr >				ConvertFromPhysicsSystemToEditor( THandle< physics::SystemResource > physicsSystem, physics::PhysicsEditorScene* scene, THandle<physics::PhysicsMesh> meshPtr);
		static red::DynArray< PhysicsBasePtr >				GenerateEditorActorsForMesh( const Matrix& meshL2W, physics::PhysicsEditorScene* scene, THandle<physics::PhysicsMesh> meshPtr = nullptr );

		static Bool											ComputeCOMForEditorActor( PhysicsActorPtr& actorPtr );
		static void											BindPhysicsActorToMesh( PhysicsActorPtr& actorPtr, PhysicsMeshPtr& meshPtr, CName boneName, const Transform& actorL2M = Transform::IDENTITY() );

		static physics::ShapeGeometryPtr					GetEditorGeometryFromCollider( THandle< physics::ICollider> collider );
		static THandle<physics::ICollider>					GetColliderFromEditorGeometry( PhysicsShapePtr shape );

		static void											SetupPhysSystemJointLimits( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint );
		static void											SetupPhysEditorJointLimits( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint );
		static void											SetupPhysicsSystemJointDrives( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint );
		static void											SetupEditorJointDrives( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint );

		static Bool											FindPhysicsMesh( physics::PhysicsEditorScene* editorScene, THandle< physics::PhysicsMesh >& meshPtr );
	};
}