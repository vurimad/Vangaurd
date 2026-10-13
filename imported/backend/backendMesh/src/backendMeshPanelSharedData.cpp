/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMeshPanelSharedData.h"
#include "backendMeshPreviewPanel.h"
#include "lodGenerationInterop.h"
#include "meshEditModes.h"
#include "physicsObjectResolver.h"
#include "physicsEditorScene.h"
#include "physicsObjectActionsHelper.h"
#include "physicsMesh.h"
#include "physicsActor.h"
#include "physicsBackendHelper.h"
#include "physicsEditModeSimulation.h"
#include "backendMeshPreviewEditMode_ClothPaint.h"
#include "backendMeshPreviewEditMode_Simulation.h"
#include "backendMeshPreviewEditMode_ShadowMesh.h"
#include "backendMeshPreviewEditMode_Destruction.h"
#include "backendMeshPreviewPanelInputListener.h"
#include "backendMeshEditor.h"

#include "../../backendEngine/include/editActionHistory.h"
#include "../../backendEngine/include/selectionIterator.h"
#include "../../backendEngine/include/selectionState.h"
#include "../../backendEngine/include/objectActionDeleteObjects.h"
#include "../../backendEngine/include/resourceBackend.h"

#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/resourceMesh/include/meshParamCompiledPhysics.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"

#include "../../../common/physics/include/physicsClothStructs.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physXNatives.h"
#include "../../../common/physics/include/physicsCollection.h"

#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/generalEditorBackend/include/clothEditorInterop.h"

#include "../../../common/redJobs2/include/jobBuilder.h"

namespace tools
{
	const String m_editorConfigDirectory = "editorOptions\\";
	const String m_editorConfigJSON = "MeshEditorPhysicsOptions.json";
    
    const CName cEditModeClothPaint = RED_NAME_CONSTEXPR("editModeClothPaint");
    const CName cEditModeClothSimulate = RED_NAME_CONSTEXPR("editModeClothSimulate");
    const CName cDefaultEditModeName = RED_NAME_CONSTEXPR("PhysicsData");
	const CName cSimulationEditModeName = RED_NAME_CONSTEXPR( "Simulation" );
	const CName cShadowMeshEditModeName = RED_NAME_CONSTEXPR( "editModeShadowMesh" );
	const CName cDestructionMeshEditModeName = RED_NAME_CONSTEXPR( "editModeDestructionMesh" );

    namespace internal
    {
        red::ArraySpan<Vector3> ExtractMeshVertexPositions(MeshPanelSharedData* panel, Uint16 chunk)
        {
            mesh::DataViewRO view(*panel->GetEditor().GetResource<CMesh>());
            return mesh::MakeSpan<Vector3>(view, chunk, MST_Position_3F);
        }

        red::ArraySpan<Uint16> ExtractMeshIndices(MeshPanelSharedData* panel, Uint16 chunk)
        {
            mesh::DataViewRO view(*panel->GetEditor().GetResource<CMesh>());
            return mesh::MakeSpan<Uint16>(view, chunk, MST_Index_U16);
        }

        Float MapToRange(Uint8 in)
        {
            return static_cast<Float>(in) / static_cast<Float>(std::numeric_limits<Uint8>::max());
        }

        Bool IsParticlePainted(Uint8 in)
        {
            return MapToRange(in) > 0.f;
        }

        Bool IsParticleLocked(Uint8 in)
        {
            return MapToRange(in) == 0.f;
        }

        Float ParticleMass(Uint8 in)
        {
            return IsParticleLocked(in) ? 0.f : 1.f;
        }

        Float ToCM(Uint8 in)
        {
            return static_cast<Float>(in) * 0.01f;
        }

        res::ResourcePath GetCollisionsResourcePath(MeshPanelSharedData* shared_data)
        {
            const auto path = shared_data->GetEditor().GetResource()->GetPath(); 
            auto file_stem = red::paths::ExtractFileStem(path.ToStringView());
            auto parent_path = red::paths::ExtractParentPath(path.ToStringView());
            return res::ResourcePath::Build(red::String::Printf("%hs/%hs.capsules.json", parent_path.AsChar(), file_stem.AsChar()));
        }

        res::ResourceTokenHandle IssueLoadingRequest(MeshPanelSharedData* shared_data, res::ResourcePath path)
        {
            return shared_data->GetResourceBackend().GetResourceLoader()->IssueLoadingRequest(path);
        }

        struct BaseAction : public IEditAction
        {
            BaseAction(MeshPanelSharedData* data)
                : ptr(data)
            {
                m_wasModified = ptr->GetEditor().GetResource()->IsModified();
            }

            Bool Do() override
            {
                if( !m_wasModified )
                {
                    ptr->GetEditor().GetResource()->MarkModified();
                }

                return true;
            }

            virtual Bool Undo() override
            {
                if( !m_wasModified )
                {
                    ptr->GetEditor().GetResource()->UnmarkModified();
                }

                return true;
            }

            MeshPanelSharedData* ptr;
            Bool m_wasModified = false;
        };
    }

    struct MeshPanelSharedData::clothImpl
    {
        RED_USE_MEMORY_POOL(red::PoolBackend);
        clothImpl();
        ~clothImpl();

        tools::ClothEditorState m_state;
        interop::InteropNotifier m_onClothStateChangedNotifier;
        THandle<physics::cloth::ClothCapsuleExportData> m_capsules;
        Bool m_clothEnabled = false;
		Bool m_useGeodesicTether = true;
		Bool m_quadifyMesh = true;
    };

    MeshPanelSharedData::clothImpl::clothImpl() = default;
    MeshPanelSharedData::clothImpl::~clothImpl() = default;

	MeshPanelSharedData::MeshPanelSharedData( IResourceEditor& editor )
		: GenericPreviewPanelSharedData( editor )
        , clothPimpl(red::CreateUniquePtr<clothImpl>())
	{
		LoadEditorOptionsFromFile();

		// edit modes
		RegisterEditMode(cSimulationEditModeName, []{ return RED_NEW(PhysicsEditModeSimulation);});

		auto param = Cast<CMesh>(editor.GetResource())->GetParameter<mesh::MeshParamCloth>();
        clothPimpl->m_clothEnabled = mesh::IsValid_ForMeshEditor(param);
        if( clothPimpl->m_clothEnabled )
        {
            RegisterEditMode(cEditModeClothPaint, []{return RED_NEW(MeshEditModeClothPaint);});
            RegisterEditMode(cEditModeClothSimulate, []{return RED_NEW(MeshEditModeClothSimulation);});
            
            clothPimpl->m_state = param->m_rawData.m_state;
            clothPimpl->m_capsules = param->m_capsules;

			for( auto& chunk : param->m_chunks )
			{
				if( !chunk.m_cookedData.Empty() && physics::cloth::NeedsVersionUpdate(chunk.m_cookedData.GetView()) )
				{
					physics::cloth::Update(chunk.m_cookedData);
					GetEditor().GetResource()->MarkModified();
				}
			}
        }
		else if( param && !mesh::IsValid_ForMeshEditor(param) )
		{
			RED_REPORT_ERROR( RED_LOCATION(GetMeshPath()), "Cloth Mesh Parameter is corrupted, please reimport the mesh to enable cloth editing");
		}

		RegisterEditMode( cShadowMeshEditModeName, [] {return RED_NEW( MeshEditModeShadowMesh ); } );

		auto destructionParam = Cast<CMesh>( editor.GetResource() )->GetParameter<mesh::MeshParamDestructionStepData>();
		if( destructionParam && destructionParam->m_offsets.Size() )
		{
			RegisterEditMode( cDestructionMeshEditModeName, [] {return RED_NEW( MeshEditModeDestructionMesh ); } );
		}

		CreatePhysicsData();

		m_lodGenerationInterop = red::CreateUniquePtr< tools::LODGenerationInterop >(this);
        
		m_monitor.Bind( editor.GetResource()->GetPath() );
        m_monitor.SetCallback([this](const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData ){OnResourceMonitorMessage(path, message, userData);});
	}
    
