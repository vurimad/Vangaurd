/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "physicsBase.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"

class RenderProxyHandle;

namespace physics
{
	//////////////////////////////////////////////////////////////////////////	 

	class PhysicsActor;
	using PhysicsActorPtr = THandle< PhysicsActor >;

	//////////////////////////////////////////////////////////////////////////	 

	struct BACKEND_MESH_API SPhysicsActorBoneMapping
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE( SPhysicsActorBoneMapping )

		Matrix						m_boneToBody;
		Matrix						m_originalL2MInverted;
		PhysicsBaseID				m_actorId;
		Uint32						m_bone;
		CName 						m_boneName;

		SPhysicsActorBoneMapping()
			: m_boneToBody( Matrix::IDENTITY() )
			, m_actorId( 0 )
			, m_bone( 0 )
			, m_originalL2MInverted( Matrix::IDENTITY() )
		{}
	};

	enum EPhysicsMeshFlag : Uint32
	{
		EPMF_IsSkinned = EPBF_Reserved_1,
	};

	class BACKEND_MESH_API PhysicsMesh : public PhysicsBase
	{
		RTTI_DECLARE_TYPE( PhysicsMesh );
		DECLARE_INTEROP_DERIVED( PhysicsMesh, PhysicsBase );

	public:
		static const CName s_unskinnedFakeBoneDEPRECATED;

	public:
		PhysicsMesh();
		PhysicsMesh( const THandle< PhysicsMesh >& copyFrom );
		PhysicsMesh( PhysicsBaseID id, const String& meshPath, PhysicsEditorScene* editorParent = nullptr );

		virtual ~PhysicsMesh();

		virtual void										OnUpdate() override;
		virtual void										OnRender( rend::IDebugDrawer& dd, PhysicsRendererOptionsPtr options ) override;
		virtual void										OnRemove() override;
		virtual void OnPostLoad( const PostLoadContext& context ) override;

		virtual void										PrepareForRendering( PhysicsRendererOptionsPtr options ) override;

		void												ClearBindings();
		void												BindPhysicsActor( PhysicsBaseID actorId, const Matrix& invOriginalL2M, const Matrix& boneToBody, CName name, Int32 boneIndex = -1 );
		void												RemoveBoneBinding( CName name, Int32 boneIndex = -1 );
		const red::DynArray< SPhysicsActorBoneMapping >&	GetActorBindings() const
		{
			return m_actorMappings;
		}

		Bool												GetActorBinding(CName name, SPhysicsActorBoneMapping& binding);
		Bool												GetActorBinding(Uint32 index, SPhysicsActorBoneMapping& binding);
		Uint32												GetBoneCount();
		red::DynArray< CName >								GetBoneNames();
		Int32												GetBoneIndex( CName name );
		CName												GetBoneName( Uint32 index );
		Bool												GetFreeBoneName(CName& name); // Find a bone without mapped actor
		Matrix												GetBoneRigMatrix( CName name );
		Matrix												GetBoneRigMatrix( Uint32 index );

		RED_INLINE THandle< CMesh >							GetMesh()
		{
			return m_mesh.GetSafe();
		}
		void												SetMesh( THandle< CMesh > mesh );

		CName												GetAppearance( ) const;
		void												SetAppearance( CName appearanceName );
		void												SetForcedLOD( Int32 forcedLOD );
		const String										GetMeshPath() const;
		void												SetMeshPath( const String& val );

		const String										GetMeshName() const;
		virtual String										GetName() const override final;

		virtual void										ToggleSelectionVisualization( const Bool drawAsSelected ) override;
		virtual red::SharedPtr< tools::IEditableProxy >		CreateEditableProxy( Bool getParentProxy = true ) const override;

		virtual void SetVisible( Bool decision );
		virtual void SetLocalToWorld( const Matrix& localToWorld ) override;

		void UpdateBoundActorsTransforms( const Matrix& localToWorld );

		virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override;
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value ) override;

		virtual void EvaluateSerializationDependencies( red::DynArray< THandle< ISerializable > >& dependenciesArray ) override final;
		void GenerateBonePoses( red::DynArray<Matrix> &bonePoses );
	private:
		PhysicsActorPtr									GetActorFromId( PhysicsBaseID id ) const;
		void												UpdateSkinning();
		void												ReleaseProxy();
		void RefreshMeshBindings();
	private:
		Matrix m_parentPrevLocalToWorld;
		TResRef< CMesh >									m_mesh;
		mesh::AppearanceMaterialsRef m_appearanceMaterialsRef;
		CName												m_meshAppearance;
		red::DynArray< SPhysicsActorBoneMapping >			m_actorMappings;
		PhysicsRendererOptionsPtr							m_options;

		red::UniquePtr<RenderProxyHandle>					m_meshProxy;
		TRenderPtr<IRenderSkinningData>						m_skinningData;
		Int32												m_forcedLOD;
	};

	using PhysicsMeshPtr = THandle< PhysicsMesh >;
}


