/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "physicsEditorScene.h"
#include "physicsBackendHelper.h"
#include "physicsShape.h"
#include "physicsActor.h"
#include "physicsJoint.h"
#include "physicsMesh.h"

#include "meshDataView.h"
#include "meshDataBuilder.h"
#include "meshGluer.h"
#include "meshPhysicsUtils.h"

#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physicsSystemJoint.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/physics/include/physicsSystemUtils.h"

#include "../../../common/redReflection/include/convexHullBuilder.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/redSystem/include/log.h"

namespace physics
{
	////////////////////////////////////////////////////////////////////////////////////
	// Function that generates skinning streams for the mesh provided in 
	// meshHandle, based on shapes of the actors, as well as bones and
	// actor bindings in editor mesh.
	// DEPRECATED - was experimental to begin with, gonna be refactored in the future
	////////////////////////////////////////////////////////////////////////////////////
	void PhysicsBackendHelper::GenerateSkinningFromPhysicsObjects( PhysicsMeshPtr mesh, THandle<CMesh> meshHandle, PhysicsBasePtr meshActor, red::DynArray<PhysicsActorPtr>& actors )
	{
		Uint32 actorCnt = actors.Size();
		if( actorCnt <= 0 ) return;

		mesh::DataView meshDataView( *meshHandle );
		// Initialize an actorBinds array - it's used to store boneIndex assigned to an actor, -1 means the actor is not assigned to a bone
		red::DynArray<Int32> actorBinds = { red::PoolBackend() };
		actorBinds.Resize( actorCnt );
		for( Uint32 i = 0; i < actorCnt; ++i )
		{
			actorBinds[ i ] = -1;
		}

		Matrix localToWorld = meshActor->GetLocalToWorld();
		Uint32 chunkNumber = meshDataView.GetNumChunks();
		Uint32 boneCount = 0;
		red::DynArray<Uint8> skinningDataArr = { red::PoolBackend() };
		red::DynArray<Float> skinningWeightArr = { red::PoolBackend() };

		for( Uint32 i = 0; i < chunkNumber; ++i )
		{
			// First, extract the vertices and create skinning streams that we will fill later
			auto positionData = meshDataView.GetChunkStreamData( i, MST_Position_3F );

			skinningDataArr.Clear();
			skinningDataArr.Resize( positionData.m_count * 4 );

			skinningWeightArr.Clear();
			skinningWeightArr.Resize( positionData.m_count * 4 );

			mesh::ChunkStreamDataRO skinningData = mesh::ChunkStreamDataRO();
			skinningData.m_stream = MST_SkinningIndices_4U8;
			skinningData.m_stride = MeshPackerUtils::GetMeshStreamSize( MST_SkinningIndices_4U8 );

			mesh::ChunkStreamDataRO skinningWeight = mesh::ChunkStreamDataRO();
			skinningWeight.m_stream = MST_SkinningWeights_4F;
			skinningWeight.m_stride = MeshPackerUtils::GetMeshStreamSize( MST_SkinningWeights_4F );

			// We go through each vertex and look for at most 4 actors it can get assigned to
			// Not really the most efficient approach, but this is supposed to be editor only and not runtime
			Float* positions = (Float*)positionData.m_data;
			for( Uint32 j = 0; j < positionData.m_count; ++j )
			{
				Vector4 pos = Vector4( positions[ 3 * j ], positions[ 3 * j + 1 ], positions[ 3 * j + 2 ] );
				pos = localToWorld.TransformPoint( pos );
				// Found value tells us how many actors (and subsequently, bones) we, well, found for that vertex to influence it
				Uint32 found = 0;
				for( Uint32 a = 0; a < actorCnt && ( found < 4 ); ++a )
				{
					auto actor = actors[ a ];

					auto& shapes = actor->GetActorShapes();
					for( auto shape : shapes )
					{
						// For each shape we perform a test if the vertex in inside it's geometry
						auto geom = shape->GetShapeGeometry();
						if( PhysicsBackendHelper::CheckPointInGeometry( geom, pos, shape->GetLocalToWorld().GetTranslation() ) )
						{
							// If it is, first we check if the actor owning a shape is already assigned a bone index, if not, we generate a new one
							if( actorBinds[ a ] < 0 )
							{
								actorBinds[ a ] = boneCount++;
							}

							// Now, if we already had an actor found for that vertex, calculate the appropriate weight of each bone.
							// TODO: It's an average of number of bones influencing the vertex atm, would be nice to have a way to control that.
							Float weight = 1.0f / ( (Float)found + 1.0f );
							for( Uint32 b = 0; b < 4; ++b )
							{
								// Add bone indice to skinning data array and the weight of it.
								if( b == found )
									skinningDataArr[ 4 * j + b ] = actorBinds[ a ];
								else if( b > found )
									skinningDataArr[ 4 * j + b ] = 0;

								if( b <= found )
									skinningWeightArr[ 4 * j + b ] = weight;
								else
									skinningWeightArr[ 4 * j + b ] = 0.0f;
							}

							// Increase the amount of found bones.
							++found;
						}
					}
				}
			}

			// Assign data to streams and set them on the mesh chunk
			skinningData.m_data = skinningDataArr.TypedData();
			skinningData.m_count = positionData.m_count;
			skinningWeight.m_data = skinningWeightArr.TypedData();
			skinningWeight.m_count = positionData.m_count;

			meshDataView.SetChunkData( i, skinningData );
			meshDataView.SetChunkData( i, skinningWeight );
		}

		meshDataView.Apply();

		// Now that we have applied streams with skinning data to the mesh, create a builder to add bones based on 
		// streams we created. We have to clear the bindings of editor mesh as well, because it's not longer gonna use the root actor, but the actors
		// used for skinning generation
		mesh::DataBuilder builder( meshDataView );
		mesh->ClearBindings();

		builder.m_bones.Resize( boneCount );
		for( Uint32 a = 0; a < actorCnt; ++a )
		{
			// If the actorBinds for this actor is negative, no bones was created for it
			if( actorBinds[ a ] < 0 ) continue;

			auto actor = actors[ a ];
			auto& shapes = actor->GetActorShapes();

			Matrix actorL2W = actor->GetLocalToWorld();
			Matrix meshL2WInverted = localToWorld.OrthonormInverted();
			Matrix rigMat = (actorL2W * meshL2WInverted );
			rigMat.OrthonormInvert();

			mesh::DataBuilder::UnpackedBone boneInfo;
			AnsiChar name[ 10 ];
			red::SNPrintF( name, RED_ARRAY_COUNT( name ), "Bone%d", (Uint64)actorBinds[ a ] );

			CName boneName = RED_NAME( name );

			// Bind an actor to editor mesh
			mesh->BindPhysicsActor( actor->GetPhysicsObjectID(), rigMat, Matrix::IDENTITY_CONSTANT, boneName, actorBinds[ a ] );

			boneInfo.m_name = boneName;
			boneInfo.m_rig.Set( rigMat );
			builder.m_bones[ actorBinds[ a ] ] = boneInfo;
		}
		builder.Flush( *meshHandle );
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that checks if a point is inside of provided geometry.
	// TODO: Does not support TriMesh at the moment.
	//////////////////////////////////////////////////////////////////////////
	Bool PhysicsBackendHelper::CheckPointInGeometry( THandle< ShapeGeometryBase > geom, const Vector4& posToCheck, const Vector4& positionOfShape )
	{
		if( geom->m_shapeType == EPST_Box )
		{
			ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)geom.Get();

			Vector4 right = Vector3::EX();
			Vector4 forward = Vector3::EZ();
			Vector4 up = Vector3::EY();

			right *= boxGeom->m_halfExtents.X;
			forward *= boxGeom->m_halfExtents.Z;
			up *= boxGeom->m_halfExtents.Y;


			Vector4 minV = positionOfShape - ( right + forward + up );
			Vector4 maxV = positionOfShape + ( right + forward + up );

			Bool xCheck = ( posToCheck.X >= minV.X ) && ( posToCheck.X <= maxV.X );
			Bool yCheck = ( posToCheck.Y >= minV.Y ) && ( posToCheck.Y <= maxV.Y );
			Bool zCheck = ( posToCheck.Z >= minV.Z ) && ( posToCheck.Z <= maxV.Z );

			if( xCheck && yCheck && zCheck )
			{
				return true;
			}
		}
		else if( geom->m_shapeType == EPST_Sphere )
		{
			ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)geom.Get();

			Sphere sphere = Sphere( positionOfShape, sphereGeom->m_radius );

			if( sphere.Contains( posToCheck ) )
			{
				return true;
			}
		}
		else if( geom->m_shapeType == EPST_Capsule )
		{
			ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)geom.Get();

