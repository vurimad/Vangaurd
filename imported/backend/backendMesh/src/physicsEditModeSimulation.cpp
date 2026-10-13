#include "build.h"

#include "physicsEditModeSimulation.h"
#include "physicsEditorScene.h"
#include "physicsObjectResolver.h"
#include "physicsBase.h"
#include "physicsMesh.h"
#include "physicsBackendHelper.h"
#include "backendMeshPreviewPanel.h"
#include "meshEditModes.h"

#include "../../backendEngine/include/objectHandler.h"
#include "../../backendEngine/include/objectPersistentCollection.h"
#include "../../backendEngine/include/previewPanelSharedData.h"
#include "../../backendEngine/include/previewInputListener.h"

#include "../../../common/engine/include/systemsUpdater.h"

#include "../../../common/physics/include/physicsTraceData.h"
#include "../../../common/physics/include/physicsSystemUtils.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSceneStructs.h"
#include "../../../common/physics/include/physicsTransformState.h"
#include "../../../common/physics/include/physicsProxy.h"
#include "../../../common/physics/include/physicsScene.h"
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsSystemBody.h"

#include "../../../common/world/include/worldRuntimeScene.h"
#include "../../../common/world/include/runtimeInfo.h"
#include "../../../common/world/include/runtimeSystemRendering.h"
#include "../../../common/worldPhysics/include/runtimeSystemPhysics.h"
#include "../../../common/worldEntities/include/physicalMeshComponent.h"
#include "../../../common/worldEntities/include/runtimeSystemDestruction.h"

#include "../../../common/renderData/include/renderSkinningData.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"
#include "../../../common/renderData/include/renderWorldTransform.h"
#include "../../../common/renderData/include/effectContextUpdateData.h"
#include "../../../common/worldEntities/include/destructionInterface.h"

#ifdef PHYSICS_DEBUG
#pragma optimize("", off)
#endif

namespace tools
{
	struct PhysicsObject : public physics::ITransformState
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		physics::ProxyID m_proxyID = physics::ProxyID();
		physics::TransformBufferPtr	m_transformBuffer;
		THandle< physics::SystemResource > m_physicsData;
		CName m_meshAppearance = CName::NONE();

