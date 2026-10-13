#include "build.h"
#include "backendMeshPreviewEditMode_ClothPaint.h"
#include "backendMeshPreviewPanel.h"
#include "backendMeshBrush.h"
#include "meshDataView.h"
#include "meshDataBuilder.h"

#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../backendMaterial/include/bitmapTextureCooker.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/resourceMesh/include/meshUtils.h"

#include "../../../common/physics/include/physXNatives.h"

#include "../../../common/redReflection/include/singleChannelCurve2.h"
#include "../../../common/generalEditorBackend/include/clothEditorInterop.h"

namespace tools
{
	struct MeshEditModeClothPaintChunkInfo
	{
		red::DynArray<DebugVertex> clothVertices;
        red::DynArray<Vector3> clothNormals;
        red::DynArray<Uint16> clothIndices;

        red::BlobSpan maxDistanceChannel;
		red::BlobSpan maxDistanceExtChannel;
        red::BlobSpan backstopDistanceChannel;
		red::BlobSpan backstopRadiusChannel;
        red::BlobSpan selfCollisionChannel;
	};

    struct MeshEditModeClothPaint::impl
    {
        RED_USE_MEMORY_POOL(red::PoolBackend);

        TRenderPtr<IRenderResource> clothMesh = nullptr;
        red::DynArray<MeshEditModeClothPaintChunkInfo> m_chunkInfo;
        red::DynArray<tools::ClothEditorState> m_prevState;
        THandle<CMesh> m_mesh = nullptr;

        MeshEditModePaintBrush* activeBrush = nullptr;
		Int32 m_forcedLodLevel = 0;
        Bool m_hasChanged = false;
    };

    MeshEditModeClothPaint::MeshEditModeClothPaint()
        : MeshPreviewEditMode("ClothEditModePaint")
        , pimpl(red::CreateUniquePtr<MeshEditModeClothPaint::impl>())
    {}

	MeshEditModeClothPaint::~MeshEditModeClothPaint() = default;

    Color MeshEditModeClothPaint::GetDisplayColor() const
    {
        return Color::DARK_BLUE();
    }

    Bool MeshEditModeClothPaint::OnInitialize(const PreviewPanelSharedDataPtr& sharedData)
    {
        MeshPanelSharedData* ptr = static_cast<MeshPanelSharedData*>(sharedData.Get());
        pimpl->m_mesh = Cast<CMesh>(ptr->GetEditor().GetResource());
		pimpl->m_forcedLodLevel = std::max(ptr->m_proxyUpdateInfo.m_forcedLodLevel, 0);

        mesh::DataViewRO view(*pimpl->m_mesh);
		const auto num_chunks = view.GetNumChunks();
		pimpl->m_prevState.Resize(num_chunks);
		pimpl->m_chunkInfo.Resize(num_chunks);
		for( auto chunk = 0; chunk < num_chunks; ++chunk )
		{
			auto& chunk_info = pimpl->m_chunkInfo[chunk];
			auto vertices3 = mesh::MakeSpan<Vector3>(view, chunk, MST_Position_3F);
			auto indices = mesh::MakeSpan<Uint16>(view, chunk, MST_Index_U16);
			auto normals = mesh::MakeSpan<Vector3>(view, chunk, MST_Normal_3F);

			chunk_info.clothIndices.PushBack(indices);
			chunk_info.clothNormals.PushBack(normals);

			chunk_info.clothVertices.Resize(vertices3.Size());
			chunk_info.maxDistanceChannel = ptr->GetMaxDistanceChannelSpan(chunk);
			chunk_info.maxDistanceExtChannel = ptr->GetMaxDistanceExtChannelSpan(chunk);
			chunk_info.backstopDistanceChannel = ptr->GetBackstopDistanceChannelSpan(chunk);
			chunk_info.backstopRadiusChannel = ptr->GetBackstopRadiusChannelSpan(chunk);
			chunk_info.selfCollisionChannel = ptr->GetSelfCollisionChannelSpan(chunk);
			red::alg::Transform(vertices3, begin(chunk_info.clothVertices), [](const auto& v3) -> DebugVertex { return { {v3, 1.f}, Vector4::ZEROS() }; });

			curve::TSingleChannelCurve<Color> curve;
			curve::TSingleChannelCurveBuilder<Color> builder(curve);
			builder.InitLinearCurve(2);
			builder.SetInitialKeyFrame_Linear(Color::BLACK(), Color::RED());

			m_maxDistanceBrush.EmplaceBack(red::CreateUniquePtr<MeshEditModePaintBrush>(ptr, curve, &chunk_info.clothVertices, chunk_info.clothNormals, chunk_info.maxDistanceChannel, false));
			m_maxDistanceExtBrush.EmplaceBack(red::CreateUniquePtr<MeshEditModePaintBrush>(ptr, curve, &chunk_info.clothVertices, chunk_info.clothNormals, chunk_info.maxDistanceExtChannel, false));
			m_backstopDistanceBrush.EmplaceBack(red::CreateUniquePtr<MeshEditModePaintBrush>(ptr, curve, &chunk_info.clothVertices, chunk_info.clothNormals, chunk_info.backstopDistanceChannel, true));
			m_backstopRadiusBrush.EmplaceBack(red::CreateUniquePtr<MeshEditModePaintBrush>(ptr, curve, &chunk_info.clothVertices, chunk_info.clothNormals, chunk_info.backstopRadiusChannel, false));
			m_selfCollisionBrush.EmplaceBack(red::CreateUniquePtr<MeshEditModePaintBrush>(ptr, curve, &chunk_info.clothVertices, chunk_info.clothNormals, chunk_info.selfCollisionChannel, false));
		}

        return TBaseClass::OnInitialize(sharedData);
    }

