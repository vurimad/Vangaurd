#include "build.h"
#include "backendMeshPreviewEditMode_Simulation.h"

#include "../../backendEngine/include/previewInputListener.h"
#include "../../backendEngine/include/previewPanel.h"

#include "../../../common/engine/include/systemsUpdater.h"
#include "../../../common/physics/include/physXNatives.h"

#include "../../../common/world/include/runtimeSystemRendering.h"
#include "../../../common/world/include/worldRuntimeScene.h"
#include "../../../common/worldPhysics/include/runtimeSystemPhysics.h"

#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"

#include "../../../common/generalEditorBackend/include/clothEditorInterop.h"

namespace tools
{

static rend::DebugFilter DF_CLOTH_VISUALIZATION_CLOTH_MESH("Physics/Cloth/Vis/ClothMesh");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_MOTION_CONSTRAINTS("Physics/Cloth/Vis/MotionConstraints");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_SEPARATION_CONSTRAINTS("Physics/Cloth/Vis/SeparationConstraints");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_TETHERS("Physics/Cloth/Vis/Tethers");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_CONSTRAINTS("Physics/Cloth/Vis/Constraints/All");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_CONSTRAINT_STIFFNESS("Physics/Cloth/Vis/ConstraintStiffness");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_CONSTRAINT_ERROR("Physics/Cloth/Vis/ConstraintError");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_POSITION_DELTA("Physics/Cloth/Vis/PositionDelta");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_BOUNDING_BOX("Physics/Cloth/Vis/BoundingBox");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_VERTICAL_PHASE("Physics/Cloth/Vis/Constraints/Vertical");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_HORIZONTAL_PHASE("Physics/Cloth/Vis/Constraints/Horizontal");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_BEND_PHASE("Physics/Cloth/Vis/Constraints/Bend");
static rend::DebugFilter DF_CLOTH_VISUALIZATION_SHEAR_PHASE("Physics/Cloth/Vis/Constraints/Shear");

struct MeshEditModeClothSimulation::impl
{
    RED_USE_MEMORY_POOL(red::PoolBackend);

    red::UniquePtr<SystemsUpdater> m_processor = nullptr;
    red::UniquePtr<world::RuntimeScene> m_runtimeScene = nullptr;
    physics::SceneID m_physicsScene = physics::SceneID();
    physics::ProxyID m_clothProxy = physics::ProxyID();

    THandle<CMesh> m_meshResource = nullptr;
    RenderProxyPtr m_clothMeshProxy = nullptr;
    red::DynArray<math::Vector4> m_clothPoints;

    ClothEditorState m_prevStateData;

    Vector3 m_windDirection = Vector3::ZEROS();
    Float m_windStrength = 30.f;
	Int32 m_forcedLodLevel = 0;
    Bool m_windBlowing = false;
    Bool m_refresh = false;
	Bool m_recompile = false;
};

mesh::LODIndex GetForcedLOD(MeshEditModeClothSimulation* ptr)
{
	return static_cast<mesh::LODIndex>(std::max(0, ptr->GetSharedData()->m_proxyUpdateInfo.m_forcedLodLevel));
}

struct MeshEditModeClothSimulation_InputListener final : public IPreviewInputListener
{
    RED_BASE_CLASS(IPreviewInputListener);

    MeshEditModeClothSimulation_InputListener(MeshEditModeClothSimulation* editMode);

    virtual Bool OnKeyDown(PreviewPanel& panel, const InputKeyEvent& evt) override final;
    virtual Bool OnKeyUp(PreviewPanel& panel, const InputKeyEvent& evt) override final;