		virtual void Pull( const physics::TransformBuffer::State * reader, Uint32 count, const WorldTransform& ) override
		{};
	};

	struct MeshScene : public PhysicsObject
	{
		red::DynArray< physics::BodyMapping> m_bodyMapping = { red::PoolEngine() };

		world::RuntimeSystemDestruction* m_destructionSystem = nullptr;
		THandle<CMesh> m_meshResource = nullptr;
		TRenderPtr<IRenderSkinningData> m_skinningData;
		RenderProxyPtr m_renderProxy = nullptr;
		red::BitSet64<256> m_activeBones;
		red::BitSet64<256> m_isEdge;
		Uint64 m_ibCacheKey = 0;
		Uint32 m_numBones;
		Bool m_isSkinned = false;
		Bool m_isDestructible = false;
		Bool m_hasEdgeMaterialSupport = false;
		Bool m_useDeprecatedSkinning = false;

		virtual void Pull( const physics::TransformBuffer::State * reader, Uint32 count, const WorldTransform& ) override final;
	};

	class PhysicsEditModeSimulation::impl
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
	public:
		red::UniquePtr<SystemsUpdater> m_processor = nullptr;
		red::UniquePtr<world::RuntimeScene> m_runtimeScene = nullptr;
		physics::SceneID m_sceneID = physics::SceneID();

		physics::PhysicsSimOptionsPtr m_simulationOptions;
		physics::PhysicsEditorScene* m_editorScene;
		world::RuntimeSystemDestruction* m_destructionSystem = nullptr;

		MeshScene m_meshScene;
		PhysicsObject m_ground;
		PhysicsObject m_throwable;

		Vector3	m_cameraPosition = Vector3::ZEROS();

		Bool m_isPaused = true;
		Bool m_stepRequested = true;
		Bool m_recreateProxies = false;
	};

	void MeshScene::Pull( const physics::TransformBuffer::State * reader, Uint32 count, const WorldTransform& )
	{
		if( m_isSkinned )
		{
			RED_FATAL_ASSERT( m_bodyMapping.Size() >= count, "Error: Trying to update more bodies than there is in the mapping." );
			if( m_isDestructible )
			{
				ent::PhysicalDestructionSkinningUpdateData skinningData =
				{
					WorldTransform::IDENTITY(),
					m_renderProxy,
					m_destructionSystem,
					m_proxyID,
					m_skinningData,
					m_meshResource,
					m_ibCacheKey,
					CName::NONE(),
					{ m_bodyMapping },
					m_activeBones,
					m_isEdge,
					Vector3::ONES(),
					m_numBones,
					m_hasEdgeMaterialSupport
				};

				//////////////////////////////////////////////////////////////////////////
				// TODO: remove when all meshes are reimported
				if( m_useDeprecatedSkinning )
				{
					ent::DEPRECATED_UpdateSkinning_PhysicalDestruction( reader, skinningData );
					return;
				}
				//////////////////////////////////////////////////////////////////////////

				ent::UpdateSkinning_PhysicalDestruction( reader, skinningData );
			}
			else
			{
				if( m_skinningData && m_skinningData->GetMatrixCount() != m_numBones )
				{
					m_skinningData = nullptr;
				}

				// Recreate skinning data if needed
				if( !m_skinningData )
				{
					m_skinningData = GRender->CreateSkinningBuffer();
					m_skinningData->KeepDataAlive( true );
				}

				if( m_skinningData )
				{
					RenderSkinningWrite skinnigData = m_skinningData->GetWriteData( SkinningDataRequest::NoLOD( m_numBones ) );
					if( skinnigData.IsValid() )
					{
						red::StaticArray< Matrix, 256 > bonePoses;
						bonePoses.Resize( m_numBones );
						Uint64 mask = ~0ull;

						for( Uint16 i : m_bodyMapping.Indices() )
						{
							auto& bodyInfo = m_bodyMapping[ i ];
							Uint32 boneInd = bodyInfo.m_boneIndex;

							if( boneInd >= m_numBones )
							{
								RED_ERROR( false, "Bone index out of bounds! fix asset" );
								continue;
							}

							bonePoses[ boneInd ] = reader[ i ].m_transform.ToMatrix();
							bonePoses[ boneInd ] = bodyInfo.m_originalBodyToModel.ToInvMatrix() * bonePoses[ boneInd ];
							bonePoses[ boneInd ] = bodyInfo.m_bodyToBone.ToMatrix() * bonePoses[ boneInd ];
							RED_ASSERT( bonePoses[ boneInd ].IsOk() );
						}

						for( Uint32 i = 0; i < m_numBones; ++i )
						{
							const Uint32 boneIndex = i;
							const Matrix& actorMat = bonePoses[ boneIndex ];
							skinnigData.Set( i, actorMat );
						}

						m_skinningData->AdvanceWrite();
						if( m_renderProxy )
						{
							m_renderProxy->SetMeshChunkMask( mask );
							m_renderProxy->RelinkAndSkin( rend::RenderProxyTransform::IDENTITY(), Box::FULL(), m_skinningData, nullptr );
						}

					}
				}
			}
		}
		else
		{
			const WorldTransform& bodyPose = reader[ 0 ].m_transform;
			auto bbox = m_meshResource->GetBoundingBox();
			bbox = bodyPose.TransformBox( bbox );
			m_renderProxy->Relink( bodyPose, bbox );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	struct PhysicsEditModeSimulation_InputListener final : public IPreviewInputListener
	{
		RED_BASE_CLASS( IPreviewInputListener );

		PhysicsEditModeSimulation_InputListener( PhysicsEditModeSimulation* editMode );

		virtual Bool OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt ) override final;
		virtual Bool OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt ) override final;
		virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt ) override final;

		PhysicsEditModeSimulation* m_editMode;
		Bool m_shiftDown = false;
	};

	PhysicsEditModeSimulation_InputListener::PhysicsEditModeSimulation_InputListener( PhysicsEditModeSimulation* editMode )
		: m_editMode( editMode )
	{}

	Bool PhysicsEditModeSimulation_InputListener::OnKeyDown( PreviewPanel& panel, const InputKeyEvent& evt )
	{
		if( evt.m_key == IK_LShift || evt.m_key == IK_RShift )
		{
			m_shiftDown = true;
		}
		return TBaseClass::OnKeyUp( panel, evt );
	}

	Bool PhysicsEditModeSimulation_InputListener::OnKeyUp( PreviewPanel& panel, const InputKeyEvent& evt )
	{
		if( evt.m_key == IK_R )
		{
			m_editMode->pimpl->m_recreateProxies = true;
		}
		else if( evt.m_key == IK_P )
		{
			m_editMode->pimpl->m_isPaused = !m_editMode->pimpl->m_isPaused;
		}
		else if( evt.m_key == IK_Right )
		{
			m_editMode->pimpl->m_stepRequested = true;
		}
		else if( evt.m_key == IK_LShift || evt.m_key == IK_RShift )
		{
			m_shiftDown = false;
		}
		return TBaseClass::OnKeyUp( panel, evt );
	}

	Bool PhysicsEditModeSimulation_InputListener::OnMouseDown( PreviewPanel& panel, const InputMouseButton& evt )
	{
		if( evt.m_button == LMB )
		{
			Vector3 outPos;
			Vector3 outDir;
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, outPos, outDir );
			if( m_shiftDown )
			{
				m_editMode->WakeUp( outPos, outDir );
			}
			else
			{
				m_editMode->ApplyImpulse( outPos, outDir );
			}
		}
		else if( evt.m_button == RMB && m_shiftDown )
		{
			Vector3 outPos;
			Vector3 outDir;
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, outPos, outDir );
			m_editMode->ThrowObject( outPos, outDir );
			return true;
		}

		return TBaseClass::OnMouseDown( panel, evt );
	}

	//////////////////////////////////////////////////////////////////////////

	PhysicsEditModeSimulation::PhysicsEditModeSimulation( )
		: GenericEditMode( "PhysicsEditor Simulation" )
		, pimpl( red::CreateUniquePtr<PhysicsEditModeSimulation::impl>() )
	{
	}

	PhysicsEditModeSimulation::~PhysicsEditModeSimulation()
	{
	}

	Bool PhysicsEditModeSimulation::OnInitialize( const PreviewPanelSharedDataPtr& sharedData )
	{
		pimpl->m_processor = red::CreateUniquePtr<SystemsUpdater>();
		m_inputListener = red::CreateUniquePtr<PhysicsEditModeSimulation_InputListener>( this );

		world::RuntimeInfo runtimeInfo( world::RuntimeMode::GamePreview );
		world::RuntimeSceneSetupInfo sceneSetup( runtimeInfo );
		sceneSetup.m_existingRenderScene = sharedData->GetRenderScene().Get();
		sceneSetup.m_processor = pimpl->m_processor.Get();

		pimpl->m_runtimeScene = world::CreateBackedScene( sceneSetup );

		pimpl->m_sceneID = pimpl->m_runtimeScene->GetSystem<world::RuntimeSystemPhysics>()->GetSceneID();
		pimpl->m_destructionSystem = pimpl->m_runtimeScene->GetSystem<world::RuntimeSystemDestruction>();
		MeshPanelSharedData* panelSharedData = static_cast<MeshPanelSharedData*>(sharedData.Get() );
		pimpl->m_simulationOptions = panelSharedData->GetPhysicsSimulationOptions();

		m_serializableNotifier.SetCallback( [ this ]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			OnNotifyPropertyChange( id, path );
		} );

		return TBaseClass::OnInitialize( sharedData );
	}
		
	Bool PhysicsEditModeSimulation::OnActivate( IPreviewEditMode* currentMode )
	{
		MeshPanelSharedData* panelSharedData = GetSharedData();
		MeshEditModePhysics* prevMode = static_cast<MeshEditModePhysics*>(currentMode);
		if( panelSharedData && prevMode )
		{
			m_activeResolver = prevMode->m_resolver;
			InitializeSceneData();
			pimpl->m_editorScene->SetVisible( false );
			RecreateRenderingProxy();

			RecreateScenePhysicsProxy();

			RecreateThrowablePhysicsProxy();
			RecreateGroundPhysicsProxy();

			pimpl->m_meshScene.m_renderProxy->SetVisibility( true );

			//////////////////////////////////////////////////////////////////////////
			// TODO: remove when all meshes are reimported
			if( pimpl->m_meshScene.m_meshResource && pimpl->m_meshScene.m_meshResource->GetParameter<mesh::MeshParamDestructionChunkIndicesOffsets>() == nullptr )
			{
				pimpl->m_meshScene.m_useDeprecatedSkinning = true;
			}
			//////////////////////////////////////////////////////////////////////////

#ifndef PHYSICS_RELEASE
			physics::DisconnectVisualDebugger();
			physics::ConnectVisualDebugger( );
#endif

			auto& panels = panelSharedData->GetPreviewPanels();
			for( auto* panel : panels )
			{
				panel->AttachInputListener( m_inputListener.Get(), false );
			}
			return true;
		}
		return false;
	}

	void PhysicsEditModeSimulation::OnDeactivate()
	{
		pimpl->m_meshScene.m_renderProxy->SetVisibility( false );
		physics::DetachProxy( pimpl->m_sceneID, pimpl->m_meshScene.m_proxyID );
		physics::DetachProxy( pimpl->m_sceneID, pimpl->m_throwable.m_proxyID );
		physics::DetachProxy( pimpl->m_sceneID, pimpl->m_ground.m_proxyID );

		physics::DestroyProxy( pimpl->m_meshScene.m_proxyID );
		physics::DestroyProxy( pimpl->m_throwable.m_proxyID );
		physics::DestroyProxy( pimpl->m_ground.m_proxyID );

		pimpl->m_meshScene.m_bodyMapping.Clear();
		pimpl->m_meshScene.m_transformBuffer = nullptr;
		pimpl->m_throwable.m_transformBuffer = nullptr;
		pimpl->m_ground.m_transformBuffer = nullptr;

		if( pimpl->m_meshScene.m_ibCacheKey != 0 )
		{
			pimpl->m_meshScene.m_destructionSystem->FreeRenderIndexBuffer_PhysicalDestruction( pimpl->m_meshScene.m_ibCacheKey );
			pimpl->m_meshScene.m_ibCacheKey = 0;
		}

		physics::PhysicsEditorScene* scene = m_activeResolver->GetPhysicsEditorScene();
		scene->SetVisible( true );

		MeshPanelSharedData* meshPanelSharedData = GetSharedData();
		if( meshPanelSharedData )
		{
			auto& panels = meshPanelSharedData->GetPreviewPanels();
			for( auto* panel : panels )
			{
				panel->DetachInputListener( m_inputListener.Get() );
			}
		}

#ifndef PHYSICS_RELEASE
		physics::DisconnectVisualDebugger();
#endif
	}

	void PhysicsEditModeSimulation::OnRender(CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo )
	{
		TBaseClass::OnRender( frame, cameraInfo );
		const CRenderCamera& camera = cameraInfo.GetRenderCamera();
		pimpl->m_cameraPosition = camera.GetPosition().AsVector3();
		pimpl->m_runtimeScene->RenderDebug( frame, cameraInfo );
		MeshPanelSharedData* meshPanelSharedData = GetSharedData();
		if( meshPanelSharedData )
		{
			rend::IDebugDrawer& dd = cameraInfo.GetDebugDrawer();

			const physics::PhysicsRendererOptionsPtr& options = meshPanelSharedData->GetRenderOptions();
			
			Uint8 alpha = (Uint8)(255.0f * options->m_simulationGroundPlaneAlpha);
			rend::FragmentBuilder builder( dd );
			builder.EnableTransparency( true );

			if( physics::IsInScene( pimpl->m_throwable.m_proxyID ) )
			{
				builder.SetColor( Color::GREEN( alpha ) );

				builder.BindTransform( physics::GetTransform( pimpl->m_throwable.m_proxyID ).ToMatrix() );
				pimpl->m_simulationOptions->m_throwableShape->OnRenderDebug( builder, physics::DRAW_OUTLINES | physics::DRAW_VOLUME | physics::CUSTOM_COLOR );
			}

			{
				Color color = options->m_simulationGroundPlaneColor;
				color.A = alpha;

				builder.SetColor( color );
				builder.BindTransform( physics::GetTransform( pimpl->m_ground.m_proxyID ).ToMatrix() );
				const auto& body = pimpl->m_ground.m_physicsData->GetBody( 0 );
				body->m_collisionShapes[ 0 ]->OnRenderDebug( builder, physics::DRAW_OUTLINES | physics::DRAW_VOLUME | physics::CUSTOM_COLOR );
			}

			constexpr Int32 startX = 10;
			constexpr Int32 startY = 20;
			constexpr Int32 offsetY = 15;
			Int32 lineCounter = 1;
			builder.SetColor( pimpl->m_isPaused ? Color::RED() : Color::GREEN() );
			builder.AddFlatText( startX, startY, pimpl->m_isPaused ? "SIMULATION PAUSED" : "SIMULATION RUNNING" );
			builder.SetColor( Color::DARK_BLUE() );
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Edit simulation options by selecting Physics in Structure Tree." ); ++lineCounter;
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press 'LMB' to apply impulse." ); ++lineCounter;
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press 'Shift + LMB' to wake up sleeping actor." ); ++lineCounter;
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press 'Shift + RMB' to throw an object." ); ++lineCounter;
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press 'P' to toggle pause." ); ++lineCounter;
			builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press 'R' to restart simulation." ); ++lineCounter;
			if( pimpl->m_isPaused )
			{
				builder.AddFlatText( startX, startY + lineCounter * offsetY, "Press '->' to move simulation a step." ); ++lineCounter;
			}

			if( physics::IsAlive( pimpl->m_throwable.m_proxyID ) )
			{
				physics::RenderDebug( pimpl->m_throwable.m_proxyID, dd );
			}
			if( physics::IsAlive( pimpl->m_ground.m_proxyID ) )
			{
				physics::RenderDebug( pimpl->m_ground.m_proxyID, dd );
			}
			if( physics::IsAlive( pimpl->m_meshScene.m_proxyID ) )
			{
				physics::RenderDebug( pimpl->m_meshScene.m_proxyID, dd );
			}
		}
	}		

	void PhysicsEditModeSimulation::OnUpdate(const UpdateContext& ctx)
	{
		TBaseClass::OnUpdate( ctx );

		if( !pimpl->m_isPaused || pimpl->m_stepRequested )
		{
			pimpl->m_stepRequested = false;
			pimpl->m_processor->BuildJobChainAndFlush( "PhysicsEditModeSimulation::OnUpdate", ctx.m_timeDelta, ctx.m_tickInfo );

			if( pimpl->m_recreateProxies )
			{
				RecreateScenePhysicsProxy();
				RecreateThrowablePhysicsProxy();
				RecreateGroundPhysicsProxy();
				RecreateRenderingProxy();

				pimpl->m_recreateProxies = false;
			}
		}
	}

	void PhysicsEditModeSimulation::InitializeSceneData( )
	{
		if( m_activeResolver )
		{
			physics::PhysicsEditorScene* scene = m_activeResolver->GetPhysicsEditorScene();
			if( scene )
			{
				pimpl->m_editorScene = scene;

				physics::TSystemObjectsMap actorMap;
				red::DynArray< physics::PhysicsBasePtr > objects = scene->GetPhysicsObjectsOnScene();
				THandle< physics::PhysicsMesh > physicsMeshData = nullptr;
				// find physics mesh
				for( const THandle< physics::PhysicsBase >& object : objects )
				{
					if( object->GetBaseType() == physics::EPBT_Mesh )
					{
						physicsMeshData = Cast< physics::PhysicsMesh >( object );
						break;
					}
				}

				pimpl->m_meshScene.m_physicsData = physics::PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( objects, actorMap, physicsMeshData );
				physics::PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( objects, actorMap, pimpl->m_meshScene.m_physicsData, physicsMeshData );

				pimpl->m_meshScene.m_meshResource = physicsMeshData->GetMesh();
				pimpl->m_meshScene.m_meshAppearance = physicsMeshData->GetAppearance();
			}
		}
	}

	void PhysicsEditModeSimulation::RecreateRenderingProxy( )
	{
		world::RuntimeSystemRendering& system = pimpl->m_runtimeScene->GetSystemRef<world::RuntimeSystemRendering>();
		pimpl->m_meshScene.m_renderProxy = nullptr;

		auto appearance = pimpl->m_meshScene.m_meshResource->GetAppearanceSafe( pimpl->m_meshScene.m_meshAppearance );

		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = true;

		meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
		meshInfo.m_renderMesh = pimpl->m_meshScene.m_meshResource->GetRenderResource();
		meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
		meshInfo.m_setupsNum = 1;

		meshInfo.m_componentFriendlyName = "<meshEditorSimulation>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = pimpl->m_meshScene.m_meshResource->GetCastGlobalShadows();
		meshInfo.m_isCastingLocalShadows = pimpl->m_meshScene.m_meshResource->GetCastLocalShadows();
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = true;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = pimpl->m_meshScene.m_meshResource->GetAutoHideDistance();
		meshInfo.m_renderingPlane = RPl_Scene;

		// base
		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = pimpl->m_meshScene.m_meshResource->GetBoundingBox();

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		pimpl->m_meshScene.m_renderProxy = system.CreateAndRegisterRenderProxy( initInfo, true );

	}

	void PhysicsEditModeSimulation::RecreateScenePhysicsProxy()
	{
		if( physics::IsAlive( pimpl->m_meshScene.m_proxyID ) )
		{
			physics::DetachProxy( pimpl->m_sceneID, pimpl->m_meshScene.m_proxyID );
			physics::DestroyProxy( pimpl->m_meshScene.m_proxyID );
			pimpl->m_meshScene.m_transformBuffer = nullptr;
			pimpl->m_meshScene.m_bodyMapping.Clear();
		}

		if( pimpl->m_meshScene.m_ibCacheKey != 0 )
		{
			pimpl->m_meshScene.m_destructionSystem->FreeRenderIndexBuffer_PhysicalDestruction( pimpl->m_meshScene.m_ibCacheKey );
			pimpl->m_meshScene.m_ibCacheKey = 0;
		}

		if( pimpl->m_meshScene.m_meshResource == nullptr )
			return;

		auto data = pimpl->m_meshScene.m_physicsData;
		if( data && !data->GetBodies().Empty() )
		{
			const mesh::DataViewRO& meshDataViewRO = mesh::DataViewRO( *pimpl->m_meshScene.m_meshResource );
			pimpl->m_meshScene.m_numBones = meshDataViewRO.GetNumBones();
			if( pimpl->m_editorScene->IsSingleMeshScene() && pimpl->m_meshScene.m_numBones > 0 )
			{
				physics::TransformBufferMeshData transformData
				{
					data,
					meshDataViewRO.GetBoneNames(),
					WorldTransform::IDENTITY(),
				};

				pimpl->m_meshScene.m_transformBuffer = physics::CreateTransformBuffer( transformData, pimpl->m_meshScene.m_bodyMapping );
				pimpl->m_meshScene.m_isSkinned = true;
			}
			else
			{
				pimpl->m_meshScene.m_transformBuffer = physics::CreateTransformBuffer( WorldTransform::IDENTITY() );
				pimpl->m_meshScene.m_isSkinned = false;
			}

			auto paramBCM = pimpl->m_meshScene.m_meshResource->GetParameter< mesh::MeshParamDestructionStepData >();
			pimpl->m_meshScene.m_isDestructible = pimpl->m_simulationOptions->m_simulateAsDestruction && paramBCM;
			pimpl->m_meshScene.m_hasEdgeMaterialSupport = pimpl->m_simulationOptions->m_destructionParams.m_markEdgeChunks;
			if( pimpl->m_meshScene.m_isDestructible && pimpl->m_meshScene.m_transformBuffer->GetCount() > 1 )
			{
				CreatePhysicalDestructionProxy();
			}
			else
			{
				CreatePhysicalSystemProxy( pimpl->m_meshScene );
			}
			pimpl->m_meshScene.Pull( pimpl->m_meshScene.m_transformBuffer->BeginRead(), pimpl->m_meshScene.m_transformBuffer->GetCount(), WorldTransform::IDENTITY() );
			pimpl->m_meshScene.m_transformBuffer->EndRead();

			physics::AttachProxy( pimpl->m_sceneID, pimpl->m_meshScene.m_proxyID );
		}
	}

	void PhysicsEditModeSimulation::RecreateGroundPhysicsProxy()
	{
		if( physics::IsAlive( pimpl->m_ground.m_proxyID ) )
		{
			physics::DetachProxy( pimpl->m_sceneID, pimpl->m_ground.m_proxyID );
			physics::DestroyProxy( pimpl->m_ground.m_proxyID );
			pimpl->m_ground.m_transformBuffer = nullptr;
		}

		pimpl->m_ground.m_physicsData = CreateHandle< physics::SystemResource >();
		physics::SystemBodyBuildData buildData;
		THandle<physics::ColliderBox > groundCollider = physics::ColliderBox::Create( Vector3( pimpl->m_simulationOptions->m_groundPlaneSize, pimpl->m_simulationOptions->m_groundPlaneSize, 1.0f ) );
		groundCollider->m_filterData = CreateHandle< physics::FilterData >();
		groundCollider->m_filterData->SetSimulationData( physics::SimulationFilter::ALL() );
		groundCollider->m_filterData->SetQueryFilter( physics::QueryFilter::ALL() );

		buildData.m_collisionShapes.EmplaceBack( groundCollider );
		buildData.m_params.m_simulationType = physics::SimulationType::Static;

		if( auto systemBody = physics::SystemBody::Create( buildData ) )
		{
			pimpl->m_ground.m_physicsData->AttachObject( systemBody );
			WorldTransform throwableTransform;
			throwableTransform.SetPosition( Vector3( 0.f, 0.f, pimpl->m_simulationOptions->m_groundPlanePosition ) );
			pimpl->m_ground.m_transformBuffer = physics::CreateTransformBuffer( throwableTransform );
		}

		CreatePhysicalSystemProxy( pimpl->m_ground );
		physics::AttachProxy( pimpl->m_sceneID, pimpl->m_ground.m_proxyID );
	}

	void PhysicsEditModeSimulation::RecreateThrowablePhysicsProxy()
	{
		if( physics::IsAlive( pimpl->m_throwable.m_proxyID ) )
		{
			physics::DetachProxy( pimpl->m_sceneID, pimpl->m_throwable.m_proxyID );
			physics::DestroyProxy( pimpl->m_throwable.m_proxyID );
			pimpl->m_throwable.m_transformBuffer = nullptr;
		}

		pimpl->m_throwable.m_physicsData = CreateHandle< physics::SystemResource >();
		physics::SystemBodyBuildData buildData;
		buildData.m_collisionShapes.EmplaceBack( pimpl->m_simulationOptions->m_throwableShape );
		buildData.m_params.m_simulationType = physics::SimulationType::Dynamic;
		// 0.f will result in mass being recalculated by physX from the shape based on it's material and volume
		buildData.m_params.m_mass = pimpl->m_simulationOptions->m_thrownObjectCustomMass ? pimpl->m_simulationOptions->m_thrownObjectMass : 0.f; 
		if( auto systemBody = physics::SystemBody::Create( buildData ) )
		{
			pimpl->m_throwable.m_physicsData->AttachObject( systemBody );
			WorldTransform throwableTransform;
			Vector3 initialPosition = pimpl->m_cameraPosition;
			pimpl->m_throwable.m_transformBuffer = physics::CreateTransformBuffer( throwableTransform );
		}

		CreatePhysicalSystemProxy( pimpl->m_throwable );
	}

	void PhysicsEditModeSimulation::CreatePhysicalSystemProxy( PhysicsObject& object )
	{
		auto simOptions = GetSharedData()->GetPhysicsSimulationOptions();
		physics::PhysicalSystemDesc desc;
		desc.m_localToWorld = WorldTransform::IDENTITY();
		desc.m_resource = object.m_physicsData;
		desc.m_meshAppearance = object.m_meshAppearance;
		desc.m_sourceType = physics::PhysicalSystemOwner::PhysicalSkinnedMeshComponent;
		desc.m_filterDataSource = pimpl->m_simulationOptions->m_filterDataSource;
		desc.m_filterData.ApplyPreset( RED_NAME_CONSTEXPR( "World Dynamic") );
		desc.m_transformBuffer = object.m_transformBuffer.Get();
		desc.m_transformState = &object;
		desc.m_startInactive = pimpl->m_simulationOptions->m_startInactive;
		desc.m_useResourceSimulationType = true;
		desc.m_editor = false;

		object.m_proxyID = physics::CreateProxy( desc );
	}

	void PhysicsEditModeSimulation::CreatePhysicalDestructionProxy( )
	{
		physics::DestructionDesc desc;
		desc.m_localToWorld = WorldTransform::IDENTITY();
		desc.m_resource = pimpl->m_meshScene.m_physicsData;
		desc.m_meshAppearance = pimpl->m_meshScene.m_meshAppearance;
		desc.m_sourceType = physics::PhysicalSystemOwner::PhysicalDestructionComponent;
		desc.m_filterDataSource = physics::FilterDataSource::Parent;
		desc.m_transformBuffer = pimpl->m_meshScene.m_transformBuffer.Get();
		desc.m_transformState = &pimpl->m_meshScene;
		desc.m_editor = false;
		desc.m_destructionParams = pimpl->m_simulationOptions->m_destructionParams;
		FillDestructionHierarchyData( pimpl->m_meshScene.m_physicsData, desc );

		desc.m_boneChunkMasks.Reserve( pimpl->m_meshScene.m_bodyMapping.Size() );
		auto paramBCM = pimpl->m_meshScene.m_meshResource->GetParameter< mesh::MeshParamDestructionBoneChunkMapping >();
		if( paramBCM )
		{
			const mesh::DestructionBoneChunkMapping& boneToChunk = paramBCM->GetDestructionBoneChunkMapping();
			for( const auto& bodyMapping : pimpl->m_meshScene.m_bodyMapping )
			{
				if( bodyMapping.m_boneIndex < boneToChunk.Size() )
				{
					desc.m_boneChunkMasks.PushBack( boneToChunk[ bodyMapping.m_boneIndex ] );
				}
				else
				{
					desc.m_boneChunkMasks.PushBack( 0 );
				}
				RED_DATA_ERROR_BEGIN( red::LoggerCategory_Physics, bodyMapping.m_boneIndex >= boneToChunk.Size(), "Destruction mesh has broken bone chunk mapping!" );
				RED_DATA_ERROR_ACTION_SHOW_ASSET( pimpl->m_meshScene.m_meshResource->GetPath().ToDebugString() );
				RED_DATA_ERROR_END();
			}
		}
		else
		{
			for( const auto& bodyMapping : pimpl->m_meshScene.m_bodyMapping )
			{
				desc.m_boneChunkMasks.PushBack( 0 );
			}
		}

		auto meshDestroBonds = pimpl->m_meshScene.m_meshResource->GetParameter< mesh::MeshParamDestructionBonds>();
		if( meshDestroBonds )
		{
			auto& bonds = meshDestroBonds->GetDestructionBonds();

			desc.m_bonds.Reserve( bonds.Size() );

			for( auto& bond : bonds )
			{
				desc.m_bonds.PushBack( { { bond.m_bondIndex }, bond.m_bondHealth } );
			}
			desc.m_bonds.MakeClean();
		}

		desc.m_onFractureCallback = ([ this ]( const red::ArraySpan< Vector4 >& fracturingPoints )
		{
			// nothing here, no previewing vfx or sound for now
		});

		desc.m_onDamagedCallback = ( [ this ]( Float damageRate, const physics::DestructionPersistentState& persistentState )
		{
			// nothing here, no listeners to inform
		} );

		desc.m_onInitialized = ( [ this ]( Float damageRate )
		{
			// nothing here, no listeners to inform
		} );

		pimpl->m_meshScene.m_proxyID = physics::CreateProxy( desc );
		pimpl->m_meshScene.m_destructionSystem = pimpl->m_destructionSystem;
	}

	void PhysicsEditModeSimulation::FillDestructionHierarchyData( const THandle< physics::SystemResource >& dataFractured, physics::DestructionDesc &desc )
	{
		auto meshDestroParam = pimpl->m_meshScene.m_meshResource->GetParameter< mesh::MeshParamDestructionStepData>();
		Uint16 hierarchySize = dataFractured->GetNumBodies();
		if( meshDestroParam == nullptr )
		{
			// fallback for old or broken assets
			Uint16 childSize = hierarchySize - 1;
			desc.m_offsets.Resize( hierarchySize );

			for( Uint16 i = 0; i < childSize; ++i )
			{
				desc.m_offsets[ i + 1 ] = 0;
			}
			physics::DestructionHierarchyOffset nonFractured = { 1, childSize };
			desc.m_offsets[ 0 ] = nonFractured;
		}
		else
		{
			desc.m_offsets = meshDestroParam->m_offsets;
			desc.m_isInstantRemovable = meshDestroParam->m_isInstantRemovable;

			if( desc.m_offsets.Size() != hierarchySize )
			{
				// fallback for old or broken assets
				Uint16 childSize = hierarchySize - 1;

				desc.m_offsets.Resize( hierarchySize );

				for( Uint16 i = 0; i < childSize; ++i )
				{
					desc.m_offsets[ i + 1 ] = 0;
				}
				physics::DestructionHierarchyOffset nonFractured = { 1, childSize };
				desc.m_offsets[ 0 ] = nonFractured;
			}
		}

		Uint32 destructionLevels = 2;
		Uint32 nextIndex = desc.m_offsets[ desc.m_offsets[ 0 ].m_begin ].m_begin;
		Uint32 size = desc.m_offsets.Size();
		while( nextIndex && nextIndex < size )
		{
			++destructionLevels;
			nextIndex = desc.m_offsets[ nextIndex ].m_begin;
		}
		auto meshBonds = pimpl->m_meshScene.m_meshResource->GetParameter< mesh::MeshParamDestructionBonds>();
		if( meshBonds )
		{
			++destructionLevels;
		}
		physics::FilterData destructible, debris;
		destructible.ApplyPreset( RED_NAME_CONSTEXPR( "Destructible" ) );
		debris.ApplyPreset( RED_NAME_CONSTEXPR( "Debris" ) );
		auto simOptions = pimpl->m_simulationOptions;
		if( destructionLevels > simOptions->m_destructionLevelData.Size() )
		{
			Uint32 currentIndex = simOptions->m_destructionLevelData.Size();
			while( destructionLevels != simOptions->m_destructionLevelData.Size() )
			{
				if( currentIndex == 0 || currentIndex == destructionLevels - 1 )
				{
					simOptions->m_destructionLevelData.PushBack( physics::DestructionLevelData( CreateHandle<physics::FilterData>( destructible ) ) );
				}
				else
				{
					simOptions->m_destructionLevelData.PushBack( physics::DestructionLevelData( CreateHandle<physics::FilterData>( debris ) ) );
				}
			}
		}
		desc.m_filterDataPerDestructionLevel.Reserve( destructionLevels );
		for( const physics::DestructionLevelData& dlData : simOptions->m_destructionLevelData )
		{
			desc.m_filterDataPerDestructionLevel.PushBack( dlData.m_filterData ? *dlData.m_filterData : debris );
		}
	}

	void PhysicsEditModeSimulation::ThrowObject( const Vector3& pos, const Vector3& dir )
	{
		if( !physics::IsInScene( pimpl->m_throwable.m_proxyID ) )
		{
			physics::AttachProxy( pimpl->m_sceneID, pimpl->m_throwable.m_proxyID );
		}
		physics::ProxyWriter writer( pimpl->m_throwable.m_proxyID );
		writer.SetPosition( pos );
		writer.SetLinearVelocity( dir * pimpl->m_simulationOptions->m_thrownObjectVelocity );
		writer.Commit();
	}

	void PhysicsEditModeSimulation::ApplyImpulse( const Vector3& pos, const Vector3& dir )
	{
		constexpr Float c_rayLength = 200.f;
		physics::TraceResult raycastResult;
		physics::RaycastParams raycastParams = physics::RaycastParams::Create( pos,
			dir, c_rayLength, physics::QueryFilter::ALL() );
		raycastParams.SetFlags( physics::RaycastParams::MESH_BOTH_SIDES );

		physics::SyncResult raycastSyncResult = physics::SyncRaycast( pimpl->m_sceneID, raycastParams, raycastResult );

		if( raycastSyncResult == physics::SyncResult::Hit )
		{
			physics::ImpulseData data;
			data.m_position = raycastResult.m_worldPosition;
			data.m_linearImpulse = (raycastResult.m_worldPosition - pos).Normalized() * pimpl->m_simulationOptions->m_impulseMagnitude;
			data.m_radius = pimpl->m_simulationOptions->m_impulseRadius;

			if( math::EqualsZero( pimpl->m_simulationOptions->m_impulseRadius ) )
			{
				if( raycastResult.m_proxyType == physics::ProxyType::Destruction )
				{
					physics::AddDestructionImpulse( raycastResult.m_proxyId, data, raycastResult.m_actorIndex, raycastResult.m_shapeIndex );
				}
				else
				{
					physics::ProxyWriter writer( raycastResult.m_proxyId );
					writer.AddImpulseAccumulator( data, raycastResult.m_actorIndex ).Commit();
				}
			}
			else
			{
				physics::OverlapParams overlapParams = physics::OverlapParams::CreateSphere( pimpl->m_simulationOptions->m_impulseRadius,
					raycastResult.m_worldPosition, physics::QueryFilter::ALL() );

				physics::TraceQueryBuffer overlapBuffer;
				physics::SyncResult overlapResult = physics::SyncOverlapN( pimpl->m_sceneID, overlapParams, overlapBuffer );

				if( overlapResult == physics::SyncResult::Hit )
				{
					for( const physics::TraceResult& overlapResult : overlapBuffer )
					{
						if( overlapResult.m_proxyType == physics::ProxyType::Destruction )
						{
							physics::AddDestructionImpulse( overlapResult.m_proxyId, data, overlapResult.m_actorIndex, overlapResult.m_shapeIndex );
						}
						else
						{
							physics::ProxyWriter writer( overlapResult.m_proxyId );
							writer.AddImpulseAccumulator( data, overlapResult.m_actorIndex ).Commit();
						}
					}
				}
			}
		}
	}

	void PhysicsEditModeSimulation::WakeUp( const Vector3& pos, const Vector3& dir )
	{
		constexpr Float c_rayLength = 200.f;
		physics::TraceResult raycastResult;
		physics::RaycastParams raycastParams = physics::RaycastParams::Create( pos,
			dir, c_rayLength, physics::QueryFilter::ALL() );
		raycastParams.SetFlags( physics::RaycastParams::MESH_BOTH_SIDES );

		physics::SyncResult raycastSyncResult = physics::SyncRaycast( pimpl->m_sceneID, raycastParams, raycastResult );

		if( raycastSyncResult == physics::SyncResult::Hit )
		{
			physics::ProxyWriter writer( raycastResult.m_proxyId );
			writer.SetIsSleeping( false, raycastResult.m_actorIndex );
			writer.Commit();
		}
	}

	void PhysicsEditModeSimulation::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		if( pimpl->m_simulationOptions->GetID() == id )
		{
			CName childName;
			rtti::PathParser parser( path );

			// TODO: recreate only what's changed
			pimpl->m_recreateProxies = true;
		}
	}

	void PhysicsEditModeSimulation::OnSetupInterop( ::interop::utils::Object& op )
	{
		op.RegisterValue<Bool>( "isPaused",
			/*GET*/	[ this ]( interop::IContext& ctx, Bool& outValue ) { outValue = this->pimpl->m_isPaused; },
			/*SET*/	[ this ]( interop::IContext& ctx, const Bool& newValue )
		{
			this->pimpl->m_isPaused = newValue;
		} );
	}
}

#ifdef PHYSICS_DEBUG
#pragma optimize("", on)
#endif