    Bool MeshEditModeClothPaint::OnActivate( IPreviewEditMode* currentMode )
    {
        MeshPanelSharedData* shared_data = GetSharedData();
        auto& panels = shared_data->GetPreviewPanels();
		pimpl->m_forcedLodLevel = std::max(0, shared_data->m_proxyUpdateInfo.m_forcedLodLevel);

        for(auto* panel : panels )
        {
            auto* meshPanel = static_cast<MeshPreviewPanel*>(panel);

            meshPanel->GetCamera()->MapRotateButton(RenderingWindowCamera::EMouseMode::LeftDown);
            meshPanel->GetCamera()->MapZoomButton(RenderingWindowCamera::EMouseMode::RightDown);
            meshPanel->GetCamera()->OrbitLockRotateDuringZoom(true);
            meshPanel->EnableOrbitCamera(true);
            meshPanel->ZoomOut(false);

            meshPanel->AttachInputListener(m_maxDistanceBrush[pimpl->m_forcedLodLevel].Get(), false);
            pimpl->activeBrush = m_maxDistanceBrush[pimpl->m_forcedLodLevel].Get();
            pimpl->activeBrush->Refresh(pimpl->m_prevState[pimpl->m_forcedLodLevel].m_maxDistance);
        }

        return TBaseClass::OnActivate(currentMode);
    }

    void MeshEditModeClothPaint::OnDeactivate()
    {
        for(auto* panel : GetSharedData()->GetPreviewPanels() )
        {
            panel->DetachInputListener(pimpl->activeBrush);
        }

        TBaseClass::OnDeactivate();
    }