    MeshEditModeClothSimulation* m_editMode;
};

MeshEditModeClothSimulation_InputListener::MeshEditModeClothSimulation_InputListener(MeshEditModeClothSimulation* editMode) : m_editMode(editMode) {}

Bool MeshEditModeClothSimulation_InputListener::OnKeyDown(PreviewPanel& panel, const InputKeyEvent& evt)
{
    if (evt.m_key == IK_Space)
    {
        m_editMode->pimpl->m_windBlowing = true;
    }

    return TBaseClass::OnKeyDown(panel, evt);
}

Bool MeshEditModeClothSimulation_InputListener::OnKeyUp(PreviewPanel& panel, const InputKeyEvent& evt)
{
    if (evt.m_key == IK_Space)
    {
        m_editMode->pimpl->m_windBlowing = false;
    }
    else if( evt.m_key == IK_R )
    {
        m_editMode->pimpl->m_recompile = true;
    }

    return TBaseClass::OnKeyUp(panel, evt);
}

MeshEditModeClothSimulation::MeshEditModeClothSimulation()
    : MeshPreviewEditMode("ClothEditModeSimulation")
	, pimpl(red::CreateUniquePtr<MeshEditModeClothSimulation::impl>())
{}

MeshEditModeClothSimulation::~MeshEditModeClothSimulation()
{
	if( physics::IsInScene(pimpl->m_clothProxy) )
	{
		physics::DetachProxy(pimpl->m_physicsScene, pimpl->m_clothProxy);
		physics::DestroyProxy(pimpl->m_clothProxy);
	}
}

Color MeshEditModeClothSimulation::GetDisplayColor() const
{
    return Color::DARK_GREEN();
}

Bool MeshEditModeClothSimulation::OnInitialize(const PreviewPanelSharedDataPtr& sharedData)
{
    m_inputListener = red::CreateUniquePtr<MeshEditModeClothSimulation_InputListener>(this);
    pimpl->m_processor = red::CreateUniquePtr<SystemsUpdater>();

    world::RuntimeInfo runtimeInfo(world::RuntimeMode::Preview);
    world::RuntimeSceneSetupInfo sceneSetup(runtimeInfo);
    sceneSetup.m_existingRenderScene = sharedData->GetRenderScene().Get();
    sceneSetup.m_processor = pimpl->m_processor.Get();

	pimpl->m_runtimeScene = world::CreateBackedScene( sceneSetup );

    pimpl->m_physicsScene = pimpl->m_runtimeScene->GetSystem<world::RuntimeSystemPhysics>()->GetSceneID();
    pimpl->m_meshResource = Cast<CMesh>(sharedData->GetEditor().GetResource());
	pimpl->m_forcedLodLevel = std::max(0, red::StaticCast<MeshPanelSharedData>(sharedData)->m_proxyUpdateInfo.m_forcedLodLevel);
	
    Internal_SetupMeshForRendering();

	m_resourceMonitor.Bind(pimpl->m_meshResource->GetPath());
	m_resourceMonitor.SetCallback([this](const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData){
		Internal_OnResourceMonitorMessage(path, message, userData);
	});

    return TBaseClass::OnInitialize(sharedData);
}

Bool MeshEditModeClothSimulation::OnActivate(IPreviewEditMode* currentMode)
{
    GetSharedData()->RefreshCompiledClothData();

	auto view = GetSharedData()->GetClothResource(pimpl->m_forcedLodLevel);
	if( view.Empty() )
		return false;

    auto& panels = GetSharedData()->GetPreviewPanels();
    for (auto* panel : panels)
    {
        panel->AttachInputListener(m_inputListener.Get(), false);
    }

	pimpl->m_prevStateData.m_runtimeInfo = physics::cloth::ExtractDefaultRuntimeInfo(view);
	pimpl->m_forcedLodLevel = GetForcedLOD(this);

    Internal_RefreshPhysicsProxy();
	Internal_SetupMeshForRendering();

    pimpl->m_clothMeshProxy->SetVisibility(true);

    return TBaseClass::OnActivate(currentMode);
}

void MeshEditModeClothSimulation::OnDeactivate()
{
    auto& panels = GetSharedData()->GetPreviewPanels();
    for (auto* panel : panels)
    {
        panel->DetachInputListener(m_inputListener.Get());
    }

    pimpl->m_clothMeshProxy->SetVisibility(false);
    physics::DetachProxy(pimpl->m_physicsScene, pimpl->m_clothProxy);
    physics::DestroyProxy(pimpl->m_clothProxy);
}

void MeshEditModeClothSimulation::OnRender(CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo)
{
    TBaseClass::OnRender(frame, cameraInfo);
    pimpl->m_windDirection = cameraInfo.GetRenderCamera().GetCameraForward().AsVector3();

    auto& dd = cameraInfo.GetDebugDrawer();
	rend::FragmentBuilder builder(dd);

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_CLOTH_MESH))
    {
        physics::cloth::visualization::VisualizeClothMesh(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_MOTION_CONSTRAINTS))
    {
        physics::cloth::visualization::VisualizeMotionConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_SEPARATION_CONSTRAINTS))
    {
        physics::cloth::visualization::VisualizeSeparationConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_TETHERS))
    {
        physics::cloth::visualization::VisualizeTethers(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_CONSTRAINTS))
    {
		physics::cloth::visualization::VisualizeConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder, {} );
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_HORIZONTAL_PHASE))
    {
		physics::cloth::visualization::VisualizeConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder, {} );
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_VERTICAL_PHASE))
    {
		physics::cloth::visualization::VisualizeConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder, {} );
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_BEND_PHASE))
    {
        physics::cloth::visualization::VisualizeConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder, {} );
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_SHEAR_PHASE))
    {
        physics::cloth::visualization::VisualizeConstraints(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder, {} );
    }

	if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_CONSTRAINT_STIFFNESS))
    {
        physics::cloth::visualization::VisualizeConstraintStiffness(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_CONSTRAINT_ERROR))
    {
        physics::cloth::visualization::VisualizeConstraintError(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_POSITION_DELTA))
    {
        physics::cloth::visualization::VisualizePositionDelta(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }

    if (dd.GetMask().Test(DF_CLOTH_VISUALIZATION_BOUNDING_BOX))
    {
        physics::cloth::visualization::VisualizeBoundingBox(pimpl->m_clothProxy, Matrix::IDENTITY_CONSTANT, builder);
    }
}

