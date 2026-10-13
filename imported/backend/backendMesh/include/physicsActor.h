/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "physicsBase.h"
#include "physicsShape.h"

#include "../../../common/physics/include/physicsFilterData.h"
#include "../../../common/physics/include/physicsSystemCommon.h"

namespace physics
{
	//////////////////////////////////////////////////////////////////////////	 
	
	class PhysicsActor;
	class PhysicsShape;
	using PhysicsShapePtr = THandle< PhysicsShape >;
	using TPhysActorShapes = red::DynArray< PhysicsShapePtr >;

	//////////////////////////////////////////////////////////////////////////	 

	enum EPhysicsSimulation : Uint8
	{
		EPS_Static,
		EPS_Dynamic,
		EPS_Kinematic,
		EPS_Undefined,
		EPS_Max
	};

	class EditorActorCOM : public PhysicsBase 
	{
		RTTI_DECLARE_TYPE( EditorActorCOM );
		DECLARE_INTEROP_DERIVED( EditorActorCOM, PhysicsBase );

		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		EditorActorCOM();
		EditorActorCOM( PhysicsEditorScene* editorParent, PhysicsBaseID parentId, const Matrix& parentL2W );
		EditorActorCOM( EditorActorCOM& copyFrom );
		~EditorActorCOM();
		
		virtual void								OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options ) override;
		virtual void								PrepareForRendering( PhysicsRendererOptionsPtr options ) override;
		virtual void								SetLocalToWorld(const Matrix& localToWorld) override;

		void										CalculateLocalToWorld();
		void										OnParentLocalToWorldChanged(const Matrix& localToWorld);

		virtual String GetName() const override final;

		RED_FORCE_INLINE const IRenderSelectable*	GetSelectableObject() const
		{
			return static_cast<const IRenderSelectable*>( this );
		}