	MeshPanelSharedData::~MeshPanelSharedData()
	{
		m_objectHandler->DetachObjectResolver( m_objectResolverPhysicsData.Get() );
        
		// save editor options to file
		serialization::json::JsonSerializer jsonSerializer( m_options );
		String result = String::EMPTY();
		jsonSerializer.Serialize( result );
		SaveStringToFile( red::paths::GetUserEditorDirectory().AddDirPath( m_editorConfigDirectory ).AddFilePath( m_editorConfigJSON ), result );
	}
    
    void MeshPanelSharedData::NotifyPanelCreated(PreviewPanel* panel)
    {
        if(auto* edit_mode = GetEditMode())
        {
            edit_mode->NotifyPanelCreated(panel);
        }
        
        TBaseClass::NotifyPanelCreated(panel);
    }
    
    void MeshPanelSharedData::NotifyPanelDestroyed(PreviewPanel* panel)
    {
        if(auto* edit_mode = GetEditMode())
        {
            edit_mode->NotifyPanelDestroyed(panel);
        }
        
        TBaseClass::NotifyPanelDestroyed(panel);
    }

	void MeshPanelSharedData::NotifyLODsChange()
	{
		m_onLODsChangedNotifier.Notify();
	}
    
	void MeshPanelSharedData::LoadEditorOptionsFromFile()
	{
		String jsonData = String::EMPTY();
		LoadFileToString( red::paths::GetUserEditorDirectory().AddDirPath( m_editorConfigDirectory ).AddFilePath( m_editorConfigJSON ), jsonData );
		if( !res::json::ParseObjectInline( std::move( jsonData ), m_options ) )
		{
			m_options = CreateHandle<physics::PhysicsEditorOptions>();
		}
		m_options->m_renderOptionsCollection = red::CreateUniquePtr<tools::ObjectCollection>( GetActionHistory() ); 
		m_options->m_simulationOptionsCollection = red::CreateUniquePtr<tools::ObjectCollection>( GetActionHistory() );
		m_options->m_markVerticesHandlerOptionsCollection = red::CreateUniquePtr<tools::ObjectCollection>( GetActionHistory() );

		m_options->m_renderOptionsCollection->SetObject( m_options->m_renderOptions.Get() );
		m_options->m_simulationOptionsCollection->SetObject( m_options->m_simulationOptions.Get() );
		m_options->m_markVerticesHandlerOptionsCollection->SetObject( m_options->m_markVerticesHandlerOptions.Get() );
	}
	
	void MeshPanelSharedData::PlaceMesh( const THandle<CMesh>& mesh )
	{
		// create physics data
		THandle< physics::SystemResource > physicsSystemResource = nullptr;
		auto physicsParam = mesh->GetParameter< mesh::MeshParamPhysics >();

		if ( physicsParam && physicsParam->GetPhysicsData() )
		{
			physicsSystemResource = physicsParam->GetPhysicsData();
		}

		m_physicsData->SetResource( mesh );

		auto physicsMeshData = CreateHandle< physics::PhysicsMesh >( m_physicsData->GetNextFreeId(), mesh->GetPath().ToString(), m_physicsData.Get() );
		m_physicsData->PlacePhysicsObject( physicsMeshData );
		
		if ( physicsSystemResource )
		{
			auto& physicsObjects = physics::PhysicsBackendHelper::ConvertFromPhysicsSystemToEditor( physicsSystemResource, m_physicsData.Get(), physicsMeshData );
			m_physicsData->PlacePhysicsObjects( physicsObjects );
		}
	}

	void MeshPanelSharedData::CreatePhysicsData()
	{
        RED_FATAL_ASSERT(m_objectHandler, "Error: Object Handler is a nullptr");
        
		if( m_objectResolverPhysicsData )
		{
			m_objectHandler->DetachObjectResolver( m_objectResolverPhysicsData.Get() );
		}	
		
		ClearEditMode( cDefaultEditModeName );
        
		const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );
		m_physicsData = physics::PhysicsEditorScene::Create( mesh, cDefaultEditModeName, true, m_options );
		PlaceMesh( mesh );
        
		m_objectResolverPhysicsData = red::CreateUniquePtr<PhysicsObjectResolver>(m_physicsData.Get(), m_selectionHandler.Get(), cDefaultEditModeName);
		m_objectHandler->AttachObjectResolver( m_objectResolverPhysicsData.Get());
        