void MeshEditModeClothSimulation::OnUpdate(const UpdateContext& ctx)
{
    TBaseClass::OnUpdate(ctx);

    if (pimpl->m_windBlowing)
    {
        physics::SetClothWindVelocity(pimpl->m_clothProxy, pimpl->m_windDirection * pimpl->m_windStrength);
    }
    else
    {
        physics::SetClothWindVelocity(pimpl->m_clothProxy, Vector3::ZEROS());
    }

    pimpl->m_processor->BuildJobChainAndFlush("ClothEditModeSimulation::OnUpdate", ctx.m_timeDelta, ctx.m_tickInfo);

	if( std::exchange(pimpl->m_recompile, false) )
	{
		GetSharedData()->RefreshCompiledClothData();
		pimpl->m_refresh = true;
	}

    if( std::exchange(pimpl->m_refresh, false) )
    {
		Internal_SetupMeshForRendering();
        Internal_RefreshPhysicsProxy();
    }
}

void MeshEditModeClothSimulation::OnStateChanged(const ClothEditorState& state)
{  
    //Runtime Changes
    {
        physics::ProxyWriter writer(pimpl->m_clothProxy);
        if (pimpl->m_prevStateData.m_runtimeInfo.m_damping != state.m_runtimeInfo.m_damping)
        {
			writer.SetClothDamping(state.m_runtimeInfo.m_damping);
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_drag != state.m_runtimeInfo.m_drag)
        {
			writer.SetClothDrag(state.m_runtimeInfo.m_drag);
        }
		else if( pimpl->m_prevStateData.m_runtimeInfo.m_inertia != state.m_runtimeInfo.m_inertia )
		{
			writer.SetClothInertia(state.m_runtimeInfo.m_inertia);
		}
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_friction != state.m_runtimeInfo.m_friction)
        {
            writer.SetClothFriction(state.m_runtimeInfo.m_friction);
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_numSolverIterations != state.m_runtimeInfo.m_numSolverIterations)
        {
            writer.SetClothSolverIterations(state.m_runtimeInfo.m_numSolverIterations);
        }
		else if (pimpl->m_prevStateData.m_runtimeInfo.m_stiffnessFrequency != state.m_runtimeInfo.m_stiffnessFrequency )
		{
			writer.SetClothStiffnessFrequency(state.m_runtimeInfo.m_stiffnessFrequency);
		}
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_tetherStiffness != state.m_runtimeInfo.m_tetherStiffness)
        {
            writer.SetClothTetherStiffness(state.m_runtimeInfo.m_tetherStiffness);
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_tetherScale != state.m_runtimeInfo.m_tetherScale)
        {
            writer.SetClothTetherScale(state.m_runtimeInfo.m_tetherScale);
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_selfCollisionDistance != state.m_runtimeInfo.m_selfCollisionDistance)
        {
            if (state.m_runtimeInfo.m_enableSelfCollision)
            {
                const Float distance = state.m_runtimeInfo.m_selfCollisionDistance * 0.01f;
                writer.SetClothSelfCollisionDistance(distance);
            }
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_selfCollisionStiffness != state.m_runtimeInfo.m_selfCollisionStiffness)
        {
            if (state.m_runtimeInfo.m_enableSelfCollision)
            {
                writer.SetClothSelfCollisionStiffness(state.m_runtimeInfo.m_selfCollisionStiffness);
            }
        }
        else if (pimpl->m_prevStateData.m_runtimeInfo.m_gravityScale != state.m_runtimeInfo.m_gravityScale)
        {
            writer.SetClothGravity(state.m_runtimeInfo.m_gravity * state.m_runtimeInfo.m_gravityScale);
        }
		else if (pimpl->m_prevStateData.m_runtimeInfo.m_liftCoefficient != state.m_runtimeInfo.m_liftCoefficient)
        {
            writer.SetClothLiftCoefficient(state.m_runtimeInfo.m_liftCoefficient);
        }
		else if (pimpl->m_prevStateData.m_runtimeInfo.m_dragCoefficient != state.m_runtimeInfo.m_dragCoefficient)
        {
            writer.SetClothDragCoefficient(state.m_runtimeInfo.m_dragCoefficient);
        }
		else if (pimpl->m_prevStateData.m_runtimeInfo.m_motionConstraintStiffness != state.m_runtimeInfo.m_motionConstraintStiffness )
		{
			writer.SetClothMotionConstraintStiffness(state.m_runtimeInfo.m_motionConstraintStiffness);
		}

		writer.Commit();
    }

    // Offline Changes
    {
		//Changes that require cloth resource to be recompiled
        pimpl->m_recompile |= pimpl->m_prevStateData.m_bendPhaseData != state.m_bendPhaseData;
        pimpl->m_recompile |= pimpl->m_prevStateData.m_shearPhaseData != state.m_shearPhaseData;
        pimpl->m_recompile |= pimpl->m_prevStateData.m_horizontalPhaseData != state.m_horizontalPhaseData;
        pimpl->m_recompile |= pimpl->m_prevStateData.m_verticalPhaseData != state.m_verticalPhaseData;
		pimpl->m_recompile |= pimpl->m_prevStateData.m_useGeodesicTether != state.m_useGeodesicTether;
		pimpl->m_recompile |= pimpl->m_prevStateData.m_quadifyMesh != state.m_quadifyMesh;

		//Changes which only require use to recreate the physics proxy
		pimpl->m_refresh |= pimpl->m_forcedLodLevel != GetForcedLOD(this);
		pimpl->m_refresh |= pimpl->m_prevStateData.m_runtimeInfo.m_enableSelfCollision != state.m_runtimeInfo.m_enableSelfCollision;

		pimpl->m_forcedLodLevel = GetForcedLOD(this);
    }

    pimpl->m_prevStateData = state;
}