	void MeshEditModeClothPaint::OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& cameraInfo )
    {
		auto info = pimpl->m_chunkInfo[pimpl->m_forcedLodLevel];

        red::DynArray<Uint32> indices(info.clothIndices.Size(), red::PoolDoubleBufferedFrame());
		red::alg::Transform(info.clothIndices, begin(indices), [](auto i){ return static_cast<Uint32>(i); });

		pimpl->clothMesh = GRender->UploadDebugMesh( info.clothVertices, indices );

		rend::FragmentBuilder builder(cameraInfo.GetDebugDrawer());
        builder.EnableTransparency(true);
		builder.BindTransform( Matrix::IDENTITY() );
		builder.AddSolidMesh( pimpl->clothMesh );
		builder.AddAxes( Matrix::IDENTITY() );

        builder.SetColor(Color::GRAY());
        builder.EnableTransparency(false);
        builder.AddSolidMesh( pimpl->clothMesh, rend::FragmentRenderMode::Wireframe );

        TBaseClass::OnRender(frame, cameraInfo);
    }

    void MeshEditModeClothPaint::OnStateChanged(const ClothEditorState& state)
    {
		Int32 forcedLodLevel = std::max(0, GetSharedData()->m_proxyUpdateInfo.m_forcedLodLevel);
		if( pimpl->m_forcedLodLevel != forcedLodLevel )
		{
			pimpl->m_forcedLodLevel = forcedLodLevel;
			ChangeBrush(state.m_activeBrush, pimpl->m_forcedLodLevel);
		}
		else
		{
			const Uint16 chunk = pimpl->m_forcedLodLevel;
			if(pimpl->m_prevState[chunk].m_activeBrush != state.m_activeBrush)
			{
				ChangeBrush(state.m_activeBrush,chunk);
			}
			else if(pimpl->m_prevState[chunk].m_maxDistance != state.m_maxDistance)
			{
				RefreshMaxDistance(state.m_maxDistance,chunk);
			}
			else if(pimpl->m_prevState[chunk].m_maxDistanceExt != state.m_maxDistanceExt)
			{
				RefreshMaxDistanceExt(state.m_maxDistanceExt,chunk);
			}
			else if(pimpl->m_prevState[chunk].m_backstopDistance != state.m_backstopDistance)
			{
				RefreshBackstopDistance(state.m_backstopDistance,chunk);
			}
			else if(pimpl->m_prevState[chunk].m_backstopRadius != state.m_backstopRadius)
			{
				RefreshBackstopRadius(state.m_backstopRadius,chunk);
			}
			else if(pimpl->m_prevState[chunk].m_backstopRadius != state.m_backstopRadius)
			{
				RefreshSelfCollision(state.m_selfCollision,chunk);
			}

			pimpl->m_prevState[chunk] = state;
		}
    }

    void MeshEditModeClothPaint::Internal_AttachInputListener(IPreviewInputListener* listener)
    {
        auto& panels = GetSharedData()->GetPreviewPanels();
        for(auto* panel : panels)
        {
            panel->AttachInputListener(listener, false);
        }
    }

    void MeshEditModeClothPaint::Internal_DetachInputListener(IPreviewInputListener* listener)
    {
        auto& panels = GetSharedData()->GetPreviewPanels();
        for(auto* panel : panels)
        {
            panel->DetachInputListener(listener);
        }
    }

    void MeshEditModeClothPaint::Internal_ChangeBrush(MeshEditModePaintBrush* brush, const ClothEditorBrushInfo& prevInfo)
    {
        if( brush != pimpl->activeBrush )
        {
            Internal_DetachInputListener(pimpl->activeBrush);
            Internal_AttachInputListener(brush);
            pimpl->activeBrush = brush;
            pimpl->activeBrush->Refresh(prevInfo);
        }
    }

    void MeshEditModeClothPaint::RefreshMaxDistance(const tools::ClothEditorBrushInfo& info, Uint16 chunk)
    {
        m_maxDistanceBrush[chunk]->Refresh(info);
    }

	void MeshEditModeClothPaint::RefreshMaxDistanceExt(const tools::ClothEditorBrushInfo& info, Uint16 chunk)
	{
		m_maxDistanceExtBrush[chunk]->Refresh(info);
	}

    void MeshEditModeClothPaint::RefreshBackstopDistance(const tools::ClothEditorBrushInfo& info, Uint16 chunk)
    {
        m_backstopDistanceBrush[chunk]->Refresh(info);
    }

	void MeshEditModeClothPaint::RefreshBackstopRadius(const tools::ClothEditorBrushInfo& info, Uint16 chunk)
	{
		m_backstopRadiusBrush[chunk]->Refresh(info);
	}

    void MeshEditModeClothPaint::RefreshSelfCollision(const tools::ClothEditorBrushInfo& info, Uint16 chunk)
    {
        m_selfCollisionBrush[chunk]->Refresh(info);
    }

    void MeshEditModeClothPaint::ChangeBrush(Int32 brushIndex, Uint16 chunk)
    {
        switch(brushIndex)
        {
            case 0: {
                Internal_ChangeBrush(m_maxDistanceBrush[chunk].Get(), pimpl->m_prevState[chunk].m_maxDistance);
            } break;

			case 1: {
                Internal_ChangeBrush(m_maxDistanceExtBrush[chunk].Get(), pimpl->m_prevState[chunk].m_maxDistanceExt);
            } break;

            case 2: {
                Internal_ChangeBrush(m_backstopDistanceBrush[chunk].Get(), pimpl->m_prevState[chunk].m_backstopDistance);
            } break;

			case 3: {
                Internal_ChangeBrush(m_backstopRadiusBrush[chunk].Get(), pimpl->m_prevState[chunk].m_backstopRadius);
            } break;

            case 4: {
                Internal_ChangeBrush(m_selfCollisionBrush[chunk].Get(), pimpl->m_prevState[chunk].m_selfCollision);
            } break;
        }
    }
}