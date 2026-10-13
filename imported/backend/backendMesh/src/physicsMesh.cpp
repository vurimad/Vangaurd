/* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.*/

#include "build.h"
#include "physicsMesh.h"
#include "physicsActor.h"
#include "physicsEditorScene.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/renderSkinningData.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

#include "../../backendEngine/include/editableProxy.h"

#include "../../../common/redReflection/include/variant.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/physics/include/physicsDestructionStructs.h"
#include "../../../common/renderData/include/effectContextUpdateData.h"

// SPhysicsActorBoneMapping

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsActorBoneMapping, physics );
RTTI_PROPERTY( m_bone ).readOnly();
RTTI_PROPERTY( m_boneName ).readOnly();
RTTI_PROPERTY( m_actorId ).readOnly();
RTTI_PROPERTY(m_boneToBody).readOnly();
RTTI_PROPERTY(m_originalL2MInverted).readOnly();
RTTI_END_TYPE();


// PhysicsMesh

RTTI_BEGIN_TYPE_IN_NAMESPACE( PhysicsMesh, physics );
RTTI_PARENT_TYPE( physics::PhysicsBase );
RTTI_PROPERTY_CATEGORY( "2. Mesh properties" );
RTTI_PROPERTY( m_mesh ).editable();
RTTI_PROPERTY( m_meshAppearance ).editable().customEditor( "MeshAppearance" ).tooltip( "Mesh appearance to be used in the output mesh - it will be merged to default appearance by mesh gluer." );
RTTI_PROPERTY( m_actorMappings ).readOnly();
RTTI_END_TYPE();

namespace physics
{
	const CName PhysicsMesh::s_unskinnedFakeBoneDEPRECATED = RED_NAME_CONSTEXPR( "rootNotSkinned" );

	PhysicsMesh::PhysicsMesh()
		: PhysicsBase( 0, EPBT_Mesh, nullptr )
		, m_mesh( nullptr )
		, m_meshAppearance( mesh::MeshAppearance::c_DefaultAppearanceName )
		, m_options( nullptr )
		, m_skinningData( nullptr )
		, m_meshProxy( nullptr )
	{
		SetFlag( EPMF_IsSkinned, false );
	}