void MeshEditModeClothSimulation::OnSetupInterop(::interop::utils::Object& op)
{
    op.RegisterRangedValue<Float>("windStrength", 1.0, 100.0,
                                   [this](interop::IContext& ctx, Float& out) { out = pimpl->m_windStrength; },
                                   [this](interop::IContext& ctx, const Float& in) { pimpl->m_windStrength = in; });
}

void MeshEditModeClothSimulation::Internal_SetupMeshForRendering()
{
    pimpl->m_clothMeshProxy = nullptr;
	world::RuntimeSystemRendering& system = pimpl->m_runtimeScene->GetSystemRef<world::RuntimeSystemRendering>();

    auto appearance = pimpl->m_meshResource->GetDefaultAppearance();

    RenderProxyMeshComponentInitData meshInfo;
    meshInfo.m_isCharacterShadowFallback = false;
    meshInfo.m_hasMeshResource = true;

    meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
    meshInfo.m_renderMesh = GRender->UploadClothMesh(pimpl->m_meshResource->GetRenderResource());
    meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
    meshInfo.m_setupsNum = 1;
	meshInfo.m_isCloth = true;
	meshInfo.m_numClothLODs = mesh::DataViewRO(*pimpl->m_meshResource).GetNumLODs();

    meshInfo.m_componentFriendlyName = "<clothMesh>";
    meshInfo.m_forcedLODLevel = pimpl->m_forcedLodLevel;
    meshInfo.m_isCastingGlobalShadows = pimpl->m_meshResource->GetCastGlobalShadows();
    meshInfo.m_isCastingLocalShadows = pimpl->m_meshResource->GetCastLocalShadows();
    meshInfo.m_isTwoSided = false;
    meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

    meshInfo.m_isVisible = true;
    meshInfo.m_isDynamicGeometryComponent = true;
    meshInfo.m_isForceNoAutohide = true;
    meshInfo.m_autoHideDistanceNear = 0.0f;
    meshInfo.m_autoHideDistanceFar = pimpl->m_meshResource->GetAutoHideDistance();
    meshInfo.m_renderingPlane = RPl_Scene;
	meshInfo.m_objectTypeID = pimpl->m_meshResource->GetRenderObjectType();

    meshInfo.m_type = RPT_Mesh;
    meshInfo.m_boundingBox = pimpl->m_meshResource->GetBoundingBox();

    RenderProxyInitInfo initInfo;
    initInfo.m_componentData = &meshInfo;

    pimpl->m_clothMeshProxy = system.CreateAndRegisterRenderProxy(initInfo, true);
}

