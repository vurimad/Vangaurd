/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/physics/include/physicsDestructionStructs.h"
#include "../../../common/worldEntities/include/physicalDestructionUtils.h"
#include "../../../common/physics/include/physicsSystemCommon.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../backendEngine/include/objectCollection.h"

namespace tools
{
	class IMeshPhysicsStructureNode;
	class ResourceBackend;
	struct MarkVerticesHandlerOptions;
}

namespace physics
{
	class PhysicsActor;
	class PhysicsBase;
	class IPhysicsNotificationHandler;
	class ICollider;
	struct PhysicsShapeRendererOptions;
	using PhysicsActorPtr = THandle< PhysicsActor >;
	using PhysicsBasePtr = THandle< PhysicsBase >;
	using PhysicsBaseID = tools::EditorObjectInternalID;
	using PhysicsRendererOptionsPtr = THandle< PhysicsShapeRendererOptions >;
	using TPhysicsObjectsMap = red::HashMap< tools::EditorObjectID, PhysicsBasePtr >;

	enum PhysicsSimulationFPS : Uint8
	{
		FPS_5,
		FPS_15,
		FPS_30,
		FPS_60
	};

	class PhysicsSimulationOptions : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( PhysicsSimulationOptions );

	public:
		PhysicsSimulationOptions();
		~PhysicsSimulationOptions();
		Float GetPhysicsStep();

		DestructionParams				m_destructionParams;
		red::DynArray< physics::DestructionLevelData > m_destructionLevelData = { red::PoolEngine() };
		Float							m_groundPlanePosition;
		Float							m_groundPlaneSize;
		Float							m_maxJitterRange;		
		Float							m_thrownObjectVelocity;
		Float							m_thrownObjectMass;
		Float							m_impulseMagnitude = 10.0f;
		Float							m_impulseRadius = 0.f;
		Bool							m_thrownObjectCustomMass;
		Bool							m_enableJitter;		
		Bool							m_startInactive = true;
		Bool							m_simulateAsDestruction = false;
		physics::FilterDataSource		m_filterDataSource = physics::FilterDataSource::Collider;
		PhysicsSimulationFPS			m_simulationFPS;
		THandle< physics::ICollider > m_throwableShape;
	};

	using PhysicsSimOptionsPtr = THandle< physics::PhysicsSimulationOptions>;

	class BACKEND_MESH_API PhysicsEditorOptions : public ISerializable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( PhysicsEditorOptions )

	public:
		PhysicsEditorOptions();
		~PhysicsEditorOptions();

		PhysicsRendererOptionsPtr	m_renderOptions;
		PhysicsSimOptionsPtr		m_simulationOptions;
		THandle<tools::MarkVerticesHandlerOptions> m_markVerticesHandlerOptions;

		red::UniquePtr<tools::ObjectCollection> m_renderOptionsCollection = nullptr;
		red::UniquePtr<tools::ObjectCollection> m_simulationOptionsCollection = nullptr;
		red::UniquePtr<tools::ObjectCollection> m_markVerticesHandlerOptionsCollection = nullptr;

		interop::InteropNotifier m_onRenderOptionsChanged;
	};

	class BACKEND_MESH_API PhysicsEditorScene : public ISerializable
	{
		RTTI_DECLARE_TYPE( PhysicsEditorScene );
		DECLARE_INTEROP( PhysicsEditorScene );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~PhysicsEditorScene();

		void											OnUpdate( Float timeDelta );
		void											OnRender( rend::IDebugDrawer& dd ) const;
		void											PrepareForRendering( ) const;

		/// Get depot path
		RED_FORCE_INLINE const String&					GetRootDirectory() const { return m_rootDirectory; }

		/// Get the root resource  - should not be used to often
		RED_FORCE_INLINE const THandle< CResource >&	GetRootResource() const { return m_rootResource; }

		/// Initialize physics view for given resource 
		static THandle<PhysicsEditorScene>				Create( const THandle< CResource >& res, CName objectType, Bool isSingleMesh, const THandle<PhysicsEditorOptions>& physicsEditorOptions );
		void											MergeMeshes();
		void											MergeMeshesWithoutSkinning();
		PhysicsBasePtr									FindByPhysicsId( const PhysicsBaseID physicsId );
		PhysicsBasePtr									FindByGlobalId( const tools::EditorObjectID globalId );

		void											PlacePhysicsObject( PhysicsBasePtr physObjects );
		void											PlacePhysicsObject( PhysicsBasePtr physObjects, red::DynArray< tools::EditorObjectID >& outActualCreatedIDs );
		void											PlacePhysicsObjects( red::DynArray< PhysicsBasePtr >& physObjects );
		void											PlacePhysicsObjects( red::DynArray< PhysicsBasePtr >& physObjects, red::DynArray< tools::EditorObjectID >& outActualCreatedIDs );
		Bool											RemovePhysicsObject( const tools::EditorObjectID id );
		void											RemovePhysicsObject( PhysicsBasePtr actor );
		void											SnapPhysicsObject( tools::EditorObjectID id, Bool alternativeMode = false );
		void											RecomputeCOM( const red::DynArray< tools::EditorObjectID >& id );
		red::DynArray<Uint32>							GenerateDefaultDestructionHierarchy();
		void											SetDestructionHierarchy(const red::DynArray<Uint32>& hierarchy);

		const tools::EditorObjectID                     FindParentId( const tools::EditorObjectID& m_ids );
		tools::EditorObjectID							GetEditorIdFromPhysicsBaseId( PhysicsBaseID baseId );
		PhysicsBaseID									GetNextFreeId();
		const red::DynArray< PhysicsBasePtr >			GetPhysicsObjectsOnScene() const;
		const TPhysicsObjectsMap&						GetPhysicsObjectsMap() const;

		void											GenerateSkinning();
		void											BindPhysicsActorToMesh( PhysicsBaseID actorId, PhysicsBaseID meshId, CName boneName );
		void											DestroyPhysicsActorToMeshBinding( PhysicsBaseID actorId );
		void											DestroyPhysicsActorToMeshBinding( PhysicsBaseID meshId, CName boneName );

		void SetResource( const THandle< CResource >& res );

		void AttachNotificationHandler( const red::SharedPtr<tools::IMeshPhysicsStructureNode>& handler );
		void SetVisible( bool isVisible );

		// Is the scene a single mesh scene (represents mesh param system resource) or is it merged mesh scene pre-merge?
		RED_FORCE_INLINE Bool IsSingleMeshScene() const
		{
			return m_singleMeshScene;
		}

		RED_FORCE_INLINE CName GetPhysicsObjectType()
		{
			return m_objectTypePhysics;
		}

		void SerializeBuffer();
		void NotifyOnRemoved();
		
		// physics editor options - shared between scenes
		THandle<PhysicsEditorOptions> m_physicsEditorOptions;
	private:
		PhysicsEditorScene( );
		void											DeserializeBuffer();
		Bool											AddPhysicsObject( PhysicsBasePtr objectPtr );


		// path to the root directory
		String											m_rootDirectory;
		CName m_objectTypePhysics; // used to bind editor scene to a resolver

		// root resource
		THandle< CResource >							m_rootResource;

		TPhysicsObjectsMap								m_physicsObjects;
		red::DynArray< red::WeakPtr<tools::IMeshPhysicsStructureNode> > m_notificationHandlers;

		PhysicsBaseID							m_lastObjectID;
		Bool m_singleMeshScene;
	};
}