			Vector4 pos = positionOfShape;
			pos.Z -= ( capsuleGeom->m_height / 2.0f ); // Engine capsule position is at it's base, physics in the middle
			FixedCapsule capsule = FixedCapsule( pos, capsuleGeom->m_radius, capsuleGeom->m_height );

			if( capsule.Contains( posToCheck ) )
			{
				return true;
			}
		}
		else if( geom->m_shapeType == EPST_Convex )
		{
			ShapeGeometryConvex* convexGeom = (ShapeGeometryConvex*)geom.Get();

			red::ConvexBuilder builder;
			builder.Build(convexGeom->m_vertexPositions.TypedData(), convexGeom->m_vertexCount );
			red::ConvexHullEx convex;
			Bool returnVal = builder.ExtractEx( convex );
			if( returnVal )
			{
				if( convex.Contains( posToCheck ) )
				{
					return true;
				}
			}
		}
		else if( geom->m_shapeType == EPST_TriMesh )
		{
			return false;
		}

		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that converts editor data into physics::SystemResource
	// If PhysicsMeshPtr is provided, it will only output physics::SystemResource for the 
	// CMesh it contains
	// TODO: fix the inconsistencies between editor actor and shape/geometry
	// and their system body and collider equivalents, fix materials
	//////////////////////////////////////////////////////////////////////////
	THandle< physics::SystemResource > PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( red::DynArray< PhysicsBasePtr >& basePtrs, TSystemObjectsMap& actorMap, PhysicsMeshPtr mesh )
	{
		if (mesh == nullptr)
			return nullptr;
		const Matrix& modelL2W =  mesh->GetLocalToWorld();

		THandle< physics::SystemResource > psResource = CreateHandle< physics::SystemResource >();

		const red::DynArray< SPhysicsActorBoneMapping >& actorBindings = mesh->GetActorBindings();

		for( const auto obj : basePtrs )
		{
			Uint8 baseType = obj->GetBaseType();

			if( baseType == EPBT_Actor )
			{
				PhysicsActorPtr actorPtr = red::StaticCast< PhysicsActor >( obj );
				PhysicsBaseID actorId = actorPtr->GetPhysicsObjectID();

				if( actorMap.KeyExist( actorId ) )
				{
					// Editor physics actor already mapped to physics system actor
					continue;
				}

				// Create basic actor description
				physics::SystemBodyParams params;

				params.m_angularDamping = actorPtr->GetAngularDamping();
				params.m_linearDamping = actorPtr->GetLinearDamping();
				params.m_simulationType = (SimulationType)actorPtr->GetSimulationType();
				params.m_solverIterationsCountPosition = actorPtr->GetSolverIterationsCountPosition();
				params.m_solverIterationsCountVelocity = actorPtr->GetSolverIterationsCountVelocity();
				params.m_comOffset = actorPtr->GetCOMOffset();
				params.m_inertia = actorPtr->GetInertia();
				params.m_mass = actorPtr->GetMass();
				params.m_volume = actorPtr->GetVolume();
				params.m_maxDepenetrationVelocity = actorPtr->m_maxDepenetrationVelocity;
				params.m_maxAngularVelocity = actorPtr->m_maxAngularVelocity;
				params.m_maxContactImpulse = actorPtr->m_maxContactImpulse;

				physics::SystemBodyBuildData sysBodyBD = physics::SystemBodyBuildData();

				sysBodyBD.m_name = RED_NAME( actorPtr->GetName() );
				Matrix actorL2W = actorPtr->GetLocalToWorld();
				Matrix actorL2M = actorL2W * modelL2W.FullInverted();

				Bool foundBinding = false;
				for (auto actorBinding : actorBindings)
				{
					if (actorBinding.m_actorId == actorPtr->GetPhysicsObjectID())
					{
						sysBodyBD.m_mappedBoneName = actorBinding.m_boneName == physics::PhysicsMesh::s_unskinnedFakeBoneDEPRECATED ? CName::NONE() : actorBinding.m_boneName;
						sysBodyBD.m_localToModel.Set( actorL2M.GetTranslation(), actorL2M.ToQuat());

						Matrix newBoneToBody = actorBinding.m_originalL2MInverted * actorL2M;
						newBoneToBody = actorBinding.m_boneToBody * newBoneToBody;
						sysBodyBD.m_mappedBoneToBody = Transform(newBoneToBody.GetTranslation(), newBoneToBody.ToQuat());
						foundBinding = true;
						break;
					}
				}
				if (!foundBinding)
				{
					continue;
				}				

				sysBodyBD.m_params = params;

				Vector4 scale = actorL2W.GetPreScale33();
				// Extract and convert shapes of an actor
				auto shapes = actorPtr->GetActorShapes();
				for( auto shape : shapes )
				{
					THandle< physics::ICollider > psShape;

					if( psShape = GetColliderFromEditorGeometry( shape ) )
					{
						Matrix local2Body = shape->GetLocalTransform();
						Vector4 shapeScale = local2Body.GetPreScale33();
						psShape->m_localToBody = { local2Body.GetTranslation(), local2Body.ToQuat() };
						psShape->m_isImported = shape->IsImported();
						psShape->m_isQueryShapeOnly = shape->IsQueryShapeOnly();
						psShape->m_material = shape->m_material;
						psShape->m_filterData = shape->m_filterData;
						psShape->m_materialApperanceOverrides = shape->m_materialApperanceOverrides;
						psShape->m_volumeModifier = shape->m_volumeModifier;
						psShape->SetObstacle(shape->m_isObstacle);
						sysBodyBD.m_collisionShapes.PushBack( psShape );
					}
				}

				if( sysBodyBD.m_collisionShapes.Empty() )
					continue;

				THandle< physics::SystemBody > actorPS = physics::SystemBody::Create( sysBodyBD );
				if( actorPS )
				{
					actorPS->m_isQueryBodyOnly = actorPtr->IsQueryOnlyActor();
					actorMap.Insert( actorId, actorPS );
					psResource->AttachObject( actorPS );
				}
			}
		}
		return psResource;
	}

	void PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( red::DynArray< PhysicsBasePtr >& basePtrs, TSystemObjectsMap& actorMap, THandle< physics::SystemResource > psResource, PhysicsMeshPtr mesh /*= nullptr */ )
	{
		Bool isMeshSpecified = mesh != nullptr;
		const Matrix& modelL2W = isMeshSpecified ? mesh->GetLocalToWorld() : Matrix::IDENTITY();
		const red::DynArray< SPhysicsActorBoneMapping >& actorBindings = isMeshSpecified ? mesh->GetActorBindings() : red::DynArray< SPhysicsActorBoneMapping >();

		// Process joints after we are done with actors, so we have the binding collection ready to make connections, skip if actorMap is empty
		Uint32 actorMapSize = actorMap.Size();
		if( actorMapSize > 0 )
		{
			for( auto obj : basePtrs )
			{
				Uint8 baseType = obj->GetBaseType();

				if( baseType == EPBT_Joint )
				{
					PhysicsJointPtr jointPtr = red::StaticCast< PhysicsJoint >( obj );

					THandle< physics::SystemJoint > sysJoint = CreateHandle< physics::SystemJoint  >();

					// Setup base joint props
					sysJoint->SetBreakingForce( jointPtr->GetBreakingForce() );
					sysJoint->SetBreakingTorque( jointPtr->GetBreakingTorque() );
					sysJoint->SetIsBreakable( jointPtr->IsBreakable() );
					sysJoint->SetProjectionAngularTolerance( jointPtr->GetProjectionAngularTolerance() );
					sysJoint->SetProjectionLinearTolerance( jointPtr->GetProjectionLinearTolerance() );
					sysJoint->SetProjectionToleranceEnabled( jointPtr->IsProjectionToleranceEnabled() );
					
					// Setup limits & drives
					SetupPhysSystemJointLimits( jointPtr, sysJoint );
					SetupPhysicsSystemJointDrives( jointPtr, sysJoint );

					// Setup actor connections
					Matrix jointL2W = jointPtr->GetLocalToWorld();

					physics::PhysicsBaseID parentId = jointPtr->GetActorId( EPJAI_Parent );
					physics::PhysicsBaseID childId = jointPtr->GetActorId( EPJAI_Child );

					if( childId == 0)
					{
						childId = parentId;
						parentId = 0;
					}
					if( ( !actorMap.KeyExist( parentId ) && parentId != 0 ) || ( !actorMap.KeyExist( childId ) && childId != 0 ) )
					{
						RED_LOG_WARNING( "PhysicsBackendHelper: Invalid editor joint - actors for joint were not found in actor map. Won't be added to physics system resource." );
						continue;
					}

					if( isMeshSpecified )
					{
						// Search for parent actor binding in the mesh
						physics::PhysicsBaseID idToCheck = parentId;
						if( parentId == 0 )
						{
							idToCheck = childId;
						}
						auto iter = std::find_if( actorBindings.Begin(), actorBindings.End(), [idToCheck] ( const SPhysicsActorBoneMapping& actorBinding )
						{
							return actorBinding.m_actorId == idToCheck;
						} );
						// If we dont find any matching parent binding, this means the joint is not to be added to this physics system resource (it might attach actor from it though, as a child)
						if( iter == actorBindings.End() )
						{
							continue;
						}
					}

					if( actorMap[ parentId ] == nullptr && actorMap[ childId ] == nullptr )
					{
						RED_LOG_WARNING( "PhysicsBackendHelper: Invalid editor joint - actors for joint were found in map but are nullptr. Won't be added to physics system resource." );
						continue;
					}

					// setup system joint transform and pins
					sysJoint->SetupWorld( jointL2W.GetTranslation(), jointL2W.ToQuat(), actorMap[parentId], actorMap[childId], Matrix::IDENTITY(), Matrix::IDENTITY() );

					if( sysJoint->IsValid() )
					{
						psResource->AttachObject( sysJoint );
					}
					else
					{
						RED_LOG_WARNING( "PhysicsBackendHelper: Invalid editor joint - ~IsValid(). Won't be added to physics system resource." );
					}
				}
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that converts physics::SystemResource into editor data
	// TODO: fix the inconsistencies between editor actor and shape/geometry
	// and their system body and collider equivalents, fix materials
	//////////////////////////////////////////////////////////////////////////
	red::DynArray< physics::PhysicsBasePtr > PhysicsBackendHelper::ConvertFromPhysicsSystemToEditor(THandle< physics::SystemResource > physicsSystem, physics::PhysicsEditorScene* scene, THandle<physics::PhysicsMesh> meshPtr)
	{
		red::DynArray< PhysicsBasePtr > physicsObjects = red::DynArray< PhysicsBasePtr >();
		if( meshPtr == nullptr )
			return physicsObjects;

		const Matrix& modelL2W = meshPtr->GetLocalToWorld();
		const auto& bodies = physicsSystem->GetBodies();
		const auto& joints = physicsSystem->GetJoints();

		red::HashMap< Uint32, physics::PhysicsBaseID > actorMapping;
		Uint32 boneCount = 0;
		for( auto& sysBody : bodies )
		{
			auto actorHandle = CreateHandle<physics::PhysicsActor>(scene->GetNextFreeId(), scene);
			actorHandle->SetSimulationType((physics::EPhysicsSimulation)sysBody->m_params.m_simulationType);

			actorHandle->SetLinearDamping(sysBody->m_params.m_linearDamping);
			actorHandle->SetAngularDamping(sysBody->m_params.m_angularDamping);
			actorHandle->SetSolverIterationsCountPosition(sysBody->m_params.m_solverIterationsCountPosition);
			actorHandle->SetsolverIterationsCountVelocity(sysBody->m_params.m_solverIterationsCountVelocity);
			actorHandle->SetIsQueryOnlyActor(sysBody->m_isQueryBodyOnly);
			actorHandle->m_maxDepenetrationVelocity = sysBody->m_params.m_maxDepenetrationVelocity;
			actorHandle->m_maxAngularVelocity = sysBody->m_params.m_maxAngularVelocity;
			actorHandle->m_maxContactImpulse = sysBody->m_params.m_maxContactImpulse;

			Matrix bodyToModel = sysBody->m_localToModel.ToMatrix();
			Matrix boneToBody = sysBody->m_mappedBoneToBody.ToMatrix();

			CName boneName = sysBody->m_mappedBoneName == physics::PhysicsMesh::s_unskinnedFakeBoneDEPRECATED ? CName::NONE() : sysBody->m_mappedBoneName;
			if( meshPtr )
			{
				Int32 boneIndex = meshPtr->GetBoneIndex( boneName );
				meshPtr->BindPhysicsActor(actorHandle->GetPhysicsObjectID(), bodyToModel.FullInverted(), boneToBody, boneName, boneIndex);
				actorHandle->SetBoneMap({ meshPtr->GetPhysicsObjectID(), boneName, boneIndex });
			}

			for( auto collider : sysBody->m_collisionShapes )
			{
				auto shapePtr = CreateHandle<physics::PhysicsShape>();
				shapePtr->SetShapeGeometry(GetEditorGeometryFromCollider(collider));
				shapePtr->SetLocalToWorld(collider->m_localToBody.ToMatrix());
				shapePtr->SetIsImported(collider->m_isImported);
				shapePtr->SetIsQueryShapeOnly(collider->m_isQueryShapeOnly);
				shapePtr->m_material = collider->m_material;
				shapePtr->m_filterData = collider->m_filterData;
				shapePtr->m_materialApperanceOverrides = collider->m_materialApperanceOverrides;
				shapePtr->m_volumeModifier = collider->m_volumeModifier;
				shapePtr->m_isObstacle = collider->IsObstacle();
				actorHandle->AddShape(shapePtr);
			}

			Matrix actorL2W = bodyToModel * modelL2W;
			actorHandle->SetLocalToWorld(actorL2W);

			THandle< physics::EditorActorCOM > com = actorHandle->GetCOM();
			com->m_comOffset = sysBody->m_params.m_comOffset;
			com->m_inertia = sysBody->m_params.m_inertia;
			com->m_volume = sysBody->m_params.m_volume;
			com->m_mass = sysBody->m_params.m_mass;
			physicsObjects.PushBack(actorHandle);
			actorMapping.Insert(sysBody->GetID().CalcHash(), actorHandle->GetPhysicsObjectID());

		}

		// Setup joints after we have actors
		for( auto& sysJoint : joints )
		{
			auto jointHandle = CreateHandle<physics::PhysicsJoint>(scene->GetNextFreeId(), scene);

			// Setup base joint props
			jointHandle->SetBreakingForce(sysJoint->GetBreakingForce());
			jointHandle->SetBreakingTorque(sysJoint->GetBreakingTorque());
			jointHandle->SetIsBreakable(sysJoint->IsBreakable());
			jointHandle->SetProjectionAngularTolerance(sysJoint->GetProjectionAngularTolerance());
			jointHandle->SetProjectionLinearTolerance(sysJoint->GetProjectionLinearTolerance());
			jointHandle->SetProjectionToleranceEnabled(sysJoint->IsProjectionToleranceEnabled());

			// Setup limits & drives
			SetupPhysEditorJointLimits(jointHandle, sysJoint);
			SetupEditorJointDrives(jointHandle, sysJoint);

			// Setup actors
			auto pinA = sysJoint->GetPinA();
			auto pinB = sysJoint->GetPinB();
			if( pinA == nullptr || pinB == nullptr )
			{
				if( pinA )
				{
					auto actorA = pinA->GetObject();
					jointHandle->SetActorId(actorMapping[ actorA->GetID().CalcHash() ], physics::EPJAI_Child);
				}
				if( pinB )
				{
					auto actorA = pinB->GetObject();
					jointHandle->SetActorId(actorMapping[ actorA->GetID().CalcHash() ], physics::EPJAI_Child);
				}
			}
			else
			{
				auto actorA = pinA->GetObject();
				jointHandle->SetActorId(actorMapping[ actorA->GetID().CalcHash() ], physics::EPJAI_Parent);
				auto actorB = pinB->GetObject();
				jointHandle->SetActorId(actorMapping[ actorB->GetID().CalcHash() ], physics::EPJAI_Child);
			}

			jointHandle->SetLocalToWorld(sysJoint->GetLocalToWorld() * modelL2W);

			physicsObjects.PushBack(jointHandle);
		}

		return physicsObjects;
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that generates actor structure
	//////////////////////////////////////////////////////////////////////////
	red::DynArray< physics::PhysicsBasePtr > PhysicsBackendHelper::GenerateEditorActorsForMesh( const Matrix& meshL2W, physics::PhysicsEditorScene* scene, THandle<physics::PhysicsMesh> meshPtr /*= nullptr */ )
	{
		THandle< CMesh > meshHandle = meshPtr->GetMesh();

		red::DynArray< PhysicsBasePtr > physicsObjects;

		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *meshHandle );
		Uint32 boneCount = meshDataViewRO.GetNumBones();

		if( meshHandle )
		{
			if( boneCount > 0 )
			{
				// skinned
				for( Uint32 i = 0; i < boneCount; ++i )
				{
					auto actorHandle = CreateHandle<physics::PhysicsActor>( scene->GetNextFreeId(), scene );
					actorHandle->SetSimulationType( physics::EPhysicsSimulation::EPS_Kinematic );
					const Matrix& boneRig = meshDataViewRO.GetBoneMatrix( i );
					const Matrix& boneRigInv = boneRig.FullInverted();
					Matrix actorL2W = boneRigInv * meshL2W;
					CName boneName = meshDataViewRO.GetBoneName( i );
					actorHandle->SetLocalToWorld( actorL2W );
					BindPhysicsActorToMesh( actorHandle, meshPtr, boneName, boneRigInv.ToXform() );
					physicsObjects.PushBack( actorHandle );
				}
			}
			else
			{
				auto actorHandle = CreateHandle<physics::PhysicsActor>( scene->GetNextFreeId(), scene );
				actorHandle->SetSimulationType( physics::EPhysicsSimulation::EPS_Kinematic );
				actorHandle->SetLocalToWorld( meshL2W );
				BindPhysicsActorToMesh( actorHandle, meshPtr, CName::NONE() );
				physicsObjects.PushBack( actorHandle );
			}
		}		
		return physicsObjects;
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that computes center of mass and inertia tensor of an actor
	// as well as it's bounds
	//////////////////////////////////////////////////////////////////////////
	Bool PhysicsBackendHelper::ComputeCOMForEditorActor( PhysicsActorPtr& actorPtr )
	{
		// Check if actor contains tri mesh shapes and compute it's bounds
		Bool hasTriMesh = false;
		const auto& actorShapes = actorPtr->GetActorShapes();
		Box bounds = Box::EMPTY();
		for( auto shape : actorShapes )
		{
			if( shape->GetShapeGeometry()->m_shapeType == EPST_TriMesh )
			{
				hasTriMesh = true;
			}
			Box shapeBounds = shape->GetBoundsLocal();
			bounds.AddBox( shapeBounds );
		}
		
		auto actorCom = actorPtr->GetCOM();
		actorCom->m_mass = 0.f;

		// We do not compute COM for actors with tri meshes, so we end after computing bounds
		if( hasTriMesh )
		{
			actorCom->m_mass = RED_FLT_MAX;
			return false;
		}

		// If the bounds are empty, that means the actor either have no shapes or the shapes have no volume, we can't continue
		if( bounds.IsEmpty() )
		{
			RED_LOG_ERROR( "PhysicsActor: Shapes of actor '%hs' have no volume. It's COM will not be computed.", actorPtr->GetName().AsChar() );
			return false;
		}

		red::DynArray< THandle< physics::ICollider >> colliders{red::PoolBackend()};
		colliders.Reserve( actorShapes.Size() );
		for( auto shape : actorShapes )
		{
			THandle< physics::ICollider > collider = PhysicsBackendHelper::GetColliderFromEditorGeometry( shape );
			if( collider )
			{
				collider->m_localToBody = shape->GetLocalTransform().ToXform();
				collider->m_material = shape->m_material;
				collider->m_filterData = shape->m_filterData;
				collider->m_materialApperanceOverrides = shape->m_materialApperanceOverrides;
				collider->m_volumeModifier = shape->m_volumeModifier;
				colliders.PushBack( collider );
			}
		}
		ComputeCOM( colliders, actorCom->m_comOffset, actorCom->m_volume, actorCom->m_mass, actorCom->m_inertia, actorPtr->GetLocalToWorld() );
		actorCom->CalculateLocalToWorld();
		return true;
	}

	void PhysicsBackendHelper::BindPhysicsActorToMesh( PhysicsActorPtr& actorPtr, PhysicsMeshPtr& meshPtr, CName boneName, const Transform& actorL2M )
	{
		THandle< CMesh > meshHandle = meshPtr->GetMesh();
		Int32 boneIndex = meshPtr->GetBoneIndex( boneName );
		if( boneIndex != -1 )
		{
			Transform boneRig = meshPtr->GetBoneRigMatrix( boneIndex ).ToXform();
			Transform boneToBody = actorL2M * boneRig;

			meshPtr->BindPhysicsActor( actorPtr->GetPhysicsObjectID(), actorL2M.ToInvMatrix(), boneToBody.ToMatrix(), boneName, boneIndex );
			actorPtr->SetBoneMap( { meshPtr->GetPhysicsObjectID(), boneName, boneIndex } );
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that converts physics::ICollider into editor geometry 
	// TODO: Investigate scale issues
	//////////////////////////////////////////////////////////////////////////
	physics::ShapeGeometryPtr PhysicsBackendHelper::GetEditorGeometryFromCollider( THandle< physics::ICollider> collider )
	{
		if( const auto box = Cast<physics::ColliderBox>( collider ) )
		{
			return CreateHandle<physics::ShapeGeometryBox>(box->GetHalfExtents());
		}
		else if( const auto sphere = Cast<physics::ColliderSphere>( collider ) )
		{
			return CreateHandle<physics::ShapeGeometrySphere>( sphere->GetRadius() );
		}
		else if( const auto capsule = Cast<physics::ColliderCapsule>( collider ) )
		{
			return CreateHandle<physics::ShapeGeometryCapsule>( capsule->GetRadius(), capsule->GetHeight() );
		}
		else if( const auto convex = Cast<physics::ColliderConvex>( collider ) )
		{
			auto convexHandle = CreateHandle<physics::ShapeGeometryConvex>( );
			convexHandle->m_vertexPositions = convex->m_vertices;
			convex->GetConvexTriangleIndices( convexHandle->m_indices );
			convexHandle->m_vertexCount = convex->m_vertices.Size();
			return convexHandle;
		}
		else if( const auto triMesh = Cast<physics::ColliderMesh>( collider ) )
		{
			const Uint32 nbVerts = triMesh->GetNumPoints();
			const Uint32 nbIndices = triMesh->GetNumIndices();

			red::DynArray< Uint16 >	indices = { red::PoolBackend() };
			indices.Resize( nbIndices );
			red::Memcpy( &indices[ 0 ], triMesh->GetIndices(), nbIndices * sizeof( Uint16 ) );

			red::DynArray< Vector3 > vertices = { red::PoolBackend() };
			vertices.Resize( nbVerts );
			red::Memcpy( &vertices[ 0 ], triMesh->GetPoints(), nbVerts * sizeof( Vector3 ) );

			red::DynArray< CName >	physicsMaterials = { red::PoolBackend() };
			red::DynArray< Uint16 >	physicsMaterialIndices = { red::PoolBackend() };

			physicsMaterialIndices = triMesh->m_faceMaterialIndices;
			physicsMaterials = triMesh->m_faceMaterials;
			return CreateHandle<ShapeGeometryTriMesh>( std::move( vertices ), std::move( indices ), std::move( physicsMaterialIndices ), std::move( physicsMaterials ) );
		}
		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////
	// Function that converts editor geometry into physics::ICollider
	//////////////////////////////////////////////////////////////////////////
	THandle<physics::ICollider> PhysicsBackendHelper::GetColliderFromEditorGeometry( PhysicsShapePtr shape )
	{
		auto geom = shape->GetShapeGeometry();
		if( geom->m_shapeType == EPST_Box )
		{
			ShapeGeometryBox* boxGeom = (ShapeGeometryBox*)geom.Get();

			return physics::ColliderBox::Create( boxGeom->m_halfExtents );
		}
		else if( geom->m_shapeType == EPST_Sphere )
		{
			ShapeGeometrySphere* sphereGeom = (ShapeGeometrySphere*)geom.Get();

			return physics::ColliderSphere::Create( sphereGeom->m_radius );
		}
		else if( geom->m_shapeType == EPST_Capsule )
		{
			ShapeGeometryCapsule* capsuleGeom = (ShapeGeometryCapsule*)geom.Get();

			return physics::ColliderCapsule::Create( capsuleGeom->m_radius, capsuleGeom->m_height );
		}
		else if( geom->m_shapeType == EPST_Convex )
		{
			ShapeGeometryConvex* convexGeom = (ShapeGeometryConvex*)geom.Get();

			return physics::ColliderConvex::Create( convexGeom->m_vertexPositions.TypedData(), convexGeom->m_vertexCount, convexGeom->m_vertexCount, !shape->IsImported() );
		}
		else if( geom->m_shapeType == EPST_TriMesh )
		{
			ShapeGeometryTriMesh* trimeshGeom = (ShapeGeometryTriMesh*)geom.Get();

			THandle<physics::ColliderMesh > trimesh = physics::ColliderMesh::Create(
				trimeshGeom->m_vertices.TypedData(), sizeof( Vector3 ), trimeshGeom->m_vertices.Size(),
				trimeshGeom->m_indices.TypedData(), trimeshGeom->m_indices.Size(), false );
			trimesh->m_faceMaterialIndices = trimeshGeom->m_physicalMaterialIndices;
			trimesh->m_faceMaterials = trimeshGeom->m_physicalMaterials;
			return trimesh;
		}

		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////
	// Helper function that setups SystemJoint limits from editor joint ones
	// TODO: create helper functions for enum converstions, plain cast kinda risky
	//////////////////////////////////////////////////////////////////////////
	void PhysicsBackendHelper::SetupPhysSystemJointLimits( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint )
	{
		// Setup linear limit
		physics::PhysicsJointLinearLimit linearLimit;
		const auto& editorLinearLimit = jointPtr->GetLinearLimit();

		linearLimit.m_bounceThreshold = editorLinearLimit.m_bounceThreshold;
		linearLimit.m_restitution = editorLinearLimit.m_restitution;
		linearLimit.m_stiffness = editorLinearLimit.m_stiffness;
		linearLimit.m_damping = editorLinearLimit.m_damping;
		linearLimit.m_contactDistance = editorLinearLimit.m_contactDistance;

		linearLimit.m_value = editorLinearLimit.m_value;
		linearLimit.m_x = ( physics::PhysicsJointMotion )editorLinearLimit.m_x;
		linearLimit.m_y = ( physics::PhysicsJointMotion )editorLinearLimit.m_y;
		linearLimit.m_z = ( physics::PhysicsJointMotion )editorLinearLimit.m_z;

		sysJoint->SetLinearLimit( linearLimit );

		// Setup twist limit
		physics::PhysicsJointAngularLimitPair twistLimit;
		const auto& editorTwistLimit = jointPtr->GetTwistLimit();

		twistLimit.m_bounceThreshold = editorTwistLimit.m_bounceThreshold;
		twistLimit.m_restitution = editorTwistLimit.m_restitution;
		twistLimit.m_stiffness = editorTwistLimit.m_stiffness;
		twistLimit.m_damping = editorTwistLimit.m_damping;
		twistLimit.m_contactDistance = editorTwistLimit.m_contactDistance;

		twistLimit.m_lower = editorTwistLimit.m_lower;
		twistLimit.m_upper = editorTwistLimit.m_upper;
		twistLimit.m_twist = ( physics::PhysicsJointMotion )editorTwistLimit.m_twist;

		sysJoint->SetTwistLimit( twistLimit );

		// Setup swing limit
		physics::PhysicsJointLimitConePair swingLimit;
		const auto& editorSwingLimit = jointPtr->GetSwingLimit();

		swingLimit.m_bounceThreshold = editorSwingLimit.m_bounceThreshold;
		swingLimit.m_restitution = editorSwingLimit.m_restitution;
		swingLimit.m_stiffness = editorSwingLimit.m_stiffness;
		swingLimit.m_damping = editorSwingLimit.m_damping;
		swingLimit.m_contactDistance = editorSwingLimit.m_contactDistance;

		swingLimit.m_yAngle = std::max( editorSwingLimit.m_yAngle, physics::PhysicsJointLimitConePair::c_minSwingAngle );
		swingLimit.m_zAngle = std::max( editorSwingLimit.m_zAngle, physics::PhysicsJointLimitConePair::c_minSwingAngle );
		swingLimit.m_swingY = ( physics::PhysicsJointMotion )editorSwingLimit.m_swingY;
		swingLimit.m_swingZ = ( physics::PhysicsJointMotion )editorSwingLimit.m_swingZ;

		sysJoint->SetSwingLimit( swingLimit );
	}

	//////////////////////////////////////////////////////////////////////////
	// Helper function that setups editor joint limits from SystemJoint ones
	// TODO: create helper functions for enum converstions, plain cast kinda risky
	//////////////////////////////////////////////////////////////////////////
	void PhysicsBackendHelper::SetupPhysEditorJointLimits( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint )
	{
		// Setup linear limit
		physics::SPhysicsJointLinearLimit linearLimit;
		const auto& editorLinearLimit = sysJoint->GetLinearLimit();

		linearLimit.m_bounceThreshold = editorLinearLimit.m_bounceThreshold;
		linearLimit.m_restitution = editorLinearLimit.m_restitution;
		linearLimit.m_stiffness = editorLinearLimit.m_stiffness;
		linearLimit.m_damping = editorLinearLimit.m_damping;
		linearLimit.m_contactDistance = editorLinearLimit.m_contactDistance;

		linearLimit.m_value = editorLinearLimit.m_value;
		linearLimit.m_x = ( physics::EPhysicsJointMotion )editorLinearLimit.m_x;
		linearLimit.m_y = ( physics::EPhysicsJointMotion )editorLinearLimit.m_y;
		linearLimit.m_z = ( physics::EPhysicsJointMotion )editorLinearLimit.m_z;

		jointPtr->SetLinearLimit( linearLimit );

		// Setup twist limit
		physics::SPhysicsJointAngularLimitPair twistLimit;
		const auto& editorTwistLimit = sysJoint->GetTwistLimit();

		twistLimit.m_bounceThreshold = editorTwistLimit.m_bounceThreshold;
		twistLimit.m_restitution = editorTwistLimit.m_restitution;
		twistLimit.m_stiffness = editorTwistLimit.m_stiffness;
		twistLimit.m_damping = editorTwistLimit.m_damping;
		twistLimit.m_contactDistance = editorTwistLimit.m_contactDistance;

		twistLimit.m_lower = editorTwistLimit.m_lower;
		twistLimit.m_upper = editorTwistLimit.m_upper;
		twistLimit.m_twist = ( physics::EPhysicsJointMotion )editorTwistLimit.m_twist;

		jointPtr->SetTwistLimit( twistLimit );

		// Setup swing limit
		physics::SPhysicsJointLimitConePair swingLimit;
		const auto& editorSwingLimit = sysJoint->GetSwingLimit();

		swingLimit.m_bounceThreshold = editorSwingLimit.m_bounceThreshold;
		swingLimit.m_restitution = editorSwingLimit.m_restitution;
		swingLimit.m_stiffness = editorSwingLimit.m_stiffness;
		swingLimit.m_damping = editorSwingLimit.m_damping;
		swingLimit.m_contactDistance = editorSwingLimit.m_contactDistance;

		swingLimit.m_yAngle = editorSwingLimit.m_yAngle;
		swingLimit.m_zAngle = editorSwingLimit.m_zAngle;
		swingLimit.m_swingY = ( physics::EPhysicsJointMotion )editorSwingLimit.m_swingY;
		swingLimit.m_swingZ = ( physics::EPhysicsJointMotion )editorSwingLimit.m_swingZ;

		jointPtr->SetSwingLimit( swingLimit );
	}

	//////////////////////////////////////////////////////////////////////////
	// Helper function that setups physics system joint drives from editor ones
	//////////////////////////////////////////////////////////////////////////
	void PhysicsBackendHelper::SetupPhysicsSystemJointDrives( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint )
	{
		for( Uint32 i = 0; i < EPhysicsJointDrive::EPJD_Count; ++i )
		{
			PhysicsJointDrive sysDrive;
			const SPhysicsJointDrive& drive = jointPtr->GetDrive( (EPhysicsJointDrive)i );
			sysDrive.m_damping = drive.m_damping;
			sysDrive.m_forceLimit = drive.m_forceLimit;
			sysDrive.m_stiffness = drive.m_stiffness;
			sysDrive.m_isAcceleration = drive.m_isAcceleration;
			sysJoint->SetDrive( (PhysicsJointDriveType)i, sysDrive );
		}
		sysJoint->SetDrivePosition( jointPtr->GetDrivePosition() );
		sysJoint->SetDriveLinearVelocity( jointPtr->GetDriveLinearVelocity() );
		sysJoint->SetDriveAngularVelocity( jointPtr->GetDriveAngularVelocity() );

	}

	//////////////////////////////////////////////////////////////////////////
	// Helper function that setups editor joint drives from physics system ones
	//////////////////////////////////////////////////////////////////////////
	void PhysicsBackendHelper::SetupEditorJointDrives( PhysicsJointPtr jointPtr, THandle<physics::SystemJoint> sysJoint )
	{
		for( Uint32 i = 0; i < EPhysicsJointDrive::EPJD_Count; ++i )
		{
			SPhysicsJointDrive editorDrive;
			const PhysicsJointDrive& drive = sysJoint->GetDrive( (PhysicsJointDriveType)i );
			editorDrive.m_damping = drive.m_damping;
			editorDrive.m_forceLimit = drive.m_forceLimit;
			editorDrive.m_stiffness = drive.m_stiffness;
			editorDrive.m_isAcceleration = drive.m_isAcceleration;
			jointPtr->SetDrive( (EPhysicsJointDrive)i, editorDrive );
		}
		jointPtr->SetDrivePosition( sysJoint->GetDrivePosition() );
		jointPtr->SetDriveLinearVelocity( sysJoint->GetDriveLinearVelocity() );
		jointPtr->SetDriveAngularVelocity( sysJoint->GetDriveAngularVelocity() );

	}

	struct MeshHierarchyHelper
	{
		PhysicsBaseID m_meshParentId;
		PhysicsBaseID m_actorParentId;
		PhysicsActorPtr m_mergedActor;
		red::DynArray< PhysicsBaseID > m_children;
		Bool m_processed;
	};

	// Function that looks for a mesh to generate collision geometry for.
	Bool PhysicsBackendHelper::FindPhysicsMesh( physics::PhysicsEditorScene* editorScene, THandle< physics::PhysicsMesh >& meshPtr )
	{
		const physics::TPhysicsObjectsMap& objectsOnScene = editorScene->GetPhysicsObjectsMap();
		for( auto& keyValue : objectsOnScene )
		{
			THandle< physics::PhysicsBase > object = keyValue.Value();
			if( object->GetBaseType() == physics::EPBT_Mesh )
			{
				if( editorScene->IsSingleMeshScene() || object->IsFlagSet( physics::EPBF_IsSelected ) )
				{
					meshPtr = Cast< physics::PhysicsMesh >( object );
					return true;
				}
			}
			else if( object->GetBaseType() == physics::EPBT_Actor )
			{
				const auto& actor = Cast< physics::PhysicsActor >( object );
				if( actor->IsFlagSet( physics::EPBF_IsSelected ) )
				{
					meshPtr = Cast< physics::PhysicsMesh >( editorScene->FindByPhysicsId( actor->GetBoneMap().m_meshId ) );
					return true;
				}
				for( const auto& shape : actor->GetActorShapes() )
				{
					if( shape->IsFlagSet( physics::EPBF_IsSelected ) )
					{
						meshPtr = Cast< physics::PhysicsMesh >( editorScene->FindByPhysicsId( actor->GetBoneMap().m_meshId ) );
						return true;
					}
				}
			}
		}
		return false;
	}

}