		static PhysicsBaseID						ExtractParentID(PhysicsBaseID id);
	public:
		Transform									m_comOffset;		// Center of mass space (in body local space)
		Vector3										m_inertia;			// Body inertia vector (in COM space, computed)
		Float										m_volume = 0.f;;			// Body volume (computed)
		Float										m_mass = 0.f;				// Body mass (computed)
	private:
		Bool										m_isSelected;
		Matrix										m_parentLocalToWorld;
		Float										m_actorCOMSize;
	};

	class BACKEND_MESH_API PhysicsMeshBoneMapping
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( PhysicsMeshBoneMapping )
	public:
		PhysicsMeshBoneMapping();
		PhysicsMeshBoneMapping( PhysicsBaseID id, CName boneName, Int32 boneIndex );
		~PhysicsMeshBoneMapping();

		Bool IsValid();

		Bool operator==( PhysicsMeshBoneMapping& maping );

	public:
		PhysicsBaseID				m_meshId;
		CName 						m_boneName;
		Int32						m_boneIndex;
	};

	class BACKEND_MESH_API PhysicsActor : public PhysicsBase
	{
		RTTI_DECLARE_TYPE( PhysicsActor );
		DECLARE_INTEROP_DERIVED( PhysicsActor, PhysicsBase );

	public:
		PhysicsActor();
		PhysicsActor( const THandle< PhysicsActor >& copyFrom );
		PhysicsActor( PhysicsBaseID id, PhysicsEditorScene* editorParent = nullptr );

		virtual ~PhysicsActor();

		virtual void								SetLocalToWorld( const Matrix& localToWorld ) override;

		// adds a shape to phys actor collection
		void										AddShape( PhysicsShapePtr shape );
		void										RemoveShape( PhysicsShapePtr shape );

		virtual void								OnUpdate() override;
		virtual void								OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options ) override;
		virtual void OnPostLoad( const PostLoadContext& context ) override;
		virtual void								OnRestore( PhysicsEditorScene* scene ) override;

		virtual void								PrepareForRendering( PhysicsRendererOptionsPtr options ) override;

		// toggle the selection for this PhysicsDecorationActor
		virtual void								ToggleSelectionVisualization( const Bool drawAsSelected ) override;

		PhysicsBasePtr								FindShapeByGlobalID( const tools::EditorObjectID globalId );
		PhysicsBasePtr								FindShapeByPhysicsId( const PhysicsBaseID physicsId );
		Uint32										FindShapeIndexByPhysicsId( const PhysicsBaseID physicsId );

		// Object serialization interface
		virtual Bool								OnPropertyPreChange( const rtti::AccessPath& path, red::SharedPtr<const rtti::ValueHolder>& inOutValue ) override;
		virtual void								OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		RED_INLINE physics::EPhysicsSimulation		GetSimulationType() const
		{
			return m_simType;
		}

		RED_INLINE void								SetSimulationType( physics::EPhysicsSimulation simType )
		{
			m_simType = simType;
		}

		RED_INLINE const physics::TPhysActorShapes&		GetActorShapes() const
		{
			return m_shapes;
		}

		RED_INLINE THandle<EditorActorCOM> GetCOM()
		{
			return m_actorCOM;
		}

		RED_INLINE Transform GetCOMOffset()
		{
			return m_actorCOM->m_comOffset;
		}

		RED_INLINE Vector3 GetInertia()
		{
			return m_actorCOM->m_inertia;
		}

		RED_INLINE Float GetMass()
		{
			return m_actorCOM->m_mass;
		}

		RED_INLINE Float GetVolume()
		{
			return m_actorCOM->m_volume;
		}

		RED_INLINE Float GetLinearDamping()
		{
			return m_linearDamping;
		}

		RED_INLINE Float GetAngularDamping()
		{
			return m_angularDamping;
		}

		RED_INLINE PhysicsMeshBoneMapping GetBoneMap()
		{
			return m_meshBoneMap;
		}

		RED_INLINE Uint32 GetSolverIterationsCountPosition()
		{
			return m_solverIterationsCountPosition;
		}

		RED_INLINE Uint32 GetSolverIterationsCountVelocity()
		{
			return m_solverIterationsCountVelocity;
		}

		RED_INLINE void SetBoneMap( PhysicsMeshBoneMapping newBoneMap )
		{
			m_meshBoneMap = newBoneMap;
		}

		RED_INLINE void SetLinearDamping( Float damping )
		{
			m_linearDamping = damping;
		}

		RED_INLINE void SetAngularDamping( Float damping )
		{
			m_angularDamping = damping;
		}

		RED_INLINE void SetSolverIterationsCountPosition( Uint32 solverIterPos )
		{
			m_solverIterationsCountPosition = solverIterPos;
		}

		RED_INLINE void SetsolverIterationsCountVelocity( Uint32 solverIterVelo )
		{
			m_solverIterationsCountVelocity = solverIterVelo;
		}

		RED_INLINE Bool IsQueryOnlyActor() const 
		{ 
			return m_isQueryOnlyActor;
		}

		RED_INLINE void SetIsQueryOnlyActor( Bool enable )
		{ 
			m_isQueryOnlyActor = enable;
		}
		
		virtual Bool IsValid();
		void ClearBoneMapping();
		void ClearActorShapes();

		virtual void OnRemove() override;
		Bool RecomputeCOM( );
		void SnapToBone();
		virtual String GetName() const override final;
		virtual void EvaluateSerializationDependencies( red::DynArray<THandle<ISerializable>>& dependenciesArray ) override;
		void NotifyChildrenOnLocalToWorldChanged();

		TPhysActorShapes							m_shapes;
		EPhysicsSimulation							m_simType;

		THandle<EditorActorCOM>						m_actorCOM;
		Float										m_linearDamping;	// Custom linear damping settings, 0=default
		Float										m_angularDamping;	// Custom angular damping settings, 0=default
		PhysicsMeshBoneMapping						m_meshBoneMap; // Bone that this actor is attached to
		Uint32			m_solverIterationsCountPosition; // 4=default
		Uint32			m_solverIterationsCountVelocity; // 1=default
		Uint32 			m_nextShapeId;
		Float m_maxDepenetrationVelocity = -1.0f;
		Float m_maxAngularVelocity = -1.0f;
		Float m_maxContactImpulse = -1.0f;
		Float m_actorSize;
		Bool m_isQueryOnlyActor;
	};

	using PhysicsActorPtr = THandle< PhysicsActor >;
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EPhysicsSimulation, physics );