	PhysicsMesh::PhysicsMesh( PhysicsBaseID id, const String& meshPath,PhysicsEditorScene* editorParent )
		: PhysicsBase( id, EPBT_Mesh, editorParent )
		, m_meshAppearance( mesh::MeshAppearance::c_DefaultAppearanceName )
		, m_options( nullptr )
		, m_skinningData( nullptr )
		, m_meshProxy( nullptr )
	{
		m_mesh.SetPath( res::ResourcePath::Build( meshPath ) );
		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );
		SetFlag( EPMF_IsSkinned, meshDataViewRO.GetNumBones() > 0 );
	}

	PhysicsMesh::PhysicsMesh( const THandle< PhysicsMesh >& copyFrom )
		: PhysicsBase( copyFrom->GetPhysicsObjectID(), EPBT_Mesh, copyFrom->GetEditorScene() )
		, m_mesh( copyFrom->m_mesh )
		, m_meshAppearance( mesh::MeshAppearance::c_DefaultAppearanceName )
		, m_options( nullptr )
		, m_skinningData( nullptr )
		, m_meshProxy( nullptr )
	{
		m_actorMappings = copyFrom->GetActorBindings();
		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );
		SetFlag( EPMF_IsSkinned, meshDataViewRO.GetNumBones() > 0 );
	}

	PhysicsMesh::~PhysicsMesh()
	{
	}

	void PhysicsMesh::OnUpdate()
	{
		PhysicsBase::OnUpdate();
		if( m_mesh )
		{
			if( IsFlagSet( EPMF_IsSkinned ) )
			{
				UpdateSkinning();
			}
			else
			{
				red::DynArray< Matrix > bonePoses = { red::PoolBackend() };
				GenerateBonePoses( bonePoses );
				if( m_meshProxy )
				{
					m_meshProxy->Relink( rend::RenderProxyTransform::SLOW_FromMatrix( bonePoses[0] ), m_bounds );
				}
			}
		}		
	}

	void PhysicsMesh::OnRender( rend::IDebugDrawer& debugDrawer, PhysicsRendererOptionsPtr options )
	{
		Bool isVisible = (IsFlagSet( EPBF_IsVisible ) && options->m_mesh );
		if( isVisible )
		{
			if( m_bounds == Box::EMPTY() )
			{
				m_bounds = m_mesh->GetBoundingBox();
			}
			PhysicsBase::OnRender( debugDrawer, options );
		}
		if( m_meshProxy )
			m_meshProxy->SetVisibility( isVisible );

		if( options->m_destructionBonds )
		{
			if( m_actorMappings.Size() > 0 )
			{
				if( IsFlagSet( physics::EPhysicsMeshFlag::EPMF_IsSkinned ) )
				{
					auto meshBondsParam = m_mesh->GetParameter<mesh::MeshParamDestructionBonds>();
					if( meshBondsParam )
					{
						Matrix meshL2WInverted = GetLocalToWorld().FullInverted();
						red::DynArray<Matrix> poses = { red::PoolFrame() };
						poses.Resize( m_actorMappings.Size() );
						Int32 selectedActorIndex = -1;
						for( Uint32 i = 0; i < m_actorMappings.Size(); ++i )
						{
							auto& mapping = m_actorMappings[ i ];
							auto& actor = GetActorFromId( mapping.m_actorId );
							if( actor->IsFlagSet( physics::EPhysicsBaseFlags::EPBF_IsSelected ) )
							{
								selectedActorIndex = i;
							}
							if( actor )
							{
								const Matrix& actorL2W = actor->GetLocalToWorld();
								poses[ i ] = actorL2W;
							}
						}
						for( auto& bond : meshBondsParam->GetDestructionBonds() )
						{
							physics::BondIndexHelper bondHelper( bond.m_bondIndex );
							if( bondHelper.m_a < poses.Size() && bondHelper.m_b < poses.Size() )
							{
								rend::FragmentBuilder builder( debugDrawer );
								builder.EnableTransparency( true );
								Color color = Color::CYAN( 128 );
								if( selectedActorIndex == bondHelper.m_a || selectedActorIndex == bondHelper.m_b )
								{
									color = Color::RED();
								}
								builder.SetColor( color );
								builder.EnableOverlay( true );
								builder.AddFatLine( poses[ bondHelper.m_a ].GetTranslation(), poses[ bondHelper.m_b ].GetTranslation(), 0.003f );
							}
						}
					}					
				}
			}
		}
	}

	void PhysicsMesh::OnRemove()
	{
		PhysicsBase::OnRemove();

		if( auto editorScene = GetEditorScene() )
		{
			for( auto mapping : m_actorMappings )
			{
				PhysicsActorPtr actor = GetActorFromId( mapping.m_actorId );
				if( actor )
				{
					editorScene->RemovePhysicsObject( actor );
				}
			}
		}	

		ReleaseProxy();
	}

	void PhysicsMesh::OnPostLoad( const PostLoadContext& context )
	{
		if( GRender )
		{
			m_appearanceMaterialsRef = m_mesh->GetAppearanceSafe( m_meshAppearance )->IssueLoadRenderMaterialSetup();
		}
	}

	PhysicsActorPtr PhysicsMesh::GetActorFromId( PhysicsBaseID id ) const
	{
		if( auto editorScene = GetEditorScene() )
		{
			return red::StaticCast< PhysicsActor >( editorScene->FindByPhysicsId( id ) );
		}
		return nullptr;
	}

	void PhysicsMesh::UpdateSkinning()
	{
		Uint32 boneCount = GetBoneCount();
		if( m_skinningData && m_skinningData->GetMatrixCount() != boneCount )
		{
			m_skinningData = nullptr;
		}

		if( !m_meshProxy )
			return;

		// Recreate skinning data if needed
		if( !m_skinningData )
		{
			m_skinningData = GRender->CreateSkinningBuffer();
			m_skinningData->KeepDataAlive( true );

		}

		if( m_mesh->GetParameter<mesh::MeshParamDestructionChunkIndicesOffsets>() )
		{
			red::BitSet64<256> activeBones;
			activeBones.SetAll();

			SMaterialParameterEffectContextUpdate updateInfo;

			constexpr Uint8 c_paramSlotFirst = 3;
			const Uint8 c_paramSlotCount = 2;
			RED_ASSERT( RED_ARRAY_COUNT( updateInfo.m_parameters ) >= c_paramSlotFirst + c_paramSlotCount );
			updateInfo.m_paramMask = ECMP_CustomParam3 | ECMP_CustomParam4;
			red::Memcpy( &updateInfo.m_parameters[ c_paramSlotFirst ], activeBones.Data(), sizeof( Uint64 ) * 4 );
			m_meshProxy->UpdateEffectInfo( updateInfo );
		}

		if( m_skinningData )
		{
			RenderSkinningWrite skinWrite = m_skinningData->GetWriteData( SkinningDataRequest::NoLOD( boneCount ) );
			if( skinWrite.IsValid() )
			{
				red::DynArray< Matrix > bonePoses = {red::PoolBackend()};
				bonePoses.Resize( boneCount );
				GenerateBonePoses( bonePoses );
				for( Int32 i = 0; i < (Int32)boneCount; i++ )
				{
					const Int32 boneIndex = i;
					const Matrix& actorMat = bonePoses[boneIndex];
					skinWrite.Set( i, actorMat );
				}

				m_skinningData->AdvanceWrite();
				m_meshProxy->RelinkAndSkin( rend::RenderProxyTransform::SLOW_FromMatrix( GetLocalToWorld() ), m_bounds, m_skinningData, nullptr );
			}
		}
	}

	void PhysicsMesh::GenerateBonePoses( red::DynArray<Matrix> &bonePoses )
    {
		bonePoses.Clear();
		bonePoses.Resize( GetBoneCount() );
		for( Uint32 i : bonePoses.Indices() )
		{
			bonePoses[i] = Matrix::IDENTITY();
		}
		if( m_actorMappings.Size() > 0 )
		{
			m_bounds = Box::EMPTY();
			if( IsFlagSet( physics::EPhysicsMeshFlag::EPMF_IsSkinned ) )
			{
				Matrix meshL2WInverted = GetLocalToWorld().FullInverted();
				for( auto& mapping : m_actorMappings )
				{
					if( bonePoses.Size() <= mapping.m_bone )
						continue;
					auto& actor = GetActorFromId( mapping.m_actorId );
					if( actor )
					{
						const Matrix& actorL2W = actor->GetLocalToWorld();

						// Transform to model space
						bonePoses[mapping.m_bone] = actorL2W * meshL2WInverted;
						bonePoses[mapping.m_bone] = mapping.m_originalL2MInverted * bonePoses[mapping.m_bone];
						bonePoses[mapping.m_bone] = mapping.m_boneToBody * bonePoses[mapping.m_bone];
						Box& actorBounds = actor->GetBoundsWorld();
						if( actorBounds == Box::EMPTY() )
						{
							m_bounds.AddPoint( actorL2W.GetTranslation() );
						}
						else
						{
							m_bounds.AddBox( actor->GetBoundsWorld() );
						}

					}
				}
			}
			else
			{
				const auto& mapping = m_actorMappings[0];
				const auto& actor = GetActorFromId( mapping.m_actorId );
				Matrix l2w = actor->GetLocalToWorld();
				bonePoses[0] = mapping.m_boneToBody * l2w;
				m_bounds.AddBox( actor->GetBoundsWorld() );
				if( m_bounds == Box::EMPTY() )
				{
					m_bounds = l2w.TransformBox( m_mesh->GetBoundingBox() );
				}
			}
		}
	}

	void PhysicsMesh::PrepareForRendering( PhysicsRendererOptionsPtr options )
	{
		PhysicsBase::PrepareForRendering( options );
	
		if( !IsFlagSet( EPBF_RequestRenderingRebuild ) )
		{
			return;
		}

		if( !m_appearanceMaterialsRef.HasFinished() )
		{
			return;
		}

		m_options = options;

		RED_ASSERT( m_options, "Renderer options released before renderer, uncool." );

		// release current proxy
		ReleaseProxy();

		// we need mesh to create visualization :)
		if( !m_mesh.Empty() && m_options )
		{
			THandle< mesh::MeshAppearance > appearance = m_mesh->GetAppearanceSafe( m_meshAppearance );

			RenderProxyMeshComponentInitData meshInfo;
			meshInfo.m_isCharacterShadowFallback = false;
			meshInfo.m_hasMeshResource = nullptr != m_mesh.Get();
			if( meshInfo.m_hasMeshResource )
			{
				meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
				meshInfo.m_renderMesh = m_mesh->GetRenderResource();
				meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
				meshInfo.m_setupsNum = 1;
			}
			else
			{
				meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
				meshInfo.m_renderMesh = nullptr;
				meshInfo.m_setupsNum = 0;
			}

			meshInfo.m_componentFriendlyName = "<noname>";
			meshInfo.m_forcedLODLevel = m_forcedLOD;
			meshInfo.m_isCastingGlobalShadows = m_mesh ? m_mesh->GetCastGlobalShadows() : false;
			meshInfo.m_isCastingLocalShadows = m_mesh ? m_mesh->GetCastLocalShadows() : false;
			meshInfo.m_isTwoSided = false;
			meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

			// drawable component
			meshInfo.m_isVisible = true;
			meshInfo.m_isDynamicGeometryComponent = true;
			meshInfo.m_isForceNoAutohide = IsFlagSet(EPBF_SimulationRunning);
			meshInfo.m_autoHideDistanceNear = 0.0f;
			meshInfo.m_autoHideDistanceFar = m_mesh ? m_mesh->GetAutoHideDistance() : 25.f;
			meshInfo.m_renderingPlane = RPl_Scene;
			meshInfo.m_objectTypeID = m_mesh ? m_mesh->GetRenderObjectType() : 0;

			// base
			meshInfo.m_type = RPT_Mesh;
			meshInfo.m_localToWorld = rend::RenderProxyTransform::SLOW_FromMatrix( m_localToWorld );
			m_bounds = m_mesh ? m_mesh->GetBoundingBox() : Box( Vector4::ZERO_3D_POINT(), 1.f );
			meshInfo.m_boundingBox = m_localToWorld.TransformBox( m_bounds );

			RenderProxyInitInfo initInfo;
			initInfo.m_componentData = &meshInfo;

			// create proxy
			auto proxy = GRender->CreateProxy( initInfo );
			m_meshProxy = red::CreateUniquePtr< RenderProxyHandle >( proxy );
			m_meshProxy->Initialize( m_options->m_renderScene, true );


			if( m_meshProxy && GetSelectableObject() )
				m_meshProxy->BindSelectable( GetSelectableObject() );

			// add proxy to rendering scene
			m_meshProxy->Attach();
		}

		SetFlag( EPBF_RequestRenderingRebuild, false );
	}

	void PhysicsMesh::ClearBindings()
	{
		m_actorMappings.Clear();
		SetFlag( EPMF_IsSkinned, false );
	}

	void PhysicsMesh::BindPhysicsActor( PhysicsBaseID actorId, const Matrix& invOriginalL2M, const Matrix& boneToBody, CName name, Int32 boneIndex )
	{
		if( auto editorScene = GetEditorScene() )
		{
			if( boneIndex < 0 )
			{
				boneIndex = GetBoneIndex( name );

				if( boneIndex < 0 )
				{
					// no matching bone to create the binding
					return;
				}
			}

			RemoveBoneBinding( name, boneIndex );
			SPhysicsActorBoneMapping mapping;
			mapping.m_actorId = actorId;
			mapping.m_bone = boneIndex;
			mapping.m_boneToBody = boneToBody;
			mapping.m_originalL2MInverted = invOriginalL2M;
			mapping.m_boneName = name;
			m_actorMappings.PushBack( mapping );

			auto& actorHandle = GetActorFromId( actorId );
			auto& thisHandle = editorScene->FindByPhysicsId( m_physicsBaseId );
			if( actorHandle && thisHandle )
			{
				actorHandle->RegisterListener( thisHandle );
				actorHandle->SetBoneMap( { m_physicsBaseId, name, boneIndex } );
			}
		}	
	}

	void PhysicsMesh::RemoveBoneBinding( CName name, Int32 boneIndex )
	{
		if( auto editorScene = GetEditorScene() )
		{
			Int32 indexToRemove = -1;
			for( auto index : m_actorMappings.Indices() )
			{
				const SPhysicsActorBoneMapping& mapping = m_actorMappings[ index ];
				if( mapping.m_boneName == name || ( boneIndex >= 0 && mapping.m_bone == (Uint32)boneIndex ) )
				{
					indexToRemove = index;
					break;
				}
			}

			if( indexToRemove >= 0 )
			{
				const SPhysicsActorBoneMapping& mapping = m_actorMappings[ indexToRemove ];
				PhysicsActorPtr actor = GetActorFromId( mapping.m_actorId );
				if( actor )
				{
					actor->ClearBoneMapping();
					auto& thisHandle = editorScene->FindByPhysicsId( m_physicsBaseId );
					if( thisHandle )
					{
						actor->UnregisterListener( thisHandle );
					}
				}
				m_actorMappings.RemoveAt( indexToRemove );
			}
		}		
	}

	Bool PhysicsMesh::GetActorBinding(CName name, SPhysicsActorBoneMapping& binding)
	{
		for (auto index : m_actorMappings.Indices())
		{
			const SPhysicsActorBoneMapping& mapping = m_actorMappings[index];
			if (mapping.m_boneName == name)
			{
				binding = mapping;
				return true;
			}
		}
		return false;
	}

	Bool PhysicsMesh::GetActorBinding(Uint32 index, SPhysicsActorBoneMapping& binding)
	{
		for (auto boneIndex : m_actorMappings.Indices())
		{
			const SPhysicsActorBoneMapping& mapping = m_actorMappings[boneIndex];
			if (mapping.m_bone == index)
			{
				binding = mapping;
				return true;
			}
		}
		return false;
	}

	Uint32 PhysicsMesh::GetBoneCount()
	{
		const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );
		return math::Max<Uint32>( meshDataViewRO.GetNumBones(), 1 );
	}

	red::DynArray< CName > PhysicsMesh::GetBoneNames()
	{
		red::DynArray< CName > boneNames = { red::PoolBackend() };
		if( !m_mesh.Empty() )
		{
			const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );

			Uint32 boneCount = meshDataViewRO.GetNumBones();
			if( boneCount > 0 )
			{
				// Go through mesh bones until we find one with provided name
				for( Uint32 i = 0; i < boneCount; ++i )
				{
					boneNames.PushBack( meshDataViewRO.GetBoneName( i ) );
				}
			}
			else
			{
				boneNames.PushBack( CName::NONE() );
			}
		}

		return boneNames;
	}

	CName PhysicsMesh::GetBoneName( Uint32 index )
	{
		if( IsFlagSet( EPMF_IsSkinned ) )
		{
			if( !m_mesh.Empty() )
			{
				// Create a view of the mesh
				const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );
				return meshDataViewRO.GetBoneName( index );
			}
		}
		else if( index == 0 )
		{			
			return CName::NONE();
		}
		return CName::NONE();
	}

	Bool PhysicsMesh::GetFreeBoneName( CName& name )
	{
		const red::DynArray< CName >& boneNames = GetBoneNames();
		if( m_actorMappings.Size() != boneNames.Size() )
		{			
			for( const CName& boneName : boneNames )
			{
				red::DynArray< SPhysicsActorBoneMapping >::iterator found = std::find_if( m_actorMappings.Begin(), m_actorMappings.End(), [ boneName ] ( SPhysicsActorBoneMapping& mapping )
				{
					return mapping.m_boneName == boneName;
				} );
				if( found == m_actorMappings.End() )
				{
					name = boneName;
					return true;
				}
			}		
		}		
		return false;
	}

	Matrix PhysicsMesh::GetBoneRigMatrix( CName name )
	{
		Int32 boneIndex = GetBoneIndex( name );
		if( boneIndex >= 0 )
		{
			return GetBoneRigMatrix( boneIndex );
		}
		return Matrix::IDENTITY();
	}

	Matrix PhysicsMesh::GetBoneRigMatrix( Uint32 boneIndex )
	{
		if( IsFlagSet( EPMF_IsSkinned ) )
		{
			if( !m_mesh.Empty() )
			{
				// Create a view of the mesh
				const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );
				return meshDataViewRO.GetBoneMatrix( boneIndex );
			}
		}

		return Matrix::IDENTITY();
	}

	Int32 PhysicsMesh::GetBoneIndex( CName name )
	{
		Int32 boneIndex = -1;
		if( IsFlagSet( EPMF_IsSkinned ) )
		{
			if( !m_mesh.Empty() )
			{
				// Create a view of the mesh
				const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *m_mesh.Get() );

				Uint32 boneCount = meshDataViewRO.GetNumBones();
				if( boneCount > 0 )
				{
					// Go through mesh bones until we find one with provided name
					for( Uint32 i = 0; i < boneCount; ++i )
					{
						if( meshDataViewRO.GetBoneName( i ) == name )
						{
							boneIndex = i;
							break;
						}
					}
				}
			}
		}
		else if( name == CName::NONE() )
		{
			// If the mesh has no bones, then we want to create a mapping and the boneIndex doesn't matter
			boneIndex = 0;
		}
		return boneIndex;
	}

	void PhysicsMesh::SetMesh( THandle< CMesh > mesh )
	{
		m_mesh = mesh;
		RefreshMeshBindings();
	}

	CName PhysicsMesh::GetAppearance() const
	{
		return m_meshAppearance;
	}

	void PhysicsMesh::SetAppearance( CName appearanceName )
	{
		m_meshAppearance = appearanceName;
		SetFlag( EPBF_RequestRenderingRebuild, true );
	}

	void PhysicsMesh::SetForcedLOD( Int32 forcedLOD )
	{
		m_forcedLOD = forcedLOD;
		SetFlag( EPBF_RequestRenderingRebuild, true );
	}

	const String PhysicsMesh::GetMeshPath() const
	{
		if( m_mesh.IsValid() )
		{
			return m_mesh->GetPath().ToString();
		}
		return String::EMPTY();
	}

	void PhysicsMesh::SetMeshPath( const String& val )
	{
		m_mesh.SetPath( res::ResourcePath::Build( val ) );
		RefreshMeshBindings();
	}

	const String PhysicsMesh::GetMeshName() const
	{
		String meshPath = GetMeshPath();
		meshPath = meshPath.StringAfterFromRight( "\\" );
		return meshPath;
	}

	String PhysicsMesh::GetName() const
	{
		return String::Printf( "Mesh (ID: %d ), %hs", GetPhysicsObjectID(), GetMeshName().AsChar() );
	}

	void PhysicsMesh::ToggleSelectionVisualization( const Bool drawAsSelected )
	{
		SetFlag( EPBF_IsSelected, drawAsSelected );
		if( m_meshProxy )
		{
			m_meshProxy->SetSelection( drawAsSelected );
		}
	}

	red::SharedPtr< tools::IEditableProxy > PhysicsMesh::CreateEditableProxy( Bool getParentProxy  ) const
	{
		if( !GetEditorScene()->IsSingleMeshScene() && m_actorMappings.Size() > 0 )
		{
			const auto& actor = GetActorFromId( m_actorMappings[ 0 ].m_actorId );
			return actor->CreateEditableProxy();
		}
		return nullptr;
	}

	void PhysicsMesh::SetVisible( Bool decision )
	{
		if( decision != IsFlagSet( physics::EPBF_IsVisible ) )
		{
			SetFlag( physics::EPBF_IsVisible, decision );
			if( m_meshProxy )
			{
				m_meshProxy->SetVisibility( decision );
				m_skinningData = nullptr;
			}
		}
	}

	void PhysicsMesh::SetLocalToWorld( const Matrix& localToWorld )
	{
		UpdateBoundActorsTransforms( localToWorld );
		PhysicsBase::SetLocalToWorld( localToWorld );
	}

	void PhysicsMesh::UpdateBoundActorsTransforms( const Matrix& localToWorld )
	{
		Matrix currentL2WInv = m_localToWorld.OrthonormInverted();
		// Not really optimized, but it's editor only and should be called only when dragging mesh around
		for( auto mapping : m_actorMappings )
		{
			PhysicsActorPtr actor = GetActorFromId( mapping.m_actorId );
			if( actor )
			{
				Matrix actorL2W = actor->GetLocalToWorld();
				actorL2W = actorL2W * currentL2WInv;
				actorL2W = actorL2W * localToWorld;
				actor->SetLocalToWorld( actorL2W );
			}
		}
	}

	Bool PhysicsMesh::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
	{
		if( propertyName == RED_NAME_CONSTEXPR_NOREG( "meshPath" ) )
		{
			String oldParams;
			RED_VERIFY( readValue.Get( oldParams ) );
			SetMeshPath( oldParams );
			return true;
		}
		return PhysicsBase::OnPropertyMissing( propertyName, readValue );
	}

	void PhysicsMesh::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		PhysicsBase::OnPropertyPostChange( path, oldValue, value );

		CName propName;
		rtti::PathParser parser( path );

		if( parser.EatName( propName ) )
		{
			if( propName == RED_NAME_CONSTEXPR_NOREG( "mesh" ) )
			{
				ReleaseProxy();
				if( m_mesh )
				{
					RefreshMeshBindings();
					SetFlag( EPBF_RequestRenderingRebuild, true );
				}
			}
		}
	}

	void PhysicsMesh::EvaluateSerializationDependencies( red::DynArray< THandle< ISerializable > >& dependenciesArray )
	{
		if( auto editorScene = GetEditorScene() )
		{
			for( auto mapping : m_actorMappings )
			{
				const auto& actorHandle = editorScene->FindByPhysicsId( mapping.m_actorId );
				dependenciesArray.PushBack( actorHandle );
			}
		}
	}

	void PhysicsMesh::ReleaseProxy()
	{
		m_meshProxy.Reset();
	}

	/*
	/ Method used when we change the mesh to fix the bindings to actors.
	/ It will try to rebind existing actors to bones in the dropped mesh.
	/ If the original mesh had more bones and thus actors, the ones overlaping will
	/ be set free.
	*/
	void PhysicsMesh::RefreshMeshBindings()
	{
		red::DynArray< SPhysicsActorBoneMapping > currentActorMappings = std::move( m_actorMappings );
		Uint32 currentMappings = currentActorMappings.Size();
		Uint32 boneCount = GetBoneCount();
		for( Uint32 i = 0; i < currentMappings; ++i )
		{
			SPhysicsActorBoneMapping& binding = currentActorMappings[ i ];
			if(  i < boneCount )
			{
				CName& boneName = GetBoneName( i );
				BindPhysicsActor( binding.m_actorId, binding.m_originalL2MInverted, binding.m_boneToBody, boneName, i );
			}
			else
			{
				RemoveBoneBinding( binding.m_boneName, binding.m_bone );
			}
		}

		SetFlag( EPBF_RequestRenderingRebuild, true );
	}

	void PhysicsMesh::SetupInterop( PhysicsMesh* ptr, ::interop::utils::Object& op )
	{

	}
}

