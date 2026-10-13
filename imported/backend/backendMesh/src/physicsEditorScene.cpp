/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "physicsEditorScene.h"
#include "physicsActor.h"
#include "physicsShape.h"
#include "physicsMesh.h"
#include "physicsJoint.h"
#include "physicsBackendHelper.h"
#include "physicsShapeRenderer.h"
#include "meshGluer.h"
#include "meshStructureNodes.h"
#include "backendMeshPreviewPanelInputListener.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/core/include/memoryBufferFile.h"
#include "../../../common/engine/include/physicsDecorationResource.h"

#include "../../../common/redReflection/include/handle.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSceneStructs.h"
#include "../../../common/redMath/include/random.h"
#include "../../../common/physics/include/physicsCollider.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( PhysicsSimulationFPS, physics )
RTTI_ENUM_OPTION_ALIAS( "5 FPS", FPS_5 );
RTTI_ENUM_OPTION_ALIAS( "15 FPS", FPS_15 );
RTTI_ENUM_OPTION_ALIAS( "30 FPS", FPS_30 );
RTTI_ENUM_OPTION_ALIAS( "60 FPS", FPS_60 );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsSimulationOptions, physics );
RTTI_PARENT_TYPE( ISerializable )
RTTI_PROPERTY_CATEGORY( "General" );
RTTI_PROPERTY( m_simulationFPS ).editable();
RTTI_PROPERTY( m_startInactive ).editable();
RTTI_PROPERTY( m_simulateAsDestruction ).editable();
RTTI_PROPERTY( m_filterDataSource ).editable();
RTTI_PROPERTY( m_impulseMagnitude ).editable().range( 1.0f, 100.f );
RTTI_PROPERTY( m_impulseRadius ).editable().range( 0.0f, 10.f );
RTTI_PROPERTY( m_enableJitter ).editable();
RTTI_PROPERTY( m_maxJitterRange ).editable().range( 0.1f, 100.f );
RTTI_PROPERTY( m_groundPlanePosition ).editable().range( -75, 75 );
RTTI_PROPERTY( m_groundPlaneSize ).editable().range( 2, 200 );
RTTI_PROPERTY_CATEGORY( "Throwable object" );
RTTI_PROPERTY( m_throwableShape ).inlined();
RTTI_PROPERTY( m_thrownObjectVelocity ).editable().range(1.0f, 20.f);
RTTI_PROPERTY( m_thrownObjectCustomMass ).editable();
RTTI_PROPERTY( m_thrownObjectMass ).editable();
RTTI_PROPERTY_CATEGORY( "Destruction params" ); 
RTTI_PROPERTY( m_destructionParams ).inlined();
RTTI_PROPERTY( m_destructionLevelData ).editable().inlined().resizable().tooltip( "Per destruction level data. If the provided mesh has bonds, last index in this array will contain data for clusters." );;
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsEditorOptions, physics );
RTTI_PARENT_TYPE( ISerializable )
RTTI_PROPERTY( m_renderOptions );
RTTI_PROPERTY( m_simulationOptions );
RTTI_PROPERTY( m_markVerticesHandlerOptions );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsEditorScene, physics );
RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

namespace physics
{
	//////////////////////////////////////////////////////////////////////////

	// Physics sim options

	PhysicsSimulationOptions::PhysicsSimulationOptions()
		: m_simulationFPS( FPS_60 )
		, m_groundPlanePosition( -3 )
		, m_groundPlaneSize( 10 )
		, m_enableJitter( false )
		, m_maxJitterRange( 5.0f )
		, m_thrownObjectVelocity( 5.0f )
		, m_thrownObjectMass( 10.f )
		, m_thrownObjectCustomMass( false )
		, m_throwableShape( physics::ColliderSphere::Create( 1.0f ) )
	{
	}

	PhysicsSimulationOptions::~PhysicsSimulationOptions()
	{
	}

	Float PhysicsSimulationOptions::GetPhysicsStep()
	{
		Float physicsStep = 1.0f;
		switch( m_simulationFPS )
		{
		case physics::FPS_5:
		{
			constexpr Float physicsStep5FPS = 1.0f / 5.0f;
			physicsStep = physicsStep5FPS;
			break;
		}
		case physics::FPS_15:
		{
			constexpr Float physicsStep15FPS = 1.0f / 15.0f;
			physicsStep = physicsStep15FPS;
			break;
		}
		case physics::FPS_30:
		{
			constexpr Float physicsStep30FPS = 1.0f / 30.0f;
			physicsStep = physicsStep30FPS;
			break;
		}
		case physics::FPS_60:
		{
			constexpr Float physicsStep60FPS = 1.0f / 60.0f;
			physicsStep = physicsStep60FPS;
			break;
		}
		default:
			break;				
		}
		if( m_enableJitter )
		{
			return physicsStep * math::DefaultRandom().Get<Float>( 1.0f, m_maxJitterRange );
		}
		return physicsStep;
	}