void MeshEditModeClothSimulation::Internal_RefreshPhysicsProxy()
{
    if( physics::IsAlive(pimpl->m_clothProxy) )
    {
        physics::DetachProxy(pimpl->m_physicsScene, pimpl->m_clothProxy);
        physics::DestroyProxy(pimpl->m_clothProxy);
    }

	auto view = GetSharedData()->GetClothResource(pimpl->m_forcedLodLevel);

    physics::ClothDesc desc(view);
    desc.m_editor = true;
    desc.m_pullFunction = [lod=pimpl->m_forcedLodLevel, this](red::ArraySpan<const Vector4> positions, Box bounds){ Internal_ClothProxyPull(lod, positions, bounds); };
    desc.m_pushFunction = [lod=pimpl->m_forcedLodLevel, this](red::ArraySpan<const Vector3>& positions, red::ArraySpan<const Vector3>& normals){ return Internal_ClothProxyPush(lod, positions,normals); };
	desc.m_runtimeInfo = pimpl->m_prevStateData.m_runtimeInfo;

    pimpl->m_clothProxy = physics::CreateProxy(desc);
    physics::AttachProxy(pimpl->m_physicsScene, pimpl->m_clothProxy);
}

Bool MeshEditModeClothSimulation::Internal_ClothProxyPush(Uint8 lod, red::ArraySpan<const Vector3>& positions, red::ArraySpan<const Vector3>& normals)
{
    mesh::DataViewRO view(*pimpl->m_meshResource);
    positions = mesh::MakeSpan<const Vector3>(view, lod, MST_Position_3F);
    normals = mesh::MakeSpan<const Vector3>(view, lod, MST_Normal_3F);
    return true;
}

void MeshEditModeClothSimulation::Internal_ClothProxyPull(Uint8 lod, red::ArraySpan<const Vector4> positions, Box bounds)
{
	red::DynArray<Vector3> finalPositions{ red::PoolDoubleBufferedFrame() };
	finalPositions.Reserve(positions.Size());

	for(Uint32 i : positions.Indices())
	{
		finalPositions.EmplaceBack( positions[i].X, positions[i].Y, positions[i].Z );
	}

    pimpl->m_clothMeshProxy->UpdateClothMeshData(lod, std::move(finalPositions), bounds);
}

void MeshEditModeClothSimulation::Internal_OnResourceMonitorMessage(const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData)
{
	if( message == RED_NAME_CONSTEXPR_NOREG( "onSave" ) && path == pimpl->m_meshResource->GetPath() )
	{
		pimpl->m_refresh = true;
	}
}

}
