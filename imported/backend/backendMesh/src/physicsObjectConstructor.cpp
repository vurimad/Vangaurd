/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "physicsObjectConstructor.h"
#include "meshPhysicsUtils.h"
#include "physicsObjectCreationInfo.h"

#include "../../backendMesh/include/physicsJoint.h"
#include "../../backendMesh/include/physicsActor.h"
#include "../../backendMesh/include/physicsMesh.h"
#include "../../backendMesh/include/physicsEditorScene.h"
#include "../../backendMesh/include/physicsBackendHelper.h"

#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/objectHandler.h"
#include "../../../common/redReflection/include/rttiValueHolder.h"
#include "../../../common/redReflection/include/rttiSingleValueHolder.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/mathConvexHullEx.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshPacker.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/redMemory/include/uniquePtr.h"
#include "../../../common/resourceMesh/include/meshUtils.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IPhysicsObjectConstructor, tools );
RTTI_END_TYPE();

namespace tools
{
	const static CName dyn = RED_NAME_CONSTEXPR( "Dynamic" );

	Matrix GetMeshLocalTransformMatrix(const THandle<CMesh>& mesh)
	{
		Matrix localTransform = Matrix::IDENTITY();
		red::DynArray< Vector3 > allVertices;
		const mesh::DataViewRO& meshData = mesh::DataViewRO(*mesh);
		mesh::utils::ExtractGeometry(meshData, allVertices, nullptr);
		if (allVertices.Empty())
			return localTransform;

		Box box;
		box.Clear();
		for (auto v : allVertices)
			box.AddPoint(v);
		const auto center = box.CalcCenter();
		localTransform.SetTranslation(center);
		return localTransform;
	}

	/// Creates an actor
	class PhysicsObjectConstructor_Actor : public IPhysicsObjectConstructor
	{
		RTTI_DECLARE_TYPE( PhysicsObjectConstructor_Actor );

	public:
		virtual ~PhysicsObjectConstructor_Actor() {};

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info ) const override final
		{
			SPhysicsBaseCreationInfo data;
			if( GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				if( ((physics::EPhysicsBaseType)data.m_type) == physics::EPBT_Actor )
				{
					return true;
				}
			}
			return false;
		}