	// !Physics sim options

	// Physics sim options

	PhysicsEditorOptions::PhysicsEditorOptions()
		: m_renderOptions( CreateHandle<physics::PhysicsShapeRendererOptions>() )
		, m_simulationOptions( CreateHandle<physics::PhysicsSimulationOptions>() )
		, m_markVerticesHandlerOptions( CreateHandle<tools::MarkVerticesHandlerOptions>() )
	{	
	}

	PhysicsEditorOptions::~PhysicsEditorOptions()
	{
	}

	// !Physics sim options

	//////////////////////////////////////////////////////////////////////////

	PhysicsEditorScene::PhysicsEditorScene( )
		: m_lastObjectID( 1 )
		, m_physicsObjects( red::PoolBackend() )
	{}

	void PhysicsEditorScene::SetResource( const THandle< CResource >& res )
	{
		// keep the resource around
		m_rootResource = res;
		m_physicsObjects.Clear();
		if( !m_singleMeshScene )
			DeserializeBuffer();
	}

	void PhysicsEditorScene::AttachNotificationHandler( const red::SharedPtr<tools::IMeshPhysicsStructureNode>& handlerToAdd )
	{
		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				if( handler == handlerToAdd )
					return;
			}
		}
		m_notificationHandlers.PushBack( handlerToAdd );
	}

	PhysicsEditorScene::~PhysicsEditorScene( void )
	{
		m_physicsObjects.Clear();

		// release our hold on the root resource
		if( m_rootResource )
		{
			m_rootResource = nullptr;
		}
	}

	void PhysicsEditorScene::OnUpdate( Float timeDelta )
	{
		auto& it = m_physicsObjects.Begin();
		auto& end = m_physicsObjects.End();
		while( it != end )
		{
			it.Value()->OnUpdate( );
			++it;
		}
	}

	void PhysicsEditorScene::OnRender( rend::IDebugDrawer& debugDrawer ) const
	{
		auto& it = m_physicsObjects.Begin();
		auto& end = m_physicsObjects.End();
		while( it != end )
		{
			it.Value()->OnRender(debugDrawer, m_physicsEditorOptions->m_renderOptions );
			++it;
		}
	}

	void PhysicsEditorScene::PrepareForRendering( ) const
	{
		auto& it = m_physicsObjects.Begin();
		auto& end = m_physicsObjects.End();
		while( it != end )
		{
			it.Value()->PrepareForRendering( m_physicsEditorOptions->m_renderOptions );
			++it;
		}
	}

	THandle<PhysicsEditorScene> PhysicsEditorScene::Create( const THandle< CResource >& res, CName objectType, Bool isSingleMesh, const THandle<PhysicsEditorOptions>& physicsEditorOptions )
	{
		// we need some root resource to create physics editor
		if( !res )
		{
			return nullptr;
		}
		if( res->IsA< CPhysicsDecorationResource >() || res->IsA< CMesh >() )
		{
			THandle<PhysicsEditorScene> scene = CreateHandle<PhysicsEditorScene>( PhysicsEditorScene() );

			scene->m_objectTypePhysics = objectType;
			scene->m_rootResource = res;
			scene->m_physicsObjects.Clear();
			scene->m_singleMeshScene = isSingleMesh;
			scene->m_physicsEditorOptions = physicsEditorOptions;
			return scene;
		}
		// unable to create a physics decoration editor structure from a given resource
		RED_LOG_ERROR( "Engine: Failed to create a world structure for '%hs'", res->GetPath().ToString().AsChar() );
		return nullptr;
	}

	PhysicsBaseID PhysicsEditorScene::GetNextFreeId()
	{
		// Get next free id
		auto editorId = GetEditorIdFromPhysicsBaseId(++m_lastObjectID);
		while( !editorId.IsValid() || m_physicsObjects.KeyExist( editorId ) )
		{
			++m_lastObjectID;
			editorId = GetEditorIdFromPhysicsBaseId(m_lastObjectID);
		}
		return m_lastObjectID;
	}

	Bool PhysicsEditorScene::AddPhysicsObject( PhysicsBasePtr objectPtr )
	{
		// check if exists or has an invalid ID. Allocate new if needed.
		auto editorId = objectPtr->GetEditorObjectID();
		if( m_physicsObjects.KeyExist( editorId ) )
		{
			m_physicsObjects[editorId] = objectPtr;
		}
		else if( !editorId.IsValid()  )
		{
			// invalid id
			objectPtr->m_physicsBaseId = GetNextFreeId();
		}
		m_physicsObjects.Insert( objectPtr->GetEditorObjectID(), objectPtr );

		auto notificationHandlersSnap = m_notificationHandlers;
		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : notificationHandlersSnap )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectAdded( objectPtr );
			}
		}
		return true;
	}

	void PhysicsEditorScene::PlacePhysicsObject( PhysicsBasePtr ptr )
	{
		// Restore scene ptr
		ptr->OnRestore( this );

		if( ptr->GetBaseType() == EPBT_Shape )
		{
			PhysicsShapePtr shapePtr = Cast< PhysicsShape >( ptr );
			PhysicsActorPtr parentActor = shapePtr->GetActorParent();
			if( parentActor )
			{
				// fix local to world and local transform before we add the shape to the actor
				Matrix parentL2WInv = parentActor->GetLocalToWorld().OrthonormInverted();
				shapePtr->UpdateLocalTransform( shapePtr->GetLocalToWorld() * parentL2WInv );

				parentActor->AddShape( shapePtr );
			}
			else
			{
				// If the actor haven't been found from the shape parent id, then it means
				// ID is not valid and we have to create new actor for the shape
				PhysicsActorPtr parentActorPtr = CreateHandle< PhysicsActor >( GetNextFreeId() );
				shapePtr->SetParentId( parentActorPtr->GetPhysicsObjectID() );
				PlacePhysicsObject( parentActorPtr );
				parentActorPtr->AddShape( shapePtr );
			}
		}
		else
		{
			AddPhysicsObject( ptr );
		}

		PostLoadContext postLoadContext{};
		ptr->OnPostLoad( postLoadContext );
	}
	
	void PhysicsEditorScene::PlacePhysicsObject( PhysicsBasePtr ptr, red::DynArray< tools::EditorObjectID >& outActualCreatedIDs )
	{
		// Restore scene ptr
		ptr->OnRestore( this );

		if( ptr->GetBaseType() == EPBT_Shape )
		{
			PhysicsShapePtr shapePtr = red::StaticCast< PhysicsShape >( ptr);
			PhysicsActorPtr parentActor = shapePtr->GetActorParent();
			if( parentActor )
			{
				// fix local to world and local transform before we add the shape to the actor
				Matrix parentL2WInv = parentActor->GetLocalToWorld().OrthonormInverted();
				shapePtr->UpdateLocalTransform( shapePtr->GetLocalToWorld() * parentL2WInv );

				parentActor->AddShape( shapePtr );
			}		
			else 
			{
				// If the actor haven't been found from the shape parent id, then it means
				// ID is not valid and we have to create new actor for the shape
				PhysicsActorPtr parentActorPtr = CreateHandle< PhysicsActor >( GetNextFreeId() );
				shapePtr->SetParentId( parentActorPtr->GetPhysicsObjectID() );
				PlacePhysicsObject( parentActorPtr, outActualCreatedIDs );
				parentActorPtr->AddShape( shapePtr );
			}
		}
		else 
		{
			AddPhysicsObject( ptr );
		}

		PostLoadContext postLoadContext{};
		ptr->OnPostLoad( postLoadContext );

		outActualCreatedIDs.PushBack( ptr->GetEditorObjectID() );
	}

	void PhysicsEditorScene::PlacePhysicsObjects( red::DynArray< PhysicsBasePtr >& physObjects )
	{
		for( PhysicsBasePtr ptr : physObjects )
		{
			PlacePhysicsObject( ptr );
		}
	}

	void PhysicsEditorScene::PlacePhysicsObjects( red::DynArray< PhysicsBasePtr >& physObjects, red::DynArray< tools::EditorObjectID >& outActualCreatedIDs )
	{
		for( PhysicsBasePtr ptr : physObjects )
		{
			PlacePhysicsObject( ptr, outActualCreatedIDs );
		}
	}

	Bool PhysicsEditorScene::RemovePhysicsObject( const tools::EditorObjectID id )
	{
		Bool result = false;
		PhysicsBasePtr baseObject = FindByGlobalId( id );

		if( baseObject == nullptr )
		{
			return result;
		}

		if( baseObject->GetBaseType() == EPBT_Shape )
		{
			PhysicsShapePtr shapePtr = red::StaticCast< PhysicsShape >( baseObject );
			PhysicsActorPtr parentActor = shapePtr->GetActorParent();
			if( parentActor )
			{
				parentActor->RemoveShape( shapePtr );
				result = true;
			}
		}
		else
		{
			RemovePhysicsObject( baseObject );
			result = true;
		}

		return result;
 	}

	void PhysicsEditorScene::RemovePhysicsObject( PhysicsBasePtr object )
	{
		if( object != nullptr )
		{
			// Remove the physics object and update last free ID if it's bigger than the id of object being removed
			m_physicsObjects.RemoveValue( object );
			object->OnRemove();
		}
	}

	void PhysicsEditorScene::SnapPhysicsObject( tools::EditorObjectID id, Bool alternativeMode )
	{
		auto object = FindByGlobalId( id );
		if( object )
		{
			EPhysicsBaseType type = object->GetBaseType();
			switch( type )
			{
				case physics::EPBT_Actor:
					if( alternativeMode )
					{
						const THandle<PhysicsActor> actor = Cast<PhysicsActor>( object );
						actor->SnapToBone();
					}
					else
					{
						object->SetLocalToWorld( Matrix::IDENTITY() );
					}
					break;
				case physics::EPBT_Shape:
				{
					const THandle<PhysicsShape> shape = Cast<PhysicsShape>( object );
					shape->SetLocalToWorld( shape->GetActorParent()->GetLocalToWorld() );
				}
				break;
				case physics::EPBT_Joint:
				{
					const THandle<PhysicsJoint> joint = Cast<PhysicsJoint>( object );
					EPhysicsJointActorIndex actorIndex = alternativeMode ? EPJAI_Child : EPJAI_Parent;
					auto actor = FindByPhysicsId( joint->GetActorId( actorIndex ) );

					joint->SetLocalToWorld( actor->GetLocalToWorld() );

				}
				break;
				default:
					break;
			}
		}
	}

	void PhysicsEditorScene::RecomputeCOM( const red::DynArray< tools::EditorObjectID >& ids )
	{
		for( const tools::EditorObjectID& id : ids )
		{
			PhysicsBasePtr object = FindByGlobalId( id );
			if( PhysicsActorPtr actorPtr = Cast< PhysicsActor >( object ) )
			{
				actorPtr->RecomputeCOM();
			}
		}		
	}

	red::DynArray<Uint32> PhysicsEditorScene::GenerateDefaultDestructionHierarchy()
	{
		red::DynArray<Uint32> oldHierarchy = { red::PoolBackend() };
		Uint32 hierarchySize = 0;
		const auto& objects = GetPhysicsObjectsOnScene();
		for( const PhysicsBasePtr& object : objects )
		{
			if( object->GetBaseType() == EPBT_Actor )
			{
				++hierarchySize;
			}
		}
		
		if( hierarchySize > 0 )
		{
			physics::DestructionHierarchyOffsets offsets = { red::PoolBackend() };
			offsets.Resize(hierarchySize);

			THandle<CMesh> meshResource = Cast<CMesh>(m_rootResource);
			const THandle<mesh::MeshParamDestructionStepData> destructionHierarchy = meshResource->CreateParameter< mesh::MeshParamDestructionStepData >();
			auto& oldHierarchyTemp = destructionHierarchy->m_offsets;
			oldHierarchy.Reserve( oldHierarchyTemp.Size() );
			for( auto hierarchyOffset : oldHierarchyTemp )
			{
				oldHierarchy.PushBack( hierarchyOffset.m_combined );
			}

			Uint16 childSize = hierarchySize - 1;
			for( Uint16 i = 0; i < childSize; ++i )
			{
				offsets[ i + 1 ] = 0;
			}

			physics::DestructionHierarchyOffset nonFractured = { 1, childSize };
			offsets[ 0 ] = nonFractured.m_combined;
			destructionHierarchy->m_offsets = offsets;
		}

		return oldHierarchy;
	}

	void PhysicsEditorScene::SetDestructionHierarchy(const red::DynArray<Uint32>& hierarchy)
	{
		THandle<CMesh> meshResource = Cast<CMesh>(m_rootResource);
		if( hierarchy.Size() > 0 )
		{
			const THandle<mesh::MeshParamDestructionStepData> destructionHierarchy = meshResource->CreateParameter< mesh::MeshParamDestructionStepData >();
			physics::DestructionHierarchyOffsets& hierarchyOffsets = destructionHierarchy->m_offsets;
			hierarchyOffsets.Clear();
			hierarchyOffsets.Reserve( hierarchy.Size() );
			for( Uint32 combined : hierarchy )
			{
				hierarchyOffsets.PushBack( physics::DestructionHierarchyOffset(combined) );
			}
		}
		else
		{
			meshResource->RemoveParameter( ClassID<mesh::MeshParamDestructionStepData>());
		}
	}

	const tools::EditorObjectID PhysicsEditorScene::FindParentId(const tools::EditorObjectID& id)
	{
		auto object = FindByGlobalId( id );
		if( object )
		{
			EPhysicsBaseType type = object->GetBaseType();
			switch( type )
			{
				case physics::EPBT_Actor:
					return tools::EditorObjectID::EditorObjectID();
				case physics::EPBT_Shape:
				{
					PhysicsShape* shape = (PhysicsShape*)object.Get();
					const auto& parent = shape->GetActorParent();
					return parent->GetEditorObjectID();
				}
				break;
				case physics::EPBT_Joint:
				{
					PhysicsJoint* joint = (PhysicsJoint*)object.Get();
					const auto& parent = FindByPhysicsId( joint->GetActorId( EPJAI_Parent ) );
					return parent->GetEditorObjectID();
				}
				break;
				default:
					break;
			}
		}
		return tools::EditorObjectID::EditorObjectID();
	}

	tools::EditorObjectID PhysicsEditorScene::GetEditorIdFromPhysicsBaseId( PhysicsBaseID baseId )
	{
		return tools::EditorObjectID::Build( m_rootResource->GetPath(), baseId, m_objectTypePhysics );
	}

	void PhysicsEditorScene::SerializeBuffer()
	{
		if( m_rootResource && m_rootResource->IsA< CPhysicsDecorationResource >() )
		{
			CPhysicsDecorationResource* resource = (CPhysicsDecorationResource*)m_rootResource.Get();
			{
				GrowableMemoryBuffer memBuffer;
				MemoryBufferFileWriter writer( memBuffer );
				for( auto it : m_physicsObjects )
				{
					const PhysicsBasePtr& obj = it.Value();
					Uint8 baseType = obj->GetBaseType();
					writer.Serialize( &baseType, sizeof( baseType ) );
					obj->OnSerialize( writer );

					if( baseType == physics::EPBT_Actor )
					{
						physics::PhysicsActorPtr actorPtr = Cast< physics::PhysicsActor >( obj );

						auto shapes = actorPtr->GetActorShapes();
						for( auto shape : shapes )
						{
							baseType = shape->GetBaseType();
							writer.Serialize( &baseType, sizeof( baseType ) );
							shape->OnSerialize( writer );
						}
					}
				}
				resource->GetEditorDataBuffer() = DataBuffer::Copy( memBuffer.Data(), memBuffer.Size() );
			}
		}
	}

	void PhysicsEditorScene::DeserializeBuffer()
	{
		if( m_rootResource && m_rootResource->IsA< CPhysicsDecorationResource >() )
		{
			CPhysicsDecorationResource* resource = (CPhysicsDecorationResource*)m_rootResource.Get();
			{
				red::DynArray< tools::EditorObjectID > createdIds;

				if( resource->GetEditorDataBuffer().Size() != 0 )
				{
					auto& buffer = resource->GetEditorDataBuffer();
					CMemoryFileReader reader( static_cast< const Uint8* >( buffer.Data() ), buffer.Size(), 0 );
					reader.m_version = resource->GetEditorDataVersion();

					// load editor objects from editor buffer
					Uint32 count = 0;
					while( reader.GetOffset() != reader.GetSize() )
					{
						Uint8 baseType;
						reader << baseType;

						if( baseType == EPBT_Mesh )
						{
							PhysicsMeshPtr mesh = PhysicsMeshPtr( RED_NEW( PhysicsMesh )() );
							mesh->OnSerialize( reader );
							PlacePhysicsObject( mesh, createdIds );
						}
						else if( baseType == EPBT_Actor )
						{
							PhysicsActorPtr actor = PhysicsActorPtr( RED_NEW( PhysicsActor )() );
							actor->OnSerialize( reader );
							actor->ClearActorShapes();
							PlacePhysicsObject( actor, createdIds );
							++count;
						}
						else if( baseType == EPBT_Joint )
						{
							PhysicsJointPtr joint = PhysicsJointPtr( RED_NEW( PhysicsJoint )() );
							joint->OnSerialize( reader );
							PlacePhysicsObject( joint, createdIds );						
							++count;
						}
						else if( baseType == EPBT_Shape )
						{
							PhysicsShapePtr shape = PhysicsShapePtr( RED_NEW( PhysicsShape )() );
							shape->OnSerialize( reader );
							PlacePhysicsObject( shape, createdIds );
							++count;
						}
					}
				}				
			}
		}
	}

	void PhysicsEditorScene::MergeMeshes()
	{
		if( m_rootResource && !IsSingleMeshScene() )
		{
			CPhysicsDecorationResource* resource = (CPhysicsDecorationResource*)m_rootResource.Get();
			red::DynArray< PhysicsBasePtr > basePtrs = {red::PoolBackend()};
			m_physicsObjects.GetValues( basePtrs );

			GrowableMemoryBuffer memBuffer;
			MemoryBufferFileWriter writer( memBuffer );

			red::DynArray< PhysicsMeshPtr > meshesToMerge = { red::PoolBackend() };

			red::DynArray< PhysicsMeshPtr > skinnedMeshesToMerge = { red::PoolBackend() };
			red::DynArray< PhysicsMeshPtr > meshesToBeSkinned = { red::PoolBackend() };

			for( auto obj : basePtrs )
			{
				Uint8 baseType = obj->GetBaseType();
				writer.Serialize( &baseType, sizeof( baseType ) );
				obj->OnSerialize( writer );

				if( baseType == EPBT_Actor )
				{
					PhysicsActorPtr actorPtr = Cast< PhysicsActor >( obj );

					auto shapes = actorPtr->GetActorShapes();
					for( auto shape : shapes )
					{
						baseType = shape->GetBaseType();
						writer.Serialize( &baseType, sizeof( baseType ) );
						shape->OnSerialize( writer );
					}
				}
				else if( baseType == EPBT_Mesh )
				{
					PhysicsMeshPtr meshPtr = Cast< PhysicsMesh >( obj );
					meshesToMerge.PushBack( meshPtr );
				}
			}
			resource->GetEditorDataBuffer() = DataBuffer::Copy( memBuffer.Data(), memBuffer.Size() );

			// Finally, we have resolved meshes that got linked, proceed to merging
			for( auto& meshPtr : meshesToMerge )
			{
				if( meshPtr->IsFlagSet( EPMF_IsSkinned ) )
				{
					skinnedMeshesToMerge.PushBack( meshPtr );
				}
				else
				{
					meshesToBeSkinned.PushBack( meshPtr );
				}
			}

			// Glue all the unskinned meshes together, generating skinning
			mesh::MeshGluerInput unskinnedMeshesInput;
			unskinnedMeshesInput.m_generateSkinning = true;
			unskinnedMeshesInput.m_transform = true;

			mesh::MeshInput meshInp;
			TSystemObjectsMap actorsMap = { red::PoolBackend() };
			for( PhysicsMeshPtr physMesh : meshesToBeSkinned )
			{
				THandle< CMesh > meshHandle = physMesh->GetMesh();
	
				meshInp.m_mesh = meshHandle.Get();
				meshInp.m_transform = physMesh->GetLocalToWorld();
				meshInp.m_customPhysics = PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( basePtrs, actorsMap, physMesh );
				unskinnedMeshesInput.m_meshes.PushBack( meshInp );
			}

			mesh::MeshGluer meshGluer;

			mesh::MeshGluerInput skinnedMeshInput;
			skinnedMeshInput.m_generateSkinning = false;
			skinnedMeshInput.m_transform = true;

			// add meshes that already had skinning
			for( PhysicsMeshPtr physMesh : skinnedMeshesToMerge )
			{
				THandle< CMesh > meshHandle = physMesh->GetMesh();
				meshInp.m_customPhysics = PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( basePtrs, actorsMap, physMesh );
				meshInp.m_mesh = meshHandle.Get();
				meshInp.m_transform = physMesh->GetLocalToWorld();
				skinnedMeshInput.m_meshes.PushBack( meshInp );
			}

			// One more pass on all PhysicsMeshPtr to create joints now that we should have actorMap filled
			for( Uint32 i = 0; i < unskinnedMeshesInput.m_meshes.Size(); ++i )
			{
				PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( basePtrs, actorsMap, unskinnedMeshesInput.m_meshes[ i ].m_customPhysics, meshesToBeSkinned[ i ] );
			}
			for( Uint32 i = 0; i < skinnedMeshInput.m_meshes.Size(); ++i )
			{
				PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( basePtrs, actorsMap, skinnedMeshInput.m_meshes[ i ].m_customPhysics, skinnedMeshesToMerge[ i ] );
			}

			// TODO: Refactor this bit
			Bool mergingSkinned = skinnedMeshesToMerge.Size() != 0;
			THandle< CMesh > outputTempMesh = mergingSkinned ? CreateHandle<CMesh>() : Cast<CMesh>( m_rootResource );
			Bool wasModified = false;
			if( meshGluer.Process_Sync( unskinnedMeshesInput, outputTempMesh.Get()) )
			{
				// once we glued the unskinned mesh together and generated skinning for them, we have to merge them with already skinned ones
				meshInp.m_mesh = outputTempMesh.Get();
				meshInp.m_transform = Matrix::IDENTITY();
				THandle< mesh::MeshParamPhysics > physicsData = outputTempMesh->GetParameter< mesh::MeshParamPhysics >();
				meshInp.m_customPhysics = physicsData->GetPhysicsData();
				wasModified = true;
				if( skinnedMeshInput.m_meshes.Size() > 0 )
				{				
					skinnedMeshInput.m_meshes.InsertAt( 0, meshInp );
				}
				else
				{
					skinnedMeshInput.m_meshes.Clear();
				}
			}
			
			// Finally, process skinned meshes if we have any
			if( mergingSkinned )
			{
				meshGluer.Process_Sync( skinnedMeshInput, resource);
				wasModified = true;
			}

			if( wasModified )
			{
				m_rootResource->MarkModified();
			}
		}
	}

	void PhysicsEditorScene::MergeMeshesWithoutSkinning()
	{
		if( m_rootResource && !IsSingleMeshScene() )
		{
			CPhysicsDecorationResource* resource = (CPhysicsDecorationResource*)m_rootResource.Get();
			red::DynArray< PhysicsBasePtr > basePtrs = { red::PoolBackend() };
			m_physicsObjects.GetValues( basePtrs );

			GrowableMemoryBuffer memBuffer;
			MemoryBufferFileWriter writer( memBuffer );
			red::DynArray< PhysicsMeshPtr > meshesToMerge = { red::PoolBackend() };

			for( auto obj : basePtrs )
			{
				if( obj->GetBaseType() == EPBT_Mesh )
				{
					PhysicsMeshPtr meshPtr = Cast< PhysicsMesh >( obj );
					meshesToMerge.PushBack( meshPtr );
				}
			}

			// Glue all the unskinned meshes together, generating skinning
			mesh::MeshGluerInput meshesInput;
			meshesInput.m_generateSkinning = false;
			meshesInput.m_deleteSkinning = true;
			meshesInput.m_transform = true;

			mesh::MeshInput meshInp;
			TSystemObjectsMap actorsMap = { red::PoolBackend() };
			for( PhysicsMeshPtr physMesh : meshesToMerge )
			{
				THandle< CMesh > meshHandle = physMesh->GetMesh();

				meshInp.m_mesh = meshHandle.Get();
				meshInp.m_transform = physMesh->GetLocalToWorld();
				meshInp.m_customPhysics = PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( basePtrs, actorsMap, physMesh );
				meshesInput.m_meshes.PushBack( meshInp );
			}

			mesh::MeshGluer meshGluer;
			if( meshGluer.Process_Sync( meshesInput, resource ) )
			{
				resource->MarkModified();
			}
		}
	}

	physics::PhysicsBasePtr PhysicsEditorScene::FindByPhysicsId( const PhysicsBaseID physicsId )
	{
		tools::EditorObjectID globalId = GetEditorIdFromPhysicsBaseId( physicsId );

		return FindByGlobalId( globalId );
	}

	PhysicsBasePtr PhysicsEditorScene::FindByGlobalId( const tools::EditorObjectID globalId )
	{
		PhysicsBasePtr physBase;
		if( m_physicsObjects.Find( globalId, physBase ) )
		{
			return physBase;
		}

		// check actor shapes
		PhysicsBaseID parentId = 0;
		if( PhysicsShape::ExtractParentID( globalId.GetInternalID(), parentId ) )
		{
			tools::EditorObjectID parentGlobalId = GetEditorIdFromPhysicsBaseId( parentId );

			PhysicsBasePtr actorBasePtr;
			if( m_physicsObjects.Find( parentGlobalId, actorBasePtr ) )
			{
				if( actorBasePtr && ( actorBasePtr->GetBaseType() == EPBT_Actor ) )
				{
					PhysicsActorPtr actorPtr = red::StaticCast< PhysicsActor >( actorBasePtr );
					PhysicsBasePtr shape = actorPtr->FindShapeByGlobalID( globalId );

					if( shape )
					{
						return shape;
					}
				}
			}
		}

		// check actor COMs
		parentId = EditorActorCOM::ExtractParentID( globalId.GetInternalID() );
		if( parentId )
		{
			tools::EditorObjectID parentGlobalId = GetEditorIdFromPhysicsBaseId( parentId );

			PhysicsBasePtr actorBasePtr;
			if( m_physicsObjects.Find( parentGlobalId, actorBasePtr ) )
			{
				if( actorBasePtr && ( actorBasePtr->GetBaseType() == EPBT_Actor ) )
				{
					PhysicsActorPtr actorPtr = red::StaticCast< PhysicsActor >( actorBasePtr );
					return actorPtr->GetCOM();
				}
			}
		}


		return nullptr;
	}

	const red::DynArray< PhysicsBasePtr > PhysicsEditorScene::GetPhysicsObjectsOnScene() const
	{
		red::DynArray< PhysicsBasePtr > objectsArray = { red::PoolBackend() };
		m_physicsObjects.GetValues( objectsArray );
		std::sort( objectsArray.Begin(), objectsArray.End(), []( const PhysicsBasePtr& a, const PhysicsBasePtr& b )
		{
			return a->GetPhysicsObjectID() < b->GetPhysicsObjectID();
		} );
		return objectsArray;
	}

	const physics::TPhysicsObjectsMap& PhysicsEditorScene::GetPhysicsObjectsMap() const
	{
		return m_physicsObjects;
	}

	void PhysicsEditorScene::GenerateSkinning()
	{
		red::DynArray< PhysicsBasePtr > objects = GetPhysicsObjectsOnScene();

		red::DynArray< PhysicsMeshPtr > meshes;
		red::DynArray< PhysicsActorPtr > actors;

		// Extract meshes and actors on scene
		for( auto obj : objects )
		{
			if( obj->GetBaseType() == EPBT_Mesh )
			{
				meshes.PushBack( red::StaticCast< PhysicsMesh >( obj ) );
			}
			if( obj->GetBaseType() == EPBT_Actor )
			{
				actors.PushBack( red::StaticCast< PhysicsActor >( obj ) );
			}
		}
		const Uint32 actorCnt = actors.Size();

		// Generate skinning for each not skinned mesh based on found actors
		// TODO: Provide some option for selecting which mesh to skin with which actors, etc
		for( PhysicsMeshPtr mesh : meshes )
		{
			if( mesh->IsFlagSet( EPMF_IsSkinned ) ) continue;

			TResRef< CMesh > meshHandle = mesh->GetMesh();
			const auto& meshBinds = mesh->GetActorBindings();

			if( !meshHandle || meshBinds.Size() <= 0 )
			{
				// that's a corrupted mesh, we might as well remove it
				RemovePhysicsObject( mesh );
				continue;
			}
			
			// An unskinned mesh has one actor that defines it's collision and simulated movement
			auto meshActor = FindByPhysicsId( meshBinds[ 0 ].m_actorId );

			// Generate skinning 
			PhysicsBackendHelper::GenerateSkinningFromPhysicsObjects( mesh, meshHandle.GetSafe(), meshActor, actors );

			// Save the mesh to a new resource and switch the path in editor mesh that represents it
			String path = meshHandle->GetPath().ToString();
			auto arraySplit = path.Split( "." );
			path = arraySplit[ 0 ] + "_skinningGenerated." + arraySplit[ 1 ];
			res::ResourcePath skinnedPath = res::ResourcePath::Build( path );
			auto writer = res::GetEditorDepot()->CreateResourceWriter( skinnedPath );

			TSystemObjectsMap actorMap;
			auto physSystem = PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( objects, actorMap, mesh );
			PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( objects, actorMap, physSystem, mesh );
			// If we successfully created physSystem, save it to a mesh
			if( physSystem && physSystem->GetNumObjects() > 0 )
			{
				THandle< mesh::MeshParamPhysics > meshCustomPhys = meshHandle->CreateParameter< mesh::MeshParamPhysics >();
				meshCustomPhys->SetPhysicsData( physSystem );

				serialization::SavingContext savingContext( meshHandle.GetSafe() );
				mesh->SetMeshPath( path );

				auto saver = serialization::ISaver::CreateSaver( "w2mesh" );
				saver->SaveObjects( *writer.Get(), savingContext );
			}
		}
	}

	void PhysicsEditorScene::BindPhysicsActorToMesh( PhysicsBaseID actorId, PhysicsBaseID meshId, CName boneName )
	{
		PhysicsActorPtr actorPtr = Cast<PhysicsActor>( FindByPhysicsId( actorId ) );
		PhysicsMeshPtr meshPtr = Cast<PhysicsMesh>( FindByPhysicsId( meshId ) );

		// clear current bindings
		if( !actorPtr || !meshPtr ) 
			return;
		meshPtr->RemoveBoneBinding(boneName);
		DestroyPhysicsActorToMeshBinding( actorId );
		if( actorPtr && meshPtr )
		{
			PhysicsBackendHelper::BindPhysicsActorToMesh( actorPtr, meshPtr, boneName, (actorPtr->GetLocalToWorld() * meshPtr->GetLocalToWorld().FullInverted()).ToXform() );
		}
	}

	void PhysicsEditorScene::DestroyPhysicsActorToMeshBinding( PhysicsBaseID actorId )
	{
		PhysicsActorPtr actorPtr = Cast<PhysicsActor>( FindByPhysicsId( actorId ) );
		if( actorPtr )
		{
			PhysicsMeshBoneMapping map = actorPtr->GetBoneMap();
			if( map.m_meshId )
			{
				DestroyPhysicsActorToMeshBinding( map.m_meshId, map.m_boneName );
			}
		}		
	}

	void PhysicsEditorScene::DestroyPhysicsActorToMeshBinding( PhysicsBaseID meshId, CName boneName )
	{
		PhysicsMeshPtr meshPtr = Cast<PhysicsMesh>( FindByPhysicsId( meshId ) );
		if( meshPtr )
		{
			meshPtr->RemoveBoneBinding( boneName );
		}
	}

	void PhysicsEditorScene::SetVisible( bool isVisible )
	{
		auto& it = m_physicsObjects.Begin();
		auto& end = m_physicsObjects.End();
		while( it != end )
		{
			it.Value()->SetVisible( isVisible );
			++it;
		}
	}

	void PhysicsEditorScene::SetupInterop( PhysicsEditorScene* ptr, ::interop::utils::Object& op )
	{
		op.RegisterFunctor<void>( "generateSkinning", [ ptr ] ( ::interop::IContext& ctx )
		{
			ptr->GenerateSkinning();
		}
		);
	}

	void PhysicsEditorScene::NotifyOnRemoved()
	{
		for( const red::WeakPtr<tools::IMeshPhysicsStructureNode>& handlerWeak : m_notificationHandlers )
		{
			if( red::SharedPtr<tools::IMeshPhysicsStructureNode> handler = handlerWeak.Lock() )
			{
				handler->OnPhysicsObjectRemoved();
			}
		}
	}
}