		RegisterEditMode( cDefaultEditModeName, [this]{ return RED_NEW(MeshEditModePhysics)(m_objectResolverPhysicsData.Get()); });
	}
    
	void MeshPanelSharedData::SetupRendering( THandle< CMesh > mesh, CName meshAppearance, Int32 forcedLodLevel )
	{
		m_proxyUpdateInfo.m_meshAppearance = meshAppearance;
		m_proxyUpdateInfo.m_forcedLodLevel = forcedLodLevel;
		m_proxyUpdateInfo.m_needsRecreation = true;
	}
    
	void MeshPanelSharedData::Save()
	{
        const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );
        
		if( m_physicsData )
		{
			SwitchToInitialVehiclePoses();
			SaveInitialVehiclePoses();

			physics::TSystemObjectsMap actorMap;
			red::DynArray< physics::PhysicsBasePtr > objects = m_physicsData->GetPhysicsObjectsOnScene();
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
            
			THandle< physics::SystemResource > physSystem = physics::PhysicsBackendHelper::ConvertToPhysicsSystemFromEditor( objects, actorMap, physicsMeshData );
			physics::PhysicsBackendHelper::CreateJointsForPhysicsSystemFromEditor( objects, actorMap, physSystem, physicsMeshData );
            
			// If we successfully created physSystem, save it to a mesh
			if( physSystem && physSystem->GetNumObjects() > 0 )
			{
				THandle< mesh::MeshParamPhysics > meshImportedPhys = mesh->CreateParameter< mesh::MeshParamPhysics >();
				meshImportedPhys->SetPhysicsData( physSystem );
			}
			else
			{
				mesh->RemoveParameter( ClassID<mesh::MeshParamPhysics>() );
			}
		}
        
        if( clothPimpl->m_clothEnabled )
        {
            auto param = mesh->GetParameter<mesh::MeshParamCloth>();
            param->m_rawData.m_state = clothPimpl->m_state;
            param->m_capsules = clothPimpl->m_capsules;
            Internal_CompileClothData();
        }
	}
    
    void MeshPanelSharedData::RegisterShortcuts(ShortcutManager& shortcutManager, CName context)
    {
		ShortcutBinding binding = ShortcutBinding().Key( EInputKey::IK_Escape, EAction::Up );
		ShortcutCallback callback = [this]( const KeyEventData& data ){ return ChangeEditMode(cDefaultEditModeName); };
		ShortcutBuilder shortcut = ShortcutBuilder().IdName( "idDefaultEditMode" ).Binding( binding ).Callback( callback ).Name( "Exit Edit Mode." ).Context( context );
		shortcutManager.Register( shortcut );
    }
    
    physics::PhysicsEditorScene* MeshPanelSharedData::GetMeshEditorPhysicsScene()
    {
        return m_physicsData.Get();
    }
    
	Bool MeshPanelSharedData::SnapToParent( const red::DynArray< EditorObjectID >& objectIds )
	{
		if( IObjectResolver* resolver = GetObjectHandler().GetUnifiedResolver( objectIds ) )
		{
			return GetActionHistory().Execute( red::CreateUniquePtr< tools::Action_SnapObject >( (PhysicsObjectResolver*)resolver, &GetSelectionHandler(), objectIds ) );
		}
        
		return false;
	}
    
	Bool MeshPanelSharedData::SnapToParentAlternativeMode( const red::DynArray< EditorObjectID >& objectIds )
	{
		if( IObjectResolver* resolver = GetObjectHandler().GetUnifiedResolver( objectIds ) )
		{
			return GetActionHistory().Execute( red::CreateUniquePtr< tools::Action_SnapObject >( (PhysicsObjectResolver*)resolver, &GetSelectionHandler(), objectIds, true ) );
		}
        
		return false;
	}
    
	Bool MeshPanelSharedData::SelectParent( const red::DynArray< EditorObjectID >& objectIds )
	{
		if( IObjectResolver* resolver = GetObjectHandler().GetUnifiedResolver( objectIds ) )
		{
			return GetActionHistory().Execute( red::CreateUniquePtr< tools::Action_SelectParent >( (PhysicsObjectResolver*)resolver, &GetSelectionHandler(), objectIds ) );
		}
        
		return false;
	}
    
	Bool MeshPanelSharedData::RecomputeCOM( const red::DynArray< EditorObjectID >& objectIds )
	{
		if( IObjectResolver* resolver = GetObjectHandler().GetUnifiedResolver( objectIds ) )
		{
			return GetActionHistory().Execute( red::CreateUniquePtr< tools::Action_RecomputeCOM >( (PhysicsObjectResolver*)resolver, &GetSelectionHandler(), objectIds ) );
		}
        
		return false;
	}
    
	Bool MeshPanelSharedData::DeleteObjects( const red::DynArray< EditorObjectIDPath >& objectIdPaths )
	{
        if( !objectIdPaths.Empty() )
        {
            return GetActionHistory().Execute( red::CreateUniquePtr< tools::Action_DeleteObjects >( GetObjectHandler(), objectIdPaths ) );
        }
        
		return true;
	}	
    
	Bool MeshPanelSharedData::GenerateDefaultDestructionHierarchy()
	{
		if( auto* resolver = GetObjectHandler().FindObjectResolver<PhysicsObjectResolver>() )
		{
            return GetActionHistory().Execute( red::CreateUniquePtr<tools::Action_GenerateDefaultDestructionHierarchy>(resolver) );
		}
        
		return false;
	}
    
	void MeshPanelSharedData::SaveInitialVehiclePoses()
	{
		const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );

		if( m_physicsData )
		{
			const THandle<mesh::MeshParamDeformableShapesData>& shapePoses = mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
			if( shapePoses )
			{
				const auto& objectsMap = m_physicsData->GetPhysicsObjectsMap();
				shapePoses->m_startingPose.Clear();
				shapePoses->m_ownerIndex.Clear();
				Uint8 ownerIndex = 0;
				for( const auto& pairs : objectsMap )
				{
					const physics::PhysicsBasePtr& object = pairs.Value();

					if( object->GetBaseType() == physics::EPBT_Actor )
					{
						const THandle<physics::PhysicsActor > actorPtr = Cast< physics::PhysicsActor>( object );
						const auto& shapes = actorPtr->GetActorShapes();
						shapePoses->m_startingPose.Reserve( shapePoses->m_startingPose.Size() + shapes.Size() );
						shapePoses->m_ownerIndex.Reserve( shapePoses->m_ownerIndex.Size() + shapes.Size() );
						for( const auto& shape : shapes )
						{
							Matrix local2Body = shape->GetLocalTransform();
							shapePoses->m_ownerIndex.PushBack( ownerIndex );
							shapePoses->m_startingPose.PushBack( { local2Body.GetTranslation(), local2Body.ToQuat() } );
						}
						++ownerIndex;
					}
				}
			}
		}
	}

	void MeshPanelSharedData::SaveFinalVehiclePoses()
	{
		const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );

		if( m_physicsData )
		{
			const THandle<mesh::MeshParamDeformableShapesData>& shapePoses = mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
			if( shapePoses ) 
			{
				const auto& objectsMap = m_physicsData->GetPhysicsObjectsMap();
				shapePoses->m_finalPose.Clear();

				for( const auto& pairs : objectsMap )
				{
					const physics::PhysicsBasePtr& object = pairs.Value();

					if( object->GetBaseType() == physics::EPBT_Actor )
					{
						const THandle<physics::PhysicsActor > actorPtr = Cast< physics::PhysicsActor>( object );
						const auto& shapes = actorPtr->GetActorShapes();
						shapePoses->m_finalPose.Reserve( shapes.Size() );
						for( Uint32 i : shapes.Indices() )
						{
							auto& shape = shapes[ i ];
							Matrix local2Body = shape->GetLocalTransform();
							shapePoses->m_finalPose.PushBack( { local2Body.GetTranslation(), local2Body.ToQuat() } );
						}
					}
				}
			}
		}
	}

	void MeshPanelSharedData::SwitchToFinalVehiclePoses()
	{
		const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );

		if( m_physicsData )
		{
			const THandle<mesh::MeshParamDeformableShapesData>& shapePoses = mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
			if( shapePoses )
			{
				const auto& objectsMap = m_physicsData->GetPhysicsObjectsMap();
				Uint8 ownerIndex = 0;
				Uint8 caret = 0;
				Uint32 minSize = math::Min( shapePoses->m_ownerIndex.Size(), shapePoses->m_finalPose.Size() );
				for( const auto& pairs : objectsMap )
				{
					const physics::PhysicsBasePtr& object = pairs.Value();
					if( object->GetBaseType() == physics::EPBT_Actor )
					{
						if( caret < minSize && shapePoses->m_ownerIndex[ caret ] == ownerIndex )
						{
							const THandle<physics::PhysicsActor > actorPtr = Cast< physics::PhysicsActor>( object );
							const auto& shapes = actorPtr->GetActorShapes();
							for( Uint32 i : shapes.Indices() )
							{
								if( caret < minSize && shapePoses->m_ownerIndex[ caret ] == ownerIndex )
								{
									auto& shape = shapes[ i ];
									shape->UpdateLocalTransform( shapePoses->m_finalPose[ caret ].ToMatrix() );
								}
								else
								{
									break;
								}
								++caret;
							}
						}
						else
						{
							break;
						}
						++ownerIndex;
					}
				}
			}
		}
	}

	void MeshPanelSharedData::SwitchToInitialVehiclePoses()
	{
		const THandle< CMesh > mesh = Cast< CMesh >( GetEditor().GetResource() );

		if( m_physicsData )
		{
			const THandle<mesh::MeshParamDeformableShapesData>& shapePoses = mesh->GetParameter<mesh::MeshParamDeformableShapesData>();
			if( shapePoses )
			{
				const auto& objectsMap = m_physicsData->GetPhysicsObjectsMap();
				Uint8 ownerIndex = 0;
				Uint8 caret = 0;
				Uint32 minSize = math::Min( shapePoses->m_ownerIndex.Size(), shapePoses->m_startingPose.Size() );
				for( const auto& pairs : objectsMap )
				{
					const physics::PhysicsBasePtr& object = pairs.Value();
					if( object->GetBaseType() == physics::EPBT_Actor )
					{
						if( caret < minSize && shapePoses->m_ownerIndex[ caret ] == ownerIndex )
						{
							const THandle<physics::PhysicsActor > actorPtr = Cast< physics::PhysicsActor>( object );
							const auto& shapes = actorPtr->GetActorShapes();
							for( Uint32 i : shapes.Indices() )
							{
								if( caret < minSize && shapePoses->m_ownerIndex[ caret ] == ownerIndex )
								{
									auto& shape = shapes[ i ];
									shape->UpdateLocalTransform( shapePoses->m_startingPose[ caret ].ToMatrix() );
								}
								else
								{
									break;
								}
								++caret;
							}
						}
						else
						{
							break;
						}
						++ownerIndex;
					}
				}
			}

		}
	}

	void MeshPanelSharedData::StartSimulation()
	{
		if( GetEditModeName() != cSimulationEditModeName )
		{
			m_previousEditMode = GetEditModeName();
			ChangeEditMode( cDefaultEditModeName );
			ChangeEditMode( cSimulationEditModeName );
		}
	}
    
	void MeshPanelSharedData::StopSimulation()
	{
		if( GetEditModeName() == cSimulationEditModeName )
		{
			ChangeEditMode( m_previousEditMode );
		}
	}
    
    CName MeshPanelSharedData::GetDefaultEditModeName() const
    {
        return cDefaultEditModeName;
    }
    
	const physics::PhysicsRendererOptionsPtr& MeshPanelSharedData::GetRenderOptions() const
	{
		return m_options->m_renderOptions;
	}
    
	const physics::PhysicsSimOptionsPtr& MeshPanelSharedData::GetPhysicsSimulationOptions() const
	{
		return m_options->m_simulationOptions;
	}
    
    red::BlobSpan MeshPanelSharedData::GetMaxDistanceChannelSpan(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
			return param->m_rawData.m_maxDistanceChannel[chunk].GetSpan();
        }
        
        return {};
    }

	red::BlobSpan MeshPanelSharedData::GetMaxDistanceExtChannelSpan(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
			return param->m_rawData.m_maxDistanceExtChannel[chunk].GetSpan();
        }
        
        return {};
    }
    
    red::BlobSpan MeshPanelSharedData::GetBackstopDistanceChannelSpan(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
			return param->m_rawData.m_backstopDistanceChannel[chunk].GetSpan();
        }
        
        return {};
    }

	red::BlobSpan MeshPanelSharedData::GetBackstopRadiusChannelSpan(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
			return param->m_rawData.m_backstopRadiusChannel[chunk].GetSpan();
        }
        
        return {};
    }

    red::BlobSpan MeshPanelSharedData::GetSelfCollisionChannelSpan(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
	        return param->m_rawData.m_selfCollisionChannel[chunk].GetSpan();
        }
        
        return {};
    }
    
    red::BlobView MeshPanelSharedData::GetClothResource(Uint16 chunk) const
    {
        if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
			return param->m_chunks[chunk].m_cookedData.GetView();
        }
        
        return {};
    }

	Uint32 MeshPanelSharedData::GetNumClothLODs() const
	{
		if( auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>())
        {
            return param->m_chunks.Size();
        }
        
        return 0;
	}

    void MeshPanelSharedData::RefreshCompiledClothData()
    {
        Internal_CompileClothData();
    }

	void MeshPanelSharedData::RemoveClothParam(THandle<mesh::MeshParameter> param)
	{
		struct Action_RemoveClothParam : public internal::BaseAction
        {
            RED_BASE_CLASS(internal::BaseAction);

			Action_RemoveClothParam(MeshPanelSharedData* data, THandle<mesh::MeshParameter> param)
                : TBaseClass(data)
                , m_param(param)
            {}

			virtual Bool Do() override final
			{
				auto mesh = ptr->GetEditor().GetResource<CMesh>();
				mesh->RemoveParameter(m_param);
				m_clothEnabled = std::exchange(ptr->clothPimpl->m_clothEnabled, false);
				ptr->GetEditor<MeshResourceEditor>().RefreshStructureTree();

				return TBaseClass::Do();
			}

			virtual Bool Undo() override final
			{
				auto mesh = ptr->GetEditor().GetResource<CMesh>();
				mesh->AddParameter(m_param);
				ptr->clothPimpl->m_clothEnabled = m_clothEnabled;
				ptr->GetEditor<MeshResourceEditor>().RefreshStructureTree();

				return TBaseClass::Undo();
			}

			virtual void GetAffectedResources(red::Set< res::ResourcePath >& outResourcePaths) const override final
			{
				const auto path = ptr->GetEditor().GetResource()->GetPath();
				outResourcePaths.Insert(path);
			}

			virtual String GetDescription() const override
			{
				if( Cast<mesh::MeshParamCloth>(m_param) != nullptr )
				{
					return "Remove Physical Cloth Param";
				}

				else if( Cast<mesh::MeshParamCloth_Graphical>(m_param) != nullptr )
				{
					return "Remove Graphical Cloth Param";
				}

				return "";
			};

			THandle<mesh::MeshParameter> m_param;
			Bool m_clothEnabled;
		};

        GetActionHistory().Execute(red::CreateUniquePtr<Action_RemoveClothParam>(this, param));
	}

	res::ResourcePath MeshPanelSharedData::GetMeshPath() const
	{
		return GetEditor().GetResource()->GetPath();
	}
    
	void MeshPanelSharedData::OnUpdateSharedState( const UpdateContext& ctx )
	{
		if ( m_proxyUpdateInfo.m_needsRecreation )
		{
			m_proxyUpdateInfo.m_needsRecreation = false;
            
			auto objects = m_physicsData->GetPhysicsObjectsOnScene();
			for ( const auto& object : objects )
			{
				if ( object->GetBaseType() == physics::EPBT_Mesh )
				{
					auto physicsMeshData = Cast< physics::PhysicsMesh >( object );
					physicsMeshData->SetAppearance( m_proxyUpdateInfo.m_meshAppearance );
					physicsMeshData->SetForcedLOD( m_proxyUpdateInfo.m_forcedLodLevel );

					const CName currentEditModeName = GetEditModeName();
					if( currentEditModeName == cEditModeClothPaint || currentEditModeName == cEditModeClothSimulate )
					{
						auto* edit_mode = GetEditMode<MeshPreviewEditMode>();
						edit_mode->OnStateChanged( clothPimpl->m_state );
					}
					else if( currentEditModeName == cDestructionMeshEditModeName )
					{
						auto* edit_mode = GetEditMode<MeshEditModeDestructionMesh>();
						edit_mode->OnUpdateSharedState( m_proxyUpdateInfo.m_meshAppearance, m_proxyUpdateInfo.m_forcedLodLevel );
					}
				}
			}
		}
	}
    
    void MeshPanelSharedData::OnResourceMonitorMessage(const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData)
    {
        if(message == RED_NAME_CONSTEXPR_NOREG( "reload" ) )
        {
            CreatePhysicsData();
            ChangeEditMode(cDefaultEditModeName);
            
            for( auto& panel : GetPreviewPanels() )
            {
                static_cast<MeshPreviewPanel*>(panel)->Refresh();
            }
        }
    }
    
    void MeshPanelSharedData::Internal_SetClothState(const tools::ClothEditorState& state)
    {
        struct Action_SetState : public internal::BaseAction
        {
            RED_BASE_CLASS(internal::BaseAction);

            Action_SetState(MeshPanelSharedData* data, const tools::ClothEditorState& current, const tools::ClothEditorState& previous)
                : TBaseClass(data)
                , curr(current)
                , prev(previous)
            {}

            virtual String GetDescription() const override final
            {
                auto _getPhaseDataDesc = [](const physics::cloth::PhaseConfig& a, const physics::cloth::PhaseConfig& b, red::String& out) -> Bool
                {
                    out = "";
                    
                    if( a.mStiffness != b.mStiffness )
                    {
                        out = "Stiffness";
                    }
                    else if(a.mStiffnessMultiplier != b.mStiffnessMultiplier)
                    {
                        out = "Stiffness Multiplier";
                    }
                    else if(a.mStretchLimit != b.mStretchLimit)
                    {
                        out = "Stretch Limit";
                    }
                    else if(a.mCompressionLimit != b.mCompressionLimit)
                    {
                        out = "Compression Limit";
                    }
                    
                    return !out.Empty();
                };

                auto _getBrushInfoDesc = [](const tools::ClothEditorBrushInfo& a, const tools::ClothEditorBrushInfo& b, red::String& out) -> Bool
                {
                    out = "";
                    
                    if( a.m_viewMin != b.m_viewMin )
                    {
                        out = "View Min";
                    }
                    else if(a.m_viewMax != b.m_viewMax)
                    {
                        out = "View Max";
                    }
                    else if(a.m_opacity != b.m_opacity)
                    {
                        out = "Brush Opacity";
                    }
                    else if(a.m_radius != b.m_radius)
                    {
                        out = "Brush Radius";
                    }
                    
                    return !out.Empty();
                };

                {
                    red::String desc;
                    if( _getPhaseDataDesc(curr.m_bendPhaseData, prev.m_bendPhaseData, desc) )
                    {
                        return red::String("Set Bend Constraint: ") + desc;
                    }
                    else if( _getPhaseDataDesc(curr.m_shearPhaseData, prev.m_shearPhaseData, desc) )
                    {
                        return red::String("Set Shear Constraint: ") + desc;
                    }
                    else if( _getPhaseDataDesc(curr.m_horizontalPhaseData, prev.m_horizontalPhaseData, desc) )
                    {
                        return red::String("Set Horizontal Constraint: ") + desc;
                    }
                    else if( _getPhaseDataDesc(curr.m_verticalPhaseData, prev.m_verticalPhaseData, desc) )
                    {
                        return red::String("Set Vertical Constraint: ") + desc;
                    }
                    else if( _getBrushInfoDesc(curr.m_maxDistance, prev.m_maxDistance, desc) )
                    {
                        return red::String("Set Max Distance Channel: ") + desc;
                    }
                    else if( _getBrushInfoDesc(curr.m_backstopDistance, prev.m_backstopDistance, desc) )
                    {
                        return red::String("Set Backstop Channel: ") + desc;
                    }
                    else if( _getBrushInfoDesc(curr.m_selfCollision, prev.m_selfCollision, desc) )
                    {
                        return red::String("Set Self Collision Channel: ") + desc;
                    }
                }

                if( curr.m_runtimeInfo.m_translation != prev.m_runtimeInfo.m_translation )
                {
                    return "Set Translation";
                }
                else if(curr.m_runtimeInfo.m_rotation != prev.m_runtimeInfo.m_rotation)
                {
                    return "Set Rotation";
                }
                else if(curr.m_runtimeInfo.m_gravity != prev.m_runtimeInfo.m_gravity)
                {
                    return "Set Gravity";
                }
                else if(curr.m_runtimeInfo.m_damping != prev.m_runtimeInfo.m_damping)
                {
                    return "Set Damping";
                }
                else if(curr.m_runtimeInfo.m_drag != prev.m_runtimeInfo.m_drag)
                {
                    return "Set Drag";
                }
                else if(curr.m_runtimeInfo.m_inertia != prev.m_runtimeInfo.m_inertia)
                {
                    return "Set Inertia";
                }
                else if(curr.m_runtimeInfo.m_numSolverIterations != prev.m_runtimeInfo.m_numSolverIterations)
                {
                    return "Set Solver Iterations Per Frame";
                }
				else if(curr.m_runtimeInfo.m_stiffnessFrequency != prev.m_runtimeInfo.m_stiffnessFrequency)
				{
					return "Set Stiffness Frequency";
				}
                else if(curr.m_runtimeInfo.m_friction != prev.m_runtimeInfo.m_friction)
                {
                    return "Set Friction";
                }
                else if(curr.m_runtimeInfo.m_tetherStiffness != prev.m_runtimeInfo.m_tetherStiffness)
                {
                    return "Set Tether Stiffness";
                }
                else if(curr.m_runtimeInfo.m_tetherScale != prev.m_runtimeInfo.m_tetherScale)
                {
                    return "Set Tether Scale";
                }
                else if(curr.m_runtimeInfo.m_selfCollisionDistance != prev.m_runtimeInfo.m_selfCollisionDistance)
                {
                    return "Set Self Collision Distance";
                }
                else if(curr.m_runtimeInfo.m_selfCollisionStiffness != prev.m_runtimeInfo.m_selfCollisionStiffness)
                {
                    return "Set Self Collision Stiffness";
                }
                else if(curr.m_runtimeInfo.m_enableSelfCollision != prev.m_runtimeInfo.m_enableSelfCollision)
                {
                    return "Set Self Collision Enabled";
                }
                else if(curr.m_runtimeInfo.m_gravityScale != prev.m_runtimeInfo.m_gravityScale)
                {
                    return "Set Gravity Scale";
                }
				else if(curr.m_useGeodesicTether != prev.m_useGeodesicTether )
				{
					return "Set use Geodesic Tethers";
				}
				else if( curr.m_quadifyMesh != prev.m_quadifyMesh )
				{
					return "Set quadify mesh";
				}
				else if( curr.m_runtimeInfo.m_motionConstraintStiffness != prev.m_runtimeInfo.m_motionConstraintStiffness )
				{
					return "Set motion constraint stiffness";
				}

                return "Set State";
            }

            virtual Bool Do() override final
            {
                ptr->clothPimpl->m_state = curr;
                ptr->clothPimpl->m_onClothStateChangedNotifier.Notify(curr);
                static_cast<MeshPreviewEditMode*>(ptr->GetEditMode())->OnStateChanged(curr);
				res::MonitorRouter::GetInstance().QueueChange(ptr->GetEditor().GetResource()->GetPath(), RED_NAME_CONSTEXPR("clothChanged"));
                return TBaseClass::Do();
            }

            virtual Bool Undo() override final
            {
                ptr->clothPimpl->m_state = prev;
                ptr->clothPimpl->m_onClothStateChangedNotifier.Notify(prev);
                static_cast<MeshPreviewEditMode*>(ptr->GetEditMode())->OnStateChanged(prev);
				res::MonitorRouter::GetInstance().QueueChange(ptr->GetEditor().GetResource()->GetPath(), RED_NAME_CONSTEXPR("clothChanged"));
                return TBaseClass::Undo();
            }

            virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final
            {
                const auto path = ptr->GetEditor().GetResource()->GetPath();
                outResourcePaths.Insert(path);
            }
            
            tools::ClothEditorState curr;
            tools::ClothEditorState prev;
        };
        
        if( clothPimpl->m_clothEnabled )
        {
            GetActionHistory().Execute(red::CreateUniquePtr<Action_SetState>(this, state, clothPimpl->m_state));
        }
    }

    void MeshPanelSharedData::Internal_CompileClothData(Bool logSuccess)
    {  
        struct CompileClothDataAction final : public internal::BaseAction
        {
            RED_BASE_CLASS(internal::BaseAction);

            CompileClothDataAction(MeshPanelSharedData* data, red::ArraySpan<DataBuffer> curr, red::ArraySpan<DataBuffer> prev)
                : TBaseClass(data)
            {
				for( auto i : curr.Indices() )
				{
					m_curr.EmplaceBack( DataBuffer::Copy(curr[i]));
					m_prev.EmplaceBack( DataBuffer::Copy(prev[i]));
				}
			}

            virtual String GetDescription() const override final
            {
                return "Compile Cloth Data";
            }

            virtual Bool Do() override final
            {
                auto param = ptr->GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>();

				for( auto i : m_curr.Indices() )
					param->m_chunks[i].m_cookedData = DataBuffer::Copy(m_curr[i]);

				res::MonitorRouter::GetInstance().QueueChange(ptr->GetEditor().GetResource()->GetPath(), RED_NAME_CONSTEXPR("clothChanged"));

                return TBaseClass::Do();
            }

            virtual Bool Undo() override final
            {
                auto param = ptr->GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>();
				for( auto i : m_prev.Indices() )
					param->m_chunks[i].m_cookedData = DataBuffer::Copy(m_prev[i]);

				res::MonitorRouter::GetInstance().QueueChange(ptr->GetEditor().GetResource()->GetPath(), RED_NAME_CONSTEXPR("clothChanged"));

                return TBaseClass::Undo();
            }

            red::DynArray<DataBuffer> m_curr;
            red::DynArray<DataBuffer> m_prev;
        };

        auto param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>();
        if( !param )
            return;

		mesh::DataViewRO view(*GetEditor().GetResource<CMesh>());
		red::DynArray<DataBuffer> curr(view.GetNumChunks());
		red::DynArray<DataBuffer> prev(view.GetNumChunks());
		for( Uint32 chunk = 0; chunk < view.GetNumChunks(); ++chunk )
		{
			auto vertexPositions = internal::ExtractMeshVertexPositions(this, chunk);
			auto indices = internal::ExtractMeshIndices(this, chunk);

			auto maxDistance = GetMaxDistanceChannelSpan(chunk);
			auto maxDistanceExt = GetMaxDistanceExtChannelSpan(chunk);
			auto backstop = GetBackstopDistanceChannelSpan(chunk);
			auto radius = GetBackstopRadiusChannelSpan(chunk);
			auto selfCollision = GetSelfCollisionChannelSpan(chunk);

			Bool hasCloth = false;
			Bool hasSeparation = false;

			red::DynArray<Vector4> vertices;
			red::DynArray<Float> maxDistanceValues;
			red::DynArray<Float> maxDistanceExtValues;
			red::DynArray<Float> backstopValues;
			red::DynArray<Float> backstopRadius;
			red::DynArray<Uint32> selfCollisionIndices;

			for(Uint32 i : vertexPositions.Indices() )
			{
				vertices.EmplaceBack(vertexPositions[i],internal::ParticleMass(maxDistance[i]));
				maxDistanceValues.EmplaceBack(internal::ToCM(maxDistance[i]) * clothPimpl->m_state.m_maxDistanceMultiplier);
				maxDistanceExtValues.EmplaceBack(internal::ToCM(maxDistanceExt[i]) * clothPimpl->m_state.m_maxDistanceMultiplier);
				backstopValues.EmplaceBack(internal::ToCM(backstop[i]));
				backstopRadius.EmplaceBack(internal::ToCM(radius[i]));

				if( internal::IsParticlePainted(selfCollision[i]) )
					selfCollisionIndices.PushBack(i);

				hasCloth |= !internal::IsParticleLocked(maxDistance[i]);
				hasSeparation |= (backstop[i] != 0);
			}

			if(hasCloth)
			{
				auto motion = red::ArraySpan<Float>(maxDistanceValues);
				auto motionExt = red::ArraySpan<Float>(maxDistanceExtValues);
				auto separation = (hasSeparation) ? red::ArraySpan<Float>(backstopValues) : red::ArraySpan<Float>();
				auto radius = (hasSeparation) ? red::ArraySpan<Float>(backstopRadius) : red::ArraySpan<Float>();
				auto capsules = clothPimpl->m_capsules ? red::ArraySpan<physics::cloth::ExportedCapsule>(clothPimpl->m_capsules->m_capsules) : red::ArraySpan<physics::cloth::ExportedCapsule>();

				physics::cloth::CreateClothResourceParams params =
				{
					{vertices},
					indices,
					motion,
					motionExt,
					separation,
					radius,
					{selfCollisionIndices},
					capsules,
					clothPimpl->m_state,
					clothPimpl->m_useGeodesicTether,
					clothPimpl->m_quadifyMesh
				};

				auto dataBuffer = physics::cloth::CreateClothResource(params);
				curr[chunk] = DataBuffer::Copy(dataBuffer);
			}
		}

		for( auto chunk : param->m_chunks.Indices() )
		{
			prev[chunk] = DataBuffer::Copy(param->m_chunks[chunk].m_cookedData);
		}

		if(GetActionHistory().Execute(red::CreateUniquePtr<CompileClothDataAction>(this, curr, prev)) && logSuccess)
		{
			RED_REPORT_SUCCESS(RED_LOCATION(GetMeshPath()), "Successfully Compiled Cloth Data");
		}
    }

	void MeshPanelSharedData::Internal_ValidateClothData()
	{
		auto phx_param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth>();
		auto gfx_param = GetEditor().GetResource<CMesh>()->GetParameter<mesh::MeshParamCloth_Graphical>();
        if( !phx_param && !gfx_param )
		{
			RED_REPORT_ERROR( RED_LOCATION(GetMeshPath()), "Validation Failed: Mesh has no cloth parameters");
			return;
		}

		if(phx_param)
		{
			Internal_CompileClothData(false);
		
			for( auto chunk : phx_param->m_chunks.Indices() )
			{
				auto view = phx_param->m_chunks[chunk].m_cookedData.GetView();
				if(!physics::cloth::Validate_NoDuplicateParticles<red::PoolBackend>(view))
				{
					RED_REPORT_ERROR( RED_LOCATION(GetMeshPath()), "Validation Failed: Mesh chunk [%] contains duplicate vertices", chunk);
					return;
				}

				if(!physics::cloth::Validate_NoZeroRestValues<red::PoolBackend>(view))
				{
					RED_REPORT_ERROR( RED_LOCATION(GetMeshPath()), "Validation Failed: Cloth mesh chunk [%] contains rest values of zero", chunk);
					return;
				}
			}
		}

		RED_REPORT_SUCCESS(RED_LOCATION(GetMeshPath()), "Validation Succeeded!");
	}

    void MeshPanelSharedData::Internal_ImportCollisionCapsules(red::AbsolutePath path)
    {
        using CapsuleExportDataPtr = THandle<physics::cloth::ClothCapsuleExportData>;

        struct ImportCapsulesAction final : public internal::BaseAction
        {
            RED_BASE_CLASS(internal::BaseAction);

            ImportCapsulesAction(MeshPanelSharedData* data, CapsuleExportDataPtr curr, CapsuleExportDataPtr prev)
                : TBaseClass(data)
                , m_curr(curr)
                , m_prev(prev)
            {}

            virtual String GetDescription() const
            {
                return "Import cloth collision capsules";
            }

            virtual Bool Do()
            {
                ptr->clothPimpl->m_capsules = m_curr;
                return TBaseClass::Do();
            }

            virtual Bool Undo()
            {
                ptr->clothPimpl->m_capsules = m_prev;
                return TBaseClass::Undo();
            }

            CapsuleExportDataPtr m_curr;
            CapsuleExportDataPtr m_prev;
        };


        auto res_path = res::GetEditorDepot()->TranslateAbsolutePath(path);
        auto token = internal::IssueLoadingRequest(this, res_path);

        job::Builder builder;
		builder.DispatchWait(token->GetWaitCounter());
		builder.DispatchJob("capusles postload", [token, this](const job::RunContext&)
		{
			if(auto json = Cast<JsonResource>(token->GetResource()))
			{
				auto capsules = Cast<physics::cloth::ClothCapsuleExportData>(json->GetRoot());
				if(GetActionHistory().Execute(red::CreateUniquePtr<ImportCapsulesAction>(this,capsules,clothPimpl->m_capsules)))
				{
					RED_REPORT_SUCCESS(RED_LOCATION(GetMeshPath()),"Import Collision Capsules Succeeded!");
				}
			}
			else
			{
				RED_REPORT_ERROR(RED_LOCATION(GetMeshPath()),"Import Collision Capsules Failed, File was not a valid json file which can be parsed by the engine");
			}
		});
    }

	void MeshPanelSharedData::Internal_ClearCollisionCapsules()
	{
		using CapsuleExportDataPtr = THandle<physics::cloth::ClothCapsuleExportData>;

        struct ClearCapsulesAction final : public internal::BaseAction
        {
            RED_BASE_CLASS(internal::BaseAction);

            ClearCapsulesAction(MeshPanelSharedData* data, CapsuleExportDataPtr prev)
                : TBaseClass(data)
                , m_prev(prev)
            {}

            virtual String GetDescription() const
            {
                return "Import cloth collision capsules";
            }

			virtual Bool CanDo() const
			{
				return m_prev != nullptr;
			}

            virtual Bool Do()
            {
                ptr->clothPimpl->m_capsules = nullptr;
                return TBaseClass::Do();
            }

            virtual Bool Undo()
            {
                ptr->clothPimpl->m_capsules = m_prev;
                return TBaseClass::Undo();
            }

            CapsuleExportDataPtr m_prev;
        };

		if(clothPimpl->m_capsules)
		{
			if(GetActionHistory().Execute(red::CreateUniquePtr<ClearCapsulesAction>(this, clothPimpl->m_capsules)))
			{
				RED_REPORT_SUCCESS(RED_LOCATION(GetMeshPath()), "Clear Collision Capsules Succeeded!");
			}
		}
		else
		{
			RED_REPORT_INFO(RED_LOCATION(GetMeshPath()), "No collision capsules to clear");
		}
	}
    
	void MeshPanelSharedData::SetupInterop( MeshPanelSharedData* ptr, ::interop::utils::Object& op )
	{
		op.RegisterObject( "lodGeneration", ptr->m_lodGenerationInterop.Get() );

        op.RegisterFunctor<Bool>("snapToParent",[ptr](::interop::IContext&)
        {
            const SelectionStatePtr& selection = ptr->GetSelection();
            if(!selection)
            {
                RED_LOG_WARNING("MeshPanelSharedData: Selection is required for snap");
                return true;
            }

            // get object to snap (currently selected one)		
            red::DynArray< EditorObjectID > selectedObjectIDs;
            for(auto it = selection->Iterate(); it; ++it)
                selectedObjectIDs.PushBack(it->GetEditorObjectID());
            return ptr->SnapToParent(selectedObjectIDs);
        });

        op.RegisterFunctor<Bool>("snapToParentAlternativeMode",[ptr](::interop::IContext&)
        {
            const SelectionStatePtr& selection = ptr->GetSelection();
            if(!selection)
            {
                RED_LOG_WARNING("MeshPanelSharedData: Selection is required for snap");
                return true;
            }

            // get object to snap (currently selected one)
            red::DynArray< EditorObjectID > selectedObjectIDs;
            for(auto it = selection->Iterate(); it; ++it)
            {
                selectedObjectIDs.PushBack(it->GetEditorObjectID());
            }

            return ptr->SnapToParentAlternativeMode(selectedObjectIDs);
        });

        op.RegisterFunctor<Bool>("selectParent",[ptr](::interop::IContext&)
        {
            const auto& selection = ptr->GetSelection();
            if(!selection)
            {
                RED_LOG_WARNING("MeshPanelSharedData: Selection is required for snap");
                return true;
            }

            // get object to replace (currently selected one)
            red::DynArray< EditorObjectID > selectedObjectIDs;
            for(auto it = selection->Iterate(); it; ++it)
            {
                selectedObjectIDs.PushBack(it->GetEditorObjectID());
            }

            return ptr->SelectParent(selectedObjectIDs);
        });

        op.RegisterFunctor< void >("startSimulation",[ptr](::interop::IContext& ctx){ ptr->StartSimulation(); });
        op.RegisterFunctor< void >("stopSimulation",[ptr](::interop::IContext& ctx){ ptr->StopSimulation(); });

        if(ptr->clothPimpl->m_clothEnabled)
        {
            op.RegisterFunctor< void, red::AbsolutePath >("importCollisionCapsules", [ptr](::interop::IContext&, red::AbsolutePath path){ 
                ptr->Internal_ImportCollisionCapsules(path);
            });

			op.RegisterFunctor< void >("clearCollisionCapsules", [ptr](::interop::IContext&){ 
                ptr->Internal_ClearCollisionCapsules();
            });

            op.RegisterFunctor< void >("compileCloth", [ptr](::interop::IContext&){ptr->Internal_CompileClothData();});
			op.RegisterFunctor< void >("validateCloth", [ptr](::interop::IContext&){ptr->Internal_ValidateClothData();});
            op.RegisterFunctor< void >("editModeClothPaint",[ptr](::interop::IContext&){ ptr->ChangeEditMode(cEditModeClothPaint); });
            op.RegisterFunctor< void >("editModeClothSimulate",[ptr](::interop::IContext&){ ptr->ChangeEditMode(cEditModeClothSimulate); });
            op.RegisterValue<tools::ClothEditorState>("clothState",
                [ptr](interop::IContext& ctx,tools::ClothEditorState& out){ out = ptr->clothPimpl->m_state; },
                [ptr](interop::IContext& ctx,const tools::ClothEditorState& in){ ptr->Internal_SetClothState(in);},
                &ptr->clothPimpl->m_onClothStateChangedNotifier);
        }

		op.RegisterFunctor< void >( "editModeShadowMesh", [ ptr ]( ::interop::IContext& )
		{
			ptr->ChangeEditMode( cShadowMeshEditModeName );
		} );

		op.RegisterFunctor< void >( "editModeShadowMeshDisable", [ ptr ]( ::interop::IContext& )
		{
			ptr->ChangeEditMode( cDefaultEditModeName );
		} );

		op.RegisterNotifier( "_onLODsChanged", ptr->m_onLODsChangedNotifier );

		op.RegisterObject( "physicsRendererOptions", ptr->m_options->m_renderOptionsCollection->GetRootInterop() );
		op.RegisterObject( "physicsSimulationOptions", ptr->m_options->m_simulationOptionsCollection->GetRootInterop() );
		op.RegisterObject( "markVerticesHandlerOptions", ptr->m_options->m_markVerticesHandlerOptionsCollection->GetRootInterop() );

		op.RegisterFunctor< void >( "toggleActors", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_actors = !ptr->m_options->m_renderOptions->m_actors;
		} );

		op.RegisterFunctor< void >( "toggleActorNames", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_actorNames = !ptr->m_options->m_renderOptions->m_actorNames;
		} );

		op.RegisterFunctor< void >( "toggleShapes", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_shapes = !ptr->m_options->m_renderOptions->m_shapes;
		} );

		op.RegisterFunctor< void >( "toggleJoints", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_joints = !ptr->m_options->m_renderOptions->m_joints;
		} );

		op.RegisterFunctor< void >( "toggleCOM", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_actorCOM = !ptr->m_options->m_renderOptions->m_actorCOM;
		} );

		op.RegisterFunctor< void >( "toggleBounds", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_bounds = !ptr->m_options->m_renderOptions->m_bounds;
		} );

		op.RegisterFunctor< void >( "toggleBonds", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_destructionBonds= !ptr->m_options->m_renderOptions->m_destructionBonds;
		} );

		op.RegisterFunctor< void >( "toggleMaterials", [ ptr ]( interop::IContext& )
		{
			ptr->m_options->m_renderOptions->m_materials = !ptr->m_options->m_renderOptions->m_materials;
		} );

		op.RegisterNotifier( "renderOptionsChanged", ptr->m_options->m_onRenderOptionsChanged );

		op.RegisterFunctor< void, Bool >( "hideAllVisualizations", [ ptr ]( interop::IContext&, const Bool hideAll )
		{
			if( hideAll )
			{
				ptr->m_options->m_renderOptions->m_actors = false;
				ptr->m_options->m_renderOptions->m_actorNames = false;
				ptr->m_options->m_renderOptions->m_shapes = false;
				ptr->m_options->m_renderOptions->m_joints = false;
				ptr->m_options->m_renderOptions->m_actorCOM = false;
				ptr->m_options->m_renderOptions->m_bounds = false;
				ptr->m_options->m_renderOptions->m_destructionBonds = false;
				ptr->m_options->m_renderOptions->m_materials = false;
			}

			ptr->m_options->m_onRenderOptionsChanged.Notify();
		} );

		op.RegisterFunctor< void >( "editModeDestructionMesh", [ ptr ]( ::interop::IContext& )
		{
			ptr->ChangeEditMode( cDestructionMeshEditModeName );
		} );

		op.RegisterFunctor< void >( "editModeDestructionMeshDisable", [ ptr ]( ::interop::IContext& )
		{
			ptr->ChangeEditMode( cDefaultEditModeName );
		} );
	}
}