		virtual const Bool CreateObject( const ObjectCreationInfo& info, red::DynArray< THandle< physics::PhysicsBase > >& physicsObjects, physics::PhysicsEditorScene* editorScene, const red::DynArray< tools::EditorObjectID >& selectedIds ) const override final
		{
			if( !CanCreateObject( info ) )
			{
				return false;
			}

			SPhysicsBaseCreationInfo data;
			if( !GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				return false;
			}

			THandle< physics::ShapeGeometryBase > convexGeom = nullptr;
			THandle< physics::PhysicsMesh > meshPtr = nullptr;
			if( !physics::PhysicsBackendHelper::FindPhysicsMesh( editorScene, meshPtr ) )
			{
				physics::PhysicsBaseID meshId = 0;
				for( const tools::EditorObjectID& editorId : selectedIds )
				{
					THandle< physics::PhysicsActor > parent = Cast<physics::PhysicsActor>( editorScene->FindByGlobalId( editorId ) );
					if( parent )
					{
						if( meshId == 0 )
						{
							meshId = parent->GetBoneMap().m_meshId;
						}
						else
						{
							// multiple actors from different meshes selected, we won't be able to find a mesh to add actor to
							return false;
						}
					}
				}
				if( meshId != 0 )
				{
					meshPtr = Cast<physics::PhysicsMesh>( editorScene->FindByPhysicsId( meshId ) );
				}
			}

			if( meshPtr )
			{
				red::UniquePtr< physics::PhysicsActor > actorPtr = red::CreateUniquePtr< physics::PhysicsActor >( editorScene->GetNextFreeId(), editorScene );
				actorPtr->SetSimulationType( ( physics::EPhysicsSimulation )data.m_data );
				CName boneName = CName::NONE();

				if( meshPtr->GetFreeBoneName( boneName ) )
				{
					physics::PhysicsActorPtr actorHandle = physics::PhysicsActorPtr(std::move(actorPtr));
					Transform rigTransform = Transform( meshPtr->GetBoneRigMatrix( boneName ).GetTranslation(), meshPtr->GetBoneRigMatrix( boneName ).ToQuat() );

					actorHandle->SetLocalToWorld( rigTransform.ToInvMatrix() * meshPtr->GetLocalToWorld());

					if (boneName == CName::NONE() )
					{
						physics::PhysicsBackendHelper::BindPhysicsActorToMesh(actorHandle, meshPtr, boneName);
					}
					else
					{
						physics::PhysicsBackendHelper::BindPhysicsActorToMesh(actorHandle, meshPtr, boneName, rigTransform.GetInverse() );
					}
					physicsObjects.PushBack( actorHandle );
				}
				else
				{
					return false;
				}
			}
			return true;
		}
	};

	/// Creates a shape
	class PhysicsObjectConstructor_Shape : public IPhysicsObjectConstructor
	{
		RTTI_DECLARE_TYPE( PhysicsObjectConstructor_Shape );

	public:
		virtual ~PhysicsObjectConstructor_Shape() {};

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info ) const override final
		{
			SPhysicsBaseCreationInfo data;
			if( GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				if( ((physics::EPhysicsBaseType)data.m_type) == physics::EPBT_Shape )
				{
					return true;
				}
			}
			return false;
		}

		virtual const Bool CreateObject( const ObjectCreationInfo& info, red::DynArray< THandle< physics::PhysicsBase > >& physicsObjects, physics::PhysicsEditorScene* editorScene, const red::DynArray< tools::EditorObjectID >& selectedIds ) const override final
		{
			if( !CanCreateObject( info ) || !editorScene )
			{
				// We can't create the object with this constructor or we have no scene to add an object to
				return false;
			}

			SPhysicsBaseCreationInfo data;
			if( !GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				// Failed parsing creation info data
				return false;
			}

			red::UniquePtr< physics::PhysicsShape > shapePtr = red::CreateUniquePtr< physics::PhysicsShape >( );
			shapePtr->m_isObstacle = data.m_isObstacle;

			// Resolve shape ownership
			if( selectedIds.Size() > 0 )
			{
				// We have multiple objects selected, a shape can have only one actor as a parent. Let's try and find one.				
				for( tools::EditorObjectID parentId : selectedIds )
				{
					const auto& basePtr = editorScene->FindByGlobalId( parentId );
					if( basePtr->GetBaseType() != physics::EPBT_Actor )
					{
						// A shape can be only owned by an actor
						continue;
					}
					physics::PhysicsBaseID parentBaseId = parentId.GetInternalID();

					if( shapePtr->GetParentId() == 0 )
					{
						shapePtr->SetParentId( parentBaseId );
						continue;
					}
					if( shapePtr->GetParentId() != parentBaseId )
					{
						// This means we have multiple actors selected and we can't resolve ownership
						return false;
					}
				}
			}
			
			if( editorScene->IsSingleMeshScene() && shapePtr->GetParentId() == 0 )
			{
				// If we are editing mesh param of an unskinned mesh, try to find a single actor to be a parent
				THandle< physics::PhysicsMesh > meshPtr = nullptr;
				if( physics::PhysicsBackendHelper::FindPhysicsMesh( editorScene, meshPtr ) )
				{
					if( !meshPtr->IsFlagSet( physics::EPhysicsMeshFlag::EPMF_IsSkinned ) )
					{
						physics::SPhysicsActorBoneMapping mapping;
						if( meshPtr->GetActorBinding( 0, mapping ) )
						{
							physics::PhysicsBaseID parentBaseId = mapping.m_actorId;
							shapePtr->SetParentId( parentBaseId );
						}
					}
				}
			}

			if( shapePtr->GetParentId() == 0 )
			{
				// No parent found, can't create the shape
				return false;
			}
			THandle< physics::PhysicsActor > parent = Cast<physics::PhysicsActor>(editorScene->FindByPhysicsId(shapePtr->GetParentId()));
			Matrix localToWorld = Matrix::IDENTITY();

			const Float	defaultSize = 1.0f;
			switch( data.m_data )
			{
			case physics::EPST_Box:
				localToWorld = info.m_transform.ToMatrix();
				shapePtr->SetShapeGeometry( CreateHandle< physics::ShapeGeometryBox >( Vector4( defaultSize, defaultSize, defaultSize ) ) );
				break;
			case physics::EPST_Sphere:
				localToWorld = info.m_transform.ToMatrix();
				shapePtr->SetShapeGeometry( CreateHandle< physics::ShapeGeometrySphere >( defaultSize ) );
				break;
			case physics::EPST_Capsule:
				localToWorld = info.m_transform.ToMatrix();
				shapePtr->SetShapeGeometry( CreateHandle< physics::ShapeGeometryCapsule >( defaultSize, 3.0f * defaultSize ) );
				break;
			case physics::EPST_Convex:
			case physics::EPST_BoxAligned:
			case physics::EPST_SphereAligned:
			case physics::EPST_CapsuleAligned:
			{
				shapePtr->SetShapeGeometry( GenerateCollision( HandleFromPtr( editorScene ), parent, localToWorld, data.m_selectedVertices, data.m_data, data.m_vertexLimit ) );
			}
			break;
			default:
				break;
			}

			if( shapePtr->GetShapeGeometry() == nullptr )
			{
				// Geometry failed to create
				return false;
			}

			THandle< physics::PhysicsBase > physicsObject;
			physicsObject = physics::PhysicsShapePtr( std::move( shapePtr ) );
			physicsObject->SetLocalToWorld( localToWorld );	
			physicsObjects.PushBack( physicsObject );
			return true;
		}

		private:
			THandle< physics::ShapeGeometryBase > GenerateCollision(const THandle<physics::PhysicsEditorScene>& editorScene, const THandle< physics::PhysicsActor >& parent, Matrix& localToWorld,
				red::DynArray<Vector3>& selectedVertices, Uint32 geomType, const Uint32 vertexLimit = 64 ) const
			{
				THandle< physics::ShapeGeometryBase > convexGeom = nullptr;
				if( !parent )
				{
					return nullptr;
				}
				const auto& boneMap = parent->GetBoneMap();
				Int32 boneIndex = boneMap.m_boneName != CName::NONE() ? boneMap.m_boneIndex : -1;
				if( THandle< physics::PhysicsMesh > meshPtr = Cast<physics::PhysicsMesh>( editorScene->FindByPhysicsId( boneMap.m_meshId ) ) )
				{
					const THandle<CMesh>& meshResource = meshPtr->GetMesh();
					THandle < physics::ICollider > collider = nullptr;
					switch (geomType)
					{
					case physics::EPST_Convex:
						collider = BuildConvexFromMesh(*meshResource.Get(), red::ArraySpan<Vector3>( selectedVertices ), boneIndex, vertexLimit, true );
						break;
					case physics::EPST_BoxAligned:
						collider = BuildBoxFromMesh(*meshResource.Get(), red::ArraySpan<Vector3>( selectedVertices ), boneIndex);
						break;
					case physics::EPST_SphereAligned:
						collider = BuildSphereFromMesh(*meshResource.Get(), red::ArraySpan<Vector3>( selectedVertices ), boneIndex);
						break;
					case physics::EPST_CapsuleAligned:
						collider = BuildCapsuleFromMesh(*meshResource.Get(), red::ArraySpan<Vector3>( selectedVertices ), boneIndex);
						break;
					default:
						break;
					}
					
					if (collider)
					{
						convexGeom = physics::PhysicsBackendHelper::GetEditorGeometryFromCollider(collider);
						localToWorld = collider->m_localToBody.ToMatrix();
						if( selectedVertices.Size() == 0 )
						{
							Matrix meshL2W = meshPtr->GetLocalToWorld();
							localToWorld = localToWorld * meshL2W;
						}						
					}
				}
				return convexGeom;
			}
	};

	/// Creates from mesh
	class PhysicsObjectConstructor_Mesh : public IPhysicsObjectConstructor
	{
		RTTI_DECLARE_TYPE( PhysicsObjectConstructor_Mesh );

	public:
		virtual ~PhysicsObjectConstructor_Mesh() {};

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info ) const override final
		{
			// resource pointer ?
			if ( info.m_arguments && info.m_arguments->IsSingle() )
			{
				const auto& singleValue = info.m_arguments->GetSingle();
				const String& value = singleValue->ToString();
				const String& pathExt = value.StringAfter( "." );
				red::DynArray< const rtti::ClassType* > classTypes;
				GetRttiSystem().EnumClasses( ClassID<CMesh>(), classTypes );
				for( const auto* resourceClass : classTypes )
				{
					const String resourceClassNewExt = resourceClass->GetDefaultObject< CResource >()->GetExtension();
					const String resourceClassDeprecatedExt = resourceClass->GetDefaultObject< CResource >()->GetDeprecatedExtension();
					if( resourceClassNewExt == pathExt || resourceClassDeprecatedExt == pathExt )
					{
						return true;
					}
				}
			}

			// not compatible
			return false;
		}

		virtual const Bool CreateObject( const ObjectCreationInfo& info, red::DynArray< THandle< physics::PhysicsBase > >& physicsObjects, physics::PhysicsEditorScene* editorScene, const red::DynArray< tools::EditorObjectID >& selectedIds ) const override final
		{
			if( editorScene->IsSingleMeshScene() )
			{
				RED_LOG( "Core: Trying to add a mesh in mesh editor - improper use." );
				return false;
			}

			String meshPath = info.m_arguments->ToString();
			if ( meshPath.Empty() )
			{
				RED_LOG( "Core: No mesh in the arguments");
				return false;
			}
			
			auto meshResPath = res::ResourcePath::Build( meshPath );
			auto meshHandle = GResourceLoader->IssueLoadingRequest( meshResPath );
			meshHandle->WaitUntilLoaded();

			// mesh failed to load
			if ( !meshHandle->IsLoaded() )
			{
				RED_LOG( "Core: Mesh failed to load");
				return false;
			}

			const Matrix localToWorld = info.m_transform.ToMatrix();

			auto meshResource = red::StaticCast< CMesh >(meshHandle->GetResource());
			if( meshResource )
			{
				// We have mesh resource loaded, so create data view for it and physics mesh representation
				const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *meshResource );
				physics::PhysicsMeshPtr meshPtr = CreateHandle< physics::PhysicsMesh >( editorScene->GetNextFreeId(), meshPath, editorScene );
				THandle< physics::SystemResource > activePhysics = mesh::utils::GetActivePhysics( *meshResource );

				if ( activePhysics && activePhysics->GetNumObjects())
				{
					// We've found some kind of valid physics system in the mesh, let's create editor representation for it
					// Convert physics system resource to editor objects
					meshPtr->SetLocalToWorld(localToWorld);
					physicsObjects = physics::PhysicsBackendHelper::ConvertFromPhysicsSystemToEditor( activePhysics, editorScene, meshPtr );
				}
				else
				{
					// We do not have any kind of physics system in the mesh, we need to create one
					physicsObjects = physics::PhysicsBackendHelper::GenerateEditorActorsForMesh( localToWorld, editorScene, meshPtr );
					meshPtr->SetLocalToWorld( localToWorld );
				}

				physicsObjects.PushBack( std::move( meshPtr )  );
			}

			return true;
		}

		void ExtractMeshGeometry(const mesh::DataViewRO &meshDataViewRO, Uint32 &vertexCount, red::DynArray<Float> &vertices, Uint32 &skinCount, red::DynArray<Uint8> &skinInd) const
		{
			Uint32 totalSize = 0;
			Uint32 stride = MeshPackerUtils::GetMeshStreamSize(MST_Position_3F);

			Uint32 totalSizeSkin = 0;
			Uint32 strideSkin = MeshPackerUtils::GetMeshStreamSize(MST_SkinningIndices_4U8);

			// First pass to get total size
			Uint32 chunkNumber = meshDataViewRO.GetNumChunks();
			for (Uint32 i = 0; i < chunkNumber; ++i)
			{
				auto positionData = meshDataViewRO.GetChunkStreamData(i, MST_Position_3F);
				totalSize += positionData.m_count * positionData.m_stride;

				auto skinningData = meshDataViewRO.GetChunkStreamData(i, MST_SkinningIndices_4U8);
				totalSizeSkin += skinningData.m_count * skinningData.m_stride;

			}

			if (totalSize && totalSizeSkin)
			{
				vertexCount = totalSize / stride;
				vertices.Resize(vertexCount * 3);

				skinCount = totalSizeSkin / strideSkin;
				skinInd.Resize(skinCount * 4);
				Uint32 offset = 0;
				Uint32 offsetSkin = 0;

				Uint32 chunkNumber = meshDataViewRO.GetNumChunks();
				for (Uint32 i = 0; i < chunkNumber; ++i)
				{
					auto positionData = meshDataViewRO.GetChunkStreamData(i, MST_Position_3F);

					Uint32 size = positionData.m_count * positionData.m_stride;
					red::Memcpy(&vertices[0] + offset, positionData.m_data, size);
					offset += size / sizeof(Float);

					auto skinningData = meshDataViewRO.GetChunkStreamData(i, MST_SkinningIndices_4U8);
					size = skinningData.m_count * skinningData.m_stride;
					red::Memcpy(&skinInd[0] + offsetSkin, skinningData.m_data, size);
					offsetSkin += size / sizeof(Uint8);
				}
			}
		}

	};

	/// Creates a joint
	class PhysicsObjectConstructor_Joint : public IPhysicsObjectConstructor
	{
		RTTI_DECLARE_TYPE( PhysicsObjectConstructor_Joint );

	public:
		virtual ~PhysicsObjectConstructor_Joint() {};

		virtual const Bool CanCreateObject( const ObjectCreationInfo& info ) const override final
		{
			SPhysicsBaseCreationInfo data;
			if( GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				if( ((physics::EPhysicsBaseType)data.m_type) == physics::EPBT_Joint )
				{
					return true;
				}
			}
			return false;
		}

		virtual const Bool CreateObject( const ObjectCreationInfo& info, red::DynArray< THandle< physics::PhysicsBase > >& physicsObjects, physics::PhysicsEditorScene* editorScene, const red::DynArray< tools::EditorObjectID >& selectedIds ) const override final
		{
			if( !CanCreateObject( info ) )
			{
				return false;
			}

			SPhysicsBaseCreationInfo data;
			if( !GetTypeObject<SPhysicsBaseCreationInfo>()->WriteValue( IRTTIContext::GetDefault(), &data, rtti::AccessPath(), *info.m_arguments ) )
			{
				return false;
			}


			red::UniquePtr< physics::PhysicsJoint > jointPtr = red::CreateUniquePtr< physics::PhysicsJoint >( editorScene->GetNextFreeId(), editorScene );

			// Let's check if we have some actors selected to connect the joint too
			if( selectedIds.Size() > 0 )
			{
				physics::PhysicsBaseID parentBaseId = 0;
				physics::PhysicsBaseID childBaseId = 0;
				for( tools::EditorObjectID selectedId : selectedIds )
				{
					const auto& basePtr = editorScene->FindByGlobalId( selectedId );
					if( !basePtr || basePtr->GetBaseType() != physics::EPBT_Actor )
					{
						// A joint can only be attached to an actor
						continue;
					}
					const physics::PhysicsActorPtr& actorPtr = Cast< physics::PhysicsActor >( basePtr );
					physics::PhysicsMeshBoneMapping& boneMap = actorPtr->GetBoneMap();					
					physics::PhysicsBaseID selectedBaseId = selectedId.GetInternalID();

					if( jointPtr->GetActorId( physics::EPJAI_Parent ) == 0 )
					{
						jointPtr->SetActorId( selectedBaseId, physics::EPJAI_Parent );
						parentBaseId = selectedBaseId;
						continue;
					}
					else if( jointPtr->GetActorId( physics::EPJAI_Child ) == 0 )
					{
						jointPtr->SetActorId( selectedBaseId, physics::EPJAI_Child);
						childBaseId = selectedBaseId;
						continue;
					}
					else
					{
						// This means we have more than two actors, can't resolve joint attachments, will be created as a free joint
						jointPtr->SetActorId( 0, physics::EPJAI_Parent );
						jointPtr->SetActorId( 0, physics::EPJAI_Child );
						parentBaseId = 0;
						childBaseId = 0;
					}
				}

				if( parentBaseId && childBaseId && parentBaseId != childBaseId )
				{
					const auto& objects = editorScene->GetPhysicsObjectsOnScene();
					for( const physics::PhysicsBasePtr& object : objects )
					{
						if( const THandle<physics::PhysicsJoint> joint = Cast<physics::PhysicsJoint>( object ) )
						{
							if( joint->GetActorId( physics::EPJAI_Parent ) == parentBaseId
								&& joint->GetActorId( physics::EPJAI_Child ) == childBaseId )
								return false; // joint between those actors already exist
							
							if( joint->GetActorId( physics::EPJAI_Parent ) == childBaseId
								&& joint->GetActorId( physics::EPJAI_Child ) == parentBaseId )
								return false; // circular dependency, makes no sense to make such joint
						}
					}
				}
			}

			Matrix localToWorld = info.m_transform.ToMatrix();

			// See if we managed to resolve parent attachment
			if( jointPtr->GetActorId( physics::EPJAI_Parent ) )
			{
				// Resolve actor0 ptr
				const auto& actor0 = editorScene->FindByPhysicsId( jointPtr->GetActorId( physics::EPJAI_Parent ) );

				// set the joint position in the pivot of the parent actor
				if( actor0 )
				{
					localToWorld = actor0->GetLocalToWorld(); 
				}
			}

			jointPtr->SetLocalToWorld( localToWorld );

			physics::PhysicsJointPtr joint = physics::PhysicsJointPtr( std::move( jointPtr ) );

			physicsObjects.PushBack( joint );

			return true;
		}
	};
} // tools

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsObjectConstructor_Actor, tools );
RTTI_PARENT_TYPE( tools::IPhysicsObjectConstructor );
RTTI_END_TYPE();



RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsObjectConstructor_Shape, tools );
RTTI_PARENT_TYPE( tools::IPhysicsObjectConstructor );
RTTI_END_TYPE();



RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsObjectConstructor_Mesh, tools );
RTTI_PARENT_TYPE( tools::IPhysicsObjectConstructor );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsObjectConstructor_Joint, tools );
RTTI_PARENT_TYPE( tools::IPhysicsObjectConstructor );
RTTI_END_TYPE();





